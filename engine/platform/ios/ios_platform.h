#pragma once

#include "platform/public/platform_api.h"
#include "ios_window.h"
#include "platform/apple/apple_process.h"
#include <map>

namespace jaeng::platform {

class IOSInput : public IInput {
public:
    bool is_key_down(KeyCode code) const override { return false; } // Handled via touch usually
    MousePos get_mouse_pos() const override { return mousePos_; }

    void set_mouse_pos(int32_t x, int32_t y) { mousePos_ = {x, y}; }

private:
    MousePos mousePos_{0, 0};
};

class IOSPlatform : public IPlatform {
public:
    IOSPlatform();
    ~IOSPlatform() override;

    result<std::unique_ptr<IWindow>> create_window(const WindowDesc& desc) override;
    IInput& get_input() override { return input_; }
    bool poll_events() override;
    void set_event_callback(EventCallback cb) override { eventCallback_ = cb; }
    
    void show_message_box(const std::string& title, const std::string& content, MessageBoxType type) override;
    void* get_native_display_handle() const override { return nullptr; }
    
    IProcessManager& get_process_manager() override { return processManager_; }
    IFileManager& get_file_manager() override { return *fileManager_; }

    std::string get_base_path() const override;
    std::string resolve_path(const std::string& path) const override;
    bool file_exists(const std::string& path) const override;

    int run(std::unique_ptr<IApplication> app) override;

private:
    IOSInput input_;
    AppleProcessManager processManager_;
    std::shared_ptr<IFileManager> fileManager_;
    EventCallback eventCallback_;
    
    static IOSPlatform* instance_;
    friend class IOSWindow;
};

} // namespace jaeng::platform
