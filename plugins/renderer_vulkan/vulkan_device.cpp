#include "vulkan_device.h"
#include <iostream>
#include <cstring>

#ifdef JAENG_LINUX
#include <wayland-client.h>
#endif

namespace jaeng::renderer {

jaeng::result<> VulkanDevice::init(bool enableValidation) {
    auto availableExtensions = vk::enumerateInstanceExtensionProperties();
    auto availableLayers = vk::enumerateInstanceLayerProperties();

    JAENG_LOG_DEBUG("Available Instance Extensions:");
    for (const auto& ext : availableExtensions) {
        JAENG_LOG_DEBUG("  {}", ext.extensionName.data());
    }

    auto has_extension = [&](const char* name) {
        for (const auto& ext : availableExtensions) {
            if (strcmp(ext.extensionName, name) == 0) return true;
        }
        return false;
    };

    auto has_layer = [&](const char* name) {
        for (const auto& layer : availableLayers) {
            if (strcmp(layer.layerName, name) == 0) return true;
        }
        return false;
    };

    vk::ApplicationInfo appInfo("jaeng", 1, "jaeng", 1, VK_API_VERSION_1_3);

    std::vector<const char*> extensions;
    if (has_extension(VK_KHR_SURFACE_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    } else {
        JAENG_LOG_ERROR("Required extension {} not found", VK_KHR_SURFACE_EXTENSION_NAME);
        return jaeng::Error::fromMessage(-1, "Missing VK_KHR_surface");
    }

#ifdef JAENG_WIN32
    if (has_extension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    }
#elif defined(JAENG_LINUX)
    if (has_extension(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
        JAENG_LOG_DEBUG("Using Wayland Surface extension.");
    }
    if (has_extension(VK_KHR_XCB_SURFACE_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
        JAENG_LOG_DEBUG("Using XCB Surface extension.");
    }
    if (!has_extension(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME) && !has_extension(VK_KHR_XCB_SURFACE_EXTENSION_NAME)) {
        JAENG_LOG_WARN("Neither Wayland nor XCB surface extensions found.");
    }
#endif

    bool portability = false;
    if (has_extension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        portability = true;
    }

    std::vector<const char*> layers;
    if (enableValidation && has_layer("VK_LAYER_KHRONOS_validation")) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        JAENG_LOG_DEBUG("Validation layer enabled.");
    }

    vk::InstanceCreateInfo instanceInfo({}, &appInfo, layers, extensions);
    if (portability) {
        instanceInfo.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
    }

    try {
        instance = vk::createInstance(instanceInfo);
    } catch (const std::exception& e) {
        JAENG_LOG_ERROR("vk::createInstance failed: {}", e.what());
        return jaeng::Error::fromMessage(-1, e.what());
    }
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);

    // Pick physical device
    auto devices = instance.enumeratePhysicalDevices();
    if (devices.empty()) {
        JAENG_LOG_ERROR("No physical devices found");
        return jaeng::Error::fromMessage(-1, "No physical devices found");
    }
    for (auto& d : devices) {
        auto props = d.getProperties();
        JAENG_LOG_DEBUG("Found device: {}", props.deviceName.data());
        if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
            physicalDevice = d;
            break;
        }
    }
    if (!physicalDevice) physicalDevice = devices[0];
    JAENG_LOG_INFO("Using GPU: {}", physicalDevice.getProperties().deviceName.data());

    // Find queues
    auto queueProps = physicalDevice.getQueueFamilyProperties();
    graphicsQueueFamily = uint32_t(-1);
    for (uint32_t i = 0; i < (uint32_t)queueProps.size(); ++i) {
        if (queueProps[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            graphicsQueueFamily = i;
            break;
        }
    }

    if (graphicsQueueFamily == uint32_t(-1)) {
        JAENG_LOG_ERROR("No graphics queue family found");
        return jaeng::Error::fromMessage(-1, "No graphics queue family found");
    }

    float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueInfo({}, graphicsQueueFamily, 1, &queuePriority);

    // Required features for Bindless
    vk::PhysicalDeviceDescriptorIndexingFeatures indexingFeatures;
    indexingFeatures.descriptorBindingPartiallyBound = true;
    indexingFeatures.runtimeDescriptorArray = true;
    indexingFeatures.shaderSampledImageArrayNonUniformIndexing = true;
    indexingFeatures.descriptorBindingSampledImageUpdateAfterBind = true;
    indexingFeatures.descriptorBindingStorageImageUpdateAfterBind = true;
    indexingFeatures.descriptorBindingStorageBufferUpdateAfterBind = true;
    indexingFeatures.descriptorBindingUniformBufferUpdateAfterBind = true;

    // Required for modern passes
    vk::PhysicalDeviceDynamicRenderingFeatures dynamicRendering(true);
    dynamicRendering.pNext = &indexingFeatures;

    vk::PhysicalDeviceFeatures2 deviceFeatures;
    deviceFeatures.pNext = &dynamicRendering;

    std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME };
    
    JAENG_LOG_DEBUG("Enabling device extensions:");
    for (const auto& ext : deviceExtensions) {
        JAENG_LOG_DEBUG("  {}", ext);
    }

    // Check for portability subset (required for some drivers if KHR_portability_enumeration is used)
    auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();
    for (auto& ext : availableDeviceExtensions) {
        if (strcmp(ext.extensionName, "VK_KHR_portability_subset") == 0) {
            deviceExtensions.push_back("VK_KHR_portability_subset");
            JAENG_LOG_DEBUG("Enabled VK_KHR_portability_subset device extension.");
            break;
        }
    }

    vk::DeviceCreateInfo deviceInfo({}, queueInfo, layers, deviceExtensions, nullptr, &deviceFeatures);
    try {
        device = physicalDevice.createDevice(deviceInfo);
    } catch (const std::exception& e) {
        JAENG_LOG_ERROR("vk::Device creation failed: {}", e.what());
        return jaeng::Error::fromMessage(-1, e.what());
    }
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device);

    graphicsQueue = device.getQueue(graphicsQueueFamily, 0);

    return {};
}

void VulkanDevice::shutdown() {
    if (device) {
        device.destroy();
        device = nullptr;
    }
    if (instance) {
        instance.destroy();
        instance = nullptr;
    }
}

uint32_t VulkanDevice::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    auto memProps = physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return uint32_t(-1);
}

} // namespace jaeng::renderer
