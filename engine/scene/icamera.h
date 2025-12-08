#pragma once

#include <glm/glm.hpp>
#include "common/math/aabb.h"
#include "common/math/conventions.h"

class ICamera {
public:
    virtual ~ICamera() {};

    virtual glm::mat4 getViewProj(jaeng::math::ClipSpaceConvention) const = 0;
    virtual jaeng::math::AABB getViewedVolume() const = 0;
};
