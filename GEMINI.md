# Project Context: jaeng

- **Core Tech:** C++20 Game Engine. Cross-platform (Win32 & Wayland), Dynamic Loaded Rendereres (D3D12, Vulkan).
- **Architecture:** Lock-free, three-thread model (Main/OS, Simulation, Render) utilizing an engine-managed Triple Buffer.
- **Roadmap:**
## JAENG Apple Expansion Tasks (macOS / iOS)

### 1. Platform & OS Layer Implementation
- [x] Create Objective-C++ `.mm` bridge files strictly for interfacing with AppKit (macOS) and UIKit (iOS)[cite: 5, 7].
- [x] Map the `NSApplication` and `UIApplication` run loops directly to the engine's Main/OS thread[cite: 8].
- [x] Wrap Cocoa window references into existing Context/Device handles to strictly avoid global state[cite: 9, 33].
- [x] Implement event handling by pushing Cocoa input and window events into the existing lock-free queues to be consumed by the Simulation thread[cite: 10].
- [x] Ensure this layer adheres to the philosophy of using native implementations over generic libraries like SDL3[cite: 6, 36].

### 2. Metal Renderer Backend
- [x] Set up `metal-cpp` to act as a dynamically loaded renderer alongside the existing D3D12 and Vulkan backends[cite: 11, 12, 28].
- [x] Generate Metal command buffers exclusively on the Render thread[cite: 13].
- [x] Strictly communicate across boundaries using the engine-managed TripleBuffer and double-buffered command queues[cite: 14, 29, 31].
- [x] Ensure the Simulation thread never blocks waiting on the Metal Render thread[cite: 15, 30].
- [x] Wrap Metal reference-counted objects (e.g., `MTL::Device`, `MTL::CommandQueue`, `MTL::Buffer`) in `UniqueHandle` patterns to avoid manual release calls[cite: 16, 17, 35].
- [x] Return the engine's internal result type for all failable Metal initializations instead of throwing exceptions[cite: 18, 34].

### 3. Shader Pipeline Translation
* Maintain native HLSL as the primary shader language[cite: 20, 37].
* Integrate SPIRV-Cross into the build pipeline to transpile SPIR-V (compiled via GLSC) into Metal Shading Language (MSL)[cite: 21, 38].
* Map Metal Tier 2 Argument Buffers to the engine's bindless `ResourceDescriptorHeap[index]` syntax[cite: 22, 39].
* Configure the pipeline to bind a single top-level Argument Buffer per frame containing all resource descriptors to keep topological passes fast[cite: 23].

### 4. Apple Silicon ECS Optimizations
  - Continue utilizing flat contiguous arrays and topological passes to maximize data locality on Apple's ARM architecture[cite: 24, 25, 32].
  - Avoid pointer-chasing and recursive tree traversals to maximize throughput on Apple's unified memory[cite: 26, 33].
  - (Future) **Presentation Layer Refactor** (Expose V-Sync and `PresentMode` via `AppConfig` to safely support uncapped framerates across both D3D12 and Vulkan backends).
- **Architecture Preferences:**
  - **Concurrency:** The Simulation thread MUST NEVER block waiting on the Render thread. Communicate across boundaries strictly via the `TripleBuffer` and double-buffered command queues (e.g., `RenderProxy`).
  - **ECS & Data Locality:** Favor flat contiguous arrays and topological passes. Avoid pointer-chasing and recursive tree traversals.
  - **No Global State:** Prefer passing Context/Device handles and Subsystem references.
  - **Error Handling:** Use the internal `result<T>` type for all failable operations.
  - **RAII:** Use `UniqueHandle` patterns; avoid manual `Release()` or `destroy()`.
  - **Platform Backend:** Native implementations (Win32, Wayland) over generic libraries like SDL3.
  - **Logging:** Unified `JAENG_LOG_*` system.
- **Shader Pipeline:**
  - Native Language: **HLSL**.
  - Target: DXIL (D3D12) and SPIR-V (Vulkan) via **DXC** (Windows) or **GLSC** (Linux).
  - Bindless Style: Use `ResourceDescriptorHeap[index]` syntax.
