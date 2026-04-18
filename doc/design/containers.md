# Containers & Thread Model

## The Three-Thread Model
JAENG uses a strictly decoupled three-thread architecture to maximize CPU utilization and eliminate stalls between gameplay logic and frame presentation.

### 1. Main / OS Thread
*   **Role:** The entry point and "Platform Host".
*   **Ownership:** Native Window Handle, OS Event Pump, and the `TaskScheduler`'s main mailbox.
*   **Execution:**
    *   Polls native OS events (Win32 messages, AppKit events, etc.).
    *   Processes **Main Thread Tasks**: Interleaves OS polling with synchronous engine tasks (e.g., resource destruction, window resizing) using `taskScheduler_.process_main_thread_tasks()`.
*   **Blocking:** Non-blocking. Uses a small sleep/yield if no OS events or mailbox tasks are pending.

### 2. Simulation Thread
*   **Role:** The "Heart" of the application logic.
*   **Ownership:** `EntityManager` (ECS), `SceneManager`, `MaterialSystem`, and `AnimationSystem`.
*   **Execution Loop:** 
    *   Uses a **Fixed Timestep Accumulator** to ensure deterministic physics and logic updates (`tick()`).
    *   Performs **State Extraction**: At the end of a logic burst, it gathers all renderable data (Transforms, Mesh/Material handles, UI) into a `std::vector<RenderCommand>`.
    *   Swaps extracted state into the `Triple Buffer`.
*   **Synchronization:** Never blocks on the Render thread. Signals the Render thread via a `std::condition_variable` when a new frame packet is pushed.

### 3. Render Thread
*   **Role:** The "Visualizer".
*   **Ownership:** `Renderer` (Frontend), `RendererPlugin` (Backend), and `RenderGraph`.
*   **Execution Loop:**
    *   Waits on `renderCv_` for a frame packet from Simulation.
    *   **Consumption:** Updates its internal view from the `Triple Buffer` using `update_consumer()`.
    *   **Frame Build:** Executes `render()` to populate a transient `RenderGraph` based on the received commands.
    *   **Submission:** Compiles and executes the graph, translating high-level passes into low-level `RendererAPI` commands.
*   **Blocking:** Blocks on the condition variable until the Simulation thread produces data.

## Data Synchronization: The Triple Buffer
The `TripleBuffer<T>` is the core lock-free bridge between Simulation and Render. 
*   **Producer Slot:** Simulation writes the latest extracted state here.
*   **Shared Slot:** Acts as a hand-off point.
*   **Consumer Slot:** Render thread reads from a stable, isolated copy of the state.
*   **Mechanism:** Uses `std::atomic::exchange` with `acq_rel` semantics to swap pointers between slots without mutexes in the hot path.
