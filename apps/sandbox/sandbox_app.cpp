#include "sandbox_app.h"
#include "mesh_utils.h"
#if defined(JAENG_WIN32) && !defined(JAENG_USE_VULKAN)
#include "pix3.h"
#endif

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
    : IApplication(platform, AppConfig{
        "jaeng Sandbox", 1280, 720, 
#if defined(JAENG_WIN32) && !defined(JAENG_USE_VULKAN)
        GfxBackend::D3D12
#else
        GfxBackend::Vulkan
#endif
    }) 
{}

bool SandboxApp::app_init() {
    setupResources();

    const auto aspect = 1280.f/720.f;
    auto camera = std::make_unique<PerspectiveCamera>(glm::vec3{3, 3, 3}, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0}, aspect);
    sceneManager().createScene("Test", std::make_unique<GridPartitioner>(), std::move(camera))
        .orElse([this](auto) -> Scene* {
            platform().show_message_box("Error", "Failed to create Test Scene.", MessageBoxType::Error);
            return nullptr;
        });

    setupEntities();

    BufferDesc cbDesc{ .size_bytes = 64, .usage = BufferUsage_Uniform };
    cbFrame_ = renderer().create_buffer(&cbDesc, nullptr);
    if (auto* scene = sceneManager().getScene("Test")) {
        scene->setCbFrame(cbFrame_);
    }

    return true;
}

void SandboxApp::app_shutdown() {
}

void SandboxApp::tick(float dt) {
    simTime_ += dt;

    // Spin the Sun (everything attached will orbit)
    if (auto* t = entityManager().getComponent<Transform>(testEntities_[0])) {
        t->rotation = glm::angleAxis(simTime_ * 0.5f, glm::vec3(0, 0, 1));
    }

    // Spin Planet 2 (its moon will orbit it, while it orbits the sun)
    if (auto* t = entityManager().getComponent<Transform>(testEntities_[2])) {
        t->rotation = glm::angleAxis(simTime_ * 2.0f, glm::vec3(0, 0, 1));
    }

    // Process the entire hierarchy into WorldMatrices
    TransformSystem::update(entityManager());
}

void SandboxApp::extract_render_state(std::vector<RenderCommand>& outQueue) {
    outQueue.clear();

    const auto& entities = entityManager().getAllEntities<WorldMatrix>();
    for (auto e : entities) {
        auto* wm = entityManager().getComponent<WorldMatrix>(e);
        auto* mesh = entityManager().getComponent<MeshHandle>(e);
        auto* mat = entityManager().getComponent<MaterialHandle>(e);
        auto* cb = entityManager().getComponent<BufferHandle>(e);

        if (wm && mesh && mat) {
            // Push an Update command mapping the EntityID directly to the ProxyID
            outQueue.push_back({
                RenderCommandType::Update,
                RenderProxy { static_cast<uint32_t>(e), wm->value, *mesh, *mat, cb ? *cb : 0 },
                static_cast<uint32_t>(e)
                });
        }
    }
}

void SandboxApp::render(const std::vector<RenderCommand>& inQueue, bool hasNewState, RenderGraph& graph, TextureHandle backbuffer, TextureHandle depthbuffer) {
    Scene* scene = sceneManager().getScene("Test");
    if (!scene) return;

    if (hasNewState) {
        for (const auto& cmd : inQueue) {
            if (cmd.type == RenderCommandType::Update) {
                scene->addOrUpdateProxy(cmd.proxy);
            } else if (cmd.type == RenderCommandType::Destroy) {
                scene->removeProxy(cmd.id);
            }
        }
        scene->getPartitioner()->build();
    }
    
    scene->buildDrawList({});
    scene->renderScene(graph, backbuffer, depthbuffer);
}

void SandboxApp::app_on_event(const Event&) {
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
    fileManager().registerMemoryFile("/mem/checker.raw", pixels.data(), pixels.size() * sizeof(uint32_t));
    fileManager().registerMemoryFile("/mem/material-test.json", materialFileData, strlen(materialFileData));

    auto meshRawData = createCubeMeshBinary();
    fileManager().registerMemoryFile("/mem/mesh-test.raw", meshRawData.data(), meshRawData.size());
}

void SandboxApp::setupEntities() {
    testEntities_.resize(4);
    for(int i=0; i<4; ++i) testEntities_[i] = entityManager().createEntity();

    if (auto meshHandle = meshSystem().loadMesh("/mem/mesh-test.raw").logError()) {
        for (int i = 0; i < 4; i++) {
            entityManager().addComponent<MeshHandle>(testEntities_[i]) = meshHandle.value();
            BufferDesc cbDesc{ .size_bytes = 64, .usage = BufferUsage_Uniform };
            entityManager().addComponent<BufferHandle>(testEntities_[i]) = renderer().create_buffer(&cbDesc, nullptr);
        }
    }
    if (auto matHandle = materialSystem().createMaterial("/mem/material-test.json", &ShaderReflection::vertexLayout, 1, ShaderReflection::inputSemantics).logError()) {
        auto h = matHandle.value();
        for (int i = 0; i < 4; i++) entityManager().addComponent<MaterialHandle>(testEntities_[i]) = h;
#ifdef JAENG_WIN32
        materialSub_ = fileManager().track("/mem/material-test.json", [this, h](const FileChangedEvent& e) {
            if (e.change == FileChangedEvent::ChangeType::Modified) {
                materialSystem().reloadMaterial(h).orElse([](auto){});
            }
        });
#endif
    }

    entityManager().addComponent<Transform>(testEntities_[0]) = Transform{ .position = { 0.0f,  0.0f, 0} }; // The Sun
    entityManager().addComponent<Transform>(testEntities_[1]) = Transform{ .position = { 2.0f,  0.0f, 0}, .scale = {.5, .5, .5} }; // Planet 1
    entityManager().addComponent<Transform>(testEntities_[2]) = Transform{ .position = {-3.0f,  0.0f, 0}, .scale = {.7, .7, .7} }; // Planet 2
    entityManager().addComponent<Transform>(testEntities_[3]) = Transform{ .position = { 0.0f,  1.5f, 0}, .scale = {.3, .3, .3} }; // Moon of Planet 2

    // Build the hierarchy
    entityManager().attachEntity(testEntities_[1], testEntities_[0]); // Planet 1 orbits Sun
    entityManager().attachEntity(testEntities_[2], testEntities_[0]); // Planet 2 orbits Sun
    entityManager().attachEntity(testEntities_[3], testEntities_[2]); // Moon orbits Planet 2

    // Run the transform system once to generate the initial matrices
    TransformSystem::update(entityManager());
}
