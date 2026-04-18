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
typedef RendererHandle VertexLayoutHandle;
typedef RendererHandle PipelineHandle;
typedef RendererHandle SwapchainHandle;
typedef RendererHandle CommandListHandle;

// --- Enums ---
enum class GfxBackend : uint32_t { D3D12=0, Vulkan=1, OpenGL=2, Metal=3 };

enum class TextureFormat : uint32_t { RGBA8_UNORM=0, BGRA8_UNORM=1, D24S8=2, D32F=3 };

enum class PresentMode : uint32_t { Fifo=0, Mailbox=1, Immediate=2 };
enum class ShaderStage : uint32_t { Vertex=1, Fragment=2, Compute=4 };
enum class PrimitiveTopology : uint32_t { TriangleList=0, TriangleStrip=1, LineList=2 };
enum class SamplerFilter : uint32_t { Nearest = 0, Linear = 1 };
enum class AddressMode:uint32_t { Repeat = 0, ClampToEdge = 1, Mirror = 2, Border = 3 };

enum AccessFlag : uint32_t {
    Access_None = 0,
    Access_HostWrite = 1 << 0,
    Access_UniformRead = 1 << 1,
    Access_VertexRead = 1 << 2,
    Access_IndexRead = 1 << 3,
    Access_ShaderRead = 1 << 4,
    Access_ShaderWrite = 1 << 5,
    Access_ColorWrite = 1 << 6,
    Access_DepthWrite = 1 << 7
};

struct Extent2D { uint32_t width, height; };

// --- Descriptors ---
struct RendererDesc {
    GfxBackend backend;
    void* platform_window; // HWND for Win32, wl_surface for Wayland
    void* platform_display; // Optional: wl_display for Wayland
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

// --- Buffers, shaders, pipelines ---
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

struct ShaderModuleDesc {
  ShaderStage stage;
  const void* data;
  uint32_t    size;
  uint32_t    format; // 0 = D3D blob
};

enum class VertexAttributeFormat : uint32_t {
    Float = 0,
    Float2,
    Float3,
    Float4,
    UByte4,
    Unknown
};

struct VertexAttributeDesc {
  uint32_t location;
  VertexAttributeFormat format;
  uint32_t offset;
  char semanticName[32];
};

struct VertexLayoutDesc {
  uint32_t stride;
  const VertexAttributeDesc* attributes;
  uint32_t attribute_count;
};

struct DepthStencilOptions {
    bool enableDepth = false;
    bool depthWrite = true;
    bool enableStencil = false;
    float clearDepth = 1.0f;
    uint8_t clearStencil = 0;
    enum class DepthFunc { Less, LessEqual, Greater, Always } depthFunc = DepthFunc::Less;
};

struct GraphicsPipelineDesc {
  ShaderModuleHandle vs;
  ShaderModuleHandle fs; // optional
  PrimitiveTopology topology;
  VertexLayoutHandle vertex_layout;
  TextureFormat      color_format; // single RT
  DepthStencilOptions depth_stencil;
  bool               enable_blend = false;
};

enum class BindGroupEntryType {
    UniformBuffer = 0,
    Texture = 1,
    Sampler = 2,
    StructuredBuffer = 4,
    ByteAddressBuffer = 8,
    UnorderedAccess = 16,
    Unknown = -1
};

enum class LoadOp : uint32_t { Load, Clear, DontCare };
struct ColorAttachmentDesc {
    TextureHandle tex;
    float clear_rgba[4];
};
struct DepthAttachmentDesc {
    TextureHandle tex;
    float clear_d;
};

// --- Renderer function table ---
typedef struct RendererAPI {
    // frame lifecycle
    bool (*begin_frame)();
    void (*end_frame)();

    // lifecycle
    bool (*init)(const RendererDesc*);
    void (*shutdown)();

    // swapchain
    SwapchainHandle (*create_swapchain)(const SwapchainDesc*);
    void (*resize_swapchain)(SwapchainHandle, Extent2D);
    void (*destroy_swapchain)(SwapchainHandle);
    TextureHandle (*get_current_backbuffer)(SwapchainHandle);
    TextureHandle (*get_depth_buffer)(SwapchainHandle);
     
    // resources
    BufferHandle (*create_buffer)(const BufferDesc*, const void* initial_data);
    void         (*destroy_buffer)(BufferHandle);
    bool         (*update_buffer)(BufferHandle, uint64_t dst_offset, const void* data, uint64_t size);

    // textures & samplers
    TextureHandle (*create_texture)(const TextureDesc*, const void* initial_data);
    void (*destroy_texture)(TextureHandle);
    SamplerHandle (*create_sampler)(const SamplerDesc*);
    void (*destroy_sampler)(SamplerHandle);

    // Bindless index access
    uint32_t (*get_texture_index)(TextureHandle);
    uint32_t (*get_sampler_index)(SamplerHandle);

    ShaderModuleHandle (*create_shader_module)(const ShaderModuleDesc*);
    void              (*destroy_shader_module)(ShaderModuleHandle);
    VertexLayoutHandle (*create_vertex_layout)(const VertexLayoutDesc*);
    void               (*destroy_vertex_layout)(VertexLayoutHandle);
    PipelineHandle (*create_graphics_pipeline)(const GraphicsPipelineDesc*);
    void           (*destroy_pipeline)(PipelineHandle);

    // command encoding
    CommandListHandle (*begin_commands)();
    void (*cmd_begin_pass)(CommandListHandle, LoadOp load_op, const ColorAttachmentDesc* colors, uint32_t count, const DepthAttachmentDesc* depth);
    void (*cmd_end_pass)(CommandListHandle);
    
    void (*cmd_bind_uniform)(CommandListHandle, uint32_t slot, BufferHandle, uint64_t offset);    
    void (*cmd_push_constants)(CommandListHandle, uint32_t offset, uint32_t count, const void* data);
    void (*cmd_barrier)(CommandListHandle, BufferHandle, uint32_t src_access, uint32_t dst_access);
    
    void (*cmd_set_pipeline)(CommandListHandle, PipelineHandle);
    void (*cmd_set_vertex_buffer)(CommandListHandle, uint32_t slot, BufferHandle, uint64_t offset);
    void (*cmd_set_index_buffer)(CommandListHandle, BufferHandle, bool index32, uint64_t offset);
    void (*cmd_draw)(CommandListHandle, uint32_t vtx_count, uint32_t instance_count, uint32_t first_vtx, uint32_t first_instance);
    void (*cmd_draw_indexed)(CommandListHandle, uint32_t idx_count, uint32_t inst_count, uint32_t first_idx, int32_t vtx_offset, uint32_t first_instance);
    void (*end_commands)(CommandListHandle);

    // submit/present
    void (*submit)(CommandListHandle* lists, uint32_t list_count);
    void (*present)(SwapchainHandle);
    void (*wait_idle)();
} RendererAPI;

typedef bool (*PFN_LoadRenderer)(RendererAPI* out_api);

} // extern "C"
