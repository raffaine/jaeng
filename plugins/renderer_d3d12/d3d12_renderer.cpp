#define RENDERER_BUILD
#include "d3d12_renderer.h"

#include "d3d12_bind.h"
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

    resources_ = std::make_unique<ResourceTable>();
    pipelines_ = std::make_unique<PipelineTable>();
    binds_     = std::make_unique<BindSpace>();
    JAENG_TRY(binds_->init(device_->dev(), cpuDesc_.get()));

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

    // Order: bind space, pipelines/resources, per-frame, swapchain, descriptors, device
    if (binds_) binds_->shutdown();
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
    std::lock_guard<std::mutex> lock(mtx_);
    JAENG_ERROR_IF(!swapchain_, jaeng::error_code::resource_not_ready, "[Renderer] No Swapchain.");

    // --- Pause presents and quiesce GPU before resizing ---
    // Ensure no new presents can happen (mtx_ held). Now flush GPU work touching backbuffers.
    wait_idle(); // uses fence: device_->signal(); device_->wait(v);

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

    // Create committed buffer resource (COMMON initially)
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = d->size_bytes;
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
        buf.vbv.SizeInBytes    = (UINT)d->size_bytes;
        buf.vbv.StrideInBytes  = 32; // default; refined by cmd_set_vertex_buffer
    }

    // IBV setup (default to 32-bit; actual format picked at bind time)
    if (d->usage & BufferUsage_Index) {
        buf.ibv.BufferLocation = buf.res->GetGPUVirtualAddress();
        buf.ibv.SizeInBytes    = (UINT)d->size_bytes;
        // buf.ibv.Format set at cmd_set_index_buffer based on `index32`
    }

    auto h = resources_->add_buffer(std::move(buf));

    // Optional initial upload (record into current frame)
    if (initial && d->size_bytes) {
        JAENG_TRY(update_buffer(h, 0, initial, d->size_bytes));
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

    bool staged = false;
    if (frameBegun_) {
        // ---- Fast path: record into the active frame's command list using the per-frame ring ----
        if (auto us = fr.upload->stage(data, size, 256ull).logError(); us.has_value()) {
            // Ensure COPY_DEST
            Barrier(fr.cmd(), b->res.Get(), b->state, D3D12_RESOURCE_STATE_COPY_DEST);
            b->state = D3D12_RESOURCE_STATE_COPY_DEST;
            // Enqueue copy
            fr.cmd()->CopyBufferRegion(b->res.Get(), dst_off, us->resource, us->offset, size);
            staged = true;
        }
    }

    if (!staged) {
        // ---- Robust path: no frame begun -> perform an immediate one-shot copy and wait ----
        // Create a transient upload resource
        D3D12_HEAP_PROPERTIES hpUp{}; hpUp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC   upDesc{};
        upDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        upDesc.Width            = size;
        upDesc.Height           = 1;
        upDesc.DepthOrArraySize = 1;
        upDesc.MipLevels        = 1;
        upDesc.SampleDesc.Count = 1;
        upDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> upload;
        JAENG_CHECK_HRESULT(device_->dev()->CreateCommittedResource(
                &hpUp, D3D12_HEAP_FLAG_NONE, &upDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)));

        // Fill upload
        uint8_t* upPtr = nullptr; D3D12_RANGE r{0,0};
        JAENG_CHECK_HRESULT(upload->Map(0, &r, reinterpret_cast<void**>(&upPtr)));
        std::memcpy(upPtr, data, size);
        upload->Unmap(0, nullptr);

        // One-shot command list to do the copy now
        ComPtr<ID3D12CommandAllocator> alloc;
        JAENG_CHECK_HRESULT(device_->dev()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)));

        ComPtr<ID3D12GraphicsCommandList> list;
        JAENG_CHECK_HRESULT(device_->dev()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)));

        // Transition and copy
        Barrier(list.Get(), b->res.Get(), b->state, D3D12_RESOURCE_STATE_COPY_DEST);
        b->state = D3D12_RESOURCE_STATE_COPY_DEST;
        list->CopyBufferRegion(b->res.Get(), dst_off, upload.Get(), 0, size);

        // Close & execute immediately, then wait
        list->Close();
        ID3D12CommandList* lists[] = { list.Get() };
        device_->queue()->ExecuteCommandLists(1, lists);
        auto fv = device_->signal();
        device_->wait(fv);
    }
    // Leave buffer in COPY_DEST; cmd_set_vertex_buffer will transition to VERTEX when binding
    return {};
}

