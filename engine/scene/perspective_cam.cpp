#include "perspective_cam.h"
#include <glm/gtc/matrix_transform.hpp>

PerspectiveCamera::PerspectiveCamera(glm::vec3 pos, glm::vec3 lookAt, glm::vec3 upVec, float aspect,
                                     float fovDeg)
    : aspect(aspect)
    , fovY(glm::radians(fovDeg))
    , znear(0.1f)
    , zfar(100.0f)
    , position(pos)
    , target(lookAt)
    , up(upVec)
{}

glm::mat4 PerspectiveCamera::getViewProj(jaeng::math::ClipSpaceConvention convention) const 
{
    if (convention.handed == jaeng::math::Handedness::Left) {
        const auto V = glm::lookAtLH(position, target, up);
        if (convention.depth == jaeng::math::DepthRange::ZeroToOne) {
            const auto P = glm::perspectiveLH_ZO(fovY, aspect, znear, zfar);
            return P * V;
        } else {
            const auto P = glm::perspectiveLH_NO(fovY, aspect, znear, zfar);
            return P * V;
        }
    } else {
        const auto V = glm::lookAtRH(position, target, up);
        if (convention.depth == jaeng::math::DepthRange::ZeroToOne) {
            const auto P = glm::perspectiveRH_ZO(fovY, aspect, znear, zfar);
            return P * V;
        } else {
            const auto P = glm::perspectiveRH_NO(fovY, aspect, znear, zfar);
            return P * V;
        }
    }
}
jaeng::math::AABB PerspectiveCamera::getViewedVolume() const
{
    return {};
}

