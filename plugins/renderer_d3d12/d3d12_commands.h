// d3d12_commands.h
#pragma once
#include <wrl.h>
#include <d3d12.h>

class DescriptorAllocatorGPU;
class UploadRing;

class FrameContext {
public:
    bool init(ID3D12Device* dev);
    void reset(); // Reset allocator and cmd list for the frame

    ID3D12GraphicsCommandList* cmd() const { return cmd_.Get(); }
    DescriptorAllocatorGPU* gpuDescs = nullptr;  // owned outside
    UploadRing*             upload   = nullptr;  // owned outside
    
    UINT64  fenceValue = 0;
private:
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmd_;
};
