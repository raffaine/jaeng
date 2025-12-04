#pragma once

#include <glm/glm.hpp>

namespace jaeng::math {
    struct AABB {
        glm::vec3 min, max;

        bool intersects(const AABB& other) {
            return  (min.x <= other.max.x && max.x >= other.min.x) &&
                    (min.y <= other.max.y && max.y >= other.min.y) &&
                    (min.z <= other.max.z && max.z >= other.min.z);

        }
    };
}
