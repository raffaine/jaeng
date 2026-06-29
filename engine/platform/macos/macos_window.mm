#include "macos_window.h"
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

namespace jaeng::platform {

MacOSWindow::MacOSWindow(NSWindow* window, NSView* view, uint32_t width, uint32_t height)
    : window_(window), view_(view), width_(width), height_(height) {
}

MacOSWindow::~MacOSWindow() {
    destroy();
}

void MacOSWindow::destroy() {
    if (window_) {
        [window_ close];
        window_ = nullptr;
        view_ = nullptr;
    }
}

void* MacOSWindow::get_native_handle() const {
    return (__bridge void*)view_.layer;
}

uint32_t MacOSWindow::get_width() const {
    if (view_) {
        NSRect bounds = [view_ bounds];
        return static_cast<uint32_t>(bounds.size.width);
    }
    return width_;
}

uint32_t MacOSWindow::get_height() const {
    if (view_) {
        NSRect bounds = [view_ bounds];
        return static_cast<uint32_t>(bounds.size.height);
    }
    return height_;
}

} // namespace jaeng::platform
