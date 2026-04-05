#define RENDERER_BUILD
#include "d3d12_renderer.h"

#include "d3d12_commands.h"
#include "d3d12_depthmanager.h"
#include "d3d12_descriptors.h"
#include "d3d12_device.h"
#include "d3d12_pipeline.h"
#include "d3d12_resources.h"
#include "d3d12_swapchain.h"
#include "d3d12_upload.h"
#include "d3d12_utils.h"

#include <dxgidebug.h>

using Microsoft::WRL::ComPtr;

static std::unique_ptr<RendererD3D12> g_renderer;

// Helpers
static DXGI_FORMAT ToDxgiFormat(TextureFormat fmt)
{
    switch (fmt) {
        case TextureFormat::BGRA8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::D24S8: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::D32F: return DXGI_FORMAT_D32_FLOAT;
        default: return DXGI_FORMAT_B8G8R8A8_UNORM;
    }
}

static D3D12_COMPARISON_FUNC ConvertDepthFunc(DepthStencilOptions::DepthFunc func)
{
    switch (func) {
        case DepthStencilOptions::DepthFunc::Less: return D3D12_COMPARISON_FUNC_LESS;
        case DepthStencilOptions::DepthFunc::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case DepthStencilOptions::DepthFunc::Greater: return D3D12_COMPARISON_FUNC_GREATER;
        case DepthStencilOptions::DepthFunc::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
        default: return D3D12_COMPARISON_FUNC_LESS;
    }
}

static void Barrier(ID3D12GraphicsCommandList* cl, ID3D12Resource* res,
                    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    if (before == after) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter  = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cl->ResourceBarrier(1, &b);
}

RendererD3D12::RendererD3D12() = default;
RendererD3D12::~RendererD3D12() = default;

FrameContext& RendererD3D12::curFrame()
{
    JAENG_ASSERT(frameIndex_ < frames_.size());
    return *frames_[frameIndex_]; 
}

jaeng::result<> RendererD3D12::executeNow(std::function<void(ID3D12GraphicsCommandList*)>&& command)
{
    ComPtr<ID3D12CommandAllocator> alloc;
    JAENG_CHECK_HRESULT(device_->dev()->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)));

    ComPtr<ID3D12GraphicsCommandList> list;
    JAENG_CHECK_HRESULT(device_->dev()->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)));

    // Run provided commands
    command(list.Get());

    // Close & execute immediately, then wait
    list->Close();
    ID3D12CommandList* lists[] = { list.Get() };
    device_->queue()->ExecuteCommandLists(1, lists);
    auto fv = device_->signal();
    device_->wait(fv);

    return {};
}

