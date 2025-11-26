// d3d12_bind.h
#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <vector>

#include "common/result.h"
#include "d3d12_descriptors.h"
#include "d3d12_resources.h"

struct BindGroupLayoutRec {
    std::vector<BindGroupLayoutEntry> entries;
};

struct BindGroupRec {
    BindGroupLayoutHandle layout{};
    TextureHandle texture{};
    SamplerHandle sampler{};
    struct { BufferHandle buf{}; UINT64 offset{}, size{}; bool present=false;
             D3D12_CPU_DESCRIPTOR_HANDLE cpuCbv{}; bool cpuValid=false; } cb;
};

class BindSpace {
public:
    jaeng::result<> init(ID3D12Device* dev, DescriptorAllocatorCPU* cpuDesc);
    void shutdown();

    // Fallback CBV:
    D3D12_CPU_DESCRIPTOR_HANDLE fallback_cbv_cpu() const { return fallbackCbvCpu_; }
    BufferHandle fallback_cb_buffer() const { return 0; }

    BindGroupLayoutHandle add_layout(const BindGroupLayoutDesc*);
    void del_layout(BindGroupLayoutHandle);

    BindGroupHandle add_group(const BindGroupDesc*, ID3D12Device*, ResourceTable*, DescriptorAllocatorCPU*);
    void del_group(BindGroupHandle);

    BindGroupRec* get_group(BindGroupHandle);

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> fallbackCb_;
    D3D12_CPU_DESCRIPTOR_HANDLE fallbackCbvCpu_{};
    bool fallbackReady_ = false;

    std::vector<BindGroupLayoutRec> layouts_;
    std::vector<BindGroupRec>       groups_;

    jaeng::result<> create_fallback_cbv(ID3D12Device* dev, DescriptorAllocatorCPU* cpu);
};
