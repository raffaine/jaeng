# Jaeng (Just Another Engine)

## Overview & Current State
Jaeng is a modern, cross-platform C++ game engine built with a strict focus on data locality, multithreading, and modularity. 

Currently, the engine features a fully decoupled, lock-free **three-thread architecture** and a **pluggable renderer** system. It supports Windows (Win32), Linux (Wayland), macOS, and iOS, capable of dynamically loading Direct3D 12, Vulkan, or Metal backends.

### Key Architectural Pillars
* **Multithreaded Subsystem Worlds**: The engine strictly divides responsibilities. 
  * *Main Thread*: Exclusively handles OS message pumping and WSI (Window System Integration) events.
  * *Simulation Thread*: Runs a deterministic, fixed-timestep game loop using a high-performance Sparse Set ECS (Entity Component System).
  * *Render Thread*: Consumes double-buffered `RenderProxy` command queues lock-free, updating its own spatial partitioner (isolated from the ECS) to dispatch draws to the GPU.
* **Pluggable Graphics APIs**: Backends (`renderer_d3d12`, `renderer_vulkan`, `renderer_metal`) are compiled as dynamic libraries (DLL/SO/dylib) and loaded at runtime via a stable C ABI. 
* **Data-Oriented ECS**: Components are densely packed in contiguous arrays using Sparse Sets to eliminate pointer-chasing and maximize CPU cache hits.
* **Bindless Metal Rendering**: The Metal backend utilizes Tier 2 Argument Buffers for bindless resource management, synchronized with a transpiled HLSL pipeline.

## Layout

```text
/engine               # Core Jaeng Library
  /common             # Utilities (Math, PubSub, Results, Logging, Apple Bridges)
  /entity             # Sparse-Set Entity Component System
  /material           # Material System & Shader Reflection
  /mesh               # Mesh & Geometric Data Management
  /platform           # OS Abstractions (Win32, Wayland, macOS, iOS)
  /render             # Rendering Frontend Wrapper & API Contracts
  /scene              # Render Proxy World & Spatial Partitioning
  /storage            # File and Memory Storage Management
/plugins              # Pluggable Graphics Backends
  /renderer_d3d12     # Windows Direct3D 12 Backend
  /renderer_vulkan    # Cross-Platform Vulkan Backend
  /renderer_metal     # Apple Metal Backend
/apps                 # Applications built on Jaeng
  /sandbox            # Multithreaded demo app utilizing the engine
/shaders              # HLSL Source and Transpilation Pipeline
```

## Building

Jaeng utilizes vcpkg for dependency management and CMakePresets.json to streamline cross-platform compilation.

### Prerequisites
- **Windows**: Visual Studio 2022+, Windows SDK, vcpkg.
- **Linux**: GCC/Clang, Wayland headers, Vulkan SDK, Ninja, vcpkg.
- **macOS/iOS**: Xcode 15+, Metal, vcpkg.

### Windows (ARM64 - D3D12 / Vulkan)
```DOS
:: Build for Windows using D3D12 (Default)
cmake --preset arm64-vs
cmake --build build/arm64-vs --config Debug

:: Build for Windows forcing the Vulkan backend
cmake --preset arm64-vs-vulkan
cmake --build build/arm64-vs-vulkan --config Debug

:: Run Sandbox
build\arm64-vs\bin\Debug\sandbox.exe
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

### macOS (Metal)
```Bash
cmake --preset macos-debug
cmake --build build/macos-debug

# Run Sandbox Bundle
open ./build/macos-debug/bin/sandbox.app
```

### iOS (Metal) - Requires XCode
```Bash
cmake --preset ios-debug

Open generated XCode project to Build and Run

```

## Near Term Roadmap

- **Presentation Layer Refactor**: Implement centralized V-Sync and PresentMode controls across all backends.
- **Physics Integration**: Add a dedicated Physics system to the Simulation loop.
- **Asset Pipeline**: Streamline texture and mesh processing into a unified runtime format.

## License

MIT
