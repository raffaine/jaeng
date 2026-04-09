#pragma once

#include <glm/glm.hpp>
#include "ray.h"
#include <algorithm>

namespace jaeng::math {
    struct AABB {
        glm::vec3 min, max;

        bool intersects(const AABB& other) {
            return  (min.x <= other.max.x && max.x >= other.min.x) &&
                    (min.y <= other.max.y && max.y >= other.min.y) &&
                    (min.z <= other.max.z && max.z >= other.min.z);

        }

        bool intersects(const Ray& ray, float& tmin) const {
            float t1 = (min.x - ray.origin.x) / ray.direction.x;
            float t2 = (max.x - ray.origin.x) / ray.direction.x;
            float t3 = (min.y - ray.origin.y) / ray.direction.y;
            float t4 = (max.y - ray.origin.y) / ray.direction.y;
            float t5 = (min.z - ray.origin.z) / ray.direction.z;
            float t6 = (max.z - ray.origin.z) / ray.direction.z;

            float tmin_x = (std::min)(t1, t2);
            float tmax_x = (std::max)(t1, t2);
            float tmin_y = (std::min)(t3, t4);
            float tmax_y = (std::max)(t3, t4);
            float tmin_z = (std::min)(t5, t6);
            float tmax_z = (std::max)(t5, t6);

            float tmin_final = (std::max)((std::max)(tmin_x, tmin_y), tmin_z);
            float tmax_final = (std::min)((std::min)(tmax_x, tmax_y), tmax_z);

            if (tmax_final < 0 || tmin_final > tmax_final) {
                return false;
            }

            tmin = tmin_final;
            return true;
        }
    };
}
