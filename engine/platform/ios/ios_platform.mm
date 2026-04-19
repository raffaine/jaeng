#include "ios_platform.h"
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CADisplayLink.h>
#include "storage/win/filestorage.h"

static std::unique_ptr<jaeng::platform::IApplication> g_app;
static UIWindow* g_window = nil;

@interface IOSAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@end

@implementation IOSAppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    // Engine initialization must happen here, synchronously and fully
    if (g_app) {
        g_app->taskScheduler().initialize();
        if (g_app->init()) {
            g_app->start_engine_threads();
        }
    }
    return YES;
}

- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)connectingSceneSession options:(UISceneConnectionOptions *)options {
    UISceneConfiguration* config = [[UISceneConfiguration alloc] initWithName:@"Default Configuration" sessionRole:connectingSceneSession.role];
    config.delegateClass = [NSClassFromString(@"IOSSceneDelegate") class];
    return config;
}
@end

@interface IOSSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property (strong, nonatomic) UIWindow *window;
@property (strong, nonatomic) CADisplayLink *displayLink;
@end

@implementation IOSSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)connectionOptions {
    UIWindowScene *windowScene = (UIWindowScene *)scene;
    self.window = [[UIWindow alloc] initWithWindowScene:windowScene];
    self.window.backgroundColor = [UIColor blackColor];
    
    UIViewController* vc = [[UIViewController alloc] init];
    UIView* view = [[UIView alloc] initWithFrame:windowScene.screen.bounds];
    view.backgroundColor = [UIColor blackColor];
    
    CAMetalLayer* metalLayer = [CAMetalLayer layer];
    metalLayer.frame = view.bounds;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metalLayer.contentsScale = windowScene.screen.nativeScale;
    [view.layer addSublayer:metalLayer];
    
    vc.view = view;
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];
    g_window = self.window;
    
    self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(engineLoop:)];
    [self.displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)engineLoop:(CADisplayLink *)sender {
    if (g_app) g_app->run_one_frame();
}
@end

namespace jaeng::platform {

IOSPlatform* IOSPlatform::instance_ = nullptr;

IOSPlatform::IOSPlatform() {
    instance_ = this;
    fileManager_ = std::make_shared<FileManager>();
    auto* fm = static_cast<FileManager*>(fileManager_.get());
    fm->set_base_path([[[NSBundle mainBundle] resourcePath] UTF8String]);
}

IOSPlatform::~IOSPlatform() { instance_ = nullptr; }

result<std::unique_ptr<IWindow>> IOSPlatform::create_window(const WindowDesc& desc) {
    if (!g_window) return Error::fromMessage(-1, "Window not yet created");
    return jaeng::result<std::unique_ptr<IWindow>>(std::make_unique<IOSWindow>(g_window, g_window.rootViewController.view, desc.width, desc.height));
}

bool IOSPlatform::poll_events() { return true; }

void IOSPlatform::show_message_box(const std::string& title, const std::string& content, MessageBoxType type) {}

std::string IOSPlatform::get_base_path() const {
    return [[[NSBundle mainBundle] resourcePath] UTF8String];
}

bool IOSPlatform::file_exists(const std::string& path) const {
    NSString* fullPath = [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:[NSString stringWithUTF8String:path.c_str()]];
    return [[NSFileManager defaultManager] fileExistsAtPath:fullPath];
}

std::string IOSPlatform::resolve_path(const std::string& path) const {
    NSString* fullPath = [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:[NSString stringWithUTF8String:path.c_str()]];
    return [fullPath UTF8String];
}

int IOSPlatform::run(std::unique_ptr<IApplication> app) {
    g_app = std::move(app);
    return UIApplicationMain(0, nil, nil, NSStringFromClass([IOSAppDelegate class]));
}

std::unique_ptr<IPlatform> create_platform() { return std::make_unique<IOSPlatform>(); }

} // namespace jaeng::platform
