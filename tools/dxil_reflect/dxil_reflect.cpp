#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <unknwn.h>
#include <comdef.h>
#include <dxcapi.h>
#include <d3d12shader.h>
#include <wrl/client.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include "render/public/renderer_api.h"

using Microsoft::WRL::ComPtr;

struct VSParam {
    uint32_t offset;
    std::string semanticName;
};

struct ReflectData {
    struct BoundResource {
        BindGroupEntryType type;      
        uint32_t resourceIndex;       
        uint32_t space;               
        uint32_t arrayCount;          
        uint32_t stageMask;           
        std::string name;
    };

    std::string name;
    std::vector<VSParam> vsParams;
    uint32_t stride;
    std::vector<BoundResource> bindings;
};

const char* get_typename(BindGroupEntryType t) {
    switch (t) {
        case BindGroupEntryType::UniformBuffer: return "BindGroupEntryType::UniformBuffer";
        case BindGroupEntryType::Texture:       return "BindGroupEntryType::Texture";
        case BindGroupEntryType::Sampler:       return "BindGroupEntryType::Sampler";
        case BindGroupEntryType::StructuredBuffer: return "BindGroupEntryType::StructuredBuffer";
        case BindGroupEntryType::ByteAddressBuffer: return "BindGroupEntryType::ByteAddressBuffer";
        case BindGroupEntryType::UnorderedAccess: return "BindGroupEntryType::UnorderedAccess";
        default: return "BindGroupEntryType::UniformBuffer";
    }
}

uint32_t stage_bit_vertex()   { return 1 << 0; }
uint32_t stage_bit_fragment() { return 1 << 1; }

BindGroupEntryType map_type(D3D_SHADER_INPUT_TYPE t) {
    switch (t) {
        case D3D_SIT_CBUFFER: return BindGroupEntryType::UniformBuffer;
        case D3D_SIT_TBUFFER: return BindGroupEntryType::UniformBuffer;
        case D3D_SIT_TEXTURE: return BindGroupEntryType::Texture;
        case D3D_SIT_SAMPLER: return BindGroupEntryType::Sampler;
        case D3D_SIT_UAV_RWTYPED: return BindGroupEntryType::UnorderedAccess;
        case D3D_SIT_STRUCTURED: return BindGroupEntryType::StructuredBuffer;
        case D3D_SIT_UAV_RWSTRUCTURED: return BindGroupEntryType::UnorderedAccess;
        case D3D_SIT_BYTEADDRESS: return BindGroupEntryType::ByteAddressBuffer;
        case D3D_SIT_UAV_RWBYTEADDRESS: return BindGroupEntryType::UnorderedAccess;
        default: return BindGroupEntryType::UniformBuffer;
    }
}

