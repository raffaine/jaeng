#include "platform/public/platform_api.h"
#include "material/materialsys.h"
#include "mesh/meshsys.h"
#include "ui/fontsys.h"
#include "common/async/awaiters.h"
#include <chrono>
#include <thread>

#ifdef JAENG_APPLE
#include <Foundation/Foundation.hpp>
#include <TargetConditionals.h>
#endif

namespace jaeng::platform {

    IApplication::IApplication(IPlatform& platform, const AppConfig& config)
        : platform_(platform), config_(config) {
        taskScheduler_ = std::make_unique<async::TaskScheduler>();
    }

    bool IApplication::init(void* device_handle) {
        JAENG_LOG_INFO("[Engine] init: Setting up scheduler...");
        async::set_current_scheduler(taskScheduler_.get());

        platform_.set_event_callback([this](const Event& ev) { this->on_event(ev); });

        JAENG_LOG_INFO("[Engine] init: Creating window...");
        auto windowResult = platform_.create_window({config_.title, config_.width, config_.height});
        if (windowResult.hasError()) return false;
        window_ = std::move(windowResult).logError().value();

        config_.width = window_->get_width();
        config_.height = window_->get_height();
        JAENG_LOG_INFO("[Engine] init: Window created {}x{}", config_.width, config_.height);

        JAENG_LOG_INFO("[Engine] init: Initializing renderer...");
        if (!renderer_.initialize(config_.backend, window_->get_native_handle(), platform_.get_native_display_handle(), 3, device_handle)) return false;

        JAENG_LOG_INFO("[Engine] init: Creating swapchain...");
        DepthStencilDesc depthDesc{.depth_enable = true, .depth_format = TextureFormat::D32F};
        SwapchainDesc swapDesc{{config_.width, config_.height}, TextureFormat::BGRA8_UNORM, depthDesc, PresentMode::Fifo};
        swap_ = renderer_->create_swapchain(&swapDesc);
        if (swap_ == 0) return false;

        JAENG_LOG_INFO("[Engine] init: Initializing subsystems...");
        auto& fileMan = platform_.get_file_manager();
        fileMan.initialize().orElse([this](auto) {
            platform_.show_message_box("Warning", "Failed to initialize FileManager.", MessageBoxType::Warning);
        });
        entityMan_ = std::make_shared<EntityManager>();
        fontSys_ = std::make_shared<FontSystem>(fileMan, renderer_.gfx);
        matSys_  = std::make_shared<MaterialSystem>(fileMan, renderer_.gfx);
        meshSys_ = std::make_shared<MeshSystem>(fileMan, renderer_.gfx);
        sceneMan_ = std::make_unique<SceneManager>(meshSys_, matSys_, renderer_.gfx);

        JAENG_LOG_INFO("[Engine] init: Calling app_init...");
        bool ok = app_init();
        JAENG_LOG_INFO("[Engine] init: app_init returned {}, fixedDt={}", ok, fixedDt_);
        
        if (ok) {
            // Start threads only after resources are loaded
            start_engine_threads();
        }

        return ok;
    }

    void IApplication::on_event(const Event& ev) {
        if (ev.type == Event::Type::WindowResize) {
            if (swap_ > 0 && renderer_.gfx) {
                renderer_.queue_resize(swap_, ev.resize.width, ev.resize.height);
            }
        } else if (ev.type == Event::Type::WindowClose) {
            shouldClose_ = true;
        }
        app_on_event(ev); // Delegate to user app
    }

    void IApplication::shutdown() {
        app_shutdown(); // Delegate to user app
        
        sceneMan_.reset();
        meshSys_.reset();
        matSys_.reset();
        fontSys_.reset();
        entityMan_.reset();
        renderer_.shutdown();
        if (window_) window_->destroy();
    }

    void IApplication::set_tick_rate(uint32_t hz) {
        if (hz > 0) {
            fixedDt_ = 1.0f / static_cast<float>(hz);
        }
    }

    void IApplication::start_engine_threads() {
        if (isRunning_) return;
        isRunning_ = true;
        if (taskScheduler_) {
            uint32_t workerCount = std::thread::hardware_concurrency();
#if TARGET_OS_SIMULATOR
            if (workerCount > 4) workerCount = 4;
#endif
            taskScheduler_->initialize(workerCount);
            JAENG_LOG_INFO("[Engine] Starting engine loops via std::thread ({} workers)", workerCount);
        } else {
            JAENG_LOG_INFO("[Engine] Starting engine loops via std::thread (no scheduler)");
        }

        simThread_ = std::thread(&IApplication::simulation_loop, this);
        renderThread_ = std::thread(&IApplication::render_loop, this);
    }

    void IApplication::stop_engine_threads() {
        isRunning_ = false;
        async::set_current_scheduler(nullptr);
        if (taskScheduler_) {
            taskScheduler_->shutdown();
        }
        renderCv_.notify_all(); // Wake up render thread if it's waiting

        if (simThread_.joinable()) simThread_.join();
        if (renderThread_.joinable()) renderThread_.join();
    }

