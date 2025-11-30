#include "materialsys.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>

using nlohmann::json;

MaterialMetadata fromJson(const json& matJson) {
    MaterialMetadata m {
        .name = matJson["name"].get<std::string>(),
    };
    auto shaders = matJson["shader"];
    m.vsPath = shaders["vertex"].get<std::string>();
    m.psPath = shaders["pixel"].get<std::string>();
    m.reflectPath = shaders["reflection"].get<std::string>();
    for (auto& t : matJson["textures"]) {
        TextureData td {
            .path = t["path"],
            .width = t["width"],
            .height = t["height"]
        };
        if (t.contains("format")) td.format = t["format"];
        auto s = t["sampler"];
        td.sampler = {s["filter"], s["addressModeU"], s["addressModeV"]};
        m.textures.emplace_back(std::move(td));
    }
    // TODO: Properly parse and separate scalar and vector
    if (matJson.contains("parameters")) {
        auto params = matJson["parameters"];
        if (params.contains("color")) {
            int i = 0;
            glm::vec4 color;
            for (auto& ce : params["color"]) {
                color[i++] = ce.get<float>();
            }
            m.vectorParams.emplace("color", color); // harcoding for now, should iterate over properties
        }
        if (params.contains("roughness")) m.scalarParams.emplace("roughness", params["roughness"].get<float>());
        if (params.contains("metallic")) m.scalarParams.emplace("metallic", params["metallic"].get<float>());
    }
    for (auto& cbEntry : matJson["constantBuffers"]) {
        m.constantBuffers.emplace_back(cbEntry["name"], cbEntry["size"].get<uint32_t>(), cbEntry["binding"].get<uint32_t>());
    }
    if (matJson.contains("pipelineStates")) {
    }

    return m;
}

template <uint64_t N>
size_t firstAvailable(std::bitset<N>& slotUsage) {
    for (size_t i = 0; i < N; ++i) {
        if (!slotUsage.test(i)) {
            slotUsage.set(i);
            return i;
        }
    }
    return 0;
}

jaeng::result<MaterialHandle> MaterialSystem::_createMaterialMetadata(IFileManager& fm, const std::string& path)
{
    // Fail fast in case we are out of slots
    JAENG_ERROR_IF(slotUsage.count() >= MaterialSystem::MAX_MATERIALS, jaeng::error_code::no_resource, "[Material] No space available");

    // Get the File Data from FileManager
    JAENG_TRY_ASSIGN(auto fdata, fm.load(path));

    // Parse it and Store
    try {
        auto matJson = json::parse(fdata.begin(), fdata.end());
        auto mat = fromJson(matJson);

        auto h = MaterialHandle(firstAvailable(slotUsage));
        storage.emplace(h, std::move(mat));

        return h;        
    } catch (const std::exception& e) {
        JAENG_ERROR(jaeng::error_code::unknown_error, e.what());
    }
}

jaeng::result<> MaterialSystem::_createMaterialResources(
    IFileManager& fm,
    MaterialSystem::Storage& material,
    const VertexLayoutDesc* vtxLayout,
    size_t vtxLayoutCount,
    const std::string* requiredSemantics, // count should match attributes on vertex layout
    const BindGroupLayoutDesc* bindGroups,
    size_t bindGroupCount)
{
    auto gfx = renderer.lock();
    JAENG_ERROR_IF(!gfx, jaeng::error_code::resource_not_ready, "[Material] Renderer is not available.");

    // Create Shaders
    {   // Vertex Shader
        JAENG_TRY_ASSIGN(auto data, fm.load(material.mat.vsPath));
        ShaderModuleDesc desc { ShaderStage::Vertex, data.data(), (uint32_t)data.size(), 0 };
        material.bg.vertexShader = gfx->create_shader_module(&desc);
    }
    {   // Pixel Shader
        JAENG_TRY_ASSIGN(auto data, fm.load(material.mat.psPath));
        ShaderModuleDesc desc { ShaderStage::Fragment, data.data(), (uint32_t)data.size(), 0 };
        material.bg.pixelShader = gfx->create_shader_module(&desc);
    }

    // Parse Input Layout for required Semantics and Register on Renderer
    material.bg.vertexLayout = gfx->create_vertex_layout(vtxLayout);
    for (int i = 0; i < vtxLayout[0].attribute_count; i++) {
        material.bg.requiredSemantics.emplace_back(requiredSemantics[i]);
    }

    // Create Texture and Sampler Resources (TODO: Check for errors)
    for (auto t : material.mat.textures) {
        JAENG_TRY_ASSIGN(auto pixels, fm.load(t.path));
        TextureDesc td{ TextureFormat::RGBA8_UNORM, t.width, t.height, 1, 1, 0 };
        TextureHandle tex = gfx->create_texture(&td, pixels.data());
        material.bg.textures.emplace_back(std::move(tex));
        SamplerDesc sd{
            .filter = SamplerFilter::Linear,
            .address_u = AddressMode::Repeat, .address_v = AddressMode::Repeat, .address_w = AddressMode::Repeat,
            .mip_lod_bias = 0.0f, .min_lod = 0.0f, .max_lod = 1000.0f,
            .border_color = {0.0f, 0.0f, 0.0f, 1.0f},
        };
        SamplerHandle samp = gfx->create_sampler(&sd);
        material.bg.samplers.emplace_back(std::move(samp));
    }

    // Create Constant Buffers
    for (auto& cbEntry : material.mat.constantBuffers) {
        BufferDesc   cbDesc{cbEntry.size, BufferUsage_Uniform};
        BufferHandle cb = gfx->create_buffer(&cbDesc, nullptr);
        material.bg.constantBuffers.emplace_back(std::move(cb));
    }

    // Create Bind Group Layout (assume one only for now) on the renderer
    material.bg.bindGroupLayout = gfx->create_bind_group_layout(bindGroups);

    // Create Bind Group
    std::vector<BindGroupEntry> bges;
    for (auto& tex : material.bg.textures)
        bges.push_back(BindGroupEntry { .type = BindGroupEntryType::Texture, .texture = tex });
    for (auto& smp : material.bg.samplers)
        bges.push_back(BindGroupEntry { .type = BindGroupEntryType::Sampler, .sampler = smp });
    for (int i = 0; i < material.bg.constantBuffers.size(); i++)
        bges.push_back(BindGroupEntry { .type = BindGroupEntryType::UniformBuffer, .buffer = material.bg.constantBuffers[i], .offset = 0, .size = material.mat.constantBuffers[i].size});

    BindGroupDesc bgd{material.bg.bindGroupLayout, bges.data(), static_cast<uint32_t>(bges.size())};
    material.bg.bindGroup = gfx->create_bind_group(&bgd);

    return {};
}

