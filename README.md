# Jaeng, Just Another Engine

## Current State
 Pluggable Renderer Starter (Win32 + D3D12 plugin)

 A minimal, upgradeable engine skeleton with a **pluggable renderer**. Started on Windows with a Direct3D 12 backend loaded as a DLL, so later Vulkan/OpenGL implementations can be added without touching game or engine core.

## Layout

```
/engine    # Jaeng (src and headers)
  /common    # Header-only utilities, such as a result object and pub-sub support
    /math        # General Use Math utilities
  /entity    # Entity Component System
  /material  # Material System
  /mesh      # Mesh (Geometric data) System
  /render    # Renderer (Pluggable)
    /public      # Stable renderer API and plugin loader
    /frontend    # RAII wrapper used by the app/engine
  /scene     # Scene Management and Space Partition
  /storage   # File (Physical or Virtual) Management
/plugins   # Available Renderer Plugins
  /renderer_d3d12  # D3D12 backend (DLL)
/apps      # Apps using Jaeng
  /sandbox      # Win32 app that loads the D3D12 renderer, builds and renders a scene
```

## Build (Windows, Visual Studio)

Prereqs: Visual Studio 2022 (or newer) with Desktop development with C++ workload and Windows 10/11 SDK. Direct3D 12 headers/libs are included in the Windows SDK.

```
:: From a Developer Command Prompt for VS (for an arm64)
cmake -S . -B build -G "Visual Studio 17 2022" -A arm64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=arm64-windows
cmake --build build --config Debug

:: Run
build\bin\Debug\sandbox.exe
```

For Ninja:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
build\bin\sandbox.exe
```

## Upgrading Path

*   Add new backends as DLLs under /plugins/renderer_vulkan, /plugins/renderer_opengl and export the same LoadRenderer symbol.
*   Replace Win32 windowing with SDL/GLFW by changing only RendererDesc::platform_window and the sandbox app.
*   The C ABI + opaque handles keep engine stable while internals evolve.

## License

MIT
