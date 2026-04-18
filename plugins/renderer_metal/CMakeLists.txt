set(RENDERER_TYPE SHARED)
if(IOS)
    set(RENDERER_TYPE STATIC)
endif()

add_library(renderer_metal ${RENDERER_TYPE}
    metal_renderer.mm
    metal_renderer.h
)

target_include_directories(renderer_metal PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../engine
    ${CMAKE_CURRENT_SOURCE_DIR}/../../engine/render/public
)

target_link_libraries(renderer_metal PRIVATE jaeng)

if(APPLE)
    find_library(METAL_LIBRARY Metal REQUIRED)
    find_library(QUARTZCORE_LIBRARY QuartzCore REQUIRED)
    find_library(FOUNDATION_LIBRARY Foundation REQUIRED)
    
    target_link_libraries(renderer_metal PRIVATE 
        ${METAL_LIBRARY} 
        ${QUARTZCORE_LIBRARY}
        ${FOUNDATION_LIBRARY}
    )
    
    set_target_properties(renderer_metal PROPERTIES 
        COMPILE_FLAGS "-fobjc-arc"
        PREFIX ""
    )
endif()
