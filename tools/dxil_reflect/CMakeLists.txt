cmake_minimum_required(VERSION 3.23)
project(dxil_reflect LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(dxil_reflect dxil_reflect.cpp)
target_link_libraries(dxil_reflect PRIVATE dxcompiler.lib d3dcompiler.lib)

# Ensure modern C++ standard
#target_compile_features(dxil_reflect PRIVATE cxx_std_20)

# Set output directory for the tool
set_target_properties(dxil_reflect PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
)

