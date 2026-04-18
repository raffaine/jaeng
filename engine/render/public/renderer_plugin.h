#pragma once
#include "renderer_api.h"
#include <memory>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#include <string>
#include <cstdlib>
#endif

namespace jaeng {

#if defined(_WIN32)
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
struct RendererPlugin {
    void* lib = nullptr;
    std::shared_ptr<RendererAPI> api;

    bool load(const char* dll_path) {
        lib = dlopen(dll_path, RTLD_NOW | RTLD_GLOBAL);
        if (!lib) {
            const char* err = dlerror();
            JAENG_LOG_ERROR("dlopen failed: {}", err ? err : "Unknown error");
            return false;
        }
        auto loadFn = (PFN_LoadRenderer)dlsym(lib, "LoadRenderer");
        if (!loadFn) {
            const char* err = dlerror();
            JAENG_LOG_ERROR("dlsym failed for LoadRenderer: {}", err ? err : "Symbol not found");
            return false;
        }
        api = std::make_shared<RendererAPI>();
        return loadFn(api.get());
    }
    bool load(const wchar_t* dll_path) {
        size_t len = std::wcstombs(nullptr, dll_path, 0);
        if (len == static_cast<size_t>(-1)) return false;
        std::string path(len, '\0');
        std::wcstombs(path.data(), dll_path, len);
        return load(path.c_str());
    }
    void unload() {
        if (lib) { dlclose(lib); lib = nullptr; }
        api.reset();
    }
};
#endif

} // namespace jaeng
