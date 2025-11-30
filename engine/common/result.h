
#pragma once
#include <expected>
#include <string>
#include <utility>
#include <format>
#include <windows.h>

namespace jaeng {

enum class error_code {
    unknown_error = 0,
    invalid_args,
    invalid_operation,
    no_resource,
    resource_not_ready
};

// -------------------- Error Type --------------------
struct Error {
    int code;
    std::string message;

    static Error fromMessage(int code, const std::string& msg) {
        return { code, msg };
    }

    static Error fromHRESULT(HRESULT hr) {
        char buffer[512];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, hr, 0, buffer, sizeof(buffer), nullptr);
        return { static_cast<int>(hr), std::string(buffer) };
    }

    static Error fromLastError() {
        DWORD err = GetLastError();
        char buffer[512];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, err, 0, buffer, sizeof(buffer), nullptr);
        return { static_cast<int>(err), std::string(buffer) };
    }
};

// -------------------- Result Type --------------------
template<typename T = void>
class [[nodiscard]] result {
public:
    // Delete copy constructor, allow move only
    result(const result&) = delete;
    result& operator=(const result&) = delete;

    result(result&& other) noexcept = default;
    result& operator=(result&& other) noexcept = default;

    // Constructors
    result(T value) : m_expected(std::move(value)) {}
    result(Error error) : m_expected(std::unexpected(std::move(error))) {}

    // Conversion constructor for error propagation
    template<typename U>
    result(result<U>&& other) requires (!std::is_same_v<T, U>) {
        if (auto err = std::move(other).logError(); !err.has_value()) {
            m_expected = std::unexpected(std::move(err.error()));
        } else {
            // To allow value conversion, handle here
            // For now, assume only error propagation
            OutputDebugStringA("Only supports Error Propagation");
        }
    }

    // State checks
    bool hasValue() const noexcept { return m_expected.has_value(); }
    bool hasError() const noexcept { return !m_expected.has_value(); }

    // r-value only methods
    T&& orValue(T&& defaultValue) && {
        if (auto e_ = std::move(*this).logError(); e_.has_value() ) {
            return std::move(e_.value());
        } else {
            return std::move(defaultValue);
        }
    }

    template<typename F>
    T&& orElse(F&& fallback) && {
        auto e_ = std::move(*this).logError(); 
        if (e_.has_value()) {
            return std::move(e_.value());
        } else {
            return std::move(fallback(std::move(e_.error())));
        }
    }

    // Log error and move out expected
    std::expected<T, Error> logError() && {
        if (!m_expected.has_value()) {
            // Replace with logging system
            OutputDebugStringA(std::format("Error [{}]: {}\n", m_expected.error().code, m_expected.error().message).c_str());
        }
        return std::move(m_expected);
    }

private:
    std::expected<T, Error> m_expected;
};

template<>
class [[nodiscard]] result<void> {
public:
    // Delete copy constructor, allow move only
    result(const result&) = delete;
    result& operator=(const result&) = delete;

    result(result&& other) noexcept = default;
    result& operator=(result&& other) noexcept = default;

    // Constructors
    result() : m_expected() {}
    result(Error error) : m_expected(std::unexpected(std::move(error))) {}

    // Conversion constructor for error propagation
    template<typename U>
    result(result<U>&& other) requires (!std::is_same_v<void, U>) {
        if (auto err = std::move(other).logError(); !err.has_value()) {
            m_expected = std::unexpected(std::move(err.error()));
        }
    }

    // State checks
    bool hasValue() const noexcept { return m_expected.has_value(); }
    bool hasError() const noexcept { return !m_expected.has_value(); }

    // r-value only methods
    template<typename F>
    void orElse(F&& fallback) && {
        if (auto e_ = std::move(*this).logError(); e_.has_value() ) {
            fallback(std::move(e_.error()));
        }
    }

    // Log error and move out expected
    std::expected<void, Error> logError() && {
        if (!m_expected.has_value()) {
            // Replace with logging system
            OutputDebugStringA(std::format("Error [{}]: {}\n", m_expected.error().code, m_expected.error().message).c_str());
        }
        return std::move(m_expected);
    }

private:
    std::expected<void, Error> m_expected;
};

// -------------------- Macros --------------------
#define JAENG_TRY_ASSIGN(decl, expr) \
    auto _tmp_##__COUNTER__ = (expr); \
    if (_tmp_##__COUNTER__.hasError()) return _tmp_##__COUNTER__; \
    decl = std::move(_tmp_##__COUNTER__).logError().value()

#define JAENG_TRY(expr) \
    { auto _tmp = (expr); if (_tmp.hasError()) return _tmp; }

// -------------------- Error Macros --------------------
#define JAENG_CHECK_HRESULT(hr) \
    if (FAILED(hr)) return jaeng::Error::fromHRESULT(hr)

#define JAENG_CHECK_LASTERROR(res) \
    if (!res) return jaeng::Error::fromLastError()

#define JAENG_CHECK_LASTERROR_(res) \
    if (res < 0) return jaeng::Error::fromLastError()

#define JAENG_ERROR_IF(predicate, code, msg) \
    if (predicate) return jaeng::Error::fromMessage(static_cast<int>(code), msg)
    
#define JAENG_ERROR(code, msg) \
    return jaeng::Error::fromMessage(static_cast<int>(code), msg)

} // jaeng namespace
