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
#undef max
#include <numeric>

#include "render/public/renderer_api.h"

using Microsoft::WRL::ComPtr;

void LoadShader(const std::string& path, std::vector<char>& data) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Failed to open shader file: " + path);
    size_t size = file.tellg();
    file.seekg(0);
    data.resize(size);
    file.read(data.data(), size);
}

struct ReflectData {
    struct VSParam {
        uint32_t format;
        uint32_t offset;
        std::string semanticName;
    };

    struct BoundResource {
        BindGroupEntryType type;      // UniformBuffer, Texture, Sampler, UAV, Structured, ByteAddress
        uint32_t bindPoint;           // e.g., 0 for b0/t0/s0/u0
        uint32_t space;               // e.g., 0 or 1
        uint32_t arrayCount;          // BindCount
        uint32_t stageMask;           // bitmask: Vertex|Fragment|Geometry|Hull|Domain|Compute
        std::string name;
    };

    std::string name;
    std::vector<VSParam> vsParams;
    uint32_t stride;
    std::vector<BoundResource> bindings;
};

static uint32_t stage_bit_vertex()   { return 1u << 0; }
static uint32_t stage_bit_fragment() { return 1u << 1; }

BindGroupEntryType map_type(D3D_SHADER_INPUT_TYPE t) {
    switch (t) {
        case D3D_SIT_CBUFFER:         return BindGroupEntryType::UniformBuffer;
        case D3D_SIT_TEXTURE:         return BindGroupEntryType::Texture;
        case D3D_SIT_SAMPLER:         return BindGroupEntryType::Sampler;
        case D3D_SIT_STRUCTURED:      return BindGroupEntryType::StructuredBuffer;
        case D3D_SIT_BYTEADDRESS:     return BindGroupEntryType::ByteAddressBuffer;
        case D3D_SIT_UAV_RWTYPED:
        case D3D_SIT_UAV_RWSTRUCTURED:
        case D3D_SIT_UAV_RWBYTEADDRESS:
        case D3D_SIT_UAV_APPEND_STRUCTURED:
        case D3D_SIT_UAV_CONSUME_STRUCTURED:
        case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
            return BindGroupEntryType::UnorderedAccess;
        default:                      return BindGroupEntryType::Unknown;
    }
};

const char* get_typename(BindGroupEntryType t) {
    switch(t) {
        case BindGroupEntryType::UniformBuffer:
            return "BindGroupEntryType::UniformBuffer";
        case BindGroupEntryType::Texture:
            return "BindGroupEntryType::Texture";
        case BindGroupEntryType::Sampler:
            return "BindGroupEntryType::Sampler";
        case BindGroupEntryType::StructuredBuffer:
            return "BindGroupEntryType::StructuredBuffer";
        case BindGroupEntryType::ByteAddressBuffer:
            return "BindGroupEntryType::ByteAddressBuffer";
        default:
            return "BindGroupEntryType::Texture";
    }
}

