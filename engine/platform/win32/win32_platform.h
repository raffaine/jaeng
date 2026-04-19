#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <map>
#include "platform/public/platform_api.h"
#include "win32_process.h"

namespace jaeng::platform {

class Win32Window : public IWindow {
public:
    Win32Window(HWND hwnd, uint32_t w, uint32_t h) : hwnd_(hwnd), width_(w), height_(h) {}
    ~Win32Window() { destroy(); }

    void destroy() override {
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

    void* get_native_handle() const override { return (void*)hwnd_; }
    uint32_t get_width() const override { return width_; }
    uint32_t get_height() const override { return height_; }
    bool is_open() const override { return hwnd_ != nullptr; }

private:
    HWND hwnd_;
    uint32_t width_, height_;
};

class Win32Input : public IInput {
public:
    bool is_key_down(KeyCode code) const override;
    MousePos get_mouse_pos() const override;

    void set_key_state(KeyCode code, bool down) { keys_[code] = down; }
    void set_mouse_pos(int32_t x, int32_t y) { mousePos_ = {x, y}; }

private:
    std::map<KeyCode, bool> keys_;
    MousePos mousePos_{0, 0};
};

class Win32Platform : public IPlatform {
public:
    Win32Platform();
    ~Win32Platform();

    jaeng::result<std::unique_ptr<IWindow>> create_window(const WindowDesc& desc) override;
    IInput& get_input() override { return input_; }
    bool poll_events() override;
    void set_event_callback(EventCallback cb) override { eventCallback_ = cb; }
    
    void show_message_box(const std::string& title, const std::string& content, MessageBoxType type) override;
    void* get_native_display_handle() const override { return nullptr; } // Not needed for Win32
    
    IProcessManager& get_process_manager() override { return processManager_; }
    IFileManager& get_file_manager() override { return *fileManager_; }
    std::string get_base_path() const override;
    int run(std::unique_ptr<IApplication> app) override;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    Win32Input input_;
    Win32ProcessManager processManager_;
    EventCallback eventCallback_;
    static Win32Platform* instance_;
    std::shared_ptr<IFileManager> fileManager_;
};

} // namespace jaeng::platform
