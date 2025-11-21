#include "d3d12_commands.h"
#include "d3d12_descriptors.h"
#include "d3d12_upload.h"

bool FrameContext::init(ID3D12Device* dev) {
    if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc_)))) return false;

    if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc_.Get(), nullptr, IID_PPV_ARGS(&cmd_)))) return false;
    
    cmd_->Close();

    return true;
}

void FrameContext::reset() {
    if (upload) upload->reset();
    if (gpuDescs) gpuDescs->reset();
    alloc_->Reset();
    cmd_->Reset(alloc_.Get(), nullptr);
}
