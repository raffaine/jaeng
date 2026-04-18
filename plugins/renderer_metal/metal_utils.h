#pragma once

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <utility>

namespace jaeng::renderer::metal {

/**
 * @brief RAII wrapper for metal-cpp (NS::Object based) types.
 * Manages reference counting via retain/release.
 */
template <typename T>
class UniqueHandle {
public:
    UniqueHandle() : ptr_(nullptr) {}
    explicit UniqueHandle(T* ptr) : ptr_(ptr) {}
    
    ~UniqueHandle() {
        if (ptr_) {
            ptr_->release();
        }
    }

    // Disable copy for true uniqueness, or implement retain if shared ownership is needed.
    // Given the engine's "UniqueHandle" naming convention, we'll favor uniqueness (move-only).
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            if (ptr_) ptr_->release();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    T* release() {
        T* temp = ptr_;
        ptr_ = nullptr;
        return temp;
    }

    void reset(T* ptr = nullptr) {
        if (ptr_) ptr_->release();
        ptr_ = ptr;
    }

private:
    T* ptr_;
};

} // namespace jaeng::renderer::metal
