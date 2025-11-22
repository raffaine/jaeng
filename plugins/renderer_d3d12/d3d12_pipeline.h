// d3d12_pipeline.h
#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <vector>

#include "render/public/renderer_api.h"

struct ShaderBlob { std::vector<uint8_t> bytes; };

struct PipelineRec {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    D3D12_PRIMITIVE_TOPOLOGY topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    uint32_t vertexStride = 0;
};

class PipelineTable {
public:
    ShaderModuleHandle add_shader(ShaderBlob&& s);
    void               del_shader(ShaderModuleHandle);
    const ShaderBlob*  get_shader(ShaderModuleHandle) const;

    PipelineHandle     add_pipeline(PipelineRec&& p);
    PipelineRec*       get_pipeline(PipelineHandle h);
    void               del_pipeline(PipelineHandle h);

private:
    std::vector<ShaderBlob> shaders_;
    std::vector<PipelineRec> pipelines_;
};

// RootSignatureBuilder creates CBV table (b0), SRV table (t0), Sampler table (s0)
Microsoft::WRL::ComPtr<ID3D12RootSignature>
CreateRootSignature_BindTables(ID3D12Device*, /*out*/ D3D_ROOT_SIGNATURE_VERSION* usedVer);
