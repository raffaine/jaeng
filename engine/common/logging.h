#pragma once
#include <iostream>
#include <format>
#include <string_view>

#ifdef JAENG_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef JAENG_APPLE
#include <os/log.h>
#endif

#ifdef JAENG_ANDROID
#include <android/log.h>
#endif

namespace jaeng {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    None
};

#ifndef JAENG_LOG_LEVEL
#ifdef NDEBUG
#define JAENG_LOG_LEVEL jaeng::LogLevel::Warning
#else
#define JAENG_LOG_LEVEL jaeng::LogLevel::Debug
#endif
#endif

template<typename... Args>
void log(LogLevel level, std::string_view fmt, Args&&... args) {
    if (level < JAENG_LOG_LEVEL) return;

    std::string msg;
    try {
        if constexpr (sizeof...(Args) > 0) {
            msg = std::vformat(fmt, std::make_format_args(args...));
        } else {
            msg = std::string(fmt);
        }
    } catch (...) {
        msg = "Log format error";
    }

    std::string prefix;
    bool use_stderr = false;
    switch (level) {
        case LogLevel::Debug:   prefix = "[Debug] "; break;
        case LogLevel::Info:    prefix = "[Info]  "; break;
        case LogLevel::Warning: prefix = "[Warn]  "; use_stderr = true; break;
        case LogLevel::Error:   prefix = "[Error] "; use_stderr = true; break;
        default: break;
    }

    std::string final_msg = prefix + msg + "\n";

#ifdef JAENG_WIN32
    OutputDebugStringA(final_msg.c_str());
#elif defined(JAENG_APPLE)
    os_log_t log_obj = os_log_create("com.sintropia.jaeng", "engine");
    os_log_with_type(log_obj, OS_LOG_TYPE_DEFAULT, "%{public}s", final_msg.c_str());
#elif defined(JAENG_ANDROID)
    int prio = ANDROID_LOG_INFO;
    switch (level) {
        case LogLevel::Debug:   prio = ANDROID_LOG_DEBUG; break;
        case LogLevel::Info:    prio = ANDROID_LOG_INFO; break;
        case LogLevel::Warning: prio = ANDROID_LOG_WARN; break;
        case LogLevel::Error:   prio = ANDROID_LOG_ERROR; break;
        default: break;
    }
    __android_log_print(prio, "JAENG", "%s", final_msg.c_str());
#else
    if (use_stderr) {
        std::cerr << final_msg << std::flush;
    } else {
        std::cout << final_msg << std::flush;
    }
#endif
}

#define JAENG_LOG_DEBUG(...) jaeng::log(jaeng::LogLevel::Debug, __VA_ARGS__)
#define JAENG_LOG_INFO(...)  jaeng::log(jaeng::LogLevel::Info, __VA_ARGS__)
#define JAENG_LOG_WARN(...)  jaeng::log(jaeng::LogLevel::Warning, __VA_ARGS__)
#define JAENG_LOG_ERROR(...) jaeng::log(jaeng::LogLevel::Error, __VA_ARGS__)

} // namespace jaeng
