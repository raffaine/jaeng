#include "metal_renderer.h"
#include "metal_utils.h"
#include "common/logging.h"
#include <vector>
#include <map>
#include <cstring>
#include <mutex>
#include <glm/glm.hpp>

namespace jaeng::renderer::metal {

constexpr uint32_t MAX_BUFFERS = 4096;
constexpr uint32_t MAX_TEXTURES = 31;
constexpr uint32_t MAX_SAMPLERS = 16;

struct MetalSwapchain {
    Extent2D size;
    UniqueHandle<MTL::Texture> depthTexture;
};

struct MetalContext {
    UniqueHandle<MTL::Device> device;
    UniqueHandle<MTL::CommandQueue> commandQueue;
    CA::MetalLayer* layer = nullptr;
    
    // Resource pools
    std::recursive_mutex resourceMutex;
    std::vector<UniqueHandle<MTL::Buffer>> buffers;
    std::vector<UniqueHandle<MTL::Texture>> textures;
    std::vector<UniqueHandle<MTL::SamplerState>> samplers;
    std::vector<UniqueHandle<MTL::Library>> libraries;
    std::vector<UniqueHandle<MTL::RenderPipelineState>> pipelines;
    std::vector<UniqueHandle<MTL::DepthStencilState>> depthStates;
    std::vector<UniqueHandle<MTL::VertexDescriptor>> vertexLayouts;
    std::vector<bool> pipelineBlending;

    // Dynamic pool for per-draw constant updates
    UniqueHandle<MTL::Buffer> dynamicBuffer;
    uint32_t dynamicBufferOffset = 0;
    static constexpr uint32_t DYNAMIC_BUFFER_SIZE = 1024 * 1024; // 1MB

    // Swapchains
    std::map<SwapchainHandle, MetalSwapchain> swapchains;
    uint32_t swapchainCounter = 0;

    // Per-frame state (Strictly Render Thread)
    // We rely on thread-local autorelease pools for these
    MTL::CommandBuffer* currentCmdBuffer = nullptr;
    MTL::RenderCommandEncoder* currentEncoder = nullptr;
    CA::MetalDrawable* currentDrawable = nullptr;

    // Command Tracking
    MTL::Buffer* currentIndexBuffer = nullptr;
    uint64_t currentIndexOffset = 0;
    MTL::IndexType currentIndexType = MTL::IndexTypeUInt16;

