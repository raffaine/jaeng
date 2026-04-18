#include "metal_renderer.h"
#include "metal_utils.h"
#include "common/logging.h"
#include <vector>
#include <map>

namespace jaeng::renderer::metal {

struct MetalContext {
    UniqueHandle<MTL::Device> device;
    UniqueHandle<MTL::CommandQueue> commandQueue;
    CA::MetalLayer* layer = nullptr;
    
    // Simple resource storage for the skeleton
    std::vector<UniqueHandle<MTL::Buffer>> buffers;
    std::vector<UniqueHandle<MTL::Texture>> textures;
};

static MetalContext* g_ctx = nullptr;

bool MetalRenderer::init(const RendererDesc* desc) {
    if (g_ctx) return true;
    
    g_ctx = new MetalContext();
    
    // Retrieve system default device
    g_ctx->device.reset(MTL::CreateSystemDefaultDevice());
    if (!g_ctx->device) {
        JAENG_LOG_ERROR("Metal: Failed to create system default device.");
        return false;
    }
    
    JAENG_LOG_INFO("Metal: Initialized with device: {}", g_ctx->device->name()->utf8String());
    
    g_ctx->commandQueue.reset(g_ctx->device->newCommandQueue());
    if (!g_ctx->commandQueue) {
        JAENG_LOG_ERROR("Metal: Failed to create command queue.");
        return false;
    }
    
    // Cast platform window (CAMetalLayer pointer from Objective-C++ layer)
    g_ctx->layer = static_cast<CA::MetalLayer*>(desc->platform_window);
    if (g_ctx->layer) {
        g_ctx->layer->setDevice(g_ctx->device.get());
        g_ctx->layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    }
    
    return true;
}

void MetalRenderer::shutdown() {
    if (g_ctx) {
        delete g_ctx;
        g_ctx = nullptr;
    }
}

bool MetalRenderer::begin_frame() { return true; }
void MetalRenderer::end_frame() {}

SwapchainHandle MetalRenderer::create_swapchain(const SwapchainDesc* desc) { return 1; }
void MetalRenderer::resize_swapchain(SwapchainHandle handle, Extent2D size) {}
void MetalRenderer::destroy_swapchain(SwapchainHandle handle) {}
TextureHandle MetalRenderer::get_current_backbuffer(SwapchainHandle handle) { return 1; }
TextureHandle MetalRenderer::get_depth_buffer(SwapchainHandle handle) { return 2; }

BufferHandle MetalRenderer::create_buffer(const BufferDesc* desc, const void* initial_data) {
    MTL::ResourceOptions options = MTL::ResourceStorageModeShared;
    MTL::Buffer* buffer = g_ctx->device->newBuffer(desc->size_bytes, options);
    
    if (initial_data && buffer) {
        std::memcpy(buffer->contents(), initial_data, desc->size_bytes);
    }
    
    g_ctx->buffers.emplace_back(buffer);
    return static_cast<BufferHandle>(g_ctx->buffers.size());
}

void MetalRenderer::destroy_buffer(BufferHandle handle) {}
bool MetalRenderer::update_buffer(BufferHandle handle, uint64_t dst_offset, const void* data, uint64_t size) {
    if (handle > 0 && handle <= g_ctx->buffers.size()) {
        auto& buffer = g_ctx->buffers[handle - 1];
        std::memcpy(static_cast<uint8_t*>(buffer->contents()) + dst_offset, data, size);
        return true;
    }
    return false;
}

TextureHandle MetalRenderer::create_texture(const TextureDesc* desc, const void* initial_data) { return 1; }
void MetalRenderer::destroy_texture(TextureHandle handle) {}
SamplerHandle MetalRenderer::create_sampler(const SamplerDesc* desc) { return 1; }
void MetalRenderer::destroy_sampler(SamplerHandle handle) {}

uint32_t MetalRenderer::get_texture_index(TextureHandle handle) { return 0; }
uint32_t MetalRenderer::get_sampler_index(SamplerHandle handle) { return 0; }

ShaderModuleHandle MetalRenderer::create_shader_module(const ShaderModuleDesc* desc) { return 1; }
void MetalRenderer::destroy_shader_module(ShaderModuleHandle handle) {}
VertexLayoutHandle MetalRenderer::create_vertex_layout(const VertexLayoutDesc* desc) { return 1; }
PipelineHandle MetalRenderer::create_graphics_pipeline(const GraphicsPipelineDesc* desc) { return 1; }
void MetalRenderer::destroy_pipeline(PipelineHandle handle) {}

CommandListHandle MetalRenderer::begin_commands() { return 1; }
void MetalRenderer::cmd_begin_pass(CommandListHandle handle, LoadOp load_op, const ColorAttachmentDesc* colors, uint32_t count, const DepthAttachmentDesc* depth) {}
void MetalRenderer::cmd_end_pass(CommandListHandle handle) {}
void MetalRenderer::cmd_bind_uniform(CommandListHandle handle, uint32_t slot, BufferHandle buffer, uint64_t offset) {}
void MetalRenderer::cmd_push_constants(CommandListHandle handle, uint32_t offset, uint32_t count, const void* data) {}
void MetalRenderer::cmd_barrier(CommandListHandle handle, BufferHandle buffer, uint32_t src_access, uint32_t dst_access) {}
void MetalRenderer::cmd_set_pipeline(CommandListHandle handle, PipelineHandle pipeline) {}
void MetalRenderer::cmd_set_vertex_buffer(CommandListHandle handle, uint32_t slot, BufferHandle buffer, uint64_t offset) {}
void MetalRenderer::cmd_set_index_buffer(CommandListHandle handle, BufferHandle buffer, bool index32, uint64_t offset) {}
void MetalRenderer::cmd_draw(CommandListHandle handle, uint32_t vtx_count, uint32_t instance_count, uint32_t first_vtx, uint32_t first_instance) {}
void MetalRenderer::cmd_draw_indexed(CommandListHandle handle, uint32_t idx_count, uint32_t inst_count, uint32_t first_idx, int32_t vtx_offset, uint32_t first_instance) {}
void MetalRenderer::end_commands(CommandListHandle handle) {}

void MetalRenderer::submit(CommandListHandle* lists, uint32_t list_count) {}
void MetalRenderer::present(SwapchainHandle handle) {
    if (!g_ctx->layer) return;
    
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    
    MTL::CommandBuffer* cmdBuf = g_ctx->commandQueue->commandBuffer();
    CA::MetalDrawable* drawable = g_ctx->layer->nextDrawable();
    
    if (drawable) {
        cmdBuf->presentDrawable(drawable);
        cmdBuf->commit();
    }
    
    pool->release();
}

void MetalRenderer::wait_idle() {}

} // namespace jaeng::renderer::metal

