#pragma once

#include "common/math/aabb.h"
#include "entity/entity.h"
#include "material/imaterialsys.h"
#include "mesh/imeshsys.h"

// Set of Components to be obtained on queries
struct ComponentPack {
    Transform* transform;
    MeshHandle* mesh;
    MaterialHandle* material;
};

// Minimal spatial partitioner interface
class ISpatialPartitioner {
public:
    virtual ~ISpatialPartitioner() = default;

    // Builds the Partition (Specific instances will probably use ECS)
    virtual void build() = 0;

    // Clears data and Rebuilds Partition
    virtual void rebuild() = 0;

    // Clears existing partition
    virtual void reset() = 0;

    // Query entities contained by given volume (e.g., frustum culling)
    virtual std::vector<ComponentPack> queryVisible(const jaeng::math::AABB& volume) const = 0;
};