    std::vector<uint32_t> lastDynamicOffsets;
    std::vector<uint32_t> bufferUsages;
    bool hasBoundResourcesInPass = false;
};

static MetalContext* g_ctx = nullptr;

bool MetalRenderer::init(const RendererDesc* desc) {
    if (g_ctx) return true;
    
    JAENG_LOG_INFO("[Metal] init START");
    g_ctx = new MetalContext();
    
    if (desc->platform_device) {
        JAENG_LOG_INFO("[Metal] Using platform-provided device: {}", desc->platform_device);
        auto* dev = static_cast<MTL::Device*>(desc->platform_device);
        dev->retain(); 
        g_ctx->device.reset(dev);
    } else {
        JAENG_LOG_INFO("[Metal] Calling MTL::CreateSystemDefaultDevice()...");
        g_ctx->device.reset(MTL::CreateSystemDefaultDevice());
    }
    
    if (!g_ctx->device) {
        JAENG_LOG_ERROR("Metal: Failed to acquire device.");
        return false;
    }
    
    JAENG_LOG_INFO("[Metal] Initialized with device: {}", g_ctx->device->name()->utf8String());
    
    g_ctx->commandQueue.reset(g_ctx->device->newCommandQueue());
    if (!g_ctx->commandQueue) {
        JAENG_LOG_ERROR("Metal: Failed to create command queue.");
        return false;
    }
    
    g_ctx->layer = static_cast<CA::MetalLayer*>(desc->platform_window);
    if (g_ctx->layer) {
        JAENG_LOG_INFO("[Metal] Attached to platform layer: {}", (void*)g_ctx->layer);
#ifdef JAENG_MACOS
        g_ctx->layer->setDevice(g_ctx->device.get());
        g_ctx->layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
#endif
    }

    g_ctx->buffers.resize(MAX_BUFFERS);
    g_ctx->textures.resize(MAX_TEXTURES);
    g_ctx->samplers.resize(MAX_SAMPLERS);
    g_ctx->lastDynamicOffsets.assign(MAX_BUFFERS + 1, 0xFFFFFFFF);
    g_ctx->bufferUsages.assign(MAX_BUFFERS + 1, 0);

    g_ctx->dynamicBuffer.reset(g_ctx->device->newBuffer(MetalContext::DYNAMIC_BUFFER_SIZE, MTL::ResourceStorageModeShared));
    
    JAENG_LOG_INFO("[Metal] init END");
    return true;
}

void MetalRenderer::shutdown() {
    if (g_ctx) {
        delete g_ctx;
        g_ctx = nullptr;
    }
}

void MetalRenderer::set_platform_drawable(void* drawable) {
    if (g_ctx) {
        g_ctx->currentDrawable = static_cast<CA::MetalDrawable*>(drawable);
    }
}

bool MetalRenderer::begin_frame() {
    if (!g_ctx->layer) return false;
    
    // Acquire new drawable for the frame. It's autoreleased by the system.
    g_ctx->currentDrawable = g_ctx->layer->nextDrawable();
    
    if (!g_ctx->currentDrawable || !g_ctx->currentDrawable->texture()) {
        g_ctx->currentDrawable = nullptr;
        return false;
    }

    g_ctx->dynamicBufferOffset = 0;
    g_ctx->lastDynamicOffsets.assign(MAX_BUFFERS + 1, 0xFFFFFFFF);

    return true;
}

void MetalRenderer::end_frame() {
    g_ctx->currentDrawable = nullptr;
    g_ctx->currentCmdBuffer = nullptr;
    g_ctx->currentEncoder = nullptr;
}

SwapchainHandle MetalRenderer::create_swapchain(const SwapchainDesc* desc) {
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    auto handle = ++g_ctx->swapchainCounter;
    auto& sw = g_ctx->swapchains[handle];
    sw.size = desc->size;

#ifdef JAENG_MACOS
    if (g_ctx->layer) {
        g_ctx->layer->setDrawableSize({(double)desc->size.width, (double)desc->size.height});
    }
#endif

    MTL::TextureDescriptor* depthDesc = MTL::TextureDescriptor::texture2DDescriptor(
        MTL::PixelFormatDepth32Float, desc->size.width, desc->size.height, false);
    depthDesc->setUsage(MTL::TextureUsageRenderTarget);
    depthDesc->setStorageMode(MTL::StorageModePrivate);
    sw.depthTexture.reset(g_ctx->device->newTexture(depthDesc));

    return handle;
}

void MetalRenderer::resize_swapchain(SwapchainHandle handle, Extent2D size) {
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    if (g_ctx->swapchains.count(handle)) {
        auto& sw = g_ctx->swapchains[handle];
        sw.size = size;

#ifdef JAENG_MACOS
        if (g_ctx->layer) {
            g_ctx->layer->setDrawableSize({(double)size.width, (double)size.height});
        }
#endif

        MTL::TextureDescriptor* depthDesc = MTL::TextureDescriptor::texture2DDescriptor(
            MTL::PixelFormatDepth32Float, size.width, size.height, false);
        depthDesc->setUsage(MTL::TextureUsageRenderTarget);
        depthDesc->setStorageMode(MTL::StorageModePrivate);
        sw.depthTexture.reset(g_ctx->device->newTexture(depthDesc));
    }
}

void MetalRenderer::destroy_swapchain(SwapchainHandle handle) {
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    g_ctx->swapchains.erase(handle);
}

TextureHandle MetalRenderer::get_current_backbuffer(SwapchainHandle handle) { return 1; }
TextureHandle MetalRenderer::get_depth_buffer(SwapchainHandle handle) { return 2; }

BufferHandle MetalRenderer::create_buffer(const BufferDesc* desc, const void* initial_data) {
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    MTL::Buffer* buffer = g_ctx->device->newBuffer(desc->size_bytes, MTL::ResourceStorageModeShared);
    if (initial_data && buffer) {
        std::memcpy(buffer->contents(), initial_data, desc->size_bytes);
    }
    
    for (uint32_t i = 0; i < MAX_BUFFERS; ++i) {
        if (!g_ctx->buffers[i]) {
            g_ctx->buffers[i].reset(buffer);
            g_ctx->bufferUsages[i + 1] = desc->usage;
            return i + 1;
        }
    }
    if (buffer) buffer->release();
    return 0;
}

void MetalRenderer::destroy_buffer(BufferHandle handle) {
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    if (handle > 0 && handle <= MAX_BUFFERS) {
        g_ctx->buffers[handle - 1].reset();
    }
}

bool MetalRenderer::update_buffer(BufferHandle handle, uint64_t dst_offset, const void* data, uint64_t size) {
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    if (handle > 0 && handle <= MAX_BUFFERS && g_ctx->buffers[handle - 1]) {
        auto& buffer = g_ctx->buffers[handle - 1];
        std::memcpy(static_cast<uint8_t*>(buffer->contents()) + dst_offset, data, size);

        if (g_ctx->bufferUsages[handle] & BufferUsage_Uniform) {
            uint32_t alignedSize = (uint32_t)((size + 255) & ~255);
            if (g_ctx->dynamicBufferOffset + alignedSize <= MetalContext::DYNAMIC_BUFFER_SIZE) {
                uint8_t* ptr = static_cast<uint8_t*>(g_ctx->dynamicBuffer->contents());
                std::memcpy(ptr + g_ctx->dynamicBufferOffset, data, size);
                g_ctx->lastDynamicOffsets[handle] = g_ctx->dynamicBufferOffset;
                g_ctx->dynamicBufferOffset += alignedSize;
            }
        }
        return true;
    }
    return false;
}

TextureHandle MetalRenderer::create_texture(const TextureDesc* desc, const void* initial_data) {
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    MTL::TextureDescriptor* mtlDesc = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, desc->width, desc->height, false);
    mtlDesc->setStorageMode(MTL::StorageModeShared);
    mtlDesc->setUsage(MTL::TextureUsageShaderRead);

