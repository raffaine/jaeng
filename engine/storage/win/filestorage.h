#pragma once

#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>

#include "storage/ifstorage.h"

namespace jaeng {

class FileManager : public IFileManager {
public:
    FileManager();
    virtual ~FileManager();

    FileManager(const FileManager&) = delete;
    FileManager& operator=(const FileManager&) = delete;
    
    FileManager(FileManager&&) noexcept = default;
    FileManager& operator=(FileManager&&) noexcept = default;

    result<> initialize() override;

    result<std::vector<uint8_t>> load(const std::string& path) override;
    async::Future<result<std::vector<uint8_t>>> loadAsync(const std::string& path) override;

    void registerMemoryFile(const std::string& path, const void* data, uint64_t byteSize) override;

    bool exists(const std::string& path) const override;
    
    std::unique_ptr<SubscriptionT> track(const std::string& path, std::function<void(const FileChangedEvent&)> callback) override;

private:
    std::unordered_map<std::string, std::vector<uint8_t>> memoryFiles;
    mutable std::mutex storageMutex_;
    std::shared_ptr<EventBus> eventBus;

    std::vector<uint8_t> loadFromDisk(const std::string& path);

    // File Watcher
    void watcherLoop();
    std::atomic<bool> stopWatcher_{false};
    std::thread watcherThread_;
    std::mutex watcherMutex_;

#ifdef JAENG_LINUX
    int inotifyFd_ = -1;
    std::unordered_map<int, std::string> watchDescriptors_;
    std::unordered_map<std::string, int> pathToWatch_;
#endif
};

} // namespace jaeng