jaeng::result<> RendererD3D12::init(const RendererDesc* desc)
{
    std::lock_guard<std::mutex> lock(mtx_);

    hwnd_ = (HWND)desc->platform_window;
    frameCount_ = desc->frame_count ? desc->frame_count : 3;

    UINT factoryFlags = 0;
#if defined(_DEBUG)
    // Enable D3D12 debug layer if available
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
    }

    // Break on errors/corruption from DXGI
    ComPtr<IDXGIInfoQueue> infoq;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&infoq)))) {
        infoq->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, TRUE);
        infoq->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    }

    factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

    JAENG_CHECK_HRESULT(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_)));
    { // Tearing Support (Off for now as it causes crashes after some time running)
        BOOL allowTearing = FALSE;    
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory_.As(&factory5))) {
            if (FAILED(factory5->CheckFeatureSupport( DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing)))) allowTearing = FALSE;
            tearing_ = (allowTearing == TRUE);
        }
        tearing_ = false;
    }

    device_ = std::make_unique<D3D12Device>();
    JAENG_TRY(device_->create(factory_.Get()));

    cpuDesc_ = std::make_unique<DescriptorAllocatorCPU>();
    JAENG_TRY(cpuDesc_->create(device_->dev(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2048));

    dsvDesc_ = std::make_unique<DescriptorAllocatorCPU>();
    JAENG_TRY(dsvDesc_->create(device_->dev(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 256));

    samplerHeapCpu_ = std::make_unique<DescriptorAllocatorCPU>();
    JAENG_TRY(samplerHeapCpu_->create(device_->dev(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 256));

    globalHeap_ = std::make_unique<GlobalDescriptorHeap>();
    JAENG_TRY(globalHeap_->create(device_->dev(), 65536 /*srv*/, 2048 /*sampler*/));

    globalRootSig_ = CreateGlobalRootSignature(device_->dev(), nullptr);
    JAENG_ERROR_IF(!globalRootSig_, jaeng::error_code::invalid_operation, "Failed to create Global Root Signature");

    resources_ = std::make_unique<ResourceTable>();
    pipelines_ = std::make_unique<PipelineTable>();

    // Frames
    frames_.resize(frameCount_);
    uploadPerFrame_.resize(frameCount_);
    gpuDescPerFrame_.resize(frameCount_);
    for (uint32_t i = 0; i < frameCount_; ++i) {
        gpuDescPerFrame_[i] = std::make_unique<DescriptorAllocatorGPU>();
        JAENG_TRY(gpuDescPerFrame_[i]->create(device_->dev(), 1024 /*srv*/, 64 /*sampler*/));

        uploadPerFrame_[i] = std::make_unique<UploadRing>();
        JAENG_TRY(uploadPerFrame_[i]->create(device_->dev(), 8ull * 1024ull * 1024ull /*8 MB ring*/));

        frames_[i] = std::make_unique<FrameContext>();
        JAENG_TRY(frames_[i]->init(device_->dev()));

        frames_[i]->upload = uploadPerFrame_[i].get();
        frames_[i]->gpuDescs = gpuDescPerFrame_[i].get();
    }

    frameIndex_ = 0;
    frameBegun_ = false;

    return {};
}

void RendererD3D12::shutdown()
{
    std::lock_guard<std::mutex> lock(mtx_);

    // Ensure GPU is idle before tearing dow
    wait_idle();

    // Order: pipelines/resources, per-frame, swapchain, descriptors, device
    pipelines_.reset();
    resources_.reset();

    for (auto& f : frames_) f.reset();
    for (auto& g : gpuDescPerFrame_) g.reset();
    for (auto& u : uploadPerFrame_) u.reset();

    if (depthManager_) depthManager_.reset();
    dsvDesc_.reset();

    if (swapchain_) swapchain_->destroy();
    samplerHeapCpu_.reset();
    cpuDesc_.reset();

    if (device_) device_->shutdown();
    factory_.Reset();

    hwnd_       = 0;
    frameCount_ = 0;
    frameIndex_ = 0;
    frameBegun_ = false;

}

void RendererD3D12::begin_frame()
{
    std::lock_guard<std::mutex> lock(mtx_);

    // Determine frame index from swapchain (or keep 0 if not created yet
    frameIndex_ = (swapchain_) ? swapchain_->current_index() : 0;

    // If GPU hasn't completed the last submit for this frame, wait
    auto& fr = curFrame();
    device_->wait(fr.fenceValue);

    // Reset frame-local systems (allocator/cmd list, GPU heaps, upload ring
    fr.reset();             // resets allocator + cmd list for this frame
    fr.gpuDescs->reset();   // resets shader-visible CBV/SRV/UAV & Sampler cursors
    fr.upload->reset();     // resets ring head to 0

    frameBegun_ = true;
}

void RendererD3D12::end_frame()
{
    std::lock_guard<std::mutex> lock(mtx_);

    frameBegun_ = false;
}

jaeng::result<SwapchainHandle> RendererD3D12::create_swapchain(const SwapchainDesc* d)
{
    std::lock_guard<std::mutex> lock(mtx_);
    JAENG_ERROR_IF(!hwnd_, jaeng::error_code::resource_not_ready, "[Renderer] No Window Handle");

    // Create & build RTVs internally
    swapchain_ = std::make_unique<D3D12Swapchain>();
    JAENG_TRY(swapchain_->create(hwnd_, factory_.Get(), device_->dev(), device_->queue(),
              ToDxgiFormat(d->format), d->size.width, d->size.height, frameCount_, tearing_));

    // Register backbuffers as textures in ResourceTable and cache handles for get_current_backbuffer()
    backbufferHandles_.clear();
    backbufferHandles_.reserve(frameCount_);
    for (uint32_t i = 0; i < frameCount_; ++i) {
        TextureRec t{};
        t.res     = swapchain_->rtv_resource(i);     // backbuffer resource
        t.state   = D3D12_RESOURCE_STATE_PRESENT;
        t.width   = d->size.width;
        t.height  = d->size.height;
        // (No SRV for backbuffer; rendering targets use RTV via swapchain)
        TextureHandle h = resources_->add_texture(std::move(t));
        backbufferHandles_.push_back(h);
    }

    if (d->depth_stencil.depth_enable) {
        // Depth Manager: create depth buffer for this swapchain size/format
        UINT dsvIndex;
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDesc_->allocate(&dsvIndex);
        depthManager_ = std::make_unique<DepthManager>(device_->dev(), dsvHandle);
        JAENG_TRY(depthManager_->init(d->size.width, d->size.height, ToDxgiFormat(d->depth_stencil.depth_format)));
    }

    return 1; // Single id for this starter
}

jaeng::result<> RendererD3D12::resize_swapchain(SwapchainHandle, Extent2D newSize)
{
    if (newSize.width == 0 || newSize.height == 0) return {};
    
    // Not safe to take the lock if presenting
    JAENG_ERROR_IF(state_ == RendererState::Presenting, jaeng::error_code::resource_not_ready, "[Renderer] Swapchain is presenting, defer this call.");
    std::lock_guard<std::mutex> lock(mtx_);
    JAENG_ERROR_IF(!swapchain_, jaeng::error_code::no_resource, "[Renderer] No Swapchain.");

    // --- Pause presents and quiesce GPU before resizing ---
    // Ensure no new presents can happen (mtx_ held). Now flush GPU work touching backbuffers.
    wait_idle(); // uses fence: device_->signal(); device_->wait(v);

    // Release RTV from resource table before resizing
    for (uint32_t i = 0; i < frameCount_ && i < backbufferHandles_.size(); ++i) {
        if (auto* tex = resources_->get_tex(backbufferHandles_[i])) tex->res.Reset();
    }

    // Resize backbuffers & RTVs inside swapchain
    JAENG_TRY(swapchain_->resize(device_->dev(), newSize.width, newSize.height, tearing_));

    // Refresh resource table entries for backbuffers
    for (uint32_t i = 0; i < frameCount_ && i < backbufferHandles_.size(); ++i)
    {
        if (auto* tex = resources_->get_tex(backbufferHandles_[i])) {
            tex->res    = swapchain_->rtv_resource(i);
            tex->width  = newSize.width;
            tex->height = newSize.height;
            tex->state  = D3D12_RESOURCE_STATE_PRESENT;
        }
    }

    // Resize depth buffer to match new size
    if (depthManager_) {
        JAENG_TRY(depthManager_->resize(newSize.width, newSize.height));
    }

    return {};
}

void RendererD3D12::destroy_swapchain(SwapchainHandle)
{
    std::lock_guard<std::mutex> lock(mtx_);
    backbufferHandles_.clear();
    if (swapchain_) swapchain_->destroy();
}

TextureHandle RendererD3D12::get_current_backbuffer(SwapchainHandle)
{
    std::lock_guard<std::mutex> lock(mtx_);
    const uint32_t idx = swapchain_->current_index();
    return (idx < backbufferHandles_.size()) ? backbufferHandles_[idx] : 0;

}

jaeng::result<BufferHandle> RendererD3D12::create_buffer(const BufferDesc* d, const void* initial)
{
    // Create DEFAULT-heap resource; if initial != null, stage via UploadRing and copy
    BufferRec buf{};
    buf.size = d->size_bytes;
    buf.usage = d->usage;

    // Uniform Buffers are 256byte aligned (limiting to 64k as well)
    if (d->usage & BufferUsage_Uniform) {
        buf.size = ((buf.size + 255ull) & ~255ull);
        if (buf.size == 0) buf.size = 256;
        if (buf.size > 64u * 1024u) buf.size = 64u * 1024u; // CBV max size
    }

    // Create committed buffer resource (COMMON initially)
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = buf.size;
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // Start in COMMON; we'll transition as needed
    JAENG_CHECK_HRESULT(device_->dev()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buf.res)));
    buf.state = D3D12_RESOURCE_STATE_COMMON;

    // Convenience: set VBV/IBV if usage flags indicate it (stride will be overridden by pipeline)
    if (d->usage & BufferUsage_Vertex) {
        buf.vbv.BufferLocation = buf.res->GetGPUVirtualAddress();
        buf.vbv.SizeInBytes    = (UINT)buf.size;
        buf.vbv.StrideInBytes  = 32; // default; refined by cmd_set_vertex_buffer
    }

    // IBV setup (default to 32-bit; actual format picked at bind time)
    if (d->usage & BufferUsage_Index) {
        buf.ibv.BufferLocation = buf.res->GetGPUVirtualAddress();
        buf.ibv.SizeInBytes    = (UINT)buf.size;
        // buf.ibv.Format set at cmd_set_index_buffer based on `index32`
    }

    if (d->usage & BufferUsage_Uniform) {
        // Build a CPU CBV once and cache it
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
        cbv.BufferLocation = buf.res->GetGPUVirtualAddress();
        cbv.SizeInBytes    = (UINT)buf.size;

        D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpuDesc_->allocate(nullptr);  // CPU CBV/SRV/UAV heap
        device_->dev()->CreateConstantBufferView(&cbv, cpu);            // write once
        buf.cbvCpu      = cpu;
        buf.cbvCpuValid = true;
    }

    auto buf_size = buf.size;
    auto h = resources_->add_buffer(std::move(buf));

    // Optional initial upload (record into current frame)
    if (initial && buf_size) {
        JAENG_TRY(update_buffer(h, 0, initial, buf_size));
    }

    return h;
}

