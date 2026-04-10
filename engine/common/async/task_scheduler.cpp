#include "task_scheduler.h"
#include "common/logging.h"

namespace jaeng::async {

TaskScheduler::TaskScheduler() {}

TaskScheduler::~TaskScheduler() {
    shutdown();
}

void TaskScheduler::initialize(uint32_t workerCount) {
    if (workerCount == 0) {
        workerCount = std::thread::hardware_concurrency();
        if (workerCount > 0) workerCount--; // Leave one core for Main/OS and Sim/Render
        if (workerCount == 0) workerCount = 1;
    }

    stop_ = false;
    for (uint32_t i = 0; i < workerCount; ++i) {
        workers_.emplace_back(&TaskScheduler::worker_loop, this);
    }
    
    JAENG_LOG_INFO("TaskScheduler initialized with {} workers", workerCount);
}

void TaskScheduler::shutdown() {
    {
        std::lock_guard<std::mutex> lock(asyncMutex_);
        if (stop_) return;
        stop_ = true;
    }
    asyncCv_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
    JAENG_LOG_INFO("TaskScheduler shut down");
}

bool TaskScheduler::process_main_thread_tasks() {
    std::deque<TaskFn> readyTasks;
    {
        std::lock_guard<std::mutex> lock(syncMutex_);
        if (syncQueue_.empty()) return false;
        readyTasks.swap(syncQueue_);
    }

    for (auto& task : readyTasks) {
        task();
    }
    return true;
}

void TaskScheduler::worker_loop() {
    while (true) {
        TaskFn task;
        {
            std::unique_lock<std::mutex> lock(asyncMutex_);
            asyncCv_.wait(lock, [this]() { return stop_ || !asyncQueue_.empty(); });
            
            if (stop_ && asyncQueue_.empty()) return;
            
            task = std::move(asyncQueue_.front());
            asyncQueue_.pop_front();
        }
        task();
    }
}

} // namespace jaeng::async
