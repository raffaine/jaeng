#pragma once

#include "icamera.h"
#include "entity/entity.h"

namespace jaeng {

class PerspectiveCamera : public ICamera {
public:
    PerspectiveCamera(EntityManager& ecs, EntityID entity);

    glm::mat4 getViewProj() const override;
    math::AABB getViewedVolume() const override;
    math::Ray  getRay(float x, float y) const override;

    // Interaction "Intent" methods
    void movePlanar(glm::vec3 direction); 
    void moveVertical(float delta);
    void rotate(glm::vec2 mouseDelta);
    void setZoom(float delta);

    void setFov(float fovDeg);
    float getFov() const;

    // Called during extraction phase to cache the view projection for the render thread
    void extractState();

private:
    EntityManager& ecs;
    EntityID entity;
    glm::mat4 cachedViewProj{1.0f};
};

} // namespace jaeng
