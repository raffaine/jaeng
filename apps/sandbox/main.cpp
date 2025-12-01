#include <windows.h>
#include <tchar.h>
#include <stdint.h>
#include <array>
#include <vector>
#include <wrl.h>
#include <d3dcompiler.h>
#include "render/frontend/renderer.h"
#include "render/graph/render_graph.h"
#include "storage/win/filestorage.h"
#include "material/materialsys.h"
#include "mesh/meshsys.h"

#include "basic_reflect.h"

#define USE_PIX
#include "pix3.h"  // provided by winpixevent package

static const char* materialFileData = R"(
{
  "name": "CheckerboardMaterial",
  "shader": {
    "vertex": "C:/dev/repos/pluggable_renderer/shaders/compiled/basic_vs.dxil",
    "pixel": "C:/dev/repos/pluggable_renderer/shaders/compiled/basic_ps.dxil",
    "reflection": "C:/dev/repos/pluggable_renderer/shaders/include/basic_reflect.json"
  },
  "textures": [
    {
      "path": "/mem/checker.raw",
      "width": 256,
      "height": 256,
      "sampler": {
        "filter": "linear",
        "addressModeU": "wrap",
        "addressModeV": "wrap"
      }
    }
  ],
  "parameters": {
    "color": [1.0, 1.0, 1.0, 1.0],
    "roughness": 0.5,
    "metallic": 0.0
  },
  "constantBuffers": [
    {
      "name": "CBTransform",
      "size": 64,
      "binding": 0
    }
  ],
  "pipelineStates": {
    "blend": {
      "enabled": false,
      "srcFactor": "one",
      "dstFactor": "zero"
    },
    "rasterizer": {
      "cullMode": "back",
      "fillMode": "solid"
    },
    "depthStencil": {
      "depthTest": true,
      "depthWrite": true
    }
  }
}
)";

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

using Microsoft::WRL::ComPtr;

static const wchar_t* kWndClass = L"SandboxWindowClass";

