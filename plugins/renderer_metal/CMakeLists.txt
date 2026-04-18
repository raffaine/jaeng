include(FetchContent)
FetchContent_Declare(
    metal-cpp
    GIT_REPOSITORY https://github.com/Unbinilium/Metal.git
    GIT_TAG        main
    GIT_SHALLOW    ON
)
FetchContent_GetProperties(metal-cpp)
if(NOT metal-cpp_POPULATED)
    FetchContent_Populate(metal-cpp)
endif()

# Define our own Metal interface target for the headers
add_library(metal-cpp-headers INTERFACE)
target_include_directories(metal-cpp-headers INTERFACE
    ${metal-cpp_SOURCE_DIR}/metal-cpp
    ${metal-cpp_SOURCE_DIR}/metal-cpp-extensions
)
target_compile_definitions(metal-cpp-headers INTERFACE
    NS_PRIVATE_IMPLEMENTATION
    CA_PRIVATE_IMPLEMENTATION
    MTL_PRIVATE_IMPLEMENTATION
)

set(RENDERER_TYPE SHARED)
if(IOS)
    set(RENDERER_TYPE STATIC)
endif()

add_library(renderer_metal ${RENDERER_TYPE}
    metal_renderer.cpp
    metal_renderer.h
)

target_include_directories(renderer_metal PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../engine
    ${CMAKE_CURRENT_SOURCE_DIR}/../../engine/render/public
)

target_link_libraries(renderer_metal PRIVATE jaeng metal-cpp-headers)

if(APPLE)
    find_library(METAL_LIBRARY Metal REQUIRED)
    find_library(QUARTZCORE_LIBRARY QuartzCore REQUIRED)
    find_library(FOUNDATION_LIBRARY Foundation REQUIRED)
    
    target_link_libraries(renderer_metal PRIVATE 
        ${METAL_LIBRARY} 
        ${QUARTZCORE_LIBRARY}
        ${FOUNDATION_LIBRARY}
    )
    
    if(NOT IOS)
        find_library(APPKIT_LIBRARY AppKit REQUIRED)
        target_link_libraries(renderer_metal PRIVATE ${APPKIT_LIBRARY})
    endif()
    
    set_target_properties(renderer_metal PROPERTIES 
        PREFIX ""
    )
endif()
