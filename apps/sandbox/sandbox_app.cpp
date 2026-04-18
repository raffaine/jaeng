#include "sandbox_app.h"
#include "mesh_utils.h"
#if defined(JAENG_WIN32) && !defined(JAENG_USE_VULKAN)
#include "pix3.h"
#endif

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <algorithm>

// Shader Includes
#include "basic.h"
#include "ui.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <cstring>

#include <thread>
#include <chrono>
#include <cerrno>
#include <filesystem>
#include <sstream>

#ifdef JAENG_MACOS
#include <mach-o/dyld.h>
#endif

#if defined(JAENG_LINUX) || defined(JAENG_MACOS) || defined(JAENG_IOS)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#define SOCKET_TYPE int
#define INVALID_SOCKET_VAL -1
#define CLOSE_SOCKET close
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#define SOCKET_TYPE SOCKET
#define INVALID_SOCKET_VAL INVALID_SOCKET
#define CLOSE_SOCKET closesocket

#ifdef Yield
// Windows defines this which breaks our definition
#undef Yield
#endif
#endif

#include "scene/grid_partition.h"
#include "scene/perspective_cam.h"
#include "entity/entity.h"
#include "entity/transform_sys.h"
#include "animation/animation.h"
#include "ui/ui.h"
#include "common/math/ray.h"
#include "scene/icamera.h"
#include "scene/render_sys.h"

using namespace jaeng;
using namespace jaeng::platform;

jaeng::async::FireAndForget SandboxApp::runAsyncTaskTest() {
    auto get_tid = []() {
        std::stringstream ss;
        ss << std::this_thread::get_id();
        return ss.str();
    };

    JAENG_LOG_INFO("Async Task Started on thread {}", get_tid());
    
    // Simulate some work on a worker thread
    co_await jaeng::async::SwitchToWorker();
    JAENG_LOG_INFO("Async Task now on worker thread {}", get_tid());
    
    // Test Yield by looping and yielding
    for (int i = 0; i < 3; ++i) {
        JAENG_LOG_INFO("Async Task heavy work chunk {} on thread {}", i, get_tid());
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        co_await jaeng::async::Yield(); // Yield to let other tasks run
    }

    // Test Async Asset Pipeline
    JAENG_LOG_INFO("Testing Async Asset Pipeline (loading /mem/material-test.json)...");
    auto assetResult = co_await fileManager().loadAsync("/mem/material-test.json");
    if (assetResult.hasValue()) {
        auto val = std::move(assetResult).logError().value();
        JAENG_LOG_INFO("Async load success! Loaded {} bytes.", val.size());
    } else {
        auto err = std::move(assetResult).logError().error();
        JAENG_LOG_ERROR("Async load failed: {}", err.message);
    }
    
    // Back to main thread to update UI or similar
    co_await jaeng::async::SwitchToMainThread();
    JAENG_LOG_INFO("Async Task back on main thread {}", get_tid());
    
    platform().show_message_box("Async Test", "Background FireAndForget task completed successfully!", MessageBoxType::Info);
}

void SandboxApp::runFutureTest() {
    JAENG_LOG_INFO("Future Test Started");
    
    taskScheduler().enqueue_async([]() {
        JAENG_LOG_INFO("Future Test: Step 1 (Background)");
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return 42;
    }).then([](int result) {
        JAENG_LOG_INFO("Future Test: Step 2 (Background, received {})", result);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return result * 2;
    }).thenSync([this](int finalResult) {
        JAENG_LOG_INFO("Future Test: Final Step on main thread, result: {}", finalResult);
        platform().show_message_box("Future Test", "Future chain completed with result: " + std::to_string(finalResult), MessageBoxType::Info);
    });
}

