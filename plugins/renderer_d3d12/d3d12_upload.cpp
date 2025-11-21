#include "d3d12_upload.h"
#include "d3d12_utils.h"

bool UploadRing::create(ID3D12Device* dev, UINT64 sizeBytes) {
        // Step 3: allocate per-frame upload ring and map it
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = sizeBytes;
        rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer_)))) return false;
        D3D12_RANGE r{0,0};
        if (FAILED(buffer_->Map(0, &r, reinterpret_cast<void**>(&mapped_)))) return false;

        return true;
}

void  UploadRing::reset() {

}

bool  UploadRing::stage(const void* src, UINT64 size, UINT64 alignment, D3D12_GPU_VIRTUAL_ADDRESS* outGpu, ID3D12Resource** outRes, UINT64* outOffset, void** outCpu /*nullptr*/) {
    uint64_t off = AlignUp(head_, alignment);
    if (off + size > size_) {
        // Ring overflow within the same frame; try wrap to 0 (safe because we fence per frame)
        off = 0;
        if (size > size_) return false; // too large for the ring
    }
    memcpy(mapped_ + off, src, size);
    head_ = off + size;

    *outRes = buffer_.Get();
    *outOffset = head_;
    return true;
}

