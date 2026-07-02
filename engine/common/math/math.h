#pragma once

// Engine determinations: Left-handed coordinate system, Depth 0 to 1 (D3D12/HLSL style)
#ifndef GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_LEFT_HANDED
#endif

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/quaternion.hpp>

namespace jaeng::math {
    // Vector types
    using vec2 = glm::vec2;
    using vec3 = glm::vec3;
    using vec4 = glm::vec4;
    using ivec2 = glm::ivec2;
    using ivec3 = glm::ivec3;
    using ivec4 = glm::ivec4;

    // Matrix types
    using mat3 = glm::mat3;
    using mat4 = glm::mat4;

    // Quaternion type
    using quat = glm::quat;

    // Common functions
    using glm::normalize;
    using glm::cross;
    using glm::dot;
    using glm::length;
    using glm::distance;
    using glm::inverse;
    using glm::transpose;
    
    // Matrix transformations
    using glm::perspective;
    using glm::ortho;
    using glm::lookAt;
    using glm::translate;
    using glm::scale;
    using glm::rotate;
    using glm::perspectiveLH_ZO;

    // Angles and Quaternions
    using glm::radians;
    using glm::degrees;
    using glm::angleAxis;
    using glm::slerp;
    using glm::toMat4;
    using glm::pi;
    using glm::half_pi;
    using glm::quarter_pi;

    // Utility
    using glm::value_ptr;
    using glm::clamp;
    using glm::min;
    using glm::max;
    using glm::mix;
}
