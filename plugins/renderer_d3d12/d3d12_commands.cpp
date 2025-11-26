#include "d3d12_commands.h"
#include "d3d12_descriptors.h"
#include "d3d12_upload.h"
#include "d3d12_utils.h"

jaeng::result<> FrameContext::init(ID3D12Device* dev)
{
    JAENG_CHECK_HRESULT(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc_)));
    JAENG_CHECK_HRESULT(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc_.Get(), nullptr, IID_PPV_ARGS(&cmd_)));
    
    HR_CHECK(cmd_->Close());
    fenceValue = 0;

    return {};
}

void FrameContext::reset()
{
    HR_CHECK(alloc_->Reset());
    HR_CHECK(cmd_->Reset(alloc_.Get(), /*pso*/nullptr));
}