jaeng::result<TextureHandle> RendererD3D12::create_texture(const TextureDesc* td, const void* initial)
{
    TextureRec t{};
    t.width  = td->width;
    t.height = td->height;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width              = td->width;
    rd.Height             = td->height;
    rd.DepthOrArraySize   = 1;
    rd.MipLevels          = (UINT16)td->mip_levels;
    rd.Format             = (td->format == TextureFormat::BGRA8_UNORM) ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count   = 1;
    rd.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    JAENG_CHECK_HRESULT(device_->dev()->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&t.res)));
    t.state = D3D12_RESOURCE_STATE_COPY_DEST;

    // If initial data provided, do one-shot upload (either in-frame or immediate list)
    if (initial) {
        // Build an upload buffer with row-aligned footprints
        UINT64 totalBytes = 0; D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT rows = 0; UINT64 rowSizeInBytes = 0;
        device_->dev()->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rowSizeInBytes, &totalBytes);

        D3D12_HEAP_PROPERTIES hpUp{}; hpUp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC upDesc{};
        upDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upDesc.Width = totalBytes; upDesc.Height = 1; upDesc.DepthOrArraySize = 1; upDesc.MipLevels = 1;
        upDesc.SampleDesc.Count = 1; upDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> upload;
        JAENG_CHECK_HRESULT(device_->dev()->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &upDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)));

        // Fill upload
        uint8_t* upPtr = nullptr; D3D12_RANGE r{0,0};
        upload->Map(0, &r, reinterpret_cast<void**>(&upPtr));
        const uint8_t* src = static_cast<const uint8_t*>(initial);
        size_t srcPitch = size_t(td->width) * 4; // RGBA8
        for (UINT y = 0; y < rows; ++y) {
            std::memcpy(upPtr + fp.Offset + y * fp.Footprint.RowPitch, src + y * srcPitch, srcPitch);
        }
        upload->Unmap(0, nullptr);

        auto doCopy = [&](ID3D12GraphicsCommandList* cl) {
            // Transition and copy
            Barrier(cl, t.res.Get(), t.state, D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION dstLoc{}; dstLoc.pResource = t.res.Get();
            dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dstLoc.SubresourceIndex = 0;
            D3D12_TEXTURE_COPY_LOCATION srcLoc{}; srcLoc.pResource = upload.Get();
            srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; srcLoc.PlacedFootprint = fp;
            cl->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
            Barrier(cl, t.res.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        };

        if (frameBegun_) {
            // We are already recording into g.cmdList this frame
            doCopy(curFrame().cmd());
        } else {
            // No frame yet -> execute a tiny one-shot list and wait
            ComPtr<ID3D12CommandAllocator> alloc;
            JAENG_CHECK_HRESULT(device_->dev()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)));
            ComPtr<ID3D12GraphicsCommandList> list;
            JAENG_CHECK_HRESULT(device_->dev()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)));

            doCopy(list.Get());
            list->Close();

            ID3D12CommandList* lists[] = { list.Get() };
            device_->queue()->ExecuteCommandLists(1, lists);
            auto fv = device_->signal();
            device_->wait(fv);
        }
        t.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // Create SRV in CPU heap (permanent)
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = rd.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = td->mip_levels;

    UINT idx = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpuDesc_->allocate(&idx);
    device_->dev()->CreateShaderResourceView(t.res.Get(), &srv, cpu);
    t.srvCpu = cpu;

    return resources_->add_texture(std::move(t));
}

void RendererD3D12::destroy_texture(TextureHandle h)
{
    if (TextureRec* tex = resources_->get_tex(h)) {
        tex->res.Reset();
    }
}

jaeng::result<SamplerHandle> RendererD3D12::create_sampler(const SamplerDesc* sd)
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
    return resources_->add_sampler(std::move(s));
}

