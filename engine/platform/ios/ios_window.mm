#include "ios_window.h"

namespace jaeng::platform {

IOSWindow::IOSWindow(void* window, void* metalLayer, uint32_t width, uint32_t height)
    : window_(window), metalLayer_(metalLayer), width_(width), height_(height) {
}

IOSWindow::~IOSWindow() {
    destroy();
}

void IOSWindow::destroy() {
    if (window_) {
        window_ = nullptr;
        metalLayer_ = nullptr;
    }
}

void* IOSWindow::get_native_handle() const {
    return metalLayer_;
}

} // namespace jaeng::platform
