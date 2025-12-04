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
#include "entity/entity.h"
#include "scene/scene.h"
#include "scene/grid_partition.h"

#include "basic_reflect.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

static HWND CreateMainWindow(HINSTANCE hInstance) {
    // Register window class
    WNDCLASSEX wc{sizeof(WNDCLASSEX)};
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = kWndClass;
    RegisterClassEx(&wc);

    // Create window
    RECT r{0, 0, 1280, 720};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd =
        CreateWindowEx(0, kWndClass, L"Pluggable Renderer - Sandbox", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                       CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, hInstance, nullptr);
    return hwnd;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    HMODULE hGpuCap = PIXLoadLatestWinPixGpuCapturerLibrary();
    if (!hGpuCap) {
        MessageBox(nullptr, L"Failed to load WinPixGpuCapturer.dll", L"Error", MB_ICONERROR);
        return -1;
    }

    HWND hwnd = CreateMainWindow(hInstance);
    if (!hwnd) return -1;

    /// ---  Renderer ---
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
    fileMan->initialize().orElse([](auto){
        MessageBox(NULL, L"Failed to initialize FileManager. Continuing but on limited capacity", L"Error", MB_ICONERROR);
    }); // Ensures monitoring threads are setup and running

    // Create a checkerboard texture (RGBA8) and store it as Custom type on File Manager
    {
        const uint32_t        W = 256, H = 256, CS = 32;
        std::vector<uint32_t> pixels(W * H);
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                bool    on        = ((x / CS) ^ (y / CS)) & 1;
                uint8_t c         = on ? 255 : 30;
                pixels[y * W + x] = (0xFFu << 24) | (c << 16) | (c << 8) | c;
            }
        }
        // Stores it on File Manager
        fileMan->registerMemoryFile("/mem/checker.raw", pixels.data(), pixels.size() * sizeof(uint32_t));
    }

    // Stores the test material description in the File Manager
    fileMan->registerMemoryFile("/mem/material-test.json", materialFileData, strlen(materialFileData));

    // Stores the test mesh in the File Manager
    auto meshRawData = createQuadMeshBinary();
    fileMan->registerMemoryFile("/mem/mesh-test.raw", meshRawData.data(), meshRawData.size());

    /// --- Brings up Entity Manager, Material and Mesh Systems ---
    std::shared_ptr<EntityManager> entityMan = std::make_shared<EntityManager>();
    std::shared_ptr<MaterialSystem> matSys  = std::make_shared<MaterialSystem>(fileMan, renderer.gfx);
    std::shared_ptr<MeshSystem>     meshSys = std::make_shared<MeshSystem>(fileMan, renderer.gfx);

    /// --- Scene Manager ---
    SceneManager sceneMan(meshSys, matSys, renderer.gfx);

    // Creates a Test Scene with a simple Grid Partitioner
    if (!sceneMan.createScene("Test", std::make_unique<GridPartitioner>(entityMan)).logError()) {
        MessageBox(NULL, L"Failed to create Test Scene. Aborting.", L"Error", MB_ICONERROR);
        return -1;
    }

    /// --- Test Entities Setup ---

    // Creates 4 entities
    EntityID testEntities[4] = { entityMan->createEntity(), entityMan->createEntity(), entityMan->createEntity(), entityMan->createEntity() };

    // Loads the test mesh into Mesh Sys and associate with all entities
    if (auto meshHandle = meshSys->loadMesh("/mem/mesh-test.raw").logError()) {
        for (int i = 0; i < 4; i++) entityMan->addComponent<MeshHandle>(testEntities[i]) = meshHandle.value();
    }

    // Creates Test Textured Material with precompiled shaders and reflected header layouts and associate with all
    std::unique_ptr<IFileManager::SubscriptionT> materialSub;
    if (auto matHandle = matSys->createMaterial("/mem/material-test.json", &ShaderReflection::vertexLayout, 1, ShaderReflection::inputSemantics, &ShaderReflection::bindGroupLayout, 1).logError()) {
        for (int i = 0; i < 4; i++) entityMan->addComponent<MaterialHandle>(testEntities[i]) = matHandle.value();
        // Enable Hot Reloading for the Material
        materialSub = fileMan->track("/mem/material-test.json", [matSys, h = matHandle.value()](const FileChangedEvent& e) {
            if (e.change == FileChangedEvent::ChangeType::Modified) matSys->reloadMaterial(h).orElse([](auto){}); //ugly ... but logError here still produces nodiscard warning
        });
    }

    // Positions the 4 entities
    entityMan->addComponent<Transform>(testEntities[0]) = Transform{.position = {-0.25f, -0.25f, 0.5}};
    entityMan->addComponent<Transform>(testEntities[1]) = Transform{.position = { 0.25f, -0.25f, 0.5}};
    entityMan->addComponent<Transform>(testEntities[2]) = Transform{.position = {-0.25f,  0.25f, 0.5}};
    entityMan->addComponent<Transform>(testEntities[3]) = Transform{.position = { 0.25f,  0.25f, 0.5}};

    // Now that entities are positioned, builts the partitions for the scene (no op on this version)
    sceneMan.getScene("Test")->getPartitioner()->build();

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

        // Gets the Test Scene Created
        Scene* scene = sceneMan.getScene("Test");
        if (!scene) continue;

        // Build a Draw List based on given AABB (an empty one for now)
        scene->buildDrawList({});

        // Creates the Required Passes on the Graph
        scene->renderScene(graph, swap);

        // Validate (No op for now)
        graph.compile();

        // Execute Render Passes
        graph.execute(*renderer.gfx, swap, 0, nullptr);
    }

    // Releases renderer resources
    renderer.shutdown();

    return 0;
}
