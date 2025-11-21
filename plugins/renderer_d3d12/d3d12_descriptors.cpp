#include "d3d12_descriptors.h"

using Microsoft::WRL::ComPtr;

bool DescriptorAllocatorCPU::create(ID3D12Device* dev, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count) {
    // RTV heap (big enough for swapchain backbuffers)
    D3D12_DESCRIPTOR_HEAP_DESC dh{};
    dh.NumDescriptors = count;
    dh.Type           = type;
    dh.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(dev->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&heap_)))) return false;
    incSize_  = dev->GetDescriptorHandleIncrementSize(type);
    capacity_ = count;

    return true;
}

bool DescriptorAllocatorGPU::create(ID3D12Device* dev, UINT srvCount, UINT sampCount) {
    D3D12_DESCRIPTOR_HEAP_DESC dh{};
    dh.NumDescriptors = srvCount;
    dh.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    dh.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if(FAILED(dev->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&srvHeap_)))) return false;
    dh.NumDescriptors = sampCount;
    dh.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    dh.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if(FAILED(dev->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&sampHeap_)))) return false;

    return true;
}

void DescriptorAllocatorGPU::reset() {
    
}
