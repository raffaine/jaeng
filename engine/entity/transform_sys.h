#pragma once

#include "entity/entity.h"
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace jaeng {

class TransformSystem {
public:
    static void update(std::shared_ptr<EntityManager> ecs) {
        std::vector<EntityID> stack;
        stack.reserve(256);

        // Find all roots (no Relationship, or parent == -1)
        const auto& transforms = ecs->getAllEntities<Transform>();
        for (auto e : transforms) {
            auto* rel = ecs->getComponent<Relationship>(e);
            if (!rel || rel->parent == static_cast<EntityID>(-1)) {
                stack.push_back(e);
            }
        }

        // Depth-First iterative update
        while (!stack.empty()) {
            EntityID curr = stack.back();
            stack.pop_back();

            auto* t = ecs->getComponent<Transform>(curr);
            auto* wm = ecs->getComponent<WorldMatrix>(curr);
            if (!wm) wm = &ecs->addComponent<WorldMatrix>(curr);

            glm::mat4 local = glm::translate(glm::mat4(1.0f), t->position) * glm::toMat4(t->rotation) * glm::scale(glm::mat4(1.0f), t->scale);

            auto* rel = ecs->getComponent<Relationship>(curr);
            if (rel && rel->parent != static_cast<EntityID>(-1)) {
                auto* parentWm = ecs->getComponent<WorldMatrix>(rel->parent);
                wm->value = parentWm->value * local;
            } else {
                wm->value = local;
            }

            // Push children to the stack
            if (rel) {
                EntityID child = rel->firstChild;
                while (child != static_cast<EntityID>(-1)) {
                    stack.push_back(child);
                    auto* childRel = ecs->getComponent<Relationship>(child);
                    child = childRel ? childRel->nextSibling : static_cast<EntityID>(-1);
                }
            }
        }
    }
};

}