#include "vulkan_context.h"
#include "vulkan_resources.h"
#include <mutex>
#include <filesystem>

#ifdef JAENG_LINUX
#include <dlfcn.h>
#include <wayland-client.h>
#endif

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

using namespace jaeng::renderer;

namespace jaeng::renderer {
    VulkanContext* g_ctx = nullptr;

    struct VulkanLoader {
        void* handle = nullptr;
        VulkanLoader() {
#ifdef JAENG_LINUX
            handle = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_GLOBAL);
            if (!handle) handle = dlopen("libvulkan.so", RTLD_NOW | RTLD_GLOBAL);
            if (handle) {
                auto vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(handle, "vkGetInstanceProcAddr");
                VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
            }
#endif
        }
        ~VulkanLoader() {
        }
    };
    static VulkanLoader g_loader;
}

// External functions for the plugin
extern "C" {

static bool vk_init(const RendererDesc* desc) {
    g_ctx = new VulkanContext();
    g_ctx->platformWindow = desc->platform_window;
    g_ctx->platformDisplay = desc->platform_display;
    auto res = g_ctx->device.init(true);
    if (res.hasError()) return false;

    (void)g_ctx->descriptors.init(&g_ctx->device, 1024);

    g_ctx->imageAvailableSemaphore = g_ctx->device.device.createSemaphore({});
    g_ctx->renderFinishedSemaphore = g_ctx->device.device.createSemaphore({});
    g_ctx->inFlightFence = g_ctx->device.device.createFence({vk::FenceCreateFlagBits::eSignaled});

    vk::CommandPoolCreateInfo poolInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, g_ctx->device.graphicsQueueFamily);
    g_ctx->commandPool = g_ctx->device.device.createCommandPool(poolInfo);

    vk::CommandBufferAllocateInfo allocInfo(g_ctx->commandPool, vk::CommandBufferLevel::ePrimary, 1);
    g_ctx->commandBuffer = g_ctx->device.device.allocateCommandBuffers(allocInfo)[0];

    vk::CommandPoolCreateInfo oneShotPoolInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, g_ctx->device.graphicsQueueFamily);
    g_ctx->oneShotPool = g_ctx->device.device.createCommandPool(oneShotPoolInfo);
    vk::CommandBufferAllocateInfo oneShotAllocInfo(g_ctx->oneShotPool, vk::CommandBufferLevel::ePrimary, 1);
    g_ctx->oneShotCmd = g_ctx->device.device.allocateCommandBuffers(oneShotAllocInfo)[0];

    // Create a small buffer for PushConstants fallback (HLSL register b0)
    vk::BufferCreateInfo bInfo({}, 256, vk::BufferUsageFlagBits::eUniformBuffer);
    g_ctx->pushConstantsBuffer = g_ctx->device.device.createBuffer(bInfo);
    vk::MemoryRequirements memReqs = g_ctx->device.device.getBufferMemoryRequirements(g_ctx->pushConstantsBuffer);
    vk::MemoryAllocateInfo memAlloc(memReqs.size, g_ctx->device.findMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
    g_ctx->pushConstantsMemory = g_ctx->device.device.allocateMemory(memAlloc);
    g_ctx->device.device.bindBufferMemory(g_ctx->pushConstantsBuffer, g_ctx->pushConstantsMemory, 0);

    // Bind it once to Set 0 Binding 0 (corresponds to slot 0)
    g_ctx->descriptors.updateUniform(0, g_ctx->pushConstantsBuffer, 0, 256);

    // Fetch device alignment requirement
    vk::PhysicalDeviceProperties props = g_ctx->device.physicalDevice.getProperties();
    g_ctx->minUniformBufferOffsetAlignment = props.limits.minUniformBufferOffsetAlignment;

    // Create and Map the Ring Buffer
    vk::BufferCreateInfo dynBufInfo({}, VulkanContext::DYNAMIC_BUFFER_SIZE, vk::BufferUsageFlagBits::eUniformBuffer);
    g_ctx->dynamicBuffer = g_ctx->device.device.createBuffer(dynBufInfo);
    vk::MemoryRequirements dynMemReqs = g_ctx->device.device.getBufferMemoryRequirements(g_ctx->dynamicBuffer);
    vk::MemoryAllocateInfo dynAlloc(dynMemReqs.size, g_ctx->device.findMemoryType(dynMemReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
    g_ctx->dynamicMemory = g_ctx->device.device.allocateMemory(dynAlloc);
    g_ctx->device.device.bindBufferMemory(g_ctx->dynamicBuffer, g_ctx->dynamicMemory, 0);
    g_ctx->mappedDynamicMemory = g_ctx->device.device.mapMemory(g_ctx->dynamicMemory, 0, VulkanContext::DYNAMIC_BUFFER_SIZE);

    // Pre Bind Slots for Dynamic Ring Buffer
    for (uint32_t i = 1; i < 8; ++i) {
        g_ctx->descriptors.updateUniform(i, g_ctx->dynamicBuffer, 0, 4096);
    }

    return true;
}

static void vk_shutdown() {
    if (!g_ctx) return;
    try {
        g_ctx->device.device.waitIdle();

        if (g_ctx->pushConstantsBuffer) g_ctx->device.device.destroyBuffer(g_ctx->pushConstantsBuffer);
        if (g_ctx->pushConstantsMemory) g_ctx->device.device.freeMemory(g_ctx->pushConstantsMemory);
        if (g_ctx->commandPool) g_ctx->device.device.destroyCommandPool(g_ctx->commandPool);
        if (g_ctx->oneShotPool) g_ctx->device.device.destroyCommandPool(g_ctx->oneShotPool);
        if (g_ctx->imageAvailableSemaphore) g_ctx->device.device.destroySemaphore(g_ctx->imageAvailableSemaphore);
        if (g_ctx->renderFinishedSemaphore) g_ctx->device.device.destroySemaphore(g_ctx->renderFinishedSemaphore);
        if (g_ctx->inFlightFence) g_ctx->device.device.destroyFence(g_ctx->inFlightFence);

        if (g_ctx->mappedDynamicMemory) {
            g_ctx->device.device.unmapMemory(g_ctx->dynamicMemory);
        }
        if (g_ctx->dynamicBuffer) {
            g_ctx->device.device.destroyBuffer(g_ctx->dynamicBuffer);
            g_ctx->device.device.freeMemory(g_ctx->dynamicMemory);
        }

        for (auto& [h, s] : g_ctx->swapchains) {
            s.shutdown(&g_ctx->device);
        }
        g_ctx->swapchains.clear();

        for (auto& [h, p] : g_ctx->pipelines) {
            g_ctx->device.device.destroyPipeline(p.pipeline);
            g_ctx->device.device.destroyPipelineLayout(p.layout);
        }
        g_ctx->pipelines.clear();

        for (auto& [h, s] : g_ctx->shaders) {
            g_ctx->device.device.destroyShaderModule(s.module);
        }
        g_ctx->shaders.clear();

        for (auto& [h, s] : g_ctx->samplers) {
            g_ctx->device.device.destroySampler(s.sampler);
        }
        g_ctx->samplers.clear();

        for (auto& [h, t] : g_ctx->textures) {
            g_ctx->device.device.destroyImageView(t.view);
            g_ctx->device.device.destroyImage(t.image);
            g_ctx->device.device.freeMemory(t.memory);
        }
        g_ctx->textures.clear();

        for (auto& [h, b] : g_ctx->buffers) {
            g_ctx->device.device.destroyBuffer(b.buffer);
            g_ctx->device.device.freeMemory(b.memory);
        }
        g_ctx->buffers.clear();
        g_ctx->vertexLayouts.clear();

        g_ctx->descriptors.shutdown();

        g_ctx->device.shutdown();
    } catch (...) {
        JAENG_LOG_ERROR("vk_shutdown: caught exception during cleanup");
    }
    
    // NOTE: We leak g_ctx here to avoid a persistent segfault on exit.
    g_ctx = nullptr;
}

static void vk_begin_frame() {
    if (!g_ctx || g_ctx->swapchains.empty()) return;
    
    try {
        g_ctx->device.device.waitIdle();
        (void)g_ctx->device.device.waitForFences(g_ctx->inFlightFence, true, UINT64_MAX);
        g_ctx->device.device.resetFences(g_ctx->inFlightFence);

        auto it = g_ctx->swapchains.begin();
        if (it != g_ctx->swapchains.end() && it->second.swapchain) {
            it->second.acquireNextImage(&g_ctx->device, g_ctx->imageAvailableSemaphore);
        }

        g_ctx->currentDynamicOffset = 0; // Reset the ring buffer
    } catch (const std::exception& e) {
        JAENG_LOG_ERROR("vk_begin_frame exception: {}", e.what());
    }
}

static void vk_end_frame() {
}

static SwapchainHandle vk_create_swapchain(const SwapchainDesc* desc) {
    VulkanSwapchain s;
    auto res = s.init(&g_ctx->device, g_ctx->platformWindow, g_ctx->platformDisplay, desc);
    if (res.hasError()) {
        JAENG_LOG_ERROR("Swapchain initialization failed");
        return 0;
    }
    
    SwapchainHandle h = g_ctx->nextSwapchainHandle++;
    g_ctx->swapchains[h] = std::move(s);
    g_ctx->swapchainFormat = g_ctx->swapchains[h].format;
    return h;
}

static void vk_resize_swapchain(SwapchainHandle h, Extent2D size) {
    auto it = g_ctx->swapchains.find(h);
    if (it != g_ctx->swapchains.end()) {
        it->second.resize(&g_ctx->device, size);
    }
}

static void vk_destroy_swapchain(SwapchainHandle h) {
    auto it = g_ctx->swapchains.find(h);
    if (it != g_ctx->swapchains.end()) {
        it->second.shutdown(&g_ctx->device);
        g_ctx->swapchains.erase(it);
    }
}

static TextureHandle vk_get_current_backbuffer(SwapchainHandle h) {
    auto it = g_ctx->swapchains.find(h);
    if (it == g_ctx->swapchains.end()) return 0;
    return 0xFFFF0000 | it->second.currentImageIndex;
}

static TextureHandle vk_get_depth_buffer(SwapchainHandle h) {
    auto it = g_ctx->swapchains.find(h);
    if (it == g_ctx->swapchains.end()) return 0;
    return 0xFFFE0000;
}

static void vk_present(SwapchainHandle h) {
    if (!g_ctx) return;
    auto it = g_ctx->swapchains.find(h);
    if (it != g_ctx->swapchains.end() && it->second.swapchain) {
        try {
            g_ctx->oneShotCmd.reset();
            g_ctx->oneShotCmd.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
            vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            vk::ImageMemoryBarrier barrier(
                vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlags(),
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                it->second.images[it->second.currentImageIndex], range
            );
            g_ctx->oneShotCmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe, {}, nullptr, nullptr, barrier);
            g_ctx->oneShotCmd.end();
            vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, &g_ctx->oneShotCmd, 0, nullptr);
            (void)g_ctx->device.graphicsQueue.submit(1, &submitInfo, nullptr);
            g_ctx->device.device.waitIdle();

            uint32_t imageIndex = it->second.currentImageIndex;
            vk::Semaphore waitSems[] = { g_ctx->renderFinishedSemaphore };
            vk::SwapchainKHR swaps[] = { it->second.swapchain };
            uint32_t indices[] = { imageIndex };
            vk::PresentInfoKHR presentInfo(1, waitSems, 1, swaps, indices);
            vk::Result res = g_ctx->device.graphicsQueue.presentKHR(presentInfo);

#ifdef JAENG_LINUX
            if (g_ctx->platformWindow && g_ctx->platformDisplay) {
                wl_surface_commit(static_cast<wl_surface*>(g_ctx->platformWindow));
                wl_display_flush(static_cast<wl_display*>(g_ctx->platformDisplay));
            }
#endif
        } catch (const std::exception& e) {
            JAENG_LOG_ERROR("vk_present exception: {}", e.what());
        }
    }
}

