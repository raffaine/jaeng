#include "perspective_cam.h"
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <algorithm>

namespace jaeng {

PerspectiveCamera::PerspectiveCamera(EntityManager& ecs, EntityID entity)
    : ecs(ecs), entity(entity)
{
    if (!ecs.getComponent<Transform>(entity)) {
        ecs.addComponent<Transform>(entity);
    }
    if (!ecs.getComponent<CameraComponent>(entity)) {
        ecs.addComponent<CameraComponent>(entity);
    }
    // Ensure WorldMatrix exists so TransformSystem updates it
    if (!ecs.getComponent<WorldMatrix>(entity)) {
        ecs.addComponent<WorldMatrix>(entity);
    }
}

glm::mat4 PerspectiveCamera::getViewProj() const 
{
    auto* t = ecs.getComponent<Transform>(entity);
    auto* c = ecs.getComponent<CameraComponent>(entity);
    if (!t || !c) return glm::mat4(1.0f);

    glm::mat4 worldMatrix;
    if (auto* wm = ecs.getComponent<WorldMatrix>(entity)) {
        worldMatrix = wm->value;
    } else {
        worldMatrix = glm::translate(glm::mat4(1.0f), t->position) * 
                      glm::toMat4(t->rotation) * 
                      glm::scale(glm::mat4(1.0f), t->scale);
    }

    // View matrix is the inverse of the camera's world matrix
    // In LH, inverse(Translate * Rotate) works correctly for View
    glm::mat4 V = glm::inverse(worldMatrix);
    glm::mat4 P = glm::perspectiveLH_ZO(glm::radians(c->fov), c->aspect, c->znear, c->zfar);

    return P * V;
}

math::AABB PerspectiveCamera::getViewedVolume() const
{
    return {};
}

math::Ray PerspectiveCamera::getRay(float x, float y) const
{
    glm::mat4 invVP = glm::inverse(getViewProj());
    
    float nx = 2.0f * x - 1.0f;
    float ny = 1.0f - 2.0f * y;
    
    glm::vec4 nearPoint = invVP * glm::vec4(nx, ny, 0.0f, 1.0f);
    glm::vec4 farPoint  = invVP * glm::vec4(nx, ny, 1.0f, 1.0f);
    
    nearPoint /= nearPoint.w;
    farPoint  /= farPoint.w;
    
    return math::Ray(glm::vec3(nearPoint), glm::vec3(farPoint - nearPoint));
}

void PerspectiveCamera::movePlanar(glm::vec3 direction)
{
    auto* t = ecs.getComponent<Transform>(entity);
    if (!t) return;

    // LH: Forward is Z+
    glm::vec3 forward = t->rotation * glm::vec3(0, 0, 1);
    glm::vec3 right = t->rotation * glm::vec3(1, 0, 0);

    forward.y = 0;
    right.y = 0;
    if (glm::length(forward) > 0.0001f) forward = glm::normalize(forward);
    if (glm::length(right) > 0.0001f) right = glm::normalize(right);

    // direction.z: + is forward (W), - is backward (S)
    // direction.x: + is right (D), - is left (A)
    t->position += (forward * direction.z + right * direction.x);
}

void PerspectiveCamera::moveVertical(float delta)
{
    auto* t = ecs.getComponent<Transform>(entity);
    if (t) t->position.y += delta;
}

void PerspectiveCamera::rotate(glm::vec2 delta)
{
    auto* t = ecs.getComponent<Transform>(entity);
    auto* c = ecs.getComponent<CameraComponent>(entity);
    if (!t || !c) return;

    // Update spherical angles
    c->yaw += delta.x;
    c->pitch = std::clamp(c->pitch + delta.y, -glm::half_pi<float>() + 0.1f, glm::half_pi<float>() - 0.1f);

    // LH Orientation: 
    // Moving mouse RIGHT should increase YAW (rotate around Y axis).
    // Moving mouse DOWN should increase PITCH (rotate around X axis).
    glm::quat qYaw = glm::angleAxis(c->yaw, glm::vec3(0, 1, 0));
    glm::quat qPitch = glm::angleAxis(c->pitch, glm::vec3(1, 0, 0));
    t->rotation = qYaw * qPitch;
}

void PerspectiveCamera::setZoom(float delta)
{
    auto* c = ecs.getComponent<CameraComponent>(entity);
    if (c) {
        c->fov = std::clamp(c->fov + delta, 10.0f, 120.0f);
    }
}

void PerspectiveCamera::setFov(float fovDeg)
{
    auto* c = ecs.getComponent<CameraComponent>(entity);
    if (c) c->fov = fovDeg;
}

float PerspectiveCamera::getFov() const
{
    auto* c = ecs.getComponent<CameraComponent>(entity);
    return c ? c->fov : 60.0f;
}

} // namespace jaeng