void outputJson(const ReflectData& reflect, const char* outPath) {
    std::ofstream out(outPath);
    out << "{\n";
    out << "  \"name\": \"" << reflect.name << "\",\n";
    out << "  \"stride\": " << reflect.stride << ",\n";
    out << "  \"attributes\": [\n";
    for (size_t i = 0; i < reflect.vsParams.size(); i++) {
        out << "    { \"semantic\": \"" << reflect.vsParams[i].semanticName << "\", \"offset\": " << reflect.vsParams[i].offset << " }";
        if (i < reflect.vsParams.size() - 1) out << ",";
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"bindings\": [\n";
    size_t i = 0;
    const auto sz = reflect.bindings.size();
    for (auto b : reflect.bindings) {        
        out << "    { \"name\": \"" << b.name
            << "\", \"type\": " << (int)b.type
            << ", \"resourceIndex\": " << b.resourceIndex
            << ", \"space\": " << b.space
            << ", \"arrayCount\": " << b.arrayCount
            << ", \"stageMask\": " << b.stageMask << " }";
        out << ((++i == sz)? "\n" : ",\n");
    }
    out << "  ]\n}\n\n";
}

void outputHeader(const ReflectData& rd, const char* headerPath, const char* vertexPath, const char* pixelPath) {
    std::ofstream out(headerPath);
    out << "#pragma once\n#include \"render/public/renderer_api.h\"\n\n";
    out << "#include <fstream>\n#include <vector>\n#include <stdexcept>\n#include <string>\n\n";
    out << "// Auto-generated pipeline reflection\nnamespace ShaderReflection { namespace " << rd.name << " {\n\n";
    
    out << "    enum : uint32_t {\n";
    out << "        Stage_None     = 0,\n";
    out << "        Stage_Vertex   = 1 << 0,\n";
    out << "        Stage_Fragment = 1 << 1,\n";
    out << "        Stage_Geometry = 1 << 2,\n";
    out << "        Stage_Hull     = 1 << 3,\n";
    out << "        Stage_Domain   = 1 << 4,\n";
    out << "        Stage_Compute  = 1 << 5\n";
    out << "    };\n\n";

    out << "    static constexpr VertexAttributeDesc vertexAttributes[] = {\n";
    for (int i = 0; i < (int)rd.vsParams.size(); i++) {
        out << "        { " << i << ", 0, " << rd.vsParams[i].offset << " }, // " << rd.vsParams[i].semanticName << "\n";
    }
    out << "    };\n\n";

    out << "    static constexpr VertexLayoutDesc vertexLayout = {\n";
    out << "        .stride = " << rd.stride << ",\n";
    out << "        .attributes = vertexAttributes,\n";
    out << "        .attribute_count = " << (uint32_t)rd.vsParams.size() << "\n";
    out << "    };\n\n";

    out << "    const char* inputSemantics[] = {\n";
    for (int i = 0; i < (int)rd.vsParams.size(); i++) {
        out << "        \"" << rd.vsParams[i].semanticName << "\",\n";
    }
    out << "    };\n\n";

    out << "    struct PushConstants {\n";
    for (auto& bind : rd.bindings) {
        out << "        uint32_t " << bind.name << "; // Index for " << get_typename(bind.type) << "\n";
    }
    out << "    };\n\n";

    out << "    const char* vsPath = \"" << vertexPath << "\";\n";
    out << "    const char* psPath = \"" << pixelPath  << "\";\n\n";
    out << "} }\n";
}

ReflectData fromReflection(ID3D12ShaderReflection* vsr, ID3D12ShaderReflection* psr, const std::string& name) {
    ReflectData r { .name = name };
    std::map<std::string, ReflectData::BoundResource> entries;

    auto add_binding = [&](BindGroupEntryType ty, uint32_t space, uint32_t slot, uint32_t count, uint32_t stage, std::string name) {
        if (space == 0 && (ty == BindGroupEntryType::Texture || ty == BindGroupEntryType::Sampler)) return;
        auto it = entries.find(name);
        if (it == entries.end()) {
            entries[name] = ReflectData::BoundResource{ty, slot, space, count, stage, std::move(name)};
        } else {
            it->second.stageMask |= stage;
            it->second.arrayCount = std::max(it->second.arrayCount, count);
        }
    };

    D3D12_SHADER_DESC vsDesc, psDesc;
    vsr->GetDesc(&vsDesc);
    for (UINT i = 0; i < vsDesc.BoundResources; i++) {
        D3D12_SHADER_INPUT_BIND_DESC b;
        vsr->GetResourceBindingDesc(i, &b);
        add_binding(map_type(b.Type), b.Space, b.BindPoint, b.BindCount, stage_bit_vertex(), b.Name);
    }
    if (psr) {
        psr->GetDesc(&psDesc);
        for (UINT i = 0; i < psDesc.BoundResources; i++) {
            D3D12_SHADER_INPUT_BIND_DESC b;
            psr->GetResourceBindingDesc(i, &b);
            add_binding(map_type(b.Type), b.Space, b.BindPoint, b.BindCount, stage_bit_fragment(), b.Name);
        }
    }

    for (auto& pair : entries) r.bindings.push_back(std::move(pair.second));

    r.stride = 0;
    for (UINT i = 0; i < vsDesc.InputParameters; i++) {
        D3D12_SIGNATURE_PARAMETER_DESC p;
        vsr->GetInputParameterDesc(i, &p);
        r.vsParams.push_back({ r.stride, p.SemanticName });
        if (p.Mask == 1) r.stride += 4;
        else if (p.Mask <= 3) r.stride += 8;
        else if (p.Mask <= 7) r.stride += 12;
        else if (p.Mask <= 15) r.stride += 16;
    }
    return r;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "Usage: dxil_reflect <vertex.dxil> <pixel.dxil> <pipeline_name> <output_base_path>\n";
        return 1;
    }
    const char* vertexPath = argv[1];
    const char* pixelPath = argv[2];
    const char* pipelineName = argv[3];
    const char* outPath = argv[4];

    try {
        ComPtr<IDxcLibrary> library;
        DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library));
        ComPtr<IDxcUtils> utils;
        DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));

        auto load_shader = [&](const char* path, ComPtr<ID3D12ShaderReflection>& ref) {
            ComPtr<IDxcBlobEncoding> blob;
            size_t len = strlen(path);
            std::vector<wchar_t> wpath(len + 1);
            MultiByteToWideChar(CP_ACP, 0, path, -1, wpath.data(), (int)len + 1);
            utils->LoadFile(wpath.data(), nullptr, &blob);
            DxcBuffer buf{ blob->GetBufferPointer(), blob->GetBufferSize(), 0 };
            utils->CreateReflection(&buf, IID_PPV_ARGS(&ref));
        };

        ComPtr<ID3D12ShaderReflection> vsr, psr;
        load_shader(vertexPath, vsr);
        load_shader(pixelPath, psr);

        ReflectData rd = fromReflection(vsr.Get(), psr.Get(), pipelineName);

        std::string jsonPath(outPath);
        jsonPath.append(".json");
        outputJson(rd, jsonPath.c_str());

        std::string headerPath(outPath);
        headerPath.append(".h");
        outputHeader(rd, headerPath.c_str(), vertexPath, pixelPath);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
