#pragma once

#include "platform/public/platform_api.h"
#include "platform/public/process.h"
#include "common/async/task.h"
#include "common/async/awaiters.h"
#include "animation/animation.h"
#include "material/imaterialsys.h"

#include <vector>
#include <memory>
#include <string>

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
    void setupAnimation();
    void setupUI();

    jaeng::async::FireAndForget setupAsync();
    jaeng::async::Task<void> setupResourcesAsync();
    jaeng::async::Task<void> setupEntitiesAsync();

    void startServer();
    void restartServer();
    void updateServerData();

    jaeng::async::FireAndForget runAsyncTaskTest();
    void runFutureTest();

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
    std::unique_ptr<jaeng::IFileManager::SubscriptionT> materialSub_;
    BufferHandle cbFrame_ = 0;

    // Simulation State
    std::vector<jaeng::EntityID> testEntities_;
    float simTime_ = 0.0f;
    jaeng::EntityID uiTextEntity_ = static_cast<jaeng::EntityID>(-1);
    jaeng::MaterialHandle uiMaterial_ = 0;
    jaeng::FontHandle defaultFont_ = 0;

    // Animation Test
    std::unique_ptr<jaeng::AnimationClip> testClip_;

    // Server management
    std::unique_ptr<jaeng::platform::IProcess> serverProcess_;
    std::string serverTime_ = "Connecting...";
    float serverPollTimer_ = 0.0f;
    jaeng::EntityID serverTextEntity_ = static_cast<jaeng::EntityID>(-1);
};
