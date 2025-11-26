#include "d3d12_upload.h"
#include "d3d12_utils.h"
#include <cstdio>

jaeng::result<> UploadRing::create(ID3D12Device* dev, UINT64 sizeBytes)
{
    size_ = sizeBytes;
    head_ = 0;

    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = sizeBytes;
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    JAENG_CHECK_HRESULT(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer_)));

    D3D12_RANGE r{0,0};
    JAENG_CHECK_HRESULT(buffer_->Map(0, &r, reinterpret_cast<void**>(&mapped_)));

    return {};
}

void  UploadRing::reset()
{
    head_ = 0;
}

jaeng::result<UploadSlice> UploadRing::stage(const void* src, UINT64 size, UINT64 alignment)
{
    JAENG_ERROR_IF((!src || size == 0), jaeng::error_code::invalid_args, "[UploadRing] stage(): Null or 0-sized source buffer is invalid");
    JAENG_ERROR_IF(!buffer_, jaeng::error_code::resource_not_ready, "[UploadRing] stage(): Buffer was not created");

    // Alignment must be >= 1 and preferably power-of-two
    if (alignment == 0) alignment = 1;
    // If alignment not pow2, round it up to nearest pow2 (defensive)
    auto is_pow2 = [](auto x){ return x && !(x & (x-1)); };
    if (!is_pow2(alignment)) {
        UINT64 p2 = 1ull;
        while (p2 < alignment) p2 <<= 1ull;
        alignment = p2;
        OutputDebugStringA("[UploadRing] stage(): non-pow2 alignment -> rounded.\n");
    }

    const UINT64 aligned = (head_ + (alignment - 1)) & ~(alignment - 1); // AlignUp
    if (aligned + size > size_) {
        // Not enough space this frame: fail (caller will fallback)
        char msg[176];
        sprintf_s(msg, "[UploadRing] stage(): out of space. size=%llu head=%llu aligned=%llu cap=%llu\n",
                  (unsigned long long)size,
                  (unsigned long long)head_,
                  (unsigned long long)aligned,
                  (unsigned long long)size_);
        return jaeng::Error::fromMessage(static_cast<int>(jaeng::error_code::invalid_operation), msg);
    }

    // Copy into mapped upload buffer
    memcpy(mapped_ + aligned, src, (size_t)size);
    head_ = aligned + size;

    return UploadSlice{ buffer_.Get(), aligned, mapped_ + aligned };
}