void SandboxApp::startServer() {
    ProcessDesc desc;
    
    // Discovery path: always check next to executable
    std::string exeDir;
    try {
#ifdef JAENG_WIN32
        exeDir = "."; // Placeholder
#elif defined(JAENG_MACOS)
        char path[1024];
        uint32_t size = sizeof(path);
        if (_NSGetExecutablePath(path, &size) == 0) {
            exeDir = std::filesystem::path(path).parent_path().string();
        } else {
            exeDir = ".";
        }
#else
        std::error_code ec;
        auto exePath = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (ec) exePath = "./sandbox";
        exeDir = exePath.parent_path().string();
#endif
    } catch (...) {
        exeDir = ".";
    }
    
    auto binDir = std::filesystem::path(exeDir);

#if defined(JAENG_LINUX) || defined(JAENG_MACOS)
    desc.command = (binDir / "TestServer").string();
#else
    desc.command = (binDir / "TestServer.exe").string();
#endif
    
    desc.workingDir = binDir.string(); 
    
    auto result = platform().get_process_manager().spawn(desc);
    if (result.hasValue()) {
        serverProcess_ = std::move(result).logError().value();
        JAENG_LOG_INFO("Spawned TestServer with PID: {}", serverProcess_->get_id());
    } else {
        auto err = std::move(result).logError();
        JAENG_LOG_ERROR("Failed to spawn TestServer: {}", err.error().message);
    }
}

void SandboxApp::restartServer() {
    if (serverProcess_) {
        JAENG_LOG_INFO("Killing TestServer (PID: {})...", serverProcess_->get_id());
        serverProcess_->kill();
    }
    startServer();
}

void SandboxApp::updateServerData() {
    if (serverProcess_ && !serverProcess_->is_running()) {
        int32_t code = serverProcess_->get_exit_code();
        JAENG_LOG_ERROR("TestServer is not running. Exit code: {}", code);
        serverTime_ = "Server Process Dead (" + std::to_string(code) + ")";
        return;
    }

#ifdef JAENG_WIN32
    static bool wsaStarted = false;
    if (!wsaStarted) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        wsaStarted = true;
    }
#endif

    // Connect to 127.0.0.1:12346
    SOCKET_TYPE sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET_VAL) {
        JAENG_LOG_ERROR("Failed to create socket");
        return;
    }

    // Set non-blocking
#if defined(JAENG_LINUX) || defined(JAENG_MACOS) || defined(JAENG_IOS)
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#else
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#endif

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(12347);
#if defined(JAENG_LINUX) || defined(JAENG_MACOS) || defined(JAENG_IOS)
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
#else
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);
#endif

    int res = connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if (res < 0) {
#if defined(JAENG_LINUX) || defined(JAENG_MACOS) || defined(JAENG_IOS)
        if (errno != EINPROGRESS) {
            JAENG_LOG_ERROR("Connect failed immediately: {} (Port 12346)", errno);
            CLOSE_SOCKET(sock);
            serverTime_ = "Server Offline (Connect Error)";
            return;
        }
#else
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            CLOSE_SOCKET(sock);
            serverTime_ = "Server Offline (Connect Error)";
            return;
        }
#endif

        // Wait for connect to complete (writable) or fail
        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(sock, &writefds);
        struct timeval tv_connect;
        tv_connect.tv_sec = 0;
        tv_connect.tv_usec = 500000; // 500ms
        res = select((int)sock + 1, NULL, &writefds, NULL, &tv_connect);
        if (res <= 0) {
             CLOSE_SOCKET(sock);
             serverTime_ = (res == 0) ? "Server Offline (Connect Timeout)" : "Server Offline (Select Error)";
             return;
        }

        // Check if actually connected
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, &len);
        if (error != 0) {
             JAENG_LOG_ERROR("Socket error after select: {} (Port 12346)", error);
             CLOSE_SOCKET(sock);
             serverTime_ = "Server Offline (Connect Failed)";
             return;
        }
    }

    // Now connected, wait for data (readable)
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    struct timeval tv_read;
    tv_read.tv_sec = 0;
    tv_read.tv_usec = 100000; // 100ms timeout

    int activity = select((int)sock + 1, &readfds, NULL, NULL, &tv_read);

    if (activity > 0 && FD_ISSET(sock, &readfds)) {
        char buffer[1024] = {0};
#if defined(JAENG_LINUX) || defined(JAENG_MACOS) || defined(JAENG_IOS)
        int valread = read(sock, buffer, 1024);
#else
        int valread = recv(sock, buffer, 1024, 0);
#endif
        if (valread > 0) {
            serverTime_ = std::string(buffer, valread);
        } else if (valread == 0) {
            serverTime_ = "Server Offline (EOF)";
        } else {
#if defined(JAENG_LINUX) || defined(JAENG_MACOS) || defined(JAENG_IOS)
            JAENG_LOG_ERROR("Server read error: {}", errno);
#else
            JAENG_LOG_ERROR("Server read error: {}", WSAGetLastError());
#endif
            serverTime_ = "Server Offline (Read Error)";
        }
    } else if (activity == 0) {
        serverTime_ = "Server Offline (Read Timeout)";
    } else {
        serverTime_ = "Server Offline (Read Select Error)";
    }

    CLOSE_SOCKET(sock);
}

