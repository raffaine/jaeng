add_library(renderer_d3d12 SHARED
  d3d12_renderer.cpp
  d3d12_device.cpp
  d3d12_descriptors.cpp
  d3d12_depthmanager.cpp
  d3d12_upload.cpp
  d3d12_swapchain.cpp
  d3d12_commands.cpp
  d3d12_pipeline.cpp
  d3d12_resources.cpp
)

if(WIN32)
  target_compile_definitions(renderer_d3d12 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(renderer_d3d12 PRIVATE d3d12 dxgi dxguid)
endif()

target_include_directories(renderer_d3d12 PRIVATE ${ENGINE_ROOT}/render/public)
target_include_directories(renderer_d3d12 PRIVATE ${ENGINE_ROOT})

# We rely on CMAKE_RUNTIME_OUTPUT_DIRECTORY placing it alongside the exe
