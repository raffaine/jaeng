#pragma once

#include "platform/public/platform_api.h"
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <unordered_map>

namespace jaeng::platform {

class WaylandInput : public IInput {
public:
    WaylandInput();
    ~WaylandInput();

    bool is_key_down(KeyCode code) const override;
    MousePos get_mouse_pos() const override;

    void set_key_state(KeyCode code, bool down) { keys_[code] = down; }
    void set_mouse_pos(int32_t x, int32_t y) { mousePos_ = {x, y}; }

    void setup_seat(wl_seat* seat);
    void cleanup();

private:
    static void seat_handle_capabilities(void* data, wl_seat* seat, uint32_t caps);
    static void seat_handle_name(void* data, wl_seat* seat, const char* name);
    static const wl_seat_listener seat_listener_;

    // Pointer events
    static void pointer_handle_enter(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy);
    static void pointer_handle_leave(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface);
    static void pointer_handle_motion(void* data, wl_pointer* pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy);
    static void pointer_handle_button(void* data, wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
    static void pointer_handle_axis(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
    static void pointer_handle_frame(void* data, wl_pointer* pointer);
    static void pointer_handle_axis_source(void* data, wl_pointer* pointer, uint32_t axis_source);
    static void pointer_handle_axis_stop(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis);
    static void pointer_handle_axis_discrete(void* data, wl_pointer* pointer, uint32_t axis, int32_t discrete);
    static void pointer_handle_axis_value120(void* data, wl_pointer* pointer, uint32_t axis, int32_t value120);
    static void pointer_handle_axis_relative_direction(void* data, wl_pointer* pointer, uint32_t axis, uint32_t direction);
    static const wl_pointer_listener pointer_listener_;

    // Keyboard events
    static void keyboard_handle_keymap(void* data, wl_keyboard* keyboard, uint32_t format, int32_t fd, uint32_t size);
    static void keyboard_handle_enter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface, wl_array* keys);
    static void keyboard_handle_leave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface);
    static void keyboard_handle_key(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
    static void keyboard_handle_modifiers(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group);
    static void keyboard_handle_repeat_info(void* data, wl_keyboard* keyboard, int32_t rate, int32_t delay);
    static const wl_keyboard_listener keyboard_listener_;

    wl_seat* seat_ = nullptr;
    wl_pointer* pointer_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;

    std::unordered_map<KeyCode, bool> keys_;
    MousePos mousePos_ = {0, 0};

    // xkb context
    xkb_context* xkb_ctx_ = nullptr;
    xkb_keymap* xkb_keymap_ = nullptr;
    xkb_state* xkb_state_ = nullptr;
};

} // namespace jaeng::platform