jaeng::result<MaterialHandle> MaterialSystem::createMaterial(const std::string& path)
{
    auto fm = fileManager.lock();
    JAENG_ERROR_IF(!fm, jaeng::error_code::resource_not_ready, "[Material] File Manager is not available");

    JAENG_TRY_ASSIGN(MaterialHandle h, _createMaterialMetadata(*fm, path));

    // Retrieves Metadata and future storage
    auto material = storage[h];

    // Parse Shader Reflect for Material Resource Descriptions
    // Need material to also tell where the reflection is material.mat.reflectPath

    // Create Resources
    //JAENG_TRY(_createMaterialResources(material, vertexLayout, vertexLayoutCount, requiredSemantics, bindGroups, bindGroupCount));

    return h;
}

jaeng::result<MaterialHandle> MaterialSystem::createMaterial(
    const std::string& path,
    const VertexLayoutDesc* vertexLayout,
    size_t vertexLayoutCount,
    const std::string* requiredSemantics, // count should match attributes on vertex layout
    const BindGroupLayoutDesc* bindGroups,
    size_t bindGroupCount)
{
    JAENG_ERROR_IF((bindGroupCount == 0) || (vertexLayoutCount == 0), jaeng::error_code::invalid_args, "[Material] No Bind Group or Vertex Layout passed.");

    auto fm = fileManager.lock();
    JAENG_ERROR_IF(!fm, jaeng::error_code::resource_not_ready, "[Material] File Manager is not available");

    JAENG_TRY_ASSIGN(MaterialHandle h, _createMaterialMetadata(*fm, path));
    
    auto& material = storage[h];
    // Shader Reflection provided, Pass it to Renderer
    JAENG_TRY(_createMaterialResources(*fm, material, vertexLayout, vertexLayoutCount, requiredSemantics, bindGroups, bindGroupCount));

    return h;
}

// Destroy material
void MaterialSystem::destroyMaterial(MaterialHandle handle)
{
    // TODO: Request Renderer for Resource Destructions
    storage.erase(handle);
}

// Query GPU bindings for rendering
jaeng::result<const MaterialBindings*> MaterialSystem::getBindData(MaterialHandle handle) const
{
    auto matIt = storage.find(handle);
    JAENG_ERROR_IF(matIt == storage.end(), jaeng::error_code::no_resource, "[Material] No Binds available as Material is not available");

    return &(matIt->second).bg;
}

jaeng::result<const MaterialMetadata*> MaterialSystem::getMetadata(MaterialHandle handle) const
{
    auto matIt = storage.find(handle);
    JAENG_ERROR_IF(matIt == storage.end(), jaeng::error_code::no_resource, "[Material] No Metadata available as Material is not available");

    return &(matIt->second).mat;
}

jaeng::result<> MaterialSystem::reloadMaterial(MaterialHandle handle)
{
    return jaeng::Error::fromMessage(static_cast<int>(jaeng::error_code::invalid_operation), "[Material] Not Implemented");
}
