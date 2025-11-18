#pragma once
#include "renderer_api.h"

#if defined(_WIN32)
#include <windows.h>

struct RendererPlugin {
    HMODULE lib = nullptr;
    RendererAPI api{};

    bool load(const wchar_t* dll_path) {
        lib = ::LoadLibraryW(dll_path);
        if (!lib) return false;
        auto loadFn = (PFN_LoadRenderer)::GetProcAddress(lib, "LoadRenderer");
        if (!loadFn) return false;
        return loadFn(&api);
    }
    void unload() {
        if (lib) { ::FreeLibrary(lib); lib = nullptr; }
        api = {};
    }
};
#else
struct RendererPlugin { bool load(const wchar_t*) { return false; } void unload() {} RendererAPI api{}; };
#endif