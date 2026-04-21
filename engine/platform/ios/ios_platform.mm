#include "ios_platform.h"
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CADisplayLink.h>
#import <Metal/Metal.h>
#include "storage/win/filestorage.h"
#include "common/async/awaiters.h"
#include <atomic>

static std::unique_ptr<jaeng::platform::IApplication> g_app;
static UIWindow* g_window = nil;
static void* g_metalLayer = nullptr;
static uint32_t g_cachedWidth = 0;
static uint32_t g_cachedHeight = 0;
static std::atomic<bool> g_engineInitialized{false};
static std::atomic<bool> g_engineInitStarted{false};
static std::atomic<bool> g_isForeground{false};

@interface IOSMetalView : UIView
@end

@implementation IOSMetalView
+ (Class)layerClass {
    return [CAMetalLayer class];
}

- (BOOL)canBecomeFirstResponder {
    return YES;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!g_app || g_app->getConfig().inputMode != jaeng::platform::InputMode::Mouse) return;
    UITouch* touch = [touches anyObject];
    CGPoint loc = [touch locationInView:self];
    int32_t x = (int32_t)(loc.x * self.contentScaleFactor);
    int32_t y = (int32_t)(loc.y * self.contentScaleFactor);
    
    auto& input = static_cast<jaeng::platform::IOSInput&>(g_app->platform().get_input());
    input.set_mouse_pos(x, y);
    
    jaeng::platform::Event ev{};
    ev.type = jaeng::platform::Event::Type::MouseDown;
    ev.mouse.x = x;
    ev.mouse.y = y;
    ev.mouse.button = 272; // Left
    g_app->on_event(ev);
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!g_app || g_app->getConfig().inputMode != jaeng::platform::InputMode::Mouse) return;
    UITouch* touch = [touches anyObject];
    CGPoint loc = [touch locationInView:self];
    int32_t x = (int32_t)(loc.x * self.contentScaleFactor);
    int32_t y = (int32_t)(loc.y * self.contentScaleFactor);
    
    auto& input = static_cast<jaeng::platform::IOSInput&>(g_app->platform().get_input());
    input.set_mouse_pos(x, y);
    
    jaeng::platform::Event ev{};
    ev.type = jaeng::platform::Event::Type::MouseMove;
    ev.mouse.x = x;
    ev.mouse.y = y;
    g_app->on_event(ev);
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!g_app || g_app->getConfig().inputMode != jaeng::platform::InputMode::Mouse) return;
    UITouch* touch = [touches anyObject];
    CGPoint loc = [touch locationInView:self];
    int32_t x = (int32_t)(loc.x * self.contentScaleFactor);
    int32_t y = (int32_t)(loc.y * self.contentScaleFactor);
    
    auto& input = static_cast<jaeng::platform::IOSInput&>(g_app->platform().get_input());
    input.set_mouse_pos(x, y);
    
    jaeng::platform::Event ev{};
    ev.type = jaeng::platform::Event::Type::MouseUp;
    ev.mouse.x = x;
    ev.mouse.y = y;
    ev.mouse.button = 272; // Left
    g_app->on_event(ev);
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self touchesEnded:touches withEvent:event];
}

static jaeng::platform::KeyCode map_ios_key(UIKeyboardHIDUsage keyCode) {
    switch (keyCode) {
        case UIKeyboardHIDUsageKeyboardEscape: return jaeng::platform::KeyCode::Escape;
        case UIKeyboardHIDUsageKeyboardSpacebar: return jaeng::platform::KeyCode::Space;
        case UIKeyboardHIDUsageKeyboardW: return jaeng::platform::KeyCode::W;
        case UIKeyboardHIDUsageKeyboardA: return jaeng::platform::KeyCode::A;
        case UIKeyboardHIDUsageKeyboardS: return jaeng::platform::KeyCode::S;
        case UIKeyboardHIDUsageKeyboardD: return jaeng::platform::KeyCode::D;
        case UIKeyboardHIDUsageKeyboardE: return jaeng::platform::KeyCode::E;
        case UIKeyboardHIDUsageKeyboardQ: return jaeng::platform::KeyCode::Q;
        case UIKeyboardHIDUsageKeyboardEqualSign: return jaeng::platform::KeyCode::Plus;
        case UIKeyboardHIDUsageKeyboardHyphen: return jaeng::platform::KeyCode::Minus;
        default: return jaeng::platform::KeyCode::Unknown;
    }
}

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    if (!g_app) {
        [super pressesBegan:presses withEvent:event];
        return;
    }
    for (UIPress* press in presses) {
        if (press.key) {
            jaeng::platform::KeyCode code = map_ios_key(press.key.keyCode);
            if (code != jaeng::platform::KeyCode::Unknown) {
                auto& input = static_cast<jaeng::platform::IOSInput&>(g_app->platform().get_input());
                input.set_key_state(code, true);
                
                jaeng::platform::Event ev{};
                ev.type = jaeng::platform::Event::Type::KeyDown;
                ev.key.code = code;
                g_app->on_event(ev);
            }
        }
    }
}

- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    if (!g_app) {
        [super pressesEnded:presses withEvent:event];
        return;
    }
    for (UIPress* press in presses) {
        if (press.key) {
            jaeng::platform::KeyCode code = map_ios_key(press.key.keyCode);
            if (code != jaeng::platform::KeyCode::Unknown) {
                auto& input = static_cast<jaeng::platform::IOSInput&>(g_app->platform().get_input());
                input.set_key_state(code, false);
                
                jaeng::platform::Event ev{};
                ev.type = jaeng::platform::Event::Type::KeyUp;
                ev.key.code = code;
                g_app->on_event(ev);
            }
        }
    }
}

- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    [self pressesEnded:presses withEvent:event];
}

@end

@interface IOSSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property (strong, nonatomic) UIWindow *window;
@property (strong, nonatomic) CADisplayLink *displayLink;
@end

@interface IOSAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@end

@implementation IOSAppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    return YES;
}

- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)connectingSceneSession options:(UISceneConnectionOptions *)options {
    UISceneConfiguration* config = [[UISceneConfiguration alloc] initWithName:@"Default Configuration" sessionRole:connectingSceneSession.role];
    config.delegateClass = [IOSSceneDelegate class];
    return config;
}
@end

@implementation IOSSceneDelegate

static id<MTLDevice> g_mainDevice = nil;

- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)connectionOptions {
    UIWindowScene *windowScene = (UIWindowScene *)scene;
    self.window = [[UIWindow alloc] initWithWindowScene:windowScene];
    self.window.backgroundColor = [UIColor blackColor];
    
    UIViewController* vc = [[UIViewController alloc] init];
    IOSMetalView* view = [[IOSMetalView alloc] initWithFrame:windowScene.screen.bounds];
    
    g_mainDevice = MTLCreateSystemDefaultDevice();
    CGFloat scale = windowScene.screen.nativeScale;
    g_cachedWidth = (uint32_t)(view.bounds.size.width * scale);
    g_cachedHeight = (uint32_t)(view.bounds.size.height * scale);

    CAMetalLayer* metalLayer = (CAMetalLayer*)view.layer;
    metalLayer.device = g_mainDevice;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metalLayer.contentsScale = scale;
    metalLayer.drawableSize = CGSizeMake(g_cachedWidth, g_cachedHeight);
    metalLayer.opaque = YES;
    metalLayer.presentsWithTransaction = NO;
    metalLayer.frame = view.bounds;
    metalLayer.allowsNextDrawableTimeout = NO; 
    
    // Explicitly retain for the static reference to avoid deallocation races
    g_metalLayer = (__bridge_retained void*)metalLayer;
    
    vc.view = view;
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];
    [view becomeFirstResponder];
    g_window = self.window;
    
    JAENG_LOG_INFO("[iOS] Scene connected, size: {}x{}, Metal device: {}", 
                  g_cachedWidth, g_cachedHeight, [g_mainDevice.name UTF8String]);
    
    self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(engineLoop:)];
    [self.displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)sceneDidBecomeActive:(UIScene *)scene {
    JAENG_LOG_INFO("[iOS] Scene became active");
    g_isForeground = true;
}

- (void)sceneWillResignActive:(UIScene *)scene {
    JAENG_LOG_INFO("[iOS] Scene will resign active");
    g_isForeground = false;
}

