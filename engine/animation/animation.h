#pragma once

#include "entity/entity.h"
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace jaeng {

struct KeyframeV3 {
    float time;
    glm::vec3 value;
};

struct KeyframeQuat {
    float time;
    glm::quat value;
};

struct AnimationTrack {
    std::vector<KeyframeV3> positionKeys;
    std::vector<KeyframeQuat> rotationKeys;
    std::vector<KeyframeV3> scaleKeys;

    glm::vec3 samplePosition(float time) const;
    glm::quat sampleRotation(float time) const;
    glm::vec3 sampleScale(float time) const;
};

struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    std::vector<AnimationTrack> tracks;
};

struct Animator {
    AnimationClip* clip = nullptr;
    float currentTime = 0.0f;
    bool isPlaying = true;
    bool loop = true;
    
    // Maps track index in AnimationClip to EntityID in ECS
    std::vector<EntityID> jointEntities;
};

class AnimationSystem {
public:
    static void update(EntityManager& ecs, float dt);
};

} // namespace jaeng
