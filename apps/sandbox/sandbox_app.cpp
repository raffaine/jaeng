#include "sandbox_app.h"
#include "mesh_utils.h"
#if defined(JAENG_WIN32) && !defined(JAENG_USE_VULKAN)
#include "pix3.h"
#endif

#include "scene/grid_partition.h"
#include "scene/perspective_cam.h"
#include "entity/entity.h"
#include "entity/transform_sys.h"
#include "animation/animation.h"
#include "common/math/ray.h"
#include "scene/icamera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <algorithm>

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
    EntityID camEntity = entityManager().createEntity();
    auto& t = entityManager().addComponent<Transform>(camEntity);
    t.position = {3.0f, 3.0f, 3.0f}; // Restore original camera position
    
    // Look at origin (0,0,0)
    glm::vec3 lookDir = glm::normalize(glm::vec3(0,0,0) - t.position);
    float yaw = std::atan2(lookDir.x, lookDir.z); 
    float pitch = std::asin(-lookDir.y); // Fix pitch inversion for LH rules
    
    entityManager().addComponent<CameraComponent>(camEntity) = {
        .aspect = aspect,
        .yaw = yaw,
        .pitch = pitch };
    
    // Sync initial rotation
    t.rotation = glm::angleAxis(yaw, glm::vec3(0, 1, 0)) * glm::angleAxis(pitch, glm::vec3(1, 0, 0));

    auto camera = std::make_unique<PerspectiveCamera>(entityManager(), camEntity);
    sceneManager().createScene("Test", std::make_unique<GridPartitioner>(), std::move(camera))
        .orElse([this](auto) -> Scene* {
            platform().show_message_box("Error", "Failed to create Test Scene.", MessageBoxType::Error);
            return nullptr;
        });

    setupEntities();
    setupAnimation();

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

    updateCamera(dt);
    handleSelection();

    // Process Animations BEFORE Transform System
    AnimationSystem::update(entityManager(), dt);

    // Process the entire hierarchy into WorldMatrices
    TransformSystem::update(entityManager());
}

void SandboxApp::updateCamera(float dt) {
    Scene* scene = sceneManager().getScene("Test");
    if (!scene) return;
    auto* cam = static_cast<PerspectiveCamera*>(scene->getCamera());
    if (!cam) return;

    float speed = 5.0f * dt;
    glm::vec3 moveDir(0.0f);

    if (inputState_.keys[(uint32_t)KeyCode::W]) moveDir.z += 1.0f; 
    if (inputState_.keys[(uint32_t)KeyCode::S]) moveDir.z -= 1.0f;
    if (inputState_.keys[(uint32_t)KeyCode::A]) moveDir.x -= 1.0f;
    if (inputState_.keys[(uint32_t)KeyCode::D]) moveDir.x += 1.0f;

    if (glm::length(moveDir) > 0.0001f) {
        cam->movePlanar(glm::normalize(moveDir) * speed);
    }

    if (inputState_.keys[(uint32_t)KeyCode::E]) cam->moveVertical(speed);
    if (inputState_.keys[(uint32_t)KeyCode::Q]) cam->moveVertical(-speed);

    // Zoom
    if (inputState_.keys[(uint32_t)KeyCode::Plus] || inputState_.keys[(uint32_t)KeyCode::Equal]) cam->setZoom(-20.0f * dt);
    if (inputState_.keys[(uint32_t)KeyCode::Minus] || inputState_.keys[(uint32_t)KeyCode::Underscore]) cam->setZoom(20.0f * dt);
    cam->setZoom(-inputState_.mouseScroll * 2.0f);
    inputState_.mouseScroll = 0;

    // Look (Click nothing and drag)
    if (inputState_.mouseButtons[0]) {
        if (!isLooking_) {
             auto ray = getRayFromMouse();
             float t;
             bool hitAny = false;
             for (auto e : entityManager().getAllEntities<Transform>()) {
                 if (entityManager().getComponent<CameraComponent>(e)) continue;
                 
                 jaeng::math::AABB box { .min = {-0.5f, -0.5f, -0.5f}, .max = {0.5f, 0.5f, 0.5f} };
                 auto* worldMat = entityManager().getComponent<WorldMatrix>(e);
                 if (worldMat) {
                     box.min += glm::vec3(worldMat->value[3]);
                     box.max += glm::vec3(worldMat->value[3]);
                 }
                 if (box.intersects(ray, t)) {
                     hitAny = true;
                     break;
                 }
             }
             if (!hitAny) {
                 isLooking_ = true;
                 inputState_.lastLookMousePos = inputState_.mousePos;
             }
        }
    } else {
        isLooking_ = false;
    }

    if (isLooking_) {
        float dx = (float)(inputState_.mousePos.x - inputState_.lastLookMousePos.x) * 0.005f;
        float dy = (float)(inputState_.mousePos.y - inputState_.lastLookMousePos.y) * 0.005f;
        inputState_.lastLookMousePos = inputState_.mousePos;
        cam->rotate({dx, dy});
    }
}

