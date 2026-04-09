#include "ui.h"
#include "fontsys.h"
#include "scene/ipartition.h"
#include <vector>
#include <algorithm>

namespace jaeng {

void UILayoutSystem::update(EntityManager& ecs, float screenW, float screenH) {
    std::vector<EntityID> stack;
    stack.reserve(256);

    // Find roots
    const auto& rects = ecs.getAllEntities<RectTransform>();
    for (auto e : rects) {
        auto* rel = ecs.getComponent<Relationship>(e);
        if (!rel || rel->parent == static_cast<EntityID>(-1)) {
            stack.push_back(e);
        }
    }

    struct Node {
        EntityID entity;
        RectTransform::WorldRect parentRect;
    };
    std::vector<Node> workStack;
    for (auto e : stack) {
        workStack.push_back({e, {0.0f, 0.0f, screenW, screenH}});
    }

    while (!workStack.empty()) {
        Node curr = workStack.back();
        workStack.pop_back();

        auto* rt = ecs.getComponent<RectTransform>(curr.entity);
        if (!rt) continue;

        // Calculate anchored position
        float parentX = curr.parentRect.x;
        float parentY = curr.parentRect.y;
        float parentW = curr.parentRect.w;
        float parentH = curr.parentRect.h;

        // Simple anchoring: use the center of the anchors as the anchor point.
        float anchorCenterX = parentX + parentW * (rt->anchorMin.x + rt->anchorMax.x) * 0.5f;
        float anchorCenterY = parentY + parentH * (rt->anchorMin.y + rt->anchorMax.y) * 0.5f;

        float finalW = rt->size.x;
        float finalH = rt->size.y;
        
        // Pivot offsets the rect.
        float pivotOffsetX = finalW * rt->pivot.x;
        float pivotOffsetY = finalH * rt->pivot.y;

        rt->worldRect.x = anchorCenterX + rt->position.x - pivotOffsetX;
        rt->worldRect.y = anchorCenterY + rt->position.y - pivotOffsetY;
        rt->worldRect.w = finalW;
        rt->worldRect.h = finalH;

        // Push children
        auto* rel = ecs.getComponent<Relationship>(curr.entity);
        if (rel) {
            EntityID child = rel->firstChild;
            while (child != static_cast<EntityID>(-1)) {
                workStack.push_back({child, rt->worldRect});
                auto* childRel = ecs.getComponent<Relationship>(child);
                child = childRel ? childRel->nextSibling : static_cast<EntityID>(-1);
            }
        }
    }
}

void UIInteractionSystem::update(EntityManager& ecs, float mouseX, float mouseY, bool isLeftMouseDown, bool& outInputConsumed) {
    outInputConsumed = false;

    struct UIElement {
        EntityID id;
        RectTransform* rt;
        UIInteractable* interactable;
    };
    std::vector<UIElement> elements;
    
    const auto& entities = ecs.getAllEntities<UIInteractable>();
    for (auto e : entities) {
        auto* rt = ecs.getComponent<RectTransform>(e);
        auto* interactable = ecs.getComponent<UIInteractable>(e);
        if (rt && interactable) {
            elements.push_back({e, rt, interactable});
        }
    }

    // Sort by zIndex descending (highest zIndex gets input first)
    std::sort(elements.begin(), elements.end(), [](const UIElement& a, const UIElement& b) {
        return a.rt->zIndex > b.rt->zIndex;
    });

    for (auto& el : elements) {
        bool wasHovered = el.interactable->isHovered;
        bool wasPressed = el.interactable->isPressed;

        el.interactable->isHovered = false;
        
        if (outInputConsumed) {
            el.interactable->isPressed = false;
            if (wasHovered && el.interactable->onHover) el.interactable->onHover(false);
            continue;
        }

        // Check intersection
        float x = el.rt->worldRect.x;
        float y = el.rt->worldRect.y;
        float w = el.rt->worldRect.w;
        float h = el.rt->worldRect.h;

        if (mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h) {
            el.interactable->isHovered = true;
            outInputConsumed = true; // Consume input

            if (isLeftMouseDown) {
                el.interactable->isPressed = true;
            } else {
                el.interactable->isPressed = false;
                // Click happens on MouseUp while hovered
                if (wasPressed && el.interactable->onClick) {
                    el.interactable->onClick();
                }
            }
        } else {
            el.interactable->isPressed = false;
        }

        // Hover callbacks
        if (!wasHovered && el.interactable->isHovered) {
            if (el.interactable->onHover) el.interactable->onHover(true);
        } else if (wasHovered && !el.interactable->isHovered) {
            if (el.interactable->onHover) el.interactable->onHover(false);
        }
    }
}

void UIRenderSystem::extract(EntityManager& ecs, IFontSystem& fontSys, std::vector<RenderCommand>& outCommands) {
    const auto& entities = ecs.getAllEntities<RectTransform>();
    
    for (auto e : entities) {
        auto* rt = ecs.getComponent<RectTransform>(e);
        if (!rt) continue;

        MeshHandle quadMesh = 0;
        MaterialHandle uiMat = 0;
        BufferHandle cb = 0;

        if (auto* mc = ecs.getComponent<MeshComponent>(e)) quadMesh = mc->handle;
        if (auto* matc = ecs.getComponent<MaterialComponent>(e)) uiMat = matc->handle;
        if (auto* bc = ecs.getComponent<BufferComponent>(e)) cb = bc->handle;

        // Render Background (UIRenderable)
        if (auto* ur = ecs.getComponent<UIRenderable>(e)) {
            RenderCommand cmd;
            cmd.type = RenderCommandType::UpdateUI;
            cmd.uiProxy = UIRenderProxy{
                static_cast<uint32_t>(e),
                rt->worldRect.x, rt->worldRect.y, rt->worldRect.w, rt->worldRect.h,
                rt->zIndex,
                ur->color,
                quadMesh,
                uiMat,
                cb,
                {0.0f, 0.0f, 1.0f, 1.0f},
                ur->textureHandle
            };
            outCommands.push_back(cmd);
        }

        // Render Text (UIText)
        if (auto* ut = ecs.getComponent<UIText>(e)) {
            auto fontRes = fontSys.getFont(ut->fontHandle);
            if (fontRes.hasValue()) {
                const auto* fontData = std::move(fontRes).orValue(nullptr);
                if (!fontData) continue;

                float fontScale = ut->fontSize / fontData->pixelHeight;
                float startX = rt->worldRect.x;
                float x = startX;
                float y = rt->worldRect.y + fontData->ascent * fontScale;
                float lineHeight = (fontData->ascent - fontData->descent + fontData->lineGap) * fontScale;

                for (size_t i = 0; i < ut->text.size(); ++i) {
                    char c = ut->text[i];
                    if (c == '\n') {
                        x = startX;
                        y += lineHeight;
                        continue;
                    }
                    if (c >= 32 && c < 128) {
                        const auto& glyph = fontData->cdata[c - 32];
                        
                        float gx = x + glyph.xoff * fontScale;
                        float gy = y + glyph.yoff * fontScale;
                        float gw = (glyph.x1 - glyph.x0) * fontScale;
                        float gh = (glyph.y1 - glyph.y0) * fontScale;

                        float u0 = (float)glyph.x0 / fontData->atlasSize;
                        float v0 = (float)glyph.y0 / fontData->atlasSize;
                        float u1 = (float)glyph.x1 / fontData->atlasSize;
                        float v1 = (float)glyph.y1 / fontData->atlasSize;

                        RenderCommand cmd;
                        cmd.type = RenderCommandType::UpdateUI;
                        cmd.uiProxy = UIRenderProxy{
                            (static_cast<uint32_t>(e) << 16) | (static_cast<uint32_t>(i) & 0xFFFF),
                            gx, gy, gw, gh,
                            rt->zIndex + 1,
                            ut->color,
                            quadMesh,
                            uiMat,
                            cb,
                            {u0, v0, u1 - u0, v1 - v0},
                            fontData->texture
                        };
                        outCommands.push_back(cmd);

                        x += glyph.xadvance * fontScale;
                    }
                }
            }
        }
    }
}

UIBuilder::UIBuilder(EntityManager& ecs, MeshHandle defaultMesh, MaterialHandle defaultMat, RendererAPI* renderer) 
    : ecs_(ecs), defaultMesh_(defaultMesh), defaultMat_(defaultMat), renderer_(renderer) {}

UIBuilder& UIBuilder::begin(const std::string& name, EntityID* outEntity) {
    EntityID e = ecs_.createEntity();
    if (!stack_.empty()) {
        ecs_.attachEntity(e, stack_.back());
    }
    current_ = e;
    stack_.push_back(e);
    ecs_.addComponent<RectTransform>(current_);

    // Automatically add rendering components if defaults are provided
    if (defaultMesh_) ecs_.addComponent<MeshComponent>(current_) = {defaultMesh_};
    if (defaultMat_) ecs_.addComponent<MaterialComponent>(current_) = {defaultMat_};
    if (renderer_) {
        BufferDesc cbDesc{ .size_bytes = 96, .usage = BufferUsage_Uniform };
        ecs_.addComponent<BufferComponent>(current_) = {renderer_->create_buffer(&cbDesc, nullptr)};
    }

    if (outEntity) *outEntity = e;
    return *this;
}

UIBuilder& UIBuilder::end() {
    if (!stack_.empty()) {
        stack_.pop_back();
        current_ = stack_.empty() ? static_cast<EntityID>(-1) : stack_.back();
    }
    return *this;
}

UIBuilder& UIBuilder::withRect(glm::vec2 size, glm::vec2 pos) {
    auto* rt = ecs_.getComponent<RectTransform>(current_);
    if (rt) {
        rt->size = size;
        rt->position = pos;
    }
    return *this;
}

UIBuilder& UIBuilder::withAnchors(glm::vec2 min, glm::vec2 max) {
    auto* rt = ecs_.getComponent<RectTransform>(current_);
    if (rt) {
        rt->anchorMin = min;
        rt->anchorMax = max;
    }
    return *this;
}

UIBuilder& UIBuilder::withPivot(glm::vec2 pivot) {
    auto* rt = ecs_.getComponent<RectTransform>(current_);
    if (rt) {
        rt->pivot = pivot;
    }
    return *this;
}

UIBuilder& UIBuilder::withZIndex(int32_t zIndex) {
    auto* rt = ecs_.getComponent<RectTransform>(current_);
    if (rt) {
        rt->zIndex = zIndex;
    }
    return *this;
}

UIBuilder& UIBuilder::withColor(glm::vec4 color) {
    auto* ur = ecs_.getComponent<UIRenderable>(current_);
    if (!ur) ur = &ecs_.addComponent<UIRenderable>(current_);
    ur->color = color;
    return *this;
}

UIBuilder& UIBuilder::withTexture(uint32_t textureHandle) {
    auto* ur = ecs_.getComponent<UIRenderable>(current_);
    if (!ur) ur = &ecs_.addComponent<UIRenderable>(current_);
    ur->textureHandle = textureHandle;
    return *this;
}

UIBuilder& UIBuilder::withText(const std::string& text, float fontSize, uint32_t fontHandle, glm::vec4 color) {
    auto& ut = ecs_.addComponent<UIText>(current_);
    ut.text = text;
    ut.fontSize = fontSize;
    ut.fontHandle = fontHandle;
    ut.color = color;
    return *this;
}

UIBuilder& UIBuilder::withMaterial(MaterialHandle handle) {
    ecs_.addComponent<MaterialComponent>(current_) = {handle};
    return *this;
}

UIBuilder& UIBuilder::withMesh(MeshHandle handle) {
    ecs_.addComponent<MeshComponent>(current_) = {handle};
    return *this;
}

UIBuilder& UIBuilder::withBuffer(BufferHandle handle) {
    ecs_.addComponent<BufferComponent>(current_) = {handle};
    return *this;
}

UIBuilder& UIBuilder::onClick(std::function<void()> callback) {
    auto* ui = ecs_.getComponent<UIInteractable>(current_);
    if (!ui) ui = &ecs_.addComponent<UIInteractable>(current_);
    ui->onClick = std::move(callback);
    return *this;
}

UIBuilder& UIBuilder::onHover(std::function<void(bool)> callback) {
    auto* ui = ecs_.getComponent<UIInteractable>(current_);
    if (!ui) ui = &ecs_.addComponent<UIInteractable>(current_);
    ui->onHover = std::move(callback);
    return *this;
}

} // namespace jaeng
