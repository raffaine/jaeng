#pragma once

#include "common/math/aabb.h"
#include "entity/entity.h"
#include "material/imaterialsys.h"
#include "mesh/imeshsys.h"

#include <vector>
#include <cstdint>

namespace jaeng {

struct MeshComponent { MeshHandle handle; };
struct MaterialComponent { MaterialHandle handle; };
struct BufferComponent { BufferHandle handle; };

// A flat, lock-free copy of the visual data needed to draw an entity
struct RenderProxy {
    uint32_t id;
    glm::mat4 worldMatrix;
    MeshHandle mesh;
    MaterialHandle material;
    BufferHandle constant;
    glm::vec4 color{1.0f};
};

struct UIRenderProxy {
    uint32_t id;
    float x, y, w, h;
    int32_t zIndex;
    glm::vec4 color;
    MeshHandle mesh;
    MaterialHandle material;
    BufferHandle constant;
};

enum class RenderCommandType { Update, Destroy, UpdateCamera, UpdateUI, DestroyUI };

struct RenderCommand {
    RenderCommandType type;
    RenderProxy proxy; // Valid if Update
    UIRenderProxy uiProxy; // Valid if UpdateUI
    uint32_t id;       // Valid if Destroy or DestroyUI
    glm::mat4 cameraViewProj; // Valid if UpdateCamera
};

// Minimal spatial partitioner interface
class ISpatialPartitioner {
public:
    virtual ~ISpatialPartitioner() = default;

    // Adds or Updates an entity proxy
    virtual void addOrUpdate(const RenderProxy& proxy) = 0;

    // Removes an entity from partition
    virtual void remove(uint32_t id) = 0;

    // Builds the Partition
    virtual void build() = 0;

    // Force a Rebuild of the Partition
    virtual void rebuild() = 0;

    // Clears existing partition
    virtual void reset() = 0;

    // Query entities contained by given volume (e.g., frustum culling)
    virtual std::vector<RenderProxy> queryVisible(const jaeng::math::AABB& volume) const = 0;
};

} // namespace jaeng
