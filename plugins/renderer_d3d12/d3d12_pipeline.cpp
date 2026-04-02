
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
CreateGlobalRootSignature(ID3D12Device* dev, /*out*/ D3D_ROOT_SIGNATURE_VERSION* usedVer)
{
    // ---------------- Descriptor ranges ----------------
    // range[0]: All SRV/CBV/UAVs (space0, t0..unbounded)
    // range[1]: All Samplers (space0, s0..unbounded)

    D3D12_DESCRIPTOR_RANGE ranges[2]{};

    // SRV/CBV/UAV table: t0 (space0)
    ranges[0].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors                    = UINT_MAX; // Unbounded
    ranges[0].BaseShaderRegister                = 0;
    ranges[0].RegisterSpace                     = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // Sampler table: s0 (space0)
    ranges[1].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    ranges[1].NumDescriptors                    = UINT_MAX; // Unbounded
    ranges[1].BaseShaderRegister                = 0;
    ranges[1].RegisterSpace                     = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // ---------------- Root parameters ----------------
    D3D12_ROOT_PARAMETER params[5]{};

    // root 0 -> Root Constants (b0, space0)
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    params[0].Constants.ShaderRegister  = 0;
    params[0].Constants.RegisterSpace   = 0;
    params[0].Constants.Num32BitValues  = 32;

    // root 1 -> Root CBV (b1, space0) - Frame
    params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].Descriptor.RegisterSpace  = 0;

    // root 2 -> Root CBV (b2, space0) - Object
    params[2].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[2].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    params[2].Descriptor.ShaderRegister = 2;
    params[2].Descriptor.RegisterSpace  = 0;

    // root 3 -> SRV/CBV/UAV table (t0..., space0)
    params[3].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    params[3].DescriptorTable           = { 1, &ranges[0] };

    // root 4 -> Sampler table (s0..., space0)
    params[4].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[4].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    params[4].DescriptorTable           = { 1, &ranges[1] };

D3D12_ROOT_SIGNATURE_DESC rs{};
rs.NumParameters     = _countof(params);
rs.pParameters       = params;
rs.NumStaticSamplers = 0;
rs.pStaticSamplers   = nullptr;
rs.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
                     | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
                     | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

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
