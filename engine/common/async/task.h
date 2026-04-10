#pragma once

#include <coroutine>
#include <exception>
#include <utility>
#include <optional>

namespace jaeng::async {

template<typename T>
struct Task {
    struct promise_type {
        std::optional<T> result;
        std::exception_ptr exception;
        std::coroutine_handle<> continuation;

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        
        struct final_awaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                if (h.promise().continuation) {
                    return h.promise().continuation;
                }
                return std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };

        final_awaiter final_suspend() noexcept { return {}; }

        void unhandled_exception() noexcept {
            exception = std::current_exception();
        }

        void return_value(T value) noexcept {
            result = std::move(value);
        }
    };

    std::coroutine_handle<promise_type> handle;

    explicit Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    ~Task() {
        if (handle) handle.destroy();
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = std::exchange(other.handle, nullptr);
        }
        return *this;
    }

    struct awaiter {
        std::coroutine_handle<promise_type> handle;

        bool await_ready() noexcept {
            return !handle || handle.done();
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
            handle.promise().continuation = continuation;
            return handle;
        }

        T await_resume() {
            if (handle.promise().exception) {
                std::rethrow_exception(handle.promise().exception);
            }
            return std::move(*handle.promise().result);
        }
    };

    awaiter operator co_await() noexcept {
        return awaiter{handle};
    }
};

template<>
struct Task<void> {
    struct promise_type {
        std::exception_ptr exception;
        std::coroutine_handle<> continuation;

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        
        struct final_awaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                if (h.promise().continuation) {
                    return h.promise().continuation;
                }
                return std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };

        final_awaiter final_suspend() noexcept { return {}; }

        void unhandled_exception() noexcept {
            exception = std::current_exception();
        }

        void return_void() noexcept {}
    };

    std::coroutine_handle<promise_type> handle;

    explicit Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    ~Task() {
        if (handle) handle.destroy();
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = std::exchange(other.handle, nullptr);
        }
        return *this;
    }

    struct awaiter {
        std::coroutine_handle<promise_type> handle;

        bool await_ready() noexcept {
            return !handle || handle.done();
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
            handle.promise().continuation = continuation;
            return handle;
        }

        void await_resume() {
            if (handle.promise().exception) {
                std::rethrow_exception(handle.promise().exception);
            }
        }
    };

    awaiter operator co_await() noexcept {
        return awaiter{handle};
    }
};

struct FireAndForget {
    struct promise_type {
        FireAndForget get_return_object() { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {
            // Ideally we log this, but we can't let it escape the thread
            try {
                std::rethrow_exception(std::current_exception());
            } catch (const std::exception& e) {
                // Log exception if logging is available here, or just swallow to prevent crash
            } catch (...) {
            }
        }
    };
};

} // namespace jaeng::async
