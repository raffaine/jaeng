#pragma once

#include "platform/public/platform_api.h"
#include <wayland-client.h>
#include <libdecor.h>
#include <memory>
#include <vector>
#include <string>
#include "wayland_input.h"
#include "wayland_process.h"
#include "storage/win/filestorage.h"

// Forward declare xdg/libdecor types
struct xdg_wm_base;
struct libdecor;
struct libdecor_frame;
struct libdecor_configuration;

namespace jaeng::platform {

class WaylandPlatform : public IPlatform {
public:
    WaylandPlatform();
    ~WaylandPlatform() override;

    jaeng::result<std::unique_ptr<IWindow>> create_window(const WindowDesc& desc) override;
    IInput& get_input() override { return input_; }
    bool poll_events() override;
    void set_event_callback(EventCallback cb) override { eventCallback_ = cb; }
    
    void show_message_box(const std::string& title, const std::string& content, MessageBoxType type) override;
    void* get_native_display_handle() const override { return display_; }
    
    IProcessManager& get_process_manager() override { return processManager_; }
    IFileManager& get_file_manager() override { return *fileManager_; }

    std::string get_base_path() const override;
    std::string resolve_path(const std::string& path) const override;
    bool file_exists(const std::string& path) const override;

    int run(std::unique_ptr<IApplication> app) override;

    static void handle_registry_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    static void handle_registry_global_remove(void* data, wl_registry* registry, uint32_t name);
    static void xdg_wm_base_ping(void* data, xdg_wm_base* xdg_wm_base, uint32_t serial);

private:
    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    xdg_wm_base* xdg_wm_base_ = nullptr;
    wl_shm* shm_ = nullptr;
    wl_seat* seat_ = nullptr;

    libdecor* decor_context_ = nullptr;

    WaylandInput input_;
    WaylandProcessManager processManager_;
    std::shared_ptr<IFileManager> fileManager_;
    EventCallback eventCallback_;

    bool running_ = true;

    static WaylandPlatform* instance_;
    friend class WaylandWindow;
    friend class WaylandInput;

    static const struct wl_registry_listener registry_listener_;
    static const struct libdecor_interface decor_interface_;
};

} // namespace jaeng::platform
