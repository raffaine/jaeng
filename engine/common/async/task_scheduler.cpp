#include "task_scheduler.h"
#include "awaiters.h"
#include "common/logging.h"

namespace jaeng::async {

TaskScheduler::TaskScheduler() {}

TaskScheduler::~TaskScheduler() {
    shutdown();
}

void TaskScheduler::initialize(uint32_t workerCount, uint32_t ioWorkerCount) {
    if (workerCount == 0) {
        workerCount = std::thread::hardware_concurrency();
        if (workerCount > 0) workerCount--; // Leave one core for Main/OS and Sim/Render
        if (workerCount == 0) workerCount = 1;
    }

    stop_ = false;
    for (uint32_t i = 0; i < workerCount; ++i) {
        workers_.emplace_back(&TaskScheduler::worker_loop, this);
    }
    
    for (uint32_t i = 0; i < ioWorkerCount; ++i) {
        ioWorkers_.emplace_back(&TaskScheduler::io_worker_loop, this);
    }
    
    JAENG_LOG_INFO("TaskScheduler initialized with {} compute workers and {} IO workers", workerCount, ioWorkerCount);
}

void TaskScheduler::shutdown() {
    {
        std::lock_guard<std::mutex> lockAsync(asyncMutex_);
        std::lock_guard<std::mutex> lockIo(ioMutex_);
        if (stop_) return;
        stop_ = true;
    }
    asyncCv_.notify_all();
    ioCv_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
    
    for (std::thread& worker : ioWorkers_) {
        if (worker.joinable()) worker.join();
    }
    ioWorkers_.clear();
    
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

thread_local bool t_isWorker = false;
thread_local bool t_isIO = false;

bool TaskScheduler::is_worker_thread() const {
    return t_isWorker;
}

bool TaskScheduler::is_io_thread() const {
    return t_isIO;
}

void TaskScheduler::worker_loop() {
    t_isWorker = true;
    set_current_scheduler(this);
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

void TaskScheduler::io_worker_loop() {
    t_isWorker = true;
    t_isIO = true;
    set_current_scheduler(this);
    while (true) {
        TaskFn task;
        {
            std::unique_lock<std::mutex> lock(ioMutex_);
            ioCv_.wait(lock, [this]() { return stop_ || !ioQueue_.empty(); });
            
            if (stop_ && ioQueue_.empty()) return;
            
            task = std::move(ioQueue_.front());
            ioQueue_.pop_front();
        }
        task();
    }
}

} // namespace jaeng::async
