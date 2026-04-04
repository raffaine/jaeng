set(SANDBOX_SOURCES
  main.cpp
  sandbox_app.cpp
  sandbox_app.h
)

if(WIN32)
  list(APPEND SANDBOX_SOURCES
    mesh_utils.cpp
    mesh_utils.h
  )
endif()

if(WIN32)
  add_executable(sandbox WIN32 ${SANDBOX_SOURCES})
else()
  add_executable(sandbox ${SANDBOX_SOURCES})
endif()

target_compile_definitions(sandbox PRIVATE UNICODE= _UNICODE= ${JAENG_PLATFORM_DEF})
target_include_directories(sandbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sandbox PRIVATE jaeng)

if(WIN32)
  add_dependencies(sandbox dxil_reflect compile_shaders renderer_d3d12)
  # Include generated reflection headers for the app
  target_include_directories(sandbox PRIVATE ${CMAKE_BINARY_DIR}/include)

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
