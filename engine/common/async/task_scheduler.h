#pragma once

#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <future>
#include <memory>
#include "task.h"
#include "common/logging.h"

namespace jaeng::async {

class TaskScheduler;

// Forward declaration of the scheduler singleton getter
TaskScheduler* get_current_scheduler();

template<typename T>
class Future {
public:
    struct SharedState {
        std::mutex mtx;
        std::condition_variable cv;
        std::optional<T> value;
        std::vector<std::function<void()>> callbacks;
        bool ready = false;

        void set_value(T val) {
            std::vector<std::function<void()>> to_call;
            {
                std::lock_guard<std::mutex> lock(mtx);
                value = std::move(val);
                ready = true;
                to_call = std::move(callbacks);
            }
            cv.notify_all();
            for (auto& cb : to_call) cb();
        }

        T get() {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [this] { return ready; });
            return std::move(*value);
        }

        void then(std::function<void()> cb) {
            bool run_now = false;
            {
                std::lock_guard<std::mutex> lock(mtx);
                if (ready) {
                    run_now = true;
                } else {
                    callbacks.push_back(std::move(cb));
                }
            }
            if (run_now) cb();
        }
    };

    Future() : shared_(std::make_shared<SharedState>()) {}
    explicit Future(std::shared_ptr<SharedState> shared) : shared_(std::move(shared)) {}

    T get() { return shared_->get(); }
    void wait() const { 
        std::unique_lock<std::mutex> lock(shared_->mtx);
        shared_->cv.wait(lock, [this] { return shared_->ready; });
    }
    bool valid() const noexcept { return shared_ != nullptr; }
    std::shared_ptr<SharedState> get_shared_state() { return shared_; }

    template<typename F>
    auto then(F&& func);

    template<typename F>
    auto thenSync(F&& func);

    auto operator co_await() noexcept;

private:
    std::shared_ptr<SharedState> shared_;
};

// Void specialization
template<>
class Future<void> {
public:
    struct SharedState {
        std::mutex mtx;
        std::condition_variable cv;
        std::vector<std::function<void()>> callbacks;
        bool ready = false;

        void set_value() {
            std::vector<std::function<void()>> to_call;
            {
                std::lock_guard<std::mutex> lock(mtx);
                ready = true;
                to_call = std::move(callbacks);
            }
            cv.notify_all();
            for (auto& cb : to_call) cb();
        }

        void get() {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [this] { return ready; });
        }

        void then(std::function<void()> cb) {
            bool run_now = false;
            {
                std::lock_guard<std::mutex> lock(mtx);
                if (ready) {
                    run_now = true;
                } else {
                    callbacks.push_back(std::move(cb));
                }
            }
            if (run_now) cb();
        }
    };

    Future() : shared_(std::make_shared<SharedState>()) {}
    explicit Future(std::shared_ptr<SharedState> shared) : shared_(std::move(shared)) {}

    void get() { shared_->get(); }
    void wait() const { shared_->get(); }
    bool valid() const noexcept { return shared_ != nullptr; }
    std::shared_ptr<SharedState> get_shared_state() { return shared_; }

    template<typename F>
    auto then(F&& func);

    template<typename F>
    auto thenSync(F&& func);

    auto operator co_await() noexcept;

private:
    std::shared_ptr<SharedState> shared_;
};

struct DetachedTask {
    struct promise_type {
        DetachedTask get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void unhandled_exception() { std::terminate(); }
        void return_void() {}
    };
};

template<typename T>
DetachedTask spawn_task_impl(Task<T> task) {
    co_await task;
}

// A lightweight type-erased task
using TaskFn = std::function<void()>;

class TaskScheduler {
public:
    TaskScheduler();
    ~TaskScheduler();

    void initialize(uint32_t workerCount = 0, uint32_t ioWorkerCount = 1);
    void shutdown();

    // Spawns a coroutine as a fire-and-forget task
    template<typename T>
    void spawn(Task<T> task) {
        auto shared_task = std::make_shared<Task<T>>(std::move(task));
        enqueue_async([shared_task]() mutable {
            spawn_task_impl(std::move(*shared_task));
        });
    }

