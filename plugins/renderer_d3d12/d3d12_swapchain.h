// d3d12_swapchain.h
#pragma once
#include <wrl.h>
#include <vector>
#include <dxgi1_6.h>
#include <d3d12.h>

#include "common/result.h"

struct BackbufferRTV {
    Microsoft::WRL::ComPtr<ID3D12Resource> res;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
};

class D3D12Swapchain {
public:
    jaeng::result<> create(HWND hwnd, IDXGIFactory6* factory, ID3D12Device* dev, ID3D12CommandQueue* queue,
                           DXGI_FORMAT rtvFormat, UINT width, UINT height, uint32_t frameCount, bool allow_tearing);
    void  destroy();

    jaeng::result<> resize(ID3D12Device* dev, UINT width, UINT height, bool allow_tearing);
    UINT  current_index() const { return swap_->GetCurrentBackBufferIndex(); }
    DXGI_FORMAT rtv_format() const { return rtvFormat_; }

    // RTV handle for the current buffer
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle(UINT index) const { return rtv_[index].rtv; }
    ID3D12Resource*             rtv_resource(UINT index) const { return rtv_[index].res.Get(); }

    IDXGISwapChain3*  swap() const { return swap_.Get(); }

private:
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_;
    std::vector<BackbufferRTV> rtv_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    UINT rtvInc_ = 0;
    uint32_t frameCount_ = 3;
    DXGI_FORMAT rtvFormat_ = DXGI_FORMAT_B8G8R8A8_UNORM;
    UINT width_ = 0, height_ = 0;

    void rebuild_rtvs(ID3D12Device* dev);
};