    MTL::Texture* texture = g_ctx->device->newTexture(mtlDesc);

    if (initial_data && texture) {
        MTL::Region region = MTL::Region::Make2D(0, 0, desc->width, desc->height);
        texture->replaceRegion(region, 0, initial_data, desc->width * 4);
    }

    for (uint32_t i = 0; i < MAX_TEXTURES; ++i) {
        if (!g_ctx->textures[i]) {
            g_ctx->textures[i].reset(texture);
            return i + 1;
        }
    }
    if (texture) texture->release();
    return 0;
}

void MetalRenderer::destroy_texture(TextureHandle handle) {
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    if (handle > 0 && handle <= MAX_TEXTURES) {
        g_ctx->textures[handle - 1].reset();
    }
}

SamplerHandle MetalRenderer::create_sampler(const SamplerDesc* desc) {
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    MTL::SamplerDescriptor* mtlDesc = MTL::SamplerDescriptor::alloc()->init();
    mtlDesc->setMinFilter(desc->filter == SamplerFilter::Linear ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);
    mtlDesc->setMagFilter(desc->filter == SamplerFilter::Linear ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);
    
    MTL::SamplerState* sampler = g_ctx->device->newSamplerState(mtlDesc);
    mtlDesc->release();

    for (uint32_t i = 0; i < MAX_SAMPLERS; ++i) {
        if (!g_ctx->samplers[i]) {
            g_ctx->samplers[i].reset(sampler);
            return i + 1;
        }
    }
    if (sampler) sampler->release();
    return 0;
}

void MetalRenderer::destroy_sampler(SamplerHandle handle) {
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    if (handle > 0 && handle <= MAX_SAMPLERS) {
        g_ctx->samplers[handle - 1].reset();
    }
}

uint32_t MetalRenderer::get_texture_index(TextureHandle handle) { return handle - 1; }
uint32_t MetalRenderer::get_sampler_index(SamplerHandle handle) { return handle - 1; }

ShaderModuleHandle MetalRenderer::create_shader_module(const ShaderModuleDesc* desc) {
    if (!desc || !desc->data || desc->size == 0) return 0;
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    
    NS::Error* error = nullptr;
    NS::String* source = NS::String::alloc()->init(const_cast<void*>(desc->data), desc->size, NS::UTF8StringEncoding, false);
    MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
    MTL::Library* library = g_ctx->device->newLibrary(source, options, &error);
    
    options->release();
    source->release();
    
    if (!library) {
        if (error) JAENG_LOG_ERROR("Metal: Shader compilation failed: {}", error->localizedDescription()->utf8String());
        return 0;
    }
    
    g_ctx->libraries.emplace_back(library);
    return static_cast<ShaderModuleHandle>(g_ctx->libraries.size());
}

void MetalRenderer::destroy_shader_module(ShaderModuleHandle handle) {}

