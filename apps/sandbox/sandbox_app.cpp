#include "sandbox_app.h"
#include "mesh_utils.h"
#if defined(JAENG_WIN32) && !defined(JAENG_USE_VULKAN)
#include "pix3.h"
#endif

#include "storage/win/filestorage.h"
#include "material/materialsys.h"
#include "mesh/meshsys.h"
#include "scene/grid_partition.h"
#include "scene/perspective_cam.h"
#include "entity/entity.h"
#include "entity/transform_sys.h"

#if defined(JAENG_WIN32) && !defined(JAENG_USE_VULKAN)
#include "basic_reflect.h"
#else
#include "basic.h"
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <cstring>

#include <thread>
#include <chrono>

using namespace jaeng;
using namespace jaeng::platform;

// Define the backend-specific extensions and files
#if defined(JAENG_WIN32) && !defined(JAENG_USE_VULKAN)
#define SHADER_EXT ".dxil"
#define REFLECT_FILE "basic_reflect.json"
#else
#define SHADER_EXT ".spv"
#define REFLECT_FILE "basic.json"
#endif

// Compile-time string concatenation to build the JSON dynamically
static const char* materialFileData = R"({
  "name": "CheckerboardMaterial",
  "shader": {
    "vertex": ")" JAENG_SHADER_DIR "/compiled/basic_vs" SHADER_EXT R"(",
    "pixel": ")" JAENG_SHADER_DIR "/compiled/basic_ps" SHADER_EXT R"(",
    "reflection": ")" JAENG_SHADER_DIR "/include/" REFLECT_FILE R"("
  },
  "textures": [
    {
      "path": "/mem/checker.raw",
      "width": 256,
      "height": 256,
      "sampler": {
        "filter": "linear",
        "addressModeU": "wrap",
        "addressModeV": "wrap"
      }
    }
  ],
  "parameters": {
    "color": [1.0, 1.0, 1.0, 1.0],
    "roughness": 0.5,
    "metallic": 0.0
  },
  "constantBuffers": [
    { "name": "CBObject", "size": 64, "binding": 0 },
    { "name": "CBFrame", "size": 64, "binding": 1 },
    { "name": "CBMaterial", "size": 256, "binding": 2 }
  ],
  "pipelineStates": {
    "blend": { "enabled": false, "srcFactor": "one", "dstFactor": "zero" },
    "rasterizer": { "cullMode": "back", "fillMode": "solid" },
    "depthStencil": { "depthTest": true, "depthWrite": true }
  }
})";

SandboxApp::SandboxApp(IPlatform& platform) 
    : platform_(platform) 
    , shouldClose_(false)
{}

bool SandboxApp::init() {
    platform_.set_event_callback([this](const Event& ev) {
        this->on_event(ev);
    });

    auto windowResult = platform_.create_window({"jaeng Sandbox", 1280, 720});
    if (windowResult.hasError()) {
        platform_.show_message_box("Error", "Failed to create window.", MessageBoxType::Error);
        return false;
    }
    window_ = std::move(windowResult).logError().value();

#if defined(JAENG_WIN32) && !defined(JAENG_USE_VULKAN)
    GfxBackend backend = GfxBackend::D3D12;
#else
    GfxBackend backend = GfxBackend::Vulkan;
#endif

    if (!renderer_.initialize(backend, window_->get_native_handle(), platform_.get_native_display_handle(), 3)) {
        platform_.show_message_box("Error", "Failed to initialize renderer.", MessageBoxType::Error);
        return false;
    }

    DepthStencilDesc depthDesc{.depth_enable = true, .depth_format = TextureFormat::D32F};
    SwapchainDesc    swapDesc{{1280u, 720u}, TextureFormat::BGRA8_UNORM, depthDesc, PresentMode::Fifo};
    swap_ = renderer_->create_swapchain(&swapDesc);
    if (swap_ == 0) {
        return false;
    }

    fileMan_ = std::make_shared<FileManager>();
    fileMan_->initialize().orElse([this](auto) {
        platform_.show_message_box("Warning", "Failed to initialize FileManager. Continuing with limited capacity.", MessageBoxType::Warning);
    });

    setupResources();

    entityMan_ = std::make_shared<EntityManager>();
    matSys_  = std::make_shared<MaterialSystem>(fileMan_, renderer_.gfx);
    meshSys_ = std::make_shared<MeshSystem>(fileMan_, renderer_.gfx);
    sceneMan_ = std::make_unique<SceneManager>(meshSys_, matSys_, renderer_.gfx);

    const auto aspect = 1280.f/720.f;
    auto camera = std::make_unique<PerspectiveCamera>(glm::vec3{3, 3, 3}, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0}, aspect);
    sceneMan_->createScene("Test", std::make_unique<GridPartitioner>(), std::move(camera))
        .orElse([this](auto) -> Scene* {
            platform_.show_message_box("Error", "Failed to create Test Scene.", MessageBoxType::Error);
            return nullptr;
        });

    setupEntities();

    BufferDesc cbDesc{ .size_bytes = 64, .usage = BufferUsage_Uniform };
    cbFrame_ = renderer_->create_buffer(&cbDesc, nullptr);
    if (auto* scene = sceneMan_->getScene("Test")) {
        scene->setCbFrame(cbFrame_);
    }

    return true;
}

