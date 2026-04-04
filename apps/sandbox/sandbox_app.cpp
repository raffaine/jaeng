#include "sandbox_app.h"
#ifdef JAENG_WIN32
#include "mesh_utils.h"
#include "pix3.h"
#endif

#include "storage/win/filestorage.h"
#include "material/materialsys.h"
#include "mesh/meshsys.h"
#include "scene/grid_partition.h"
#include "scene/perspective_cam.h"

#ifdef JAENG_WIN32
#include "basic_reflect.h"
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

#ifdef JAENG_WIN32
static const char* materialFileData = R"(
{
  "name": "CheckerboardMaterial",
  "shader": {
    "vertex": "C:/dev/repos/jaeng/shaders/compiled/basic_vs.dxil",
    "pixel": "C:/dev/repos/jaeng/shaders/compiled/basic_ps.dxil",
    "reflection": "C:/dev/repos/jaeng/shaders/include/basic_reflect.json"
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
    {
      "name": "CBObject",
      "size": 64,
      "binding": 0
    },
    {
      "name": "CBFrame",
      "size": 64,
      "binding": 1
    },
    {
      "name": "CBMaterial",
      "size": 256,
      "binding": 2
    }
  ],
  "pipelineStates": {
    "blend": {
      "enabled": false,
      "srcFactor": "one",
      "dstFactor": "zero"
    },
    "rasterizer": {
      "cullMode": "back",
      "fillMode": "solid"
    },
    "depthStencil": {
      "depthTest": true,
      "depthWrite": true
    }
  }
}
)";
#else
static const char* materialFileData = R"(
{
  "name": "CheckerboardMaterial",
  "shader": {
    "vertex": "dummy_vs.dxil",
    "pixel": "dummy_ps.dxil",
    "reflection": "dummy_reflect.json"
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
    {
      "name": "CBObject",
      "size": 64,
      "binding": 0
    },
    {
      "name": "CBFrame",
      "size": 64,
      "binding": 1
    },
    {
      "name": "CBMaterial",
      "size": 256,
      "binding": 2
    }
  ],
  "pipelineStates": {
    "blend": {
      "enabled": false,
      "srcFactor": "one",
      "dstFactor": "zero"
    },
    "rasterizer": {
      "cullMode": "back",
      "fillMode": "solid"
    },
    "depthStencil": {
      "depthTest": true,
      "depthWrite": true
    }
  }
}
)";
#endif

SandboxApp::SandboxApp(IPlatform& platform) 
    : platform_(platform) 
    , swap_(0)
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

#ifdef JAENG_WIN32
    GfxBackend backend = GfxBackend::D3D12;
#else
    GfxBackend backend = GfxBackend::Vulkan;
#endif

    if (!renderer_.initialize(backend, window_->get_native_handle(), 3)) {
        platform_.show_message_box("Error", "Failed to initialize renderer.", MessageBoxType::Error);
        return false;
    }

    DepthStencilDesc depthDesc{.depth_enable = true, .depth_format = TextureFormat::D32F};
    SwapchainDesc    swapDesc{{1280u, 720u}, TextureFormat::BGRA8_UNORM, depthDesc, PresentMode::Fifo};
    swap_ = renderer_->create_swapchain(&swapDesc);

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

    return true;
}

void SandboxApp::update() {
    if (!window_) return;
    RenderGraph graph;
    Scene* scene = sceneMan_->getScene("Test");
    if (scene) {
        scene->buildDrawList({});
        scene->renderScene(graph, swap_);
        graph.compile();
        graph.execute(*renderer_.gfx, swap_, 0, nullptr);
        renderer_->present(swap_);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void SandboxApp::on_event(const Event& ev) {
    if (ev.type == Event::Type::WindowResize) {
        if (swap_ > 0) {
            renderer_->resize_swapchain(swap_, { ev.resize.width, ev.resize.height });
        }
    } else if (ev.type == Event::Type::WindowClose) {
        shouldClose_ = true;
    }
}

void SandboxApp::shutdown() {
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

#ifndef JAENG_WIN32
    // Dummy shader files
    fileMan_->registerMemoryFile("dummy_vs.dxil", "VS", 2);
    fileMan_->registerMemoryFile("dummy_ps.dxil", "PS", 2);
#endif

#ifdef JAENG_WIN32
    auto meshRawData = createCubeMeshBinary();
    fileMan_->registerMemoryFile("/mem/mesh-test.raw", meshRawData.data(), meshRawData.size());
#else
    // Dummy empty data for now on linux
    fileMan_->registerMemoryFile("/mem/mesh-test.raw", "DUMMY", 5);
#endif
}

void SandboxApp::setupEntities() {
    EntityID testEntities[4];
    for(int i=0; i<4; ++i) testEntities[i] = entityMan_->createEntity();

    if (auto meshHandle = meshSys_->loadMesh("/mem/mesh-test.raw").logError()) {
        for (int i = 0; i < 4; i++) entityMan_->addComponent<MeshHandle>(testEntities[i]) = meshHandle.value();
    }

#ifdef JAENG_WIN32
    if (auto matHandle = matSys_->createMaterial("/mem/material-test.json", &ShaderReflection::vertexLayout, 1, ShaderReflection::inputSemantics).logError()) {
        auto h = matHandle.value();
        for (int i = 0; i < 4; i++) entityMan_->addComponent<MaterialHandle>(testEntities[i]) = h;
        materialSub_ = fileMan_->track("/mem/material-test.json", [this, h](const FileChangedEvent& e) {
            if (e.change == FileChangedEvent::ChangeType::Modified) {
                matSys_->reloadMaterial(h).orElse([](auto){});
            }
        });
    }
#else
    // Still need a dummy layout to satisfy createMaterial
    VertexAttributeDesc attrs[] = { {0, 0, 0} };
    VertexLayoutDesc dummyLayout{ sizeof(RAWFormatVertex), attrs, 1 };
    const char* dummySemantics[] = { "POSITION" };
    if (auto matHandle = matSys_->createMaterial("/mem/material-test.json", &dummyLayout, 1, dummySemantics).logError()) {
        auto h = matHandle.value();
        for (int i = 0; i < 4; i++) entityMan_->addComponent<MaterialHandle>(testEntities[i]) = h;
    }
#endif

    entityMan_->addComponent<Transform>(testEntities[0]) = Transform{.position = {-0.25f, -0.25f, 0}};
    entityMan_->addComponent<Transform>(testEntities[1]) = Transform{.position = { 0.25f, -0.25f, 0}, .rotation = glm::angleAxis(glm::radians(90.f), glm::vec3{1, 0, 0})};
    entityMan_->addComponent<Transform>(testEntities[2]) = Transform{.position = {-0.25f,  0.25f, 0}, .rotation = glm::angleAxis(glm::radians(90.f), glm::vec3{0, 1, 0})};
    entityMan_->addComponent<Transform>(testEntities[3]) = Transform{.position = { 0.25f,  0.25f, 0}, .rotation = glm::angleAxis(glm::radians(90.f), glm::vec3{0,-1, 0}), .scale = {.5,.5,.5}};

    sceneMan_->getScene("Test")->getPartitioner()->build();
}