VertexLayoutHandle MetalRenderer::create_vertex_layout(const VertexLayoutDesc* desc) {
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    MTL::VertexDescriptor* mtlDesc = MTL::VertexDescriptor::alloc()->init();
    for (uint32_t i = 0; i < desc->attribute_count; ++i) {
        const auto& attr = desc->attributes[i];
        auto* mtlAttr = mtlDesc->attributes()->object(attr.location);
        mtlAttr->setOffset(attr.offset);
        mtlAttr->setBufferIndex(30); 
        mtlAttr->setFormat(attr.location == 2 ? MTL::VertexFormatFloat2 : MTL::VertexFormatFloat3);
    }
    mtlDesc->layouts()->object(30)->setStride(desc->stride);
    g_ctx->vertexLayouts.emplace_back(mtlDesc);
    return static_cast<VertexLayoutHandle>(g_ctx->vertexLayouts.size());
}

PipelineHandle MetalRenderer::create_graphics_pipeline(const GraphicsPipelineDesc* desc) {
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    MTL::RenderPipelineDescriptor* mtlDesc = MTL::RenderPipelineDescriptor::alloc()->init();
    auto* vsLib = g_ctx->libraries[desc->vs - 1].get();
    auto* fsLib = g_ctx->libraries[desc->fs - 1].get();
    
    auto* vsFunc = vsLib->newFunction(NS::String::string("main0", NS::UTF8StringEncoding));
    auto* fsFunc = fsLib->newFunction(NS::String::string("main0", NS::UTF8StringEncoding));
    mtlDesc->setVertexFunction(vsFunc);
    mtlDesc->setFragmentFunction(fsFunc);
    vsFunc->release();
    fsFunc->release();
    
    mtlDesc->setVertexDescriptor(g_ctx->vertexLayouts[desc->vertex_layout - 1].get());
    auto* colorAtt = mtlDesc->colorAttachments()->object(0);
    colorAtt->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    
    if (desc->enable_blend) {
        colorAtt->setBlendingEnabled(true);
        colorAtt->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
        colorAtt->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    }
    mtlDesc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
    
    NS::Error* error = nullptr;
    MTL::RenderPipelineState* pso = g_ctx->device->newRenderPipelineState(mtlDesc, &error);
    mtlDesc->release();
    if (!pso) return 0;
    
    MTL::DepthStencilDescriptor* dsDesc = MTL::DepthStencilDescriptor::alloc()->init();
    dsDesc->setDepthWriteEnabled(!desc->enable_blend && desc->depth_stencil.enableDepth);
    dsDesc->setDepthCompareFunction(desc->depth_stencil.enableDepth ? MTL::CompareFunctionLessEqual : MTL::CompareFunctionAlways);
    MTL::DepthStencilState* dsState = g_ctx->device->newDepthStencilState(dsDesc);
    dsDesc->release();

    g_ctx->pipelines.emplace_back(pso);
    g_ctx->depthStates.emplace_back(dsState);
    g_ctx->pipelineBlending.push_back(desc->enable_blend);
    return static_cast<PipelineHandle>(g_ctx->pipelines.size());
}

void MetalRenderer::destroy_pipeline(PipelineHandle handle) {}

CommandListHandle MetalRenderer::begin_commands() {
    // commandBuffer() returns an autoreleased object. We do NOT retain it.
    g_ctx->currentCmdBuffer = g_ctx->commandQueue->commandBuffer();
    return 1;
}

