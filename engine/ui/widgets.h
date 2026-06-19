#pragma once
#include "ui.h"
#include <string>
#include <functional>

namespace jaeng::ui {

struct UIPanel {
    std::string name = "Panel";
    glm::vec2 size;
    glm::vec2 pos = {0,0};
    glm::vec2 anchorMin = {0,0};
    glm::vec2 anchorMax = {0,0};
    glm::vec2 pivot = {0,0};
    glm::vec4 color = {1,1,1,1};
    int32_t zIndex = 0;
    enum class Layout { None, Vertical, Horizontal } layout = Layout::None;
    float layoutSpacing = 0.0f;
    float layoutPadding = 0.0f;
    std::function<void(UIBuilder&)> onBuild;
    std::function<void(UIBuilder&)> content;

    void build(UIBuilder& b) const {
        b.begin(name)
         .withRect(size, pos)
         .withAnchors(anchorMin, anchorMax)
         .withPivot(pivot)
         .withColor(color);
        if (zIndex != 0) b.withZIndex(zIndex);
        if (layout == Layout::Vertical) b.withVerticalLayout(layoutSpacing, layoutPadding);
        if (layout == Layout::Horizontal) b.withHorizontalLayout(layoutSpacing, layoutPadding);
        
        if (onBuild) onBuild(b);
        if (content) content(b);
        
        b.end();
    }
};

struct UIButton {
    std::string text;
    glm::vec2 size = {140, 40};
    glm::vec2 pos = {0,0};
    glm::vec2 anchorMin = {0,0};
    glm::vec2 anchorMax = {0,0};
    glm::vec2 pivot = {0,0};
    glm::vec4 color = {0.4f, 0.4f, 0.8f, 1.0f};
    glm::vec4 hoverColor = {0.6f, 0.6f, 1.0f, 1.0f};
    int32_t zIndex = 0;
    std::function<void()> onClick;
    uint32_t fontHandle = static_cast<uint32_t>(-1);
    std::function<void(UIBuilder&)> onBuild;

    void build(UIBuilder& b) const {
        b.begin("Button")
         .withRect(size, pos)
         .withAnchors(anchorMin, anchorMax)
         .withPivot(pivot)
         .withColor(color);
         
        EntityID e = b.getCurrent(); // Capture the correct entity ID here
        EntityManager* ecs = &b.get_ecs();
        glm::vec4 c = color;
        glm::vec4 hc = hoverColor;
        
        b.onClick(onClick)
         .onHover([ecs, e, c, hc](bool hovered) {
             if (e == static_cast<EntityID>(-1)) return;
             if (auto* ur = ecs->getComponent<UIRenderable>(e)) {
                 ur->color = hovered ? hc : c;
             }
         });
        
        if (zIndex != 0) b.withZIndex(zIndex);
        if (onBuild) onBuild(b);
        
        if (!text.empty() && fontHandle != static_cast<uint32_t>(-1)) {
            b.begin("Button_Text")
             .withRect(size, {5, 0})
             .withAnchors({0, 0.5f}, {0, 0.5f})
             .withPivot({0, 0.5f})
             .withText(text, 24.0f, fontHandle)
             .end();
        }
        
        b.end();
    }
};

struct UILabel {
    std::string text;
    float fontSize = 24.0f;
    uint32_t fontHandle = static_cast<uint32_t>(-1);
    glm::vec4 color = {1,1,1,1};
    glm::vec2 anchorMin = {0,0};
    glm::vec2 anchorMax = {0,0};
    glm::vec2 pivot = {0,0};
    int32_t zIndex = 0;
    jaeng::EntityID* outEntity = nullptr;
    std::function<void(UIBuilder&)> onBuild;

    void build(UIBuilder& b) const {
        b.begin("Label", outEntity)
         .withAnchors(anchorMin, anchorMax)
         .withPivot(pivot);
        
        if (fontHandle != static_cast<uint32_t>(-1)) {
            b.withText(text, fontSize, fontHandle, color);
        }
        
        if (zIndex != 0) b.withZIndex(zIndex);
        if (onBuild) onBuild(b);
         
        b.end();
    }
};

} // namespace jaeng::ui