// Define the backend-specific extensions and files
#if defined(JAENG_WIN32) && !defined(JAENG_USE_VULKAN)
#define SHADER_EXT ".dxil"
#define REFLECT_FILE "basic_reflect.json"
#elif defined(JAENG_MACOS) || defined(JAENG_IOS)
#define SHADER_EXT ".msl"
#define REFLECT_FILE "basic.json"
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
    { "name": "CBObject", "size": 96, "binding": 0 },
    { "name": "CBFrame", "size": 64, "binding": 1 }
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
#elif defined(JAENG_MACOS) || defined(JAENG_IOS)
        GfxBackend::Metal
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
    
    // Initial orientation sync
    glm::quat qYaw = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
    glm::quat qPitch = glm::angleAxis(pitch, glm::vec3(1, 0, 0));
    t.rotation = qYaw * qPitch;

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

    setupUI();
    startServer();

    return true;
}

void SandboxApp::app_shutdown() {
    if (serverProcess_) serverProcess_->kill();
}

void SandboxApp::tick(float dt) {
    simTime_ += dt;
    static uint32_t frameCount = 0;
    if (frameCount++ % 100 == 0) {
        JAENG_LOG_DEBUG("Simulation Frame: {}", frameCount);
    }

    // 1) Update UI Layout
    UILayoutSystem::update(entityManager(), static_cast<float>(getConfig().width), static_cast<float>(getConfig().height));

    // 2) Process UI Interaction
    bool inputConsumed = false;
    UIInteractionSystem::update(entityManager(), static_cast<float>(inputState_.mousePos.x), static_cast<float>(inputState_.mousePos.y), inputState_.mouseButtons[0], inputConsumed);

    if (!inputConsumed) {
        updateCamera(dt);
        handleSelection();
    } else {
        isLooking_ = false; // Reset drag if interacting with UI
    }

    // Process Animations BEFORE Transform System
    AnimationSystem::update(entityManager(), dt);

    // Poll server data
    serverPollTimer_ += dt;
    if (serverPollTimer_ >= 0.5f) {
        updateServerData();
        serverPollTimer_ = 0.0f;
    }

    // Refresh Server UI Text
    if (serverTextEntity_ != static_cast<EntityID>(-1)) {
        if (auto* ut = entityManager().getComponent<UIText>(serverTextEntity_)) {
            ut->text = "Server Time: " + serverTime_;
        }
    }

    // Update UI Text with selection info (Moved to handler in refactor? 
    // No, keep it here for simple polling if needed, or we could use an event bus later)
    if (uiTextEntity_ != static_cast<EntityID>(-1)) {
        if (auto* ut = entityManager().getComponent<UIText>(uiTextEntity_)) {
            if (selectionState_.selectedEntity != static_cast<EntityID>(-1)) {
                ut->text = "Selected: " + std::to_string(static_cast<uint32_t>(selectionState_.selectedEntity));
            } else {
                ut->text = "No Selection";
            }
        }
    }

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
            selectionState_.selectedEntity = bestEntity;
            if (bestEntity != static_cast<EntityID>(-1)) {
                auto mat = *entityManager().getComponent<MaterialComponent>(bestEntity);
                if (auto metadata = std::move(materialSystem().getMetadata(mat.handle)).logError()) {
                    selectionState_.originalColor = (*metadata)->vectorParams.at("color");
                }
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

    // 1) Camera State
    if (auto* scene = sceneManager().getScene("Test")) {
        if (auto* cam = static_cast<PerspectiveCamera*>(scene->getCamera())) {
            RenderCommand cmd;
            cmd.type = RenderCommandType::UpdateCamera;
            cmd.cameraViewProj = cam->getViewProj();
            outQueue.push_back(cmd);
        }
    }

    // 2) 3D Scene State (Engine-managed extraction with selection visitor)
    if (auto* scene = sceneManager().getScene("Test")) {
        SceneRenderSystem::extract(*scene, entityManager(), outQueue, [this](EntityID e, RenderProxy& proxy) {
            if (e == selectionState_.selectedEntity) {
                proxy.color = glm::vec4(1, 0, 0, 1);
            }
        });
    }

    // 3) UI state every frame to avoid ghosting
    RenderCommand clearCmd;
    clearCmd.type = RenderCommandType::ClearUI;
    outQueue.push_back(clearCmd);

    // 4) UI State (Engine-managed extraction)
    UIRenderSystem::extract(entityManager(), fontSystem(), outQueue);
}

void SandboxApp::render(const std::vector<RenderCommand>& inQueue, bool hasNewState, RenderGraph& graph, TextureHandle backbuffer, TextureHandle depthbuffer) {
    Scene* scene = sceneManager().getScene("Test");
    if (!scene) return;

    // Dispatch all logic updates to the scene
    if (hasNewState) {
        scene->processCommands(inQueue);
    } else {
        // Even if no state change, we still need to sync the camera every frame
        for (const auto& cmd : inQueue) {
            if (cmd.type == RenderCommandType::UpdateCamera) {
                scene->setCameraViewProj(cmd.cameraViewProj);
            }
        }
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

static const char* uiMaterialFileData = R"({
  "name": "UIMaterial",
  "shader": {
    "vertex": ")" JAENG_SHADER_DIR "/compiled/ui_vs" SHADER_EXT R"(",
    "pixel": ")" JAENG_SHADER_DIR "/compiled/ui_ps" SHADER_EXT R"(",
    "reflection": ")" JAENG_SHADER_DIR "/include/ui.json" R"("
  },
  "textures": [
    {
      "path": "/mem/white.raw",
      "width": 1,
      "height": 1,
      "format": "rgba8",
      "sampler": {
        "filter": "nearest",
        "addressModeU": "clamp",
        "addressModeV": "clamp"
      }
    }
  ],
  "parameters": {
    "color": [1.0, 1.0, 1.0, 1.0],
    "roughness": 0.5,
    "metallic": 0.0
  },
  "constantBuffers": [
    { "name": "CBObject", "size": 96, "binding": 0 },
    { "name": "CBFrame", "size": 64, "binding": 1 }
  ],
  "pipelineStates": {
    "blend": { "enabled": true, "srcFactor": "src_alpha", "dstFactor": "one_minus_src_alpha" },
    "rasterizer": { "cullMode": "none", "fillMode": "solid" },
    "depthStencil": { "depthTest": false, "depthWrite": false }
  }
})";

void SandboxApp::setupResources() {
    uint32_t whitePixel = 0xFFFFFFFF;
    fileManager().registerMemoryFile("/mem/white.raw", &whitePixel, sizeof(uint32_t));

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
    fileManager().registerMemoryFile("/mem/ui-material.json", uiMaterialFileData, strlen(uiMaterialFileData));

    auto meshRawData = createCubeMeshBinary();
    fileManager().registerMemoryFile("/mem/mesh-test.raw", meshRawData.data(), meshRawData.size());

    auto quadRawData = createQuadMeshBinary();
    fileManager().registerMemoryFile("/mem/quad-test.raw", quadRawData.data(), quadRawData.size());

    if (auto fontHandle = fontSystem().loadFont(JAENG_ASSET_DIR "/Roboto-Regular.ttf", 32.0f).logError()) {
        defaultFont_ = fontHandle.value();
    }
    
    if (auto matHandle = materialSystem().createMaterial("/mem/ui-material.json", &ShaderReflection::ui::vertexLayout, 1, ShaderReflection::ui::inputSemantics).logError()) {
        uiMaterial_ = matHandle.value();
    }
}

void SandboxApp::setupEntities() {
    testEntities_.resize(4);
    for(int i=0; i<4; ++i) testEntities_[i] = entityManager().createEntity();

    if (auto meshHandle = meshSystem().loadMesh("/mem/mesh-test.raw").logError()) {
        for (int i = 0; i < 4; i++) {
            entityManager().addComponent<MeshComponent>(testEntities_[i]) = {meshHandle.value()};
            BufferDesc cbDesc{ .size_bytes = 96, .usage = BufferUsage_Uniform };
            entityManager().addComponent<BufferComponent>(testEntities_[i]) = {renderer().create_buffer(&cbDesc, nullptr)};
        }
    }
    if (auto matHandle = materialSystem().createMaterial("/mem/material-test.json", &ShaderReflection::basic::vertexLayout, 1, ShaderReflection::basic::inputSemantics).logError()) {
        auto h = matHandle.value();
        for (int i = 0; i < 4; i++) entityManager().addComponent<MaterialComponent>(testEntities_[i]) = {h};
#ifdef JAENG_WIN32
        materialSub_ = fileManager().track("/mem/material-test.json", [this, h](const FileChangedEvent& e) {
            if (e.change == FileChangedEvent::ChangeType::Modified) {
                materialSystem().reloadMaterial(h).orElse([](auto) {});
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

void SandboxApp::setupUI() {
    auto quadMesh = meshSystem().loadMesh("/mem/quad-test.raw").orValue(0);

    UIBuilder builder(entityManager(), quadMesh, uiMaterial_, &renderer());
    builder.begin("Server_Panel")
        .withRect({300.0f, 100.0f}, {10.0f, 10.0f})
        .withAnchors({0.0f, 0.0f}, {0.0f, 0.0f})
        .withPivot({0.0f, 0.0f})
        .withZIndex(100)
        .withColor({0.1f, 0.1f, 0.1f, 0.7f})
        .begin("Restart_Button")
            .withRect({140.0f, 40.0f}, {10.0f, 50.0f})
            .withAnchors({0.0f, 0.0f}, {0.0f, 0.0f})
            .withPivot({0.0f, 0.0f})
            .withZIndex(110)
            .withColor({0.6f, 0.2f, 0.2f, 1.0f})
            .onClick([this](){ restartServer(); })
            .onHover([this, e = builder.getCurrent()](bool hovered){
                if (auto* ur = entityManager().getComponent<UIRenderable>(e)) {
                    ur->color = hovered ? glm::vec4(0.8f, 0.3f, 0.3f, 1.0f) : glm::vec4(0.6f, 0.2f, 0.2f, 1.0f);
                }
            })
            .begin("Restart_Text")
                .withRect({140.0f, 40.0f}, {5.0f, 0.0f})
                .withAnchors({0.0f, 0.5f}, {0.0f, 0.5f})
                .withPivot({0.0f, 0.5f})
                .withZIndex(120)
                .withText("Restart Server", 24.0f, defaultFont_)
            .end()
        .end()
        .begin("Async_Test_Button")
            .withRect({140.0f, 40.0f}, {160.0f, 50.0f})
            .withAnchors({0.0f, 0.0f}, {0.0f, 0.0f})
            .withPivot({0.0f, 0.0f})
            .withZIndex(110)
            .withColor({0.2f, 0.6f, 0.2f, 1.0f})
            .onClick([this](){ runAsyncTaskTest(); })
            .onHover([this, e = builder.getCurrent()](bool hovered){
                if (auto* ur = entityManager().getComponent<UIRenderable>(e)) {
                    ur->color = hovered ? glm::vec4(0.3f, 0.8f, 0.3f, 1.0f) : glm::vec4(0.2f, 0.6f, 0.2f, 1.0f);
                }
            })
            .begin("Async_Test_Text")
                .withRect({140.0f, 40.0f}, {5.0f, 0.0f})
                .withAnchors({0.0f, 0.5f}, {0.0f, 0.5f})
                .withPivot({0.0f, 0.5f})
                .withZIndex(120)
                .withText("Run Async Test", 24.0f, defaultFont_)
            .end()
        .end()
        .begin("Future_Test_Button")
            .withRect({140.0f, 40.0f}, {310.0f, 50.0f})
            .withAnchors({0.0f, 0.0f}, {0.0f, 0.0f})
            .withPivot({0.0f, 0.0f})
            .withZIndex(110)
            .withColor({0.2f, 0.2f, 0.6f, 1.0f})
            .onClick([this](){ runFutureTest(); })
            .onHover([this, e = builder.getCurrent()](bool hovered){
                if (auto* ur = entityManager().getComponent<UIRenderable>(e)) {
                    ur->color = hovered ? glm::vec4(0.3f, 0.3f, 0.8f, 1.0f) : glm::vec4(0.2f, 0.2f, 0.6f, 1.0f);
                }
            })
            .begin("Future_Test_Text")
                .withRect({140.0f, 40.0f}, {5.0f, 0.0f})
                .withAnchors({0.0f, 0.5f}, {0.0f, 0.5f})
                .withPivot({0.0f, 0.5f})
                .withZIndex(120)
                .withText("Run Future Test", 24.0f, defaultFont_)
            .end()
        .end()
        .begin("Server_Time_Text", &serverTextEntity_)
            .withRect({280.0f, 40.0f}, {10.0f, 10.0f})
            .withAnchors({0.0f, 0.0f}, {0.0f, 0.0f})
            .withPivot({0.0f, 0.0f})
            .withZIndex(110)
            .withText("Server Time: Offline", 24.0f, defaultFont_)
        .end()
    .end();

    builder.begin("HUD_Panel")
        .withRect({ 200.0f, 150.0f }, { -10.0f, -10.0f })
        .withAnchors({ 1.0f, 1.0f }, { 1.0f, 1.0f })
        .withPivot({ 1.0f, 1.0f })
        .withZIndex(10)
        .withColor({ 0.2f, 0.2f, 0.2f, 0.8f })
        .begin("Selection_Button")
            .withRect({ 150.0f, 50.0f }, { 0.0f, 0.0f })
            .withAnchors({ 0.5f, 0.5f }, { 0.5f, 0.5f })
            .withPivot({ 0.5f, 0.5f })
            .withZIndex(20)
            .withColor({ 0.4f, 0.4f, 0.8f, 1.0f })
            .onClick([this]() { selectionState_.selectedEntity = static_cast<EntityID>(-1); })
            .onHover([this, e = builder.getCurrent()](bool hovered) {
                if (auto* ur = entityManager().getComponent<UIRenderable>(e)) {
                    ur->color = hovered ? glm::vec4(0.6f, 0.6f, 1.0f, 1.0f) : glm::vec4(0.4f, 0.4f, 0.8f, 1.0f);
                }
            })
            .begin("Selection_Text", &uiTextEntity_)
                .withRect({ 150.0f, 50.0f }, { 10.0f, 0.0f })
                .withAnchors({ 0.0f, 0.5f }, { 0.0f, 0.5f })
                .withPivot({ 0.0f, 0.5f })
                .withZIndex(30)
                .withText("No Selection", 32.0f, defaultFont_)
            .end()
        .end()
    .end();
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