void RendererD3D12::destroy_buffer(BufferHandle h)
{
    if (BufferRec* buf = resources_->get_buf(h)) {
        buf->res.Reset();
    }
}

jaeng::result<> RendererD3D12::update_buffer(BufferHandle h, uint64_t dst_off, const void* data, uint64_t size)
{
    if (!data || size == 0) return {};
    auto* b = resources_->get_buf(h);
    JAENG_ERROR_IF(!b, jaeng::error_code::no_resource, "[Renderer] No buffer to update");

    FrameContext& fr = curFrame();

    // Helper to Generate the required Action to be executed on the CommandList
    auto actionGen = [](BufferRec& b, const UploadSlice& us, uint64_t dst_off, uint64_t size) {
        return [&b, us, dst_off, size](ID3D12GraphicsCommandList* list) {
            // Ensure COPY_DEST
            Barrier(list, b.res.Get(), b.state, D3D12_RESOURCE_STATE_COPY_DEST);
            b.state = D3D12_RESOURCE_STATE_COPY_DEST;
            list->CopyBufferRegion(b.res.Get(), dst_off, us.resource, us.offset, size);
            // Leave buffer in COPY_DEST; cmd_set_vertex_buffer will transition to VERTEX when binding
        };
    };

    if (frameBegun_) {
        // ---- Fast path: record into the active frame's command list using the per-frame ring ----
        JAENG_TRY_ASSIGN(UploadSlice us, fr.upload->stage(data, size, 256ull));
        actionGen(*b, us, dst_off, size)(fr.cmd());
    }
    else {
        // ---- Robust path: no frame begun -> perform an immediate one-shot copy and wait ----
        UploadRing up;
        JAENG_TRY(up.create(device_->dev(), size));
        JAENG_TRY_ASSIGN(UploadSlice us, up.stage(data, size, 256ull));
        JAENG_TRY(executeNow(actionGen(*b, us, dst_off, size)));
    }
    return {};
}