void SandboxApp::tick(float dt) {
    simTime_ += dt;

    // Spin the Sun (everything attached will orbit)
    if (auto* t = entityMan_->getComponent<Transform>(testEntities_[0])) {
        t->rotation = glm::angleAxis(simTime_ * 0.5f, glm::vec3(0, 0, 1));
    }

    // Spin Planet 2 (its moon will orbit it, while it orbits the sun)
    if (auto* t = entityMan_->getComponent<Transform>(testEntities_[2])) {
        t->rotation = glm::angleAxis(simTime_ * 2.0f, glm::vec3(0, 0, 1));
    }

    // Process the entire hierarchy into WorldMatrices
    TransformSystem::update(entityMan_);
}

void SandboxApp::extract_render_state() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    renderQueue_.clear(); // Double buffering: wipe the old commands

    const auto& entities = entityMan_->getAllEntities<WorldMatrix>();
    for (auto e : entities) {
        auto* wm = entityMan_->getComponent<WorldMatrix>(e);
        auto* mesh = entityMan_->getComponent<MeshHandle>(e);
        auto* mat = entityMan_->getComponent<MaterialHandle>(e);
        auto* cb = entityMan_->getComponent<BufferHandle>(e);

        if (wm && mesh && mat) {
            // Push an Update command mapping the EntityID directly to the ProxyID
            renderQueue_.push_back({
                RenderCommandType::Update,
                RenderProxy { static_cast<uint32_t>(e), wm->value, *mesh, *mat, cb ? *cb : 0 },
                static_cast<uint32_t>(e)
                });
        }
    }
}

void SandboxApp::render() {
    if (!window_) return;

    // Flush any pending resizes safely on the Render Thread
    renderer_.process_pending_resizes();

    Scene* scene = sceneMan_->getScene("Test");
    if (scene) {
        // Lock the queue to extract needed commands
        std::vector<RenderCommand> localQueue;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            localQueue = std::move(renderQueue_);
        }

        // Process commands outside of lock
        for (const auto& cmd : localQueue) {
            if (cmd.type == RenderCommandType::Update) {
                scene->addOrUpdateProxy(cmd.proxy);
            }
            else if (cmd.type == RenderCommandType::Destroy) {
                scene->removeProxy(cmd.id);
            }
        }

        // Builds the Scene partition and generate draw list
        scene->getPartitioner()->build();
        scene->buildDrawList({});
    }

    renderer_->begin_frame();
    TextureHandle backbuffer = renderer_->get_current_backbuffer(swap_);
    TextureHandle depthbuffer = renderer_->get_depth_buffer(swap_);

    RenderGraph graph;
    if (scene) scene->renderScene(graph, backbuffer, depthbuffer);

    graph.compile();
    graph.execute(*renderer_.gfx, depthbuffer, nullptr);
    renderer_->present(swap_);
    renderer_->end_frame();
}

//void SandboxApp::update() {
//    if (!window_) return;
//
//    // Calculate elapsed time in seconds
//    float time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime_).count();
//
//    // Update transforms for orbit and pulse
//    for (size_t i = 0; i < testEntities_.size(); ++i) {
//        if (auto* transform = entityMan_->getComponent<Transform>(testEntities_[i])) {
//            // 90-degree offset per cube
//            float angle = (time * 1.5f) + (i * glm::half_pi<float>());
//            // Sine wave to make them drift apart and back together
//            float radius = 0.5f + std::sin(time * 2.0f) * 0.25f;
//
//            transform->position.x = std::cos(angle) * radius;
//            transform->position.y = std::sin(angle) * radius;
//
//            // Add a continuous tumble
//            transform->rotation = glm::angleAxis(time * (1.0f + i * 0.2f), glm::normalize(glm::vec3(1, 1, 0)));
//        }
//    }
//    
//    renderer_->begin_frame();
//    TextureHandle backbuffer = renderer_->get_current_backbuffer(swap_);
//    TextureHandle depthbuffer = renderer_->get_depth_buffer(swap_);
//
//    RenderGraph graph;
//    Scene* scene = sceneMan_->getScene("Test");
//    if (scene) {
//        // Rebuild the spatial partitioner so the new positions are registered
//        scene->getPartitioner()->build();
//
//        scene->buildDrawList({});
//        scene->renderScene(graph, backbuffer, depthbuffer);
//        graph.compile();
//        graph.execute(*renderer_.gfx, depthbuffer, nullptr);
//        renderer_->present(swap_);
//        renderer_->end_frame();
//    }
//    std::this_thread::sleep_for(std::chrono::milliseconds(1));
//}

