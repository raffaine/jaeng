#pragma once

#include <bitset>
#include <string>
#include <memory>
#include <unordered_map>

#include "common/result.h"
#include "imaterialsys.h"
#include "storage/ifstorage.h"

class MaterialSystem : public IMaterialSystem {
public:
    static constexpr size_t MAX_MATERIALS = 1024;

    explicit MaterialSystem(std::shared_ptr<IFileManager>& fm, std::shared_ptr<RendererAPI>& gfx)
        : fileManager(fm), renderer(gfx) {}

    virtual ~MaterialSystem() = default;
    
    MaterialSystem(const MaterialSystem&) = delete;
    MaterialSystem& operator=(const MaterialSystem&) = delete;
    
    MaterialSystem(MaterialSystem&&) noexcept = default;
    MaterialSystem& operator=(MaterialSystem&&) noexcept = default;

    jaeng::result<MaterialHandle> createMaterial(const std::string& path) override;

    // Create Material from a virtual path but with hardcoded layout descritors (from reflection)
    jaeng::result<MaterialHandle> createMaterial(
        const std::string& path,
        const VertexLayoutDesc* vertexLayout,
        size_t vertexLayoutCount,
        const std::string* requiredSemantics, // count should match attributes on vertex layout
        const BindGroupLayoutDesc* bindGroups,
        size_t bindGroupCount
    ) override;

    // Destroy material
    void destroyMaterial(MaterialHandle handle) override;

    // Query GPU bindings for rendering
    jaeng::result<const MaterialBindings*> getBindData(MaterialHandle handle) const override;
    
    // Query Metadata about the material
    jaeng::result<const MaterialMetadata*> getMetadata(MaterialHandle handle) const override;

    // Hot-reload material
    jaeng::result<> reloadMaterial(MaterialHandle handle) override;

    // Event subscription for material changes
    void subscribe(MaterialEventListener* listener) override {}

private:
    // External Derpendencies
    std::weak_ptr<IFileManager> fileManager;
    std::weak_ptr<RendererAPI>  renderer;

    // Internal Storage
    struct Storage {
        MaterialMetadata mat;
        MaterialBindings bg;
    };

    std::unordered_map<MaterialHandle, Storage> storage;
    std::bitset<MAX_MATERIALS> slotUsage;
    
    // Common Logic
    jaeng::result<MaterialHandle> _createMaterialMetadata(IFileManager& fm, const std::string& path);
    jaeng::result<> _createMaterialResources(IFileManager& fm, Storage& m, const VertexLayoutDesc* vtxLayout, size_t vtxLayoutCount, 
                                             const std::string* requiredSemantics, const BindGroupLayoutDesc* bindGroups, size_t bindGroupCount);
};
