#include "vulkan_resources.h"
#include "vulkan_device.h"
#include "vulkan_descriptors.h"
#include <map>
#include <vector>

namespace jaeng::renderer {

jaeng::result<VulkanShaderModule> create_vulkan_shader(VulkanDevice* device, const ShaderModuleDesc* desc) {
    vk::ShaderModuleCreateInfo info({}, desc->size, static_cast<const uint32_t*>(desc->data));
    return VulkanShaderModule{ device->device.createShaderModule(info) };
}

jaeng::result<VulkanPipeline> create_vulkan_pipeline(VulkanDevice* device, VulkanDescriptorHeap* heap, const GraphicsPipelineDesc* desc, const std::map<ShaderModuleHandle, VulkanShaderModule>& shaders, const std::map<VertexLayoutHandle, VulkanVertexLayout>& vertexLayouts, vk::Format colorFormat) {
    auto vs = shaders.at(desc->vs).module;
    auto fs = shaders.at(desc->fs).module;

    std::vector<vk::PipelineShaderStageCreateInfo> stages = {
        { {}, vk::ShaderStageFlagBits::eVertex, vs, "main" },
        { {}, vk::ShaderStageFlagBits::eFragment, fs, "main" }
    };

    // Use all 3 layouts from the heap
    std::vector<vk::DescriptorSetLayout> layouts = {
        heap->getLayout(0),
        heap->getLayout(1),
        heap->getLayout(2)
    };

    vk::PipelineLayoutCreateInfo layoutInfo({}, layouts);
    vk::PipelineLayout layout = device->device.createPipelineLayout(layoutInfo);

    // Dynamic Rendering
    vk::PipelineRenderingCreateInfo renderingInfo({}, 1, &colorFormat);
    
    // We should ideally map desc->depth_stencil to a vk::Format
    vk::Format depthFormat = vk::Format::eD32Sfloat; 
    renderingInfo.depthAttachmentFormat = depthFormat;

    // Vertex Input from stored layout
    auto& vl = vertexLayouts.at(desc->vertex_layout);
    vk::PipelineVertexInputStateCreateInfo vertexInput({}, vl.bindings, vl.attributes);

    // Minimal pipeline state
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList);
    vk::PipelineRasterizationStateCreateInfo rasterizer({}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, false, 0, 0, 0, 1.0f);
    vk::PipelineMultisampleStateCreateInfo multisampling({}, vk::SampleCountFlagBits::e1);
    vk::PipelineColorBlendAttachmentState colorBlendAttachment(false, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    if (desc->enable_blend) {
        colorBlendAttachment.blendEnable = true;
        colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
        colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
    }
    vk::PipelineColorBlendStateCreateInfo colorBlending({}, false, vk::LogicOp::eCopy, 1, &colorBlendAttachment);
    
    vk::PipelineDepthStencilStateCreateInfo depthStencil({}, true, true, vk::CompareOp::eLessOrEqual, false, false);

    vk::PipelineViewportStateCreateInfo viewportState({}, 1, nullptr, 1, nullptr);
    std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamicStates);

    vk::GraphicsPipelineCreateInfo pipelineInfo({}, stages, &vertexInput, &inputAssembly, nullptr, &viewportState, &rasterizer, &multisampling, &depthStencil, &colorBlending, &dynamicState, layout);
    pipelineInfo.pNext = &renderingInfo;

    auto pipelineResult = device->device.createGraphicsPipeline(nullptr, pipelineInfo);
    auto pipeline = pipelineResult.value;

    return VulkanPipeline{ pipeline, layout };
}

} // namespace jaeng::renderer
