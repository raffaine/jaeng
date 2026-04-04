# Wayland platform configuration

# Wayland-scanner generation
set(XDG_SHELL_XML "/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml")
set(XDG_SHELL_PROTOCOL_C "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c")
set(XDG_SHELL_CLIENT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h")
set(XDG_DECORATION_XML "/usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
set(XDG_DECORATION_PROTOCOL_C "${CMAKE_CURRENT_BINARY_DIR}/xdg-decoration-protocol.c")
set(XDG_DECORATION_CLIENT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/xdg-decoration-client-protocol.h")

add_custom_command(
    OUTPUT "${XDG_SHELL_PROTOCOL_C}"
    COMMAND ${WAYLAND_SCANNER} public-code "${XDG_SHELL_XML}" "${XDG_SHELL_PROTOCOL_C}"
    DEPENDS "${XDG_SHELL_XML}"
    VERBATIM
)

add_custom_command(
    OUTPUT "${XDG_SHELL_CLIENT_HEADER}"
    COMMAND ${WAYLAND_SCANNER} client-header "${XDG_SHELL_XML}" "${XDG_SHELL_CLIENT_HEADER}"
    DEPENDS "${XDG_SHELL_XML}"
    VERBATIM
)

add_custom_command(
    OUTPUT "${XDG_DECORATION_PROTOCOL_C}"
    COMMAND ${WAYLAND_SCANNER} public-code "${XDG_DECORATION_XML}" "${XDG_DECORATION_PROTOCOL_C}"
    DEPENDS "${XDG_DECORATION_XML}"
    VERBATIM
)

add_custom_command(
    OUTPUT "${XDG_DECORATION_CLIENT_HEADER}"
    COMMAND ${WAYLAND_SCANNER} client-header "${XDG_DECORATION_XML}" "${XDG_DECORATION_CLIENT_HEADER}"
    DEPENDS "${XDG_DECORATION_XML}"
    VERBATIM
)

set_source_files_properties("${XDG_SHELL_PROTOCOL_C}" PROPERTIES GENERATED TRUE)
set_source_files_properties("${XDG_SHELL_CLIENT_HEADER}" PROPERTIES GENERATED TRUE)
set_source_files_properties("${XDG_DECORATION_PROTOCOL_C}" PROPERTIES GENERATED TRUE)
set_source_files_properties("${XDG_DECORATION_CLIENT_HEADER}" PROPERTIES GENERATED TRUE)

add_library(jaeng_protocol STATIC "${XDG_SHELL_PROTOCOL_C}" "${XDG_DECORATION_PROTOCOL_C}")
set_target_properties(jaeng_protocol PROPERTIES LINKER_LANGUAGE C)
target_include_directories(jaeng_protocol PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(jaeng_protocol PRIVATE wayland-client)

list(APPEND JAENG_SOURCES
  platform/wayland/wayland_platform.h
  platform/wayland/wayland_platform.cpp
  platform/wayland/wayland_window.h
  platform/wayland/wayland_window.cpp
  platform/wayland/wayland_input.h
  platform/wayland/wayland_input.cpp
  storage/win/filestorage.cpp
  "${XDG_SHELL_CLIENT_HEADER}"
  "${XDG_DECORATION_CLIENT_HEADER}"
)

# Platform-specific target configuration
function(jaeng_configure_platform_wayland target)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${WAYLAND_INCLUDE_DIRS})
  target_link_libraries(${target} PUBLIC wayland-client xkbcommon jaeng_protocol decor-0)
endfunction()
