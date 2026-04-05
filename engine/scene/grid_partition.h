#pragma once

#include <memory>

#include "ipartition.h"
#include <unordered_map>

class GridPartitioner : public ISpatialPartitioner {
public:
    void addOrUpdate(const RenderProxy& proxy) override;
    void remove(uint32_t id) override;

    // Builds the Partition (No Op on this Example)
    void build() override;

    // Clears data and Rebuilds Partition (No Op on this Example)
    void rebuild() override;

    // Clears existing partition (No Op on this Example)
    void reset() override;

    // Query the drawing Components of entities in the given volume (All entities in this case)
    std::vector<RenderProxy> queryVisible(const jaeng::math::AABB& volume) const override;

private:
    std::unordered_map<uint32_t, RenderProxy> proxies_;
};