void SandboxApp::handleSelection() {
    if (inputState_.mouseButtons[0] && !isLooking_) {
        auto ray = getRayFromMouse();
        float minT = std::numeric_limits<float>::max();
        EntityID bestEntity = static_cast<EntityID>(-1);

        for (auto e : entityManager().getAllEntities<Transform>()) {
            if (entityManager().getComponent<CameraComponent>(e)) continue;

            jaeng::math::AABB box { .min = {-0.5f, -0.5f, -0.5f}, .max = {0.5f, 0.5f, 0.5f} };
            auto* worldMat = entityManager().getComponent<WorldMatrix>(e);
            if (worldMat) {
                 box.min += glm::vec3(worldMat->value[3]);
                 box.max += glm::vec3(worldMat->value[3]);
            }
            float t;
            if (box.intersects(ray, t)) {
                if (t < minT) {
                    minT = t;
                    bestEntity = e;
                }
            }
        }

        if (bestEntity != selectionState_.selectedEntity) {
            if (selectionState_.selectedEntity != static_cast<EntityID>(-1)) {
                auto matHandle = *entityManager().getComponent<MaterialHandle>(selectionState_.selectedEntity);
                materialSystem().setVectorParam(matHandle, "color", selectionState_.originalColor);
                materialSystem().updateMaterialParameters(matHandle);
            }

            selectionState_.selectedEntity = bestEntity;
            if (bestEntity != static_cast<EntityID>(-1)) {
                JAENG_LOG_INFO("Picked entity: {}", bestEntity);
                auto matHandle = *entityManager().getComponent<MaterialHandle>(bestEntity);
                if (auto metadata = std::move(materialSystem().getMetadata(matHandle)).logError()) {
                    selectionState_.originalColor = (*metadata)->vectorParams.at("color");
                }
                materialSystem().setVectorParam(matHandle, "color", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
                materialSystem().updateMaterialParameters(matHandle);
            }
        }
    }
}

math::Ray SandboxApp::getRayFromMouse() const {
    Scene* scene = const_cast<SandboxApp*>(this)->sceneManager().getScene("Test");
    auto* cam = static_cast<PerspectiveCamera*>(scene->getCamera());
    
    float x = (float)inputState_.mousePos.x / (float)getConfig().width;
    float y = (float)inputState_.mousePos.y / (float)getConfig().height;
    
    return cam->getRay(x, y);
}

void SandboxApp::extract_render_state(std::vector<RenderCommand>& outQueue) {
    outQueue.clear();

    if (auto* scene = sceneManager().getScene("Test")) {
        if (auto* cam = static_cast<PerspectiveCamera*>(scene->getCamera())) {
            RenderCommand cmd;
            cmd.type = RenderCommandType::UpdateCamera;
            cmd.cameraViewProj = cam->getViewProj();
            outQueue.push_back(cmd);
        }
    }

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
            } else if (cmd.type == RenderCommandType::UpdateCamera) {
                scene->setCameraViewProj(cmd.cameraViewProj);
            }
        }
        scene->getPartitioner()->build();
    }
    
    scene->buildDrawList({});
    scene->renderScene(graph, backbuffer, depthbuffer);
}

