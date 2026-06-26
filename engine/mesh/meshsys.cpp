#include "meshsys.h"
#include "common/logging.h"
#include <filesystem>
#include <sstream>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

namespace jaeng {

namespace {

// Helper to determine extension
std::string getExtension(const std::string& path) {
    auto pos = path.find_last_of('.');
    if (pos == std::string::npos) return "";
    std::string ext = path.substr(pos);
    for (char& c : ext) c = std::tolower(c);
    return ext;
}

result<Mesh> parseRAW(const std::vector<uint8_t>& rawData, std::shared_ptr<RendererAPI> gfx) {
    auto header = reinterpret_cast<const RAWFormatHeader*>(rawData.data());
    auto* vertices = reinterpret_cast<const RAWFormatVertex*>(rawData.data() + sizeof(RAWFormatHeader));
    auto* indices = reinterpret_cast<const uint32_t*>(rawData.data() + sizeof(RAWFormatHeader) + (sizeof(RAWFormatVertex)*header->vertexCount));

    BufferDesc vbd{
        .size_bytes = sizeof(RAWFormatVertex) * header->vertexCount,
        .usage      = BufferUsage_Vertex
    };
    BufferHandle vb = gfx->create_buffer(&vbd, vertices);

    BufferDesc ibd{
        .size_bytes = sizeof(uint32_t) * header->indexCount,
        .usage      = BufferUsage_Index
    };
    BufferHandle ib = gfx->create_buffer(&ibd, indices);

    return Mesh {
        .vertexBuffer = vb, .indexBuffer = ib,
        .semantics = {"POSITION", "COLOR", "TEXCOORD"},
        .topology = PrimitiveTopology::TriangleList,
        .indexCount = header->indexCount
    };
}

result<Mesh> parseOBJ(const std::string& path, const std::vector<uint8_t>& rawData, std::shared_ptr<RendererAPI> gfx, const MeshImportDesc& desc) {
    std::string err;
    tinyobj::ObjReaderConfig reader_config;
    reader_config.triangulate = true;
    tinyobj::ObjReader reader;
    
    std::string objText(rawData.begin(), rawData.end());
    if (!reader.ParseFromString(objText, "", reader_config)) {
        return Error::fromMessage((int)error_code::invalid_args, "OBJ parse error"); // Simplified error handling
    }
    if (!reader.Warning().empty()) {
        JAENG_LOG_WARN("OBJ Warning: {}", reader.Warning());
    }

    auto& attrib = reader.GetAttrib();
    auto& shapes = reader.GetShapes();

    std::vector<RAWFormatVertex> vertices;
    std::vector<uint32_t> indices;

    for (size_t s = 0; s < shapes.size(); s++) {
        size_t index_offset = 0;
        for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {
            size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);
            for (size_t v = 0; v < fv; v++) {
                tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
                RAWFormatVertex vertex{};
                vertex.position[0] = attrib.vertices[3*size_t(idx.vertex_index)+0] * desc.uniformScale;
                vertex.position[1] = attrib.vertices[3*size_t(idx.vertex_index)+1] * desc.uniformScale;
                vertex.position[2] = attrib.vertices[3*size_t(idx.vertex_index)+2] * desc.uniformScale;

                if (idx.normal_index >= 0) {
                    // For now, mapping normals to color for testing/compatibility
                    vertex.color[0] = attrib.normals[3*size_t(idx.normal_index)+0];
                    vertex.color[1] = attrib.normals[3*size_t(idx.normal_index)+1];
                    vertex.color[2] = attrib.normals[3*size_t(idx.normal_index)+2];
                } else {
                    vertex.color[0] = 1.0f; vertex.color[1] = 1.0f; vertex.color[2] = 1.0f;
                }

                if (idx.texcoord_index >= 0) {
                    vertex.uv[0] = attrib.texcoords[2*size_t(idx.texcoord_index)+0];
                    vertex.uv[1] = 1.0f - attrib.texcoords[2*size_t(idx.texcoord_index)+1];
                }

                indices.push_back(static_cast<uint32_t>(vertices.size()));
                vertices.push_back(vertex);
            }
            index_offset += fv;
        }
    }

