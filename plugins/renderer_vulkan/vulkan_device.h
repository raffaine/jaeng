#pragma once

#include "vulkan_utils.h"
#include <vector>

namespace jaeng::renderer {

struct VulkanDevice {
    vk::Instance instance;
    vk::PhysicalDevice physicalDevice;
    vk::Device device;
    
    vk::Queue graphicsQueue;
    uint32_t graphicsQueueFamily;

    jaeng::result<> init(bool enableValidation);
    void shutdown();

    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
};

} // namespace jaeng::renderer
