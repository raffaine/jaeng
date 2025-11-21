// d3d12_device.h
#pragma once
#include <wrl.h>
#include <dxgi1_6.h>
#include <d3d12.h>

class D3D12Device {
public:
    bool create(IDXGIFactory6* factory);
    ID3D12Device*          dev()     const { return device_.Get(); }
    ID3D12CommandQueue*    queue()   const { return gfxQueue_.Get(); }
    ID3D12Fence*           fence()   const { return fence_.Get(); }
    HANDLE                 fenceEvent() const { return fenceEvent_; }
    UINT64                 signal();
    void                   wait(UINT64 value);
    void                   shutdown();

private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> gfxQueue_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = NULL;
    UINT64 fenceValue_ = 0;
};
