#include "wayland_platform.h"
#include "wayland_window.h"
#include "common/logging.h"
#include <cstring>
#include <iostream>
#include <libdecor.h>

extern "C" {
#include "xdg-shell-client-protocol.h"
}

namespace jaeng::platform {

WaylandPlatform* WaylandPlatform::instance_ = nullptr;

const wl_registry_listener WaylandPlatform::registry_listener_ = {
    .global = WaylandPlatform::registry_handle_global,
    .global_remove = WaylandPlatform::registry_handle_global_remove,
};

static const xdg_wm_base_listener xdg_wm_base_listener_obj = {
    .ping = WaylandPlatform::xdg_wm_base_ping,
};

const void* WaylandPlatform::get_xdg_wm_base_listener() {
    return &xdg_wm_base_listener_obj;
}

static void libdecor_error_handler(struct libdecor* context, enum libdecor_error error, const char* message) {
    JAENG_LOG_ERROR("libdecor error {}: {}", (int)error, message);
}

static struct libdecor_interface libdecor_iface = {
    .error = libdecor_error_handler,
};

WaylandPlatform::WaylandPlatform() {
    instance_ = this;
    display_ = wl_display_connect(nullptr);
    if (!display_) {
        JAENG_LOG_ERROR("Failed to connect to Wayland display");
        return;
    }

    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &registry_listener_, this);

    wl_display_roundtrip(display_);

    decor_context_ = libdecor_new(display_, &libdecor_iface);
}

WaylandPlatform::~WaylandPlatform() {
    input_.cleanup(); 

    if (decor_context_) libdecor_unref(decor_context_);
    if (shm_) wl_shm_destroy(shm_);
    if (xdg_wm_base_) xdg_wm_base_destroy(xdg_wm_base_);
    if (compositor_) wl_compositor_destroy(compositor_);
    if (seat_) {
        wl_seat_destroy(seat_);
    }
    if (registry_) wl_registry_destroy(registry_);
    if (display_) wl_display_disconnect(display_);
    instance_ = nullptr;
}

void WaylandPlatform::registry_handle_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    auto self = static_cast<WaylandPlatform*>(data);
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        self->compositor_ = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, version));
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        self->xdg_wm_base_ = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, version));
        xdg_wm_base_add_listener(self->xdg_wm_base_, static_cast<const xdg_wm_base_listener*>(get_xdg_wm_base_listener()), self);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        self->shm_ = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, version));
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        self->seat_ = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, version));
        self->input_.setup_seat(self->seat_);
    }
}

void WaylandPlatform::registry_handle_global_remove(void* data, wl_registry* registry, uint32_t name) {
}

void WaylandPlatform::xdg_wm_base_ping(void* data, xdg_wm_base* xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

jaeng::result<std::unique_ptr<IWindow>> WaylandPlatform::create_window(const WindowDesc& desc) {
    if (!compositor_ || !xdg_wm_base_) {
        return jaeng::Error::fromMessage((int)jaeng::error_code::platform_error, "Compositor or XDG WM base not available");
    }

    std::unique_ptr<IWindow> window = std::make_unique<WaylandWindow>(this, desc);
    wl_display_roundtrip(display_);
    return window;
}

bool WaylandPlatform::poll_events() {
    if (!display_) return false;

    // libdecor needs to process events too
    if (decor_context_) {
        libdecor_dispatch(decor_context_, 0);
    }

    if (wl_display_prepare_read(display_) == 0) {
        if (wl_display_read_events(display_) == -1) {
            running_ = false;
            return false;
        }
    }
    if (wl_display_dispatch_pending(display_) == -1) {
        running_ = false;
        return false;
    }
    wl_display_flush(display_);
    
    return running_;
}

void WaylandPlatform::show_message_box(const std::string& title, const std::string& content, MessageBoxType type) {
    JAENG_LOG_INFO("[{}] {}", title, content);
}

int WaylandPlatform::run(std::unique_ptr<IApplication> app) {
    if (!app->init()) return -1;

    while (poll_events() && !app->should_close()) {
        app->update();
    }

    app->shutdown();
    return 0;
}

std::unique_ptr<IPlatform> create_platform() {
    return std::make_unique<WaylandPlatform>();
}

} // namespace jaeng::platform