void MetalRenderer::cmd_begin_pass(CommandListHandle handle, LoadOp load_op, const ColorAttachmentDesc* colors, uint32_t count, const DepthAttachmentDesc* depth) {
    if (!g_ctx->currentCmdBuffer) return;

    MTL::RenderPassDescriptor* passDesc = MTL::RenderPassDescriptor::renderPassDescriptor();
    if (count > 0 && g_ctx->currentDrawable) { 
        auto* colorAtt = passDesc->colorAttachments()->object(0);
        colorAtt->setTexture(g_ctx->currentDrawable->texture());
        colorAtt->setLoadAction(load_op == LoadOp::Clear ? MTL::LoadActionClear : MTL::LoadActionLoad);
        colorAtt->setStoreAction(MTL::StoreActionStore);
        if (load_op == LoadOp::Clear) {
            colorAtt->setClearColor(MTL::ClearColor::Make(colors[0].clear_rgba[0], colors[0].clear_rgba[1], colors[0].clear_rgba[2], colors[0].clear_rgba[3]));
        }
    }
    
    if (depth && depth->tex > 0 && !g_ctx->swapchains.empty()) {
        auto* depthAtt = passDesc->depthAttachment();
        depthAtt->setTexture(g_ctx->swapchains.begin()->second.depthTexture.get());
        depthAtt->setLoadAction(load_op == LoadOp::Clear ? MTL::LoadActionClear : MTL::LoadActionLoad);
        depthAtt->setStoreAction(MTL::StoreActionStore);
        if (load_op == LoadOp::Clear) depthAtt->setClearDepth(depth->clear_d);
    }
    
    g_ctx->currentEncoder = g_ctx->currentCmdBuffer->renderCommandEncoder(passDesc);
    if (!g_ctx->currentEncoder) return;

    g_ctx->hasBoundResourcesInPass = false;
    g_ctx->currentEncoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
    g_ctx->currentEncoder->setCullMode(MTL::CullModeBack);

    if (g_ctx->currentDrawable && g_ctx->currentDrawable->texture()) {
        MTL::Viewport vp = {0.0, 0.0, (double)g_ctx->currentDrawable->texture()->width(), (double)g_ctx->currentDrawable->texture()->height(), 0.0, 1.0};
        g_ctx->currentEncoder->setViewport(vp);
        MTL::ScissorRect scr = {0, 0, (NS::UInteger)g_ctx->currentDrawable->texture()->width(), (NS::UInteger)g_ctx->currentDrawable->texture()->height()};
        g_ctx->currentEncoder->setScissorRect(scr);
    }
}

void MetalRenderer::cmd_end_pass(CommandListHandle handle) {
    if (g_ctx->currentEncoder) {
        g_ctx->currentEncoder->endEncoding();
        g_ctx->currentEncoder = nullptr;
    }
}

void MetalRenderer::cmd_bind_uniform(CommandListHandle handle, uint32_t slot, BufferHandle buffer, uint64_t offset) {
    if (!g_ctx->currentEncoder) return;
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    if (buffer > 0 && buffer <= MAX_BUFFERS) {
        uint32_t dynOffset = g_ctx->lastDynamicOffsets[buffer];
        uint32_t metalSlot = slot + 1;
        if (dynOffset != 0xFFFFFFFF) {
            g_ctx->currentEncoder->setVertexBuffer(g_ctx->dynamicBuffer.get(), dynOffset + offset, metalSlot); 
            g_ctx->currentEncoder->setFragmentBuffer(g_ctx->dynamicBuffer.get(), dynOffset + offset, metalSlot);
        } else if (g_ctx->buffers[buffer - 1]) {
            auto* mtlBuf = g_ctx->buffers[buffer - 1].get();
            g_ctx->currentEncoder->setVertexBuffer(mtlBuf, offset, metalSlot); 
            g_ctx->currentEncoder->setFragmentBuffer(mtlBuf, offset, metalSlot);
        }
    }
}

void MetalRenderer::cmd_push_constants(CommandListHandle handle, uint32_t offset, uint32_t count, const void* data) {
    if (!g_ctx->currentEncoder) return;
    g_ctx->currentEncoder->setVertexBytes(data, count * 4, 0);
    g_ctx->currentEncoder->setFragmentBytes(data, count * 4, 0);

    if (!g_ctx->hasBoundResourcesInPass) {
        std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
        // Metal validation requires all slots in an array to be bound if the shader declares an array,
        // even if not all are used dynamically. Bind a valid fallback sampler and texture to empty slots.
        MTL::Texture* fallbackTexture = nullptr;
        for (uint32_t i = 0; i < MAX_TEXTURES; ++i) {
            if (g_ctx->textures[i]) {
                fallbackTexture = g_ctx->textures[i].get();
                break;
            }
        }

        for (uint32_t i = 0; i < MAX_TEXTURES; ++i) {
            if (g_ctx->textures[i]) {
                g_ctx->currentEncoder->setFragmentTexture(g_ctx->textures[i].get(), i);
            } else if (fallbackTexture) {
                g_ctx->currentEncoder->setFragmentTexture(fallbackTexture, i);
            }
        }

        MTL::SamplerState* fallbackSampler = nullptr;
        for (uint32_t i = 0; i < MAX_SAMPLERS; ++i) {
            if (g_ctx->samplers[i]) {
                fallbackSampler = g_ctx->samplers[i].get();
                break;
            }
        }

        for (uint32_t i = 0; i < MAX_SAMPLERS; ++i) {
            if (g_ctx->samplers[i]) {
                g_ctx->currentEncoder->setFragmentSamplerState(g_ctx->samplers[i].get(), i);
            } else if (fallbackSampler) {
                g_ctx->currentEncoder->setFragmentSamplerState(fallbackSampler, i);
            }
        }
        g_ctx->hasBoundResourcesInPass = true;
    }
}

