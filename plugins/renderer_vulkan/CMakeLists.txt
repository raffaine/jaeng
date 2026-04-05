add_library(renderer_vulkan SHARED
  vulkan_renderer.cpp
  vulkan_device.cpp
  vulkan_swapchain.cpp
  vulkan_descriptors.cpp
  vulkan_resources.cpp
  vulkan_pipeline.cpp
  vulkan_commands.cpp
)

target_compile_definitions(renderer_vulkan PRIVATE RENDERER_BUILD ${JAENG_PLATFORM_DEF})

find_package(Vulkan REQUIRED)
target_include_directories(renderer_vulkan PRIVATE ${Vulkan_INCLUDE_DIRS})
# target_link_libraries(renderer_vulkan PRIVATE Vulkan::Vulkan) # Use dynamic dispatch instead

target_include_directories(renderer_vulkan PRIVATE ${ENGINE_ROOT}/render/public)
target_include_directories(renderer_vulkan PRIVATE ${ENGINE_ROOT})

# Vulkan-HPP uses C++20/23 features
target_compile_features(renderer_vulkan PRIVATE cxx_std_23)

if(WIN32)
  target_compile_definitions(renderer_vulkan PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
