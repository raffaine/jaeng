#include "platform/public/platform_api.h"
#include <chrono>

namespace jaeng::platform {

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

            // Render the extracted state
            render(stateBuffer_.get_consumer(), hasNewState);
        }
    }

} // namespace jaeng::platform