jaeng::result<TextureHandle> RendererD3D12::create_texture(const TextureDesc* td, const void* initial)
{
    TextureRec t {
        .width  = td->width,
        .height = td->height
    };

    D3D12_RESOURCE_DESC rd {
        .Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Width            = td->width,
        .Height           = td->height,
        .DepthOrArraySize = 1,
        .MipLevels        = (UINT16)td->mip_levels,
        .Format           = (td->format == TextureFormat::BGRA8_UNORM) ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM,
        .SampleDesc       = { .Count = 1 },
        .Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN
    };

    D3D12_HEAP_PROPERTIES hp{ .Type = D3D12_HEAP_TYPE_DEFAULT };
    JAENG_CHECK_HRESULT(device_->dev()->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&t.res)));
    t.state = D3D12_RESOURCE_STATE_COPY_DEST;

    // If initial data provided, do one-shot upload (either in-frame or immediate list)
    if (initial) {
        // Build an upload buffer with row-aligned footprints
        UINT64 totalBytes = 0; D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT rows = 0; UINT64 rowSizeInBytes = 0;
        device_->dev()->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rowSizeInBytes, &totalBytes);

        UploadRing up;
        JAENG_TRY(up.create(device_->dev(), totalBytes));
        JAENG_TRY_ASSIGN(UploadSlice us, up.stage_pitched(static_cast<const uint8_t*>(initial), rows, td->width * 4 /*RGBA8*/, fp));

        auto doCopy = [&](ID3D12GraphicsCommandList* cl) {
            // Transition and copy
            Barrier(cl, t.res.Get(), t.state, D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION dstLoc {
                .pResource        = t.res.Get(),
                .Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                .SubresourceIndex = 0
            };
            D3D12_TEXTURE_COPY_LOCATION srcLoc {
                .pResource       = us.resource,
                .Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
                .PlacedFootprint = fp
            };
            cl->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
            Barrier(cl, t.res.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            t.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        };

        if (frameBegun_) {
            // We are already recording into g.cmdList this frame
            doCopy(curFrame().cmd());
        } else {
            // No frame yet -> execute a tiny one-shot list and wait
            JAENG_TRY(executeNow(std::move(doCopy)));
        }
    }

    // Create SRV in CPU heap (permanent)
    D3D12_SHADER_RESOURCE_VIEW_DESC srv {
        .Format                  = rd.Format,
        .ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Texture2D               = { .MipLevels = td->mip_levels }
    };

    D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpuDesc_->allocate(nullptr);
    device_->dev()->CreateShaderResourceView(t.res.Get(), &srv, cpu);
    t.srvCpu = cpu;

    // Create SRV in Global Heap (Persistent)
    t.descriptorIndex = globalHeap_->allocate_srv();
    device_->dev()->CreateShaderResourceView(t.res.Get(), &srv, globalHeap_->cpu_srv(t.descriptorIndex));

    return resources_->add_texture(std::move(t));
}

void RendererD3D12::destroy_texture(TextureHandle h)
{
    if (TextureRec* tex = resources_->get_tex(h)) {
        tex->res.Reset();
    }
}