    // Enqueue a task to be executed on any worker thread
    template<typename F, typename... Args>
    auto enqueue_async(F&& f, Args&&... args) -> Future<std::invoke_result_t<F, Args...>> {
        using return_type = std::invoke_result_t<F, Args...>;
        auto shared = std::make_shared<typename Future<return_type>::SharedState>();

        {
            std::lock_guard<std::mutex> lock(asyncMutex_);
            if (stop_) throw std::runtime_error("TaskScheduler is stopped");
            asyncQueue_.emplace_back([f = std::forward<F>(f), args = std::make_tuple(std::forward<Args>(args)...), shared]() mutable {
                if constexpr (std::is_void_v<return_type>) {
                    std::apply(f, std::move(args));
                    shared->set_value();
                } else {
                    shared->set_value(std::apply(f, std::move(args)));
                }
            });
        }
        asyncCv_.notify_one();
        return Future<return_type>(shared);
    }

    // Enqueue an IO task to be executed on dedicated IO thread(s)
    template<typename F, typename... Args>
    auto enqueue_io(F&& f, Args&&... args) -> Future<std::invoke_result_t<F, Args...>> {
        using return_type = std::invoke_result_t<F, Args...>;
        auto shared = std::make_shared<typename Future<return_type>::SharedState>();

        {
            std::lock_guard<std::mutex> lock(ioMutex_);
            if (stop_) throw std::runtime_error("TaskScheduler is stopped");
            ioQueue_.emplace_back([f = std::forward<F>(f), args = std::make_tuple(std::forward<Args>(args)...), shared]() mutable {
                if constexpr (std::is_void_v<return_type>) {
                    std::apply(f, std::move(args));
                    shared->set_value();
                } else {
                    shared->set_value(std::apply(f, std::move(args)));
                }
            });
        }
        ioCv_.notify_one();
        return Future<return_type>(shared);
    }

    // Enqueue a task to be executed on the Main/OS thread
    template<typename F, typename... Args>
    auto enqueue_sync(F&& f, Args&&... args) -> Future<std::invoke_result_t<F, Args...>> {
        using return_type = std::invoke_result_t<F, Args...>;
        auto shared = std::make_shared<typename Future<return_type>::SharedState>();

        {
            std::lock_guard<std::mutex> lock(syncMutex_);
            if (stop_) throw std::runtime_error("TaskScheduler is stopped");
            syncQueue_.emplace_back([f = std::forward<F>(f), args = std::make_tuple(std::forward<Args>(args)...), shared]() mutable {
                if constexpr (std::is_void_v<return_type>) {
                    std::apply(f, std::move(args));
                    shared->set_value();
                } else {
                    shared->set_value(std::apply(f, std::move(args)));
                }
            });
        }
        return Future<return_type>(shared);
    }

    // Returns true if any tasks were processed
    bool process_main_thread_tasks();

    bool is_worker_thread() const;
    bool is_io_thread() const;

private:
    void worker_loop();
    void io_worker_loop();

    std::vector<std::thread> workers_;
    std::vector<std::thread> ioWorkers_;
    
    // Async Queue (MPMC)
    std::deque<TaskFn> asyncQueue_;
    std::mutex asyncMutex_;
    std::condition_variable asyncCv_;

    // IO Queue (MPMC)
    std::deque<TaskFn> ioQueue_;
    std::mutex ioMutex_;
    std::condition_variable ioCv_;

    // Sync Mailbox (MPSC)
    std::deque<TaskFn> syncQueue_;
    std::mutex syncMutex_;

    std::atomic<bool> stop_ = false;
};

} // namespace jaeng::async

// Implement Future::then here because it requires TaskScheduler to be fully defined
namespace jaeng::async {
    template<typename T>
    template<typename F>
    auto Future<T>::then(F&& func) {
        using return_type = std::invoke_result_t<F, T>;
        auto next_shared = std::make_shared<typename Future<return_type>::SharedState>();
        
        shared_->then([func = std::forward<F>(func), shared = shared_, next_shared]() mutable {
            auto* scheduler = get_current_scheduler();
            auto task = [func = std::move(func), shared, next_shared]() mutable {
                auto val = shared->get();
                if constexpr (std::is_void_v<return_type>) {
                    func(std::move(val));
                    next_shared->set_value();
                } else {
                    next_shared->set_value(func(std::move(val)));
                }
            };

            if (scheduler) {
                scheduler->enqueue_async(std::move(task));
            } else {
                task();
            }
        });
        return Future<return_type>(next_shared);
    }

