#pragma once
#include "platform/public/platform_api.h"

namespace jaeng::platform {

class AndroidInput : public IInput {
public:
    AndroidInput() = default;
    ~AndroidInput() override = default;

    bool is_key_down(KeyCode code) const override { return false; } // Basic touch input only for now
    MousePos get_mouse_pos() const override { return mousePos_; }

    void set_mouse_pos(int32_t x, int32_t y) {
        mousePos_.x = x;
        mousePos_.y = y;
    }

private:
    MousePos mousePos_{0, 0};
};

} // namespace jaeng::platform
