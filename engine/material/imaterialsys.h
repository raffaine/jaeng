#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>

#include "render/public/renderer_api.h"
#include "common/result.h"

typedef uint32_t MaterialHandle; // Opaque identifier

struct BindGroup {
    RendererHandle pipeline;
    RendererHandle textureSRV;
    RendererHandle sampler;
    RendererHandle constantBuffer;
};

struct MaterialMetadata {
    std::string name;
    std::string shaderPath;
    std::vector<std::string> texturePaths;
    std::unordered_map<std::string, float> scalarParams;
    std::unordered_map<std::string, glm::vec4> vectorParams;
};

struct MaterialEventListener {
};

class IMaterialSystem {
public:
    virtual ~IMaterialSystem() = default;

    // Create material from a virtual path (disk, memory, etc.)
    virtual jaeng::result<MaterialHandle> createMaterial(const std::string& path) = 0;

    // Create Material from a virtual path but with hardcoded layout descritors (from reflection)
    virtual jaeng::result<MaterialHandle> createMaterial(
        const std::string& path,
        const VertexLayoutDesc* vertexLayout,
        size_t vertexLayoutCount,
        const BindGroupLayoutDesc* bindGroups,
        size_t bindGroupCount
    ) = 0;

    // Destroy material
    virtual void destroyMaterial(MaterialHandle handle) = 0;

    // Query GPU bindings for rendering
    virtual jaeng::result<const BindGroup*> getBindGroup(MaterialHandle handle) const = 0;

    // Query metadata (for editor or debug)
    virtual jaeng::result<const MaterialMetadata*> getMetadata(MaterialHandle handle) const = 0;

    // Hot-reload material
    virtual jaeng::result<> reloadMaterial(MaterialHandle handle) = 0;

    // Event subscription for material changes
    virtual void subscribe(MaterialEventListener* listener) = 0;
};
