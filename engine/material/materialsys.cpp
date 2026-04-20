#include "materialsys.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include "common/logging.h"

using json = nlohmann::json;

namespace jaeng {

// Local helper for bitset
static uint32_t firstAvailable(const std::bitset<MaterialSystem::MAX_MATERIALS>& bits) {
    for (uint32_t i = 0; i < MaterialSystem::MAX_MATERIALS; ++i) {
        if (!bits.test(i)) return i;
    }
    return static_cast<uint32_t>(-1);
}

#ifdef JAENG_APPLE
extern "C" {
    void jaeng_apple_run_in_autorelease_pool(void(*func)(void*), void* context);
}
#endif

async::Task<result<MaterialHandle>> MaterialSystem::createMaterialAsync(const std::string& path)
{
    JAENG_LOG_DEBUG("[Material] createMaterialAsync: {}", path);
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

    JAENG_LOG_INFO("[Material] Async creation finished: {}", material->mat.name);
    co_return h;
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

    material.bg.textures.clear();
    material.bg.samplers.clear();
    material.bg.constantBuffers.clear();
    material.bg.textureIndices.clear();
    material.bg.samplerIndices.clear();
    material.bg.requiredSemantics.clear();

    // Create Shaders
    {   // Vertex Shader
        std::vector<uint8_t> data;
        JAENG_TRY_ASSIGN_ASYNC(data, fm.loadAsync(material.mat.vsPath));
        ShaderModuleDesc desc { ShaderStage::Vertex, data.data(), (uint32_t)data.size(), 0 };
        material.bg.vertexShader = gfx->create_shader_module(&desc);
    }
    {   // Pixel Shader
        std::vector<uint8_t> data;
        JAENG_TRY_ASSIGN_ASYNC(data, fm.loadAsync(material.mat.psPath));
        ShaderModuleDesc desc { ShaderStage::Fragment, data.data(), (uint32_t)data.size(), 0 };
        material.bg.pixelShader = gfx->create_shader_module(&desc);
    }

    material.bg.vertexLayout = gfx->create_vertex_layout(vtxLayout);
    for (uint32_t i = 0; i < vtxLayoutCount; i++) {
        material.bg.requiredSemantics.emplace_back(requiredSemantics[i]);
    }

    // Create Texture and Sampler Resources
    for (auto& t : material.mat.textures) {
        std::vector<uint8_t> pixels;
        JAENG_TRY_ASSIGN_ASYNC(pixels, fm.loadAsync(t.path));
        TextureDesc td{ TextureFormat::RGBA8_UNORM, t.width, t.height, 1, 1, 0 };
        TextureHandle tex = gfx->create_texture(&td, pixels.data());
        material.bg.textures.emplace_back(tex);
        
        SamplerDesc sd{
            .filter = (t.sampler.filter == "linear") ? SamplerFilter::Linear : SamplerFilter::Nearest,
            .address_u = AddressMode::Repeat, .address_v = AddressMode::Repeat, .address_w = AddressMode::Repeat,
        };
        material.bg.samplers.emplace_back(gfx->create_sampler(&sd));
        material.bg.textureIndices.push_back(gfx->get_texture_index(tex));
        material.bg.samplerIndices.push_back(gfx->get_sampler_index(material.bg.samplers.back()));
    }

    // Create Constant Buffers
    for (auto& cbEntry : material.mat.constantBuffers) {
        BufferDesc   cbDesc{cbEntry.size, BufferUsage_Uniform};
        BufferHandle cb = gfx->create_buffer(&cbDesc, nullptr);
        material.bg.constantBuffers.emplace_back(cb);
    }

    co_return result<>{};
}

async::Task<result<MaterialSystem::ReflectionData>> MaterialSystem::_loadReflectionAsync(IFileManager& fm, const std::string& path) {
    std::vector<uint8_t> fdata;
    JAENG_TRY_ASSIGN_ASYNC(fdata, fm.loadAsync(path));
    try {
        json j = json::parse(fdata);
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
            else ad.format = VertexAttributeFormat::Float4; 
            std::strncpy(ad.semanticName, sem.c_str(), sizeof(ad.semanticName) - 1);
            rd.attributes.push_back(ad);
            rd.semantics.push_back(sem);
        }
        co_return rd;
    } catch (const std::exception& e) {
        co_return Error::fromMessage((int)error_code::unknown_error, e.what());
    }
}

