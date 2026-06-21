# JAENG Engine Integration Guide

JAENG is designed to be easily embedded directly into your game or application projects as a Git submodule. This allows you to build the engine simultaneously with your project, ensuring matching toolchains and seamless debugging across the engine boundary.

This guide will walk you through setting up a new project, configuring CMake to integrate JAENG, and building your first application.

## 1. Adding JAENG as a Submodule

In your game's root directory, add the JAENG repository as a Git submodule:

```bash
mkdir my_game
cd my_game
git init
git submodule add https://github.com/sintropia/jaeng.git external/jaeng
```

## 2. Dependencies

JAENG relies on several external dependencies. It is expected that the parent project manages these dependencies using **vcpkg** (or a similar package manager) and passes the toolchain to CMake.

### Required Packages (vcpkg)
Create a `vcpkg.json` at the root of your project:
```json
{
  "name": "my-game",
  "version-string": "0.1.0",
  "dependencies": [
    "glm",
    "nlohmann-json",
    "nlohmann-json-schema-validator",
    "stb",
    "spirv-cross",
    {
      "name": "wayland",
      "platform": "linux & !android"
    },
    {
      "name": "libdecor",
      "platform": "linux & !android"
    }
  ]
}
```

## 3. CMake Configuration

Create a `CMakeLists.txt` in the root of your project. You simply need to call `add_subdirectory(external/jaeng)` and link your executable to the `jaeng::engine` alias target.

```cmake
cmake_minimum_required(VERSION 3.24)
project(my_game C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Output configurations (DLLs, plugins, and EXE all go to the same bin directory)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 2. Add JAENG Engine
add_subdirectory(external/jaeng)

# 3. Add your Game Executable
add_executable(my_game src/main.cpp)

# 4. Link JAENG Engine (This will automatically link dependencies and set include directories)
target_link_libraries(my_game PRIVATE jaeng::engine)

# 5. Asset and Plugin Management
# JAENG dynamically loads renderer plugins (e.g., librenderer_vulkan.so / renderer_d3d12.dll).
# Because we mapped the global CMAKE_RUNTIME_OUTPUT_DIRECTORY and CMAKE_LIBRARY_OUTPUT_DIRECTORY 
# to the same 'bin' directory, the game executable and the plugins will naturally sit side-by-side!

# You MUST copy JAENG's compiled default shaders to your binary output directory:
add_custom_command(TARGET my_game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
        ${CMAKE_CURRENT_SOURCE_DIR}/external/jaeng/shaders 
        $<TARGET_FILE_DIR:my_game>/shaders
    COMMENT "Copying JAENG core shaders to binary directory"
)
```

## 4. The Shader Pipeline

JAENG comes with a powerful, automated multi-backend shader compiler that transpiles HLSL into DXIL (Windows), SPIR-V (Vulkan), MSL (Apple), and generates C++ Reflection data (JSON).

### Compiling Your Custom Shaders
Instead of reinventing the wheel, JAENG exposes its internal shader compiler via a CMake function: `jaeng_add_shaders()`.

If you write custom shaders for your game in `src/shaders/hlsl`, you can compile them automatically during the build:

```cmake
# Call the JAENG shader compiler macro
jaeng_add_shaders(
    compile_my_game_shaders             # The CMake Target Name
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/hlsl      # Input directory containing pipeline folders
    $<TARGET_FILE_DIR:my_game>/shaders/compiled       # Output directory for compiled bytecode
    $<TARGET_FILE_DIR:my_game>/shaders/include        # Output directory for reflection JSON
)

# Ensure shaders compile before the game builds
add_dependencies(my_game compile_my_game_shaders)
```

**Directory Structure:**
The input directory must contain subdirectories representing "pipelines". Each pipeline directory must contain a `vertex.hlsl` and `pixel.hlsl` file.
```text
src/
  shaders/
    hlsl/
      my_custom_material/
        vertex.hlsl
        pixel.hlsl
```

## 5. Minimal Game Application (`src/main.cpp`)

To bring up the engine, implement the `jaeng::platform::Application` interface. The engine `main` entrypoint is already provided by JAENG internally (across Windows, macOS, Android, and Linux).

```cpp
#include "platform/public/platform_api.h"
#include "common/app_state.h"
#include "common/logging.h"

using namespace jaeng;

class MyGame : public platform::Application {
public:
    void init() override {
        JAENG_LOG_INFO("MyGame Initializing!");
    }
    
    void update(float dt) override {
        // Game Logic here
    }
    
    void draw(float dt) override {
        // Rendering logic here
    }
    
    void shutdown() override {
        JAENG_LOG_INFO("MyGame Shutting Down!");
    }
};

// Implement the factory function expected by JAENG's internal platform main()
std::unique_ptr<platform::Application> jaeng::platform::create_application() {
    return std::make_unique<MyGame>();
}
```

## 6. CMakePresets.json (Recommended Building Method)

Instead of passing verbose toolchain arguments manually, it is highly recommended to leverage `CMakePresets.json`. This allows developers on your team to simply run `cmake --preset <name>` seamlessly.

Create a `CMakePresets.json` in your project root. You can adapt the following to fit your paths:

```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "macos-debug",
            "displayName": "macOS Debug",
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/macos-debug",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "CMAKE_TOOLCHAIN_FILE": "/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake",
                "VCPKG_TARGET_TRIPLET": "arm64-osx"
            }
        },
        {
            "name": "windows-vulkan",
            "displayName": "Windows (Vulkan)",
            "generator": "Visual Studio 17 2022",
            "binaryDir": "${sourceDir}/build/windows-vulkan",
            "cacheVariables": {
                "CMAKE_TOOLCHAIN_FILE": "C:/vcpkg/scripts/buildsystems/vcpkg.cmake",
                "JAENG_FORCE_VULKAN": "ON"
            }
        }
    ]
}
```
You can now build your game by running:
```bash
cmake --preset macos-debug
cmake --build build/macos-debug
```

## 7. Android Onboarding

Android requires an Android Studio project wrapper to bundle the C++ executable and shaders into an APK. 

1. Create a standard Android Studio C++ project in an `android/` subdirectory.
2. In your `android/app/build.gradle`, configure the `externalNativeBuild` to point to your project's root `CMakeLists.txt`:
```groovy
    externalNativeBuild {
        cmake {
            path "../../../CMakeLists.txt"
            version "3.22.1+"
        }
    }
```
3. Pass the CMake Preset equivalent toolchain variables inside `defaultConfig`:
```groovy
        externalNativeBuild {
            cmake {
                arguments "-DANDROID=ON",
                          "-DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake",
                          "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=${project.android.ndkDirectory.absolutePath}/build/cmake/android.toolchain.cmake",
                          "-DVCPKG_TARGET_TRIPLET=arm64-android"
                targets "my_game", "renderer_vulkan"
            }
        }
```
4. Map the engine's compiled shaders to the Android `assets` directory so the engine can load them at runtime:
```groovy
    sourceSets {
        main {
            assets.srcDirs = [
                'src/main/assets',
                '../../../external/jaeng/shaders' // JAENG Core Shaders
                // '../../../shaders/compiled'    // Your Custom Shaders
            ]
        }
    }
```
