# Apple platform configuration (macOS and iOS)

if(APPLE)
    if(IOS)
        list(APPEND JAENG_SOURCES
            platform/ios/ios_platform.mm
            platform/ios/ios_platform.h
            platform/ios/ios_window.mm
            platform/ios/ios_window.h
        )
    else()
        list(APPEND JAENG_SOURCES
            platform/macos/macos_platform.mm
            platform/macos/macos_platform.h
            platform/macos/macos_window.mm
            platform/macos/macos_window.h
        )
    endif()

    list(APPEND JAENG_SOURCES
        platform/apple/apple_process.cpp
        platform/apple/apple_process.h
        storage/win/filestorage.cpp
        storage/win/filestorage.h
    )

    set_source_files_properties(
        platform/ios/ios_platform.mm
        platform/ios/ios_window.mm
        platform/macos/macos_platform.mm
        platform/macos/macos_window.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )

    function(jaeng_configure_platform_apple target)
        find_library(FOUNDATION_LIBRARY Foundation REQUIRED)
        find_library(QUARTZCORE_LIBRARY QuartzCore REQUIRED)
        
        if(IOS)
            find_library(UIKIT_LIBRARY UIKit REQUIRED)
            target_link_libraries(${target} PRIVATE ${UIKIT_LIBRARY})
        else()
            find_library(APPKIT_LIBRARY AppKit REQUIRED)
            target_link_libraries(${target} PRIVATE ${APPKIT_LIBRARY})
        endif()
        
        target_link_libraries(${target} PRIVATE ${FOUNDATION_LIBRARY} ${QUARTZCORE_LIBRARY})
    endfunction()
endif()
