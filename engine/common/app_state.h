#pragma once

#include <memory>
#include <vector>
#include "platform/public/platform_api.h"

namespace jaeng {

// Forward declarations
class AppStateMachine;

class IAppState {
public:
    virtual ~IAppState() = default;

    virtual void onEnter(platform::IApplication& app) {}
    virtual void onExit(platform::IApplication& app) {}
    
    // Lifecycle events delegated from IApplication
    virtual void onEvent(platform::IApplication& app, const platform::Event& ev) {}
    virtual void tick(platform::IApplication& app, float dt) {}
    virtual void extract(platform::IApplication& app, std::vector<RenderCommand>& outQueue) {}
    virtual void render(platform::IApplication& app, const std::vector<RenderCommand>& inQueue, bool hasNewState, RenderGraph& graph, TextureHandle backbuffer, TextureHandle depthbuffer) {}
};

class AppStateMachine {
public:
    AppStateMachine(platform::IApplication& app);
    ~AppStateMachine();

    // State transition API (deferred until next tick)
    void changeState(std::unique_ptr<IAppState> newState);
    void pushState(std::unique_ptr<IAppState> newState);
    void popState();

    // Immediate Lifecycle delegation
    void tick(float dt);
    void extract(std::vector<RenderCommand>& outQueue);
    void render(const std::vector<RenderCommand>& inQueue, bool hasNewState, RenderGraph& graph, TextureHandle backbuffer, TextureHandle depthbuffer);
    void onEvent(const platform::Event& ev);

private:
    void processPendingTransitions();

    platform::IApplication& app_;
    std::vector<std::unique_ptr<IAppState>> states_;

    enum class TransitionType {
        None,
        Change,
        Push,
        Pop
    };
    
    TransitionType pendingTransition_ = TransitionType::None;
    std::unique_ptr<IAppState> pendingState_;
};

} // namespace jaeng