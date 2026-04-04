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

static struct libdecor_frame_interface libdecor_frame_iface = {
    .configure = WaylandWindow::handle_configure,
    .close = WaylandWindow::handle_close,
    .commit = WaylandWindow::handle_commit,
};

void WaylandWindow::attach_dummy_buffer() {
    if (dummy_buffer_) {
        wl_buffer_destroy(dummy_buffer_);
        dummy_buffer_ = nullptr;
    }

    int stride = width_ * 4;
    int size = stride * height_;

    const char* shm_name = "/jaeng-shm";
    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        shm_unlink(shm_name);
        fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    }
    shm_unlink(shm_name);
    if (fd < 0) return;

    if (ftruncate(fd, size) < 0) {
        close(fd);
        return;
    }

    unsigned char* data = (unsigned char*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data != MAP_FAILED) {
        for (int i = 0; i < size; i += 4) {
            data[i] = 0xAA;     // B
            data[i + 1] = 0x55; // G
            data[i + 2] = 0x22; // R
            data[i + 3] = 0xFF; // X (Opaque)
        }
        munmap(data, size);
    }

    wl_shm_pool* pool = wl_shm_create_pool(platform_->get_shm(), fd, size);
    dummy_buffer_ = wl_shm_pool_create_buffer(pool, 0, width_, height_, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    wl_surface_attach(surface_, dummy_buffer_, 0, 0);
    wl_surface_damage(surface_, 0, 0, width_, height_);
}

WaylandWindow::WaylandWindow(WaylandPlatform* platform, const WindowDesc& desc)
    : platform_(platform), width_(desc.width), height_(desc.height) {
    
    surface_ = wl_compositor_create_surface(platform_->get_compositor());

    if (platform_->get_libdecor()) {
        frame_ = libdecor_decorate(platform_->get_libdecor(), surface_, &libdecor_frame_iface, this);
        libdecor_frame_set_title(frame_, desc.title.c_str());
        libdecor_frame_set_app_id(frame_, "jaeng.sandbox");
        
        // Enable standard window actions
        // TODO: Investigate why maximize/minimize buttons are missing on some libdecor plugins/themes
        libdecor_frame_set_capabilities(frame_, (libdecor_capabilities)(
            LIBDECOR_ACTION_MOVE | 
            LIBDECOR_ACTION_RESIZE | 
            LIBDECOR_ACTION_MINIMIZE | 
            LIBDECOR_ACTION_FULLSCREEN |
            LIBDECOR_ACTION_CLOSE));

        libdecor_frame_set_min_content_size(frame_, 100, 100);

        libdecor_frame_map(frame_);
    }

    wl_surface_commit(surface_);
}

WaylandWindow::~WaylandWindow() {
    destroy();
}

void WaylandWindow::destroy() {
    if (frame_) {
        libdecor_frame_unref(frame_);
        frame_ = nullptr;
    }
    if (dummy_buffer_) {
        wl_buffer_destroy(dummy_buffer_);
        dummy_buffer_ = nullptr;
    }
    if (surface_) {
        wl_surface_destroy(surface_);
        surface_ = nullptr;
    }
}

void WaylandWindow::handle_configure(struct libdecor_frame* frame, struct libdecor_configuration* configuration, void* user_data) {
    auto self = static_cast<WaylandWindow*>(user_data);
    int width, height;

    if (!libdecor_configuration_get_content_size(configuration, frame, &width, &height)) {
        width = self->width_;
        height = self->height_;
    }

    self->width_ = width;
    self->height_ = height;

    self->attach_dummy_buffer();
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

} // namespace jaeng::platform
