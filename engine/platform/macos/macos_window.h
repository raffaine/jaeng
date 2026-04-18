#pragma once

#include "platform/public/platform_api.h"

#ifdef __OBJC__
@class NSWindow;
@class NSView;
#else
typedef void NSWindow;
typedef void NSView;
#endif

namespace jaeng::platform {

class MacOSWindow : public IWindow {
public:
    MacOSWindow(NSWindow* window, NSView* view, uint32_t width, uint32_t height);
    ~MacOSWindow() override;

    void destroy() override;
    void* get_native_handle() const override;
    uint32_t get_width() const override { return width_; }
    uint32_t get_height() const override { return height_; }
    bool is_open() const override { return window_ != nullptr; }

private:
    NSWindow* window_;
    NSView* view_;
    uint32_t width_;
    uint32_t height_;
};

} // namespace jaeng::platform
