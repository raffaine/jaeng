// d3d12_upload.h
#pragma once
#include <wrl.h>
#include <d3d12.h>

class UploadRing {
public:
    bool  create(ID3D12Device* dev, UINT64 sizeBytes);
    void  reset(); // per-frame
    bool  stage(const void* src, UINT64 size, UINT64 alignment, D3D12_GPU_VIRTUAL_ADDRESS* outGpu, ID3D12Resource** outRes, UINT64* outOffset, void** outCpu = nullptr);

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer_;
    UINT64 size_ = 0, head_ = 0;
    uint8_t* mapped_ = nullptr;
};