    bool IApplication::process_main_thread_tasks() {
        return taskScheduler_ && taskScheduler_->process_main_thread_tasks();
    }

    IFileManager& IApplication::fileManager() {
        return platform_.get_file_manager();
    }

    void IApplication::run_one_frame() {
        tick(fixedDt_);
        JAENG_LOG_DEBUG("[Engine] run_one_frame executed");
        auto& producerQueue = stateBuffer_.get_producer();
        extract_render_state(producerQueue);
        stateBuffer_.push_producer();
        stateBuffer_.update_consumer();
        
        renderer_.process_pending_resizes();
        if (renderer_->begin_frame()) {
            TextureHandle backbuffer = renderer_->get_current_backbuffer(swap_);
            TextureHandle depthbuffer = renderer_->get_depth_buffer(swap_);
            RenderGraph graph;
            render(stateBuffer_.get_consumer(), true, graph, backbuffer, depthbuffer);
            graph.compile();
            graph.execute(*renderer_.gfx, depthbuffer, nullptr);
            renderer_->present(swap_);
            renderer_->end_frame();
        }
    }

    void IApplication::simulation_loop() {
#ifdef JAENG_APPLE
        async::set_current_scheduler(taskScheduler_.get());
#endif

        auto lastTime = std::chrono::high_resolution_clock::now();
        float accumulator = 0.0f;
        JAENG_LOG_INFO("[Engine] Simulation loop started");

        while (isRunning_) {
#ifdef JAENG_APPLE
            auto* pool = NS::AutoreleasePool::alloc()->init();
#endif

            auto now = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;

            if (dt > 0.25f) dt = 0.25f;
            accumulator += dt;

            static uint32_t traceCount = 0;
            if (traceCount++ % 120 == 0) {
                JAENG_LOG_DEBUG("[Engine] Sim Loop Trace: dt={}, acc={}, fixedDt={}", dt, accumulator, fixedDt_);
            }

            bool stateChanged = false;

            // Run deterministic simulation steps
            while (accumulator >= fixedDt_) {
                tick(fixedDt_);
                accumulator -= fixedDt_;
                stateChanged = true;
            }

            // If the state changed, extract it and hand it off to the render thread
            if (stateChanged) {
                static uint32_t simFrameCount = 0;
                if (simFrameCount++ % 60 == 0) {
                    JAENG_LOG_DEBUG("[Engine] Sim frame produced, notifying render thread");
                }
                
                auto& producerQueue = stateBuffer_.get_producer();
                extract_render_state(producerQueue);
                stateBuffer_.push_producer();

                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    frameReady_ = true;
                }
                renderCv_.notify_one();
            }
            else {
                // Prevent aggressive spinning on some platforms (like iOS Simulator)
                jaeng::platform::thread::yield();
            }

#ifdef JAENG_APPLE
            pool->release();
#endif
        }

        JAENG_LOG_INFO("[Engine] Simulation loop stopped");
    }

    void IApplication::render_loop() {
#ifdef JAENG_APPLE
        async::set_current_scheduler(taskScheduler_.get());
#endif

        JAENG_LOG_INFO("[Engine] Render loop started");
        while (isRunning_) {
            // Wait for the simulation thread to produce a new frame packet
            std::unique_lock<std::mutex> lock(stateMutex_);
            renderCv_.wait(lock, [this]() { return frameReady_.load() || !isRunning_; });

            if (!isRunning_) break;

            // Reset the flag immediately so Sim can start working on the next frame
            frameReady_ = false;
            lock.unlock();

#ifdef JAENG_APPLE
            auto* pool = NS::AutoreleasePool::alloc()->init();
#endif

            // Check if we are in foreground before rendering (essential for iOS stability)
            if (platform_.is_foreground()) {
                // Check the triple buffer for new data
                bool hasNewState = stateBuffer_.update_consumer();

                // Engine strictly owns the resize, compile, and present steps!
                renderer_.process_pending_resizes();
                if (renderer_->begin_frame()) {
                    TextureHandle backbuffer = renderer_->get_current_backbuffer(swap_);
                    TextureHandle depthbuffer = renderer_->get_depth_buffer(swap_);

                    RenderGraph graph;

                    // Render the extracted state
                    render(stateBuffer_.get_consumer(), hasNewState, graph, backbuffer, depthbuffer);

                    graph.compile();
                    graph.execute(*renderer_.gfx, depthbuffer, nullptr);
                    renderer_->present(swap_);
                    renderer_->end_frame();
                }
            } else {
                // Throttle while in background
                jaeng::platform::thread::sleep_idle();
            }

#ifdef JAENG_APPLE
            pool->release();
#endif
        }

        JAENG_LOG_INFO("[Engine] Render loop stopped");
    }

} // namespace jaeng::platform
