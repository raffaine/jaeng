#pragma once

#include <vector>
#include <string>

#include "render/public/renderer_api.h"
#include "common/result.h"

using MeshHandle = uint32_t;

struct Mesh {
    BufferHandle vertexBuffer;
    BufferHandle indexBuffer;
    std::vector<std::string> semantics;
    PrimitiveTopology topology;
    size_t indexCount;
};

class IMeshSystem {
public:
    // Load mesh from file (e.g., .obj or custom format)
    virtual jaeng::result<MeshHandle> loadMesh(const std::string& path) = 0;

    // Remove mesh
    virtual jaeng::result<void> removeMesh(MeshHandle handle) = 0;

    // Get mesh for rendering
    virtual jaeng::result<const Mesh*> getMesh(MeshHandle handle) const = 0;
};
