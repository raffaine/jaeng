#pragma once
#include <cstdint>
#include <memory>
#include "render/public/renderer_plugin.h"

#ifdef JAENG_LINUX
#include <wayland-client.h>
static void* g_null_window_handle = nullptr;
static void null_present(SwapchainHandle) {
    if (g_null_window_handle) {
        wl_surface_commit(static_cast<wl_surface*>(g_null_window_handle));
    }
}
#endif

class Renderer {
public:
    bool initialize(GfxBackend backend, void* window_handle, uint32_t frame_count = 3) {
#ifdef JAENG_WIN32
        if (backend == GfxBackend::D3D12) {
            if (!plugin.load(L"renderer_d3d12.dll")) return false;
            gfx = plugin.api;
        }
#endif
        if (!gfx) {
            // Null/Mock backend for platform transition
            gfx = std::make_shared<RendererAPI>();
            *gfx = {}; // Clear all pointers
            
            // Minimal no-op implementation
            gfx->init = [](const RendererDesc*) { return true; };
            gfx->shutdown = []() {};
            gfx->begin_frame = []() {};
            gfx->end_frame = []() {};
            gfx->wait_idle = []() {};

            gfx->create_swapchain = [](const SwapchainDesc*) { return (SwapchainHandle)1; };
            gfx->resize_swapchain = [](SwapchainHandle, Extent2D) {};
            gfx->destroy_swapchain = [](SwapchainHandle) {};
            gfx->get_current_backbuffer = [](SwapchainHandle) { return (TextureHandle)1; };

            gfx->create_buffer = [](const BufferDesc*, const void*) { return (BufferHandle)1; };
            gfx->destroy_buffer = [](BufferHandle) {};
            gfx->update_buffer = [](BufferHandle, uint64_t, const void*, uint64_t) { return true; };

            gfx->create_texture = [](const TextureDesc*, const void*) { return (TextureHandle)1; };
            gfx->destroy_texture = [](TextureHandle) {};
            gfx->create_sampler = [](const SamplerDesc*) { return (SamplerHandle)1; };
            gfx->destroy_sampler = [](SamplerHandle) {};

            gfx->get_texture_index = [](TextureHandle) { return 0u; };
            gfx->get_sampler_index = [](SamplerHandle) { return 0u; };

            gfx->create_shader_module = [](const ShaderModuleDesc*) { return (ShaderModuleHandle)1; };
            gfx->destroy_shader_module = [](ShaderModuleHandle) {};
            gfx->create_vertex_layout = [](const VertexLayoutDesc*) { return (VertexLayoutHandle)1; };
            gfx->create_graphics_pipeline = [](const GraphicsPipelineDesc*) { return (PipelineHandle)1; };
            gfx->destroy_pipeline = [](PipelineHandle) {};

            gfx->begin_commands = []() { return (CommandListHandle)1; };
            gfx->cmd_begin_pass = [](CommandListHandle, LoadOp, const ColorAttachmentDesc*, uint32_t, const DepthAttachmentDesc*) {};
            gfx->cmd_end_pass = [](CommandListHandle) {};
            gfx->cmd_bind_uniform = [](CommandListHandle, uint32_t, BufferHandle, uint64_t) {};
            gfx->cmd_push_constants = [](CommandListHandle, uint32_t, uint32_t, const void*) {};
            gfx->cmd_set_pipeline = [](CommandListHandle, PipelineHandle) {};
            gfx->cmd_set_vertex_buffer = [](CommandListHandle, uint32_t, BufferHandle, uint64_t) {};
            gfx->cmd_set_index_buffer = [](CommandListHandle, BufferHandle, bool, uint64_t) {};
            gfx->cmd_draw = [](CommandListHandle, uint32_t, uint32_t, uint32_t, uint32_t) {};
            gfx->cmd_draw_indexed = [](CommandListHandle, uint32_t, uint32_t, uint32_t, int32_t, uint32_t) {};
            gfx->end_commands = [](CommandListHandle) {};

            gfx->submit = [](CommandListHandle*, uint32_t) {};
            
#ifdef JAENG_LINUX
            g_null_window_handle = window_handle;
            gfx->present = null_present;
#else
            gfx->present = [](SwapchainHandle) {};
#endif
        }

        RendererDesc d{ backend, window_handle, frame_count };
        return (gfx->init) ? gfx->init(&d) : false;
    }

    void shutdown() {
        if (!gfx) return;
        if (gfx->wait_idle) gfx->wait_idle();
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
