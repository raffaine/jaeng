// d3d12_commands.h
#pragma once
#include <wrl.h>
#include <d3d12.h>

#include "common/result.h"

class DescriptorAllocatorGPU;
class UploadRing;

class FrameContext {
public:
    jaeng::result<> init(ID3D12Device* dev);
    void reset(); // Reset allocator and cmd list for the frame

    ID3D12GraphicsCommandList* cmd() const { return cmd_.Get(); }
    DescriptorAllocatorGPU* gpuDescs = nullptr;  // owned outside
    UploadRing*             upload   = nullptr;  // owned outside
    
    UINT64  fenceValue = 0;

    // CBV table slots reserved at pass begin
    D3D12_CPU_DESCRIPTOR_HANDLE cbvBaseCpu{}; // slot for b0 (frame)
    D3D12_GPU_DESCRIPTOR_HANDLE cbvBaseGpu{};
    D3D12_CPU_DESCRIPTOR_HANDLE cbvObjCpu{};  // slot for b1 (object)
    D3D12_GPU_DESCRIPTOR_HANDLE cbvObjGpu{};
private:
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmd_;
    
};
