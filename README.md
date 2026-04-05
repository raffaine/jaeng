# Jaeng (Just Another Engine)

## Overview & Current State
Jaeng is a modern, cross-platform C++ game engine built with a strict focus on data locality, multithreading, and modularity. 

Currently, the engine features a fully decoupled, lock-free **three-thread architecture** and a **pluggable renderer** system. It supports Windows (Win32) and Linux (Wayland), capable of dynamically loading Direct3D 12 or Vulkan backends without exposing graphics API details to the simulation layer.

### Key Architectural Pillars
* **Multithreaded Subsystem Worlds**: The engine strictly divides responsibilities. 
  * *Main Thread*: Exclusively handles OS message pumping and WSI (Window System Integration) events.
  * *Simulation Thread*: Runs a deterministic, fixed-timestep game loop using a high-performance Sparse Set ECS (Entity Component System).
  * *Render Thread*: Consumes double-buffered `RenderProxy` command queues lock-free, updating its own spatial partitioner (isolated from the ECS) to dispatch draws to the GPU.
* **Pluggable Graphics APIs**: Backends (`renderer_d3d12`, `renderer_vulkan`) are compiled as dynamic libraries (DLL/SO) and loaded at runtime via a stable C ABI. 
* **Data-Oriented ECS**: Components are densely packed in contiguous arrays using Sparse Sets to eliminate pointer-chasing and maximize CPU cache hits.

## Layout

```text
/engine               # Core Jaeng Library (src and headers)
  /common             # Header-only utilities (Math, PubSub, Results, Logging)
  /entity             # Sparse-Set Entity Component System
  /material           # Material System & Shader Reflection
  /mesh               # Mesh & Geometric Data Management
  /platform           # OS Abstractions (Win32, Wayland)
  /render             # Rendering Frontend Wrapper & API Contracts
  /scene              # Render Proxy World & Spatial Partitioning
  /storage            # File and Memory Storage Management
/plugins              # Pluggable Graphics Backends
  /renderer_d3d12     # Windows Direct3D 12 Backend
  /renderer_vulkan    # Cross-Platform Vulkan Backend
/apps                 # Applications built on Jaeng
  /sandbox            # Multithreaded demo app utilizing the engine
```
## Building

Jaeng utilizes vcpkg for dependency management and CMakePresets.json to streamline cross-platform compilation.

### Prerequisites
Windows: Visual Studio 2022 (or newer), Windows SDK, and vcpkg.

Linux (Wayland): GCC/Clang, Wayland development headers, Vulkan SDK, ninja-build, and vcpkg.

### Windows (ARM64 / x64)

Use the included presets from a Developer Command Prompt.

```DOS
:: Build for Windows using D3D12 (Default)
cmake --preset arm64-vs
cmake --build build/arm64-vs --config Debug

:: Build for Windows forcing the Vulkan backend
cmake --preset arm64-vs-vulkan
cmake --build build/arm64-vs-vulkan --config Debug

:: Run Sandbox
build\arm64-vs-vulkan\bin\Debug\sandbox.exe
```

### Linux (Wayland / Vulkan)

The Linux build leverages Ninja for highly parallelized, fast incremental builds.

```Bash
# Configure and build using Ninja
cmake --preset linux-debug
cmake --build build/linux-debug

# Run Sandbox
./build/linux-debug/bin/sandbox
```

## Upgrading Path

- Simulation Systems: Add Physics or Animation systems directly to the Simulation loop; they will sync with the Render Thread automatically via the Proxy Command Queue.

- New Backends: Add new backends (e.g., Metal) under /plugins/renderer_metal exporting the standard LoadRenderer symbol.

- Windowing: Additional platform layers (e.g., macOS/Cocoa) can be added cleanly under /engine/platform by fulfilling the IApplication and IWindow contracts.

## License

MIT
