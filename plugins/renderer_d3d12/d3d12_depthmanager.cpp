#include "d3d12_depthmanager.h"

using Microsoft::WRL::ComPtr;

DepthManager::DepthManager(ID3D12Device* dev, D3D12_CPU_DESCRIPTOR_HANDLE dh) : device(dev), dsvHandle(dh), resState(D3D12_RESOURCE_STATE_COMMON) {}

DepthManager::~DepthManager() {}

jaeng::result<> DepthManager::init(UINT width, UINT height, DXGI_FORMAT format) {
    depthFormat = format;
    return create_depth_buffer(width, height);
}

jaeng::result<> DepthManager::resize(UINT width, UINT height) {
    depthResource.Reset();
    return create_depth_buffer(width, height);
}

jaeng::result<> DepthManager::create_depth_buffer(UINT width, UINT height) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = depthFormat;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearVal{};
    clearVal.Format = depthFormat;
    clearVal.DepthStencil.Depth = clearDepth;
    clearVal.DepthStencil.Stencil = clearStencil;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    JAENG_CHECK_HRESULT(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                    D3D12_RESOURCE_STATE_COMMON, &clearVal,
                                    IID_PPV_ARGS(&depthResource)));

    // Create DSV
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = depthFormat;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(depthResource.Get(), &dsvDesc, dsvHandle);

    resState = D3D12_RESOURCE_STATE_COMMON;

    return {};
}

void DepthManager::bind(ID3D12GraphicsCommandList* cmd, D3D12_CPU_DESCRIPTOR_HANDLE* rtvs, UINT rtvCount) {
    cmd->OMSetRenderTargets(rtvCount, rtvs, FALSE, &dsvHandle);
}

void DepthManager::clear(ID3D12GraphicsCommandList* cmd) {
    cmd->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                                             clearDepth, clearStencil, 0, nullptr);
}
