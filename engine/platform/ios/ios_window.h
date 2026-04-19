#pragma once

#include "platform/public/platform_api.h"

namespace jaeng::platform {

class IOSWindow : public IWindow {
public:
    IOSWindow(void* window, void* metalLayer, uint32_t width, uint32_t height);
    ~IOSWindow() override;

    void destroy() override;
    void* get_native_handle() const override;
    uint32_t get_width() const override { return width_; }
    uint32_t get_height() const override { return height_; }
    bool is_open() const override { return window_ != nullptr; }

private:
    void* window_;
    void* metalLayer_;
    uint32_t width_;
    uint32_t height_;
};

} // namespace jaeng::platform
