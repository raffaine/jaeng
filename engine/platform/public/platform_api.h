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
#include "scene/ipartition.h"

namespace jaeng::platform {

enum class KeyCode : uint32_t {
    Unknown = 0,
    Escape,
    Space,
    W, A, S, D,
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
    };

    Type type = Type::None;
    union {
        struct { uint32_t width, height; } resize;
        struct { KeyCode code; } key;
        struct { int32_t x, y; } mouse;
    };
};

using EventCallback = std::function<void(const Event&)>;

enum class MessageBoxType {
    Info,
    Warning,
    Error
};

class IApplication {
public:
    virtual ~IApplication() = default;

    virtual bool init() = 0;
    virtual void on_event(const Event& ev) = 0;
    virtual void shutdown() = 0;
    virtual bool should_close() const = 0;

    void set_tick_rate(uint32_t hz);

    // Main thread entry and exit points
    void start_engine_threads();
    void stop_engine_threads();

protected:
    // Simulation Phase (Sim Thread)
    virtual void tick(float dt) = 0;

    // Extraction Phase (Sim Thread -> Render Thread sync point)
    // Copies necessary data from ECS into a Render Packet
    virtual void extract_render_state(std::vector<RenderCommand>& outQueue) = 0;

    // Render Phase (Render Thread)
    // Consumes the Render Packet and dispatches to the GPU
    virtual void render(const std::vector<RenderCommand>& inQueue, bool hasNewState) = 0;

private:
    void simulation_loop();
    void render_loop();

    std::jthread simThread_;
    std::jthread renderThread_;
    std::atomic<bool> isRunning_ = false;

    float fixedDt_ = 1.0f / 60.0f;

    // Synchronization primitives for the frame swap
    std::mutex stateMutex_;
    std::condition_variable renderCv_;
    std::atomic<bool> frameReady_ = false;

    jaeng::TripleBuffer<std::vector<RenderCommand>> stateBuffer_;
};

class IPlatform {
public:
    virtual ~IPlatform() = default;
    virtual jaeng::result<std::unique_ptr<IWindow>> create_window(const WindowDesc& desc) = 0;
    virtual IInput& get_input() = 0;
    virtual bool poll_events() = 0;
    virtual void set_event_callback(EventCallback cb) = 0;
    
    virtual void show_message_box(const std::string& title, const std::string& content, MessageBoxType type) = 0;
    
    virtual void* get_native_display_handle() const = 0;

    // The entry point abstraction: takes application and enters the loop
    virtual int run(std::unique_ptr<IApplication> app) = 0;
};

// Factory function
std::unique_ptr<IPlatform> create_platform();

} // namespace jaeng::platform
