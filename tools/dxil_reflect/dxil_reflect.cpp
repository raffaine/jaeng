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

struct ReflectData {
    struct VSParam {
        uint32_t format;
        uint32_t offset;
        std::string semanticName;
    };

    struct BoundResource {
        uint32_t bindPoint;
        std::string type;
        std::string name;
    };

    std::string name;
    std::vector<VSParam> vsParams;
    uint32_t stride;
    std::vector<BoundResource> vsBindings;
    std::vector<BoundResource> psBindings;
};

ReflectData fromReflection(ID3D12ShaderReflection* vsr, ID3D12ShaderReflection* psr, const std::string& name) {
    ReflectData r { .name = name };
    
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
    
    for (UINT i = 0; i < vsDesc.BoundResources; ++i) {
        D3D12_SHADER_INPUT_BIND_DESC bind;
        vsr->GetResourceBindingDesc(i, &bind);
        r.vsBindings.emplace_back(bind.BindPoint, "uniform", bind.Name);
    }
    for (UINT i = 0; i < psDesc.BoundResources; ++i) {
        D3D12_SHADER_INPUT_BIND_DESC bind;
        psr->GetResourceBindingDesc(i, &bind);
        std::string name(bind.Name);
        r.psBindings.emplace_back(bind.BindPoint, name.starts_with("t")?"texture":"sampler", std::move(name));
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
    out <<    "  \"bindGroups\": [\n";
    i = 0;
    const auto sz = reflect.vsBindings.size() + reflect.psBindings.size();
    for (auto p : reflect.vsBindings) {
        out << "   { \"name\": \"" << p.name << "\", \"binding\": \"" << p.bindPoint << "\", \"type\": \"" << p.type <<"\", \"stage\": \"vertex\"}";
        out << ((++i == sz)? "\n" : ",\n");
    }
    for (auto p : reflect.psBindings) {
        out << "   { \"name\": \"" << p.name << "\", \"binding\": \"" << p.bindPoint << "\", \"type\": \"" << p.type <<"\", \"stage\": \"pixel\"}";
        out << ((++i == sz)? "\n" : ",\n");
    }
    out <<    "  ]\n}\n\n";
}

void outputHeader(const ReflectData& rd, const char* headerPath, const char* vertexPath, const char* pixelPath) {
        std::ofstream out(headerPath);
        out << "#pragma once\n#include \"renderer_api.h\"\n\n";
        out << "#include <fstream>\n#include <vector>\n#include <stdexcept>\n#include <string>\n\n";
        out << "// Auto-generated pipeline reflection\nnamespace ShaderReflection {\n";

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

        // Combined resources
        out << "    static constexpr BindGroupLayoutEntry bindGroupEntries[] = {\n";
        for (auto& bind : rd.vsBindings) {
            out << "        { " << bind.bindPoint << ", BindGroupEntryType::UniformBuffer, (uint32_t)ShaderStage::Vertex }, // " << bind.name << "\n";
        }
        for (auto& bind : rd.psBindings) {
            out << "        { " << bind.bindPoint << ", BindGroupEntryType::Texture, (uint32_t)ShaderStage::Fragment }, // " << bind.name << "\n";
        }
        out << "    };\n\n";
        out << "    static constexpr BindGroupLayoutDesc bindGroupLayout = {\n";
        out << "        .entries = bindGroupEntries,\n";
        out << "        .entry_count = " << (rd.vsBindings.size() + rd.psBindings.size()) << "\n";
        out << "    };\n";
        // Closes namespace
        out << "}\n";

        // Helper to Load the associated shaders
        out << "\ninline void LoadShaders(RendererAPI* api, ShaderModuleHandle& vsHandle, ShaderModuleHandle& fsHandle) {\n";
        out << "    // Vertex Shader\n";
        out << "    {\n";
        out << "        std::ifstream vsFile(\"" << vertexPath << "\", std::ios::binary | std::ios::ate);\n";
        out << "        if (!vsFile) throw std::runtime_error(\"Failed to open vertex shader file\");\n";
        out << "        size_t size = vsFile.tellg();\n";
        out << "        vsFile.seekg(0);\n";
        out << "        std::vector<char> data(size);\n";
        out << "        vsFile.read(data.data(), size);\n";
        out << "        ShaderModuleDesc desc { ShaderStage::Vertex, data.data(), (uint32_t)data.size(), 0 };\n";
        out << "        vsHandle = api->create_shader_module(&desc);\n";
        out << "    }\n\n";
        out << "    // Pixel Shader\n";
        out << "    {\n";
        out << "        std::ifstream psFile(\"" << pixelPath << "\", std::ios::binary | std::ios::ate);\n";
        out << "        if (!psFile) throw std::runtime_error(\"Failed to open pixel shader file\");\n";
        out << "        size_t size = psFile.tellg();\n";
        out << "        psFile.seekg(0);\n";
        out << "        std::vector<char> data(size);\n";
        out << "        psFile.read(data.data(), size);\n";
        out << "        ShaderModuleDesc desc { ShaderStage::Fragment, data.data(), (uint32_t)data.size(), 0 };\n";
        out << "        fsHandle = api->create_shader_module(&desc);\n";
        out << "    }\n";
        out << "}\n";

        // Inline function to create all pipeline-related resources
        out << "\ninline void CreatePipelineResources(RendererAPI* api, PipelineReflectionResources& out) {\n";

        // Load shaders using previously generated inline function
        out << "    LoadShaders(api, out.vs, out.fs);\n\n";

        // Create graphics pipeline
        out << "    GraphicsPipelineDesc pipelineDesc {\n";
        out << "        .vs = out.vs,\n";
        out << "        .fs = out.fs,\n";
        out << "        .topology = PrimitiveTopology::TriangleList,\n";
        out << "        .vertex_layout = ShaderReflection::vertexLayout,\n";
        out << "        .color_format = TextureFormat::RGBA8_UNORM,\n";
        out << "        .depth_stencil = {}\n";
        out << "    };\n";
        out << "    out.pipeline = api->create_graphics_pipeline(&pipelineDesc);\n\n";

        // Create uniform buffer
        const UINT cbSize = 256;
        out << "    BufferDesc bufferDesc { " << cbSize << ", BufferUsage_Uniform };\n";
        out << "    out.uniformBuffer = api->create_buffer(&bufferDesc, nullptr);\n\n";

        // Create texture
        out << "    TextureDesc texDesc { TextureFormat::RGBA8_UNORM, 256, 256, 1, 1, 0 };\n";
        out << "    out.texture = api->create_texture(&texDesc, nullptr);\n\n";

        // Create sampler
        out << "    SamplerDesc samplerDesc { SamplerFilter::Linear, AddressMode::Repeat, AddressMode::Repeat, AddressMode::Repeat,\n";
        out << "        0.0f, 0.0f, 1.0f, {0, 0, 0, 0} };\n";
        out << "    out.sampler = api->create_sampler(&samplerDesc);\n\n";

        // Create bind group layout
        out << "    out.bindGroupLayout = api->create_bind_group_layout(&ShaderReflection::bindGroupLayout);\n\n";

        // Create bind group entries
        out << "    BindGroupEntry entries[] = {\n";
        out << "        { BindGroupEntryType::UniformBuffer, out.uniformBuffer, 0, " << cbSize << ", {}, {} },\n";
        out << "        { BindGroupEntryType::Sampler, {}, 0, 0, {}, out.sampler },\n";
        out << "        { BindGroupEntryType::Texture, {}, 0, 0, out.texture, {} }\n";
        out << "    };\n";
        out << "    BindGroupDesc bindGroupDesc { out.bindGroupLayout, entries, " << (rd.vsBindings.size() + rd.psBindings.size()) << " };\n";
        out << "    out.bindGroup = api->create_bind_group(&bindGroupDesc);\n";
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
