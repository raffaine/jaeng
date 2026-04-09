#pragma once

#include "entity/entity.h"
#include "render/public/renderer_api.h"
#include "material/imaterialsys.h"
#include "mesh/imeshsys.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace jaeng {

struct UIRenderProxy;
struct RenderCommand;
class IFontSystem;

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

struct UIText {
    std::string text;
    uint32_t fontHandle = 0;
    glm::vec4 color{1.0f};
    float fontSize = 32.0f;
};

struct UIInteractable {
    bool isHovered = false;
    bool isPressed = false;
    std::function<void()> onClick;
    std::function<void(bool)> onHover;
};

class UILayoutSystem {
public:
    static void update(EntityManager& ecs, float screenW, float screenH);
};

class UIInteractionSystem {
public:
    static void update(EntityManager& ecs, float mouseX, float mouseY, bool isLeftMouseDown, bool& outInputConsumed);
};

/**
 * @brief System responsible for generating low-level render proxies from high-level UI components.
 * This encapsulates the logic for glyph layout, quad positioning, etc.
 */
class UIRenderSystem {
public:
    static void extract(EntityManager& ecs, IFontSystem& fontSys, std::vector<RenderCommand>& outCommands);
};

/**
 * @brief Fluent API for building UI hierarchies.
 */
class UIBuilder {
public:
    UIBuilder(EntityManager& ecs, MeshHandle defaultMesh = 0, MaterialHandle defaultMat = 0, RendererAPI* renderer = nullptr);

    UIBuilder& begin(const std::string& name = "UIEntity", EntityID* outEntity = nullptr);
    UIBuilder& end();

    UIBuilder& withRect(glm::vec2 size, glm::vec2 pos = {0,0});
    UIBuilder& withAnchors(glm::vec2 min, glm::vec2 max);
    UIBuilder& withPivot(glm::vec2 pivot);
    UIBuilder& withZIndex(int32_t zIndex);

    UIBuilder& withColor(glm::vec4 color);
    UIBuilder& withTexture(uint32_t textureHandle);
    UIBuilder& withText(const std::string& text, float fontSize, uint32_t fontHandle, glm::vec4 color = {1,1,1,1});
    
    UIBuilder& withMaterial(MaterialHandle handle);
    UIBuilder& withMesh(MeshHandle handle);
    UIBuilder& withBuffer(BufferHandle handle);

    UIBuilder& onClick(std::function<void()> callback);
    UIBuilder& onHover(std::function<void(bool)> callback);

    EntityID getCurrent() const { return current_; }

private:
    EntityManager& ecs_;
    MeshHandle defaultMesh_;
    MaterialHandle defaultMat_;
    RendererAPI* renderer_;
    EntityID current_ = static_cast<EntityID>(-1);
    std::vector<EntityID> stack_;
};

} // namespace jaeng
