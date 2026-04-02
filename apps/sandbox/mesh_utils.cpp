#include "mesh_utils.h"
#include <cstring>

std::vector<uint8_t> createQuadMeshBinary() {
    RAWFormatHeader header{4, 6};

    RAWFormatVertex vertices[4] = {
        {{-0.5f,-0.5f,0.0f},{1,0,0},{0,1}},
        {{-0.5f, 0.5f,0.0f},{0,1,0},{0,0}},
        {{ 0.5f, 0.5f,0.0f},{0,0,1},{1,0}},
        {{ 0.5f,-0.5f,0.0f},{1,1,1},{1,1}}
    };

    uint32_t indices[6] = {0,1,2,0,2,3};

    size_t totalSize = sizeof(header) + sizeof(vertices) + sizeof(indices);
    std::vector<uint8_t> buffer(totalSize);

    uint8_t* ptr = buffer.data();
    std::memcpy(ptr, &header, sizeof(header));
    ptr += sizeof(header);
    std::memcpy(ptr, vertices, sizeof(vertices));
    ptr += sizeof(vertices);
    std::memcpy(ptr, indices, sizeof(indices));

    return buffer;
}

std::vector<uint8_t> createCubeMeshBinary() {
    RAWFormatHeader header{24, 36};

    RAWFormatVertex v[24];
    auto V = [&](int i, float x, float y, float z, float r, float g, float b, float u, float vv) {
        v[i].position[0]=x; v[i].position[1]=y; v[i].position[2]=z;
        v[i].color[0]=r; v[i].color[1]=g; v[i].color[2]=b;
        v[i].uv[0]=u; v[i].uv[1]=vv;
    };

    V(0, +0.5f,-0.5f,-0.5f, 1,0,0, 0,1);
    V(1, +0.5f,-0.5f,+0.5f, 1,0,0, 0,0);
    V(2, +0.5f,+0.5f,+0.5f, 1,0,0, 1,0);
    V(3, +0.5f,+0.5f,-0.5f, 1,0,0, 1,1);

    V(4, -0.5f,-0.5f,+0.5f, 0,1,0, 0,0);
    V(5, -0.5f,-0.5f,-0.5f, 0,1,0, 0,1);
    V(6, -0.5f,+0.5f,-0.5f, 0,1,0, 1,1);
    V(7, -0.5f,+0.5f,+0.5f, 0,1,0, 1,0);

    V(8, -0.5f,+0.5f,-0.5f, 0,0,1, 0,1);
    V(9, +0.5f,+0.5f,-0.5f, 0,0,1, 1,1);
    V(10,+0.5f,+0.5f,+0.5f, 0,0,1, 1,0);
    V(11,-0.5f,+0.5f,+0.5f, 0,0,1, 0,0);

    V(12,-0.5f,-0.5f,+0.5f, 1,1,0, 0,0);
    V(13,+0.5f,-0.5f,+0.5f, 1,1,0, 1,0);
    V(14,+0.5f,-0.5f,-0.5f, 1,1,0, 1,1);
    V(15,-0.5f,-0.5f,-0.5f, 1,1,0, 0,1);

    V(16,+0.5f,-0.5f,+0.5f, 1,0,1, 1,1);
    V(17,-0.5f,-0.5f,+0.5f, 1,0,1, 0,1);
    V(18,-0.5f,+0.5f,+0.5f, 1,0,1, 0,0);
    V(19,+0.5f,+0.5f,+0.5f, 1,0,1, 1,0);

    V(20,-0.5f,-0.5f,-0.5f, 0,1,1, 1,1);
    V(21,+0.5f,-0.5f,-0.5f, 0,1,1, 0,1);
    V(22,+0.5f,+0.5f,-0.5f, 0,1,1, 0,0);
    V(23,-0.5f,+0.5f,-0.5f, 0,1,1, 1,0);

    uint32_t idx[36] = {
        0,1,2, 0,2,3, 4,5,6, 4,6,7, 8,9,10, 8,10,11,
        12,13,14, 12,14,15, 16,17,18, 16,18,19, 20,21,22, 20,22,23
    };

    size_t totalSize = sizeof(RAWFormatHeader) + sizeof(v) + sizeof(idx);
    std::vector<uint8_t> buffer(totalSize);
    uint8_t* ptr = buffer.data();
    std::memcpy(ptr, &header, sizeof(RAWFormatHeader));
    ptr += sizeof(RAWFormatHeader);
    std::memcpy(ptr, v, sizeof(v));
    ptr += sizeof(v);
    std::memcpy(ptr, idx, sizeof(idx));
    return buffer;
}
