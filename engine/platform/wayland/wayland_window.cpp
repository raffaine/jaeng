#include "wayland_window.h"
#include "wayland_platform.h"
#include "common/logging.h"
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <libdecor.h>

extern "C" {
#include "xdg-shell-client-protocol.h"
}

namespace jaeng::platform {

WaylandWindow::WaylandWindow(WaylandPlatform* platform, const WindowDesc& desc)
    : platform_(platform), width_(desc.width), height_(desc.height) {
    
    surface_ = wl_compositor_create_surface(platform_->compositor_);
    
    frame_ = libdecor_decorate(platform_->decor_context_, surface_, const_cast<struct libdecor_frame_interface*>(&decor_frame_interface_), this);
    libdecor_frame_set_title(frame_, desc.title.c_str());
    libdecor_frame_map(frame_);
}

WaylandWindow::~WaylandWindow() {
    destroy();
}

void WaylandWindow::destroy() {
    if (frame_) {
        libdecor_frame_unref(frame_);
        frame_ = nullptr;
    }
    if (surface_) {
        wl_surface_destroy(surface_);
        surface_ = nullptr;
    }
}

void WaylandWindow::attach_dummy_buffer() {
}

// libdecor callbacks
void WaylandWindow::handle_configure(struct libdecor_frame* frame, struct libdecor_configuration* configuration, void* user_data) {
    auto self = static_cast<WaylandWindow*>(user_data);
    int width, height;

    if (!libdecor_configuration_get_content_size(configuration, frame, &width, &height)) {
        width = self->width_;
        height = self->height_;
    }

    if (width <= 0) width = 1280;
    if (height <= 0) height = 720;

    self->width_ = width;
    self->height_ = height;
    self->mapped_ = true;

    // libdecor expects us to create a new state
    struct libdecor_state* state = libdecor_state_new(width, height);
    libdecor_frame_commit(frame, state, configuration);
    libdecor_state_free(state);

    // Ensure surface is committed after attach
    wl_surface_commit(self->surface_);

    Event ev{};
    ev.type = Event::Type::WindowResize;
    ev.resize.width = width;
    ev.resize.height = height;
    if (self->platform_->eventCallback_) self->platform_->eventCallback_(ev);
}

void WaylandWindow::handle_close(struct libdecor_frame* frame, void* user_data) {
    auto self = static_cast<WaylandWindow*>(user_data);
    self->open_ = false;

    Event ev{};
    ev.type = Event::Type::WindowClose;
    if (self->platform_->eventCallback_) self->platform_->eventCallback_(ev);
}

void WaylandWindow::handle_commit(struct libdecor_frame* frame, void* user_data) {
    auto self = static_cast<WaylandWindow*>(user_data);
    wl_surface_commit(self->surface_);
}

const struct libdecor_frame_interface WaylandWindow::decor_frame_interface_ = {
    WaylandWindow::handle_configure,
    WaylandWindow::handle_close,
    WaylandWindow::handle_commit,
};

} // namespace jaeng::platform
