#pragma once

#include <d3d12.h>
#include <wrl.h>

#include "common/result.h"

class DepthManager {
public:
    DepthManager(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle);
    ~DepthManager();

    jaeng::result<> init(UINT width, UINT height, DXGI_FORMAT format);
    jaeng::result<> resize(UINT width, UINT height);
    void bind(ID3D12GraphicsCommandList* cmd, D3D12_CPU_DESCRIPTOR_HANDLE* rtvs, UINT rtvCount);
    void clear(ID3D12GraphicsCommandList* cmd);

    D3D12_CPU_DESCRIPTOR_HANDLE get_dsv() const { return dsvHandle; }
    ID3D12Resource* dsv_resource() const { return depthResource.Get(); }
    DXGI_FORMAT get_format() const { return depthFormat; }
    
    D3D12_RESOURCE_STATES resState;

private:
    ID3D12Device* device;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthResource;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
    DXGI_FORMAT depthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    float clearDepth = 1.0f;
    UINT8 clearStencil = 0;

    jaeng::result<> create_depth_buffer(UINT width, UINT height);
};
