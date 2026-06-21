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
    PresentMode lastPresentMode = PresentMode::Fifo;
    TextureFormat lastFormat = TextureFormat::BGRA8_UNORM;
    
    std::vector<vk::Image> images;
    std::vector<vk::ImageView> imageViews;
    uint32_t currentImageIndex = 0;

    vk::Image depthImage;
    vk::DeviceMemory depthMemory;
    vk::ImageView depthView;
    vk::Format depthFormat;
    bool imageAcquired = false;

    jaeng::result<> init(VulkanDevice* device, void* window, void* display, const SwapchainDesc* desc, vk::SwapchainKHR oldSwapchain = nullptr);
    void shutdown(VulkanDevice* device);
    
    void resize(VulkanDevice* device, Extent2D size, void* window = nullptr, void* display = nullptr);
    void set_present_mode(VulkanDevice* device, PresentMode mode);
    vk::Result acquireNextImage(VulkanDevice* device, vk::Semaphore signalSemaphore);
};

} // namespace jaeng::renderer
