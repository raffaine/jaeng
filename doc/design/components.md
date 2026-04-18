# Subsystems (Components)

## Entity Component System (ECS)
JAENG utilizes a high-performance **Sparse-Dense Array** (SoA-style) ECS for data locality.
*   **EntityManager:** The central hub. Manages `EntityID` (uint32) allocation and type-erased `ComponentPool` instances.
*   **ComponentPool<T>:** Implements sparse sets to keep components packed contiguously in memory, maximizing CPU cache hits during system updates.
*   **Built-in Components:** `Transform`, `WorldMatrix`, `Relationship` (Hierarchy), `MeshComponent`, `MaterialComponent`.

## Scene Subsystem
Organizes entities into a renderable world.
*   **Scene:** Aggregates an `ISpatialPartitioner` and an `ICamera`.
*   **GridPartitioner:** A 3D grid-based implementation of spatial partitioning for efficient frustum culling and selection.
*   **SceneRenderSystem:** The extraction bridge. It traverses the scene's spatial structure and converts ECS data into `RenderProxy` objects for the Triple Buffer.

## Rendering Architecture
The rendering pipeline is strictly divided into a command-building Frontend and a stateless Backend.
*   **Renderer Frontend:**
    *   `Renderer`: Handles cross-platform dynamic loading of backend `.dll` / `.dylib` files. Provides a default **Null/Mock** implementation for stability.
    *   `RenderGraph`: A high-level orchestration layer where the application defines `RGPass` objects. It manages render target clearing and pass sequencing.
*   **Renderer API (Backend):** 
    *   A stateless C-style function table (`RendererAPI`).
    *   Plugins (Vulkan, D3D12, Metal) implement this table to translate generic engine commands into API-specific primitives.

## Async & Asset Subsystem
Built on C++20 Coroutines for non-blocking I/O and compute.
*   **TaskScheduler:** A worker thread pool with specialized queues.
    *   **Worker Queues:** For MPMC (Multi-Producer Multi-Consumer) background compute.
    *   **Main Mailbox:** An MPSC (Multi-Producer Single-Consumer) queue for tasks that must run on the OS thread.
*   **FileManager:** Integrated with the TaskScheduler. Supports asynchronous loading (`Task<T>`) and file-system tracking for hot-reloading.
*   **Future<T>:** A lightweight, fluent alternative to coroutines for task chaining via `.then()` and `.thenSync()`.

## UI Subsystem
A modular system for interactive elements.
*   **FontSystem:** Uses `stb_truetype` to bake glyphs into atlas textures managed by the renderer.
*   **UIBuilder:** A fluent API for composing UI hierarchies with anchors, pivots, and auto-layout.
*   **UISystems:** 
    *   `UILayoutSystem`: Computes geometry.
    *   `UIInteractionSystem`: Processes mouse/touch intersection and callbacks.
    *   `UIRenderSystem`: Extracts UI geometry into the render queue.
