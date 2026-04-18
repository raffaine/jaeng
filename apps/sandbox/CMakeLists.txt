set(SANDBOX_SOURCES
  main.cpp
  sandbox_app.cpp
  sandbox_app.h
  mesh_utils.cpp
  mesh_utils.h
)

if(WIN32)
  add_executable(sandbox WIN32 ${SANDBOX_SOURCES})
elseif(APPLE)
  add_executable(sandbox MACOSX_BUNDLE ${SANDBOX_SOURCES})
  set_target_properties(sandbox PROPERTIES
    MACOSX_BUNDLE_BUNDLE_IDENTIFIER "com.sintropia.jaeng.sandbox"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.sintropia.jaeng.sandbox"
    MACOSX_BUNDLE_BUNDLE_NAME "jaeng Sandbox"
  )
else()
  add_executable(sandbox ${SANDBOX_SOURCES})
endif()

target_compile_definitions(sandbox PRIVATE UNICODE= _UNICODE= ${JAENG_PLATFORM_DEF} 
  "JAENG_SHADER_DIR=\"${CMAKE_SOURCE_DIR}/shaders\""
  "JAENG_ASSET_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\""
)
target_include_directories(sandbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/shaders/include)
target_link_libraries(sandbox PRIVATE jaeng)

if(APPLE)
  target_link_libraries(sandbox PRIVATE renderer_metal)
  
  # Ensure plugins and TestServer are inside the bundle for easy loading
  add_custom_command(TARGET sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:renderer_metal> $<TARGET_FILE_DIR:sandbox>/
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:TestServer> $<TARGET_FILE_DIR:sandbox>/
    COMMENT "Copying dependencies into sandbox bundle"
  )
endif()

add_dependencies(sandbox TestServer)


if(WIN32)
  # Visual Studio Project Settings
  set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT sandbox)
  set_target_properties(sandbox PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$(TargetDir)")
  if(JAENG_FORCE_VULKAN)
    add_dependencies(sandbox compile_shaders renderer_vulkan)
    target_compile_definitions(sandbox PRIVATE JAENG_USE_VULKAN)
  else()
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
  endif()

  target_compile_definitions(sandbox PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

  add_custom_command(TARGET sandbox POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_RUNTIME_DLLS:sandbox> $<TARGET_FILE_DIR:sandbox>
      COMMAND_EXPAND_LISTS
  )
endif()
