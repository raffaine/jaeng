# Project Context: jaeng

- **Core Tech:** C++20 Game Engine. Cross-platform (Win32 & Wayland), Dynamic Loaded Rendereres (D3D12, Vulkan).
- **Architecture:** Lock-free, three-thread model (Main/OS, Simulation, Render) utilizing an engine-managed Triple Buffer.
- **Roadmap:**
  1. (Completed) **Multithreaded Subsystem Worlds** & Triple Buffering.
  2. (Completed) **Data-Oriented Scene Graph** (Hierarchical Transforms baked per tick).
  3. (Completed) **Skeletal Animation System** (ECS-based bones, Animator component).
  4. (Completed) **Interaction & UI Systems** (Screen-to-world raycasting for entity picking, 2D Orthographic pass for text/UI batching).
  5. (Completed) **Platform Process Hosting** (Expand Engine to Launch and Manage other Processes through the Platform class in a ProcessManager. Spawned Processes should terminate with the main process.).
  6. (Current) **Threading and Async Task Group Infrastructure** (Expose to apps a uniform threading capacity that ensures the three-thread model while enabling apps ability to expand. Main/OS thread will also hold a scheduler for Task distribution into a pool of worker threads, this should be exposed to the app in a way that allows for task chaining and ability to run on scheduler's thread, a form of sync mode. This system should also expose an interface for apps to use C++ co_await semantics)
  7. (Next) **Asynchronous Asset Pipeline** (Background IO thread pool for lock-free streaming).
  8. (Future) **Presentation Layer Refactor** (Expose V-Sync and `PresentMode` via `AppConfig` to safely support uncapped framerates across both D3D12 and Vulkan backends).
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
