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
enum class ShaderStage : uint32_t { Vertex=1, Fragment=2, Compute=4 };
enum class PrimitiveTopology : uint32_t { TriangleList=0, TriangleStrip=1, LineList=2 };
enum class SamplerFilter : uint32_t { Nearest = 0, Linear = 1 };
enum class AddressMode:uint32_t { Repeat = 0, ClampToEdge = 1, Mirror = 2, Border = 3 };

struct Extent2D { uint32_t width, height; };

// --- Descriptors ---
struct RendererDesc {
    GfxBackend backend;
    void* platform_window; // HWND for Win32
    uint32_t frame_count;  // 2 or 3
};

struct DepthStencilDesc {
    bool depth_enable;
    bool stencil_enable;
    TextureFormat depth_format;
};

struct SwapchainDesc {
    Extent2D size;
    TextureFormat format;
    DepthStencilDesc depth_stencil;
    PresentMode present_mode;
};

// --- New: buffers, shaders, pipelines ---
enum BufferUsage : uint32_t {
    BufferUsage_Vertex  = 1 << 0,
    BufferUsage_Index   = 1 << 1,
    BufferUsage_Uniform = 1 << 2,
    BufferUsage_Upload  = 1 << 3
};

struct BufferDesc {
    uint64_t size_bytes;
    uint32_t usage;
};

// Textures & samplers (Step 4)
struct TextureDesc {
    TextureFormat format;
    uint32_t      width, height;
    uint32_t      mip_levels; // use 1 for now
    uint32_t      layers;     // use 1 for now
    uint32_t      usage;      // reserved for future (rt, sample, storage)
};

struct SamplerDesc {
    SamplerFilter filter;
    AddressMode   address_u, address_v, address_w;
    float         mip_lod_bias;
    float         min_lod;
    float         max_lod;
    float         border_color[4]; // used if AddressMode::Border
};

// 0 = D3D blob (DXBC/DXIL) for this sample
struct ShaderModuleDesc {
  ShaderStage stage;
  const void* data;
  uint32_t    size;
  uint32_t    format; // 0 = D3D blob
};

struct VertexAttributeDesc {
  uint32_t location;  // 0=POSITION, 1=COLOR in sample
  uint32_t format;    // 0 = R32G32B32_FLOAT
  uint32_t offset;    // byte offset
};
struct VertexLayoutDesc {
  uint32_t stride;
  const VertexAttributeDesc* attributes;
  uint32_t attribute_count;
};

struct DepthStencilOptions {
    bool enableDepth = false;
    bool enableStencil = false;
    float clearDepth = 1.0f;
    uint8_t clearStencil = 0;
    enum class DepthFunc { Less, LessEqual, Greater, Always } depthFunc = DepthFunc::Less;
    // Future: stencil ops, masks, etc    // Future: stencil ops, masks, etc.
};

struct GraphicsPipelineDesc {
  ShaderModuleHandle vs;
  ShaderModuleHandle fs; // optional
  PrimitiveTopology topology;
  VertexLayoutDesc   vertex_layout;
  TextureFormat      color_format; // single RT
  DepthStencilOptions depth_stencil;
};

enum class BindGroupEntryType {
    UniformBuffer = 0,
    Texture = 1,
    Sampler = 2,
};

// Bind groups
struct BindGroupLayoutEntry {
    uint32_t binding; // 0..3
    BindGroupEntryType type;
    uint32_t stages;  // ShaderStage bitmask
};

struct BindGroupLayoutDesc {
    const BindGroupLayoutEntry* entries;
    uint32_t                    entry_count;
};

struct BindGroupEntry {
    BindGroupEntryType type;
    BufferHandle  buffer;
    uint64_t      offset;
    uint64_t      size;   // for CBV/SSBO (reserved)
    TextureHandle texture;
    SamplerHandle sampler;
};

struct BindGroupDesc {
    BindGroupLayoutHandle layout;
    const BindGroupEntry* entries;
    uint32_t              entry_count;
};

// Used on Reflection Generated Headers
struct PipelineReflectionResources {
    ShaderModuleHandle vs;
    ShaderModuleHandle fs;
    PipelineHandle pipeline;

    BufferHandle uniformBuffer;
    TextureHandle texture;
    SamplerHandle sampler;

