// d3d12_upload.h
#pragma once
#include <wrl.h>
#include <d3d12.h>

#include "common/result.h"

struct UploadSlice {
    ID3D12Resource* resource;   // UPLOAD heap, caller treats as non-owning
    UINT64          offset;     // byte offset into resource
    void*           cpu;        // mapped CPU address (optional)
};

class UploadRing {
public:
    jaeng::result<>  create(ID3D12Device* dev, UINT64 sizeBytes);
    void  reset(); // per-frame
    jaeng::result<UploadSlice>  stage(const void* src, UINT64 size, UINT64 alignment);
    jaeng::result<UploadSlice>  stage_pitched(const uint8_t* src, int rows, int elems_per_row, D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp);

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer_;
    UINT64 size_ = 0, head_ = 0;
    uint8_t* mapped_ = nullptr;
};