SamplerHandle RendererD3D12::create_sampler(const SamplerDesc* sd)
{
    D3D12_SAMPLER_DESC d{};
    d.Filter   = (sd->filter == SamplerFilter::Nearest) ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    d.AddressU = (sd->address_u == AddressMode::Repeat) ? D3D12_TEXTURE_ADDRESS_MODE_WRAP :
                (sd->address_u == AddressMode::Mirror) ? D3D12_TEXTURE_ADDRESS_MODE_MIRROR : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    d.AddressV = d.AddressU;
    d.AddressW = d.AddressU;
    d.MinLOD   = sd->min_lod; d.MaxLOD = sd->max_lod; d.MipLODBias = sd->mip_lod_bias;
    d.MaxAnisotropy = 1;
    d.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    d.BorderColor[0] = sd->border_color[0];
    d.BorderColor[1] = sd->border_color[1];
    d.BorderColor[2] = sd->border_color[2];
    d.BorderColor[3] = sd->border_color[3];

    UINT idx = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = samplerHeapCpu_->allocate(&idx);
    device_->dev()->CreateSampler(&d, cpu);

    SamplerRec s{};
    s.cpu = cpu;

    // Create Sampler in Global Heap (Persistent)
    s.descriptorIndex = globalHeap_->allocate_samp();
    device_->dev()->CreateSampler(&d, globalHeap_->cpu_samp(s.descriptorIndex));

    return resources_->add_sampler(std::move(s));
}

void RendererD3D12::destroy_sampler(SamplerHandle)
{
    // linear CPU heap => no free; TODO: resource table should release records
}

uint32_t RendererD3D12::get_texture_index(TextureHandle h)
{
    if (auto* tex = resources_->get_tex(h)) return tex->descriptorIndex;
    return 0;
}

uint32_t RendererD3D12::get_sampler_index(SamplerHandle h)
{
    if (auto* smp = resources_->get_samp(h)) return smp->descriptorIndex;
    return 0;
}

ShaderModuleHandle RendererD3D12::create_shader_module(const ShaderModuleDesc* d)
{
    ShaderBlob sb{};
    sb.bytes.assign((const uint8_t*)d->data, (const uint8_t*)d->data + d->size);
    return pipelines_->add_shader(std::move(sb));
}

void RendererD3D12::destroy_shader_module(ShaderModuleHandle h)
{
    pipelines_->del_shader(h);
}

VertexLayoutHandle RendererD3D12::create_vertex_layout(const VertexLayoutDesc* vld)
{
    std::vector<D3D12_INPUT_ELEMENT_DESC> ils;
    ils.reserve(vld->attribute_count);
    for (uint32_t i = 0; i < vld->attribute_count; ++i) {
        const auto& a = vld->attributes[i];
        D3D12_INPUT_ELEMENT_DESC e{};
        e.SemanticName = (a.location == 0) ? "POSITION" : (a.location == 1) ? "COLOR" : "TEXCOORD";
        e.SemanticIndex = 0;
        e.Format = (a.location == 2) ? DXGI_FORMAT_R32G32_FLOAT : DXGI_FORMAT_R32G32B32_FLOAT;
        e.InputSlot = 0;
        e.AlignedByteOffset = a.offset;
        e.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        ils.push_back(e);
    }

    vertexLayouts_.emplace_back(std::move(ils), vld->stride);
    return (VertexLayoutHandle)(vertexLayouts_.size() - 1);
}

PipelineHandle RendererD3D12::create_graphics_pipeline(const GraphicsPipelineDesc* gp)
{
    // Use Global Root Signature
    ComPtr<ID3D12RootSignature> root = globalRootSig_;
    if (!root) return 0;

    auto* vsb = pipelines_->get_shader(gp->vs);
    auto* psb = pipelines_->get_shader(gp->fs);
    if (!vsb) return 0;

    if (gp->vertex_layout >= vertexLayouts_.size()) return 0;
    auto& vld = vertexLayouts_[gp->vertex_layout];

    // PSO defaults
    D3D12_BLEND_DESC blend{};
    auto& rt0 = blend.RenderTarget[0];
    rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC rast{};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    rast.CullMode = D3D12_CULL_MODE_FRONT; // Flip to see if it fixes inversion
    rast.DepthClipEnable = TRUE;

    D3D12_DEPTH_STENCIL_DESC depth{};
    if (depthManager_ && gp->depth_stencil.enableDepth) {
        depth.DepthEnable = TRUE;
        depth.DepthFunc = ConvertDepthFunc(gp->depth_stencil.depthFunc);
        depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    }
    depth.StencilEnable = FALSE; // gp->depth_stencil.enableStencil;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature   = root.Get();
    pso.VS               = { vsb->bytes.data(), vsb->bytes.size() };
    if (psb) pso.PS      = { psb->bytes.data(), psb->bytes.size() };
    pso.BlendState       = blend;
    pso.SampleMask       = UINT_MAX;
    pso.RasterizerState  = rast;
    pso.DepthStencilState= depth;
    pso.DSVFormat        = (depth.DepthEnable) ? depthManager_->get_format() : DXGI_FORMAT_UNKNOWN;
    pso.InputLayout      = { vld.ieds.data(), (UINT)vld.ieds.size() };
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0]    = DXGI_FORMAT_B8G8R8A8_UNORM; // Force format for now to ensure it matches swapchain
    pso.SampleDesc.Count = 1;

    PipelineRec rec{};
    rec.root = root;

    if (FAILED(device_->dev()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&rec.pso)))) return 0;
    rec.topo         = (gp->topology == PrimitiveTopology::TriangleStrip) ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP :
                       (gp->topology == PrimitiveTopology::LineList)     ? D3D_PRIMITIVE_TOPOLOGY_LINELIST :
                                                                           D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    rec.vertexStride = vld.stride;

    return pipelines_->add_pipeline(std::move(rec));
}

