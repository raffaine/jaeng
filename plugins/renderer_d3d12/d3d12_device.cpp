#include <wrl.h>
#include <cassert>

#include "d3d12_device.h"

using Microsoft::WRL::ComPtr;

bool D3D12Device::create(IDXGIFactory6* factory) {
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc1{};
        adapter->GetDesc1(&desc1);
        if (desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)))) {
            break;
        }
    }
    if (!device_) {
        // fallback WARP
        if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))) return false;
        if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)))) return false;
    }

    // Command queue
    D3D12_COMMAND_QUEUE_DESC qdesc{};
    qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qdesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    qdesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    qdesc.NodeMask = 0;
    if (FAILED(device_->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&gfxQueue_)))) return false;

    // Fence
    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) return false;
    if (fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr); !fenceEvent_) return false;
    fenceValue_ = 1;

    return true;
}

UINT64 D3D12Device::signal() {
    assert(gfxQueue_ && fence_);
    // Increments the fence value and signals it on the queue
    const UINT64 v = ++fenceValue_;
    gfxQueue_->Signal(fence_.Get(), v);
    return v;
}

void D3D12Device::wait(UINT64 value) {
    assert(fence_);
    if (fence_->GetCompletedValue() < value) {
        fence_->SetEventOnCompletion(value, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void D3D12Device::shutdown() {
    // Ensure GPU has finished all work we submitted
    if (gfxQueue_ && fence_) {
        const UINT64 v = signal();
        wait(v);
    }
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = NULL;
    }
    fence_.Reset();
    gfxQueue_.Reset();
    device_.Reset();
}
