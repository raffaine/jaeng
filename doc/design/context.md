# System Context

## Overview
JAENG is a high-performance, cross-platform C++20 game engine designed with a focus on strict thread separation, lock-free concurrency, and data locality. It provides a foundational layer for building interactive 3D applications on Windows, Linux (Wayland), and Apple (macOS/iOS) platforms.

## System Context Diagram
The following describes how JAENG interacts with external entities:

| User/System | Interaction |
| :--- | :--- |
| **Developer** | Writes application logic by inheriting from `IApplication` and interacting with Engine Subsystems (ECS, Scene, UI). |
| **Operating System** | JAENG uses native OS APIs (Win32, AppKit, UIKit, Wayland) for window management, input, and process spawning. |
| **Graphics API** | JAENG leverages high-performance graphics backends (D3D12, Vulkan, Metal) through a unified Renderer API. |
| **Asset Files** | The engine loads resources (Shaders, Textures, Meshes, Fonts) from the local filesystem via the `FileManager`. |

## Core Goals
1.  **Strict Threading:** Maintain a deterministic separation between OS/Main, Simulation, and Render threads.
2.  **Native Performance:** Favor native platform implementations and modern Graphics APIs over generic abstraction layers.
3.  **Stability:** Utilize strong typing (`jaeng::result<T>`) and RAII patterns for error handling and resource management.
