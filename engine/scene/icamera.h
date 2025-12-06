#pragma once

#include <glm/glm.hpp>
#include "common/math/aabb.h"

class ICamera {
public:
    virtual ~ICamera() {};

    virtual glm::mat4 getViewProj() const = 0;
    virtual jaeng::math::AABB getViewedVolume() const = 0;
};
