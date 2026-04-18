#include "ios_window.h"
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>

namespace jaeng::platform {

IOSWindow::IOSWindow(UIWindow* window, UIView* view, uint32_t width, uint32_t height)
    : window_(window), view_(view), width_(width), height_(height) {
}

IOSWindow::~IOSWindow() {
    destroy();
}

void IOSWindow::destroy() {
    if (window_) {
        window_ = nullptr;
        view_ = nullptr;
    }
}

void* IOSWindow::get_native_handle() const {
    return (__bridge void*)view_.layer;
}

} // namespace jaeng::platform
