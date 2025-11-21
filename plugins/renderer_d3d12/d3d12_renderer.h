#pragma once
#include <wrl.h>
#include <memory>
#include <vector>
#include <mutex>
#include "render/public/renderer_api.h"

// Forward declarations for internal classes
class D3D12Device;              
class D3D12Swapchain;           
class DescriptorAllocatorCPU;   
class DescriptorAllocatorGPU;   
class UploadRing;               
class ResourceTable;            
class PipelineTable;            
class BindSpace;                
class FrameContext;             

class RendererD3D12 {
public:
    RendererD3D12();
    ~RendererD3D12();

    // Non-copyable
    RendererD3D12(const RendererD3D12&) = delete;
    RendererD3D12& operator=(const RendererD3D12&) = delete;

    // --- RendererAPI methods ---
    bool init(const RendererDesc*);
    void shutdown();

    void begin_frame();
    void end_frame();

    SwapchainHandle create_swapchain(const SwapchainDesc*);
    void resize_swapchain(SwapchainHandle, Extent2D);
    void destroy_swapchain(SwapchainHandle);
    TextureHandle get_current_backbuffer(SwapchainHandle);

    BufferHandle create_buffer(const BufferDesc*, const void* initial);
    void destroy_buffer(BufferHandle);
    bool update_buffer(BufferHandle, uint64_t dst_off, const void* data, uint64_t size);

    TextureHandle create_texture(const TextureDesc*, const void* initial);
    void destroy_texture(TextureHandle);
    SamplerHandle create_sampler(const SamplerDesc*);
    void destroy_sampler(SamplerHandle);

    ShaderModuleHandle create_shader_module(const ShaderModuleDesc*);
    void               destroy_shader_module(ShaderModuleHandle);
    PipelineHandle     create_graphics_pipeline(const GraphicsPipelineDesc*);
    void               destroy_pipeline(PipelineHandle);

    BindGroupLayoutHandle create_bind_group_layout(const BindGroupLayoutDesc*);
    void                  destroy_bind_group_layout(BindGroupLayoutHandle);
    BindGroupHandle       create_bind_group(const BindGroupDesc*);
    void                  destroy_bind_group(BindGroupHandle);

    CommandListHandle begin_commands();
    void cmd_begin_rendering_ops(CommandListHandle,
                                 const ColorAttachmentDesc* colors, uint32_t count,
                                 const DepthAttachmentDesc* depth);
    void cmd_end_rendering(CommandListHandle);

    void cmd_set_bind_group(CommandListHandle, uint32_t set_index, BindGroupHandle);
    void cmd_set_pipeline(CommandListHandle, PipelineHandle);
    void cmd_set_vertex_buffer(CommandListHandle, uint32_t slot, BufferHandle, uint64_t offset);
    void cmd_set_index_buffer(CommandListHandle, BufferHandle, bool index32, uint64_t offset);

    void cmd_draw(CommandListHandle, uint32_t vtx_count, uint32_t inst_count,
                  uint32_t first_vtx, uint32_t first_inst);
    void cmd_draw_indexed(CommandListHandle, uint32_t idx_count, uint32_t inst_count,
                          uint32_t first_idx, int32_t vtx_offset, uint32_t first_inst);

    void end_commands(CommandListHandle);
    void submit(CommandListHandle* lists, uint32_t list_count);
    void present(SwapchainHandle);
    void wait_idle();

    // Expose the API table filled with trampolines
    static bool LoadRenderer(RendererAPI* out_api);

private:
    std::mutex mtx_;
    ComPtr<IDXGIFactory6> factory_;
    std::unique_ptr<D3D12Device> device_;
    std::unique_ptr<D3D12Swapchain> swapchain_;

    std::unique_ptr<DescriptorAllocatorCPU> cpuDesc_;
    std::unique_ptr<DescriptorAllocatorCPU> samplerHeapCpu_;
    std::vector<std::unique_ptr<DescriptorAllocatorGPU>> gpuDescPerFrame_;
    std::vector<std::unique_ptr<UploadRing>> uploadPerFrame_;

    std::unique_ptr<ResourceTable> resources_;  // buffers, textures, samplers, shader blobs
    std::unique_ptr<PipelineTable> pipelines_;  // root signatures, PSOs
    std::unique_ptr<BindSpace>     binds_;      // layouts + bind groups (+ fallback CBV)

    std::vector<std::unique_ptr<FrameContext>> frames_;
    uint32_t frameIndex_ = 0;
    uint32_t frameCount_ = 3;
    bool frameBegun_ = false;
    HWND hwnd_ = 0;

    // Helpers
    FrameContext& curFrame();
};
