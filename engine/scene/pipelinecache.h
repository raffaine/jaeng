#pragma once

#include <string>
#include <unordered_map>
#include <optional>

#include "material/imaterialsys.h"
#include "render/public/renderer_api.h"

class PipelineCache {
public:
    // Pipeline is mainly defined by Material but keys on topology as well
    struct Key {
        MaterialHandle material;
        PrimitiveTopology topology;

        bool operator==(const Key& other) const {
            return material == other.material && topology == other.topology;
        }
    };

    struct KeyHash {
        size_t operator()(const Key& key) const {
            return std::hash<uint64_t>()(static_cast<uint64_t>(key.material) + ((static_cast<uint64_t>(key.topology)+1) << sizeof(uint32_t)) );
        }
    };

    void storePipeline(const Key& key, PipelineHandle pipelineHandle) {
        cache[key] = pipelineHandle;
    }

    std::optional<PipelineHandle> getPipeline(const Key& key) const {
        if (auto it = cache.find(key); it != cache.end())
            return it->second;
        else
            return std::nullopt;
    }

private:
    std::unordered_map<Key, PipelineHandle, KeyHash> cache;
};
