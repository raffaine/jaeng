#include "perspective_cam.h"
#include <glm/gtc/matrix_transform.hpp>

PerspectiveCamera::PerspectiveCamera(float aspect, float fovDeg) 
    : aspect(aspect)
    , fovY(glm::radians(fovDeg))
    , znear(0.1f)
    , zfar(100.0f)
{}

glm::mat4 PerspectiveCamera::getViewProj() const 
{
    const auto V = glm::lookAt(position, target, up);
    const auto P = glm::perspective(fovY, aspect, znear, zfar);
    return V * P;
}
jaeng::math::AABB PerspectiveCamera::getViewedVolume() const
{
    return {};
}

