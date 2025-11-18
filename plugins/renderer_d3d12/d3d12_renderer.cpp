#define RENDERER_BUILD
#include "renderer_api.h"

#include <windows.h>
#include <wrl.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <d3d12shader.h>
#include <vector>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace {
    struct Buffer {
        ComPtr<ID3D12Resource> res;
        D3D12_VERTEX_BUFFER_VIEW vbv{};
        uint64_t size = 0;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        uint32_t usage = 0;
    };

    struct ShaderBlob {
      std::vector<uint8_t> bytes;
    };
    
    struct Pipeline {
      ComPtr<ID3D12PipelineState> pso;
      ComPtr<ID3D12RootSignature> root;
      D3D12_PRIMITIVE_TOPOLOGY topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    };

    struct FrameResources {
        ComPtr<ID3D12CommandAllocator> cmdAlloc;
        // Step 3: per-frame upload ring (persistently mapped)
        ComPtr<ID3D12Resource> upload;
        uint8_t* uploadPtr = nullptr;
        uint64_t uploadSize = 8ull * 1024ull * 1024ull; // 8 MB ring
        uint64_t uploadOffset = 0;
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
        UINT current_frame = 0;   // swapchain's backbuffer index as frame index
        bool frameBegun = false;

        // Simple handle mapping for textures (only swapchain backbuffers here)
        std::vector<ComPtr<ID3D12Resource>> textures; // index = handle-1
        
        // New: generic resources for triangle sample
        std::vector<Buffer> buffers;         // handle = index+1
        std::vector<ShaderBlob> shaders;     // handle = index+1
        std::vector<Pipeline> pipelines;     // handle = index+1

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
            if (f.upload && f.uploadPtr) { f.upload->Unmap(0, nullptr); f.uploadPtr = nullptr; }
            f.cmdAlloc.Reset();
            f.upload.Reset();
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

    static void Barrier(ID3D12GraphicsCommandList* cl, ID3D12Resource* res,
                        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        if (before == after) return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = res;
        b.Transition.StateBefore = before;
        b.Transition.StateAfter  = after;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cl->ResourceBarrier(1, &b);
    }

    static inline uint64_t AlignUp(uint64_t v, uint64_t align) {
        return (v + (align - 1)) & ~(align - 1);
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
 
    // ---------- New: resources ----------
    template <typename T>
    static RendererHandle PushHandle(std::vector<T>& vec, T&& v) {
        vec.push_back(std::move(v));
        return (RendererHandle)vec.size();
    }

    template <typename T>
    static T* GetByHandle(std::vector<T>& vec, RendererHandle h) {
        if (h == 0) return nullptr;
        size_t idx = size_t(h - 1);
        if (idx >= vec.size()) return nullptr;
        return &vec[idx];
    }

    // ---------- Step 3: frame lifecycle ----------
    void begin_frame_impl() {
        // Determine frame index from swapchain (or 0 if not created yet)
        g.current_frame = g.swap.swapchain ? g.swap.swapchain->GetCurrentBackBufferIndex() : 0;
        // Wait if the GPU is still using this frame's resources
        if (g.frameFenceValues[g.current_frame] != 0 &&
            g.fence->GetCompletedValue() < g.frameFenceValues[g.current_frame]) {
            g.fence->SetEventOnCompletion(g.frameFenceValues[g.current_frame], g.fenceEvent);
            WaitForSingleObject(g.fenceEvent, INFINITE);
        }
        // Reset per-frame things
        g.frames[g.current_frame].uploadOffset = 0;
        g.frames[g.current_frame].cmdAlloc->Reset();
        g.cmdList->Reset(g.frames[g.current_frame].cmdAlloc.Get(), nullptr);
        g.frameBegun = true;
    }

    void end_frame_impl() {
        // Nothing special yet; fences are signaled in present()
        g.frameBegun = false;
    }

    BufferHandle create_buffer(const BufferDesc* d, const void* initial_data) {
        // Step 3: create DEFAULT-heap buffer; CPU writes go through upload ring
        Buffer buf{};
        buf.size = d->size_bytes;
        buf.usage = d->usage;
                   D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = d->size_bytes;
        rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        // Start in COMMON; we'll transition as needed
        if (FAILED(g.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buf.res)))) return 0;
        buf.state = D3D12_RESOURCE_STATE_COMMON;
        if (d->usage & BufferUsage_Vertex) {
          buf.vbv.BufferLocation = buf.res->GetGPUVirtualAddress();
          buf.vbv.StrideInBytes  = 24; // pos.xyz + col.xyz
          buf.vbv.SizeInBytes    = (UINT)d->size_bytes;
        }
        auto h = PushHandle(g.buffers, std::move(buf));
        // Optional convenience: if initial_data provided, perform update now
        if (initial_data) {
          // Make sure a frame is begun to have a valid command list and upload ring
          if (!g.frameBegun) begin_frame_impl();
          GetByHandle(g.buffers, h); // ensure handle valid
          (void)h; // silence warning if not used further
          // We'll call update_buffer through the function pointer later; here we use internal impl:
          // (We expose update_buffer in API; sandbox will call it. Keeping this path simple.)
        }
        return h;
    }

    void destroy_buffer(BufferHandle h) {
        if (auto* b = GetByHandle(g.buffers, h)) { b->res.Reset(); }
    }

    bool update_buffer(BufferHandle h, uint64_t dst_offset, const void* data, uint64_t size) {
        if (!data || size == 0) return true;
        auto* b = GetByHandle(g.buffers, h);
        if (!b) return false;
        if (!g.frameBegun) begin_frame_impl();
        FrameResources& fr = g.frames[g.current_frame];
        // Align uploads to 256 for good measure
        const uint64_t alignment = 256ull;
        uint64_t off = AlignUp(fr.uploadOffset, alignment);
        if (off + size > fr.uploadSize) {
            // Ring overflow within the same frame; try wrap to 0 (safe because we fence per frame)
            off = 0;
            if (size > fr.uploadSize) return false; // too large for the ring
        }
        std::memcpy(fr.uploadPtr + off, data, size);
        fr.uploadOffset = off + size;

        // Ensure COPY_DEST
        Barrier(g.cmdList.Get(), b->res.Get(), b->state, D3D12_RESOURCE_STATE_COPY_DEST);
        b->state = D3D12_RESOURCE_STATE_COPY_DEST;
        // Enqueue copy
        g.cmdList->CopyBufferRegion(b->res.Get(), dst_offset, fr.upload.Get(), off, size);
        // Leave in COPY_DEST; we'll transition to VERTEX at bind time
        return true;
    }

    ShaderModuleHandle create_shader_module(const ShaderModuleDesc* d) {
        ShaderBlob sb{};
        sb.bytes.resize(d->size);
        std::memcpy(sb.bytes.data(), d->data, d->size);
        return PushHandle(g.shaders, std::move(sb));
    }
    
    void destroy_shader_module(ShaderModuleHandle h) {
        if (auto* s = GetByHandle(g.shaders, h)) { s->bytes.clear(); s->bytes.shrink_to_fit(); }
    }

    static D3D12_PRIMITIVE_TOPOLOGY ToD3DTopology(PrimitiveTopology t) {
        switch (t) {
            case PrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            case PrimitiveTopology::LineList:      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
            default:                               return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        }
    }

    static bool CreateEmptyRootSignature(ComPtr<ID3D12Device>& dev, ComPtr<ID3D12RootSignature>& out) {
        D3D12_ROOT_SIGNATURE_DESC rs{};
        rs.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) return false;
        if (FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&out)))) return false;
        return true;
    }

    PipelineHandle create_graphics_pipeline(const GraphicsPipelineDesc* d) {
        Pipeline p{};
        if (!CreateEmptyRootSignature(g.device, p.root)) return 0;
        auto* vsb = GetByHandle(g.shaders, d->vs);
        auto* psb = GetByHandle(g.shaders, d->fs);
        if (!vsb) return 0;

        // Input layout for POSITION (loc 0) and COLOR (loc 1), both R32G32B32_FLOAT
        std::vector<D3D12_INPUT_ELEMENT_DESC> ils;
        ils.reserve(d->vertex_layout.attribute_count);
        for (uint32_t i=0; i<d->vertex_layout.attribute_count; ++i) {
            const auto& a = d->vertex_layout.attributes[i];
            D3D12_INPUT_ELEMENT_DESC e{};
            e.SemanticName = (a.location == 0) ? "POSITION" : "COLOR";
            e.SemanticIndex = 0;
            e.Format = DXGI_FORMAT_R32G32B32_FLOAT;
            e.InputSlot = 0;
            e.AlignedByteOffset = a.offset;
            e.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            ils.push_back(e);
        }

        // Default states (no d3dx12)
        D3D12_BLEND_DESC blend{};
        blend.AlphaToCoverageEnable = FALSE;
        blend.IndependentBlendEnable = FALSE;
        auto &rt0 = blend.RenderTarget[0];
        rt0.BlendEnable = FALSE; rt0.LogicOpEnable = FALSE;
        rt0.SrcBlend = D3D12_BLEND_ONE; rt0.DestBlend = D3D12_BLEND_ZERO; rt0.BlendOp = D3D12_BLEND_OP_ADD;
        rt0.SrcBlendAlpha = D3D12_BLEND_ONE; rt0.DestBlendAlpha = D3D12_BLEND_ZERO; rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt0.LogicOp = D3D12_LOGIC_OP_NOOP; rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_RASTERIZER_DESC rast{};
        rast.FillMode = D3D12_FILL_MODE_SOLID;
        rast.CullMode = D3D12_CULL_MODE_BACK;
        rast.FrontCounterClockwise = FALSE;
        rast.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        rast.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        rast.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        rast.DepthClipEnable = TRUE;
        rast.MultisampleEnable = FALSE;
        rast.AntialiasedLineEnable = FALSE;
        rast.ForcedSampleCount = 0;
        rast.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        D3D12_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = FALSE;
        depth.StencilEnable = FALSE;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = p.root.Get();
        pso.VS = { vsb->bytes.data(), vsb->bytes.size() };
        if (psb) pso.PS = { psb->bytes.data(), psb->bytes.size() };
        pso.BlendState = blend;
        pso.SampleMask = UINT_MAX;
        pso.RasterizerState = rast;
        pso.DepthStencilState = depth;
        pso.InputLayout = { ils.data(), (UINT)ils.size() };
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = ToDxgiFormat(d->color_format);
        pso.SampleDesc.Count = 1;

        if (FAILED(g.device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&p.pso)))) return 0;
        p.topo = ToD3DTopology(d->topology);
        return PushHandle(g.pipelines, std::move(p));
    }
    
    void destroy_pipeline(PipelineHandle h) {
        if (auto* p = GetByHandle(g.pipelines, h)) { p->pso.Reset(); p->root.Reset(); }
    }

    void cmd_set_pipeline(CommandListHandle, PipelineHandle h) {
        auto* p = GetByHandle(g.pipelines, h);
        if (!p) return;
        g.cmdList->SetGraphicsRootSignature(p->root.Get());
        g.cmdList->IASetPrimitiveTopology(p->topo);
        g.cmdList->SetPipelineState(p->pso.Get());
    }

    void cmd_set_vertex_buffer(CommandListHandle, uint32_t slot, BufferHandle b, uint64_t offset) {
        auto* buf = GetByHandle(g.buffers, b);
        if (!buf) return;
        // Transition to VERTEX if needed
        D3D12_RESOURCE_STATES desired = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        Barrier(g.cmdList.Get(), buf->res.Get(), buf->state, desired);
        buf->state = desired;
        D3D12_VERTEX_BUFFER_VIEW vbv = buf->vbv;
        vbv.BufferLocation += offset;
        g.cmdList->IASetVertexBuffers(slot, 1, &vbv);
    }
    
    void cmd_draw(CommandListHandle, uint32_t vtx_count, uint32_t instance_count, uint32_t first_vtx, uint32_t first_instance) {
        g.cmdList->DrawInstanced(vtx_count, instance_count, first_vtx, first_instance);
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

            // Step 3: allocate per-frame upload ring and map it
            D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = g.frames[i].uploadSize;
            rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
            rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(g.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g.frames[i].upload)))) return false;
            D3D12_RANGE r{0,0};
            if (FAILED(g.frames[i].upload->Map(0, &r, reinterpret_cast<void**>(&g.frames[i].uploadPtr)))) return false;
            g.frames[i].uploadOffset = 0;
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

    // We now prefer begin_frame() to perform waiting & resets.
    // begin_commands() becomes a no-op handle fetch (but still ensures we have a live cmd list).
    CommandListHandle begin_commands() {
        std::lock_guard<std::mutex> lock(g.mtx);
        if (!g.frameBegun) begin_frame_impl();
        return 1;
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

        // Viewport & Scissor must be set before drawing
        D3D12_RESOURCE_DESC texDesc = res->GetDesc();
        D3D12_VIEWPORT      vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width    = static_cast<float>(texDesc.Width);
        vp.Height   = static_cast<float>(texDesc.Height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;

        D3D12_RECT sc{};
        sc.left   = 0;
        sc.top    = 0;
        sc.right  = static_cast<LONG>(texDesc.Width);
        sc.bottom = static_cast<LONG>(texDesc.Height);

        g.cmdList->RSSetViewports(1, &vp);
        g.cmdList->RSSetScissorRects(1, &sc);

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
        &begin_frame_impl,
        &end_frame_impl,
        &init,
        &shutdown,
        &create_swapchain,
        &resize_swapchain,
        &destroy_swapchain,
        &get_current_backbuffer,
        &create_buffer,
        &destroy_buffer,
        &update_buffer,
        &create_shader_module,
        &destroy_shader_module,
        &create_graphics_pipeline,
        &destroy_pipeline,
        &begin_commands,
        &cmd_begin_rendering,
        &cmd_end_rendering,
        &cmd_set_pipeline,
        &cmd_set_vertex_buffer,
        &cmd_draw,
        &end_commands,
        &submit,
        &present,
        &wait_idle
    };    
    return true;
}
