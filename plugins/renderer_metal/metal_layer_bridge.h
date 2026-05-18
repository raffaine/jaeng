#pragma once
#include <cstdint>

namespace jaeng::renderer::metal {
    void set_layer_sync_enabled(void* layer, bool enabled);
    void set_layer_max_drawables(void* layer, uint32_t count);
}
