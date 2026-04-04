# Project Context: jaeng

- **Core Tech:** C++20 Game Engine (D3D12/Win32).
- **Roadmap:**
  1. (Completed) **Bindless Rendering** (Resource Descriptor Heaps / Descriptor Indexing).
  2. (Completed) **Platform Abstraction Layer** (PAL) and Engine/Application decoupling.
  3. (Completed) **Wayland** Platform Backend for Linux (WSL2/Linux) with libdecor and xkbcommon.
  4. (Next) **Vulkan-HPP** renderer backend for Linux targets.
- **Architecture Preferences:**
  - **No Global State:** Prefer passing Context/Device handles.
  - **Error Handling:** Use the internal `result<T>` type for all failable operations.
  - **RAII:** Use `UniqueHandle` patterns; avoid manual `Release()` or `destroy()`.
  - **Platform Backend:** Native implementations (Win32, Wayland) over generic libraries like SDL3.
  - **Logging:** Unified `JAENG_LOG_*` system (OutputDebugStringA on Windows, stdout/stderr on Linux).
- **Shader Pipeline:**
  - Native Language: **HLSL**.
  - Target: DXIL (D3D12) and SPIR-V (Vulkan) via **DXC**.
  - Bindless Style: Use `ResourceDescriptorHeap[index]` syntax.
