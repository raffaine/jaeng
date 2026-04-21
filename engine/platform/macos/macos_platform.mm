#include "macos_platform.h"
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include "storage/win/filestorage.h"

@interface MacOSMetalView : NSView
@end

@implementation MacOSMetalView
- (CALayer *)makeBackingLayer {
    CAMetalLayer* layer = [CAMetalLayer layer];
    return layer;
}
@end

@interface MacOSAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation MacOSAppDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    [NSApp stop:nil];
    // This allows the manual event loop in run() to take over
    NSEvent* ev = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                    location:NSMakePoint(0,0)
                               modifierFlags:0
                                   timestamp:0
                                windowNumber:0
                                     context:nil
                                     subtype:0
                                       data1:0
                                       data2:0];
    [NSApp postEvent:ev atStart:YES];
}
@end

namespace jaeng::platform {

static KeyCode map_macos_key(unsigned short keyCode) {
    switch (keyCode) {
        case 53: return KeyCode::Escape;
        case 49: return KeyCode::Space;
        case 13: return KeyCode::W;
        case 0:  return KeyCode::A;
        case 1:  return KeyCode::S;
        case 2:  return KeyCode::D;
        case 14: return KeyCode::E;
        case 12: return KeyCode::Q;
        case 24: return KeyCode::Plus;
        case 27: return KeyCode::Minus;
        default: return KeyCode::Unknown;
    }
}

MacOSPlatform* MacOSPlatform::instance_ = nullptr;

MacOSPlatform::MacOSPlatform() {
    instance_ = this;
    fileManager_ = std::make_shared<FileManager>();
    auto* fm = static_cast<FileManager*>(fileManager_.get());
    fm->set_base_path(get_base_path());
    fm->set_exists_func([this](const std::string& path) {
        return this->file_exists(path);
    });
    fm->set_path_resolver([this](const std::string& path) {
        return this->resolve_path(path);
    });
}

MacOSPlatform::~MacOSPlatform() {
    instance_ = nullptr;
}

result<std::unique_ptr<IWindow>> MacOSPlatform::create_window(const WindowDesc& desc) {
    NSRect frame = NSMakeRect(0, 0, desc.width, desc.height);
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable;
    
    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    [window setTitle:[NSString stringWithUTF8String:desc.title.c_str()]];
    
    MacOSMetalView* view = [[MacOSMetalView alloc] initWithFrame:frame];
    [view setWantsLayer:YES];
    [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    
    CAMetalLayer* metalLayer = (CAMetalLayer*)view.layer;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metalLayer.contentsScale = window.backingScaleFactor;
    metalLayer.opaque = YES;
    
    [window setContentView:view];
    [window makeKeyAndOrderFront:nil];
    
    return jaeng::result<std::unique_ptr<IWindow>>(std::make_unique<MacOSWindow>(window, view, desc.width, desc.height));
}

bool MacOSPlatform::poll_events() {
    @autoreleasepool {
        for (;;) {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate distantPast]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (!event) break;
            
            [NSApp sendEvent:event];
            
            if (eventCallback_) {
                Event ev{};
                NSWindow* window = [event window];
                if (window) {
                    NSPoint locationInWindow = [event locationInWindow];
                    CGFloat height = [[window contentView] bounds].size.height;
                    // Flip Y to top-left origin
                    int32_t x = (int32_t)locationInWindow.x;
                    int32_t y = (int32_t)(height - locationInWindow.y);
                    
                    switch ([event type]) {
                        case NSEventTypeKeyDown: {
                            if (![event isARepeat]) {
                                KeyCode code = map_macos_key([event keyCode]);
                                input_.set_key_state(code, true);
                                ev.type = Event::Type::KeyDown;
                                ev.key.code = code;
                                eventCallback_(ev);
                            }
                            break;
                        }
                        case NSEventTypeKeyUp: {
                            KeyCode code = map_macos_key([event keyCode]);
                            input_.set_key_state(code, false);
                            ev.type = Event::Type::KeyUp;
                            ev.key.code = code;
                            eventCallback_(ev);
                            break;
                        }
                        case NSEventTypeMouseMoved:
                        case NSEventTypeLeftMouseDragged:
                        case NSEventTypeRightMouseDragged:
                        case NSEventTypeOtherMouseDragged: {
                            input_.set_mouse_pos(x, y);
                            ev.type = Event::Type::MouseMove;
                            ev.mouse.x = x;
                            ev.mouse.y = y;
                            eventCallback_(ev);
                            break;
                        }
                        case NSEventTypeLeftMouseDown:
                        case NSEventTypeRightMouseDown:
                        case NSEventTypeOtherMouseDown: {
                            ev.type = Event::Type::MouseDown;
                            ev.mouse.x = x;
                            ev.mouse.y = y;
                            ev.mouse.button = ([event type] == NSEventTypeLeftMouseDown) ? 272 :
                                              ([event type] == NSEventTypeRightMouseDown) ? 273 : 274;
                            eventCallback_(ev);
                            break;
                        }
                        case NSEventTypeLeftMouseUp:
                        case NSEventTypeRightMouseUp:
                        case NSEventTypeOtherMouseUp: {
                            ev.type = Event::Type::MouseUp;
                            ev.mouse.x = x;
                            ev.mouse.y = y;
                            ev.mouse.button = ([event type] == NSEventTypeLeftMouseUp) ? 272 :
                                              ([event type] == NSEventTypeRightMouseUp) ? 273 : 274;
                            eventCallback_(ev);
                            break;
                        }
                        case NSEventTypeScrollWheel: {
                            ev.type = Event::Type::MouseScroll;
                            ev.scroll.delta = (float)[event scrollingDeltaY];
                            eventCallback_(ev);
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        }
    }
    return true;
}

void MacOSPlatform::show_message_box(const std::string& title, const std::string& content, MessageBoxType type) {
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:[NSString stringWithUTF8String:title.c_str()]];
    [alert setInformativeText:[NSString stringWithUTF8String:content.c_str()]];
    
    switch (type) {
        case MessageBoxType::Info:    [alert setAlertStyle:NSAlertStyleInformational]; break;
        case MessageBoxType::Warning: [alert setAlertStyle:NSAlertStyleWarning]; break;
        case MessageBoxType::Error:   [alert setAlertStyle:NSAlertStyleCritical]; break;
    }
    
    [alert runModal];
}

std::string MacOSPlatform::get_base_path() const {
    char path[1024];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        std::string pathStr = path;
        return pathStr.substr(0, pathStr.find_last_of("/"));
    }
    return ".";
}

bool MacOSPlatform::file_exists(const std::string& path) const {
    return access(path.c_str(), F_OK) == 0;
}

std::string MacOSPlatform::resolve_path(const std::string& path) const {
    if (!path.empty() && path[0] == '/') return path;
    return get_base_path() + "/" + path;
}

int MacOSPlatform::run(std::unique_ptr<IApplication> app) {
    [NSApplication sharedApplication];
    MacOSAppDelegate* delegate = [[MacOSAppDelegate alloc] init];
    [NSApp setDelegate:delegate];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];

    if (!app->init()) return -1;

    app->start_engine_threads();

    while (!app->should_close()) {
        poll_events();
        if (!app->process_main_thread_tasks()) {
            jaeng::platform::thread::yield();
        }
    }

    app->stop_engine_threads();
    app->shutdown();
    return 0;
}

std::unique_ptr<IPlatform> create_platform() {
    return std::make_unique<MacOSPlatform>();
}

} // namespace jaeng::platform
