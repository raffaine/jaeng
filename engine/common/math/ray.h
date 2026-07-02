#pragma once

#include "common/math/math.h"

namespace jaeng::math {

struct Ray {
    jaeng::math::vec3 origin;
    jaeng::math::vec3 direction;

    Ray(jaeng::math::vec3 o, jaeng::math::vec3 d) : origin(o), direction(jaeng::math::normalize(d)) {}
};

} // namespace jaeng::math
