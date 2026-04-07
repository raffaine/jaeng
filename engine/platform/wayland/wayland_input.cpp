#include "wayland_input.h"
#include "common/logging.h"
#include <unistd.h>
#include <sys/mman.h>
#include <cstring>
#include <iostream>
#include "wayland_platform.h"

namespace jaeng::platform {

static KeyCode map_xkb_key(xkb_keysym_t sym) {
    switch (sym) {
        case XKB_KEY_Escape: return KeyCode::Escape;
        case XKB_KEY_space:  return KeyCode::Space;
        case XKB_KEY_w:      return KeyCode::W;
        case XKB_KEY_a:      return KeyCode::A;
        case XKB_KEY_s:      return KeyCode::S;
        case XKB_KEY_d:      return KeyCode::D;
        case XKB_KEY_q:      return KeyCode::Q;
        case XKB_KEY_e:      return KeyCode::E;
        case XKB_KEY_plus:   return KeyCode::Plus;
        case XKB_KEY_minus:  return KeyCode::Minus;
        case XKB_KEY_equal:  return KeyCode::Equal;
        case XKB_KEY_underscore: return KeyCode::Underscore;
        default:             return KeyCode::Unknown;
    }
}

const wl_seat_listener WaylandInput::seat_listener_ = {
    .capabilities = WaylandInput::seat_handle_capabilities,
    .name = WaylandInput::seat_handle_name,
};

const wl_pointer_listener WaylandInput::pointer_listener_ = {
    .enter = WaylandInput::pointer_handle_enter,
    .leave = WaylandInput::pointer_handle_leave,
    .motion = WaylandInput::pointer_handle_motion,
    .button = WaylandInput::pointer_handle_button,
    .axis = WaylandInput::pointer_handle_axis,
    .frame = WaylandInput::pointer_handle_frame,
    .axis_source = WaylandInput::pointer_handle_axis_source,
    .axis_stop = WaylandInput::pointer_handle_axis_stop,
    .axis_discrete = WaylandInput::pointer_handle_axis_discrete,
    .axis_value120 = WaylandInput::pointer_handle_axis_value120,
    .axis_relative_direction = WaylandInput::pointer_handle_axis_relative_direction,
};

const wl_keyboard_listener WaylandInput::keyboard_listener_ = {
    .keymap = WaylandInput::keyboard_handle_keymap,
    .enter = WaylandInput::keyboard_handle_enter,
    .leave = WaylandInput::keyboard_handle_leave,
    .key = WaylandInput::keyboard_handle_key,
    .modifiers = WaylandInput::keyboard_handle_modifiers,
    .repeat_info = WaylandInput::keyboard_handle_repeat_info,
};

WaylandInput::WaylandInput() {
    xkb_ctx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
}

WaylandInput::~WaylandInput() {
    cleanup();
    if (xkb_ctx_) xkb_context_unref(xkb_ctx_);
}

void WaylandInput::cleanup() {
    if (xkb_state_) {
        xkb_state_unref(xkb_state_);
        xkb_state_ = nullptr;
    }
    if (xkb_keymap_) {
        xkb_keymap_unref(xkb_keymap_);
        xkb_keymap_ = nullptr;
    }
    if (pointer_) {
        wl_pointer_destroy(pointer_);
        pointer_ = nullptr;
    }
    if (keyboard_) {
        wl_keyboard_destroy(keyboard_);
        keyboard_ = nullptr;
    }
}

bool WaylandInput::is_key_down(KeyCode code) const {
    auto it = keys_.find(code);
    return (it != keys_.end()) ? it->second : false;
}

MousePos WaylandInput::get_mouse_pos() const {
    return mousePos_;
}

void WaylandInput::setup_seat(wl_seat* seat) {
    seat_ = seat;
    wl_seat_add_listener(seat, &seat_listener_, this);
}

void WaylandInput::seat_handle_capabilities(void* data, wl_seat* seat, uint32_t caps) {
    auto self = static_cast<WaylandInput*>(data);
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !self->pointer_) {
        self->pointer_ = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(self->pointer_, &pointer_listener_, self);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && self->pointer_) {
        wl_pointer_destroy(self->pointer_);
        self->pointer_ = nullptr;
    }

    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !self->keyboard_) {
        self->keyboard_ = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(self->keyboard_, &keyboard_listener_, self);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && self->keyboard_) {
        wl_keyboard_destroy(self->keyboard_);
        self->keyboard_ = nullptr;
    }
}

void WaylandInput::seat_handle_name(void* data, wl_seat* seat, const char* name) {
}

