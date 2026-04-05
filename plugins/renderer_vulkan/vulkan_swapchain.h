#pragma once

#include "vulkan_utils.h"
#include "render/public/renderer_api.h"
#include <vector>

namespace jaeng::renderer {

struct VulkanDevice;

struct VulkanSwapchain {
    vk::SurfaceKHR surface;
    vk::SwapchainKHR swapchain;
    vk::Format format;
    vk::Extent2D extent;
    
    std::vector<vk::Image> images;
    std::vector<vk::ImageView> imageViews;
    uint32_t currentImageIndex = 0;

    vk::Image depthImage;
    vk::DeviceMemory depthMemory;
    vk::ImageView depthView;
    vk::Format depthFormat;

    jaeng::result<> init(VulkanDevice* device, void* window, void* display, const SwapchainDesc* desc);
    void shutdown(VulkanDevice* device);
    
    void resize(VulkanDevice* device, Extent2D size);
    void acquireNextImage(VulkanDevice* device, vk::Semaphore signalSemaphore);
};

} // namespace jaeng::renderer
