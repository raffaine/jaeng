// NEW FILE: engine/common/triple_buffer.h
#pragma once

#include <array>
#include <atomic>

namespace jaeng {

template <typename T>
class TripleBuffer {
public:
    TripleBuffer() {
        producer_ = &buffers_[0];
        consumer_ = &buffers_[1];
        shared_.store(&buffers_[2], std::memory_order_relaxed);
    }

    // Sim Thread gets the current buffer to fill
    T& get_producer() { return *producer_; }

    // Sim Thread swaps the filled buffer into the shared slot
    void push_producer() {
        producer_ = shared_.exchange(producer_, std::memory_order_acq_rel);
        new_data_.store(true, std::memory_order_release);
    }

    // Render Thread attempts to grab a new buffer. Returns true if swapped.
    bool update_consumer() {
        if (new_data_.exchange(false, std::memory_order_acquire)) {
            consumer_ = shared_.exchange(consumer_, std::memory_order_acq_rel);
            return true;
        }
        return false;
    }

    // Render Thread reads the safe, isolated buffer
    const T& get_consumer() const { return *consumer_; }

private:
    std::array<T, 3> buffers_;
    T* producer_;
    T* consumer_;
    std::atomic<T*> shared_;
    std::atomic<bool> new_data_{false};
};

} // namespace jaeng
