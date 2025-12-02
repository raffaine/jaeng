#pragma once

#include <string>
#include <vector>
#include "common/result.h"
#include "common/pubsub.h"

struct FileChangedEvent {
    enum class ChangeType {
        Created = 0,
        Modified,
        Deleted
    } change;
};

class IFileManager {
public:
    virtual ~IFileManager() = default;

    virtual jaeng::result<> initialize() = 0;

    // Return file contents or error
    virtual jaeng::result<std::vector<uint8_t>> load(const std::string& path) = 0;

    // Register in-memory file
    virtual void registerMemoryFile(const std::string& path, const void* data, uint64_t byteSize) = 0;

    // Check if file exists
    virtual bool exists(const std::string& path) const = 0;

    // Tracks changes to a path
    using SubscriptionT = EventBus::Subscription<FileChangedEvent>;
    virtual std::unique_ptr<SubscriptionT> track(const std::string& path, std::function<void(const FileChangedEvent&)> callback) = 0;
};
