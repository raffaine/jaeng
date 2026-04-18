#include "materialsys.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>

using nlohmann::json;

namespace jaeng {

MaterialMetadata fromJson(const json& matJson, const std::string& path) {
    MaterialMetadata m {
        .path = path,
        .name = matJson["name"].get<std::string>(),
    };
    auto shaders = matJson["shader"];
    m.vsPath = shaders["vertex"].get<std::string>();
    m.psPath = shaders["pixel"].get<std::string>();
    m.reflectPath = shaders["reflection"].get<std::string>();
    for (auto& t : matJson["textures"]) {
        TextureData td {
            .path = t["path"],
            .format = t.contains("format") ? t["format"].get<std::string>() : "rgba8",
            .width = t["width"],
            .height = t["height"]
        };
        auto s = t["sampler"];
        td.sampler = {s["filter"], s["addressModeU"], s["addressModeV"]};
        m.textures.emplace_back(std::move(td));
    }

    if (matJson.contains("parameters")) {
        auto params = matJson["parameters"];
        for (auto it = params.begin(); it != params.end(); ++it) {
            if (it.value().is_array()) {
                glm::vec4 vec(0.0f);
                int i = 0;
                for (auto& e : it.value()) {
                    if (i < 4) vec[i++] = e.get<float>();
                }
                m.vectorParams.emplace(it.key(), vec);
            } else if (it.value().is_number()) {
                m.scalarParams.emplace(it.key(), it.value().get<float>());
            }
        }
    }

    for (auto& cbEntry : matJson["constantBuffers"]) {
        m.constantBuffers.emplace_back(cbEntry["name"], cbEntry["size"].get<uint32_t>(), cbEntry["binding"].get<uint32_t>());
    }

    if (matJson.contains("pipelineStates")) {
        auto ps = matJson["pipelineStates"];
        if (ps.contains("blend")) {
            auto b = ps["blend"];
            m.blendState = { b["enabled"].get<bool>(), b["srcFactor"].get<std::string>(), b["dstFactor"].get<std::string>() };
        }
        if (ps.contains("rasterizer")) {
            auto r = ps["rasterizer"];
            m.rasterizer = { r["cullMode"].get<std::string>(), r["fillMode"].get<std::string>() };
        }
        if (ps.contains("depthStencil")) {
            auto d = ps["depthStencil"];
            m.depthStencil = { d["depthTest"].get<bool>(), d["depthWrite"].get<bool>() };
        }
    }

    return m;
}

template<size_t N>
size_t firstAvailable(std::bitset<N>& slotUsage) {
    for (size_t i = 0; i < N; ++i) {
        if (!slotUsage.test(i)) {
            slotUsage.set(i);
            return i;
        }
    }
    return static_cast<size_t>(-1);
}

result<MaterialHandle> MaterialSystem::_createMaterialMetadata(IFileManager& fm, const std::string& path)
{
    JAENG_LOG_DEBUG("[Material] Creating metadata for {}", path);
    // Fail fast in case we are out of slots
    JAENG_ERROR_IF(slotUsage.count() >= MaterialSystem::MAX_MATERIALS, error_code::no_resource, "[Material] No space available");

    // Get the File Data from FileManager
    JAENG_TRY_ASSIGN(auto fdata, fm.load(path));

    // Parse it and Store
    try {
        auto matJson = json::parse(fdata.begin(), fdata.end());
        auto mat = fromJson(matJson, path);

        auto h = MaterialHandle(firstAvailable(slotUsage));
        JAENG_LOG_DEBUG("[Material] Assigned handle {} for {}", (uint32_t)h, path);
        storage.emplace(h, std::make_shared<Storage>(Storage{ .mat = std::move(mat) }));

        return h;        
    } catch (const std::exception& e) {
        JAENG_ERROR(error_code::unknown_error, e.what());
    }
}

result<> MaterialSystem::_createMaterialResources(
    IFileManager& fm,
    MaterialSystem::Storage& material,
    const VertexLayoutDesc* vtxLayout,
    size_t vtxLayoutCount,
    const char* requiredSemantics[])
{
    auto gfx = renderer.lock();
    JAENG_ERROR_IF(!gfx, error_code::resource_not_ready, "[Material] Renderer is not available.");

    // Clear existing bindings if any
    material.bg.textures.clear();
    material.bg.samplers.clear();
    material.bg.constantBuffers.clear();
    material.bg.textureIndices.clear();
    material.bg.samplerIndices.clear();
    material.bg.requiredSemantics.clear();

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
    for (uint32_t i = 0; i < vtxLayoutCount; i++) {
        material.bg.requiredSemantics.emplace_back(requiredSemantics[i]);
    }

    // Create Texture and Sampler Resources
    for (auto& t : material.mat.textures) {
        JAENG_TRY_ASSIGN(auto pixels, fm.load(t.path));
        TextureDesc td{ TextureFormat::RGBA8_UNORM, t.width, t.height, 1, 1, 0 };
        TextureHandle tex = gfx->create_texture(&td, pixels.data());
        material.bg.textures.emplace_back(tex);
        
        SamplerDesc sd{
            .filter = (t.sampler.filter == "linear") ? SamplerFilter::Linear : SamplerFilter::Nearest,
            .address_u = AddressMode::Repeat, .address_v = AddressMode::Repeat, .address_w = AddressMode::Repeat,
            .mip_lod_bias = 0.0f, .min_lod = 0.0f, .max_lod = 1000.0f,
            .border_color = {0.0f, 0.0f, 0.0f, 1.0f},
        };
        SamplerHandle samp = gfx->create_sampler(&sd);
        material.bg.samplers.emplace_back(samp);

        // Store Bindless Indices
        material.bg.textureIndices.push_back(gfx->get_texture_index(tex));
        material.bg.samplerIndices.push_back(gfx->get_sampler_index(samp));
    }

    // Create Constant Buffers
    for (auto& cbEntry : material.mat.constantBuffers) {
        BufferDesc   cbDesc{cbEntry.size, BufferUsage_Uniform};
        BufferHandle cb = gfx->create_buffer(&cbDesc, nullptr);
        material.bg.constantBuffers.emplace_back(cb);
    }

    return {};
}

result<MaterialSystem::ReflectionData> MaterialSystem::_loadReflection(IFileManager& fm, const std::string& path) {
    JAENG_LOG_DEBUG("[Material] Loading reflection from {}", path);
    JAENG_TRY_ASSIGN(std::vector<uint8_t> data, fm.load(path));
    try {
        json j = json::parse(data);
        ReflectionData rd;
        rd.stride = j["stride"].get<uint32_t>();
        for (auto& attr : j["attributes"]) {
            std::string sem = attr["semantic"].get<std::string>();
            uint32_t offset = attr["offset"].get<uint32_t>();
            
            VertexAttributeDesc ad{};
            ad.offset = offset;
            ad.location = static_cast<uint32_t>(rd.attributes.size());
            
            if (sem == "POSITION") ad.format = VertexAttributeFormat::Float3;
            else if (sem == "COLOR") ad.format = VertexAttributeFormat::Float3;
            else if (sem == "TEXCOORD") ad.format = VertexAttributeFormat::Float2;
            else ad.format = VertexAttributeFormat::Float4; // fallback

            size_t len = sem.length() < (sizeof(ad.semanticName) - 1) ? sem.length() : (sizeof(ad.semanticName) - 1);
            std::memcpy(ad.semanticName, sem.c_str(), len);
            ad.semanticName[len] = '\0';

            rd.attributes.push_back(ad);
            rd.semantics.push_back(sem);
        }

        return rd;
    } catch (const std::exception& e) {
        JAENG_LOG_ERROR("[Material] Reflection Parse Error: {}", e.what());
        return Error::fromMessage((int)error_code::unknown_error, std::string("[Material] Reflection Parse Error: ") + e.what());
    }
}

async::Task<result<MaterialHandle>> MaterialSystem::createMaterialAsync(const std::string& path)
{
    auto fm = fileManager;

    MaterialHandle h;
    JAENG_TRY_ASSIGN_ASYNC(h, _createMaterialMetadataAsync(*fm, path));
    
    std::shared_ptr<Storage> material;
    {
        std::lock_guard<std::mutex> lock(storageMutex);
        material = storage[h];
    }

    JAENG_LOG_DEBUG("[Material] Fetching reflection for {}", material->mat.name);
    ReflectionData rd;
    JAENG_TRY_ASSIGN_ASYNC(rd, _loadReflectionAsync(*fm, material->mat.reflectPath));

    // Create Resources
    JAENG_LOG_DEBUG("[Material] Creating resources for {}", material->mat.name);
    std::vector<const char*> semPtrs;
    for (const auto& s : rd.semantics) semPtrs.push_back(s.c_str());

    VertexLayoutDesc vld { .stride = rd.stride, .attributes = rd.attributes.data(), .attribute_count = static_cast<uint32_t>(rd.attributes.size()) };
    JAENG_TRY_ASYNC(_createMaterialResourcesAsync(*fm, *material, &vld, vld.attribute_count, semPtrs.data()));

    co_return h;
}

async::Task<result<MaterialSystem::ReflectionData>> MaterialSystem::_loadReflectionAsync(IFileManager& fm, const std::string& path) {
    JAENG_LOG_DEBUG("[Material] Loading reflection (async) from {}", path);
    std::vector<uint8_t> data;
    JAENG_TRY_ASSIGN_ASYNC(data, fm.loadAsync(path));
    if (data.empty()) {
        JAENG_LOG_ERROR("[Material] Reflection file is empty: {}", path);
        co_return jaeng::Error::fromMessage((int)error_code::no_resource, "Reflection file is empty");
    }
    try {
        json j = json::parse(data);
        ReflectionData rd;
        rd.stride = j["stride"].get<uint32_t>();
        for (auto& attr : j["attributes"]) {
            std::string sem = attr["semantic"].get<std::string>();
            uint32_t offset = attr["offset"].get<uint32_t>();
            
            VertexAttributeDesc ad{};
            ad.offset = offset;
            ad.location = static_cast<uint32_t>(rd.attributes.size());
            
            if (sem == "POSITION") ad.format = VertexAttributeFormat::Float3;
            else if (sem == "COLOR") ad.format = VertexAttributeFormat::Float3;
            else if (sem == "TEXCOORD") ad.format = VertexAttributeFormat::Float2;
            else ad.format = VertexAttributeFormat::Float4; // fallback

            size_t len = sem.length() < (sizeof(ad.semanticName) - 1) ? sem.length() : (sizeof(ad.semanticName) - 1);
            std::memcpy(ad.semanticName, sem.c_str(), len);
            ad.semanticName[len] = '\0';

            rd.attributes.push_back(ad);
            rd.semantics.push_back(sem);
        }

        co_return rd;
    } catch (const std::exception& e) {
        JAENG_LOG_ERROR("[Material] Reflection Parse Error: {}", e.what());
        co_return Error::fromMessage((int)error_code::unknown_error, std::string("[Material] Reflection Parse Error: ") + e.what());
    }
}

async::Task<result<MaterialHandle>> MaterialSystem::_createMaterialMetadataAsync(IFileManager& fm, const std::string& path)
{
    JAENG_LOG_DEBUG("[Material] Creating metadata (async) for {}", path);
    // Fail fast in case we are out of slots
    JAENG_ERROR_IF_ASYNC(slotUsage.count() >= MaterialSystem::MAX_MATERIALS, error_code::no_resource, "[Material] No space available");

    // Get the File Data from FileManager
    std::vector<uint8_t> fdata;
    JAENG_TRY_ASSIGN_ASYNC(fdata, fm.loadAsync(path));

    // Parse it and Store
    try {
        auto matJson = json::parse(fdata.begin(), fdata.end());
        auto mat = fromJson(matJson, path);

        auto h = MaterialHandle(firstAvailable(slotUsage));
        JAENG_LOG_DEBUG("[Material] Assigned handle {} for {}", (uint32_t)h, path);
        {
            std::lock_guard<std::mutex> lock(storageMutex);
            storage.emplace(h, std::make_shared<Storage>(Storage{ .mat = std::move(mat) }));
        }

        co_return h;        
    } catch (const std::exception& e) {
        co_return jaeng::Error::fromMessage((int)error_code::unknown_error, e.what());
    }
}

async::Task<result<>> MaterialSystem::_createMaterialResourcesAsync(
    IFileManager& fm,
    MaterialSystem::Storage& material,
    const VertexLayoutDesc* vtxLayout,
    size_t vtxLayoutCount,
    const char* requiredSemantics[])
{
    auto gfx = renderer.lock();
    JAENG_ERROR_IF_ASYNC(!gfx, error_code::resource_not_ready, "[Material] Renderer is not available.");

    // Clear existing bindings if any
    material.bg.textures.clear();
    material.bg.samplers.clear();
    material.bg.constantBuffers.clear();
    material.bg.textureIndices.clear();
    material.bg.samplerIndices.clear();
    material.bg.requiredSemantics.clear();

    // Create Shaders
    {   // Vertex Shader
        JAENG_LOG_DEBUG("[Material] Loading VS: {}", material.mat.vsPath);
        std::vector<uint8_t> data;
        JAENG_TRY_ASSIGN_ASYNC(data, fm.loadAsync(material.mat.vsPath));
        JAENG_LOG_DEBUG("[Material] VS loaded, size={}, creating module", data.size());
        ShaderModuleDesc desc { ShaderStage::Vertex, data.data(), (uint32_t)data.size(), 0 };
        material.bg.vertexShader = gfx->create_shader_module(&desc);
    }
    {   // Pixel Shader
        JAENG_LOG_DEBUG("[Material] Loading PS: {}", material.mat.psPath);
        std::vector<uint8_t> data;
        JAENG_TRY_ASSIGN_ASYNC(data, fm.loadAsync(material.mat.psPath));
        JAENG_LOG_DEBUG("[Material] PS loaded, size={}, creating module", data.size());
        ShaderModuleDesc desc { ShaderStage::Fragment, data.data(), (uint32_t)data.size(), 0 };
        material.bg.pixelShader = gfx->create_shader_module(&desc);
    }

    // Parse Input Layout for required Semantics and Register on Renderer
    JAENG_LOG_DEBUG("[Material] Creating vertex layout");
    for (uint32_t i = 0; i < vtxLayout->attribute_count; ++i) {
        JAENG_LOG_DEBUG("[Material] Attr[{}]: semanticName={}, offset={}, format={}", i, (vtxLayout->attributes[i].semanticName ? vtxLayout->attributes[i].semanticName : "NULL"), vtxLayout->attributes[i].offset, (uint32_t)vtxLayout->attributes[i].format);
    }
    material.bg.vertexLayout = gfx->create_vertex_layout(vtxLayout);
    for (uint32_t i = 0; i < vtxLayoutCount; i++) {
        material.bg.requiredSemantics.emplace_back(requiredSemantics[i]);
    }

    // Create Texture and Sampler Resources
    for (auto& t : material.mat.textures) {
        JAENG_LOG_DEBUG("[Material] Loading Texture: {}", t.path);
        std::vector<uint8_t> pixels;
        JAENG_TRY_ASSIGN_ASYNC(pixels, fm.loadAsync(t.path));
        JAENG_LOG_DEBUG("[Material] Texture loaded, size={}, creating resource", pixels.size());
        TextureDesc td{ TextureFormat::RGBA8_UNORM, t.width, t.height, 1, 1, 0 };
        TextureHandle tex = gfx->create_texture(&td, pixels.data());
        material.bg.textures.emplace_back(tex);
        
        SamplerDesc sd{
            .filter = (t.sampler.filter == "linear") ? SamplerFilter::Linear : SamplerFilter::Nearest,
            .address_u = AddressMode::Repeat, .address_v = AddressMode::Repeat, .address_w = AddressMode::Repeat,
            .mip_lod_bias = 0.0f, .min_lod = 0.0f, .max_lod = 1000.0f,
            .border_color = {0.0f, 0.0f, 0.0f, 1.0f},
        };
        SamplerHandle samp = gfx->create_sampler(&sd);
        material.bg.samplers.emplace_back(samp);

        // Store Bindless Indices
        material.bg.textureIndices.push_back(gfx->get_texture_index(tex));
        material.bg.samplerIndices.push_back(gfx->get_sampler_index(samp));
    }

    // Create Constant Buffers
    for (auto& cbEntry : material.mat.constantBuffers) {
        JAENG_LOG_DEBUG("[Material] Creating CB: {}, size={}", cbEntry.name, cbEntry.size);
        BufferDesc   cbDesc{cbEntry.size, BufferUsage_Uniform};
        BufferHandle cb = gfx->create_buffer(&cbDesc, nullptr);
        material.bg.constantBuffers.emplace_back(cb);
    }

    JAENG_LOG_DEBUG("[Material] _createMaterialResourcesAsync: Finished for {}", material.mat.name);
    co_return {};
}

result<MaterialHandle> MaterialSystem::createMaterial(const std::string& path)
{
    auto fm = fileManager;

    JAENG_TRY_ASSIGN(MaterialHandle h, _createMaterialMetadata(*fm, path));
    auto& material = storage[h];

    JAENG_LOG_DEBUG("[Material] Fetching reflection for {}", material->mat.name);
    JAENG_TRY_ASSIGN(ReflectionData rd, _loadReflection(*fm, material->mat.reflectPath));

    // Create Resources
    JAENG_LOG_DEBUG("[Material] Creating resources for {}", material->mat.name);
    std::vector<const char*> semPtrs;
    for (const auto& s : rd.semantics) semPtrs.push_back(s.c_str());

    VertexLayoutDesc vld { .stride = rd.stride, .attributes = rd.attributes.data(), .attribute_count = static_cast<uint32_t>(rd.attributes.size()) };
    JAENG_TRY(_createMaterialResources(*fm, *material, &vld, vld.attribute_count, semPtrs.data()));

    return h;
}

result<MaterialHandle> MaterialSystem::createMaterial(
    const std::string& path,
    const VertexLayoutDesc* vertexLayout,
    size_t vertexLayoutCount,
    const char* requiredSemantics[]) // count should match attributes on vertex layout
{
    JAENG_ERROR_IF((vertexLayoutCount == 0), error_code::invalid_args, "[Material] No Vertex Layout passed.");

    auto fm = fileManager;

    JAENG_TRY_ASSIGN(MaterialHandle h, _createMaterialMetadata(*fm, path));
    
    auto& material = storage[h];
    // Shader Reflection provided, Pass it to Renderer
    JAENG_TRY(_createMaterialResources(*fm, *material, vertexLayout, vertexLayoutCount, requiredSemantics));

    return h;
}

void MaterialSystem::destroyMaterial(MaterialHandle handle)
{
    auto it = storage.find(handle);
    if (it != storage.end()) {
        auto gfx = renderer.lock();
        if (gfx) {
            auto& bg = it->second->bg;
            if (bg.vertexShader) gfx->destroy_shader_module(bg.vertexShader);
            if (bg.pixelShader) gfx->destroy_shader_module(bg.pixelShader);
            if (bg.vertexLayout) gfx->destroy_vertex_layout(bg.vertexLayout);
            
            for (auto h : bg.textures) gfx->destroy_texture(h);
            for (auto h : bg.samplers) gfx->destroy_sampler(h);
            for (auto h : bg.constantBuffers) gfx->destroy_buffer(h);
        }
        storage.erase(it);
        slotUsage.reset(handle);
    }
}

result<const MaterialBindings*> MaterialSystem::getBindData(MaterialHandle handle) const
{
    std::lock_guard<std::mutex> lock(storageMutex);
    auto matIt = storage.find(handle);
    JAENG_ERROR_IF(matIt == storage.end(), error_code::no_resource, "[Material] No Binds available as Material is not available");

    return &(matIt->second->bg);
}

result<const MaterialMetadata*> MaterialSystem::getMetadata(MaterialHandle handle) const
{
    std::lock_guard<std::mutex> lock(storageMutex);
    auto matIt = storage.find(handle);
    JAENG_ERROR_IF(matIt == storage.end(), error_code::no_resource, "[Material] No Metadata available as Material is not available");

    return &(matIt->second->mat);
}

result<> MaterialSystem::reloadMaterial(MaterialHandle handle)
{
    auto it = storage.find(handle);
    JAENG_ERROR_IF(it == storage.end(), error_code::no_resource, "[Material] Cannot reload non-existent material");

    auto fm = fileManager;
    auto gfx = renderer.lock();
    JAENG_ERROR_IF(!gfx, error_code::resource_not_ready, "[Material] Renderer unavailable for reload");

    std::string path = it->second->mat.path;

    // 1. Re-load Metadata
    JAENG_TRY_ASSIGN(auto fdata, fm->load(path));
    json matJson = json::parse(fdata);
    auto newMeta = fromJson(matJson, path);

    // 2. Load Reflection
    JAENG_TRY_ASSIGN(ReflectionData rd, _loadReflection(*fm, newMeta.reflectPath));

    // 3. Create New Resources
    auto newStorage = std::make_shared<Storage>();
    newStorage->mat = std::move(newMeta);
    
    std::vector<const char*> semPtrs;
    for (const auto& s : rd.semantics) semPtrs.push_back(s.c_str());
    VertexLayoutDesc vld { .stride = rd.stride, .attributes = rd.attributes.data(), .attribute_count = static_cast<uint32_t>(rd.attributes.size()) };
    
    JAENG_TRY(_createMaterialResources(*fm, *newStorage, &vld, vld.attribute_count, semPtrs.data()));

    // 4. Destroy Old Resources
    auto& oldBg = it->second->bg;
    if (oldBg.vertexShader) gfx->destroy_shader_module(oldBg.vertexShader);
    if (oldBg.pixelShader) gfx->destroy_shader_module(oldBg.pixelShader);
    if (oldBg.vertexLayout) gfx->destroy_vertex_layout(oldBg.vertexLayout);
    for (auto h : oldBg.textures) gfx->destroy_texture(h);
    for (auto h : oldBg.samplers) gfx->destroy_sampler(h);
    for (auto h : oldBg.constantBuffers) gfx->destroy_buffer(h);

    // 5. Swap
    it->second = std::move(newStorage);

    return {};
}

void MaterialSystem::setVectorParam(MaterialHandle handle, const std::string& name, const glm::vec4& value) {
    auto it = storage.find(handle);
    if (it != storage.end()) {
        it->second->mat.vectorParams[name] = value;
    }
}

} // namespace jaeng

