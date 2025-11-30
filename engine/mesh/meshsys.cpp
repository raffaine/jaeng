#include "meshsys.h"

jaeng::result<MeshHandle> MeshSystem::loadMesh(const std::string& path)
{
    JAENG_ERROR(jaeng::error_code::invalid_operation, "[Mesh] Not Implemented.");
}

jaeng::result<void> MeshSystem::removeMesh(MeshHandle handle)
{
    auto meshIt = meshes.find(handle);
    JAENG_ERROR_IF(meshIt == meshes.end(), jaeng::error_code::no_resource, "[Mesh] Mesh is not available.");
    meshes.erase(meshIt);
    
    return {};
}

jaeng::result<const Mesh*> MeshSystem::getMesh(MeshHandle handle) const
{
    auto meshIt = meshes.find(handle);
    JAENG_ERROR_IF(meshIt == meshes.end(), jaeng::error_code::no_resource, "[Mesh] Mesh is not available.");

    return &(meshIt->second);
}