    BindGroupLayoutHandle bindGroupLayout;
    BindGroupHandle bindGroup;

    // Optional: offsets for uniform data, descriptor indices, etc.
};

enum class LoadOp : uint32_t { Load, Clear, DontCare };
struct ColorAttachmentDesc {
    TextureHandle tex;
    float clear_rgba[4];
};
struct DepthAttachmentDesc {
    float clear_d;
    /* stencil optional */
};

// --- Renderer function table ---
typedef struct RendererAPI {
    // frame lifecycle
    // Call once per frame before encoding commands; the backend ensures the previous GPU work
    // for this frame index is complete and resets per-frame transient state (upload ring, etc.).
    void (*begin_frame)();
    // Call once after presenting (or at the end of command submission) to mark end-of-frame.
    void (*end_frame)();

    // lifecycle
    bool (*init)(const RendererDesc*);
    void (*shutdown)();

    // swapchain
    SwapchainHandle (*create_swapchain)(const SwapchainDesc*);
    void (*resize_swapchain)(SwapchainHandle, Extent2D);
    void (*destroy_swapchain)(SwapchainHandle);
    TextureHandle (*get_current_backbuffer)(SwapchainHandle); // helper for sample
     
    // resources
    BufferHandle (*create_buffer)(const BufferDesc*, const void* initial_data);
    void         (*destroy_buffer)(BufferHandle);
    // Queue a CPU->GPU update into a DEFAULT-heap buffer. Data is staged into a per-frame
    // persistently-mapped upload ring and copied with CopyBufferRegion in the current cmd list.
    // Returns false if the request cannot be satisfied (e.g., ring overflow).
    bool         (*update_buffer)(BufferHandle, uint64_t dst_offset, const void* data, uint64_t size);

    // Step 4: textures & samplers
    TextureHandle (*create_texture)(const TextureDesc*, const void* initial_data);
    void (*destroy_texture)(TextureHandle);
    SamplerHandle (*create_sampler)(const SamplerDesc*);
    void (*destroy_sampler)(SamplerHandle);

    ShaderModuleHandle (*create_shader_module)(const ShaderModuleDesc*);
    void              (*destroy_shader_module)(ShaderModuleHandle);
    PipelineHandle (*create_graphics_pipeline)(const GraphicsPipelineDesc*);
    void           (*destroy_pipeline)(PipelineHandle);

    // Step 4: bind groups
    BindGroupLayoutHandle (*create_bind_group_layout)(const BindGroupLayoutDesc*);
    void (*destroy_bind_group_layout)(BindGroupLayoutHandle);
    BindGroupHandle (*create_bind_group)(const BindGroupDesc*);
    void (*destroy_bind_group)(BindGroupHandle);

    // command encoding (subset sufficient to clear)
    CommandListHandle (*begin_commands)();
    void (*cmd_begin_rendering_ops)(CommandListHandle, LoadOp load_op, const ColorAttachmentDesc* colors, uint32_t count, const DepthAttachmentDesc* depth);
    void (*cmd_end_rendering)(CommandListHandle);
    void (*cmd_set_bind_group)(CommandListHandle, uint32_t set_index, BindGroupHandle);
    void (*cmd_set_pipeline)(CommandListHandle, PipelineHandle);
    void (*cmd_set_vertex_buffer)(CommandListHandle, uint32_t slot, BufferHandle, uint64_t offset);
    void (*cmd_set_index_buffer)(CommandListHandle, BufferHandle, bool index32, uint64_t offset);
    void (*cmd_draw)(CommandListHandle, uint32_t vtx_count, uint32_t instance_count, uint32_t first_vtx, uint32_t first_instance);
    void (*cmd_draw_indexed)(CommandListHandle, uint32_t idx_count, uint32_t instance_count, uint32_t first_idx, uint32_t vtx_offset, uint32_t first_instance);
    void (*end_commands)(CommandListHandle);

    // submit/present
    void (*submit)(CommandListHandle* lists, uint32_t list_count);
    void (*present)(SwapchainHandle);
    void (*wait_idle)();
} RendererAPI;

// Plugin factory symbol name: "LoadRenderer"
typedef bool (*PFN_LoadRenderer)(RendererAPI* out_api);

} // extern "C"
