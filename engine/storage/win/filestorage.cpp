#include <filesystem>
#include <fstream>
#include <cstring>

#include "filestorage.h"

namespace jaeng {

FileManager::FileManager() : eventBus(std::make_unique<EventBus>()) {
}

result<> FileManager::initialize() {
    return {};
}

result<std::vector<uint8_t>> FileManager::load(const std::string& path) {
    JAENG_ERROR_IF(!exists(path), error_code::no_resource, "[FileManager] No file on requested path");
    if (auto it = memoryFiles.find(path); it != memoryFiles.end()) {
        return it->second;
    }
    // Fallback to disk
    return loadFromDisk(path);
}

async::Future<result<std::vector<uint8_t>>> FileManager::loadAsync(const std::string& path) {
    if (!exists(path)) {
        std::promise<result<std::vector<uint8_t>>> p;
        p.set_value(Error::fromMessage((int)error_code::no_resource, "[FileManager] No file on requested path"));
        return async::Future<result<std::vector<uint8_t>>>(p.get_future());
    }
    if (auto it = memoryFiles.find(path); it != memoryFiles.end()) {
        std::promise<result<std::vector<uint8_t>>> p;
        p.set_value(it->second);
        return async::Future<result<std::vector<uint8_t>>>(p.get_future());
    }
    
    return async::get_current_scheduler()->enqueue_io([this, path]() -> result<std::vector<uint8_t>> {
        return loadFromDisk(path);
    });
}

void FileManager::registerMemoryFile(const std::string& path, const void* data, uint64_t byteSize) {
    std::vector<uint8_t> vec(byteSize, 0);
    std::memcpy(vec.data(), data, byteSize);

    memoryFiles[path] = std::move(vec);
}

bool FileManager::exists(const std::string& path) const {
    return memoryFiles.contains(path) || std::filesystem::exists(path);
}

std::unique_ptr<FileManager::SubscriptionT> FileManager::track(const std::string& path, std::function<void(const FileChangedEvent&)> callback) {
    // TODO: Install a tracking mechanism to detect changes on a path
    return eventBus->subscribe(callback);
}

std::vector<uint8_t> FileManager::loadFromDisk(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), {});
}

} // namespace jaeng
