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
    sceneMan_->createScene("Test", std::make_unique<GridPartitioner>(entityMan_), std::move(camera))
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
    // Lock the ECS while the simulation updates transforms
    std::lock_guard<std::mutex> lock(ecsMutex_);

    for (size_t i = 0; i < testEntities_.size(); ++i) {
        if (auto* transform = entityMan_->getComponent<Transform>(testEntities_[i])) {
            float angle = (simTime_ * 1.5f) + (i * glm::half_pi<float>());
            float radius = 0.5f + std::sin(simTime_ * 2.0f) * 0.25f;

            transform->position.x = std::cos(angle) * radius;
            transform->position.y = std::sin(angle) * radius;
            transform->rotation = glm::angleAxis(simTime_ * (1.0f + i * 0.2f), glm::normalize(glm::vec3(1, 1, 0)));
        }
    }
    simTime_ += dt;
    stateChanged_ = true;
}

void SandboxApp::extract_render_state() {
    // Left intentionally blank for now. 
    // We will use this in the next refactor to copy the state lock-free.
}

void SandboxApp::render() {
    if (!window_) return;

    // Flush any pending resizes safely on the Render Thread
    renderer_.process_pending_resizes();

    renderer_->begin_frame();
    TextureHandle backbuffer = renderer_->get_current_backbuffer(swap_);
    TextureHandle depthbuffer = renderer_->get_depth_buffer(swap_);

    RenderGraph graph;

    {
        // Lock the ECS ONLY while building the render graph
        std::lock_guard<std::mutex> lock(ecsMutex_);
        Scene* scene = sceneMan_->getScene("Test");
        if (scene) {
            if (stateChanged_) {
                scene->getPartitioner()->build();
                stateChanged_ = false;
            }

            scene->buildDrawList({});
            scene->renderScene(graph, backbuffer, depthbuffer);
        }
    } // ECS IS UNLOCKED HERE. The Sim Thread is free to tick again!

    // The heavy lifting (command buffer recording & execution) happens totally in parallel
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


    entityMan_->addComponent<Transform>(testEntities_[0]) = Transform{.position = {-0.25f, -0.25f, 0}};
    entityMan_->addComponent<Transform>(testEntities_[1]) = Transform{.position = { 0.25f, -0.25f, 0}, .rotation = glm::angleAxis(glm::radians(90.f), glm::vec3{1, 0, 0})};
    entityMan_->addComponent<Transform>(testEntities_[2]) = Transform{.position = {-0.25f,  0.25f, 0}, .rotation = glm::angleAxis(glm::radians(90.f), glm::vec3{0, 1, 0})};
    entityMan_->addComponent<Transform>(testEntities_[3]) = Transform{.position = { 0.25f,  0.25f, 0}, .rotation = glm::angleAxis(glm::radians(90.f), glm::vec3{0,-1, 0}), .scale = {.5,.5,.5}};

    sceneMan_->getScene("Test")->getPartitioner()->build();
}
