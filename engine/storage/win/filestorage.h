#pragma once

#include <unordered_map>

#include "storage/ifstorage.h"

class FileManager : public IFileManager {
public:
    FileManager();
    virtual ~FileManager() = default;

    FileManager(const FileManager&) = delete;
    FileManager& operator=(const FileManager&) = delete;
    
    FileManager(FileManager&&) noexcept = default;
    FileManager& operator=(FileManager&&) noexcept = default;

    jaeng::result<> initialize() override;

    jaeng::result<std::vector<uint8_t>> load(const std::string& path) override;

    void registerMemoryFile(const std::string& path, const void* data, uint64_t byteSize) override;

    bool exists(const std::string& path) const override;
    
    std::unique_ptr<SubscriptionT> track(const std::string& path, std::function<void(const FileChangedEvent&)> callback) override;

private:
    std::unordered_map<std::string, std::vector<uint8_t>> memoryFiles;
    std::shared_ptr<EventBus> eventBus;

    std::vector<uint8_t> loadFromDisk(const std::string& path);
};
