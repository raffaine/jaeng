#include "animation/animation.h"
#include "entity/transform_sys.h" 
#include <algorithm>
#include <cmath>

namespace jaeng {

glm::vec3 AnimationTrack::samplePosition(float time) const {
    if (positionKeys.empty()) return glm::vec3(0.0f);
    if (positionKeys.size() == 1) return positionKeys[0].value;
    if (time <= positionKeys.front().time) return positionKeys.front().value;
    if (time >= positionKeys.back().time) return positionKeys.back().value;

    for (size_t i = 0; i < positionKeys.size() - 1; ++i) {
        if (time >= positionKeys[i].time && time < positionKeys[i + 1].time) {
            float t = (time - positionKeys[i].time) / (positionKeys[i + 1].time - positionKeys[i].time);
            return glm::mix(positionKeys[i].value, positionKeys[i + 1].value, t);
        }
    }
    return positionKeys.back().value;
}

glm::quat AnimationTrack::sampleRotation(float time) const {
    if (rotationKeys.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (rotationKeys.size() == 1) return rotationKeys[0].value;
    if (time <= rotationKeys.front().time) return rotationKeys.front().value;
    if (time >= rotationKeys.back().time) return rotationKeys.back().value;

    for (size_t i = 0; i < rotationKeys.size() - 1; ++i) {
        if (time >= rotationKeys[i].time && time < rotationKeys[i + 1].time) {
            float t = (time - rotationKeys[i].time) / (rotationKeys[i + 1].time - rotationKeys[i].time);
            return glm::slerp(rotationKeys[i].value, rotationKeys[i + 1].value, t);
        }
    }
    return rotationKeys.back().value;
}

glm::vec3 AnimationTrack::sampleScale(float time) const {
    if (scaleKeys.empty()) return glm::vec3(1.0f);
    if (scaleKeys.size() == 1) return scaleKeys[0].value;
    if (time <= scaleKeys.front().time) return scaleKeys.front().value;
    if (time >= scaleKeys.back().time) return scaleKeys.back().value;

    for (size_t i = 0; i < scaleKeys.size() - 1; ++i) {
        if (time >= scaleKeys[i].time && time < scaleKeys[i + 1].time) {
            float t = (time - scaleKeys[i].time) / (scaleKeys[i + 1].time - scaleKeys[i].time);
            return glm::mix(scaleKeys[i].value, scaleKeys[i + 1].value, t);
        }
    }
    return scaleKeys.back().value;
}

void AnimationSystem::update(EntityManager& ecs, float dt) {
    const auto& entities = ecs.getAllEntities<Animator>();
    
    if (entities.empty()) {
        return;
    }

    for (EntityID entity : entities) {
        Animator* animator = ecs.getComponent<Animator>(entity);
        if (!animator || !animator->clip || !animator->isPlaying) continue;

        if (animator->clip->duration <= 0.0001f) {
            animator->currentTime = 0.0f;
        } else {
            animator->currentTime += dt;
            if (animator->currentTime > animator->clip->duration) {
                if (animator->loop) {
                    animator->currentTime = std::fmod(animator->currentTime, animator->clip->duration);
                } else {
                    animator->currentTime = animator->clip->duration;
                    animator->isPlaying = false;
                }
            }
        }

        // Apply animation to joints
        for (size_t trackIdx = 0; trackIdx < animator->clip->tracks.size(); ++trackIdx) {
            if (trackIdx >= animator->jointEntities.size()) break;
            
            EntityID jointEntity = animator->jointEntities[trackIdx];
            if (jointEntity == static_cast<EntityID>(-1)) continue;

            Transform* transform = ecs.getComponent<Transform>(jointEntity);
            if (!transform) {
                transform = &ecs.addComponent<Transform>(jointEntity);
            }

            const auto& track = animator->clip->tracks[trackIdx];
            
            if (!track.positionKeys.empty()) {
                transform->position = track.samplePosition(animator->currentTime);
            }
            if (!track.rotationKeys.empty()) {
                transform->rotation = track.sampleRotation(animator->currentTime);
            }
            if (!track.scaleKeys.empty()) {
                transform->scale = track.sampleScale(animator->currentTime);
            }
        }
    }
}

} // namespace jaeng
