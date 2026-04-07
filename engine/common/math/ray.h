#pragma once

#include <glm/glm.hpp>

namespace jaeng::math {

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;

    Ray(glm::vec3 o, glm::vec3 d) : origin(o), direction(glm::normalize(d)) {}
};

} // namespace jaeng::math
