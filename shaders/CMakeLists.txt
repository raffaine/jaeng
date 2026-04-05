cmake_minimum_required(VERSION 3.20)
project(ShaderCompiler LANGUAGES CXX)

set(SHADER_ROOT ${CMAKE_SOURCE_DIR}/shaders)
set(OUTPUT_DIR ${SHADER_ROOT}/compiled)
set(REFLECT_DIR ${SHADER_ROOT}/include)

find_program(DXC_EXECUTABLE dxc)
find_program(GLSLC_EXECUTABLE glslc)

# Gather all pipeline folders
file(GLOB PIPELINES RELATIVE ${SHADER_ROOT}/hlsl "${SHADER_ROOT}/hlsl/*" LIST_DIRECTORIES true)

add_custom_target(compile_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${REFLECT_DIR}
    COMMENT "Compiling HLSL shaders and generating reflection"
)

foreach(PIPELINE ${PIPELINES})
    set(VERTEX_SHADER ${SHADER_ROOT}/hlsl/${PIPELINE}/vertex.hlsl)
    set(PIXEL_SHADER ${SHADER_ROOT}/hlsl/${PIPELINE}/pixel.hlsl)

    if(EXISTS ${VERTEX_SHADER} AND EXISTS ${PIXEL_SHADER})
        set(VERTEX_DXIL ${OUTPUT_DIR}/${PIPELINE}_vs.dxil)
        set(PIXEL_DXIL ${OUTPUT_DIR}/${PIPELINE}_ps.dxil)
        set(VERTEX_SPV ${OUTPUT_DIR}/${PIPELINE}_vs.spv)
        set(PIXEL_SPV ${OUTPUT_DIR}/${PIPELINE}_ps.spv)
        set(REFLECT_OUT ${REFLECT_DIR}/${PIPELINE})

        if(WIN32 AND DXC_EXECUTABLE)
            add_custom_command(TARGET compile_shaders POST_BUILD
                COMMAND ${DXC_EXECUTABLE} -T vs_6_6 -E main -Fo ${VERTEX_DXIL} ${VERTEX_SHADER}
                COMMAND ${DXC_EXECUTABLE} -T ps_6_6 -E main -Fo ${PIXEL_DXIL} ${PIXEL_SHADER}
                COMMAND dxil_reflect ${VERTEX_DXIL} ${PIXEL_DXIL} ${PIPELINE} ${REFLECT_OUT}
                COMMENT "Pipeline ${PIPELINE}: Compiling DXIL and generating reflection"
            )
        endif()

        if(DXC_EXECUTABLE)
            add_custom_command(TARGET compile_shaders POST_BUILD
                COMMAND ${DXC_EXECUTABLE} -spirv -T vs_6_6 -E main -Fo ${VERTEX_SPV} ${VERTEX_SHADER} -D VULKAN
                COMMAND ${DXC_EXECUTABLE} -spirv -T ps_6_6 -E main -Fo ${PIXEL_SPV} ${PIXEL_SHADER} -D VULKAN -D VULKAN_BGRA
                COMMENT "Pipeline ${PIPELINE}: Compiling SPIR-V"
            )
        elseif(GLSLC_EXECUTABLE)
            add_custom_command(TARGET compile_shaders POST_BUILD
                COMMAND ${GLSLC_EXECUTABLE} -x hlsl -fshader-stage=vertex -fentry-point=main ${VERTEX_SHADER} -o ${VERTEX_SPV}
                COMMAND ${GLSLC_EXECUTABLE} -x hlsl -fshader-stage=fragment -fentry-point=main -DVULKAN_BGRA ${PIXEL_SHADER} -o ${PIXEL_SPV}
                COMMENT "Pipeline ${PIPELINE}: Compiling SPIR-V with glslc"
            )
        endif()
    endif()
endforeach()
