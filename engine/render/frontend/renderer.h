#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <filesystem>
#include "render/public/renderer_plugin.h"
#include "common/logging.h"

#ifdef JAENG_LINUX
#include <wayland-client.h>
#include <unistd.h>
#endif

#ifdef JAENG_MACOS
#include <mach-o/dyld.h>
#endif

#ifdef JAENG_IOS
extern "C" bool LoadRenderer(RendererAPI* out_api);
#endif

namespace jaeng {

#ifdef JAENG_LINUX
static void* g_null_window_handle = nullptr;
static void null_present(SwapchainHandle) {
    if (g_null_window_handle) {
        wl_surface_commit(static_cast<wl_surface*>(g_null_window_handle));
    }
}
#endif

class Renderer {
public:
    bool initialize(GfxBackend backend, void* window_handle, void* display_handle, uint32_t frame_count = 3, void* device_handle = nullptr) {
        RendererDesc desc{};
        desc.platform_window = window_handle;
        desc.platform_display = display_handle;
        desc.platform_device = device_handle;
        desc.frame_count = frame_count;

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
        bool tryMetal = (backend == GfxBackend::Metal);
#if defined(JAENG_MACOS) || defined(JAENG_IOS)
        if (backend != GfxBackend::Vulkan) tryMetal = true; 
#endif

        // Discovery path: always check next to executable
        std::string exeDir;
        try {
#ifdef JAENG_MACOS
            char path[1024];
            uint32_t size = sizeof(path);
            if (_NSGetExecutablePath(path, &size) == 0) {
                exeDir = std::filesystem::path(path).parent_path().string();
            }
#else
            auto exePath = std::filesystem::read_symlink("/proc/self/exe");
            exeDir = exePath.parent_path().string();
#endif
        } catch (...) {}

        if (backend == GfxBackend::Vulkan) {
            tryMetal = false; 
            std::string name = "librenderer_vulkan.so";
#ifdef JAENG_MACOS
            name = "librenderer_vulkan.dylib";
#endif
            std::string fullPath = (std::filesystem::path(exeDir) / name).string();

            if (plugin.load(fullPath.c_str()) || plugin.load(name.c_str())) {
                gfx = plugin.api;
                JAENG_LOG_INFO("Loaded Vulkan renderer plugin.");
            } else {
                JAENG_LOG_WARN("Failed to load Vulkan renderer, falling back to Metal if available.");
                tryMetal = true;
            }
        }
        
        if (tryMetal) {
#ifdef JAENG_IOS
            gfx = std::make_shared<RendererAPI>();
            if (LoadRenderer(gfx.get())) {
                JAENG_LOG_INFO("Using Static Metal renderer (iOS).");
            } else {
                JAENG_LOG_ERROR("Failed to initialize Static Metal renderer (iOS).");
                gfx.reset();
            }
#elif defined(JAENG_MACOS)
            std::string name = "renderer_metal.dylib";
            std::string fullPath = (std::filesystem::path(exeDir) / name).string();

            if (plugin.load(fullPath.c_str()) || plugin.load(name.c_str()) || plugin.load("librenderer_metal.dylib")) {
                gfx = plugin.api;
                JAENG_LOG_INFO("Loaded Metal renderer plugin.");
            }
#endif
        }
#endif

        if (gfx) {
            return gfx->init(&desc);
        }

        JAENG_LOG_INFO("Initializing Null/Mock renderer backend.");
        gfx = std::make_shared<RendererAPI>();
        
        // Lifecycle
        gfx->init = [](const RendererDesc*) { return true; };
        gfx->shutdown = []() {};
        gfx->begin_frame = []() { return true; };
        gfx->end_frame = []() {};
        gfx->wait_idle = []() {};

        // Swapchain
        gfx->create_swapchain = [](const SwapchainDesc*) { return (SwapchainHandle)1; };
        gfx->resize_swapchain = [](SwapchainHandle, Extent2D) {};
        gfx->destroy_swapchain = [](SwapchainHandle) {};
        gfx->get_current_backbuffer = [](SwapchainHandle) { return (TextureHandle)1; };
        gfx->get_depth_buffer = [](SwapchainHandle) { return (TextureHandle)2; };

        // Resources
        gfx->create_buffer = [](const BufferDesc*, const void*) { return (BufferHandle)1; };
        gfx->destroy_buffer = [](BufferHandle) {};
        gfx->update_buffer = [](BufferHandle, uint64_t, const void*, uint64_t) { return true; };
        gfx->create_texture = [](const TextureDesc*, const void*) { return (TextureHandle)1; };
        gfx->destroy_texture = [](TextureHandle) {};
        gfx->create_sampler = [](const SamplerDesc*) { return (SamplerHandle)1; };
        gfx->destroy_sampler = [](SamplerHandle) {};
        gfx->get_texture_index = [](TextureHandle) { return 0u; };
        gfx->get_sampler_index = [](SamplerHandle) { return 0u; };

        // Shaders & Pipelines
        gfx->create_shader_module = [](const ShaderModuleDesc*) { return (ShaderModuleHandle)1; };
        gfx->destroy_shader_module = [](ShaderModuleHandle) {};
        gfx->create_vertex_layout = [](const VertexLayoutDesc*) { return (VertexLayoutHandle)1; };
        gfx->create_graphics_pipeline = [](const GraphicsPipelineDesc*) { return (PipelineHandle)1; };
        gfx->destroy_pipeline = [](PipelineHandle) {};

        // Commands
        gfx->begin_commands = []() { return (CommandListHandle)1; };
        gfx->cmd_begin_pass = [](CommandListHandle, LoadOp, const ColorAttachmentDesc*, uint32_t, const DepthAttachmentDesc*) {};
        gfx->cmd_end_pass = [](CommandListHandle) {};
        gfx->cmd_bind_uniform = [](CommandListHandle, uint32_t, BufferHandle, uint64_t) {};
        gfx->cmd_push_constants = [](CommandListHandle, uint32_t, uint32_t, const void*) {};
        gfx->cmd_barrier = [](CommandListHandle, BufferHandle, uint32_t, uint32_t) {};
        gfx->cmd_set_pipeline = [](CommandListHandle, PipelineHandle) {};
        gfx->cmd_set_vertex_buffer = [](CommandListHandle, uint32_t, BufferHandle, uint64_t) {};
        gfx->cmd_set_index_buffer = [](CommandListHandle, BufferHandle, bool, uint64_t) {};
        gfx->cmd_draw = [](CommandListHandle, uint32_t, uint32_t, uint32_t, uint32_t) {};
        gfx->cmd_draw_indexed = [](CommandListHandle, uint32_t, uint32_t, uint32_t, int32_t, uint32_t) {};
        gfx->end_commands = [](CommandListHandle) {};

        // Submit/Present
        gfx->submit = [](CommandListHandle*, uint32_t) {};
        gfx->present = [](SwapchainHandle) {};

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

    void queue_resize(SwapchainHandle h, uint32_t width, uint32_t height) {
        resize_handle_ = h;
        new_width_.store(width, std::memory_order_relaxed);
        new_height_.store(height, std::memory_order_relaxed);
        pending_resize_.store(true, std::memory_order_release);
    }

    void process_pending_resizes() {
        if (pending_resize_.exchange(false, std::memory_order_acquire)) {
            uint32_t w = new_width_.load(std::memory_order_relaxed);
            uint32_t h = new_height_.load(std::memory_order_relaxed);
            if (gfx && gfx->resize_swapchain && resize_handle_ > 0 && w > 0 && h > 0) {
                // Safely execute the backend resize on the current thread
                gfx->resize_swapchain(resize_handle_, { w, h });
            }
        }
    }

    std::shared_ptr<RendererAPI> gfx{};
    
private:
    RendererPlugin plugin;

    // Safely handle Resizes as they bridge threads (Main and Render)
    std::atomic<bool> pending_resize_{ false };
    std::atomic<uint32_t> new_width_{ 0 };
    std::atomic<uint32_t> new_height_{ 0 };
    SwapchainHandle resize_handle_{ 0 };
};

} // namespace jaeng
