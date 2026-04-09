#pragma once

#include "platform/public/platform_api.h"
#include "animation/animation.h"
#include "material/imaterialsys.h"

#include <vector>
#include <memory>

class SandboxApp : public jaeng::platform::IApplication {
public:
    SandboxApp(jaeng::platform::IPlatform& platform);

    bool app_init() override;
    void app_on_event(const jaeng::platform::Event& ev) override;
    void app_shutdown() override;

protected:
    void tick(float dt) override;
    void extract_render_state(std::vector<jaeng::RenderCommand>& outQueue) override;
    void render(const std::vector<jaeng::RenderCommand>& inQueue, bool hasNewState, jaeng::RenderGraph& graph, TextureHandle backbuffer, TextureHandle depthbuffer) override;

private:
    void setupResources();
    void setupEntities();
    void setupAnimation();

    struct InputState {
        bool keys[256] = {false};
        jaeng::platform::MousePos mousePos;
        jaeng::platform::MousePos lastMousePos;
        jaeng::platform::MousePos lastLookMousePos;
        bool mouseButtons[3] = {false}; // 0: Left, 1: Middle, 2: Right
        float mouseScroll = 0.0f;
    } inputState_;

    struct SelectionState {
        jaeng::EntityID selectedEntity = static_cast<jaeng::EntityID>(-1);
        glm::vec4 originalColor = glm::vec4(1.0f);
    } selectionState_;

    bool isLooking_ = false;

    void updateCamera(float dt);
    void handleSelection();
    jaeng::math::Ray getRayFromMouse() const;

    // Test resources
    std::unique_ptr<IFileManager::SubscriptionT> materialSub_;
    BufferHandle cbFrame_ = 0;

    // Simulation State
    std::vector<jaeng::EntityID> testEntities_;
    float simTime_ = 0.0f;
    jaeng::EntityID uiTextEntity_ = static_cast<jaeng::EntityID>(-1);
    jaeng::MaterialHandle uiMaterial_ = 0;
    jaeng::FontHandle defaultFont_ = 0;

    // Animation Test
    std::unique_ptr<jaeng::AnimationClip> testClip_;
};
