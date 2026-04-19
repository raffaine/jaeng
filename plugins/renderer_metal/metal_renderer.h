#pragma once

#include "render/public/renderer_api.h"

namespace jaeng::renderer::metal {

class MetalRenderer {
public:
    static bool init(const RendererDesc* desc);
    static void shutdown();

    static bool begin_frame();
    static void end_frame();

    static SwapchainHandle create_swapchain(const SwapchainDesc* desc);
    static void resize_swapchain(SwapchainHandle handle, Extent2D size);
    static void destroy_swapchain(SwapchainHandle handle);
    static TextureHandle get_current_backbuffer(SwapchainHandle handle);
    static TextureHandle get_depth_buffer(SwapchainHandle handle);

    static BufferHandle create_buffer(const BufferDesc* desc, const void* initial_data);
    static void destroy_buffer(BufferHandle handle);
    static bool update_buffer(BufferHandle handle, uint64_t dst_offset, const void* data, uint64_t size);

    static TextureHandle create_texture(const TextureDesc* desc, const void* initial_data);
    static void destroy_texture(TextureHandle handle);
    static SamplerHandle create_sampler(const SamplerDesc* desc);
    static void destroy_sampler(SamplerHandle handle);

    static uint32_t get_texture_index(TextureHandle handle);
    static uint32_t get_sampler_index(SamplerHandle handle);

    static ShaderModuleHandle create_shader_module(const ShaderModuleDesc* desc);
    static void destroy_shader_module(ShaderModuleHandle handle);
    static VertexLayoutHandle create_vertex_layout(const VertexLayoutDesc* desc);
    static PipelineHandle create_graphics_pipeline(const GraphicsPipelineDesc* desc);
    static void destroy_pipeline(PipelineHandle handle);

    static CommandListHandle begin_commands();
    static void cmd_begin_pass(CommandListHandle handle, LoadOp load_op, const ColorAttachmentDesc* colors, uint32_t count, const DepthAttachmentDesc* depth);
    static void cmd_end_pass(CommandListHandle handle);
    static void cmd_bind_uniform(CommandListHandle handle, uint32_t slot, BufferHandle buffer, uint64_t offset);
    static void cmd_push_constants(CommandListHandle handle, uint32_t offset, uint32_t count, const void* data);
    static void cmd_barrier(CommandListHandle handle, BufferHandle buffer, uint32_t src_access, uint32_t dst_access);
    static void cmd_set_pipeline(CommandListHandle handle, PipelineHandle pipeline);
    static void cmd_set_vertex_buffer(CommandListHandle handle, uint32_t slot, BufferHandle buffer, uint64_t offset);
    static void cmd_set_index_buffer(CommandListHandle handle, BufferHandle buffer, bool index32, uint64_t offset);
    static void cmd_draw(CommandListHandle handle, uint32_t vtx_count, uint32_t instance_count, uint32_t first_vtx, uint32_t first_instance);
    static void cmd_draw_indexed(CommandListHandle handle, uint32_t idx_count, uint32_t inst_count, uint32_t first_idx, int32_t vtx_offset, uint32_t first_instance);
    static void end_commands(CommandListHandle handle);

    static void submit(CommandListHandle* lists, uint32_t list_count);
    static void present(SwapchainHandle handle);
    static void wait_idle();
};

} // namespace jaeng::renderer::metal

extern "C" RENDERER_API bool LoadRenderer(RendererAPI* out_api);
