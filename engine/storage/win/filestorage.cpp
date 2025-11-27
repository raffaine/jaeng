#include <filesystem>
#include <fstream>

#include "filestorage.h"

jaeng::result<> FileManager::initialize() {
    return {};
}

jaeng::result<std::vector<uint8_t>> FileManager::load(const std::string& path) {
    JAENG_ERROR_IF(!exists(path), jaeng::error_code::no_resource, "[FileManager] No file on requested path");
    if (auto it = memoryFiles.find(path); it != memoryFiles.end()) {
        return it->second;
    }
    // Fallback to disk
    return loadFromDisk(path);
}

void FileManager::registerMemoryFile(const std::string& path, const void* data, uint64_t byteSize) {
    std::vector<uint8_t> vec(byteSize, 0);
    std::memcpy(vec.data(), data, byteSize);

    memoryFiles[path] = std::move(vec);
}

bool FileManager::exists(const std::string& path) const {
    return memoryFiles.contains(path) || std::filesystem::exists(path);
}

std::vector<uint8_t> FileManager::loadFromDisk(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), {});
}