static BufferHandle vk_create_buffer(const BufferDesc* desc, const void* initial_data) {
    auto res = create_vulkan_buffer(&g_ctx->device, desc, initial_data);
    if (res.hasError()) return 0;
    BufferHandle h = g_ctx->nextBufferHandle++;
    g_ctx->buffers[h] = std::move(res).logError().value();
    return h;
}

static bool vk_update_buffer(BufferHandle h, uint64_t offset, const void* data, uint64_t size) {
    auto it = g_ctx->buffers.find(h);
    if (it == g_ctx->buffers.end()) return false;
    auto& b = it->second;

    // Calculate aligned offset
    uint32_t alignedSize = (size + g_ctx->minUniformBufferOffsetAlignment - 1) & ~(g_ctx->minUniformBufferOffsetAlignment - 1);

    if (g_ctx->currentDynamicOffset + alignedSize > VulkanContext::DYNAMIC_BUFFER_SIZE) {
        JAENG_LOG_ERROR("Dynamic buffer overflow!");
        return false;
    }

    // Copy to ring buffer
    void* dest = static_cast<char*>(g_ctx->mappedDynamicMemory) + g_ctx->currentDynamicOffset;
    memcpy(dest, data, size);

    // Save the offset used for this buffer handle
    b.dynamicOffset = g_ctx->currentDynamicOffset;
    g_ctx->currentDynamicOffset += alignedSize;

    return true;
}

