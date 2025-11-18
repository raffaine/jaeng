#define RENDERER_BUILD
#include "renderer_api.h"

#include <windows.h>
#include <wrl.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <vector>
#include <cassert>
#include <cstdint>
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace {
    struct FrameResources {
        ComPtr<ID3D12CommandAllocator> cmdAlloc;
    };

    struct Backbuffer {
        ComPtr<ID3D12Resource> resource;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    };

    struct SwapchainState {
        ComPtr<IDXGISwapChain3> swapchain;
        std::vector<Backbuffer> backbuffers;
        UINT current_index = 0;
        UINT buffer_count = 0;
    };

    struct D3D12State {
        HWND hwnd = nullptr;
        uint32_t frame_count = 3;

        ComPtr<ID3D12Device> device;
        ComPtr<IDXGIFactory6> factory;
        ComPtr<ID3D12CommandQueue> gfxQueue;
        ComPtr<ID3D12Fence> fence;
        HANDLE fenceEvent = NULL;
        UINT64 fenceValue = 0;
        std::vector<UINT64> frameFenceValues; // size = frame_count

        ComPtr<ID3D12DescriptorHeap> rtvHeap;
        UINT rtvDescriptorSize = 0;

        std::vector<FrameResources> frames; // size = frame_count

        ComPtr<ID3D12GraphicsCommandList> cmdList; // reused

        SwapchainState swap{};

        // Simple handle mapping for textures (only swapchain backbuffers here)
        std::vector<ComPtr<ID3D12Resource>> textures; // index = handle-1

        std::mutex mtx;        
    } g;

    static void cleanup_state() {
        // Ensure GPU idle before tearing down GPU objects
        if (g.gfxQueue && g.fence) {
            g.gfxQueue->Signal(g.fence.Get(), ++g.fenceValue);
            g.fence->SetEventOnCompletion(g.fenceValue, g.fenceEvent);
            WaitForSingleObject(g.fenceEvent, INFINITE);
        }

        // Close event
        if (g.fenceEvent) {
            CloseHandle(g.fenceEvent);
            g.fenceEvent = NULL;
        }

        // Release swapchain and RTVs
        g.swap = {};  // Backbuffers ComPtrs release automatically

        // Release command list and per-frame allocators
        g.cmdList.Reset();
        for (auto &f : g.frames) {
            f.cmdAlloc.Reset();
        }
        g.frames.clear();
        g.frameFenceValues.clear();

        // Release descriptor heaps
        g.rtvHeap.Reset();
        g.rtvDescriptorSize = 0;

        // Release textures handle table
        g.textures.clear();

        // Release core D3D objects
        g.gfxQueue.Reset();
        g.fence.Reset();
        g.device.Reset();
        g.factory.Reset();

        // Reset simple POD members
        g.hwnd = nullptr;
        g.frame_count = 0;
    }   

    static DXGI_FORMAT ToDxgiFormat(TextureFormat fmt) {
        switch (fmt) {
            case TextureFormat::BGRA8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
            case TextureFormat::RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case TextureFormat::D24S8: return DXGI_FORMAT_D24_UNORM_S8_UINT;
            case TextureFormat::D32F: return DXGI_FORMAT_D32_FLOAT;
            default: return DXGI_FORMAT_B8G8R8A8_UNORM;
        }
    }

    bool init(const RendererDesc* desc) {
        std::lock_guard<std::mutex> lock(g.mtx);
        g.hwnd = (HWND)desc->platform_window;
        g.frame_count = desc->frame_count ? desc->frame_count : 3;

#if defined(_DEBUG)
        // Enable D3D12 debug layer if available
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
        }
#endif

        UINT factoryFlags = 0;
#if defined(_DEBUG)
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
        if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&g.factory)))) {
            return false;
        }

        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; g.factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc1{};
            adapter->GetDesc1(&desc1);
            if (desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g.device)))) {
                break;
            }
        }
        if (!g.device) {
            // fallback WARP
            if (FAILED(g.factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))) return false;
            if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g.device)))) return false;
        }

        // Command queue
        D3D12_COMMAND_QUEUE_DESC qdesc{};
        qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(g.device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&g.gfxQueue)))) return false;

        // Fence
        if (FAILED(g.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g.fence)))) return false;
        g.fenceValue = 1;
        g.fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        // RTV heap (big enough for swapchain backbuffers)
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.NumDescriptors = 8; // plenty for starter
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED(g.device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g.rtvHeap)))) return false;
        g.rtvDescriptorSize = g.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Frames
        g.frames.resize(g.frame_count);
        g.frameFenceValues.resize(g.frame_count, 0);
        for (uint32_t i = 0; i < g.frame_count; ++i) {
            if (FAILED(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g.frames[i].cmdAlloc)))) return false;
        }

        if (FAILED(g.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g.frames[0].cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&g.cmdList)))) return false;
        g.cmdList->Close();

        return true;
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(g.mtx);
        cleanup_state();
    }

    SwapchainHandle create_swapchain(const SwapchainDesc* d) {
        std::lock_guard<std::mutex> lock(g.mtx);
        if (!g.hwnd) return 0;

        DXGI_SWAP_CHAIN_DESC1 scd{};
        scd.BufferCount = g.frame_count;
        scd.Width = d->size.width;
        scd.Height = d->size.height;
        scd.Format = ToDxgiFormat(d->format);
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scd.SampleDesc.Count = 1;
        scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        scd.Scaling = DXGI_SCALING_STRETCH;

        ComPtr<IDXGISwapChain1> swap1;
        if (FAILED(g.factory->CreateSwapChainForHwnd(g.gfxQueue.Get(), g.hwnd, &scd, nullptr, nullptr, &swap1))) return 0;
        ComPtr<IDXGISwapChain3> swap3;
        swap1.As(&swap3);

        g.swap.swapchain = swap3;
        g.swap.buffer_count = g.frame_count;
        g.swap.backbuffers.resize(g.frame_count);

        // Create RTVs (no CD3DX12 dependency)
        D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = g.rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < g.frame_count; ++i) {
            Backbuffer bb{};
            swap3->GetBuffer(i, IID_PPV_ARGS(&bb.resource));
            D3D12_CPU_DESCRIPTOR_HANDLE handle;
            handle.ptr = rtvStart.ptr + SIZE_T(i) * SIZE_T(g.rtvDescriptorSize);
            bb.rtv = handle;
            g.device->CreateRenderTargetView(bb.resource.Get(), nullptr, bb.rtv);
            g.swap.backbuffers[i] = bb;
        }
        g.swap.current_index = swap3->GetCurrentBackBufferIndex();

        // Allocate texture handles for backbuffers (1-based handles)
        g.textures.assign(g.frame_count, {});
        for (UINT i = 0; i < g.frame_count; ++i) {
            g.textures[i] = g.swap.backbuffers[i].resource; // store
        }

        return 1; // single swapchain id in this starter
    }

    void resize_swapchain(SwapchainHandle, Extent2D) {
        // (Left as an exercise: destroy RTVs, ResizeBuffers, recreate RTVs)
    }

    void destroy_swapchain(SwapchainHandle) {
        std::lock_guard<std::mutex> lock(g.mtx);
        g.swap = SwapchainState{};
    }

    TextureHandle get_current_backbuffer(SwapchainHandle) {
        std::lock_guard<std::mutex> lock(g.mtx);
        g.swap.current_index = g.swap.swapchain->GetCurrentBackBufferIndex();
        // Handle is index+1
        return (TextureHandle)(g.swap.current_index + 1);
    }

    CommandListHandle begin_commands() {
        std::lock_guard<std::mutex> lock(g.mtx);
        UINT frame = g.swap.swapchain ? g.swap.swapchain->GetCurrentBackBufferIndex() : 0;

        // Wait if this frame is still in-flight
        if (g.frameFenceValues[frame] != 0 && g.fence->GetCompletedValue() < g.frameFenceValues[frame]) {
            g.fence->SetEventOnCompletion(g.frameFenceValues[frame], g.fenceEvent);
            WaitForSingleObject(g.fenceEvent, INFINITE);
        }

        g.frames[frame].cmdAlloc->Reset();
        g.cmdList->Reset(g.frames[frame].cmdAlloc.Get(), nullptr);
        return 1; // single command list handle
    }

    static ID3D12Resource* TexFromHandle(TextureHandle h) {
        if (h == 0) return nullptr;
        uint32_t idx = h - 1;
        if (idx >= g.textures.size()) return nullptr;
        return g.textures[idx].Get();
    }

    void cmd_begin_rendering(CommandListHandle, TextureHandle* color_rt, uint32_t rt_count, float clear_rgba[4]) {
        assert(rt_count >= 1);
        ID3D12Resource* res = TexFromHandle(color_rt[0]);

        // Transition PRESENT -> RENDER_TARGET
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = res;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        g.cmdList->ResourceBarrier(1, &barrier);

        // RTV handle for current buffer
        UINT idx = g.swap.current_index;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = g.swap.backbuffers[idx].rtv;

        g.cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        g.cmdList->ClearRenderTargetView(rtv, clear_rgba, 0, nullptr);
    }

    void cmd_end_rendering(CommandListHandle) {
        // Transition RENDER_TARGET -> PRESENT
        ID3D12Resource* res = g.swap.backbuffers[g.swap.current_index].resource.Get();
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = res;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        g.cmdList->ResourceBarrier(1, &barrier);
    }

    void end_commands(CommandListHandle) {
        g.cmdList->Close();
    }

    void submit(CommandListHandle* lists, uint32_t list_count) {
        (void)lists; (void)list_count; // single list in this starter
        ID3D12CommandList* submitLists[] = { g.cmdList.Get() };
        g.gfxQueue->ExecuteCommandLists(1, submitLists);
    }

    void present(SwapchainHandle) {
        UINT syncInterval = 1; // vsync on (Fifo)
        UINT flags = 0;
        g.swap.swapchain->Present(syncInterval, flags);

        // Signal fence for this frame
        UINT frame = g.swap.swapchain->GetCurrentBackBufferIndex();
        g.gfxQueue->Signal(g.fence.Get(), ++g.fenceValue);
        g.frameFenceValues[frame] = g.fenceValue;
    }

    void wait_idle() {
        g.gfxQueue->Signal(g.fence.Get(), ++g.fenceValue);
        g.fence->SetEventOnCompletion(g.fenceValue, g.fenceEvent);
        WaitForSingleObject(g.fenceEvent, INFINITE);
    }
} // namespace

extern "C" RENDERER_API bool LoadRenderer(RendererAPI* out_api) {
    if (!out_api) return false;
    *out_api = RendererAPI{
    &init,
    &shutdown,
    &create_swapchain,
    &resize_swapchain,
    &destroy_swapchain,
    &get_current_backbuffer,
    &begin_commands,
    &cmd_begin_rendering,
    &cmd_end_rendering,
    &end_commands,
    &submit,
    &present,
    &wait_idle
};
return true;
}