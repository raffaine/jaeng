#include "platform/public/platform_api.h"
#include "storage/win/filestorage.h"
#include "material/materialsys.h"
#include "mesh/meshsys.h"
#include "ui/fontsys.h"
#include <chrono>

namespace jaeng::platform {

    IApplication::IApplication(IPlatform& platform, const AppConfig& config)
        : platform_(platform), config_(config) {}

    bool IApplication::init() {
        platform_.set_event_callback([this](const Event& ev) { this->on_event(ev); });

        auto windowResult = platform_.create_window({config_.title, config_.width, config_.height});
        if (windowResult.hasError()) return false;
        window_ = std::move(windowResult).logError().value();

        if (!renderer_.initialize(config_.backend, window_->get_native_handle(), platform_.get_native_display_handle(), 3)) return false;

        DepthStencilDesc depthDesc{.depth_enable = true, .depth_format = TextureFormat::D32F};
        SwapchainDesc swapDesc{{config_.width, config_.height}, TextureFormat::BGRA8_UNORM, depthDesc, PresentMode::Fifo};
        swap_ = renderer_->create_swapchain(&swapDesc);
        if (swap_ == 0) return false;

        
        fileMan_ = std::make_shared<FileManager>();
        fileMan_->initialize().orElse([this](auto) {
            platform_.show_message_box("Warning", "Failed to initialize FileManager. Continuing with limited capacity.", MessageBoxType::Warning);
        });
        entityMan_ = std::make_shared<EntityManager>();
        fontSys_ = std::make_shared<FontSystem>(fileMan_, renderer_.gfx);
        matSys_  = std::make_shared<MaterialSystem>(fileMan_, renderer_.gfx);
        meshSys_ = std::make_shared<MeshSystem>(fileMan_, renderer_.gfx);
        sceneMan_ = std::make_unique<SceneManager>(meshSys_, matSys_, renderer_.gfx);

        return app_init(); // Delegate to user app
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
        isRunning_ = true;
        simThread_ = std::jthread(&IApplication::simulation_loop, this);
        renderThread_ = std::jthread(&IApplication::render_loop, this);
    }

    void IApplication::stop_engine_threads() {
        isRunning_ = false;
        renderCv_.notify_one(); // Wake up render thread if it's waiting
        // std::jthread automatically joins on destruction, but we can be explicit
        if (simThread_.joinable()) simThread_.join();
        if (renderThread_.joinable()) renderThread_.join();
    }

    void IApplication::simulation_loop() {
        auto lastTime = std::chrono::high_resolution_clock::now();
        float accumulator = 0.0f;

        while (isRunning_) {
            auto now = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;

            if (dt > 0.25f) dt = 0.25f;
            accumulator += dt;

            bool stateChanged = false;

            // Run deterministic simulation steps
            while (accumulator >= fixedDt_) {
                tick(fixedDt_);
                accumulator -= fixedDt_;
                stateChanged = true;
            }

            // If the state changed, extract it and hand it off to the render thread
            if (stateChanged) {
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
                // Yield to avoid pegging a CPU core at 100% when no updates occur
                std::this_thread::yield();
            }
        }
    }

    void IApplication::render_loop() {
        while (isRunning_) {
            // Wait for the simulation thread to produce a new frame packet
            std::unique_lock<std::mutex> lock(stateMutex_);
            renderCv_.wait(lock, [this]() { return frameReady_.load() || !isRunning_; });

            if (!isRunning_) break;

            // Reset the flag immediately so Sim can start working on the next frame
            frameReady_ = false;
            lock.unlock();

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
        }
    }

} // namespace jaeng::platform
