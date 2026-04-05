#pragma once

#include "vulkan_device.h"
#include "vulkan_descriptors.h"
#include "vulkan_swapchain.h"
#include "vulkan_resources.h"
#include <map>

namespace jaeng::renderer {

struct VulkanContext {
    VulkanDevice device;
    VulkanDescriptorHeap descriptors;
    
    void* platformWindow = nullptr;
    void* platformDisplay = nullptr;
    void* libvulkan = nullptr;
    
    std::map<SwapchainHandle, VulkanSwapchain> swapchains;
    uint32_t nextSwapchainHandle = 1;

    std::map<BufferHandle, VulkanBuffer> buffers;
    uint32_t nextBufferHandle = 1;

    std::map<TextureHandle, VulkanTexture> textures;
    uint32_t nextTextureHandle = 1;

    std::map<SamplerHandle, VulkanSampler> samplers;
    uint32_t nextSamplerHandle = 1;

    std::map<ShaderModuleHandle, VulkanShaderModule> shaders;
    uint32_t nextShaderHandle = 1;

    std::map<VertexLayoutHandle, VulkanVertexLayout> vertexLayouts;
    uint32_t nextVertexLayoutHandle = 1;

    std::map<PipelineHandle, VulkanPipeline> pipelines;
    uint32_t nextPipelineHandle = 1;

    vk::Semaphore imageAvailableSemaphore;
    vk::Semaphore renderFinishedSemaphore;
    vk::Fence inFlightFence;

    vk::CommandPool commandPool;
    vk::CommandBuffer commandBuffer;
    vk::PipelineLayout currentPipelineLayout;
    vk::Format swapchainFormat = vk::Format::eUndefined;

    vk::Buffer pushConstantsBuffer;
    vk::DeviceMemory pushConstantsMemory;

    vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eColorAttachmentOutput;

    vk::CommandPool oneShotPool;
    vk::CommandBuffer oneShotCmd;

    vk::Buffer dynamicBuffer;
    vk::DeviceMemory dynamicMemory;
    void* mappedDynamicMemory = nullptr;
    uint32_t currentDynamicOffset = 0;
    static constexpr uint32_t DYNAMIC_BUFFER_SIZE = 1024 * 1024; // 1MB
    uint32_t minUniformBufferOffsetAlignment = 256; // We will fetch this from device properties

    // Track offsets for the 8 uniform bindings in Set 0
    uint32_t dynamicOffsets[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
};

extern VulkanContext* g_ctx;

} // namespace jaeng::renderer
