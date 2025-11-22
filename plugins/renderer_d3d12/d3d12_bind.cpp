#include "d3d12_bind.h"
#include "d3d12_utils.h"

#include "render/public/renderer_api.h"

using Microsoft::WRL::ComPtr;

bool BindSpace::init(ID3D12Device* dev, DescriptorAllocatorCPU* cpuDesc)
{
    // Build fallback 256-byte CBV in CPU heap (CBV/SRV/UAV)
    return create_fallback_cbv(dev, cpuDesc);
}

void BindSpace::shutdown()
{
    if (fallbackCb_ && fallbackReady_)
    {
        fallbackCb_->Unmap(0, nullptr);
        fallbackReady_ = false;
    }
    fallbackCb_.Reset();
}

BindGroupLayoutHandle BindSpace::add_layout(const BindGroupLayoutDesc* d)
{
    BindGroupLayoutRec rec{};
    rec.entries.assign(d->entries, d->entries + d->entry_count);
    layouts_.push_back(std::move(rec));
    return (BindGroupLayoutHandle)layouts_.size();
}

void BindSpace::del_layout(BindGroupLayoutHandle /*h*/)
{
    // No-op (simple vector storage); could mark tombstones if needed
}

BindGroupHandle BindSpace::add_group(const BindGroupDesc* d, ID3D12Device* device, ResourceTable* resources, DescriptorAllocatorCPU* cpu)
{
    BindGroupRec bg{};
    bg.layout = d->layout;

    for (uint32_t i = 0; i < d->entry_count; ++i)
    {
        const BindGroupEntry& e = d->entries[i];
        switch (e.type)
        {
        case BindGroupEntryType::Texture:
            bg.texture = e.texture;
            break;
        case BindGroupEntryType::Sampler:
            bg.sampler = e.sampler;
            break;
        case BindGroupEntryType::UniformBuffer:
        {
            bg.cb.present = true;
            bg.cb.buf     = e.buffer;
            bg.cb.offset  = e.offset;
            bg.cb.size    = e.size;

            // Create persistent CPU CBV descriptor
            if (auto* br = resources->get_buf(e.buffer))
            {
                D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
                cbv.BufferLocation = br->res->GetGPUVirtualAddress() + e.offset;
                cbv.SizeInBytes    = (UINT)((e.size + 255ull) & ~255ull);
                if (cbv.SizeInBytes == 0) cbv.SizeInBytes = 256;

                UINT idx = 0;
                bg.cb.cpuCbv = cpu->allocate(&idx);
                device->CreateConstantBufferView(&cbv, bg.cb.cpuCbv);

                bg.cb.cpuValid = true;
            }
        } break;
        default:
            break;
        }
    }

    groups_.push_back(bg);
    return (BindGroupHandle)groups_.size();
}

void BindSpace::del_group(BindGroupHandle /*h*/)
{
    // No-op (linear storage)
}

BindGroupRec* BindSpace::get_group(BindGroupHandle h)
{
    if (h == 0) return nullptr;
    size_t idx = size_t(h - 1);
    return (idx < groups_.size()) ? &groups_[idx] : nullptr;
}

bool BindSpace::create_fallback_cbv(ID3D12Device* dev, DescriptorAllocatorCPU* cpu)
{
    if (fallbackReady_) return true;

    // Small upload buffer (256 bytes)
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC   rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = 256;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HR_CHECK(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&fallbackCb_)));

    // Zero initialize
    void* p = nullptr; D3D12_RANGE r{0, 0};
    HR_CHECK(fallbackCb_->Map(0, &r, &p));
    std::memset(p, 0, 256);
    fallbackCb_->Unmap(0, nullptr);

    // Allocate one CPU CBV slot and create CBV
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
    cbv.BufferLocation = fallbackCb_->GetGPUVirtualAddress();
    cbv.SizeInBytes    = 256;

    UINT idx = 0;
    fallbackCbvCpu_ = cpu->allocate(&idx);

    // We need a device pointer to create the CBV.
    ComPtr<ID3D12Device> devRef = dev; // keep local ref
    devRef->CreateConstantBufferView(&cbv, fallbackCbvCpu_);

    fallbackReady_ = true;
    return true;
}
