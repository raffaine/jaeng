#include "d3d12_descriptors.h"
#include "d3d12_utils.h"

using Microsoft::WRL::ComPtr;

// ------------------ CPU allocator (linear, non-shader-visible) ------------------

jaeng::result<> DescriptorAllocatorCPU::create(ID3D12Device* dev, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count)
{
    // RTV heap (big enough for swapchain backbuffers)
    D3D12_DESCRIPTOR_HEAP_DESC dh{};
    dh.NumDescriptors = count;
    dh.Type           = type;
    dh.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    JAENG_CHECK_HRESULT(dev->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&heap_)));

    incSize_  = dev->GetDescriptorHandleIncrementSize(type);
    capacity_ = count;
    used_     = 0;

    return {};
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocatorCPU::allocate(UINT* outIndex)
{
    JAENG_ASSERT(used_ < capacity_);
    if (outIndex) *outIndex = used_;

    D3D12_CPU_DESCRIPTOR_HANDLE base = heap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE h{};
    h.ptr = base.ptr + SIZE_T(used_) * SIZE_T(incSize_);
    used_++;

    return h;
}

// ------------------ GPU allocators (shader-visible, per-frame reset) ------------

jaeng::result<> DescriptorAllocatorGPU::create(ID3D12Device* dev, UINT srvCount, UINT sampCount) {
    // SRV/CBV/UAV heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC dh{};
        dh.NumDescriptors = srvCount;
        dh.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        dh.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        JAENG_CHECK_HRESULT(dev->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&srvHeap_)));

        srvInc_ = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        srvCap_ = srvCount;
        srvUsed_= 0;
    }
    // Sampler heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC dh{};
        dh.NumDescriptors = sampCount;
        dh.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        dh.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        JAENG_CHECK_HRESULT(dev->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&sampHeap_)));

        sampInc_ = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        sampCap_ = sampCount;
        sampUsed_= 0;
    }

    return {};
}

void DescriptorAllocatorGPU::reset()
{
    srvUsed_  = 0;
    sampUsed_ = 0;
}

void DescriptorAllocatorGPU::alloc_srv(D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
{
    if (srvUsed_ >= srvCap_) srvUsed_ = 0; // simple wrap; caller resets per-frame
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += SIZE_T(srvUsed_) * SIZE_T(srvInc_);

    D3D12_GPU_DESCRIPTOR_HANDLE gpu = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += SIZE_T(srvUsed_) * SIZE_T(srvInc_);

    srvUsed_++;

    if (outCpu) *outCpu = cpu;
    if (outGpu) *outGpu = gpu;
}

void DescriptorAllocatorGPU::alloc_samp(D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
{
    if (sampUsed_ >= sampCap_) sampUsed_ = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = sampHeap_->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += SIZE_T(sampUsed_) * SIZE_T(sampInc_);

    D3D12_GPU_DESCRIPTOR_HANDLE gpu = sampHeap_->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += SIZE_T(sampUsed_) * SIZE_T(sampInc_);

    sampUsed_++;

    if (outCpu) *outCpu = cpu;
    if (outGpu) *outGpu = gpu;
}