// Pointer
void WaylandInput::pointer_handle_enter(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy) {
}
void WaylandInput::pointer_handle_leave(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface) {
}
void WaylandInput::pointer_handle_motion(void* data, wl_pointer* pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
    auto self = static_cast<WaylandInput*>(data);
    int32_t x = wl_fixed_to_int(sx);
    int32_t y = wl_fixed_to_int(sy);
    self->set_mouse_pos(x, y);

    static uint32_t motionCount = 0;
    if (motionCount++ % 100 == 0) {
        JAENG_LOG_DEBUG("Mouse Motion: {}, {}", x, y);
    }

    Event ev{};
    ev.type = Event::Type::MouseMove;
    ev.mouse.x = x;
    ev.mouse.y = y;
    if (WaylandPlatform::instance_->eventCallback_) WaylandPlatform::instance_->eventCallback_(ev);
}
void WaylandInput::pointer_handle_button(void* data, wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    auto self = static_cast<WaylandInput*>(data);
    bool down = (state == WL_POINTER_BUTTON_STATE_PRESSED);
    JAENG_LOG_DEBUG("Mouse button: {} {}", button, down ? "Down" : "Up");

    Event ev{};
    ev.type = down ? Event::Type::MouseDown : Event::Type::MouseUp;
    ev.mouse.x = self->mousePos_.x;
    ev.mouse.y = self->mousePos_.y;
    ev.mouse.button = button;
    if (WaylandPlatform::instance_->eventCallback_) WaylandPlatform::instance_->eventCallback_(ev);
}
void WaylandInput::pointer_handle_axis(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
    JAENG_LOG_DEBUG("Mouse Axis: {} {}", axis, wl_fixed_to_double(value));
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        Event ev{};
        ev.type = Event::Type::MouseScroll;
        ev.scroll.delta = -wl_fixed_to_double(value);
        if (WaylandPlatform::instance_->eventCallback_) WaylandPlatform::instance_->eventCallback_(ev);
    }
}

void WaylandInput::pointer_handle_frame(void* data, wl_pointer* pointer) {
}

void WaylandInput::pointer_handle_axis_source(void* data, wl_pointer* pointer, uint32_t axis_source) {
}

void WaylandInput::pointer_handle_axis_stop(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis) {
}

void WaylandInput::pointer_handle_axis_discrete(void* data, wl_pointer* pointer, uint32_t axis, int32_t discrete) {
}

void WaylandInput::pointer_handle_axis_value120(void* data, wl_pointer* pointer, uint32_t axis, int32_t value120) {
}

void WaylandInput::pointer_handle_axis_relative_direction(void* data, wl_pointer* pointer, uint32_t axis, uint32_t direction) {
}

// Keyboard
void WaylandInput::keyboard_handle_keymap(void* data, wl_keyboard* keyboard, uint32_t format, int32_t fd, uint32_t size) {
    auto self = static_cast<WaylandInput*>(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char* map_str = (char*)mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map_str == MAP_FAILED) {
        close(fd);
        return;
    }

    if (self->xkb_keymap_) xkb_keymap_unref(self->xkb_keymap_);
    self->xkb_keymap_ = xkb_keymap_new_from_string(self->xkb_ctx_, map_str, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_str, size);
    close(fd);

    if (self->xkb_state_) xkb_state_unref(self->xkb_state_);
    self->xkb_state_ = xkb_state_new(self->xkb_keymap_);
}

void WaylandInput::keyboard_handle_enter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface, wl_array* keys) {
}
void WaylandInput::keyboard_handle_leave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface) {
}
void WaylandInput::keyboard_handle_key(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    auto self = static_cast<WaylandInput*>(data);
    uint32_t scancode = key + 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(self->xkb_state_, scancode);
    KeyCode code = map_xkb_key(sym);

    bool down = (state == WL_KEYBOARD_KEY_STATE_PRESSED);
    self->set_key_state(code, down);

    JAENG_LOG_DEBUG("Key: {} (code {}) {}", (uint32_t)code, key, down ? "Down" : "Up");

    Event ev{};
    ev.type = down ? Event::Type::KeyDown : Event::Type::KeyUp;
    ev.key.code = code;
    if (WaylandPlatform::instance_->eventCallback_) WaylandPlatform::instance_->eventCallback_(ev);
}
void WaylandInput::keyboard_handle_modifiers(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
    auto self = static_cast<WaylandInput*>(data);
    xkb_state_update_mask(self->xkb_state_, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

void WaylandInput::keyboard_handle_repeat_info(void* data, wl_keyboard* keyboard, int32_t rate, int32_t delay) {
}

} // namespace jaeng::platform
