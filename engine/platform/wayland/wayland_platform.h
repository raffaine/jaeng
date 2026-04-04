#pragma once

#include "platform/public/platform_api.h"
#include <wayland-client.h>
#include <memory>
#include <vector>
#include <string>
#include "wayland_input.h"

// Forward declare xdg/libdecor types
struct xdg_wm_base;
struct xdg_wm_base_listener;
struct zxdg_decoration_manager_v1;
struct libdecor;
struct libdecor_frame;
struct libdecor_configuration;

extern "C" {
    extern const struct wl_interface xdg_wm_base_interface;
}

namespace jaeng::platform {

class WaylandPlatform : public IPlatform {
public:
    WaylandPlatform();
    ~WaylandPlatform();

    jaeng::result<std::unique_ptr<IWindow>> create_window(const WindowDesc& desc) override;
    IInput& get_input() override { return input_; }
    bool poll_events() override;
    void set_event_callback(EventCallback cb) override { eventCallback_ = cb; }
    
    void show_message_box(const std::string& title, const std::string& content, MessageBoxType type) override;
    
    int run(std::unique_ptr<IApplication> app) override;

    // Wayland specific access
    wl_display* get_display() const { return display_; }
    wl_compositor* get_compositor() const { return compositor_; }
    xdg_wm_base* get_xdg_wm_base() const { return xdg_wm_base_; }
    wl_shm* get_shm() const { return shm_; }
    libdecor* get_libdecor() const { return decor_context_; }

    static void registry_handle_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    static void registry_handle_global_remove(void* data, wl_registry* registry, uint32_t name);
    static void xdg_wm_base_ping(void* data, xdg_wm_base* xdg_wm_base, uint32_t serial);

private:
    static const wl_registry_listener registry_listener_;
    static const void* get_xdg_wm_base_listener();

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    xdg_wm_base* xdg_wm_base_ = nullptr;
    wl_shm* shm_ = nullptr;
    wl_seat* seat_ = nullptr;

    // libdecor
    libdecor* decor_context_ = nullptr;

    WaylandInput input_;
    EventCallback eventCallback_;

    bool running_ = true;

    static WaylandPlatform* instance_;
    friend class WaylandWindow;
    friend class WaylandInput;
};

} // namespace jaeng::platform
