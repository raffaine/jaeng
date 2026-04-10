#pragma once

#include "task_scheduler.h"
#include <coroutine>

namespace jaeng::async {

// Internal helper to get/set the current scheduler for awaiters
// This is set by the Application during startup
void set_current_scheduler(TaskScheduler* scheduler);
TaskScheduler* get_current_scheduler();

struct SwitchToWorker {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) const noexcept {
        if (auto* s = get_current_scheduler()) {
            s->enqueue_async([h]() { h.resume(); });
        } else {
            // Fallback: resume immediately if no scheduler (should not happen in app)
            h.resume();
        }
    }
    void await_resume() const noexcept {}
};

struct SwitchToMainThread {
    bool await_ready() const noexcept {
        // We could potentially check if we are already on the main thread here
        return false; 
    }
    void await_suspend(std::coroutine_handle<> h) const noexcept {
        if (auto* s = get_current_scheduler()) {
            s->enqueue_sync([h]() { h.resume(); });
        } else {
            h.resume();
        }
    }
    void await_resume() const noexcept {}
};

struct Yield {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) const noexcept {
        if (auto* s = get_current_scheduler()) {
            s->enqueue_async([h]() { h.resume(); });
        } else {
            h.resume();
        }
    }
    void await_resume() const noexcept {}
};

} // namespace jaeng::async
