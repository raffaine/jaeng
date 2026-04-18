#pragma once

#include <bitset>
#include <string>
#include <memory>
#include <unordered_map>

#include "common/result.h"
#include "imaterialsys.h"
#include "storage/ifstorage.h"

namespace jaeng {

class MaterialSystem : public IMaterialSystem {
public:
    static constexpr size_t MAX_MATERIALS = 1024;

    explicit MaterialSystem(IFileManager& fm, std::shared_ptr<RendererAPI>& gfx)
        : fileManager(&fm), renderer(gfx) {}

    virtual ~MaterialSystem() = default;
    
    MaterialSystem(const MaterialSystem&) = delete;
    MaterialSystem& operator=(const MaterialSystem&) = delete;
    
    MaterialSystem(MaterialSystem&&) noexcept = default;
    MaterialSystem& operator=(MaterialSystem&&) noexcept = default;

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
    result<const MaterialBindings*> getBindData(MaterialHandle handle) const override;
    
    // Query Metadata about the material
    result<const MaterialMetadata*> getMetadata(MaterialHandle handle) const override;

    // Hot-reload material
    result<> reloadMaterial(MaterialHandle handle) override;

    // Update material parameters
    void setVectorParam(MaterialHandle handle, const std::string& name, const glm::vec4& value) override;

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
    };

    std::unordered_map<MaterialHandle, std::shared_ptr<Storage>> storage;
    std::bitset<MAX_MATERIALS> slotUsage;
    mutable std::mutex storageMutex;
    
    // Common Logic
    struct ReflectionData {
        uint32_t stride;
        std::vector<VertexAttributeDesc> attributes;
        std::vector<std::string> semantics;
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