void RendererD3D12::destroy_sampler(SamplerHandle)
{
    // linear CPU heap => no free; TODO: resource table should release records
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

PipelineHandle RendererD3D12::create_graphics_pipeline(const GraphicsPipelineDesc* gp)
{
    // Build root signature with tables (CBV b0 / SRV t0 / Sampler s0) via helper
    D3D_ROOT_SIGNATURE_VERSION usedVer{};
    ComPtr<ID3D12RootSignature> root = CreateRootSignature_BindTables(device_->dev(), &usedVer);
    if (!root) return 0;

    auto* vsb = pipelines_->get_shader(gp->vs);
    auto* psb = pipelines_->get_shader(gp->fs);
    if (!vsb) return 0;

    // Input layout from GraphicsPipelineDesc
    std::vector<D3D12_INPUT_ELEMENT_DESC> ils;
    ils.reserve(gp->vertex_layout.attribute_count);
    for (uint32_t i = 0; i < gp->vertex_layout.attribute_count; ++i) {
        const auto& a = gp->vertex_layout.attributes[i];
        D3D12_INPUT_ELEMENT_DESC e{};
        e.SemanticName = (a.location == 0) ? "POSITION" : (a.location == 1) ? "COLOR" : "TEXCOORD";
        e.SemanticIndex = 0;
        e.Format = (a.location == 2) ? DXGI_FORMAT_R32G32_FLOAT : DXGI_FORMAT_R32G32B32_FLOAT;
        e.InputSlot = 0;
        e.AlignedByteOffset = a.offset;
        e.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        ils.push_back(e);
    }

    // PSO defaults
    D3D12_BLEND_DESC blend{};
    auto& rt0 = blend.RenderTarget[0];
    rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC rast{};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    rast.CullMode = D3D12_CULL_MODE_NONE;
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
    pso.InputLayout      = { ils.data(), (UINT)ils.size() };
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0]    = swapchain_ ? swapchain_->rtv_format() : DXGI_FORMAT_B8G8R8A8_UNORM;
    pso.SampleDesc.Count = 1;

    PipelineRec rec{};
    rec.root = root;

    if (FAILED(device_->dev()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&rec.pso)))) return 0;
    rec.topo         = (gp->topology == PrimitiveTopology::TriangleStrip) ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP :
                       (gp->topology == PrimitiveTopology::LineList)     ? D3D_PRIMITIVE_TOPOLOGY_LINELIST :
                                                                           D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    rec.vertexStride = gp->vertex_layout.stride;

    return pipelines_->add_pipeline(std::move(rec));
}

void RendererD3D12::destroy_pipeline(PipelineHandle h)
{
    pipelines_->del_pipeline(h);
}

BindGroupLayoutHandle RendererD3D12::create_bind_group_layout(const BindGroupLayoutDesc* d)
{
    return binds_->add_layout(d);
}

void RendererD3D12::destroy_bind_group_layout(BindGroupLayoutHandle h)
{
    binds_->del_layout(h);
}

BindGroupHandle RendererD3D12::create_bind_group(const BindGroupDesc* d)
{
    return binds_->add_group(d, device_->dev(), resources_.get(), cpuDesc_.get());
}

void RendererD3D12::destroy_bind_group(BindGroupHandle h)
{
    binds_->del_group(h);
}

CommandListHandle RendererD3D12::begin_commands()
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (!frameBegun_) begin_frame();
    return 1; // single list in this backend (FrameContext::cmd())
}

