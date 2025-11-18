#pragma once
#include <cstdint>
#include "render/public/renderer_plugin.h"

class Renderer {
public:
    bool initialize(GfxBackend backend, void* hwnd, uint32_t frame_count = 3) {
        // For now, always load the D3D12 plugin. Later: choose based on backend.
        if (!plugin.load(L"renderer_d3d12.dll")) return false;
        gfx = plugin.api;
        RendererDesc d{ backend, hwnd, frame_count };
        return gfx.init(&d);
    }

    void shutdown() {
        if (gfx.wait_idle) gfx.wait_idle();
        if (gfx.shutdown) gfx.shutdown();
        plugin.unload();
    }

    RendererAPI gfx{};
    
private:
    RendererPlugin plugin;
};