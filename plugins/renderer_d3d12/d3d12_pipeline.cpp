
#include "d3d12_pipeline.h"
#include <windows.h>
#include <d3d12shader.h>

#include "common/result.h"
#include "d3d12_utils.h"

using Microsoft::WRL::ComPtr;

// ------------------ PipelineTable ------------------

ShaderModuleHandle PipelineTable::add_shader(ShaderBlob&& s)
{
    shaders_.push_back(std::move(s));
    return (ShaderModuleHandle)shaders_.size();
}

void PipelineTable::del_shader(ShaderModuleHandle h)
{
    if (h == 0) return;
    size_t idx = size_t(h - 1);
    if (idx < shaders_.size()) shaders_[idx].bytes.clear();
}

const ShaderBlob* PipelineTable::get_shader(ShaderModuleHandle h) const
{
    if (h == 0) return nullptr;
    size_t idx = size_t(h - 1);
    return (idx < shaders_.size()) ? &shaders_[idx] : nullptr;
}

PipelineHandle PipelineTable::add_pipeline(PipelineRec&& p)
{
    pipelines_.push_back(std::move(p));
    return (PipelineHandle)pipelines_.size();
}

PipelineRec* PipelineTable::get_pipeline(PipelineHandle h)
{
    if (h == 0) return nullptr;
    size_t idx = size_t(h - 1);
    return (idx < pipelines_.size()) ? &pipelines_[idx] : nullptr;
}

void PipelineTable::del_pipeline(PipelineHandle h)
{
    if (auto* p = get_pipeline(h)) { p->pso.Reset(); p->root.Reset(); }
}

// ------------------ Root signature helper ------------------

ComPtr<ID3D12RootSignature>
CreateRootSignature_BindTables(ID3D12Device* dev, /*out*/ D3D_ROOT_SIGNATURE_VERSION* usedVer)
{
    // CBV table (b0, space0), SRV table (t0, space0) & Sampler table (s0, space0)
    D3D12_DESCRIPTOR_RANGE ranges[3]{};

    ranges[0].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    ranges[0].NumDescriptors                    = 1;
    ranges[0].BaseShaderRegister                = 0;
    ranges[0].RegisterSpace                     = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;

    ranges[1].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[1].NumDescriptors                    = 1;
    ranges[1].BaseShaderRegister                = 0;
    ranges[1].RegisterSpace                     = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 0;

    ranges[2].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    ranges[2].NumDescriptors                    = 1;
    ranges[2].BaseShaderRegister                = 0;
    ranges[2].RegisterSpace                     = 0;
    ranges[2].OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType                     = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].ShaderVisibility                  = D3D12_SHADER_VISIBILITY_ALL;
    params[0].DescriptorTable = { 1, &ranges[0] };

    params[1].ParameterType                     = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].ShaderVisibility                  = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].DescriptorTable = { 1, &ranges[1] };

    params[2].ParameterType                     = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility                  = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].DescriptorTable = { 1, &ranges[2] };

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters     = _countof(params);
    rs.pParameters       = params;
    rs.NumStaticSamplers = 0;
    rs.pStaticSamplers   = nullptr;
    rs.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
                         | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
                         | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
                         | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3D12RootSignature> root;
    if(!std::invoke([&] -> jaeng::result<> {
        ComPtr<ID3DBlob> sig, err;
        JAENG_CHECK_HRESULT(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        JAENG_CHECK_HRESULT(dev->CreateRootSignature(/*nodeMask*/ 0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&root)));
        if (usedVer) *usedVer = D3D_ROOT_SIGNATURE_VERSION_1;
        return {};
    }).logError()) return nullptr;

    return root;
}
