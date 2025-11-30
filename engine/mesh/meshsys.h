#pragma once

#include "imeshsys.h"
#include "storage/ifstorage.h"

#include <memory>
#include <bitset>
#include <string>
#include <unordered_map>

class MeshSystem : public IMeshSystem {
public:
    static constexpr uint32_t MAX_MESH_ENTRIES = 1024;

    MeshSystem(std::shared_ptr<RendererAPI> renderer,
               std::shared_ptr<IFileManager> fileManager)
        : renderer_(renderer), fileManager_(fileManager) {}

    // Load mesh from file (e.g., .obj or custom format)
    jaeng::result<MeshHandle> loadMesh(const std::string& path) override;

    // Remove mesh
    jaeng::result<void> removeMesh(MeshHandle handle) override;

    // Get mesh for rendering
    jaeng::result<const Mesh*> getMesh(MeshHandle handle) const override;

private:
    MeshHandle allocateSlot();
    void freeSlot(MeshHandle handle);

    std::weak_ptr<RendererAPI> renderer_;
    std::weak_ptr<IFileManager> fileManager_;

    std::unordered_map<MeshHandle, Mesh> meshes;
    std::bitset<MAX_MESH_ENTRIES> slotUsage; // lightweight slot tracking
    MeshHandle nextHandle = 0;
};