    BufferDesc vbd{ .size_bytes = static_cast<uint32_t>(sizeof(RAWFormatVertex) * vertices.size()), .usage = BufferUsage_Vertex };
    BufferHandle vb = gfx->create_buffer(&vbd, vertices.data());

    BufferDesc ibd{ .size_bytes = static_cast<uint32_t>(sizeof(uint32_t) * indices.size()), .usage = BufferUsage_Index };
    BufferHandle ib = gfx->create_buffer(&ibd, indices.data());

    return Mesh {
        .vertexBuffer = vb, .indexBuffer = ib,
        .semantics = {"POSITION", "COLOR", "TEXCOORD"},
        .topology = PrimitiveTopology::TriangleList,
        .indexCount = indices.size()
    };
}

result<Mesh> parseGLTF(const std::string& path, const std::vector<uint8_t>& rawData, std::shared_ptr<RendererAPI> gfx, const MeshImportDesc& desc) {
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result res = cgltf_parse(&options, rawData.data(), rawData.size(), &data);
    if (res != cgltf_result_success) return Error::fromMessage((int)error_code::invalid_args, "glTF parse error");

    res = cgltf_load_buffers(&options, data, path.c_str());
    if (res != cgltf_result_success) { cgltf_free(data); return Error::fromMessage((int)error_code::invalid_args, "glTF buffer error"); }

    // Simplified extraction: just grab the first mesh and its first primitive
    if (data->meshes_count == 0) { cgltf_free(data); return Error::fromMessage((int)error_code::invalid_args, "No meshes"); }

    cgltf_mesh* mesh = &data->meshes[0];
    if (mesh->primitives_count == 0) { cgltf_free(data); return Error::fromMessage((int)error_code::invalid_args, "No primitives"); }

    cgltf_primitive* prim = &mesh->primitives[0];
    
    // Fallback logic for extraction...
    // To keep this clean for now, we'll build a standard VBO/IBO like OBJ.
    // (A complete cgltf implementation would map buffer views directly when possible)
    
    std::vector<RAWFormatVertex> vertices;
    std::vector<uint32_t> indices;
    
    size_t vertexCount = 0;
    for (size_t i = 0; i < prim->attributes_count; ++i) {
        if (prim->attributes[i].type == cgltf_attribute_type_position) {
            vertexCount = prim->attributes[i].data->count;
            break;
        }
    }
    vertices.resize(vertexCount);
    
    for (size_t i = 0; i < prim->attributes_count; ++i) {
        cgltf_attribute& attr = prim->attributes[i];
        if (attr.type == cgltf_attribute_type_position) {
            for (size_t v = 0; v < vertexCount; ++v) {
                cgltf_accessor_read_float(attr.data, v, vertices[v].position, 3);
                vertices[v].position[0] *= desc.uniformScale;
                vertices[v].position[1] *= desc.uniformScale;
                vertices[v].position[2] *= desc.uniformScale;
            }
        } else if (attr.type == cgltf_attribute_type_normal) {
            for (size_t v = 0; v < vertexCount; ++v) {
                cgltf_accessor_read_float(attr.data, v, vertices[v].color, 3);
            }
        } else if (attr.type == cgltf_attribute_type_texcoord) {
            for (size_t v = 0; v < vertexCount; ++v) {
                cgltf_accessor_read_float(attr.data, v, vertices[v].uv, 2);
            }
        }
    }
    
    if (prim->indices) {
        indices.resize(prim->indices->count);
        for (size_t i = 0; i < prim->indices->count; ++i) {
            indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(prim->indices, i));
        }
    } else {
        indices.resize(vertexCount);
        for(size_t i=0; i<vertexCount; ++i) indices[i] = i;
    }
    
    cgltf_free(data);

    BufferDesc vbd{ .size_bytes = static_cast<uint32_t>(sizeof(RAWFormatVertex) * vertices.size()), .usage = BufferUsage_Vertex };
    BufferHandle vb = gfx->create_buffer(&vbd, vertices.data());

    BufferDesc ibd{ .size_bytes = static_cast<uint32_t>(sizeof(uint32_t) * indices.size()), .usage = BufferUsage_Index };
    BufferHandle ib = gfx->create_buffer(&ibd, indices.data());

    return Mesh {
        .vertexBuffer = vb, .indexBuffer = ib,
        .semantics = {"POSITION", "COLOR", "TEXCOORD"},
        .topology = PrimitiveTopology::TriangleList,
        .indexCount = indices.size()
    };
}

} // namespace

