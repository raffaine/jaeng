#pragma once
#include "renderer_api.h"

#if defined(_WIN32)
#include <windows.h>
#include <memory>

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
struct RendererPlugin { bool load(const wchar_t*) { return false; } void unload() {} RendererAPI api{}; };
#endif