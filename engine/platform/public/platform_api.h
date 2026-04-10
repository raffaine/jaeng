#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "common/result.h"
#include "common/triple_buffer.h"
#include "entity/entity.h"
#include "material/imaterialsys.h"
#include "mesh/imeshsys.h"
#include "render/frontend/renderer.h"
#include "render/graph/render_graph.h"
#include "scene/ipartition.h"
#include "scene/scene.h"
#include "storage/ifstorage.h"
#include "ui/fontsys.h"
#include "process.h"
#include "common/async/task_scheduler.h"

namespace jaeng::platform {

enum class KeyCode : uint32_t {
    Unknown = 0,
    Escape,
    Space,
    W, A, S, D, E, Q,
    Plus,
    Minus,
    Equal,
    Underscore,
};

struct MousePos {
    int32_t x, y;
};

struct WindowDesc {
    std::string title;
    uint32_t width;
    uint32_t height;
};

class IWindow {
public:
    virtual ~IWindow() = default;
    virtual void destroy() = 0;
    virtual void* get_native_handle() const = 0;
    virtual uint32_t get_width() const = 0;
    virtual uint32_t get_height() const = 0;
    virtual bool is_open() const = 0;
};

class IInput {
public:
    virtual ~IInput() = default;
    virtual bool is_key_down(KeyCode code) const = 0;
    virtual MousePos get_mouse_pos() const = 0;
};

struct Event {
    enum class Type {
        None = 0,
        WindowClose,
        WindowResize,
        KeyDown,
        KeyUp,
        MouseMove,
        MouseDown,
        MouseUp,
        MouseScroll,
    };

    Type type = Type::None;
    union {
        struct { uint32_t width, height; } resize;
        struct { KeyCode code; } key;
        struct { int32_t x, y; uint32_t button; } mouse;
        struct { float delta; } scroll;
    };
};

using EventCallback = std::function<void(const Event&)>;

enum class MessageBoxType {
    Info,
    Warning,
    Error
};

// Configuration for the engine bootstrapper
struct AppConfig {
    std::string title = "Jaeng Application";
    uint32_t width = 1280;
    uint32_t height = 720;
    GfxBackend backend = GfxBackend::Vulkan;
};

class IPlatform;

class IApplication {
public:
    IApplication(IPlatform& platform, const AppConfig& config);
    virtual ~IApplication() = default;

    bool init();
    void on_event(const Event& ev);
    void shutdown();
    bool should_close() const { return shouldClose_; }

    void set_tick_rate(uint32_t hz);
    void start_engine_threads();
    void stop_engine_threads();
    bool process_main_thread_tasks();

protected:

    // Application overrides for app-specific behavior
    virtual bool app_init() = 0;
    virtual void app_on_event(const Event& ev) {} // Optional
    virtual void app_shutdown() = 0;

    // Simulation Phase (Sim Thread)
    virtual void tick(float dt) = 0;

    // Extraction Phase (Sim Thread -> Render Thread sync point)
    // Copies necessary data from ECS into a Render Packet
    virtual void extract_render_state(std::vector<RenderCommand>& outQueue) = 0;

    // Render Phase (Render Thread)
    // Consumes the Render Packet and dispatches to the GPU
    virtual void render(const std::vector<RenderCommand>& inQueue, bool hasNewState, RenderGraph& graph, TextureHandle backbuffer, TextureHandle depthbuffer) = 0;

    // Engine System Accessors for user app
    IFileManager& fileManager() { return *fileMan_; }
    EntityManager& entityManager() { return *entityMan_; }
    IMaterialSystem& materialSystem() { return *matSys_; }
    IMeshSystem& meshSystem() { return *meshSys_; }
    IFontSystem& fontSystem() { return *fontSys_; }
    SceneManager& sceneManager() { return *sceneMan_; }
    RendererAPI& renderer() { return *renderer_.gfx; }
    async::TaskScheduler& taskScheduler() { return taskScheduler_; }
    IPlatform& platform() { return platform_; }
    IWindow& window() { return *window_; }
    const AppConfig& getConfig() const { return config_; }

private:
    void simulation_loop();
    void render_loop();

    async::TaskScheduler taskScheduler_;
    std::jthread simThread_;
    std::jthread renderThread_;
    std::atomic<bool> isRunning_ = false;

    float fixedDt_ = 1.0f / 60.0f;

    // Synchronization primitives for the frame swap
    std::mutex stateMutex_;
    std::condition_variable renderCv_;
    std::atomic<bool> frameReady_ = false;

    // Triple buffer for passing render commands from Sim to Render thread without blocking
    TripleBuffer<std::vector<RenderCommand>> stateBuffer_;
    
    // Engine Core State and Systems
    IPlatform& platform_;
    std::unique_ptr<IWindow> window_;
    Renderer renderer_;
    SwapchainHandle swap_ = 0;
    AppConfig config_;
    bool shouldClose_ = false;
    
    // Engine subsystems
    std::shared_ptr<IFileManager> fileMan_;
    std::shared_ptr<EntityManager> entityMan_;
    std::shared_ptr<IMaterialSystem> matSys_;
    std::shared_ptr<IMeshSystem> meshSys_;
    std::shared_ptr<IFontSystem> fontSys_;
    std::unique_ptr<SceneManager> sceneMan_;
};

class IPlatform {
public:
    virtual ~IPlatform() = default;
    virtual result<std::unique_ptr<IWindow>> create_window(const WindowDesc& desc) = 0;
    virtual IInput& get_input() = 0;
    virtual bool poll_events() = 0;
    virtual void set_event_callback(EventCallback cb) = 0;
    
    virtual void show_message_box(const std::string& title, const std::string& content, MessageBoxType type) = 0;
    
    virtual void* get_native_display_handle() const = 0;

    virtual IProcessManager& get_process_manager() = 0;

    // The entry point abstraction: takes application and enters the loop
    virtual int run(std::unique_ptr<IApplication> app) = 0;
};

// Factory function
std::unique_ptr<IPlatform> create_platform();

} // namespace jaeng::platform