result<MeshHandle> MeshSystem::loadMesh(const std::string& path, const MeshImportDesc& desc)
{
    auto fm = fileManager_;
    auto gfx = renderer_.lock();
    JAENG_ERROR_IF(!gfx, error_code::resource_not_ready, "[Mesh] Renderer is not available.");

    MeshHandle h;
    {
        std::lock_guard<std::mutex> lock(storageMutex);
        JAENG_TRY_ASSIGN(h, allocateSlot());
    }

    JAENG_TRY_ASSIGN(std::vector<uint8_t> rawData, fm->load(path));    
    
    std::string ext = getExtension(path);
    result<Mesh> meshRes = Error::fromMessage((int)error_code::unknown_error, "Init");
    if (ext == ".obj") {
        meshRes = parseOBJ(path, rawData, gfx, desc);
    } else if (ext == ".gltf" || ext == ".glb") {
        meshRes = parseGLTF(path, rawData, gfx, desc);
    } else {
        meshRes = parseRAW(rawData, gfx);
    }
    
    if (meshRes.hasError()) return std::move(meshRes).logError().error();

    {
        std::lock_guard<std::mutex> lock(storageMutex);
        meshes.emplace(h, std::move(std::move(meshRes).logError().value()));
    }

    return h;
}

async::Task<result<MeshHandle>> MeshSystem::loadMeshAsync(const std::string& path, const MeshImportDesc& desc)
{
    auto fm = fileManager_;
    auto gfx = renderer_.lock();
    JAENG_ERROR_IF_ASYNC(!gfx, error_code::resource_not_ready, "[Mesh] Renderer is not available.");

    MeshHandle h;
    {
        std::lock_guard<std::mutex> lock(storageMutex);
        auto res = allocateSlot();
        if (res.hasError()) co_return res;
        h = std::move(res).logError().value();
    }

    std::vector<uint8_t> rawData;
    {
        auto res = co_await fm->loadAsync(path);
        if (res.hasError()) co_return res;
        rawData = std::move(res).logError().value();
    }

    std::string ext = getExtension(path);
    result<Mesh> meshRes = Error::fromMessage((int)error_code::unknown_error, "Init");
    if (ext == ".obj") {
        meshRes = parseOBJ(path, rawData, gfx, desc);
    } else if (ext == ".gltf" || ext == ".glb") {
        meshRes = parseGLTF(path, rawData, gfx, desc);
    } else {
        meshRes = parseRAW(rawData, gfx);
    }
    
    if (meshRes.hasError()) co_return std::move(meshRes).logError().error();

    {
        std::lock_guard<std::mutex> lock(storageMutex);
        meshes.emplace(h, std::move(std::move(meshRes).logError().value()));
    }

    co_return h;
}

result<void> MeshSystem::removeMesh(MeshHandle handle)
{
    std::lock_guard<std::mutex> lock(storageMutex);
    auto it = meshes.find(handle);
    if (it == meshes.end()) return Error::fromMessage((int)error_code::resource_not_ready, "Not ready");
    
    if (auto gfx = renderer_.lock()) {
        gfx->destroy_buffer(it->second.vertexBuffer);
        gfx->destroy_buffer(it->second.indexBuffer);
    }
    
    meshes.erase(it);
    freeSlot(handle);
    return {};
}

result<const Mesh*> MeshSystem::getMesh(MeshHandle handle) const
{
    std::lock_guard<std::mutex> lock(storageMutex);
    auto it = meshes.find(handle);
    if (it == meshes.end()) return Error::fromMessage((int)error_code::resource_not_ready, "Not ready");
    return &it->second;
}

result<MeshHandle> MeshSystem::allocateSlot() {
    for (size_t i = 0; i < MAX_MESH_ENTRIES; ++i) {
        if (!slotUsage.test(i)) {
            slotUsage.set(i);
            return MeshHandle(i);
        }
    }
    return Error::fromMessage((int)error_code::no_resource, "Max meshes reached");
}

void MeshSystem::freeSlot(MeshHandle handle) {
    if (handle < MAX_MESH_ENTRIES) {
        slotUsage.reset(handle);
    }
}

} // namespace jaeng
