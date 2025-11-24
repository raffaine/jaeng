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

using Microsoft::WRL::ComPtr;

void LoadShader(const std::string& path, std::vector<char>& data) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Failed to open shader file: " + path);
    size_t size = file.tellg();
    file.seekg(0);
    data.resize(size);
    file.read(data.data(), size);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: dxil_reflect <vertex.dxil> <pixel.dxil> <output_header.h>\n";
        return 1;
    }

    const char* vertexPath = argv[1];
    const char* pixelPath = argv[2];
    const char* headerPath = argv[3];

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

        D3D12_SHADER_DESC vsDesc, psDesc;
        vsReflect->GetDesc(&vsDesc);
        psReflect->GetDesc(&psDesc);
        
        UINT stride = 0;
        for (UINT i = 0; i < vsDesc.InputParameters; ++i) {
            D3D12_SIGNATURE_PARAMETER_DESC param;
            vsReflect->GetInputParameterDesc(i, &param);
            UINT componentCount = __popcnt(param.Mask);
            stride += componentCount * 4;
        }

        std::ofstream out(headerPath);
        out << "#pragma once\n#include \"renderer_api.h\"\n\n";
        out << "// Auto-generated pipeline reflection\nnamespace ShaderReflection {\n";

        // Vertex layout
        out << "    static constexpr VertexAttributeDesc vertexAttributes[] = {\n";
        UINT offset = 0;
        for (UINT i = 0; i < vsDesc.InputParameters; ++i) {
            D3D12_SIGNATURE_PARAMETER_DESC param;
            vsReflect->GetInputParameterDesc(i, &param);
            UINT componentCount = __popcnt(param.Mask);
            out << "        { " << i << ", 0, " << offset << " }, // " << param.SemanticName << "\n";
            offset += componentCount * 4; // 4 bytes per float
        }
        out << "    };\n\n";
        out << "    static constexpr VertexLayoutDesc vertexLayout = {\n";
        out << "        .stride = " << stride << ",\n";
        out << "        .attributes = vertexAttributes,\n";
        out << "        .attribute_count = " << vsDesc.InputParameters << "\n";
        out << "    };\n\n";


        // Combined resources
        out << "    static constexpr BindGroupLayoutEntry bindGroupEntries[] = {\n";
        for (UINT i = 0; i < vsDesc.BoundResources; ++i) {
            D3D12_SHADER_INPUT_BIND_DESC bind;
            vsReflect->GetResourceBindingDesc(i, &bind);
            out << "        { " << bind.BindPoint << ", BindGroupEntryType::UniformBuffer, (uint32_t)ShaderStage::Vertex }, // " << bind.Name << "\n";
        }
        for (UINT i = 0; i < psDesc.BoundResources; ++i) {
            D3D12_SHADER_INPUT_BIND_DESC bind;
            psReflect->GetResourceBindingDesc(i, &bind);
            out << "        { " << bind.BindPoint << ", BindGroupEntryType::Texture, (uint32_t)ShaderStage::Fragment }, // " << bind.Name << "\n";
        }
        out << "    };\n\n";
        out << "    static constexpr BindGroupLayoutDesc bindGroupLayout = {\n";
        out << "        .entries = bindGroupEntries,\n";
        out << "        .entry_count = " << (vsDesc.BoundResources + psDesc.BoundResources) << "\n";
        out << "    };\n";

        out << "}\n";
        out.close();

        std::cout << "Reflection header generated: " << headerPath << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
