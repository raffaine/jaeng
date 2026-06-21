#pragma once
#include "platform/public/platform_api.h"
#include <android_native_app_glue.h>

namespace jaeng::platform {

class AndroidWindow : public IWindow {
public:
    AndroidWindow(struct android_app* state, uint32_t width, uint32_t height);
    ~AndroidWindow() override;

    void destroy() override;
    void* get_native_handle() const override;
    uint32_t get_width() const override;
    uint32_t get_height() const override;
    bool is_open() const override;

private:
    struct android_app* state_;
    uint32_t width_;
    uint32_t height_;
    bool isOpen_;
};

} // namespace jaeng::platform
