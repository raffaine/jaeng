#pragma once

#include <string>

#include "common/result.h"
#include "imaterialsys.h"
#include "storage/ifstorage.h"

class MaterialSystem : public IMaterialSystem {
public:
    explicit MaterialSystem(IFileManager* fm)
        : fileManager(fm) {}

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
        const BindGroupLayoutDesc* bindGroups,
        size_t bindGroupCount
    ) override;

    // Destroy material
    void destroyMaterial(MaterialHandle handle) override;

    // Query GPU bindings for rendering
    jaeng::result<const BindGroup*> getBindGroup(MaterialHandle handle) const override;
    jaeng::result<const MaterialMetadata*> getMetadata(MaterialHandle handle) const override;

    // Hot-reload material
    jaeng::result<> reloadMaterial(MaterialHandle handle) override;

    // Event subscription for material changes
    void subscribe(MaterialEventListener* listener) override {}

private:
    IFileManager* fileManager;
    //std::unordered_map<MaterialHandle, MaterialMetadata> metadataStore;
    std::unordered_map<MaterialHandle, BindGroup> materialStore;
};
