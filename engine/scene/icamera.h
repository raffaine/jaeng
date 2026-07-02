#pragma once

#include "common/math/math.h"
#include "common/math/aabb.h"
#include "common/math/conventions.h"
#include "common/math/ray.h"

namespace jaeng {

class ICamera {
public:
    virtual ~ICamera() {};

    virtual jaeng::math::mat4 getViewProj() const = 0;
    virtual math::AABB getViewedVolume() const = 0;
    virtual math::Ray  getRay(float x, float y) const = 0;
};

} // namespace jaeng
