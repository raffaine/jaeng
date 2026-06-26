#pragma once

#include <bitset>
#include <string>
#include <memory>
#include <unordered_map>

#include "common/result.h"
#include "imaterialsys.h"
#include "storage/ifstorage.h"

namespace jaeng {

using namespace renderer;

class MaterialSystem : public IMaterialSystem {
public:
    static constexpr size_t MAX_MATERIALS = 1024;

    explicit MaterialSystem(IFileManager& fm, std::shared_ptr<RendererAPI>& gfx)
        : fileManager(&fm), renderer(gfx) {}

    virtual ~MaterialSystem() = default;
    
    MaterialSystem(const MaterialSystem&) = delete;
    MaterialSystem& operator=(const MaterialSystem&) = delete;
    
    MaterialSystem(MaterialSystem&&) noexcept = delete;
    MaterialSystem& operator=(MaterialSystem&&) noexcept = delete;

    result<MaterialHandle> createMaterial(const std::string& path) override;

    async::Task<result<MaterialHandle>> createMaterialAsync(const std::string& path) override;

    // Create Material from a virtual path but with hardcoded layout descriptors (from reflection)
    result<MaterialHandle> createMaterial(
        const std::string& path,
        const VertexLayoutDesc* vertexLayout,
        size_t vertexLayoutCount,
        const char* requiredSemantics[] // count should match attributes on vertex layout
    ) override;

    // Destroy material
    void destroyMaterial(MaterialHandle handle) override;

    // Query GPU bindings for rendering
    // Runtime Material Mutations
    void setTextureSlot(MaterialHandle material, uint32_t slotIndex, TextureHandle texture) override;
    void setFloatParam(MaterialHandle material, const std::string& name, float value) override;
    void setVectorParam(MaterialHandle material, const std::string& name, const glm::vec4& value) override;

    result<const MaterialBindings*> getBindData(MaterialHandle handle) const override;
    
    // Query Metadata about the material
    result<const MaterialMetadata*> getMetadata(MaterialHandle handle) const override;

    // Hot-reload material
    result<> reloadMaterial(MaterialHandle handle) override;

    // Event subscription for material changes
    void subscribe(MaterialEventListener* listener) override {}

private:
    // External Derpendencies
    IFileManager* fileManager;
    std::weak_ptr<RendererAPI>  renderer;

    // Internal Storage
    struct Storage {
        MaterialMetadata mat;
        MaterialBindings bg;
        std::vector<std::vector<uint8_t>> cbShadows; // Shadow copy of constant buffers for parameter mutation
    };

    std::unordered_map<MaterialHandle, std::shared_ptr<Storage>> storage;
    std::bitset<MAX_MATERIALS> slotUsage;
    mutable std::mutex storageMutex;
    
    // Common Logic
    struct ReflectionData {
        struct CBufferReflect {
            std::string name;
            uint32_t size;
            std::vector<CBVarData> variables;
        };

        std::string name;
        uint32_t stride;
        std::vector<VertexAttributeDesc> attributes;
        std::vector<std::string> semantics;
        std::vector<CBufferReflect> cbuffers;
    };
    result<ReflectionData> _loadReflection(IFileManager& fm, const std::string& path);
    result<MaterialHandle> _createMaterialMetadata(IFileManager& fm, const std::string& path);
    result<> _createMaterialResources(IFileManager& fm, Storage& m, const VertexLayoutDesc* vtxLayout, size_t vtxLayoutCount, 
                                             const char* requiredSemantics[]);

    async::Task<result<ReflectionData>> _loadReflectionAsync(IFileManager& fm, const std::string& path);
    async::Task<result<MaterialHandle>> _createMaterialMetadataAsync(IFileManager& fm, const std::string& path);
    async::Task<result<>> _createMaterialResourcesAsync(IFileManager& fm, Storage& m, const VertexLayoutDesc* vtxLayout, size_t vtxLayoutCount, 
                                             const char* requiredSemantics[]);
};

} // namespace jaeng
