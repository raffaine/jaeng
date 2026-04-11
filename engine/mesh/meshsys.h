#pragma once

#include "imeshsys.h"
#include "storage/ifstorage.h"

#include <memory>
#include <bitset>
#include <string>
#include <unordered_map>

namespace jaeng {

// Structs should be in imeshsys.h or common/math/conventions.h or similar.
// For now, they are in imeshsys.h.

class MeshSystem : public IMeshSystem {
public:
    static constexpr uint32_t MAX_MESH_ENTRIES = 1024;

    MeshSystem(IFileManager& fileManager,
               std::shared_ptr<RendererAPI> renderer)
        : renderer_(renderer), fileManager_(&fileManager) {}

    // Load mesh from file (e.g., .obj or custom format)
    result<MeshHandle> loadMesh(const std::string& path) override;

    // Remove mesh
    result<void> removeMesh(MeshHandle handle) override;

    // Get mesh for rendering
    result<const Mesh*> getMesh(MeshHandle handle) const override;

private:
    result<MeshHandle> allocateSlot();
    void freeSlot(MeshHandle handle);

    std::weak_ptr<RendererAPI> renderer_;
    IFileManager* fileManager_;

    std::unordered_map<MeshHandle, Mesh> meshes;
    std::bitset<MAX_MESH_ENTRIES> slotUsage; // lightweight slot tracking
    MeshHandle nextHandle = 0;
};

} // namespace jaeng
