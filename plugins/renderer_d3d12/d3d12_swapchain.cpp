#include "d3d12_swapchain.h"
#include "d3d12_utils.h"

using Microsoft::WRL::ComPtr;

bool  D3D12Swapchain::create(HWND hwnd, IDXGIFactory6* factory, ID3D12Device* dev,
                             ID3D12CommandQueue* queue, DXGI_FORMAT rtvFormat, UINT width, UINT height, uint32_t frameCount)
{
    frameCount_ = frameCount;
    rtvFormat_ = rtvFormat;
    width_ = width;
    height_ = height;

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.BufferCount = frameCount_;
    scd.Width = width;
    scd.Height = height;
    scd.Format = rtvFormat;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.SampleDesc.Count = 1;
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    scd.Scaling = DXGI_SCALING_STRETCH;

    ComPtr<IDXGISwapChain1> swap1;
    if (FAILED(factory->CreateSwapChainForHwnd(queue, hwnd, &scd, nullptr, nullptr, &swap1))) return false;
    if (FAILED(swap1.As(&swap_))) return false;

    // RTV heap (big enough for swapchain backbuffers)
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.NumDescriptors = 8; // plenty for starter
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(dev->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_)))) return false;
    rtvInc_ = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    
    // Create RTVs
    rebuild_rtvs(dev);

    return true;
}

void  D3D12Swapchain::destroy()
{
    rtv_.clear();
    swap_.Reset();
    rtvHeap_.Reset();
    rtvInc_ = 0;
    frameCount_ = 0;
}

void D3D12Swapchain::resize(ID3D12Device* dev, UINT width, UINT height)
{
    if (!swap_) return;
    width_ = width;
    height_ = height;
    
    // Release old backbuffers before resizing
    for (auto& bb : rtv_) bb.res.Reset();
    rtv_.clear();

    HR_CHECK(swap_->ResizeBuffers(frameCount_, width_, height_, rtvFormat_, 0));
    rebuild_rtvs(dev);
}

void D3D12Swapchain::rebuild_rtvs(ID3D12Device* dev) {    
    // Create RTVs
    rtv_.clear();
    rtv_.resize(frameCount_);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < frameCount_; ++i) {
        BackbufferRTV bb{};
        HR_CHECK(swap_->GetBuffer(i, IID_PPV_ARGS(&bb.res)));

        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        handle.ptr = rtvStart.ptr + SIZE_T(i) * SIZE_T(rtvInc_);
        bb.rtv = handle;

        dev->CreateRenderTargetView(bb.res.Get(), nullptr, bb.rtv);
        rtv_[i] = std::move(bb);
    }
}
