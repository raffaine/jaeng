# Project Context: jaeng

- **Core Tech:** C++20 Game Engine (D3D12/Win32).
- **Roadmap:**
  1. Implement **Bindless Rendering** (Resource Descriptor Heaps / Descriptor Indexing).
  2. Integrate **SDL3** for Cross-Platform Windowing/Input.
  3. Add **Vulkan-HPP** backend specifically for **WSL2/Linux** targets.
- **Architecture Preferences:**
  - **No Global State:** Prefer passing Context/Device handles.
  - **Error Handling:** Use the internal `result<T>` type for all failable operations.
  - **RAII:** Use `UniqueHandle` patterns; avoid manual `Release()` or `destroy()`.
- **Shader Pipeline:**
  - Native Language: **HLSL**.
  - Target: DXIL (D3D12) and SPIR-V (Vulkan) via **DXC**.
  - Bindless Style: Use `ResourceDescriptorHeap[index]` syntax.
  