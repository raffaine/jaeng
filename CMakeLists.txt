cmake_minimum_required(VERSION 3.23)
project(PluggableRenderer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

# DXIL Reflection (D3D12 HLSL Compiled Shaders -> Header Files for main function)
add_subdirectory(tools/dxil_reflect)

# Shader Compiler
add_subdirectory(shaders)

# Output directories for all targets (DLLs/EXEs in one bin dir)
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
  string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG_UPPER)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG})
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${CMAKE_BINARY_DIR}/lib/${OUTPUTCONFIG})
endforeach()
if(NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
endif()

set(ENGINE_ROOT ${CMAKE_SOURCE_DIR}/engine)

# Interface target for renderer API headers
add_library(renderer_api INTERFACE)
target_include_directories(renderer_api INTERFACE ${ENGINE_ROOT}/render/public)

# D3D12 plugin (DLL)
add_library(renderer_d3d12 SHARED
  plugins/renderer_d3d12/d3d12_renderer.cpp
  plugins/renderer_d3d12/d3d12_bind.cpp
  plugins/renderer_d3d12/d3d12_device.cpp
  plugins/renderer_d3d12/d3d12_descriptors.cpp
  plugins/renderer_d3d12/d3d12_depthmanager.cpp
  plugins/renderer_d3d12/d3d12_upload.cpp
  plugins/renderer_d3d12/d3d12_swapchain.cpp
  plugins/renderer_d3d12/d3d12_commands.cpp
  plugins/renderer_d3d12/d3d12_pipeline.cpp
  plugins/renderer_d3d12/d3d12_resources.cpp
)
if(WIN32)
  target_compile_definitions(renderer_d3d12 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(renderer_d3d12 PRIVATE d3d12 dxgi dxguid)
endif()
target_include_directories(renderer_d3d12 PRIVATE ${ENGINE_ROOT}/render/public)
target_include_directories(renderer_d3d12 PRIVATE ${ENGINE_ROOT})

# GLM (OpenGL Math Library) for General Systems
find_package(glm CONFIG REQUIRED)

# Find nlohmann/json for Material System
find_package(nlohmann_json CONFIG REQUIRED)

# Find json-schema-validator
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)

# Sandbox app
add_executable(sandbox WIN32
  apps/sandbox/main.cpp
  engine/common/pubsub.h
  engine/render/frontend/renderer.h
  engine/render/graph/render_graph.h
  engine/storage/ifstorage.h
  engine/storage/win/filestorage.cpp
  engine/material/imaterialsys.h
  engine/material/materialsys.cpp
  engine/mesh/imeshsys.h
  engine/mesh/meshsys.cpp
  engine/entity/entity.h
  engine/scene/scene.cpp
  engine/scene/grid_partition.cpp
)
target_compile_definitions(sandbox PRIVATE UNICODE= _UNICODE=)
target_include_directories(sandbox PRIVATE ${ENGINE_ROOT})
target_link_libraries(sandbox PRIVATE renderer_api glm::glm-header-only)
target_link_libraries(sandbox PRIVATE nlohmann_json::nlohmann_json nlohmann_json_schema_validator::validator)
add_dependencies(sandbox dxil_reflect compile_shaders renderer_d3d12)

# Include generated reflection headers for the app
target_include_directories(sandbox PRIVATE ${CMAKE_BINARY_DIR}/include)


if(WIN32)
  # vcpkg provides a config package for WinPixEventRuntime
  find_package(winpixevent CONFIG REQUIRED)

  find_path(PIX_INCLUDE_DIRS "pix3.h")
  target_include_directories(sandbox PRIVATE 
    ${CMAKE_SOURCE_DIR}/shaders/include
    ${PIX_INCLUDE_DIRS}
  )

  target_link_libraries(sandbox PRIVATE d3dcompiler Microsoft::WinPixEventRuntime)
  target_compile_definitions(sandbox PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

  add_custom_command(TARGET sandbox POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_RUNTIME_DLLS:sandbox> $<TARGET_FILE_DIR:sandbox>
      COMMAND_EXPAND_LISTS
  )
endif()

# Copy the plugin DLL next to the sandbox exe after build
add_custom_command(TARGET renderer_d3d12 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:renderer_d3d12>"
    "$<TARGET_FILE_DIR:sandbox>"
)