static void vk_destroy_buffer(BufferHandle h) {
    auto it = g_ctx->buffers.find(h);
    if (it != g_ctx->buffers.end()) {
        g_ctx->device.device.destroyBuffer(it->second.buffer);
        g_ctx->device.device.freeMemory(it->second.memory);
        g_ctx->buffers.erase(it);
    }
}

static TextureHandle vk_create_texture(const TextureDesc* desc, const void* initial_data) {
    auto res = create_vulkan_texture(&g_ctx->device, &g_ctx->descriptors, desc, initial_data);
    if (res.hasError()) return 0;
    TextureHandle h = g_ctx->nextTextureHandle++;
    g_ctx->textures[h] = std::move(res).logError().value();
    return h;
}

static void vk_destroy_texture(TextureHandle h) {
    auto it = g_ctx->textures.find(h);
    if (it != g_ctx->textures.end()) {
        g_ctx->device.device.destroyImageView(it->second.view);
        g_ctx->device.device.destroyImage(it->second.image);
        g_ctx->device.device.freeMemory(it->second.memory);
        g_ctx->textures.erase(it);
    }
}

static uint32_t vk_get_texture_index(TextureHandle h) {
    auto it = g_ctx->textures.find(h);
    if (it == g_ctx->textures.end()) return 0;
    return it->second.srvIndex;
}

