# Project Context: jaeng

- **Core Tech:** C++20 Game Engine. Cross-platform (Win32, macOS, iOS, Wayland), Dynamic Loaded Renderers (D3D12, Vulkan, Metal).
- **Architecture:** Lock-free, three-thread model (Main/OS, Simulation, Render) utilizing an engine-managed Triple Buffer.
- **Architecture Preferences:**
  - **Concurrency:** The Simulation thread MUST NEVER block waiting on the Render thread. Communicate across boundaries strictly via the `TripleBuffer` and double-buffered command queues (e.g., `RenderProxy`).
  - **ECS & Data Locality:** Favor flat contiguous arrays and topological passes. Avoid pointer-chasing and recursive tree traversals.
  - **No Global State:** Prefer passing Context/Device handles and Subsystem references.
  - **Error Handling:** Use the internal `result<T>` type for all failable operations.
  - **RAII:** Use `UniqueHandle` patterns; avoid manual `Release()` or `destroy()`.
  - **Platform Backend:** Native implementations (Win32, Wayland) over generic libraries like SDL3.
  - **Logging:** Unified `JAENG_LOG_*` system.

- **Roadmap:**
## Active Tasks

- [ ] **Presentation Layer Refactor**
  - Expose V-Sync and `PresentMode` via `AppConfig`.
  - Safely support uncapped framerates across D3D12, Vulkan, and Metal backends.
  - Implement adaptive sync support where available.

## Architecture Preferences
- **Concurrency:** The Simulation thread MUST NEVER block waiting on the Render thread. Communicate across boundaries strictly via the `TripleBuffer` and double-buffered command queues (e.g., `RenderProxy`).
- **ECS & Data Locality:** Favor flat contiguous arrays and topological passes. Avoid pointer-chasing and recursive tree traversals.
- **No Global State:** Prefer passing Context/Device handles and Subsystem references.
- **Error Handling:** Use the internal `result<T>` type for all failable operations.
- **RAII:** Use `UniqueHandle` patterns; avoid manual `Release()` or `destroy()`.
- **Platform Backend:** Native implementations (Win32, Wayland, macOS, iOS) over generic libraries like SDL3.
- **Logging:** Unified `JAENG_LOG_*` system.

## Shader Pipeline
- **Native Language:** HLSL.
- **Target:** DXIL (D3D12), SPIR-V (Vulkan), and MSL (Metal via SPIRV-Cross).
- **Bindless Style:** Use `ResourceDescriptorHeap[index]` syntax. Tier 2 Argument Buffers for Metal.
