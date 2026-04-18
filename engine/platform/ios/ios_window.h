#pragma once

#include "platform/public/platform_api.h"

#ifdef __OBJC__
@class UIWindow;
@class UIView;
#else
typedef void UIWindow;
typedef void UIView;
#endif

namespace jaeng::platform {

class IOSWindow : public IWindow {
public:
    IOSWindow(UIWindow* window, UIView* view, uint32_t width, uint32_t height);
    ~IOSWindow() override;

    void destroy() override;
    void* get_native_handle() const override;
    uint32_t get_width() const override { return width_; }
    uint32_t get_height() const override { return height_; }
    bool is_open() const override { return window_ != nullptr; }

private:
    UIWindow* window_;
    UIView* view_;
    uint32_t width_;
    uint32_t height_;
};

} // namespace jaeng::platform
