#include "android_platform.h"
#include "android_window.h"
#include "android_input.h"
#include "android_process.h"
#include "storage/win/filestorage.h"
#include "common/logging.h"
#include <android/log.h>
#include <android/asset_manager.h>

namespace jaeng::platform {

AndroidPlatform::AndroidPlatform(struct android_app* state)
    : state_(state) {
    state_->userData = this;
    state_->onAppCmd = AndroidPlatform::onAppCmd;
    state_->onInputEvent = AndroidPlatform::onInputEvent;
    
    input_ = std::make_unique<AndroidInput>();
    processManager_ = std::make_unique<AndroidProcessManager>();
    fileManager_ = std::make_shared<jaeng::FileManager>();
    auto fm = std::dynamic_pointer_cast<jaeng::FileManager>(fileManager_);
    if (fm && state->activity->assetManager) {
        AAssetManager* assetManager = state->activity->assetManager;
        
        auto sanitizePath = [](const std::string& p) {
            std::string path = p;
            if (path.find("Roboto-Regular.ttf") != std::string::npos) return std::string("Roboto-Regular.ttf");
            if (path.starts_with("shaders/")) path = path.substr(8);
            else if (path.find("/shaders/") != std::string::npos) path = path.substr(path.find("/shaders/") + 9);
            while (!path.empty() && path[0] == '/') path = path.substr(1);
            return path;
        };
        
        fm->set_exists_func([assetManager, sanitizePath](const std::string& p) {
            std::string path = sanitizePath(p);
            AAsset* asset = AAssetManager_open(assetManager, path.c_str(), AASSET_MODE_UNKNOWN);
            if (asset) {
                AAsset_close(asset);
                return true;
            }
            return false;
        });

        fm->set_load_func([assetManager, sanitizePath](const std::string& p) -> std::vector<uint8_t> {
            std::string path = sanitizePath(p);
            AAsset* asset = AAssetManager_open(assetManager, path.c_str(), AASSET_MODE_BUFFER);
            if (!asset) return {};
            size_t size = AAsset_getLength(asset);
            std::vector<uint8_t> data(size);
            AAsset_read(asset, data.data(), size);
            AAsset_close(asset);
            return data;
        });
    }
}

AndroidPlatform::~AndroidPlatform() {
}

result<std::unique_ptr<IWindow>> AndroidPlatform::create_window(const WindowDesc& desc) {
    if (!state_->window) {
        return jaeng::Error::fromMessage(-1, "Android window is not initialized yet by the OS.");
    }
    return jaeng::result<std::unique_ptr<IWindow>>(std::make_unique<AndroidWindow>(state_, desc.width, desc.height));
}

IInput& AndroidPlatform::get_input() {
    return *input_;
}

bool AndroidPlatform::poll_events() {
    int ident;
    int events;
    struct android_poll_source* source;

    // Pump the looper
    while ((ident = ALooper_pollOnce(isForeground_ ? 0 : -1, nullptr, &events, (void**)&source)) >= 0) {
        if (source != nullptr) {
            source->process(state_, source);
        }

        if (state_->destroyRequested != 0) {
            return false;
        }
    }
    return true;
}

void AndroidPlatform::show_message_box(const std::string& title, const std::string& content, MessageBoxType type) {
    int prio = ANDROID_LOG_INFO;
    if (type == MessageBoxType::Warning) prio = ANDROID_LOG_WARN;
    if (type == MessageBoxType::Error) prio = ANDROID_LOG_ERROR;
    __android_log_print(prio, "JAENG", "%s: %s", title.c_str(), content.c_str());
}

IProcessManager& AndroidPlatform::get_process_manager() {
    return *processManager_;
}

IFileManager& AndroidPlatform::get_file_manager() {
    return *fileManager_;
}

std::string AndroidPlatform::get_base_path() const {
    if (state_->activity && state_->activity->internalDataPath) {
        return std::string(state_->activity->internalDataPath);
    }
    return "";
}

std::string AndroidPlatform::resolve_path(const std::string& path) const {
    return get_base_path() + "/" + path;
}

bool AndroidPlatform::file_exists(const std::string& path) const {
    return fileManager_->exists(resolve_path(path));
}

int AndroidPlatform::run(std::unique_ptr<IApplication> app) {
    app_ = std::move(app);
    
    // Wait for the window to be initialized before starting the main loop logic
    while (state_->destroyRequested == 0) {
        poll_events();
        
        if (isWindowInitialized_ && !app_->should_close()) {
            if (!app_->init()) {
                show_message_box("Error", "Application initialization failed", MessageBoxType::Error);
                break;
            }
            break; // Break the wait loop, transition to normal execution
        } else if (app_->should_close()) {
            return 0;
        }
    }

    app_->start_engine_threads();

    // Engine loop
    while (state_->destroyRequested == 0 && !app_->should_close()) {
        poll_events();
        
        if (isWindowInitialized_) {
            if (!app_->process_main_thread_tasks()) {
                std::this_thread::yield();
            }
        } else {
            // Sleep when suspended
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    app_->stop_engine_threads();
    app_->shutdown();
    return 0;
}

void AndroidPlatform::onAppCmd(struct android_app* app, int32_t cmd) {
    auto* platform = static_cast<AndroidPlatform*>(app->userData);
    platform->processAppCmd(cmd);
}

void AndroidPlatform::processAppCmd(int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (state_->window != nullptr) {
                isWindowInitialized_ = true;
                if (app_) {
                    app_->set_platform_drawable(state_->window);
                    
                    if (eventCallback_) {
                        Event ev{};
                        ev.type = Event::Type::WindowResize;
                        ev.resize.width = ANativeWindow_getWidth(state_->window);
                        ev.resize.height = ANativeWindow_getHeight(state_->window);
                        eventCallback_(ev);
                    }
                }
            }
            break;
        case APP_CMD_WINDOW_RESIZED:
            if (state_->window != nullptr && app_ && eventCallback_) {
                Event ev{};
                ev.type = Event::Type::WindowResize;
                ev.resize.width = ANativeWindow_getWidth(state_->window);
                ev.resize.height = ANativeWindow_getHeight(state_->window);
                eventCallback_(ev);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            isWindowInitialized_ = false;
            break;
        case APP_CMD_GAINED_FOCUS:
            isForeground_ = true;
            break;
        case APP_CMD_LOST_FOCUS:
            isForeground_ = false;
            break;
        default:
            break;
    }
}

int32_t AndroidPlatform::onInputEvent(struct android_app* app, AInputEvent* event) {
    auto* platform = static_cast<AndroidPlatform*>(app->userData);
    return platform->processInputEvent(event);
}

int32_t AndroidPlatform::processInputEvent(AInputEvent* event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t x = AMotionEvent_getX(event, 0);
        int32_t y = AMotionEvent_getY(event, 0);
        static_cast<AndroidInput*>(input_.get())->set_mouse_pos(x, y);

        int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        JAENG_LOG_INFO("[Input] Action: {}, X: {}, Y: {}", action, x, y);
        if (eventCallback_) {
            Event ev{};
            if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
                ev.type = Event::Type::MouseDown;
            } else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP) {
                ev.type = Event::Type::MouseUp;
            } else {
                ev.type = Event::Type::MouseMove;
            }
            ev.mouse.x = x;
            ev.mouse.y = y;
            ev.mouse.button = 272; // Map touch to Left Mouse Button (BTN_LEFT)
            eventCallback_(ev);
        }
        return 1;
    }
    return 0;
}

} // namespace jaeng::platform

namespace jaeng::platform {

std::unique_ptr<IPlatform> create_platform(void* context) {
    struct android_app* state = static_cast<struct android_app*>(context);
    return std::make_unique<AndroidPlatform>(state);
}

}
