# Pluggable Renderer Starter (Win32 + D3D12 plugin)

A minimal, upgradeable engine skeleton with a **pluggable renderer**. Starts on Windows with a Direct3D 12 backend loaded as a DLL, so you can later add Vulkan/OpenGL implementations without touching your game or engine core.

## Layout

`

/engine
    /render
        /public           # Stable renderer API and plugin loader
            renderer_api.h
            renderer_plugin.h
        /frontend         # RAII wrapper used by the app/engine
            renderer.h
    /plugins
        /renderer_d3d12     # D3D12 backend (DLL)
            d3d12_renderer.cpp
/apps
    /sandbox            # Win32 app that loads the renderer and clears the screen
        main.cpp

``

## Build (Windows, Visual Studio)

Prereqs: Visual Studio 2022 (or newer) with Desktop development with C++ workload and Windows 10/11 SDK. Direct3D 12 headers/libs are included in the Windows SDK.

`at
:: From a Developer Command Prompt for VS
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug

:: Run
build\bin\Debug\sandbox.exe
``

For Ninja:

`at
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
build\bin\sandbox.exe
`

## Upgrading Path

*   Add new backends as DLLs under /plugins/renderer_vulkan, /plugins/renderer_opengl and export the same LoadRenderer symbol.
*   Replace Win32 windowing with SDL/GLFW by changing only RendererDesc::platform_window and the sandbox app.
*   The C ABI + opaque handles keep your engine stable while you evolve internals.

## License

MIT by default â€” adjust as you prefer.