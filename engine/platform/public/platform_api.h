#pragma once

#include <stdint.h>
#include <string>
#include <functional>
#include <memory>
#include "common/result.h"

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
    virtual void update() = 0;
    virtual void on_event(const Event& ev) = 0;
    virtual void shutdown() = 0;
    virtual bool should_close() const = 0;
};

class IPlatform {
public:
    virtual ~IPlatform() = default;
    virtual jaeng::result<std::unique_ptr<IWindow>> create_window(const WindowDesc& desc) = 0;
    virtual IInput& get_input() = 0;
    virtual bool poll_events() = 0;
    virtual void set_event_callback(EventCallback cb) = 0;
    
    virtual void show_message_box(const std::string& title, const std::string& content, MessageBoxType type) = 0;
    
    // The entry point abstraction: takes application and enters the loop
    virtual int run(std::unique_ptr<IApplication> app) = 0;
};

// Factory function
std::unique_ptr<IPlatform> create_platform();

} // namespace jaeng::platform
