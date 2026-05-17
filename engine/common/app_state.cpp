#include "app_state.h"

namespace jaeng {

AppStateMachine::AppStateMachine(platform::IApplication& app)
    : app_(app) {
}

AppStateMachine::~AppStateMachine() {
    // Exit all states
    while (!states_.empty()) {
        states_.back()->onExit(app_);
        states_.pop_back();
    }
}

void AppStateMachine::changeState(std::unique_ptr<IAppState> newState) {
    pendingTransition_ = TransitionType::Change;
    pendingState_ = std::move(newState);
}

void AppStateMachine::pushState(std::unique_ptr<IAppState> newState) {
    pendingTransition_ = TransitionType::Push;
    pendingState_ = std::move(newState);
}

void AppStateMachine::popState() {
    pendingTransition_ = TransitionType::Pop;
}

void AppStateMachine::processPendingTransitions() {
    if (pendingTransition_ == TransitionType::None) {
        return;
    }

    if (pendingTransition_ == TransitionType::Change) {
        if (!states_.empty()) {
            states_.back()->onExit(app_);
            states_.pop_back();
        }
        if (pendingState_) {
            pendingState_->onEnter(app_);
            states_.push_back(std::move(pendingState_));
        }
    } else if (pendingTransition_ == TransitionType::Push) {
        if (pendingState_) {
            pendingState_->onEnter(app_);
            states_.push_back(std::move(pendingState_));
        }
    } else if (pendingTransition_ == TransitionType::Pop) {
        if (!states_.empty()) {
            states_.back()->onExit(app_);
            states_.pop_back();
        }
    }

    pendingTransition_ = TransitionType::None;
    pendingState_ = nullptr;
}

void AppStateMachine::tick(float dt) {
    processPendingTransitions();

    if (!states_.empty()) {
        states_.back()->tick(app_, dt);
    }
}

void AppStateMachine::extract(std::vector<RenderCommand>& outQueue) {
    if (!states_.empty()) {
        states_.back()->extract(app_, outQueue);
    }
}

void AppStateMachine::render(const std::vector<RenderCommand>& inQueue, bool hasNewState, RenderGraph& graph, TextureHandle backbuffer, TextureHandle depthbuffer) {
    if (!states_.empty()) {
        states_.back()->render(app_, inQueue, hasNewState, graph, backbuffer, depthbuffer);
    }
}

void AppStateMachine::onEvent(const platform::Event& ev) {
    if (!states_.empty()) {
        states_.back()->onEvent(app_, ev);
    }
}

} // namespace jaeng