#include "default_assets.h"
#include <vector>
#include <cstring>

namespace jaeng {

// Define the backend-specific extensions and files
#if defined(JAENG_WIN32) && !defined(JAENG_USE_VULKAN)
#define SHADER_EXT ".dxil"
#elif defined(JAENG_MACOS) || defined(JAENG_IOS)
#define SHADER_EXT ".msl"
#else
#define SHADER_EXT ".spv"
#endif

static const char* uiMaterialFileData = "{\n"
  "  \"name\": \"DefaultUIMaterial\",\n"
  "  \"shader\": {\n"
  "    \"vertex\": \"shaders/compiled/ui_vs" SHADER_EXT "\",\n"
  "    \"pixel\": \"shaders/compiled/ui_ps" SHADER_EXT "\",\n"
  "    \"reflection\": \"shaders/include/ui.json\"\n"
  "  },\n"
  "  \"textures\": [\n"
  "    {\n"
  "      \"path\": \"/engine/ui/white.raw\",\n"
  "      \"width\": 1,\n"
  "      \"height\": 1,\n"
  "      \"format\": \"rgba8\",\n"
  "      \"sampler\": {\n"
  "        \"filter\": \"nearest\",\n"
  "        \"addressModeU\": \"clamp\",\n"
  "        \"addressModeV\": \"clamp\"\n"
  "      }\n"
  "    }\n"
  "  ],\n"
  "  \"parameters\": {\n"
  "    \"color\": [1.0, 1.0, 1.0, 1.0],\n"
  "    \"roughness\": 0.5,\n"
  "    \"metallic\": 0.0\n"
  "  },\n"
  "  \"constantBuffers\": [\n"
  "    { \"name\": \"CBFrame\", \"size\": 64, \"binding\": 1 },\n"
  "    { \"name\": \"CBObject\", \"size\": 96, \"binding\": 2 }\n"
  "  ],\n"
  "  \"pipelineStates\": {\n"
  "    \"blend\": { \"enabled\": true, \"srcFactor\": \"src_alpha\", \"dstFactor\": \"one_minus_src_alpha\" },\n"
  "    \"rasterizer\": { \"cullMode\": \"none\", \"fillMode\": \"solid\" },\n"
  "    \"depthStencil\": { \"depthTest\": false, \"depthWrite\": false }\n"
  "  }\n"
  "}";

static std::vector<uint8_t> createQuadMeshBinary() {
    struct Header { uint32_t v; uint32_t i; } h { 4, 6 };
    struct Vertex { float p[3]; float c[3]; float u[2]; };
    std::vector<Vertex> v = {
        {{-0.5f, -0.5f, 0.0f}, {1,1,1}, {0,0}},
        {{-0.5f,  0.5f, 0.0f}, {1,1,1}, {0,1}},
        {{ 0.5f,  0.5f, 0.0f}, {1,1,1}, {1,1}},
        {{ 0.5f, -0.5f, 0.0f}, {1,1,1}, {1,0}}
    };
    std::vector<uint32_t> i = { 0, 1, 2, 0, 2, 3 };
    std::vector<uint8_t> data(sizeof(h) + sizeof(Vertex)*4 + sizeof(uint32_t)*6);
    uint8_t* p = data.data();
    std::memcpy(p, &h, sizeof(h)); p += sizeof(h);
    std::memcpy(p, v.data(), sizeof(Vertex)*4); p += sizeof(Vertex)*4;
    std::memcpy(p, i.data(), sizeof(uint32_t)*6);
    return data;
}

void RegisterDefaultAssets(IFileManager& fm) {
    // 1) White Texture
    static uint32_t whitePixel = 0xFFFFFFFF;
    fm.registerMemoryFile("/engine/ui/white.raw", &whitePixel, sizeof(uint32_t));

    // 2) Checker Texture
    static std::vector<uint32_t> checkerPixels;
    if (checkerPixels.empty()) {
        const uint32_t W = 256, H = 256, CS = 32;
        checkerPixels.resize(W * H);
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                bool on = ((x / CS) ^ (y / CS)) & 1;
                uint8_t c = on ? 255 : 30;
                checkerPixels[y * W + x] = (0xFFu << 24) | (c << 16) | (c << 8) | c;
            }
        }
    }
    fm.registerMemoryFile("/engine/ui/checker.raw", checkerPixels.data(), checkerPixels.size() * sizeof(uint32_t));

    // 3) UI Material
    fm.registerMemoryFile("/engine/ui/material.json", uiMaterialFileData, std::strlen(uiMaterialFileData));

    // 4) Quad Mesh
    static std::vector<uint8_t> quadData = createQuadMeshBinary();
    fm.registerMemoryFile("/engine/ui/quad.raw", quadData.data(), quadData.size());
}

} // namespace jaeng
