cmake_minimum_required(VERSION 3.23)
project(jaeng C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

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

# Global dependencies
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)

if(UNIX AND NOT APPLE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-protocols xkbcommon libdecor-0)
  find_program(WAYLAND_SCANNER NAMES wayland-scanner REQUIRED)
endif()

set(ENGINE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/engine)

if(WIN32)
  set(JAENG_PLATFORM_DEF JAENG_WIN32)
elseif(UNIX AND NOT APPLE)
  set(JAENG_PLATFORM_DEF JAENG_LINUX)
endif()

# Tools
if(WIN32)
  add_subdirectory(tools/dxil_reflect)
endif()

# Shaders
if(WIN32)
  add_subdirectory(shaders)
else()
  find_program(DXC_EXECUTABLE NAMES dxc)
  if(DXC_EXECUTABLE)
    add_subdirectory(shaders)
  else()
    message(WARNING "dxc not found, skipping shaders")
  endif()
endif()

# Engine Core & Plugins
add_subdirectory(engine)
if(WIN32)
  add_subdirectory(plugins/renderer_d3d12)
endif()

# Options
option(JAENG_BUILD_SANDBOX "Build the sandbox application" ON)

if(JAENG_BUILD_SANDBOX)
  add_subdirectory(apps/sandbox)
endif()