void SandboxApp::app_on_event(const Event& ev) {
    switch (ev.type) {
        case Event::Type::KeyDown:
            if (static_cast<uint32_t>(ev.key.code) < 256)
                inputState_.keys[static_cast<uint32_t>(ev.key.code)] = true;
            break;
        case Event::Type::KeyUp:
            if (static_cast<uint32_t>(ev.key.code) < 256)
                inputState_.keys[static_cast<uint32_t>(ev.key.code)] = false;
            break;
        case Event::Type::MouseMove:
            inputState_.lastMousePos = inputState_.mousePos;
            inputState_.mousePos = {ev.mouse.x, ev.mouse.y};
            break;
        case Event::Type::MouseDown:
            if (ev.mouse.button == 272) inputState_.mouseButtons[0] = true; // Left
            if (ev.mouse.button == 273) inputState_.mouseButtons[2] = true; // Right
            inputState_.lastLookMousePos = inputState_.mousePos;
            break;
        case Event::Type::MouseUp:
            if (ev.mouse.button == 272) inputState_.mouseButtons[0] = false;
            if (ev.mouse.button == 273) inputState_.mouseButtons[2] = false;
            break;
        case Event::Type::MouseScroll:
            inputState_.mouseScroll += ev.scroll.delta;
            break;
        default:
            break;
    }
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

void SandboxApp::setupAnimation() {
    testClip_ = std::make_unique<AnimationClip>();
    testClip_->name = "SolarSystemAnimation";
    
    // Duration for a perfectly seamless loop of both 0.5 rad/s and 2.0 rad/s:
    // Sun: 2*pi / 0.5 = 4*pi seconds
    // Planet 2: 2*pi / 2.0 = pi seconds
    // LCM is 4*pi.
    const float PI = 3.14159265f;
    const float duration = 4.0f * PI;
    testClip_->duration = duration;

    // Track 0: Sun Spin (0.5 rad/s) -> 1 full rotation (2*pi) over 4*pi seconds
    AnimationTrack sunTrack;
    for (int i = 0; i <= 4; ++i) {
        float ratio = (float)i / 4.0f;
        sunTrack.rotationKeys.push_back({ ratio * duration, glm::angleAxis(ratio * 2.0f * PI, glm::vec3(0, 0, 1)) });
    }
    testClip_->tracks.push_back(std::move(sunTrack));

    // Track 1: Planet 2 Spin (2.0 rad/s) -> 4 full rotations (8*pi) over 4*pi seconds
    AnimationTrack planet2Track;
    for (int i = 0; i <= 16; ++i) {
        float ratio = (float)i / 16.0f;
        planet2Track.rotationKeys.push_back({ ratio * duration, glm::angleAxis(ratio * 8.0f * PI, glm::vec3(0, 0, 1)) });
    }
    testClip_->tracks.push_back(std::move(planet2Track));

    // Track 2: Moon Pulse/Bounce/Spin
    AnimationTrack moonTrack;
    // 4 cycles of pulse and bounce to fit the duration
    for (int cycle = 0; cycle < 4; ++cycle) {
        float startT = cycle * (duration / 4.0f);
        float midT = startT + (duration / 8.0f);
        moonTrack.scaleKeys.push_back({startT, {0.3f, 0.3f, 0.3f}});
        moonTrack.scaleKeys.push_back({midT,   {0.6f, 0.6f, 0.6f}});

        moonTrack.positionKeys.push_back({startT, {0.0f, 1.5f, 0.0f}});
        moonTrack.positionKeys.push_back({midT,   {0.0f, 2.5f, 0.0f}});
    }
    // Final keyframes to close the loop
    moonTrack.scaleKeys.push_back({duration, {0.3f, 0.3f, 0.3f}});
    moonTrack.positionKeys.push_back({duration, {0.0f, 1.5f, 0.0f}});

    // Spin: 4 spins total
    for (int i = 0; i <= 16; ++i) {
        float ratio = (float)i / 16.0f;
        moonTrack.rotationKeys.push_back({ ratio * duration, glm::angleAxis(ratio * 8.0f * PI, glm::vec3(0, 1, 0)) });
    }

    testClip_->tracks.push_back(std::move(moonTrack));

    // Attach Animator to Entity 0 (The Sun)
    auto& animator = entityManager().addComponent<Animator>(testEntities_[0]);
    animator.clip = testClip_.get();
    
    // Map tracks to entities
    animator.jointEntities.push_back(testEntities_[0]); // Track 0 -> Sun
    animator.jointEntities.push_back(testEntities_[2]); // Track 1 -> Planet 2
    animator.jointEntities.push_back(testEntities_[3]); // Track 2 -> Moon
    
    animator.isPlaying = true;
    animator.loop = true;
}

