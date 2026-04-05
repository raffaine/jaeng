#include "vulkan_descriptors.h"
#include "vulkan_device.h"

namespace jaeng::renderer {

jaeng::result<> VulkanDescriptorHeap::init(VulkanDevice* device, uint32_t maxDescriptors) {
    device_ = device;
    maxDescriptors_ = maxDescriptors;

    // Set 0: Uniforms (b registers)
    std::vector<vk::DescriptorSetLayoutBinding> uBindings;
    for (uint32_t i = 0; i < 8; ++i) {
        uBindings.push_back({ i, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eAllGraphics });
    }
    
    // Enable UpdateAfterBind for uniforms too to allow mid-frame updates
    vk::DescriptorBindingFlags uFlags = vk::DescriptorBindingFlagBits::eUpdateAfterBind;
    std::vector<vk::DescriptorBindingFlags> uFlagsList(8, uFlags);
    vk::DescriptorSetLayoutBindingFlagsCreateInfo uFlagsInfo(uFlagsList);
    
    vk::DescriptorSetLayoutCreateInfo uLayoutInfo(vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool, uBindings);
    uLayoutInfo.pNext = &uFlagsInfo;
    layouts.push_back(device_->device.createDescriptorSetLayout(uLayoutInfo));

    // Set 1: Sampled Images (t registers) - Bindless Array
    std::vector<vk::DescriptorSetLayoutBinding> tBindings = {
        { 0, vk::DescriptorType::eSampledImage, maxDescriptors, vk::ShaderStageFlagBits::eAllGraphics }
    };
    vk::DescriptorBindingFlags tFlags = vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind;
    vk::DescriptorSetLayoutBindingFlagsCreateInfo tFlagsInfo(tFlags);
    vk::DescriptorSetLayoutCreateInfo tLayoutInfo(vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool, tBindings);
    tLayoutInfo.pNext = &tFlagsInfo;
    layouts.push_back(device_->device.createDescriptorSetLayout(tLayoutInfo));

    // Set 2: Samplers (s registers) - Bindless Array
    std::vector<vk::DescriptorSetLayoutBinding> sBindings = {
        { 0, vk::DescriptorType::eSampler, maxDescriptors, vk::ShaderStageFlagBits::eAllGraphics }
    };
    vk::DescriptorBindingFlags sFlags = vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind;
    vk::DescriptorSetLayoutBindingFlagsCreateInfo sFlagsInfo(sFlags);
    vk::DescriptorSetLayoutCreateInfo sLayoutInfo(vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool, sBindings);
    sLayoutInfo.pNext = &sFlagsInfo;
    layouts.push_back(device_->device.createDescriptorSetLayout(sLayoutInfo));

    // Create Pool
    std::vector<vk::DescriptorPoolSize> poolSizes = {
        { vk::DescriptorType::eUniformBuffer, 8 },
        { vk::DescriptorType::eSampledImage, maxDescriptors },
        { vk::DescriptorType::eSampler, maxDescriptors }
    };
    vk::DescriptorPoolCreateInfo poolInfo(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind, 3, poolSizes);
    pool = device_->device.createDescriptorPool(poolInfo);

    // Allocate Sets
    sets = device_->device.allocateDescriptorSets({ pool, layouts });

    return {};
}

void VulkanDescriptorHeap::shutdown() {
    if (device_) {
        for (auto l : layouts) device_->device.destroyDescriptorSetLayout(l);
        device_->device.destroyDescriptorPool(pool);
    }
    layouts.clear();
    sets.clear();
}

uint32_t VulkanDescriptorHeap::allocateSrv() {
    return srvCount++;
}

uint32_t VulkanDescriptorHeap::allocateSampler() {
    return samplerCount++;
}

void VulkanDescriptorHeap::updateSrv(uint32_t index, vk::ImageView view, vk::ImageLayout layout) {
    vk::DescriptorImageInfo imageInfo(nullptr, view, layout);
    vk::WriteDescriptorSet write(sets[1], 0, index, 1, vk::DescriptorType::eSampledImage, &imageInfo);
    device_->device.updateDescriptorSets(write, nullptr);
}

void VulkanDescriptorHeap::updateSampler(uint32_t index, vk::Sampler sampler) {
    vk::DescriptorImageInfo samplerInfo(sampler, nullptr, vk::ImageLayout::eUndefined);
    vk::WriteDescriptorSet write(sets[2], 0, index, 1, vk::DescriptorType::eSampler, &samplerInfo);
    device_->device.updateDescriptorSets(write, nullptr);
}

void VulkanDescriptorHeap::updateUniform(uint32_t binding, vk::Buffer buffer, uint64_t offset, uint64_t range) {
    vk::DescriptorBufferInfo bufferInfo(buffer, offset, range);
    vk::WriteDescriptorSet write(sets[0], binding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufferInfo);
    device_->device.updateDescriptorSets(write, nullptr);
}

} // namespace jaeng::renderer
