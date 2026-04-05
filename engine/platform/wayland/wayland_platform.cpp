#include "wayland_platform.h"
#include "wayland_window.h"
#include "common/logging.h"
#include <cstring>
#include <iostream>
#include <libdecor.h>
#include <poll.h>

extern "C" {
#include "xdg-shell-client-protocol.h"
}

namespace jaeng::platform {

WaylandPlatform* WaylandPlatform::instance_ = nullptr;

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

    if (display_) {
        decor_context_ = libdecor_new(display_, const_cast<struct libdecor_interface*>(&decor_interface_));
    }
}

WaylandPlatform::~WaylandPlatform() {
    if (decor_context_) {
        libdecor_unref(decor_context_);
        decor_context_ = nullptr;
    }
    if (shm_) {
        wl_shm_destroy(shm_);
        shm_ = nullptr;
    }
    if (xdg_wm_base_) {
        xdg_wm_base_destroy(xdg_wm_base_);
        xdg_wm_base_ = nullptr;
    }
    if (compositor_) {
        wl_compositor_destroy(compositor_);
        compositor_ = nullptr;
    }
    if (registry_) {
        wl_registry_destroy(registry_);
        registry_ = nullptr;
    }
    if (display_) {
        JAENG_LOG_DEBUG("WaylandPlatform::~WaylandPlatform: leaking display_ to prevent driver exit crash");
        // wl_display_disconnect(display_);
        display_ = nullptr;
    }
    instance_ = nullptr;
    JAENG_LOG_DEBUG("WaylandPlatform::~WaylandPlatform: done");
}

jaeng::result<std::unique_ptr<IWindow>> WaylandPlatform::create_window(const WindowDesc& desc) {
    auto window = std::make_unique<WaylandWindow>(this, desc);
    wl_display_roundtrip(display_);
    std::unique_ptr<IWindow> base_window = std::move(window);
    return { std::move(base_window) };
}

bool WaylandPlatform::poll_events() {
    if (!display_) return false;

    while (wl_display_dispatch_pending(display_) > 0);

    if (decor_context_) {
        libdecor_dispatch(decor_context_, 0);
    }

    if (wl_display_prepare_read(display_) == 0) {
        wl_display_flush(display_);

        struct pollfd fds[] = {
            { wl_display_get_fd(display_), POLLIN, 0 },
        };

        if (poll(fds, 1, 0) > 0) {
            wl_display_read_events(display_);
        } else {
            wl_display_cancel_read(display_);
        }
    }

    while (wl_display_dispatch_pending(display_) > 0);
    
    return running_;
}

void WaylandPlatform::show_message_box(const std::string& title, const std::string& content, MessageBoxType type) {
    JAENG_LOG_INFO("[{}] {}", title, content);
}

int WaylandPlatform::run(std::unique_ptr<IApplication> app) {
    if (!display_ || !compositor_) return -1;

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

// Registry callbacks
void WaylandPlatform::handle_registry_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    auto self = static_cast<WaylandPlatform*>(data);
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        self->compositor_ = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, version));
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        self->xdg_wm_base_ = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, version));
        static const struct xdg_wm_base_listener xdg_wm_base_listener = { xdg_wm_base_ping };
        xdg_wm_base_add_listener(self->xdg_wm_base_, &xdg_wm_base_listener, self);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        self->shm_ = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, version));
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        self->seat_ = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, version));
        self->input_.setup_seat(self->seat_);
    }
}

void WaylandPlatform::handle_registry_global_remove(void* data, wl_registry* registry, uint32_t name) {
}

void WaylandPlatform::xdg_wm_base_ping(void* data, xdg_wm_base* xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

const struct wl_registry_listener WaylandPlatform::registry_listener_ = {
    WaylandPlatform::handle_registry_global,
    WaylandPlatform::handle_registry_global_remove
};

static void decor_error(struct libdecor* context, enum libdecor_error error, const char* message) {
    JAENG_LOG_ERROR("libdecor error {}: {}", (int)error, message);
}

const struct libdecor_interface WaylandPlatform::decor_interface_ = {
    decor_error,
};

} // namespace jaeng::platform