void SandboxApp::on_event(const Event& ev) {
    if (ev.type == Event::Type::WindowResize) {
        if (swap_ > 0 && renderer_.gfx) {
            // Queue the resize to safely handle this on render thread
            renderer_.queue_resize(swap_, ev.resize.width, ev.resize.height);
        }
    } else if (ev.type == Event::Type::WindowClose) {
        shouldClose_ = true;
    }
}

void SandboxApp::shutdown() {
    sceneMan_.reset();
    meshSys_.reset();
    matSys_.reset();
    entityMan_.reset();
    renderer_.shutdown();
}

void SandboxApp::setupResources() {
    const uint32_t W = 256, H = 256, CS = 32;
    std::vector<uint32_t> pixels(W * H);
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            bool on = ((x / CS) ^ (y / CS)) & 1;
            uint8_t c = on ? 255 : 30;
            pixels[y * W + x] = (0xFFu << 24) | (c << 16) | (c << 8) | c;
        }
    }
    fileMan_->registerMemoryFile("/mem/checker.raw", pixels.data(), pixels.size() * sizeof(uint32_t));
    fileMan_->registerMemoryFile("/mem/material-test.json", materialFileData, strlen(materialFileData));

    auto meshRawData = createCubeMeshBinary();
    fileMan_->registerMemoryFile("/mem/mesh-test.raw", meshRawData.data(), meshRawData.size());
}

void SandboxApp::setupEntities() {
    testEntities_.resize(4);
    for(int i=0; i<4; ++i) testEntities_[i] = entityMan_->createEntity();

    if (auto meshHandle = meshSys_->loadMesh("/mem/mesh-test.raw").logError()) {
        for (int i = 0; i < 4; i++) {
            entityMan_->addComponent<MeshHandle>(testEntities_[i]) = meshHandle.value();
            BufferDesc cbDesc{ .size_bytes = 64, .usage = BufferUsage_Uniform };
            entityMan_->addComponent<BufferHandle>(testEntities_[i]) = renderer_->create_buffer(&cbDesc, nullptr);
        }
    }
    if (auto matHandle = matSys_->createMaterial("/mem/material-test.json", &ShaderReflection::vertexLayout, 1, ShaderReflection::inputSemantics).logError()) {
        auto h = matHandle.value();
        for (int i = 0; i < 4; i++) entityMan_->addComponent<MaterialHandle>(testEntities_[i]) = h;
#ifdef JAENG_WIN32
        materialSub_ = fileMan_->track("/mem/material-test.json", [this, h](const FileChangedEvent& e) {
            if (e.change == FileChangedEvent::ChangeType::Modified) {
                matSys_->reloadMaterial(h).orElse([](auto){});
            }
        });
#endif
    }

    entityMan_->addComponent<Transform>(testEntities_[0]) = Transform{ .position = { 0.0f,  0.0f, 0} }; // The Sun
    entityMan_->addComponent<Transform>(testEntities_[1]) = Transform{ .position = { 2.0f,  0.0f, 0}, .scale = {.5, .5, .5} }; // Planet 1
    entityMan_->addComponent<Transform>(testEntities_[2]) = Transform{ .position = {-3.0f,  0.0f, 0}, .scale = {.7, .7, .7} }; // Planet 2
    entityMan_->addComponent<Transform>(testEntities_[3]) = Transform{ .position = { 0.0f,  1.5f, 0}, .scale = {.3, .3, .3} }; // Moon of Planet 2

    // Build the hierarchy
    entityMan_->attachEntity(testEntities_[1], testEntities_[0]); // Planet 1 orbits Sun
    entityMan_->attachEntity(testEntities_[2], testEntities_[0]); // Planet 2 orbits Sun
    entityMan_->attachEntity(testEntities_[3], testEntities_[2]); // Moon orbits Planet 2

    // Run the transform system once to generate the initial matrices
    TransformSystem::update(entityMan_);

    extract_render_state();
}
