#include "android_window.h"

namespace jaeng::platform {

AndroidWindow::AndroidWindow(struct android_app* state, uint32_t width, uint32_t height)
    : state_(state), width_(width), height_(height), isOpen_(true) {
}

AndroidWindow::~AndroidWindow() {
    destroy();
}

void AndroidWindow::destroy() {
    isOpen_ = false;
}

void* AndroidWindow::get_native_handle() const {
    return state_->window;
}

uint32_t AndroidWindow::get_width() const {
    if (state_->window) {
        return static_cast<uint32_t>(ANativeWindow_getWidth(state_->window));
    }
    return width_;
}

uint32_t AndroidWindow::get_height() const {
    if (state_->window) {
        return static_cast<uint32_t>(ANativeWindow_getHeight(state_->window));
    }
    return height_;
}

bool AndroidWindow::is_open() const {
    return isOpen_;
}

} // namespace jaeng::platform
