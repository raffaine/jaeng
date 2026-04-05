#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <filesystem>
#include "render/public/renderer_plugin.h"
#include "common/logging.h"

#ifdef JAENG_LINUX
#include <wayland-client.h>
#include <unistd.h>
static void* g_null_window_handle = nullptr;
static void null_present(SwapchainHandle) {
    if (g_null_window_handle) {
        wl_surface_commit(static_cast<wl_surface*>(g_null_window_handle));
    }
}
#endif

class Renderer {
public:
    bool initialize(GfxBackend backend, void* window_handle, void* display_handle, uint32_t frame_count = 3) {
#ifdef JAENG_WIN32
        if (backend == GfxBackend::D3D12) {
            if (plugin.load(L"renderer_d3d12.dll")) {
                gfx = plugin.api;
                JAENG_LOG_INFO("Loaded D3D12 renderer plugin.");
            }
        } else if (backend == GfxBackend::Vulkan) {
            if (plugin.load(L"renderer_vulkan.dll")) {
                gfx = plugin.api;
                JAENG_LOG_INFO("Loaded Vulkan renderer plugin.");
            }
        }
#else
        if (backend == GfxBackend::Vulkan) {
            std::string pluginPath = "librenderer_vulkan.so";
            try {
                auto exePath = std::filesystem::read_symlink("/proc/self/exe");
                pluginPath = (exePath.parent_path() / "librenderer_vulkan.so").string();
            } catch (...) {}

            if (plugin.load(pluginPath.c_str()) || 
                plugin.load("librenderer_vulkan.so") || 
                plugin.load("./librenderer_vulkan.so")) {
                gfx = plugin.api;
                JAENG_LOG_INFO("Loaded Vulkan renderer plugin.");
            } else {
                JAENG_LOG_WARN("Failed to load librenderer_vulkan.so");
            }
        }
#endif

        if (gfx) {
            RendererDesc desc{};
            desc.platform_window = window_handle;
            desc.platform_display = display_handle;
            desc.frame_count = frame_count;
            return gfx->init(&desc);
        }

        JAENG_LOG_INFO("Initializing Null/Mock renderer backend.");
        gfx = std::make_shared<RendererAPI>();
        gfx->init = [](const RendererDesc*) { return true; };
#ifdef JAENG_LINUX
        g_null_window_handle = window_handle;
        gfx->present = null_present;
#endif
        return true;
    }

    void shutdown() {
        if (!gfx) return;
        if (gfx->shutdown) gfx->shutdown();
        plugin.unload();
    }

    RendererAPI* operator->() const {
        return gfx.get();
    }

    std::shared_ptr<RendererAPI> gfx{};
    
private:
    RendererPlugin plugin;
};
