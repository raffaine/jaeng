#pragma once

#include "vulkan_utils.h"
#include "render/public/renderer_api.h"
#include <map>

namespace jaeng::renderer {

struct VulkanDevice;
class VulkanDescriptorHeap;

struct VulkanBuffer {
    vk::Buffer buffer;
    vk::DeviceMemory memory;
    uint64_t size;
    bool needsBarrier = false;
};

struct VulkanTexture {
    vk::Image image;
    vk::DeviceMemory memory;
    vk::ImageView view;
    uint32_t srvIndex;
};

struct VulkanSampler {
    vk::Sampler sampler;
    uint32_t samplerIndex;
};

struct VulkanShaderModule {
    vk::ShaderModule module;
};

struct VulkanVertexLayout {
    std::vector<vk::VertexInputBindingDescription> bindings;
    std::vector<vk::VertexInputAttributeDescription> attributes;
};

struct VulkanPipeline {
    vk::Pipeline pipeline;
    vk::PipelineLayout layout;
};

jaeng::result<VulkanShaderModule> create_vulkan_shader(VulkanDevice* device, const ShaderModuleDesc* desc);
jaeng::result<VulkanPipeline> create_vulkan_pipeline(VulkanDevice* device, VulkanDescriptorHeap* heap, const GraphicsPipelineDesc* desc, const std::map<ShaderModuleHandle, VulkanShaderModule>& shaders, const std::map<VertexLayoutHandle, VulkanVertexLayout>& vertexLayouts, vk::Format colorFormat);
jaeng::result<VulkanBuffer> create_vulkan_buffer(VulkanDevice* device, const BufferDesc* desc, const void* initial_data);
jaeng::result<VulkanTexture> create_vulkan_texture(VulkanDevice* device, VulkanDescriptorHeap* heap, const TextureDesc* desc, const void* initial_data);

} // namespace jaeng::renderer