Renderer renderer {};
SwapchainHandle swap = 0;

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {        
        case WM_SIZE: {
            UINT w = LOWORD(lParam), h = HIWORD(lParam);
            if (swap > 0 && w > 0 && h > 0) {
                renderer.gfx->resize_swapchain(swap, { w, h });
            }
            return 0;
        }

        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

struct CBTransform {
    float m[16]; // column-major 4x4
};

// Simple function to build a translation matrix
CBTransform Translate(float x, float y, float z = 0.0f) {
    CBTransform cb{};
    cb.m[0]=1; cb.m[5]=1; cb.m[10]=1; cb.m[15]=1;
    cb.m[12]=x; cb.m[13]=y;  cb.m[14]=z;  // translation components
    return cb;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    HMODULE hGpuCap = PIXLoadLatestWinPixGpuCapturerLibrary();
    if (!hGpuCap) {
        MessageBox(nullptr, L"Failed to load WinPixGpuCapturer.dll", L"Error", MB_ICONERROR);
        return -1;
    }

    // Register window class
    WNDCLASSEX wc{ sizeof(WNDCLASSEX) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = kWndClass;
    RegisterClassEx(&wc);

    // Create window
    RECT r{0,0,1280,720};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowEx(0, kWndClass, L"Pluggable Renderer - Sandbox",
    WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
    r.right - r.left, r.bottom - r.top,
    nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return -1;

    // Renderer
    if (!renderer.initialize(GfxBackend::D3D12, hwnd, 3)) {
        MessageBox(hwnd, L"Failed to initialize renderer.", L"Error", MB_ICONERROR);
        return -2;
    }

    // SwapChain and Default Depth Buffer
    DepthStencilDesc depthDesc{.depth_enable = true, .depth_format = TextureFormat::D32F};
    SwapchainDesc    swapDesc{{1280u, 720u}, TextureFormat::BGRA8_UNORM, depthDesc, PresentMode::Fifo};
    swap = renderer->create_swapchain(&swapDesc);

    // --- File Manager and Data Setup ---
    std::shared_ptr<IFileManager> fileMan = std::make_shared<FileManager>();

    // Create a checkerboard texture (RGBA8) + sampler + bind group (set 0)
    const uint32_t W = 256, H = 256, CS = 32;
    std::vector<uint32_t> pixels(W * H);
    for (uint32_t y=0; y<H; ++y) {
        for (uint32_t x=0; x<W; ++x) {
            bool on = ((x/CS) ^ (y/CS)) & 1;
            uint8_t c = on ? 255 : 30;
            pixels[y*W + x] = (0xFFu<<24) | (c<<16) | (c<<8) | c;
        }
    }
    // Stores it on File Manager
    fileMan->registerMemoryFile("/mem/checker.raw", pixels.data(), pixels.size()*sizeof(uint32_t));

    // Stores the material description in the File Manager
    fileMan->registerMemoryFile("/mem/material-test.json", materialFileData, strlen(materialFileData));

    // Stores the mesh in the File Manager
    auto meshRawData = createQuadMeshBinary();
    fileMan->registerMemoryFile("/mem/mesh-test.raw", meshRawData.data(), meshRawData.size());

    // --- Material System setup ---
    MaterialSystem matSys(fileMan, renderer.gfx);

    // Creates Test Checkerboard Textured Material with precompiled shaders and reflected header layouts
    MaterialHandle matHandle = matSys.createMaterial("/mem/material-test.json", &ShaderReflection::vertexLayout, 1, ShaderReflection::inputSemantics, &ShaderReflection::bindGroupLayout, 1).orValue({});

    // Retrieve Material Created and Convenience Variables for Constant Buffer and Bind Group
    const MaterialBindings* matBg = matSys.getBindData(matHandle).orValue(nullptr);
    const BufferHandle      cb    = matBg->constantBuffers[0];
    const BindGroupHandle   bg    = matBg->bindGroup;

    // ---- Mesh System Setup ---
    MeshSystem meshSys(fileMan, renderer.gfx);

    // Loads the test mesh into Mesh Sys
    auto meshHandle = meshSys.loadMesh("/mem/mesh-test.raw").orValue({});

    // Retrieves the Created Mesh and Convenience Variables
    const Mesh* testMesh = meshSys.getMesh(meshHandle).orValue(nullptr);
    const BufferHandle vb = testMesh->vertexBuffer;
    const BufferHandle ib = testMesh->indexBuffer;

    // --- Pipeline Setup ---

    // Creates the Required Pipelines (Involves Geometry aside from Material)
    GraphicsPipelineDesc pdesc{matBg->vertexShader, matBg->pixelShader, PrimitiveTopology::TriangleList, matBg->vertexLayout, TextureFormat::BGRA8_UNORM};
    PipelineHandle       pso = renderer->create_graphics_pipeline(&pdesc);

    // Constant Buffer for VS/PS
    const CBTransform cbData[4] = {Translate(-0.25f, -0.25f, 0.5), Translate(0.25f, -0.25f, 0.5),
                                   Translate(-0.25f, 0.25f, 0.5), Translate(0.25f, 0.25f, 0.5)};

    // ---- Main Loop ----
    bool running = true;
    MSG msg{};
    while (running) {
        // -- Process Windows Events --
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        // Build a small render graph: Clear -> Forward
        RenderGraph graph;

        // 1) Clear pass
        graph.add_pass("Clear", { {
            .tex = renderer->get_current_backbuffer(swap),
            .clear_rgba = { 0.07f, 0.08f, 0.12f, 1.0f }
        } }, { .tex = 1, .clear_depth = 1.0f }, nullptr);
        // 2) Forward pass
        graph.add_pass("Forward", 
            { { .tex = renderer->get_current_backbuffer(swap) } }, { .tex = 1 /*enable depth*/ },
            [&](const RGPassContext& ctx) {
                ctx.gfx->cmd_set_pipeline(ctx.cmd, pso);
                ctx.gfx->cmd_set_vertex_buffer(ctx.cmd, 0, vb, 0);
                ctx.gfx->cmd_set_index_buffer(ctx.cmd, ib, true, 0);

                for (int i=0; i<4; i++) {
                    ctx.gfx->update_buffer(cb, 0, &cbData[i], sizeof(CBTransform));
                    ctx.gfx->cmd_set_bind_group(ctx.cmd, 0, bg);
                    ctx.gfx->cmd_draw_indexed(ctx.cmd, 6, 1, 0, 0, 0);
                }
            }
        );

        // Validate
        graph.compile();

        // Execute Render Passes
        graph.execute(*renderer.gfx, swap, 0, nullptr);
    }

    // Releases renderer resources
    renderer.shutdown();

    return 0;
}
