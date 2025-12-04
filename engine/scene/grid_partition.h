#pragma once

#include <memory>

#include "ipartition.h"

class GridPartitioner : public ISpatialPartitioner {
public:
    GridPartitioner(std::shared_ptr<EntityManager> ecs) : entitySource(ecs) {}
    ~GridPartitioner() {};

    // Builds the Partition (No Op on this Example)
    void build() override;

    // Clears data and Rebuilds Partition (No Op on this Example)
    void rebuild() override;

    // Clears existing partition (No Op on this Example)
    void reset() override;

    // Query the drawing Components of entities in the given volume (All entities in this case)
    std::vector<ComponentPack> queryVisible(const jaeng::math::AABB& volume) const override;

private:
    std::weak_ptr<EntityManager> entitySource;
};
