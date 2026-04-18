#include <filesystem>
#include <fstream>
#include <cstring>

#include "filestorage.h"
#include "common/logging.h"

#ifdef JAENG_LINUX
#include <sys/inotify.h>
#include <unistd.h>
#include <poll.h>
#endif

namespace jaeng {

FileManager::FileManager() : eventBus(std::make_shared<EventBus>()) {
}

FileManager::~FileManager() {
    stopWatcher_ = true;
#ifdef JAENG_LINUX
    if (inotifyFd_ != -1) {
        // Writing to the inotify FD isn't useful for waking up, 
        // but stopping the thread and closing the FD will work.
        close(inotifyFd_);
    }
#endif
    if (watcherThread_.joinable()) {
        watcherThread_.join();
    }
}

result<> FileManager::initialize() {
#ifdef JAENG_LINUX
    inotifyFd_ = inotify_init1(IN_NONBLOCK);
    if (inotifyFd_ == -1) {
        return { Error::fromMessage((int)error_code::platform_error, "Failed to initialize inotify") };
    }
    watcherThread_ = std::thread(&FileManager::watcherLoop, this);
#endif
    return {};
}

result<std::vector<uint8_t>> FileManager::load(const std::string& path) {
    JAENG_ERROR_IF(!exists(path), error_code::no_resource, "[FileManager] No file on requested path");
    {
        std::lock_guard<std::mutex> lock(storageMutex_);
        if (auto it = memoryFiles.find(path); it != memoryFiles.end()) {
            return it->second;
        }
    }
    // Fallback to disk
    return loadFromDisk(path);
}

async::Future<result<std::vector<uint8_t>>> FileManager::loadAsync(const std::string& path) {
    if (!exists(path)) {
        async::Future<result<std::vector<uint8_t>>> f;
        f.get_shared_state()->set_value(Error::fromMessage((int)error_code::no_resource, "[FileManager] No file on requested path"));
        return f;
    }
    {
        std::lock_guard<std::mutex> lock(storageMutex_);
        if (auto it = memoryFiles.find(path); it != memoryFiles.end()) {
            async::Future<result<std::vector<uint8_t>>> f;
            f.get_shared_state()->set_value(it->second);
            return f;
        }
    }
    
    auto* scheduler = async::get_current_scheduler();
    if (!scheduler) {
        async::Future<result<std::vector<uint8_t>>> f;
        f.get_shared_state()->set_value(Error::fromMessage((int)error_code::unknown_error, "[FileManager] No scheduler active"));
        return f;
    }

    return scheduler->enqueue_io([this, path]() -> result<std::vector<uint8_t>> {
        return loadFromDisk(path);
    });
}

void FileManager::registerMemoryFile(const std::string& path, const void* data, uint64_t byteSize) {
    std::vector<uint8_t> vec(byteSize, 0);
    std::memcpy(vec.data(), data, byteSize);

    {
        std::lock_guard<std::mutex> lock(storageMutex_);
        memoryFiles[path] = std::move(vec);
    }
}

bool FileManager::exists(const std::string& path) const {
    {
        std::lock_guard<std::mutex> lock(storageMutex_);
        if (memoryFiles.contains(path)) return true;
    }
    return std::filesystem::exists(path);
}

std::unique_ptr<FileManager::SubscriptionT> FileManager::track(const std::string& path, std::function<void(const FileChangedEvent&)> callback) {
#ifdef JAENG_LINUX
    bool isMemoryFile = false;
    {
        std::lock_guard<std::mutex> lock(storageMutex_);
        isMemoryFile = memoryFiles.contains(path);
    }
    if (inotifyFd_ != -1 && !isMemoryFile) {
        std::lock_guard<std::mutex> lock(watcherMutex_);
        if (!pathToWatch_.contains(path)) {
            int wd = inotify_add_watch(inotifyFd_, path.c_str(), IN_MODIFY);
            if (wd != -1) {
                pathToWatch_[path] = wd;
                watchDescriptors_[wd] = path;
            }
        }
    }
#endif
    return eventBus->subscribe(callback);
}

std::vector<uint8_t> FileManager::loadFromDisk(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), {});
}

void FileManager::watcherLoop() {
#ifdef JAENG_LINUX
    char buffer[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    
    while (!stopWatcher_) {
        struct pollfd pfd = { inotifyFd_, POLLIN, 0 };
        int ret = poll(&pfd, 1, 100); // 100ms timeout
        
        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t len = read(inotifyFd_, buffer, sizeof(buffer));
            if (len < 0 && errno != EAGAIN) {
                break;
            }

            if (len > 0) {
                const struct inotify_event* event;
                for (char* ptr = buffer; ptr < buffer + len; ptr += sizeof(struct inotify_event) + event->len) {
                    event = (const struct inotify_event*)ptr;
                    if (event->mask & IN_MODIFY) {
                        std::string path;
                        {
                            std::lock_guard<std::mutex> lock(watcherMutex_);
                            auto it = watchDescriptors_.find(event->wd);
                            if (it != watchDescriptors_.end()) {
                                path = it->second;
                            }
                        }
                        if (!path.empty()) {
                            FileChangedEvent ev{ .change = FileChangedEvent::ChangeType::Modified, .path = path };
                            eventBus->publish(ev);
                        }
                    }
                }
            }
        }
    }
#endif
}

} // namespace jaeng
