#include <functional>

#ifdef JAENG_APPLE
#import <Foundation/Foundation.h>

extern "C" {
void jaeng_apple_run_in_autorelease_pool(void(*func)(void*), void* context) {
    @autoreleasepool {
        func(context);
    }
}
}
#endif
