#pragma once
#include "renderer_api.h"
#include <memory>

namespace jaeng {

#if defined(_WIN32)
#include <windows.h>

struct RendererPlugin {
    HMODULE lib = nullptr;
    std::shared_ptr<RendererAPI> api;

    bool load(const wchar_t* dll_path) {
        lib = ::LoadLibraryW(dll_path);
        if (!lib) return false;
        auto loadFn = (PFN_LoadRenderer)::GetProcAddress(lib, "LoadRenderer");
        if (!loadFn) return false;
        api = std::make_shared<RendererAPI>();
        return loadFn(api.get());
    }
    void unload() {
        if (lib) { ::FreeLibrary(lib); lib = nullptr; }
        api.reset();
    }
};
#else
#include <dlfcn.h>
#include <string>
#include <codecvt>
#include <locale>

struct RendererPlugin {
    void* lib = nullptr;
    std::shared_ptr<RendererAPI> api;

    bool load(const char* dll_path) {
        lib = dlopen(dll_path, RTLD_NOW | RTLD_GLOBAL);
        if (!lib) {
            JAENG_LOG_ERROR("dlopen failed: {}", dlerror());
            return false;
        }
        auto loadFn = (PFN_LoadRenderer)dlsym(lib, "LoadRenderer");
        if (!loadFn) return false;
        api = std::make_shared<RendererAPI>();
        return loadFn(api.get());
    }
    bool load(const wchar_t* dll_path) {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return load(converter.to_bytes(dll_path).c_str());
    }
    void unload() {
        if (lib) { dlclose(lib); lib = nullptr; }
        api.reset();
    }
};
#endif

} // namespace jaeng