void MetalRenderer::cmd_barrier(CommandListHandle handle, BufferHandle buffer, uint32_t src_access, uint32_t dst_access) {}

void MetalRenderer::cmd_set_pipeline(CommandListHandle handle, PipelineHandle pipeline) {
    if (!g_ctx->currentEncoder) return;
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    if (pipeline > 0 && pipeline <= g_ctx->pipelines.size()) {
        g_ctx->currentEncoder->setRenderPipelineState(g_ctx->pipelines[pipeline - 1].get());
        g_ctx->currentEncoder->setDepthStencilState(g_ctx->depthStates[pipeline - 1].get());
        g_ctx->currentEncoder->setCullMode(g_ctx->pipelineBlending[pipeline - 1] ? MTL::CullModeNone : MTL::CullModeBack);
    }
}

void MetalRenderer::cmd_set_vertex_buffer(CommandListHandle handle, uint32_t slot, BufferHandle buffer, uint64_t offset) {
    if (!g_ctx->currentEncoder) return;
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    if (buffer > 0 && buffer <= MAX_BUFFERS && g_ctx->buffers[buffer - 1]) {
        g_ctx->currentEncoder->setVertexBuffer(g_ctx->buffers[buffer - 1].get(), offset, 30 + slot);
    }
}

void MetalRenderer::cmd_set_index_buffer(CommandListHandle handle, BufferHandle buffer, bool index32, uint64_t offset) {
    std::lock_guard<std::recursive_mutex> lock(g_ctx->resourceMutex);
    if (buffer > 0 && buffer <= MAX_BUFFERS && g_ctx->buffers[buffer - 1]) {
        g_ctx->currentIndexBuffer = g_ctx->buffers[buffer - 1].get();
        g_ctx->currentIndexOffset = offset;
        g_ctx->currentIndexType = index32 ? MTL::IndexTypeUInt32 : MTL::IndexTypeUInt16;
    }
}

void MetalRenderer::cmd_draw(CommandListHandle handle, uint32_t vtx_count, uint32_t instance_count, uint32_t first_vtx, uint32_t first_instance) {
    if (!g_ctx->currentEncoder) return;
    g_ctx->currentEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, first_vtx, vtx_count, instance_count, first_instance);
}

void MetalRenderer::cmd_draw_indexed(CommandListHandle handle, uint32_t idx_count, uint32_t inst_count, uint32_t first_idx, int32_t vtx_offset, uint32_t first_instance) {
    if (!g_ctx->currentEncoder) return;
    if (g_ctx->currentIndexBuffer) {
        uint32_t idxSize = (g_ctx->currentIndexType == MTL::IndexTypeUInt32) ? 4 : 2;
        g_ctx->currentEncoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, idx_count, g_ctx->currentIndexType, 
            g_ctx->currentIndexBuffer, g_ctx->currentIndexOffset + (first_idx * idxSize), inst_count, vtx_offset, first_instance);
    }
}

void MetalRenderer::end_commands(CommandListHandle handle) {}
void MetalRenderer::submit(CommandListHandle* lists, uint32_t list_count) {}

void MetalRenderer::present(SwapchainHandle handle) {
    if (g_ctx->currentCmdBuffer) {
        if (g_ctx->currentDrawable) {
            g_ctx->currentCmdBuffer->presentDrawable(g_ctx->currentDrawable);
        }
        g_ctx->currentCmdBuffer->commit();
        
        // Command buffer and Drawable are autoreleased by the frame pool.
        g_ctx->currentCmdBuffer = nullptr;
        g_ctx->currentDrawable = nullptr;
    }
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
    out_api->set_platform_drawable = MetalRenderer::set_platform_drawable;
    out_api->submit = MetalRenderer::submit;
    out_api->present = MetalRenderer::present;
    out_api->wait_idle = MetalRenderer::wait_idle;
    return true;
}
}