void RendererD3D12::destroy_pipeline(PipelineHandle h)
{
    pipelines_->del_pipeline(h);
}

CommandListHandle RendererD3D12::begin_commands()
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (!frameBegun_) begin_frame();
    return 1; // single list in this backend (FrameContext::cmd())
}

void RendererD3D12::cmd_begin_pass(CommandListHandle, LoadOp load_op,
                                  const ColorAttachmentDesc* colors, uint32_t count,
                                  const DepthAttachmentDesc* depth)
{
    JAENG_ASSERT(count >= 1 && colors);
    auto& fc = curFrame();
    auto* cl = fc.cmd();

    // Set Global Heaps
    ID3D12DescriptorHeap* heaps[2] = { globalHeap_->srv_heap(), globalHeap_->samp_heap() };
    cl->SetDescriptorHeaps(2, heaps);

    // For now we only support rendering to the swapchain backbuffer.
    TextureHandle bbHandle = get_current_backbuffer(/*swapchain*/1);
    auto* tex = resources_->get_tex(bbHandle);
    if (!tex || !tex->res) return;

    // Transition to RENDER_TARGET if needed
    Barrier(cl, tex->res.Get(), tex->state, D3D12_RESOURCE_STATE_RENDER_TARGET);
    tex->state = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // Bind RTV and Depth
    const uint32_t idx = swapchain_->current_index();
    auto rtv = swapchain_->rtv_handle(idx);
    D3D12_CPU_DESCRIPTOR_HANDLE* dsvPtr = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};

    if (depthManager_ && depth) {
        dsv = depthManager_->get_dsv();
        Barrier(cl, depthManager_->dsv_resource(), depthManager_->resState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        depthManager_->resState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        dsvPtr = &dsv;
    }
    
    cl->OMSetRenderTargets(1, &rtv, FALSE, dsvPtr);

    // Viewport/scissor from backbuffer size
    D3D12_RESOURCE_DESC texDesc = tex->res->GetDesc();
    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f; vp.TopLeftY = 0.0f;
    vp.Width    = float(texDesc.Width);
    vp.Height   = float(texDesc.Height);
    vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;

    D3D12_RECT sc{0, 0, (LONG)texDesc.Width, (LONG)texDesc.Height};
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &sc);

    // Clear if requested
    if (load_op == LoadOp::Clear) {
        cl->ClearRenderTargetView(rtv, colors[0].clear_rgba, 0, nullptr);
        if (depthManager_ && depth) {
            cl->ClearDepthStencilView(depthManager_->get_dsv(), D3D12_CLEAR_FLAG_DEPTH, depth->clear_d, 0, 0, nullptr);
        }
    }
}

void RendererD3D12::cmd_end_pass(CommandListHandle)
{
    // No-op for single-list backbuffer rendering
}

