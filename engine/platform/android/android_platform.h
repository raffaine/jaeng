#pragma once
#include "platform/public/platform_api.h"
#include <android_native_app_glue.h>
#include <string>

namespace jaeng::platform {

class AndroidPlatform : public IPlatform {
public:
    AndroidPlatform(struct android_app* state);
    ~AndroidPlatform() override;

    result<std::unique_ptr<IWindow>> create_window(const WindowDesc& desc) override;
    IInput& get_input() override;
    bool poll_events() override;
    void set_event_callback(EventCallback cb) override { eventCallback_ = cb; }
    
    void show_message_box(const std::string& title, const std::string& content, MessageBoxType type) override;
    void* get_native_display_handle() const override { return nullptr; }

    IProcessManager& get_process_manager() override;
    IFileManager& get_file_manager() override;

    std::string get_base_path() const override;
    std::string resolve_path(const std::string& path) const override;
    bool file_exists(const std::string& path) const override;
    bool is_foreground() const override { return isForeground_; }

    int run(std::unique_ptr<IApplication> app) override;

    struct android_app* get_android_state() const { return state_; }

private:
    static void onAppCmd(struct android_app* app, int32_t cmd);
    static int32_t onInputEvent(struct android_app* app, AInputEvent* event);

    void processAppCmd(int32_t cmd);
    int32_t processInputEvent(AInputEvent* event);

    struct android_app* state_ = nullptr;
    EventCallback eventCallback_;
    std::unique_ptr<IInput> input_;
    std::unique_ptr<IProcessManager> processManager_;
    std::shared_ptr<IFileManager> fileManager_;
    std::unique_ptr<IApplication> app_;

    bool isForeground_ = false;
    bool isWindowInitialized_ = false;
};

} // namespace jaeng::platform
