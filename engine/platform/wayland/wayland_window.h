#pragma once

#include "platform/public/platform_api.h"
#include <wayland-client.h>
#include <string>

// Forward declare xdg/libdecor types
struct xdg_surface;
struct xdg_toplevel;
struct libdecor_frame;
struct libdecor_configuration;

namespace jaeng::platform {

class WaylandPlatform;

class WaylandWindow : public IWindow {
public:
    WaylandWindow(WaylandPlatform* platform, const WindowDesc& desc);
    ~WaylandWindow() override;

    void destroy() override;
    void* get_native_handle() const override { return surface_; }
    uint32_t get_width() const override { return width_; }
    uint32_t get_height() const override { return height_; }
    bool is_open() const override { return open_; }

    void set_open(bool open) { open_ = open; }
    void set_size(uint32_t w, uint32_t h) { width_ = w; height_ = h; }

    // libdecor callbacks
    static void handle_configure(struct libdecor_frame* frame, struct libdecor_configuration* configuration, void* user_data);
    static void handle_close(struct libdecor_frame* frame, void* user_data);
    static void handle_commit(struct libdecor_frame* frame, void* user_data);

    void attach_dummy_buffer();

private:
    WaylandPlatform* platform_ = nullptr;
    wl_surface* surface_ = nullptr;
    libdecor_frame* frame_ = nullptr;
    wl_buffer* dummy_buffer_ = nullptr;

    uint32_t width_ = 0, height_ = 0;
    bool open_ = true;
    bool mapped_ = false;
};

} // namespace jaeng::platform
