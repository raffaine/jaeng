# Win32 platform configuration

list(APPEND JAENG_SOURCES
  platform/win32/win32_platform.h
  platform/win32/win32_platform.cpp
  storage/win/filestorage.cpp
  storage/win/filestorage.h
)

# Platform-specific target configuration
function(jaeng_configure_platform_win32 target)
  target_compile_definitions(${target} PRIVATE UNICODE= _UNICODE=)
endfunction()
