#pragma once

#include "platform/public/platform_api.h"
#include "macos_window.h"
#include "platform/apple/apple_process.h"
#include <map>

namespace jaeng::platform {

class MacOSInput : public IInput {
public:
    bool is_key_down(KeyCode code) const override {
        auto it = keys_.find(code);
        return it != keys_.end() && it->second;
    }
    MousePos get_mouse_pos() const override { return mousePos_; }

    void set_key_state(KeyCode code, bool down) { keys_[code] = down; }
    void set_mouse_pos(int32_t x, int32_t y) { mousePos_ = {x, y}; }

private:
    std::map<KeyCode, bool> keys_;
    MousePos mousePos_{0, 0};
};

class MacOSPlatform : public IPlatform {
public:
    MacOSPlatform();
    ~MacOSPlatform() override;

    result<std::unique_ptr<IWindow>> create_window(const WindowDesc& desc) override;
    IInput& get_input() override { return input_; }
    bool poll_events() override;
    void set_event_callback(EventCallback cb) override { eventCallback_ = cb; }
    
    void show_message_box(const std::string& title, const std::string& content, MessageBoxType type) override;
    void* get_native_display_handle() const override { return nullptr; }
    
    IProcessManager& get_process_manager() override { return processManager_; }
    IFileManager& get_file_manager() override { return *fileManager_; }

    int run(std::unique_ptr<IApplication> app) override;

private:
    MacOSInput input_;
    AppleProcessManager processManager_;
    std::shared_ptr<IFileManager> fileManager_;
    EventCallback eventCallback_;
    
    static MacOSPlatform* instance_;
    friend class MacOSWindow;
};

} // namespace jaeng::platform
