#pragma once

#include <string>
#include <unordered_map>
#include <optional>

#include "material/imaterialsys.h"
#include "render/public/renderer_api.h"

namespace jaeng {

class PipelineCache {
public:
    // Pipeline is mainly defined by Material but keys on topology as well
    struct Key {
        MaterialHandle material;
        PrimitiveTopology topology;
        bool enableBlend;

        bool operator==(const Key& other) const {
            return material == other.material && topology == other.topology && enableBlend == other.enableBlend;
        }
    };

    struct KeyHash {
        size_t operator()(const Key& key) const {
            size_t h1 = std::hash<uint32_t>{}(key.material);
            size_t h2 = std::hash<uint32_t>{}(static_cast<uint32_t>(key.topology));
            size_t h3 = std::hash<bool>{}(key.enableBlend);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
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

} // namespace jaeng
