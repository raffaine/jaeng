#pragma once

#include "platform/public/platform_api.h"
#include "render/frontend/renderer.h"
#include "render/graph/render_graph.h"
#include "material/imaterialsys.h"
#include "mesh/imeshsys.h"
#include "scene/scene.h"
#include "storage/ifstorage.h"
#include "entity/entity.h"

class SandboxApp : public jaeng::platform::IApplication {
public:
    SandboxApp(jaeng::platform::IPlatform& platform);

    bool init() override;
    void update() override;
    void on_event(const jaeng::platform::Event& ev) override;
    void shutdown() override;
    bool should_close() const override { return shouldClose_; }

private:
    void setupResources();
    void setupEntities();

    jaeng::platform::IPlatform& platform_;
    std::unique_ptr<jaeng::platform::IWindow> window_;
    Renderer renderer_;
    SwapchainHandle swap_ = 0;
    std::shared_ptr<IFileManager> fileMan_;
    std::shared_ptr<EntityManager> entityMan_;
    std::shared_ptr<IMaterialSystem> matSys_;
    std::shared_ptr<IMeshSystem> meshSys_;
    std::unique_ptr<SceneManager> sceneMan_;
    std::unique_ptr<IFileManager::SubscriptionT> materialSub_;
    bool shouldClose_ = false;
};