static MaterialMetadata fromJsonInternal(const nlohmann::json& j) {
    MaterialMetadata m;
    m.name = j.value("name", "Unnamed Material");
    auto shader = j["shader"];
    m.vsPath = shader.value("vertex", "");
    m.psPath = shader.value("pixel", "");
    m.reflectPath = shader.value("reflection", "");
    if (j.contains("textures")) {
        for (auto& tObj : j["textures"]) {
            TextureData mt;
            mt.path = tObj.value("path", "");
            mt.width = tObj.value("width", 1);
            mt.height = tObj.value("height", 1);
            mt.sampler.filter = tObj["sampler"].value("filter", "linear");
            m.textures.push_back(mt);
        }
    }
    if (j.contains("parameters")) {
        auto params = j["parameters"];
        for (auto it = params.begin(); it != params.end(); ++it) {
            if (it.value().is_array()) {
                auto v = it.value();
                m.vectorParams[it.key()] = glm::vec4(v[0], v[1], v[2], v[3]);
            } else {
                m.scalarParams[it.key()] = it.value().get<float>();
            }
        }
    }
    if (j.contains("constantBuffers")) {
        for (auto& cbEntry : j["constantBuffers"]) {
            m.constantBuffers.emplace_back(cbEntry["name"], cbEntry["size"].get<uint32_t>(), cbEntry["binding"].get<uint32_t>());
        }
    }
    auto states = j["pipelineStates"];
    m.blendState.enabled = states["blend"].value("enabled", false);
    m.rasterizer.cullMode = states["rasterizer"].value("cullMode", "back");
    m.depthStencil.depthTest = states["depthStencil"].value("depthTest", true);
    m.depthStencil.depthWrite = states["depthStencil"].value("depthWrite", true);
    return m;
}

async::Task<result<MaterialHandle>> MaterialSystem::_createMaterialMetadataAsync(IFileManager& fm, const std::string& path)
{
    JAENG_ERROR_IF_ASYNC(slotUsage.count() >= MaterialSystem::MAX_MATERIALS, error_code::no_resource, "[Material] No space");
    std::vector<uint8_t> fdata;
    JAENG_TRY_ASSIGN_ASYNC(fdata, fm.loadAsync(path));
    try {
        auto matJson = json::parse(fdata.begin(), fdata.end());
        auto mat = fromJsonInternal(matJson);
        auto h = MaterialHandle(firstAvailable(slotUsage));
        {
            std::lock_guard<std::mutex> lock(storageMutex);
            storage.emplace(h, std::make_shared<Storage>(Storage{ .mat = std::move(mat) }));
            slotUsage.set(h);
        }
        co_return h;        
    } catch (const std::exception& e) {
        co_return jaeng::Error::fromMessage((int)error_code::unknown_error, e.what());
    }
}

result<MaterialHandle> MaterialSystem::createMaterial(const std::string& path)
{
    JAENG_TRY_ASSIGN(MaterialHandle h, _createMaterialMetadata(*fileManager, path));
    std::shared_ptr<Storage> material;
    {
        std::lock_guard<std::mutex> lock(storageMutex);
        material = storage[h];
    }
    JAENG_TRY_ASSIGN(ReflectionData rd, _loadReflection(*fileManager, material->mat.reflectPath));
    std::vector<const char*> semPtrs;
    for (const auto& s : rd.semantics) semPtrs.push_back(s.c_str());
    VertexLayoutDesc vld { .stride = rd.stride, .attributes = rd.attributes.data(), .attribute_count = static_cast<uint32_t>(rd.attributes.size()) };
    JAENG_TRY(_createMaterialResources(*fileManager, *material, &vld, vld.attribute_count, semPtrs.data()));
    return h;
}

result<MaterialHandle> MaterialSystem::createMaterial(const std::string& path, const VertexLayoutDesc* vertexLayout, size_t vertexLayoutCount, const char* requiredSemantics[])
{
    JAENG_TRY_ASSIGN(MaterialHandle h, _createMaterialMetadata(*fileManager, path));
    std::shared_ptr<Storage> material;
    {
        std::lock_guard<std::mutex> lock(storageMutex);
        material = storage[h];
    }
    JAENG_TRY(_createMaterialResources(*fileManager, *material, vertexLayout, vertexLayoutCount, requiredSemantics));
    return h;
}

