#include "materialsys.h"

#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>

struct Material {
    std::string name;
    std::string shader;
    std::vector<std::string> textures;
    glm::vec4 color;
    float roughness = 0.0f;
    float metallic = 0.0f;
};

using nlohmann::json;

Material fromJson(const json& matJson) {
    Material m {
        .name = matJson["name"].get<std::string>(),
        .shader = matJson["shader"].get<std::string>(),
    };
    for (auto& t : matJson["textures"]) {
        m.textures.emplace_back(t.get<std::string>());
    }
    if (matJson.contains("parameters")) {
        auto params = matJson["parameters"];
        if (params.contains("color")) {
            int i = 0;
            for (auto& ce : params["color"]) {
                m.color[i++] = ce.get<float>();
            }
        }
        if (params.contains("roughness")) m.roughness = params["roughness"].get<float>();
        if (params.contains("metallic")) m.roughness = params["metallic"].get<float>();
    }

    return m;
}

jaeng::result<MaterialHandle> MaterialSystem::createMaterial(const std::string& path)
{
    JAENG_TRY_ASSIGN(auto fdata, fileManager->load(path));

    auto matJson = json::parse(fdata.begin(), fdata.end());
    auto mat = fromJson(matJson);

    return 0;
}

jaeng::result<MaterialHandle> MaterialSystem::createMaterial(
    const std::string& path,
    const VertexLayoutDesc* vertexLayout,
    size_t vertexLayoutCount,
    const BindGroupLayoutDesc* bindGroups,
    size_t bindGroupCount)
{
    JAENG_TRY_ASSIGN(auto fdata, fileManager->load(path));

    auto matJson = json::parse(fdata.begin(), fdata.end());
    auto mat = fromJson(matJson);
    
    return 0;
}

// Destroy material
void MaterialSystem::destroyMaterial(MaterialHandle handle)
{
}

// Query GPU bindings for rendering
jaeng::result<const BindGroup*> MaterialSystem::getBindGroup(MaterialHandle handle) const
{
    return jaeng::Error::fromMessage(static_cast<int>(jaeng::error_code::invalid_operation), "Not IMplemented");
}

jaeng::result<const MaterialMetadata*> MaterialSystem::getMetadata(MaterialHandle handle) const
{
    return jaeng::Error::fromMessage(static_cast<int>(jaeng::error_code::invalid_operation), "Not IMplemented");
}

jaeng::result<> MaterialSystem::reloadMaterial(MaterialHandle handle)
{
    return jaeng::Error::fromMessage(static_cast<int>(jaeng::error_code::invalid_operation), "Not IMplemented");
}
