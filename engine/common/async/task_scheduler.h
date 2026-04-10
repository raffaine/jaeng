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

namespace jaeng::async {

class TaskScheduler;

// Forward declaration of the scheduler singleton getter
TaskScheduler* get_current_scheduler();

template<typename T>
class Future {
public:
    Future() = default;
    explicit Future(std::future<T> fut) : future_(std::move(fut)) {}

    Future(Future&& other) noexcept = default;
    Future& operator=(Future&& other) noexcept = default;

    T get() {
        return future_.get();
    }

    void wait() const {
        future_.wait();
    }

    bool valid() const noexcept {
        return future_.valid();
    }

    // .then() executes the callback when the future completes, returning a new Future.
    template<typename F>
    auto then(F&& func);

    // .thenSync() executes the callback on the Main/OS thread when the future completes.
    template<typename F>
    auto thenSync(F&& func);

private:
    std::future<T> future_;
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

    void initialize(uint32_t workerCount = 0);
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

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::lock_guard<std::mutex> lock(asyncMutex_);
            if (stop_) throw std::runtime_error("TaskScheduler is stopped");
            asyncQueue_.emplace_back([task]() { (*task)(); });
        }
        asyncCv_.notify_one();
        return Future<return_type>(std::move(res));
    }

    // Enqueue a task to be executed on the Main/OS thread
    template<typename F, typename... Args>
    auto enqueue_sync(F&& f, Args&&... args) -> Future<std::invoke_result_t<F, Args...>> {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::lock_guard<std::mutex> lock(syncMutex_);
            if (stop_) throw std::runtime_error("TaskScheduler is stopped");
            syncQueue_.emplace_back([task]() { (*task)(); });
        }
        return Future<return_type>(std::move(res));
    }

    // Called by the Main/OS thread to process its mailbox
    // Returns true if any tasks were processed
    bool process_main_thread_tasks();

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    
    // Async Queue (MPMC)
    std::deque<TaskFn> asyncQueue_;
    std::mutex asyncMutex_;
    std::condition_variable asyncCv_;

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
        if constexpr (std::is_void_v<T>) {
            using return_type = std::invoke_result_t<F>;
            auto shared_fut = std::make_shared<std::future<T>>(std::move(future_));
            return get_current_scheduler()->enqueue_async([shared_fut = std::move(shared_fut), func = std::forward<F>(func)]() mutable -> return_type {
                shared_fut->wait();
                return func();
            });
        } else {
            using return_type = std::invoke_result_t<F, T>;
            auto shared_fut = std::make_shared<std::future<T>>(std::move(future_));
            return get_current_scheduler()->enqueue_async([shared_fut = std::move(shared_fut), func = std::forward<F>(func)]() mutable -> return_type {
                return func(shared_fut->get());
            });
        }
    }

    template<typename T>
    template<typename F>
    auto Future<T>::thenSync(F&& func) {
        if constexpr (std::is_void_v<T>) {
            using return_type = std::invoke_result_t<F>;
            auto shared_fut = std::make_shared<std::future<T>>(std::move(future_));
            // We first enqueue an async task to wait, then that task enqueues the sync callback.
            // This prevents blocking the main thread while waiting for the future to resolve.
            return get_current_scheduler()->enqueue_async([shared_fut = std::move(shared_fut), func = std::forward<F>(func)]() mutable {
                shared_fut->wait();
                return get_current_scheduler()->enqueue_sync([func = std::move(func)]() mutable -> return_type {
                    return func();
                });
            }).then([](auto syncFuture) { return syncFuture.get(); }); // Unwrap the nested future
        } else {
            using return_type = std::invoke_result_t<F, T>;
            auto shared_fut = std::make_shared<std::future<T>>(std::move(future_));
            return get_current_scheduler()->enqueue_async([shared_fut = std::move(shared_fut), func = std::forward<F>(func)]() mutable {
                auto val = shared_fut->get();
                return get_current_scheduler()->enqueue_sync([val = std::move(val), func = std::move(func)]() mutable -> return_type {
                    return func(std::move(val));
                });
            }).then([](auto syncFuture) { return syncFuture.get(); }); // Unwrap the nested future
        }
    }
} // namespace jaeng::async
