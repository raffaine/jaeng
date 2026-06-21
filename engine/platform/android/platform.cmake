function(jaeng_configure_platform_android target)
    if(NOT TARGET android_native_app_glue)
        add_library(android_native_app_glue STATIC ${CMAKE_ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
        target_include_directories(android_native_app_glue PUBLIC ${CMAKE_ANDROID_NDK}/sources/android/native_app_glue)
    endif()

    target_sources(${target} PRIVATE
        platform/android/android_platform.cpp
        platform/android/android_window.cpp
        platform/android/android_input.cpp
        platform/android/android_process.cpp
        storage/win/filestorage.cpp
    )
    
    target_compile_definitions(${target} PUBLIC JAENG_ANDROID=1)
    
    # Link Android specific libraries
    target_link_libraries(${target} PUBLIC android log EGL GLESv3 android_native_app_glue)
endfunction()
