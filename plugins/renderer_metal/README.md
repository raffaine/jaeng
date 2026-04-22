# Metal Renderer Plugin

This plugin implements the Apple Metal backend for the `jaeng` engine, providing high-performance rendering for macOS and iOS devices.

## Core Implementation Details

- **Language:** C++20 with Objective-C++ (`.mm`) bridges for AppKit/UIKit integration.
- **Metal Interface:** Uses `metal-cpp` to interact with the Metal API without the overhead of Objective-C messaging in the hot path.
- **Resource Management:** Employs the `UniqueHandle` RAII pattern (defined in `metal_utils.h`) to manage the lifecycle of Metal objects (`MTL::Device`, `MTL::Buffer`, etc.) safely.
- **Threading:** Strictly follows the engine's three-thread model. Command buffers are generated exclusively on the Render thread, and the Simulation thread never blocks on GPU execution.
- **Memory:** Utilizes the engine-managed `TripleBuffer` and double-buffered command queues for cross-boundary communication.

## Bindless Model & Hardware Requirements

The Metal backend is designed to achieve parity with the engine's D3D12 and Vulkan bindless architectures using **Tier 2 Argument Buffers**. This allows shaders to access resources via the `ResourceDescriptorHeap[index]` syntax.

### Hardware & API Floor

To maintain architectural simplicity and avoid "discrete fallback" paths, this renderer targets modern Apple hardware and APIs:

| Platform | Minimum Version | Hardware Support |
| :--- | :--- | :--- |
| **macOS** | 13.0 (Ventura) | Apple Silicon (M1+) and modern Intel Macs (2018+). |
| **iOS** | 16.0 | A13 Bionic (iPhone 11) and later. |

### Technical Rationale

- **`gpuResourceID` & `gpuAddress`:** By targeting macOS 13+ and iOS 16+, we leverage direct GPU resource identifiers. This allows us to bypass `MTLArgumentEncoder` and treat the top-level Argument Buffer as a raw array of IDs, matching the D3D12/Vulkan descriptor heap model perfectly.
- **Unified Memory:** On Apple Silicon, the CPU writes descriptors directly into GPU-visible memory, eliminating staging copies and making topological passes extremely efficient.
- **Legacy Support:** Devices that do not support Tier 2 (e.g., iPhone XS and older, or legacy Intel iGPUs) are not supported by this plugin to prevent performance and code complexity regressions.

## Build Requirements

- **CMake:** Part of the standard engine build.
- **Dependencies:** 
  - `metal-cpp`: Downloaded automatically via `FetchContent`.
  - `spirv-cross`: Required for transpiling SPIR-V (from HLSL) to MSL. Must be available in the system path (e.g., `brew install spirv-cross`).
  - `glslc`: Part of the Vulkan SDK, used for the initial HLSL to SPIR-V compilation.

## Shader Pipeline

Native **HLSL** is the primary source. The pipeline is:
1. `glslc` compiles HLSL to SPIR-V.
2. `spirv-cross` transpiles SPIR-V to Metal Shading Language (MSL).
3. A custom Python script (`spv_reflect.py`) extracts reflection data from the SPIR-V to generate `MaterialSys` compatible JSON headers.
