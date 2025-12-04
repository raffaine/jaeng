#include "grid_partition.h"

void GridPartitioner::build()
{}

void GridPartitioner::rebuild()
{}

void GridPartitioner::reset()
{}

std::vector<ComponentPack> GridPartitioner::queryVisible(const jaeng::math::AABB& volume) const
{
    auto ecs = entitySource.lock();
    if (!ecs) return {};

    auto entities = ecs->getAllEntities<Transform>();
    std::vector<ComponentPack> cps;
    for (auto e : entities) {
        cps.push_back(ComponentPack{
            .transform = ecs->getComponent<Transform>(e),
            .mesh = ecs->getComponent<MeshHandle>(e),
            .material = ecs->getComponent<MaterialHandle>(e)
        });
    }
    return cps;
}
