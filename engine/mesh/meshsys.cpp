#include "meshsys.h"

jaeng::result<MeshHandle> MeshSystem::loadMesh(const std::string& path)
{
    auto fm = fileManager_.lock();
    JAENG_ERROR_IF(!fm, jaeng::error_code::resource_not_ready, "[Mesh] File Manager is not available");
    auto gfx = renderer_.lock();
    JAENG_ERROR_IF(!gfx, jaeng::error_code::resource_not_ready, "[Mesh] Renderer is not available.");

    // Gets a new handle early or fail fast
    JAENG_TRY_ASSIGN(MeshHandle h, allocateSlot());

    // Assumes RAWFormat for now
    JAENG_TRY_ASSIGN(std::vector<uint8_t> rawData, fm->load(path));    
    auto header = reinterpret_cast<const RAWFormatHeader*>(rawData.data());
    auto vertices = reinterpret_cast<const RAWFormatVertex*>(rawData.data() + sizeof(RAWFormatHeader));
    auto indices = reinterpret_cast<const uint32_t*>(rawData.data() + sizeof(RAWFormatHeader) + (sizeof(RAWFormatVertex)*header->vertex_count));

    // Creates Resources on Renderer
    BufferDesc vbd{
        .size_bytes = sizeof(RAWFormatVertex) * header->vertex_count,
        .usage      = BufferUsage_Vertex
    };
    BufferHandle vb = gfx->create_buffer(&vbd, vertices);

    BufferDesc ibd{
        .size_bytes = sizeof(uint32_t) * header->index_count,
        .usage      = BufferUsage_Index
    };
    BufferHandle ib = gfx->create_buffer(&ibd, indices);

    Mesh m {
        .vertexBuffer = vb, .indexBuffer = ib,
        .semantics = {"POSITION", "COLOR", "TEXCOORD"},
        .topology = PrimitiveTopology::TriangleList,
    };
    meshes.emplace(h, std::move(m));

    return h;
}

jaeng::result<void> MeshSystem::removeMesh(MeshHandle handle)
{
    auto meshIt = meshes.find(handle);
    JAENG_ERROR_IF(meshIt == meshes.end(), jaeng::error_code::no_resource, "[Mesh] Mesh is not available.");
    auto gfx = renderer_.lock();
    JAENG_ERROR_IF(!gfx, jaeng::error_code::resource_not_ready, "[Mesh] Renderer is not available.");

    // Remove resources on renderer
    if (meshIt->second.vertexBuffer) gfx->destroy_buffer(meshIt->second.vertexBuffer);
    if (meshIt->second.indexBuffer)  gfx->destroy_buffer(meshIt->second.indexBuffer);

    meshes.erase(meshIt);
    freeSlot(handle);
    
    return {};
}

jaeng::result<const Mesh*> MeshSystem::getMesh(MeshHandle handle) const
{
    auto meshIt = meshes.find(handle);
    JAENG_ERROR_IF(meshIt == meshes.end(), jaeng::error_code::no_resource, "[Mesh] Mesh is not available.");

    return &(meshIt->second);
}

jaeng::result<MeshHandle> MeshSystem::allocateSlot()
{
    for (size_t i = 0; i < MeshSystem::MAX_MESH_ENTRIES; ++i) {
        if (!slotUsage.test(i)) {
            slotUsage.set(i);
            return i;
        }
    }
    JAENG_ERROR(jaeng::error_code::no_resource, "[Mesh] Out of Storage");
}

void MeshSystem::freeSlot(MeshHandle handle)
{
    if (handle < MeshSystem::MAX_MESH_ENTRIES) {
        slotUsage.reset(size_t(handle));
    }
}
