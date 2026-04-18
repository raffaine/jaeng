#include "macos_platform.h"
#import <AppKit/AppKit.h>
#include "storage/win/filestorage.h"

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

MacOSPlatform* MacOSPlatform::instance_ = nullptr;

MacOSPlatform::MacOSPlatform() {
    instance_ = this;
    fileManager_ = std::make_shared<FileManager>();
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
    [window makeKeyAndOrderFront:nil];
    
    NSView* view = [[NSView alloc] initWithFrame:frame];
    [view setWantsLayer:YES];
    [window setContentView:view];
    
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
                switch ([event type]) {
                    case NSEventTypeKeyDown:
                        // Map keys here
                        break;
                    case NSEventTypeLeftMouseDown:
                        ev.type = Event::Type::MouseDown;
                        ev.mouse.button = 0;
                        eventCallback_(ev);
                        break;
                    default:
                        break;
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
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
