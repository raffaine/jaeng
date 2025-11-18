#pragma once
#include <stdint.h>

#if defined(_WIN32)
#ifdef RENDERER_BUILD
#define RENDERER_API __declspec(dllexport)
#else
#define RENDERER_API __declspec(dllimport)
#endif
#else
#define RENDERER_API
#endif

extern "C" {

// --- Opaque handles ---
typedef uint32_t RendererHandle;  // generic id
typedef RendererHandle BufferHandle;
typedef RendererHandle TextureHandle;
typedef RendererHandle SamplerHandle;
typedef RendererHandle ShaderModuleHandle;
typedef RendererHandle BindGroupLayoutHandle;
typedef RendererHandle BindGroupHandle;
typedef RendererHandle PipelineHandle;
typedef RendererHandle SwapchainHandle;
typedef RendererHandle CommandListHandle;

// --- Enums ---
enum class GfxBackend : uint32_t { D3D12=0, Vulkan=1, OpenGL=2 };

enum class TextureFormat : uint32_t { RGBA8_UNORM=0, BGRA8_UNORM=1, D24S8=2, D32F=3 };

enum class PresentMode : uint32_t { Fifo=0, Mailbox=1, Immediate=2 };

struct Extent2D { uint32_t width, height; };

// --- Descriptors ---
struct RendererDesc {
    GfxBackend backend;
    void* platform_window; // HWND for Win32
    uint32_t frame_count;  // 2 or 3
};

struct SwapchainDesc {
    Extent2D size;
    TextureFormat format;
    PresentMode present_mode;
};

// (Other resource descriptors omitted for brevity in this starter)

// --- Renderer function table ---
typedef struct RendererAPI {
    // lifecycle
    bool (*init)(const RendererDesc*);
    void (*shutdown)();

    // swapchain
    SwapchainHandle (*create_swapchain)(const SwapchainDesc*);
    void (*resize_swapchain)(SwapchainHandle, Extent2D);
    void (*destroy_swapchain)(SwapchainHandle);
    TextureHandle (*get_current_backbuffer)(SwapchainHandle); // helper for sample

    // command encoding (subset sufficient to clear)
    CommandListHandle (*begin_commands)();
    void (*cmd_begin_rendering)(CommandListHandle, TextureHandle* color_rt, uint32_t rt_count, float clear_rgba[4]);
    void (*cmd_end_rendering)(CommandListHandle);
    void (*end_commands)(CommandListHandle);

    // submit/present
    void (*submit)(CommandListHandle* lists, uint32_t list_count);
    void (*present)(SwapchainHandle);
    void (*wait_idle)();
} RendererAPI;

// Plugin factory symbol name: "LoadRenderer"
typedef bool (*PFN_LoadRenderer)(RendererAPI* out_api);

} // extern "C"