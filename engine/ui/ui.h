#pragma once

#include "entity/entity.h"
#include <glm/glm.hpp>
#include <cstdint>

namespace jaeng {

// 2D Rect matching Unity's RectTransform concept.
struct RectTransform {
    glm::vec2 position{0.0f}; // Local position relative to pivot
    glm::vec2 size{100.0f, 100.0f};
    glm::vec2 anchorMin{0.5f, 0.5f}; // Normalized [0, 1] relative to parent
    glm::vec2 anchorMax{0.5f, 0.5f};
    glm::vec2 pivot{0.5f, 0.5f}; // Normalized [0, 1] pivot point of this rect
    int32_t zIndex = 0; // Higher renders on top

    // Computed absolute screen coordinates (updated by UILayoutSystem)
    struct WorldRect {
        float x, y; // Top-left corner
        float w, h;
    } worldRect;
};

struct UIRenderable {
    glm::vec4 color{1.0f};
    uint32_t textureHandle = 0; // 0 means untextured
};

struct UIInteractable {
    bool isHovered = false;
    bool isPressed = false;
};

class UILayoutSystem {
public:
    static void update(EntityManager& ecs, float screenW, float screenH);
};

class UIInteractionSystem {
public:
    static void update(EntityManager& ecs, float mouseX, float mouseY, bool isLeftMouseDown, bool& outInputConsumed);
};

} // namespace jaeng