static SamplerHandle vk_create_sampler(const SamplerDesc* desc) {
    vk::SamplerCreateInfo info({}, (vk::Filter)desc->filter, (vk::Filter)desc->filter, (vk::SamplerMipmapMode)desc->filter);
    vk::Sampler s = g_ctx->device.device.createSampler(info);
    uint32_t idx = g_ctx->descriptors.allocateSampler();
    g_ctx->descriptors.updateSampler(idx, s);
    SamplerHandle h = g_ctx->nextSamplerHandle++;
    g_ctx->samplers[h] = VulkanSampler{ s, idx };
    return h;
}

static uint32_t vk_get_sampler_index(SamplerHandle h) {
    auto it = g_ctx->samplers.find(h);
    if (it == g_ctx->samplers.end()) return 0;
    return it->second.samplerIndex;
}

static ShaderModuleHandle vk_create_shader_module(const ShaderModuleDesc* desc) {
    auto res = create_vulkan_shader(&g_ctx->device, desc);
    if (res.hasError()) return 0;
    ShaderModuleHandle h = g_ctx->nextShaderHandle++;
    g_ctx->shaders[h] = std::move(res).logError().value();
    return h;
}

static void vk_destroy_shader_module(ShaderModuleHandle h) {
    auto it = g_ctx->shaders.find(h);
    if (it != g_ctx->shaders.end()) {
        g_ctx->device.device.destroyShaderModule(it->second.module);
        g_ctx->shaders.erase(it);
    }
}

static VertexLayoutHandle vk_create_vertex_layout(const VertexLayoutDesc* desc) {
    VertexLayoutHandle h = g_ctx->nextVertexLayoutHandle++;
    auto& vl = g_ctx->vertexLayouts[h];
    
    vl.bindings.push_back({ 0, desc->stride, vk::VertexInputRate::eVertex });
    for (uint32_t i = 0; i < desc->attribute_count; ++i) {
        vk::Format format = vk::Format::eR32G32B32Sfloat; 
        if (desc->attributes[i].offset == 24) format = vk::Format::eR32G32Sfloat; // UV is 2 floats
        vl.attributes.push_back({ desc->attributes[i].location, 0, format, desc->attributes[i].offset });
    }
    return h;
}