extern "C" {
RENDERER_API bool LoadRenderer(RendererAPI* out_api) {
    using namespace jaeng::renderer::metal;
    out_api->init = MetalRenderer::init;
    out_api->shutdown = MetalRenderer::shutdown;
    out_api->begin_frame = MetalRenderer::begin_frame;
    out_api->end_frame = MetalRenderer::end_frame;
    out_api->create_swapchain = MetalRenderer::create_swapchain;
    out_api->resize_swapchain = MetalRenderer::resize_swapchain;
    out_api->destroy_swapchain = MetalRenderer::destroy_swapchain;
    out_api->get_current_backbuffer = MetalRenderer::get_current_backbuffer;
    out_api->get_depth_buffer = MetalRenderer::get_depth_buffer;
    out_api->create_buffer = MetalRenderer::create_buffer;
    out_api->destroy_buffer = MetalRenderer::destroy_buffer;
    out_api->update_buffer = MetalRenderer::update_buffer;
    out_api->create_texture = MetalRenderer::create_texture;
    out_api->destroy_texture = MetalRenderer::destroy_texture;
    out_api->create_sampler = MetalRenderer::create_sampler;
    out_api->destroy_sampler = MetalRenderer::destroy_sampler;
    out_api->get_texture_index = MetalRenderer::get_texture_index;
    out_api->get_sampler_index = MetalRenderer::get_sampler_index;
    out_api->create_shader_module = MetalRenderer::create_shader_module;
    out_api->destroy_shader_module = MetalRenderer::destroy_shader_module;
    out_api->create_vertex_layout = MetalRenderer::create_vertex_layout;
    out_api->create_graphics_pipeline = MetalRenderer::create_graphics_pipeline;
    out_api->destroy_pipeline = MetalRenderer::destroy_pipeline;
    out_api->begin_commands = MetalRenderer::begin_commands;
    out_api->cmd_begin_pass = MetalRenderer::cmd_begin_pass;
    out_api->cmd_end_pass = MetalRenderer::cmd_end_pass;
    out_api->cmd_bind_uniform = MetalRenderer::cmd_bind_uniform;
    out_api->cmd_push_constants = MetalRenderer::cmd_push_constants;
    out_api->cmd_barrier = MetalRenderer::cmd_barrier;
    out_api->cmd_set_pipeline = MetalRenderer::cmd_set_pipeline;
    out_api->cmd_set_vertex_buffer = MetalRenderer::cmd_set_vertex_buffer;
    out_api->cmd_set_index_buffer = MetalRenderer::cmd_set_index_buffer;
    out_api->cmd_draw = MetalRenderer::cmd_draw;
    out_api->cmd_draw_indexed = MetalRenderer::cmd_draw_indexed;
    out_api->end_commands = MetalRenderer::end_commands;
    out_api->submit = MetalRenderer::submit;
    out_api->present = MetalRenderer::present;
    out_api->wait_idle = MetalRenderer::wait_idle;
    return true;
}
}