ReflectData fromReflection(ID3D12ShaderReflection* vsr, ID3D12ShaderReflection* psr, const std::string& name) {
    ReflectData r { .name = name };

    std::map<std::tuple<BindGroupEntryType,uint32_t/*space*/,uint32_t/*bindPoint*/>, ReflectData::BoundResource> entries;

    // Helper to add/merge an entry
    auto add_binding = [&](BindGroupEntryType ty, uint32_t space, uint32_t slot, uint32_t count, uint32_t stage, std::string name) {
        auto it = entries.find({ty, space, slot});
        if (it == entries.end()) {
            entries[{ty, space, slot}] = ReflectData::BoundResource{ty, slot, space, count, stage, std::move(name)};
        } else {
            it->second.stageMask |= stage;
            it->second.arrayCount = std::max(it->second.arrayCount, count);
            // keep first name or choose a canonical one 
        }
    };
    
    D3D12_SHADER_DESC vsDesc, psDesc;
    vsr->GetDesc(&vsDesc);
    psr->GetDesc(&psDesc);
    
    UINT stride = 0;
    for (UINT i = 0; i < vsDesc.InputParameters; ++i) {
        D3D12_SIGNATURE_PARAMETER_DESC param;
        vsr->GetInputParameterDesc(i, &param);
        UINT componentCount = __popcnt(param.Mask);
        
        r.vsParams.emplace_back(0, stride, param.SemanticName);
        stride += componentCount * 4;
    }
    r.stride = stride;

    // VS-bound resources
    for (UINT i = 0; i < vsDesc.BoundResources; ++i) {
        D3D12_SHADER_INPUT_BIND_DESC b{};
        vsr->GetResourceBindingDesc(i, &b);
        add_binding(map_type(b.Type), b.Space, b.BindPoint, b.BindCount, stage_bit_vertex(),
                    b.Name ? std::string(b.Name) : std::string{});
    }
    
    // PS-bound resources
    for (UINT i = 0; i < psDesc.BoundResources; ++i) {
        D3D12_SHADER_INPUT_BIND_DESC b{};
        psr->GetResourceBindingDesc(i, &b);
        add_binding(map_type(b.Type), b.Space, b.BindPoint, b.BindCount, stage_bit_fragment(),
                    b.Name ? std::string(b.Name) : std::string{});
    }

    // Flatten map into vector
    r.bindings.reserve(entries.size());
    for (auto& [key, br] : entries) {
        r.bindings.push_back(std::move(br));
    }

    return r;
}

void outputJSON(const ReflectData& reflect, const char* outPath) {
    std::ofstream out(outPath);
    
    // Header
    out << "{\n  \"name\": \"" << reflect.name << "\",\n";
    // Vertex layout
    out <<    "  \"vertexLayout\": [\n";
    auto i = 0;
    for (auto p : reflect.vsParams) {
        out << "   { \"semantic\": \"" << p.semanticName << "\", \"format\": \"" << p.format <<"\", \"offset\": \"" << p.offset << "\"}";
        out << ((++i == reflect.vsParams.size())? "\n" : ",\n");
    }
    out <<    "  ],\n";
    out <<    "  \"stride\": " << reflect.stride << ",\n";
    // Bind Groups
    out <<    "  \"bindings\": [\n";
    i = 0;
    const auto sz = reflect.bindings.size();
    for (auto b : reflect.bindings) {        
        out << "    { \"name\": \"" << b.name
            << "\", \"type\": " << (int)b.type
            << ", \"bindPoint\": " << b.bindPoint
            << ", \"space\": " << b.space
            << ", \"arrayCount\": " << b.arrayCount
            << ", \"stageMask\": " << b.stageMask << " }";
        //out << "   { \"name\": \"" << b.name << "\", \"binding\": \"" << b.bindPoint << "\", \"type\": \"" << typeStr <<"\", \"stage\": \"vertex\"}";
        out << ((++i == sz)? "\n" : ",\n");
    }
    out <<    "  ]\n}\n\n";
}

