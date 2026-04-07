#include "ui.h"
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
        el.interactable->isHovered = false;
        
        if (outInputConsumed) {
            el.interactable->isPressed = false;
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
            }
        } else {
            el.interactable->isPressed = false;
        }
    }
}

} // namespace jaeng