- (void)engineLoop:(CADisplayLink *)sender {
    if (!g_app) return;

    @autoreleasepool {
        if (!g_engineInitialized.load(std::memory_order_acquire)) {
            if (!g_engineInitStarted.exchange(true, std::memory_order_acq_rel)) {
                JAENG_LOG_INFO("[iOS] Dispatching background engine init");
                dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
                    @autoreleasepool {
                        jaeng::async::set_current_scheduler(&(g_app->taskScheduler()));
                        if (g_app->init((__bridge void*)g_mainDevice)) {
                            g_engineInitialized.store(true, std::memory_order_release);
                            JAENG_LOG_INFO("[iOS] Engine background init completed successfully");
                        } else {
                            JAENG_LOG_ERROR("[iOS] Engine background init failed");
                        }
                    }
                });
            }
            return; 
        }

        jaeng::async::set_current_scheduler(&(g_app->taskScheduler()));
        g_app->process_main_thread_tasks();
    }
}
@end

static std::string g_cachedResourcePath;

namespace jaeng::platform {

IOSPlatform* IOSPlatform::instance_ = nullptr;

IOSPlatform::IOSPlatform() {
    instance_ = this;
    fileManager_ = std::make_shared<FileManager>();
    auto* fm = static_cast<FileManager*>(fileManager_.get());
    @autoreleasepool {
        g_cachedResourcePath = [[[NSBundle mainBundle] resourcePath] UTF8String];
    }
    fm->set_base_path(g_cachedResourcePath);
    fm->set_exists_func([this](const std::string& path) { return this->file_exists(path); });
    fm->set_path_resolver([this](const std::string& path) { return this->resolve_path(path); });
}

IOSPlatform::~IOSPlatform() { 
    instance_ = nullptr; 
    if (g_metalLayer) {
        CFRelease(g_metalLayer);
        g_metalLayer = nullptr;
    }
}

result<std::unique_ptr<IWindow>> IOSPlatform::create_window(const WindowDesc& desc) {
    if (!g_window || !g_metalLayer) return Error::fromMessage(-1, "Window not yet created");
    return jaeng::result<std::unique_ptr<IWindow>>(std::make_unique<IOSWindow>((__bridge void*)g_window, g_metalLayer, g_cachedWidth, g_cachedHeight));
}

bool IOSPlatform::poll_events() { return true; }

void IOSPlatform::show_message_box(const std::string& title, const std::string& content, MessageBoxType type) {
    dispatch_async(dispatch_get_main_queue(), ^{
        UIAlertController* alert = [UIAlertController alertControllerWithTitle:[NSString stringWithUTF8String:title.c_str()]
                                                                       message:[NSString stringWithUTF8String:content.c_str()]
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        [g_window.rootViewController presentViewController:alert animated:YES completion:nil];
    });
}

std::string IOSPlatform::get_base_path() const { return g_cachedResourcePath; }

bool IOSPlatform::file_exists(const std::string& path) const {
    @autoreleasepool {
        if (path.empty()) return false;
        NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
        if ([nsPath isAbsolutePath]) return [[NSFileManager defaultManager] fileExistsAtPath:nsPath];
        NSString* nsBasePath = [NSString stringWithUTF8String:g_cachedResourcePath.c_str()];
        NSString* fullPath = [nsBasePath stringByAppendingPathComponent:nsPath];
        return [[NSFileManager defaultManager] fileExistsAtPath:fullPath];
    }
}

std::string IOSPlatform::resolve_path(const std::string& path) const {
    @autoreleasepool {
        if (path.empty()) return path;
        if (path[0] == '/') return path;
        NSString* nsBasePath = [NSString stringWithUTF8String:g_cachedResourcePath.c_str()];
        NSString* relPath = [NSString stringWithUTF8String:path.c_str()];
        NSString* fullPath = [nsBasePath stringByAppendingPathComponent:relPath];
        if ([[NSFileManager defaultManager] fileExistsAtPath:fullPath]) return [fullPath UTF8String];
        NSString* fileName = [relPath lastPathComponent];
        NSString* flatPath = [nsBasePath stringByAppendingPathComponent:fileName];
        if ([[NSFileManager defaultManager] fileExistsAtPath:flatPath]) return [flatPath UTF8String];
        return [fullPath UTF8String];
    }
}

bool IOSPlatform::is_foreground() const { return g_isForeground.load(std::memory_order_acquire); }

int IOSPlatform::run(std::unique_ptr<IApplication> app) {
    g_app = std::move(app);
    return UIApplicationMain(0, nullptr, nil, NSStringFromClass([IOSAppDelegate class]));
}

std::unique_ptr<IPlatform> create_platform() { return std::make_unique<IOSPlatform>(); }

} // namespace jaeng::platform