static PipelineHandle vk_create_graphics_pipeline(const GraphicsPipelineDesc* desc) {
    auto res = create_vulkan_pipeline(&g_ctx->device, &g_ctx->descriptors, desc, g_ctx->shaders, g_ctx->vertexLayouts, g_ctx->swapchainFormat);
    if (res.hasError()) return 0;
    PipelineHandle h = g_ctx->nextPipelineHandle++;
    g_ctx->pipelines[h] = std::move(res).logError().value();
    return h;
}

static void vk_destroy_pipeline(PipelineHandle h) {
    auto it = g_ctx->pipelines.find(h);
    if (it != g_ctx->pipelines.end()) {
        g_ctx->device.device.destroyPipeline(it->second.pipeline);
        g_ctx->device.device.destroyPipelineLayout(it->second.layout);
        g_ctx->pipelines.erase(it);
    }
}

// Forward decls for commands
CommandListHandle vk_begin_commands();
void vk_cmd_begin_pass(CommandListHandle, LoadOp, const ColorAttachmentDesc*, uint32_t, const DepthAttachmentDesc*);
void vk_cmd_end_pass(CommandListHandle);
void vk_cmd_draw(CommandListHandle, uint32_t, uint32_t, uint32_t, uint32_t);
void vk_cmd_draw_indexed(CommandListHandle, uint32_t, uint32_t, uint32_t, int32_t, uint32_t);
void vk_cmd_set_pipeline(CommandListHandle, PipelineHandle);
void vk_cmd_set_vertex_buffer(CommandListHandle, uint32_t, BufferHandle, uint64_t);
void vk_cmd_set_index_buffer(CommandListHandle, BufferHandle, bool, uint64_t);
void vk_cmd_bind_uniform(CommandListHandle, uint32_t, BufferHandle, uint64_t);
void vk_cmd_push_constants(CommandListHandle, uint32_t, uint32_t, const void*);
void vk_cmd_barrier(CommandListHandle, BufferHandle, uint32_t, uint32_t);
void vk_end_commands(CommandListHandle);
void vk_submit(CommandListHandle*, uint32_t);

RENDERER_API bool LoadRenderer(RendererAPI* out_api) {
    *out_api = {};
    out_api->init = vk_init;
    out_api->shutdown = vk_shutdown;
    out_api->begin_frame = vk_begin_frame;
    out_api->end_frame = vk_end_frame;
    out_api->create_swapchain = vk_create_swapchain;
    out_api->resize_swapchain = vk_resize_swapchain;
    out_api->destroy_swapchain = vk_destroy_swapchain;
    out_api->get_current_backbuffer = vk_get_current_backbuffer;
    out_api->get_depth_buffer = vk_get_depth_buffer;
    out_api->present = vk_present;
    out_api->create_buffer = vk_create_buffer;
    out_api->update_buffer = vk_update_buffer;
    out_api->destroy_buffer = vk_destroy_buffer;
    out_api->create_texture = vk_create_texture;
    out_api->destroy_texture = vk_destroy_texture;
    out_api->get_texture_index = vk_get_texture_index;
    out_api->create_sampler = vk_create_sampler;
    out_api->get_sampler_index = vk_get_sampler_index;
    out_api->create_shader_module = vk_create_shader_module;
    out_api->destroy_shader_module = vk_destroy_shader_module;
    out_api->create_vertex_layout = vk_create_vertex_layout;
    out_api->create_graphics_pipeline = vk_create_graphics_pipeline;
    out_api->destroy_pipeline = vk_destroy_pipeline;
    out_api->begin_commands = vk_begin_commands;
    out_api->cmd_begin_pass = vk_cmd_begin_pass;
    out_api->cmd_end_pass = vk_cmd_end_pass;
    out_api->cmd_draw = vk_cmd_draw;
    out_api->cmd_draw_indexed = vk_cmd_draw_indexed;
    out_api->cmd_set_pipeline = vk_cmd_set_pipeline;
    out_api->cmd_set_vertex_buffer = vk_cmd_set_vertex_buffer;
    out_api->cmd_set_index_buffer = vk_cmd_set_index_buffer;
    out_api->cmd_bind_uniform = vk_cmd_bind_uniform;
    out_api->cmd_push_constants = vk_cmd_push_constants;
    out_api->cmd_barrier = vk_cmd_barrier;
    out_api->end_commands = vk_end_commands;
    out_api->submit = vk_submit;
    out_api->wait_idle = [](){ if(g_ctx) g_ctx->device.device.waitIdle(); };
    return true;
}

} // extern "C"
