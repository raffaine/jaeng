#pragma once

#include "entity/entity.h"
#include "scene/ipartition.h"
#include <vector>
#include <functional>
#include <glm/glm.hpp>

namespace jaeng {

class Scene;

/**
 * @brief System responsible for extracting low-level RenderProxies from high-level ECS components.
 */
class SceneRenderSystem {
public:
    /**
     * @brief Gathers entities with WorldMatrix, MeshComponent, and MaterialComponent into the provided render queue.
     * 
     * @param scene The scene context for extraction.
     * @param ecs The entity manager to query.
     * @param outCommands The output command queue.
     * @param visitor Optional callback to visit and modify each generated proxy before queuing.
     * @param volume Optional volume to filter entities by position.
     */
    static void extract(Scene& scene, EntityManager& ecs, std::vector<RenderCommand>& outCommands, 
                        std::function<void(EntityID, RenderProxy&)> visitor = nullptr,
                        const math::AABB* volume = nullptr);
};

} // namespace jaeng