void outputHeader(const ReflectData& rd, const char* headerPath, const char* vertexPath, const char* pixelPath) {
        std::ofstream out(headerPath);
        out << "#pragma once\n#include \"renderer_api.h\"\n\n";
        out << "#include <fstream>\n#include <vector>\n#include <stdexcept>\n#include <string>\n\n";
        out << "// Auto-generated pipeline reflection\nnamespace ShaderReflection {\n\n";
        // Stage bits (match your engine)
        out << "    enum : uint32_t {\n";
        out << "        Stage_None     = 0,\n";
        out << "        Stage_Vertex   = 1 << 0,\n";
        out << "        Stage_Fragment = 1 << 1,\n";
        out << "        Stage_Geometry = 1 << 2,\n";
        out << "        Stage_Hull     = 1 << 3,\n";
        out << "        Stage_Domain   = 1 << 4,\n";
        out << "        Stage_Compute  = 1 << 5\n";
        out << "    };\n\n";
        // Vertex layout
        out << "    static constexpr VertexAttributeDesc vertexAttributes[] = {\n";
        for (int i = 0; i < rd.vsParams.size(); i++) {
            out << "        { " << i << ", 0, " << rd.vsParams[i].offset << " }, // " << rd.vsParams[i].semanticName << "\n";
        }
        out << "    };\n\n";
        out << "    static constexpr VertexLayoutDesc vertexLayout = {\n";
        out << "        .stride = " << rd.stride << ",\n";
        out << "        .attributes = vertexAttributes,\n";
        out << "        .attribute_count = " << rd.vsParams.size() << "\n";
        out << "    };\n\n";
        out << "    const char* inputSemantics[] = {\n";
        for (int i = 0; i < rd.vsParams.size(); i++) {
            out << "        \"" << rd.vsParams[i].semanticName << "\",\n";
        }
        out << "    };\n\n";

        // Bindings
        out << "    static constexpr BindGroupLayoutEntry bindGroupEntries[] = {\n";
        for (auto& bind : rd.bindings) {
            out << "        { " 
                << bind.bindPoint << ", "
                << bind.space << ", "
                << get_typename(bind.type) << ", "
                << bind.stageMask << " }, // " << bind.name << "\n";
        }
        out << "    };\n\n";
        out << "    static constexpr BindGroupLayoutDesc bindGroupLayout = {\n";
        out << "        .entries = bindGroupEntries,\n";
        out << "        .entry_count = " << rd.bindings.size() << "\n";
        out << "    };\n\n";
        out << "    const char* vsPath = \"" << vertexPath << "\";\n";
        out << "    const char* psPath = \"" << pixelPath  << "\";\n\n";

        // Closes namespace
        out << "}\n";

        out.close();
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: dxil_reflect <vertex.dxil> <pixel.dxil> <output_header.h>\n";
        return 1;
    }

    const char* vertexPath = argv[1];
    const char* pixelPath = argv[2];
    const char* pipeline = argv[3];
    const char* outPath = argv[4];

    try {
        // Initialize DXC
        ComPtr<IDxcUtils> utils;
        ComPtr<IDxcContainerReflection> containerReflection;
        DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
        DxcCreateInstance(CLSID_DxcContainerReflection, IID_PPV_ARGS(&containerReflection));

        auto reflectShader = [&](const std::string& path) -> ComPtr<ID3D12ShaderReflection> {
            std::vector<char> data;
            LoadShader(path, data);

            ComPtr<IDxcBlobEncoding> blob;
            utils->CreateBlob(data.data(), data.size(), DXC_CP_ACP, &blob);

            HRESULT hr = containerReflection->Load(blob.Get());
            if (FAILED(hr)) throw std::runtime_error("Failed to load DXIL container for: " + path);

            UINT32 partIndex;
            hr = containerReflection->FindFirstPartKind(DXC_PART_DXIL, &partIndex);
            if (FAILED(hr)) throw std::runtime_error("DXIL part not found in: " + path);

            ComPtr<ID3D12ShaderReflection> shaderReflection;
            hr = containerReflection->GetPartReflection(partIndex, IID_PPV_ARGS(&shaderReflection));
            if (FAILED(hr)) throw std::runtime_error("Failed to get shader reflection for: " + path);

            return shaderReflection;
        };

        auto vsReflect = reflectShader(vertexPath);
        auto psReflect = reflectShader(pixelPath);

        ReflectData rd = fromReflection(vsReflect.Get(), psReflect.Get(), pipeline);

        std::string headerPath(outPath);
        headerPath.append("_reflect.h");
        outputHeader(rd, headerPath.c_str(), vertexPath, pixelPath);
        std::cout << "Reflection header generated: " << headerPath << "\n";

        std::string jsonPath(outPath);
        jsonPath.append("_reflect.json");
        outputJSON(rd, jsonPath.c_str());
        std::cout << "Reflection JSON generated: " << jsonPath << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
