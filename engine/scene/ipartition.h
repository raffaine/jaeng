#pragma once

#include "common/math/aabb.h"
#include "entity/entity.h"
#include "material/imaterialsys.h"
#include "mesh/imeshsys.h"

#include <vector>
#include <cstdint>

// A flat, lock-free copy of the visual data needed to draw an entity
struct RenderProxy {
    uint32_t id;
    Transform transform;
    MeshHandle mesh;
    MaterialHandle material;
    BufferHandle constant;
};

// Minimal spatial partitioner interface
class ISpatialPartitioner {
public:
    virtual ~ISpatialPartitioner() = default;

    virtual void addOrUpdate(const RenderProxy& proxy) = 0;
    virtual void remove(uint32_t id) = 0;

    // Builds the Partition
    virtual void build() = 0;

    // Clears data and Rebuilds Partition
    virtual void rebuild() = 0;

    // Clears existing partition
    virtual void reset() = 0;

    // Query entities contained by given volume (e.g., frustum culling)
    virtual std::vector<RenderProxy> queryVisible(const jaeng::math::AABB& volume) const = 0;
};