    template<typename F>
    auto Future<void>::then(F&& func) {
        using return_type = std::invoke_result_t<F>;
        auto next_shared = std::make_shared<typename Future<return_type>::SharedState>();
        
        shared_->then([func = std::forward<F>(func), next_shared]() mutable {
            auto* scheduler = get_current_scheduler();
            auto task = [func = std::move(func), next_shared]() mutable {
                if constexpr (std::is_void_v<return_type>) {
                    func();
                    next_shared->set_value();
                } else {
                    next_shared->set_value(func());
                }
            };

            if (scheduler) {
                scheduler->enqueue_async(std::move(task));
            } else {
                task();
            }
        });
        return Future<return_type>(next_shared);
    }

    template<typename T>
    template<typename F>
    auto Future<T>::thenSync(F&& func) {
        using return_type = std::invoke_result_t<F, T>;
        auto next_shared = std::make_shared<typename Future<return_type>::SharedState>();
        
        shared_->then([func = std::forward<F>(func), shared = shared_, next_shared]() mutable {
            auto* scheduler = get_current_scheduler();
            auto task = [func = std::move(func), shared, next_shared]() mutable {
                auto val = shared->get();
                if constexpr (std::is_void_v<return_type>) {
                    func(std::move(val));
                    next_shared->set_value();
                } else {
                    next_shared->set_value(func(std::move(val)));
                }
            };

            if (scheduler) {
                scheduler->enqueue_sync(std::move(task));
            } else {
                task();
            }
        });
        return Future<return_type>(next_shared);
    }

    template<typename F>
    auto Future<void>::thenSync(F&& func) {
        using return_type = std::invoke_result_t<F>;
        auto next_shared = std::make_shared<typename Future<return_type>::SharedState>();
        
        shared_->then([func = std::forward<F>(func), next_shared]() mutable {
            auto* scheduler = get_current_scheduler();
            auto task = [func = std::move(func), next_shared]() mutable {
                if constexpr (std::is_void_v<return_type>) {
                    func();
                    next_shared->set_value();
                } else {
                    next_shared->set_value(func());
                }
            };

            if (scheduler) {
                scheduler->enqueue_sync(std::move(task));
            } else {
                task();
            }
        });
        return Future<return_type>(next_shared);
    }

    template<typename T>
    auto Future<T>::operator co_await() noexcept {
        struct awaiter {
            std::shared_ptr<typename Future<T>::SharedState> shared;

            bool await_ready() const noexcept {
                std::lock_guard<std::mutex> lock(shared->mtx);
                return shared->ready;
            }

            void await_suspend(std::coroutine_handle<> h) noexcept {
                shared->then([h]() mutable {
                    auto* scheduler = get_current_scheduler();
                    if (scheduler) {
                        scheduler->enqueue_async([h]() { h.resume(); });
                    } else {
                        h.resume();
                    }
                });
            }

            T await_resume() {
                return shared->get();
            }
        };
        return awaiter{shared_};
    }

    inline auto Future<void>::operator co_await() noexcept {
        struct awaiter {
            std::shared_ptr<Future<void>::SharedState> shared;

            bool await_ready() const noexcept {
                std::lock_guard<std::mutex> lock(shared->mtx);
                return shared->ready;
            }

            void await_suspend(std::coroutine_handle<> h) noexcept {
                shared->then([h]() mutable {
                    auto* scheduler = get_current_scheduler();
                    if (scheduler) {
                        scheduler->enqueue_async([h]() { h.resume(); });
                    } else {
                        h.resume();
                    }
                });
            }

            void await_resume() {
                shared->get();
            }
        };
        return awaiter{shared_};
    }
} // namespace jaeng::async
