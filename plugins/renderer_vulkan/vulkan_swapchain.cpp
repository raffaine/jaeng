#include "vulkan_swapchain.h"
#include "vulkan_device.h"

#ifdef JAENG_WIN32
#include <windows.h>
#elif defined(JAENG_LINUX)
#include <wayland-client.h>
#endif

namespace jaeng::renderer {

static vk::SurfaceKHR create_platform_surface(vk::Instance instance, void* window, void* display) {
#ifdef JAENG_WIN32
    vk::Win32SurfaceCreateInfoKHR info({}, GetModuleHandle(nullptr), (HWND)window);
    return instance.createWin32SurfaceKHR(info);
#elif defined(JAENG_LINUX)
    auto availableExtensions = vk::enumerateInstanceExtensionProperties();
    bool hasWayland = false;
    for (auto& ext : availableExtensions) {
        if (strcmp(ext.extensionName, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME) == 0) {
            hasWayland = true;
            break;
        }
    }

    if (!hasWayland) {
        JAENG_LOG_ERROR("Cannot create surface: {} extension not supported by instance", VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
        return nullptr;
    }

    vk::WaylandSurfaceCreateInfoKHR info({}, static_cast<wl_display*>(display), static_cast<wl_surface*>(window));
    try {
        return instance.createWaylandSurfaceKHR(info);
    } catch (const std::exception& e) {
        JAENG_LOG_ERROR("createWaylandSurfaceKHR failed: {}", e.what());
        return nullptr;
    }
#else
    return {};
#endif
}

jaeng::result<> VulkanSwapchain::init(VulkanDevice* device, void* window, void* display, const SwapchainDesc* desc, vk::SwapchainKHR oldSwapchain) {
    try {
        if (!surface) {
            surface = create_platform_surface(device->instance, window, display);
            if (!surface) {
                JAENG_LOG_ERROR("create_platform_surface returned null");
                return jaeng::Error::fromMessage(-1, "Failed to create Vulkan surface");
            }
        }

        auto caps = device->physicalDevice.getSurfaceCapabilitiesKHR(surface);
        auto formats = device->physicalDevice.getSurfaceFormatsKHR(surface);
        
        vk::Format requestedFormat = vk::Format::eB8G8R8A8Unorm;
        if (desc->format == TextureFormat::RGBA8_UNORM) requestedFormat = vk::Format::eR8G8B8A8Unorm;
        else if (desc->format == TextureFormat::BGRA8_UNORM) requestedFormat = vk::Format::eB8G8R8A8Unorm;

        format = formats[0].format; 
        bool foundRequested = false;
        for (const auto& f : formats) {
            if (f.format == requestedFormat) {
                format = f.format;
                foundRequested = true;
                break;
            }
        }
        
        if (!foundRequested) {
            JAENG_LOG_WARN("Requested format {} not found, using fallback {}", vk::to_string(requestedFormat), vk::to_string(format));
        }
        JAENG_LOG_DEBUG("Selected format: {}", vk::to_string(format));

        if (caps.currentExtent.width != 0xFFFFFFFF) {
            extent = caps.currentExtent;
        } else {
            uint32_t width = (desc->size.width > 0) ? desc->size.width : 1280;
            uint32_t height = (desc->size.height > 0) ? desc->size.height : 720;
            extent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        JAENG_LOG_DEBUG("Swapchain extent: {}x{}", extent.width, extent.height);

        uint32_t minImageCount = std::max(caps.minImageCount, 3u);
        if (caps.maxImageCount > 0 && minImageCount > caps.maxImageCount) {
            minImageCount = caps.maxImageCount;
        }

        vk::SwapchainCreateInfoKHR swapInfo(
            {}, surface, minImageCount, format, formats[0].colorSpace, extent, 1,
            vk::ImageUsageFlagBits::eColorAttachment,
            vk::SharingMode::eExclusive, {},
            caps.currentTransform, vk::CompositeAlphaFlagBitsKHR::eOpaque,
            vk::PresentModeKHR::eFifo, VK_TRUE, oldSwapchain
        );

        swapInfo.preTransform = caps.currentTransform;
        swapInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        swapInfo.presentMode = vk::PresentModeKHR::eFifo;

        swapchain = device->device.createSwapchainKHR(swapInfo);
        images = device->device.getSwapchainImagesKHR(swapchain);
        JAENG_LOG_DEBUG("Swapchain created with {} images", images.size());

        for (auto img : images) {
            vk::ComponentMapping swizzle = { vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity };

            vk::ImageViewCreateInfo viewInfo(
                {}, img, vk::ImageViewType::e2D, format, 
                swizzle,
                { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
            );
            imageViews.push_back(device->device.createImageView(viewInfo));
        }

        // Create Depth Buffer
        depthFormat = vk::Format::eD32Sfloat; 
        vk::ImageCreateInfo depthInfo(
            {}, vk::ImageType::e2D, depthFormat, { extent.width, extent.height, 1 },
            1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment
        );
        depthImage = device->device.createImage(depthInfo);
        vk::MemoryRequirements memReqs = device->device.getImageMemoryRequirements(depthImage);
        vk::MemoryAllocateInfo allocInfo(memReqs.size, device->findMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal));
        depthMemory = device->device.allocateMemory(allocInfo);
        device->device.bindImageMemory(depthImage, depthMemory, 0);

        vk::ImageViewCreateInfo depthViewInfo(
            {}, depthImage, vk::ImageViewType::e2D, depthFormat, {},
            { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 }
        );
        depthView = device->device.createImageView(depthViewInfo);

    } catch (const std::exception& e) {
        JAENG_LOG_ERROR("Swapchain init failed: {}", e.what());
        return jaeng::Error::fromMessage(-1, e.what());
    }

    return {};
}

void VulkanSwapchain::shutdown(VulkanDevice* device) {
    if (depthView) device->device.destroyImageView(depthView);
    if (depthImage) device->device.destroyImage(depthImage);
    if (depthMemory) device->device.freeMemory(depthMemory);
    for (auto v : imageViews) device->device.destroyImageView(v);
    imageViews.clear();
    if (swapchain) device->device.destroySwapchainKHR(swapchain);
    if (surface) device->instance.destroySurfaceKHR(surface);
}

void VulkanSwapchain::resize(VulkanDevice* device, Extent2D size) {
    // Wait for GPU to finish current operations
    device->device.waitIdle();

    // Save the old swapchain handle
    vk::SwapchainKHR oldSwapchain = swapchain;

    // Destroy the image views and depth buffer (they depend on the old size)
    if (depthView) device->device.destroyImageView(depthView);
    if (depthImage) device->device.destroyImage(depthImage);
    if (depthMemory) device->device.freeMemory(depthMemory);
    for (auto v : imageViews) device->device.destroyImageView(v);
    imageViews.clear();

    // We do NOT destroy the old swapchain yet; we pass it to init()
    swapchain = nullptr;
    imageAcquired = false;

    // Re-initialize with the new size
    SwapchainDesc desc;
    desc.size = size;
    desc.format = (format == vk::Format::eR8G8B8A8Unorm) ? TextureFormat::RGBA8_UNORM : TextureFormat::BGRA8_UNORM;

    // Note: window/display are ignored if 'surface' already exists, which it does.
    auto res = init(device, nullptr, nullptr, &desc, oldSwapchain);
    if (res.hasError()) {
        JAENG_LOG_ERROR("Failed to recreate swapchain during resize.");
    }

    // Finally, destroy the old swapchain
    if (oldSwapchain) {
        device->device.destroySwapchainKHR(oldSwapchain);
    }
}

vk::Result VulkanSwapchain::acquireNextImage(VulkanDevice* device, vk::Semaphore signalSemaphore) {
    try {
        // Use a small timeout instead of UINT64_MAX to allow forward progress and avoid validation errors
        // on some implementations when the window is not visible or minimized.
        vk::ResultValue<uint32_t> res = device->device.acquireNextImageKHR(swapchain, 1000000000ull /* 1s */, signalSemaphore, nullptr);
        if (res.result == vk::Result::eSuccess || res.result == vk::Result::eSuboptimalKHR) {
            currentImageIndex = res.value;
        }
        return res.result;
    } catch (const vk::OutOfDateKHRError&) {
        return vk::Result::eErrorOutOfDateKHR;
    } catch (const std::exception& e) {
        JAENG_LOG_ERROR("acquireNextImage failed: {}", e.what());
        return vk::Result::eErrorUnknown;
    }
}

} // namespace jaeng::renderer
