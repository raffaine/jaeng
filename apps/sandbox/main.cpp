#include <windows.h>
#include <tchar.h>
#include <stdint.h>
#include <wrl.h>
#include <d3dcompiler.h>
#include "render/frontend/renderer.h"

using Microsoft::WRL::ComPtr;

static const wchar_t* kWndClass = L"SandboxWindowClass";

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

static const char* kVS = R"(
struct VSIn { float3 pos: POSITION; float3 col: COLOR; };
struct VSOut { float4 pos: SV_Position; float3 col: COLOR; };
VSOut main(VSIn v) { VSOut o; o.pos = float4(v.pos, 1.0); o.col = v.col; return o; }
)";
static const char* kPS = R"(
struct PSIn { float4 pos: SV_Position; float3 col: COLOR; };
float4 main(PSIn i) : SV_Target { return float4(i.col, 1.0); }
)";

static bool CompileHlsl(const char* src, const char* entry, const char* target, ID3DBlob** outBlob) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> sh, err;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, target, flags, 0, &sh, &err);
    if (FAILED(hr)) { if (err) OutputDebugStringA((const char*)err->GetBufferPointer()); return false; }
    *outBlob = sh.Detach();
    return true;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
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

    Renderer renderer;
    if (!renderer.initialize(GfxBackend::D3D12, hwnd, 3)) {
        MessageBox(hwnd, L"Failed to initialize renderer.", L"Error", MB_ICONERROR);
        return -2;
    }

    SwapchainDesc swapDesc { {1280u, 720u}, TextureFormat::BGRA8_UNORM, PresentMode::Fifo };
    SwapchainHandle swap = renderer.gfx.create_swapchain(&swapDesc);

    // Triangle vertex buffer (pos.xyz, col.xyz)
    struct Vtx { float px,py,pz, cx,cy,cz; };
    Vtx tri[3] = {
        {  0.0f,  0.5f, 0.0f, 1,0,0 },
        {  0.5f, -0.5f, 0.0f, 0,1,0 },
        { -0.5f, -0.5f, 0.0f, 0,0,1 },
    };
    // Step 3: create DEFAULT-heap VB and upload via update_buffer (staged in per-frame ring)
    BufferDesc vbDesc{ sizeof(tri), BufferUsage_Vertex };
    BufferHandle vb = renderer.gfx.create_buffer(&vbDesc, nullptr);

    // Compile HLSL and create shader modules
    ComPtr<ID3DBlob> vsBlob, psBlob;
    CompileHlsl(kVS, "main", "vs_5_0", &vsBlob);
    CompileHlsl(kPS, "main", "ps_5_0", &psBlob);
    ShaderModuleDesc vertexDesc {
        ShaderStage::Vertex, vsBlob->GetBufferPointer(), (uint32_t)vsBlob->GetBufferSize(), 0
    };
    ShaderModuleDesc fragmentDesc {
        ShaderStage::Fragment, psBlob->GetBufferPointer(), (uint32_t)psBlob->GetBufferSize(), 0
    };
    ShaderModuleHandle vsh = renderer.gfx.create_shader_module(&vertexDesc);
    ShaderModuleHandle psh = renderer.gfx.create_shader_module(&fragmentDesc);

    // Pipeline (POSITION@0 offset 0, COLOR@1 offset 12, stride 24)
    VertexAttributeDesc attrs[2] = {
        { 0, 0, 0 },
        { 1, 0, 12 },
    };
    VertexLayoutDesc vtxLayout{ 24, attrs, 2 };
    GraphicsPipelineDesc pdesc{
    vsh, psh, PrimitiveTopology::TriangleList, vtxLayout, TextureFormat::BGRA8_UNORM
    };
    PipelineHandle pso = renderer.gfx.create_graphics_pipeline(&pdesc);


    bool running = true;
    MSG msg{};
    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        // Frame lifecycle
        renderer.gfx.begin_frame();
        // Stage vertex data into DEFAULT heap (ring upload -> CopyBufferRegion)
        renderer.gfx.update_buffer(vb, 0, tri, sizeof(tri));

        auto cmd = renderer.gfx.begin_commands();

        float clear[4] = {0.07f, 0.08f, 0.12f, 1.0f};
        TextureHandle rt = renderer.gfx.get_current_backbuffer(swap);
        renderer.gfx.cmd_begin_rendering(cmd, &rt, 1, clear);
        // draw triangle
        renderer.gfx.cmd_set_pipeline(cmd, pso);
        renderer.gfx.cmd_set_vertex_buffer(cmd, 0, vb, 0);
        renderer.gfx.cmd_draw(cmd, 3, 1, 0, 0);
        renderer.gfx.cmd_end_rendering(cmd);

        renderer.gfx.end_commands(cmd);
        renderer.gfx.submit(&cmd, 1);
        renderer.gfx.present(swap);
        renderer.gfx.end_frame();
    }

    // Optional cleanup
    renderer.gfx.destroy_pipeline(pso);
    renderer.gfx.destroy_shader_module(psh);
    renderer.gfx.destroy_shader_module(vsh);
    renderer.gfx.destroy_buffer(vb);
    renderer.shutdown();
    return 0;
}