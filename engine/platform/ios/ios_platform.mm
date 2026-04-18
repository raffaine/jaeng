#include "ios_platform.h"
#import <UIKit/UIKit.h>
#include "storage/win/filestorage.h"

@interface IOSAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@property (assign, nonatomic) std::unique_ptr<jaeng::platform::IApplication>* app_ptr;
@end

@implementation IOSAppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    CGRect screenBounds = [UIScreen mainScreen].bounds;
    self.window = [[UIWindow alloc] initWithFrame:screenBounds];
    self.window.backgroundColor = [UIColor blackColor];
    
    UIViewController* vc = [[UIViewController alloc] init];
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];
    
    // In a real implementation, we would start a CADisplayLink here
    // to drive the engine's main thread tasks.
    
    return YES;
}

@end

namespace jaeng::platform {

IOSPlatform* IOSPlatform::instance_ = nullptr;

IOSPlatform::IOSPlatform() {
    instance_ = this;
    fileManager_ = std::make_shared<FileManager>();
}

IOSPlatform::~IOSPlatform() {
    instance_ = nullptr;
}

result<std::unique_ptr<IWindow>> IOSPlatform::create_window(const WindowDesc& desc) {
    // On iOS, the window is usually created in the AppDelegate.
    // For this skeleton, we assume the window exists or we return a dummy.
    UIWindow* window = [[UIApplication sharedApplication] keyWindow];
    UIView* view = [window rootViewController].view;
    return jaeng::result<std::unique_ptr<IWindow>>(std::make_unique<IOSWindow>(window, view, desc.width, desc.height));
}

bool IOSPlatform::poll_events() {
    // iOS events are handled by the main run loop.
    return true;
}

void IOSPlatform::show_message_box(const std::string& title, const std::string& content, MessageBoxType type) {
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:[NSString stringWithUTF8String:title.c_str()]
                                                                   message:[NSString stringWithUTF8String:content.c_str()]
                                                            preferredStyle:UIAlertControllerStyleAlert];
    
    UIAlertAction* defaultAction = [UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault
                                                         handler:^(UIAlertAction * action) {}];
    
    [alert addAction:defaultAction];
    [[[UIApplication sharedApplication] keyWindow].rootViewController presentViewController:alert animated:YES completion:nil];
}

int IOSPlatform::run(std::unique_ptr<IApplication> app) {
    @autoreleasepool {
        // UIApplicationMain doesn't return, so the 'app' lifecycle
        // needs careful management.
        return UIApplicationMain(0, nil, nil, NSStringFromClass([IOSAppDelegate class]));
    }
}

// Factory function for iOS if defined
std::unique_ptr<IPlatform> create_platform() {
    return std::make_unique<IOSPlatform>();
}

} // namespace jaeng::platform
