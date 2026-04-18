#pragma once

#include <vector>
#include <string>

#include "render/public/renderer_api.h"
#include "common/result.h"
#include "common/async/task.h"

namespace jaeng {

using MeshHandle = uint32_t;

struct RAWFormatHeader {
    uint32_t vertexCount;
    uint32_t indexCount;
};

struct RAWFormatVertex {
    float position[3];
    float color[3];
    float uv[2];
};

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
    virtual result<MeshHandle> loadMesh(const std::string& path) = 0;

    // Load mesh asynchronously
    virtual async::Task<result<MeshHandle>> loadMeshAsync(const std::string& path) = 0;

    // Remove mesh
    virtual result<void> removeMesh(MeshHandle handle) = 0;

    // Get mesh for rendering
    virtual result<const Mesh*> getMesh(MeshHandle handle) const = 0;
};

} // namespace jaeng
