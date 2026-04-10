#include "awaiters.h"
#include <atomic>

namespace jaeng::async {

static std::atomic<TaskScheduler*> g_currentScheduler = nullptr;

void set_current_scheduler(TaskScheduler* scheduler) {
    g_currentScheduler.store(scheduler);
}

TaskScheduler* get_current_scheduler() {
    return g_currentScheduler.load();
}

} // namespace jaeng::async
