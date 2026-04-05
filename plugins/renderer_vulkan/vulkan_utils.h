#pragma once

#ifdef JAENG_WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(JAENG_LINUX)
#define VK_USE_PLATFORM_WAYLAND_KHR
#define VK_USE_PLATFORM_XCB_KHR
#endif

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>
#include "common/result.h"
#include "common/logging.h"

#define JAENG_CHECK_VK(res) \
    if (res != vk::Result::eSuccess) { \
        JAENG_LOG_ERROR("Vulkan Error: {}", vk::to_string(res)); \
        return jaeng::Error::fromMessage((int)res, vk::to_string(res)); \
    }

namespace jaeng::renderer {

// Handle management helpers
template<typename T>
uint32_t to_handle(T val) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<typename T::NativeType>(val))); }

} // namespace jaeng::renderer
