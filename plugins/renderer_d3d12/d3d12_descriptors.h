// d3d12_descriptors.h
#pragma once
#include <wrl.h>
#include <d3d12.h>

class DescriptorAllocatorCPU {
public:
    bool create(ID3D12Device* dev, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count);
    D3D12_CPU_DESCRIPTOR_HANDLE allocate(UINT* outIndex); // linear bump
    UINT inc() const { return incSize_; }
    ID3D12DescriptorHeap* heap() const { return heap_.Get(); }
    UINT capacity() const { return capacity_; }
    UINT used() const { return used_; }

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    UINT incSize_ = 0;
    UINT capacity_ = 0;
    UINT used_ = 0;
};

class DescriptorAllocatorGPU {
public:
    bool create(ID3D12Device* dev, UINT srvCount, UINT sampCount);
    void reset(); // per-frame
    // Allocate 1 CBV/SRV/UAV or 1 Sampler slot (returns CPU+GPU handles)
    void alloc_srv(D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu);
    void alloc_samp(D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu);
    ID3D12DescriptorHeap* srv_heap() const { return srvHeap_.Get(); }
    ID3D12DescriptorHeap* samp_heap() const { return sampHeap_.Get(); }
    UINT srv_inc() const { return srvInc_; }
    UINT samp_inc() const { return sampInc_; }

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sampHeap_;
    UINT srvInc_ = 0, sampInc_ = 0;
    UINT srvUsed_ = 0, sampUsed_ = 0;
    UINT srvCap_ = 0, sampCap_ = 0;
};
