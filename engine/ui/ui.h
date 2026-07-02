#pragma once

#include "entity/entity.h"
#include "render/public/renderer_api.h"
#include "material/imaterialsys.h"
#include "mesh/imeshsys.h"
#include "common/math/math.h"
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
    jaeng::math::vec2 position{0.0f}; // Local position relative to pivot
    jaeng::math::vec2 size{100.0f, 100.0f};
    jaeng::math::vec2 anchorMin{0.5f, 0.5f}; // Normalized [0, 1] relative to parent
    jaeng::math::vec2 anchorMax{0.5f, 0.5f};
    jaeng::math::vec2 pivot{0.5f, 0.5f}; // Normalized [0, 1] pivot point of this rect
    int32_t zIndex = 0; // Higher renders on top

    // Computed absolute screen coordinates (updated by UILayoutSystem)
    struct WorldRect {
        float x, y; // Top-left corner
        float w, h;
    } worldRect;

    jaeng::math::vec4 clipRect{0.0f, 0.0f, -1.0f, -1.0f};
};

struct UIScrollComponent {
    jaeng::math::vec2 scrollOffset{0.0f};
    jaeng::math::vec2 contentSize{0.0f};
    bool showScrollbarVertical = false;
    bool showScrollbarHorizontal = false;
};

struct UIRenderable {
    jaeng::math::vec4 color{1.0f};
    uint32_t textureHandle = 0; // 0 means untextured
    jaeng::math::vec4 uvRect{0.0f, 0.0f, 1.0f, 1.0f}; // UV offset {u_min, v_min, u_width, v_height}
};

struct UIText {
    enum class Alignment { Left, Center, Right };

    std::string text;
    uint32_t fontHandle = 0;
    jaeng::math::vec4 color{1.0f};
    float fontSize = 32.0f;
    Alignment alignment = Alignment::Left;
};

struct UIInteractable {
    bool isHovered = false;
    bool isPressed = false;
    std::function<void()> onClick;
    std::function<void(bool)> onHover;
};

struct UIVerticalLayout {
    float spacing = 0.0f;
    float padding = 0.0f; // Simplified: uniform padding for now
};

struct UIHorizontalLayout {
    float spacing = 0.0f;
    float padding = 0.0f;
};

struct UITween {
    enum class Property { PositionX, PositionY, SizeX, SizeY, ColorAlpha };
    enum class Easing { Linear, EaseInOut };

    Property targetProperty = Property::PositionX;
    float startValue = 0.0f;
    float endValue = 0.0f;
    float duration = 1.0f;
    float currentTime = 0.0f;
    Easing easing = Easing::Linear;
    bool isPlaying = false;
    bool isLooping = false;
    bool isPingPong = false;
    bool forwards = true;
};

class UILayoutSystem {
public:
    static void update(EntityManager& ecs, float screenW, float screenH);
};

class UIInteractionSystem {
public:
    static void update(EntityManager& ecs, float mouseX, float mouseY, bool isLeftMouseDown, float scrollDeltaX, float scrollDeltaY, bool& outInputConsumed);
};

/**
 * @brief System responsible for generating low-level render proxies from high-level UI components.
 * This encapsulates the logic for glyph layout, quad positioning, etc.
 */
class UIRenderSystem {
public:
    static void extract(EntityManager& ecs, IFontSystem& fontSys, std::vector<RenderCommand>& outCommands);
};

class UITweenSystem {
public:
    static void update(EntityManager& ecs, float dt);
};

/**
 * @brief Fluent API for building UI hierarchies.
 */
class UIBuilder {
public:
    UIBuilder(EntityManager& ecs, MeshHandle defaultMesh = 0, MaterialHandle defaultMat = 0, RendererAPI* renderer = nullptr);

    UIBuilder& begin(const std::string& name = "UIEntity", EntityID* outEntity = nullptr);
    UIBuilder& end();

    UIBuilder& withRect(jaeng::math::vec2 size, jaeng::math::vec2 pos = {0,0});
    UIBuilder& withAnchors(jaeng::math::vec2 min, jaeng::math::vec2 max);
    UIBuilder& withPivot(jaeng::math::vec2 pivot);
    UIBuilder& withZIndex(int32_t zIndex);

    UIBuilder& withColor(jaeng::math::vec4 color);
    UIBuilder& withTexture(uint32_t textureHandle);
    UIBuilder& withUVRect(jaeng::math::vec4 uvRect);
    UIBuilder& withText(const std::string& text, float fontSize, uint32_t fontHandle, jaeng::math::vec4 color = {1,1,1,1}, UIText::Alignment alignment = UIText::Alignment::Left);
    
    UIBuilder& withMaterial(MaterialHandle handle);
    UIBuilder& withMesh(MeshHandle handle);
    UIBuilder& withBuffer(BufferHandle handle);

    UIBuilder& withVerticalLayout(float spacing, float padding = 0.0f);
    UIBuilder& withHorizontalLayout(float spacing, float padding = 0.0f);
    UIBuilder& withTween(UITween::Property prop, float start, float end, float duration, UITween::Easing easing = UITween::Easing::Linear, bool isLooping = false, bool isPingPong = false);

    UIBuilder& beginHorizontalLayout(const std::string& name, float spacing = 0.0f, float padding = 0.0f);
    UIBuilder& endHorizontalLayout();

    UIBuilder& beginScrollContainer(const std::string& name, bool vertical = true, bool horizontal = false);
    UIBuilder& endScrollContainer();

    template<typename T>
    UIBuilder& widget(const T& w) {
        w.build(*this);
        // Assuming the widget's build() calls b.begin() and b.end(),
        // current_ is restored to the parent.
        // If we want to chain methods like .withTween *after* .widget(),
        // we can't do it because the entity is already popped.
        // The user must use the widget's struct properties (like we added in widgets.h).
        return *this;
    }

    UIBuilder& onClick(std::function<void()> callback);
    UIBuilder& onHover(std::function<void(bool)> callback);

    EntityID getCurrent() const { return current_; }
    EntityManager& get_ecs() { return ecs_; }

private:
    EntityManager& ecs_;
    MeshHandle defaultMesh_;
    MaterialHandle defaultMat_;
    RendererAPI* renderer_;
    EntityID current_ = static_cast<EntityID>(-1);
    std::vector<EntityID> stack_;
};

} // namespace jaeng
