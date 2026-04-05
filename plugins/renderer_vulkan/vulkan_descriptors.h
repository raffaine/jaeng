#pragma once

#include "vulkan_utils.h"
#include <vector>

namespace jaeng::renderer {

struct VulkanDevice;

class VulkanDescriptorHeap {
public:
    jaeng::result<> init(VulkanDevice* device, uint32_t maxDescriptors);
    void shutdown();

    vk::DescriptorSetLayout getLayout(uint32_t set) const { return layouts[set]; }
    vk::DescriptorSet getSet(uint32_t set) const { return sets[set]; }

    uint32_t allocateSrv();
    uint32_t allocateSampler();

    void updateSrv(uint32_t index, vk::ImageView view, vk::ImageLayout layout);
    void updateSampler(uint32_t index, vk::Sampler sampler);
    void updateUniform(uint32_t binding, vk::Buffer buffer, uint64_t offset, uint64_t range);

private:
    VulkanDevice* device_ = nullptr;
    std::vector<vk::DescriptorSetLayout> layouts;
    vk::DescriptorPool pool;
    std::vector<vk::DescriptorSet> sets;

    uint32_t srvCount = 0;
    uint32_t samplerCount = 0;
    uint32_t maxDescriptors_ = 0;
};

} // namespace jaeng::renderer