void RendererD3D12::cmd_begin_rendering_ops(CommandListHandle, LoadOp load_op,
                                            const ColorAttachmentDesc* colors, uint32_t count,
                                            const DepthAttachmentDesc* depth)
{
    JAENG_ASSERT(count >= 1 && colors);
    auto& fc = curFrame();
    auto* cl = fc.cmd();
    
    // For now we only support rendering to the swapchain backbuffer.
    // If the pass specifies a different texture, warn and still use the backbuffer.
    {
        auto bbHandle = get_current_backbuffer(/*swapchain*/1);
        if (colors[0].tex && colors[0].tex != bbHandle) {
            OutputDebugStringA("[jaeng:d3d12] cmd_begin_rendering_ops(): ColorAttachmentDesc.tex != backbuffer; "
                               "rendering will target the swapchain backbuffer.\n");
        }
    }

    // Transition PRESENT -> RENDER_TARGET on current backbuffer
    const uint32_t idx = swapchain_->current_index();
    ID3D12Resource* res = swapchain_->rtv_resource(idx);
    Barrier(cl, res, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    // Bind RTV (and optionally depth if you decide to add a DSV path later)
    auto rtv = swapchain_->rtv_handle(idx);
    cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    if (depthManager_ && depth) {
        auto dsv = depthManager_->get_dsv();
        Barrier(cl, depthManager_->dsv_resource(), depthManager_->resState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        depthManager_->resState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        cl->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    }

    // Viewport/scissor from backbuffer size
    D3D12_RESOURCE_DESC texDesc = res->GetDesc();
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

void RendererD3D12::cmd_end_rendering(CommandListHandle)
{
    auto& fc = curFrame();
    auto* cl = fc.cmd();

    // Transition RENDER_TARGET -> PRESENT
    const uint32_t idx = swapchain_->current_index();
    ID3D12Resource* res = swapchain_->rtv_resource(idx);
    Barrier(cl, res, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
}

void RendererD3D12::cmd_set_bind_group(CommandListHandle, uint32_t set_index, BindGroupHandle h)
{
    // Minimal set 0: copy CBV/SRV to this frame's GPU-visible heaps and set root tables
    if (set_index != 0) return;
    auto& fc = curFrame();
    auto* cl = fc.cmd();

    // Ensure heaps are bound for the current frame (SRV/CBV/UAV + Sampler)
    ID3D12DescriptorHeap* heaps[2] = { fc.gpuDescs->srv_heap(), fc.gpuDescs->samp_heap() };
    cl->SetDescriptorHeaps(2, heaps);

    // Let BindSpace perform CPU->GPU descriptor copies and set root descriptor tables:
    // - b0 CBV (fallback if group has no CB)
    // - t0 SRV and s0 Sampler
    if (auto* bg = binds_->get_group(h)) {
        // Copy persistent CPU CBV (or fallback) into frame heap and set root param 0
        D3D12_CPU_DESCRIPTOR_HANDLE cbvCpu = (bg->cb.present && bg->cb.cpuValid)
                                             ? bg->cb.cpuCbv
                                             : binds_->fallback_cbv_cpu();

        // Allocate next slots & copy descriptors (1 CBV, 1 SRV, 1 Sampler)
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{}, sampCpu{};
        if (auto* tex = resources_->get_tex(bg->texture)) srvCpu = tex->srvCpu;
        if (auto* smp = resources_->get_samp(bg->sampler)) sampCpu = smp->cpu;

        // CBV table at root 0
        D3D12_CPU_DESCRIPTOR_HANDLE cbvGpuCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE cbvGpu{};
        fc.gpuDescs->alloc_srv(&cbvGpuCpu, &cbvGpu); // slot for CBV
        device_->dev()->CopyDescriptorsSimple(1, cbvGpuCpu, cbvCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        BufferRec* cbBuf = resources_->get_buf((bg->cb.present && bg->cb.cpuValid)? bg->cb.buf : binds_->fallback_cb_buffer());
        if (cbBuf && cbBuf->res) {
            Barrier(cl, cbBuf->res.Get(), cbBuf->state, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            cbBuf->state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        }

        cl->SetGraphicsRootDescriptorTable(0, cbvGpu);

        // SRV at root 1
        D3D12_CPU_DESCRIPTOR_HANDLE srvGpuCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
        fc.gpuDescs->alloc_srv(&srvGpuCpu, &srvGpu); // slot for SRV
        if (srvCpu.ptr) {
            device_->dev()->CopyDescriptorsSimple(1, srvGpuCpu, srvCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        cl->SetGraphicsRootDescriptorTable(1, srvGpu); // t0

        // Allocate Sampler slot
        D3D12_CPU_DESCRIPTOR_HANDLE sampGpuCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE sampGpu{};
        fc.gpuDescs->alloc_samp(&sampGpuCpu, &sampGpu); // slot for Sampler
        if (sampCpu.ptr) {
            device_->dev()->CopyDescriptorsSimple(1, sampGpuCpu, sampCpu, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        }
        cl->SetGraphicsRootDescriptorTable(2, sampGpu); // s0
    }
}

void RendererD3D12::cmd_set_pipeline(CommandListHandle, PipelineHandle h)
{
    if (auto* p = pipelines_->get_pipeline(h)) {
        auto* cl = curFrame().cmd();
        cl->SetGraphicsRootSignature(p->root.Get());
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
    curFrame().cmd()->Close();

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
    ID3D12CommandList* one[] = { curFrame().cmd() };
    device_->queue()->ExecuteCommandLists(1, one);

    // Signal fence and store value for this frame, so begin_frame can wait next time
    auto& fc = curFrame();
    fc.fenceValue = device_->signal();
}

void RendererD3D12::present(SwapchainHandle)
{
    std::lock_guard<std::mutex> lock(mtx_);
    // Present (vsync on by default; can add a flag in RendererDesc if desired)
    UINT syncInterval = (tearing_)? 0 : 1;
    swapchain_->swap()->Present(syncInterval, (tearing_)? DXGI_PRESENT_ALLOW_TEARING : 0);
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

    api.create_buffer = [](const BufferDesc* d, const void* p) { return g_renderer->create_buffer(d,p).orValue(0); };
    api.destroy_buffer= [](BufferHandle h) { g_renderer->destroy_buffer(h); };
    api.update_buffer = [](BufferHandle h, uint64_t o, const void* p, uint64_t sz) { return g_renderer->update_buffer(h,o,p,sz).logError().has_value(); };

    api.create_texture = [](const TextureDesc* d, const void* p) { return g_renderer->create_texture(d,p).orValue(0); };
    api.destroy_texture= [](TextureHandle h) { g_renderer->destroy_texture(h); };
    api.create_sampler = [](const SamplerDesc* d) { return g_renderer->create_sampler(d).orValue(0); };
    api.destroy_sampler= [](SamplerHandle h) { g_renderer->destroy_sampler(h); };

    api.create_shader_module = [](const ShaderModuleDesc* d) { return g_renderer->create_shader_module(d); };
    api.destroy_shader_module= [](ShaderModuleHandle h) { g_renderer->destroy_shader_module(h); };
    api.create_graphics_pipeline = [](const GraphicsPipelineDesc* d) { return g_renderer->create_graphics_pipeline(d); };
    api.destroy_pipeline= [](PipelineHandle h) { g_renderer->destroy_pipeline(h); };

    api.create_bind_group_layout = [](const BindGroupLayoutDesc* d) { return g_renderer->create_bind_group_layout(d); };
    api.destroy_bind_group_layout = [](BindGroupLayoutHandle h) { g_renderer->destroy_bind_group_layout(h); };
    api.create_bind_group = [](const BindGroupDesc* d) { return g_renderer->create_bind_group(d); };
    api.destroy_bind_group = [](BindGroupHandle h) { g_renderer->destroy_bind_group(h); };

    api.begin_commands = []{ return g_renderer->begin_commands(); };
    api.cmd_begin_rendering_ops = [](CommandListHandle c, LoadOp op, const ColorAttachmentDesc* cs, uint32_t n, const DepthAttachmentDesc* dp){
        g_renderer->cmd_begin_rendering_ops(c, op, cs, n, dp);
    };
    api.cmd_end_rendering = [](CommandListHandle c) { g_renderer->cmd_end_rendering(c); };

    api.cmd_set_bind_group = [](CommandListHandle c, uint32_t i, BindGroupHandle h) { g_renderer->cmd_set_bind_group(c,i,h); };
    api.cmd_set_pipeline = [](CommandListHandle c, PipelineHandle p) { g_renderer->cmd_set_pipeline(c,p); };
    api.cmd_set_vertex_buffer = [](CommandListHandle c, uint32_t s, BufferHandle b, uint64_t o) { g_renderer->cmd_set_vertex_buffer(c,s,b,o); };
    api.cmd_set_index_buffer = [](CommandListHandle c, BufferHandle b, bool i32, uint64_t o) { g_renderer->cmd_set_index_buffer(c,b,i32,o); };

    api.cmd_draw = [](CommandListHandle c, uint32_t vc, uint32_t ic, uint32_t fv, uint32_t first) {
        g_renderer->cmd_draw(c, vc, ic, fv, first);
    };
    api.cmd_draw_indexed = [](CommandListHandle c, uint32_t in, uint32_t ic, uint32_t fi, uint32_t o, uint32_t first) {
        g_renderer->cmd_draw_indexed(c, in, ic, fi, o, first);
    };

    api.end_commands = [](CommandListHandle c) { g_renderer->end_commands(c); };
    api.submit = [](CommandListHandle* l, uint32_t n) { g_renderer->submit(l,n); };
    api.present= [](SwapchainHandle s) { g_renderer->present(s); };
    api.wait_idle = []{ g_renderer->wait_idle(); };

    *out_api = api;
    return true;
}
