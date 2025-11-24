cmake_minimum_required(VERSION 3.20)
project(ShaderCompiler LANGUAGES CXX)

set(SHADER_ROOT ${CMAKE_SOURCE_DIR}/shaders)
set(OUTPUT_DIR ${SHADER_ROOT}/compiled)
set(REFLECT_DIR ${SHADER_ROOT}/include)

find_program(DXC_EXECUTABLE dxc REQUIRED)

# Gather all pipeline folders
file(GLOB PIPELINES RELATIVE ${SHADER_ROOT}/hlsl "${SHADER_ROOT}/hlsl/*" LIST_DIRECTORIES true)

add_custom_target(compile_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${REFLECT_DIR}
    COMMENT "Compiling HLSL shaders and generating reflection headers"
)

foreach(PIPELINE ${PIPELINES})
    set(VERTEX_SHADER ${SHADER_ROOT}/hlsl/${PIPELINE}/vertex.hlsl)
    set(PIXEL_SHADER ${SHADER_ROOT}/hlsl/${PIPELINE}/pixel.hlsl)

    if(EXISTS ${VERTEX_SHADER} AND EXISTS ${PIXEL_SHADER})
        set(VERTEX_DXIL ${OUTPUT_DIR}/${PIPELINE}_vs.dxil)
        set(PIXEL_DXIL ${OUTPUT_DIR}/${PIPELINE}_ps.dxil)
        set(REFLECT_HEADER ${REFLECT_DIR}/${PIPELINE}_reflect.h)

        add_custom_command(TARGET compile_shaders POST_BUILD
            COMMAND ${DXC_EXECUTABLE} -T vs_6_0 -E main -Fo ${VERTEX_DXIL} ${VERTEX_SHADER}
            COMMAND ${DXC_EXECUTABLE} -T ps_6_0 -E main -Fo ${PIXEL_DXIL} ${PIXEL_SHADER}
            COMMAND dxil_reflect ${VERTEX_DXIL} ${PIXEL_DXIL} ${REFLECT_HEADER}

            COMMENT "Pipeline ${PIPELINE}: Compiling Vertex & Pixel shaders and generating reflection"
        )
    endif()
endforeach()
