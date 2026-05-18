#include "metal_layer_bridge.h"
#import <QuartzCore/CAMetalLayer.h>

namespace jaeng::renderer::metal {

void set_layer_sync_enabled(void* layer, bool enabled) {
    CAMetalLayer* metalLayer = (__bridge CAMetalLayer*)layer;
#if TARGET_OS_OSX
    if ([metalLayer respondsToSelector:@selector(setDisplaySyncEnabled:)]) {
        metalLayer.displaySyncEnabled = enabled;
    }
#endif
}

void set_layer_max_drawables(void* layer, uint32_t count) {
    CAMetalLayer* metalLayer = (__bridge CAMetalLayer*)layer;
    metalLayer.maximumDrawableCount = count;
}

} // namespace jaeng::renderer::metal
