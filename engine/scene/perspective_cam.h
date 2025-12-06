#pragma once

#include "icamera.h"

class PerspectiveCamera : public ICamera {
public:
    PerspectiveCamera(float aspect, float fovDeg = 60.0f);

    glm::mat4 getViewProj() const override;
    jaeng::math::AABB getViewedVolume() const override;

private:
    glm::vec3 position;
    glm::vec3 target;
    glm::vec3 up;
    float fovY;
    float aspect;
    float znear;
    float zfar;
};
