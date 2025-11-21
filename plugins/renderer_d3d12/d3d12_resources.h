// d3d12_resources.h
#pragma once
#include <wrl.h>
#include <vector>
#include <d3d12.h>
#include "render/public/renderer_api.h"

struct BufferRec {
    Microsoft::WRL::ComPtr<ID3D12Resource> res;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW  ibv{};
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    uint32_t usage = 0;
    UINT64   size = 0;
};

struct TextureRec {
    Microsoft::WRL::ComPtr<ID3D12Resource> res;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    UINT width=0, height=0;
};

struct SamplerRec {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
};

class ResourceTable {
public:
    BufferHandle  add_buffer(BufferRec&& b);
    TextureHandle add_texture(TextureRec&& t);
    SamplerHandle add_sampler(SamplerRec&& s);

    BufferRec*  get_buf(BufferHandle h);
    TextureRec* get_tex(TextureHandle h);
    SamplerRec* get_samp(SamplerHandle h);

    // ... destroy helpers ...

private:
    std::vector<BufferRec>  buffers_;
    std::vector<TextureRec> textures_;
    std::vector<SamplerRec> samplers_;
};
