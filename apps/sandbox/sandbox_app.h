#pragma once

#include "platform/public/platform_api.h"
#include "animation/animation.h"

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
    void extract_render_state(std::vector<RenderCommand>& outQueue) override;
    void render(const std::vector<RenderCommand>& inQueue, bool hasNewState, RenderGraph& graph, TextureHandle backbuffer, TextureHandle depthbuffer) override;

private:
    void setupResources();
    void setupEntities();
    void setupAnimation();

    // Test resources
    std::unique_ptr<IFileManager::SubscriptionT> materialSub_;
    BufferHandle cbFrame_ = 0;

    // Simulation State
    std::vector<EntityID> testEntities_;
    float simTime_ = 0.0f;

    // Animation Test
    std::unique_ptr<jaeng::AnimationClip> testClip_;
};
