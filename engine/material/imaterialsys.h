#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>

#include "render/public/renderer_api.h"
#include "common/result.h"
#include "common/async/task.h"

namespace jaeng {

typedef uint32_t MaterialHandle; // Opaque identifier

struct MaterialBindings {
    ShaderModuleHandle vertexShader;
    ShaderModuleHandle pixelShader;
    RendererHandle vertexLayout;
    std::vector<std::string> requiredSemantics;
    std::vector<TextureHandle> textures;
    std::vector<SamplerHandle> samplers;
    std::vector<BufferHandle>  constantBuffers;
    
    // Bindless indices
    std::vector<uint32_t> textureIndices;
    std::vector<uint32_t> samplerIndices;
};

struct CBData {
    std::string name;
    uint32_t size;
    uint32_t binding;
};

struct TextureData {
    std::string path;
    std::string format;
    uint32_t width, height;
    struct {
        std::string filter;
        std::string addressModeU;
        std::string addressModeV;
    } sampler;
};

struct MaterialMetadata {
    std::string path;
    std::string name;
    std::string vsPath;
    std::string psPath;
    std::string reflectPath;
    std::vector<TextureData> textures;
    // --- Pipeline States ---
    struct {
        bool enabled;
        std::string srcFactor;
        std::string dstFactor;
    } blendState;
    struct {
        std::string cullMode;
        std::string fillMode;
    } rasterizer;
    struct {
        bool depthTest;
        bool depthWrite;
    } depthStencil;
    // --- Constant Buffers and Per Model Data ---
    std::unordered_map<std::string, float> scalarParams;
    std::unordered_map<std::string, glm::vec4> vectorParams;
    std::vector<CBData> constantBuffers;
};

struct MaterialEventListener {
};

class IMaterialSystem {
public:
    virtual ~IMaterialSystem() = default;

    // Create material from a virtual path (disk, memory, etc.)
    virtual result<MaterialHandle> createMaterial(const std::string& path) = 0;

    // Create material asynchronously
    virtual async::Task<result<MaterialHandle>> createMaterialAsync(const std::string& path) = 0;

    // Create Material from a virtual path but with hardcoded layout descriptors (from reflection)
    virtual result<MaterialHandle> createMaterial(
        const std::string& path,
        const VertexLayoutDesc* vertexLayout,
        size_t vertexLayoutCount,
        const char* requiredSemantics[]
    ) = 0;

    // Destroy material
    virtual void destroyMaterial(MaterialHandle handle) = 0;

    // Query GPU bindings for rendering
    virtual result<const MaterialBindings*> getBindData(MaterialHandle handle) const = 0;

    // Query metadata (for editor or debug)
    virtual result<const MaterialMetadata*> getMetadata(MaterialHandle handle) const = 0;

    // Hot-reload material
    virtual result<> reloadMaterial(MaterialHandle handle) = 0;

    // Update material parameters
    virtual void setVectorParam(MaterialHandle handle, const std::string& name, const glm::vec4& value) = 0;

    // Event subscription for material changes
    virtual void subscribe(MaterialEventListener* listener) = 0;
};

} // namespace jaeng