void RendererD3D12::cmd_bind_uniform(CommandListHandle, uint32_t slot, BufferHandle h, uint64_t offset)
{
    auto& fc = curFrame();
    auto* cl = fc.cmd();

    BufferRec* buf = resources_->get_buf(h);
    if (!buf || !buf->res || !(buf->usage & BufferUsage_Uniform)) return;

    // slot 0 -> register b1, slot 1 -> register b2
    UINT rootParam = slot + 1;
    if (rootParam > 2) return; 

    cl->SetGraphicsRootConstantBufferView(rootParam, buf->res->GetGPUVirtualAddress() + offset);

    // Make sure the buffer is in CBV-readable state
    Barrier(cl, buf->res.Get(), buf->state, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    buf->state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
}

void RendererD3D12::cmd_push_constants(CommandListHandle, uint32_t offset, uint32_t count, const void* data)
{
    auto* cl = curFrame().cmd();
    cl->SetGraphicsRoot32BitConstants(0, count, data, offset);
}

void RendererD3D12::cmd_set_pipeline(CommandListHandle, PipelineHandle h)
{
    if (auto* p = pipelines_->get_pipeline(h)) {
        auto* cl = curFrame().cmd();
        cl->SetGraphicsRootSignature(globalRootSig_.Get());
        
        // Root parameters are undefined after SetGraphicsRootSignature!
        // Re-bind descriptor tables
        cl->SetGraphicsRootDescriptorTable(3, globalHeap_->gpu_srv(0));
        cl->SetGraphicsRootDescriptorTable(4, globalHeap_->gpu_samp(0));

        cl->IASetPrimitiveTopology(p->topo);
        cl->SetPipelineState(p->pso.Get());
        currentVertexStride_ = p->vertexStride;
    }
}

void RendererD3D12::cmd_set_vertex_buffer(CommandListHandle, uint32_t slot, BufferHandle b, uint64_t offset)
{
    auto* buf = resources_->get_buf(b);
    if (!buf) return;

    // Transition to VERTEX/CONSTANT and bind (stride refined by pipeline)
    auto* cl = curFrame().cmd();
    Barrier(cl, buf->res.Get(), buf->state, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    buf->state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;

    D3D12_VERTEX_BUFFER_VIEW vbv = buf->vbv;
    if (currentVertexStride_ != 0)
        vbv.StrideInBytes = currentVertexStride_;

    vbv.BufferLocation += offset;
    cl->IASetVertexBuffers(slot, 1, &vbv);
}

void RendererD3D12::cmd_set_index_buffer(CommandListHandle, BufferHandle h, bool index32, uint64_t offset)
{
    auto* buf = resources_->get_buf(h);
    if (!buf) return;

    auto* cl = curFrame().cmd();

    // Transition COPY/COMMON -> INDEX_BUFFER when binding
    Barrier(cl, buf->res.Get(), buf->state, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    buf->state = D3D12_RESOURCE_STATE_INDEX_BUFFER;

    // Build the view (respect offset and remaining size)
    D3D12_INDEX_BUFFER_VIEW ibv = buf->ibv;
    ibv.BufferLocation += offset;
    ibv.SizeInBytes     = (UINT)std::max<uint64_t>(0, buf->size - offset);
    ibv.Format          = index32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;

    cl->IASetIndexBuffer(&ibv);
}

void RendererD3D12::cmd_draw(CommandListHandle, uint32_t vtx_count, uint32_t inst_count,
                             uint32_t first_vtx, uint32_t first_inst)
{
    curFrame().cmd()->DrawInstanced(vtx_count, inst_count, first_vtx, first_inst);
}

void RendererD3D12::cmd_draw_indexed(CommandListHandle, uint32_t idx_count, uint32_t inst_count,
                                     uint32_t first_idx, int32_t vtx_offset, uint32_t first_inst)
{
    curFrame().cmd()->DrawIndexedInstanced(idx_count, inst_count, first_idx, vtx_offset, first_inst);
}

void RendererD3D12::end_commands(CommandListHandle) {
    auto* cl = curFrame().cmd();

    // Final backbuffer transition to PRESENT
    {
        auto bbHandle = get_current_backbuffer(/*swapchain*/1);
        if (auto* tex = resources_->get_tex(bbHandle)) {
            if (tex->res) {
                Barrier(cl, tex->res.Get(), tex->state, D3D12_RESOURCE_STATE_PRESENT);
                tex->state = D3D12_RESOURCE_STATE_PRESENT;
            }
        }
    }

    cl->Close();

    // Reflect implicit decay to COMMON for buffers after ExecuteCommandLists
    // (We will submit right after, so the cache should match the runtime.)
    // If you prefer finer control, track only buffers used in this list.
    resources_->on_all_buffers([](BufferRec& b) {
        if (b.res) {
            // Most read-only buffer states will decay; COMMON is a safe cache reset
            b.state = D3D12_RESOURCE_STATE_COMMON;
        }
    });
}

void RendererD3D12::submit(CommandListHandle* /*lists*/, uint32_t /*list_count*/)
{
    auto* cl = curFrame().cmd();
    
    ID3D12CommandList* one[] = { cl };
    device_->queue()->ExecuteCommandLists(1, one);
    
    // Signal fence and store value for this frame, so begin_frame can wait next time
    auto fv = device_->signal();
    auto& fc = curFrame();
    fc.fenceValue = fv;
}

void RendererD3D12::present(SwapchainHandle)
{
    std::lock_guard<std::mutex> lock(mtx_);
    state_ = RendererState::Presenting;
    // Present (respect tearing flag)
    UINT syncInterval = (tearing_)? 0 : 1;
    swapchain_->swap()->Present(syncInterval, (tearing_)? DXGI_PRESENT_ALLOW_TEARING : 0);
    state_ = RendererState::Rendering;
}

void RendererD3D12::wait_idle()
{
    const UINT64 v = device_->signal();
    device_->wait(v);
}

// ... (implement methods by delegating to subsystems; examples further below)

extern "C" RENDERER_API bool LoadRenderer(RendererAPI* out_api) {
    if (!out_api) return false;
    g_renderer = std::make_unique<RendererD3D12>();

    // Fill function table with static lambdas forwarding to instance
    static RendererAPI api = {};

    api.init       = [](const RendererDesc* d) { return g_renderer->init(d).logError().has_value(); };
    api.shutdown   = []{ g_renderer->shutdown(); };

    api.begin_frame = []{ g_renderer->begin_frame(); };
    api.end_frame   = []{ g_renderer->end_frame(); };

    api.create_swapchain = [](const SwapchainDesc* d) { return g_renderer->create_swapchain(d).orValue(0); };
    api.resize_swapchain = [](SwapchainHandle s, Extent2D e) { if(!g_renderer->resize_swapchain(s,e).logError()); };
    api.destroy_swapchain= [](SwapchainHandle s) { g_renderer->destroy_swapchain(s); };
    api.get_current_backbuffer = [](SwapchainHandle s) { return g_renderer->get_current_backbuffer(s); };
    api.get_depth_buffer = [](SwapchainHandle s) { return (TextureHandle)1; }; // Special handle for swapchain depth

    api.create_buffer = [](const BufferDesc* d, const void* p) { return g_renderer->create_buffer(d,p).orValue(0); };
    api.destroy_buffer= [](BufferHandle h) { g_renderer->destroy_buffer(h); };
    api.update_buffer = [](BufferHandle h, uint64_t o, const void* p, uint64_t sz) { return g_renderer->update_buffer(h,o,p,sz).logError().has_value(); };

    api.create_texture = [](const TextureDesc* d, const void* p) { return g_renderer->create_texture(d,p).orValue(0); };
    api.destroy_texture= [](TextureHandle h) { g_renderer->destroy_texture(h); };
    api.create_sampler = [](const SamplerDesc* d) { return g_renderer->create_sampler(d); };
    api.destroy_sampler= [](SamplerHandle h) { g_renderer->destroy_sampler(h); };

    api.get_texture_index = [](TextureHandle h) { return g_renderer->get_texture_index(h); };
    api.get_sampler_index = [](SamplerHandle h) { return g_renderer->get_sampler_index(h); };

    api.create_shader_module = [](const ShaderModuleDesc* d) { return g_renderer->create_shader_module(d); };
    api.destroy_shader_module= [](ShaderModuleHandle h) { g_renderer->destroy_shader_module(h); };
    api.create_vertex_layout = [](const VertexLayoutDesc* d){ return g_renderer->create_vertex_layout(d); };
    api.create_graphics_pipeline = [](const GraphicsPipelineDesc* d) { return g_renderer->create_graphics_pipeline(d); };
    api.destroy_pipeline= [](PipelineHandle h) { g_renderer->destroy_pipeline(h); };

    api.begin_commands = []{ return g_renderer->begin_commands(); };
    api.cmd_begin_pass = [](CommandListHandle c, LoadOp op, const ColorAttachmentDesc* cs, uint32_t n, const DepthAttachmentDesc* dp){
        g_renderer->cmd_begin_pass(c, op, cs, n, dp);
    };
    api.cmd_end_pass = [](CommandListHandle c) { g_renderer->cmd_end_pass(c); };

    api.cmd_bind_uniform = [](CommandListHandle c, uint32_t s, BufferHandle h, uint64_t o) { g_renderer->cmd_bind_uniform(c, s, h, o); };
    api.cmd_push_constants = [](CommandListHandle c, uint32_t off, uint32_t count, const void* d) { g_renderer->cmd_push_constants(c, off, count, d); };
    api.cmd_barrier = [](CommandListHandle c, BufferHandle b, uint32_t src, uint32_t dst) { /* no-op for now */ };

    api.cmd_set_pipeline = [](CommandListHandle c, PipelineHandle p) { g_renderer->cmd_set_pipeline(c,p); };
    api.cmd_set_vertex_buffer = [](CommandListHandle c, uint32_t s, BufferHandle b, uint64_t o) { g_renderer->cmd_set_vertex_buffer(c,s,b,o); };
    api.cmd_set_index_buffer = [](CommandListHandle c, BufferHandle b, bool i32, uint64_t o) { g_renderer->cmd_set_index_buffer(c,b,i32,o); };

    api.cmd_draw = [](CommandListHandle c, uint32_t vc, uint32_t ic, uint32_t fv, uint32_t first) {
        g_renderer->cmd_draw(c, vc, ic, fv, first);
    };
    api.cmd_draw_indexed = [](CommandListHandle c, uint32_t in, uint32_t ic, uint32_t fi, int32_t o, uint32_t first) {
        g_renderer->cmd_draw_indexed(c, in, ic, fi, o, first);
    };

    api.end_commands = [](CommandListHandle c) { g_renderer->end_commands(c); };
    api.submit = [](CommandListHandle* l, uint32_t n) { g_renderer->submit(l,n); };
    api.present= [](SwapchainHandle s) { g_renderer->present(s); };
    api.wait_idle = []{ g_renderer->wait_idle(); };

    *out_api = api;
    return true;
}