void MaterialSystem::destroyMaterial(MaterialHandle handle)
{
    std::lock_guard<std::mutex> lock(storageMutex);
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

result<const MaterialBindings*> MaterialSystem::getBindData(MaterialHandle handle) const {
    std::lock_guard<std::mutex> lock(storageMutex);
    auto it = storage.find(handle);
    JAENG_ERROR_IF(it == storage.end(), error_code::no_resource, "Material not found");
    return &it->second->bg;
}

result<const MaterialMetadata*> MaterialSystem::getMetadata(MaterialHandle handle) const {
    std::lock_guard<std::mutex> lock(storageMutex);
    auto it = storage.find(handle);
    JAENG_ERROR_IF(it == storage.end(), error_code::no_resource, "Material not found");
    return &it->second->mat;
}

result<> MaterialSystem::reloadMaterial(MaterialHandle handle) { return result<>{}; }

void MaterialSystem::setVectorParam(MaterialHandle handle, const std::string& name, const glm::vec4& value) {
    std::lock_guard<std::mutex> lock(storageMutex);
    auto it = storage.find(handle);
    if (it != storage.end()) it->second->mat.vectorParams[name] = value;
}

result<MaterialHandle> MaterialSystem::_createMaterialMetadata(IFileManager& fm, const std::string& path) {
    JAENG_ERROR_IF(slotUsage.count() >= MAX_MATERIALS, error_code::no_resource, "No space");
    JAENG_TRY_ASSIGN(auto fdata, fm.load(path));
    auto matJson = json::parse(fdata.begin(), fdata.end());
    auto mat = fromJsonInternal(matJson);
    auto h = MaterialHandle(firstAvailable(slotUsage));
    {
        std::lock_guard<std::mutex> lock(storageMutex);
        storage.emplace(h, std::make_shared<Storage>(Storage{ .mat = std::move(mat) }));
        slotUsage.set(h);
    }
    return h;
}

result<MaterialSystem::ReflectionData> MaterialSystem::_loadReflection(IFileManager& fm, const std::string& path) {
    JAENG_TRY_ASSIGN(auto fdata, fm.load(path));
    json j = json::parse(fdata);
    ReflectionData rd;
    rd.stride = j["stride"].get<uint32_t>();
    for (auto& attr : j["attributes"]) {
        VertexAttributeDesc ad{};
        ad.offset = attr["offset"].get<uint32_t>();
        ad.location = static_cast<uint32_t>(rd.attributes.size());
        std::string sem = attr["semantic"].get<std::string>();

        if (sem == "POSITION") ad.format = VertexAttributeFormat::Float3;
        else if (sem == "COLOR") ad.format = VertexAttributeFormat::Float3;
        else if (sem == "TEXCOORD") ad.format = VertexAttributeFormat::Float2;
        else ad.format = VertexAttributeFormat::Float4;

        std::strncpy(ad.semanticName, sem.c_str(), sizeof(ad.semanticName)-1);
        rd.attributes.push_back(ad);
        rd.semantics.push_back(sem);
    }
    return rd;
}

result<> MaterialSystem::_createMaterialResources(IFileManager& fm, Storage& material, const VertexLayoutDesc* vtxLayout, size_t vtxLayoutCount, const char* requiredSemantics[]) {
    auto gfx = renderer.lock();
    JAENG_ERROR_IF(!gfx, error_code::resource_not_ready, "Renderer missing");
    {
        JAENG_TRY_ASSIGN(auto data, fm.load(material.mat.vsPath));
        ShaderModuleDesc desc { ShaderStage::Vertex, data.data(), (uint32_t)data.size(), 0 };
        material.bg.vertexShader = gfx->create_shader_module(&desc);
    }
    {
        JAENG_TRY_ASSIGN(auto data, fm.load(material.mat.psPath));
        ShaderModuleDesc desc { ShaderStage::Fragment, data.data(), (uint32_t)data.size(), 0 };
        material.bg.pixelShader = gfx->create_shader_module(&desc);
    }
    material.bg.vertexLayout = gfx->create_vertex_layout(vtxLayout);
    for (auto& t : material.mat.textures) {
        JAENG_TRY_ASSIGN(auto pixels, fm.load(t.path));
        TextureDesc td{ TextureFormat::RGBA8_UNORM, t.width, t.height, 1, 1, 0 };
        TextureHandle tex = gfx->create_texture(&td, pixels.data());
        material.bg.textures.push_back(tex);
        material.bg.textureIndices.push_back(gfx->get_texture_index(tex));
        SamplerDesc sd{ .filter = SamplerFilter::Linear };
        material.bg.samplers.push_back(gfx->create_sampler(&sd));
        material.bg.samplerIndices.push_back(gfx->get_sampler_index(material.bg.samplers.back()));
    }
    for (auto& cb : material.mat.constantBuffers) {
        BufferDesc bd{ cb.size, BufferUsage_Uniform };
        material.bg.constantBuffers.push_back(gfx->create_buffer(&bd, nullptr));
    }
    return result<>{};
}

} // namespace jaeng
