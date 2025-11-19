#include <windows.h>
#include <tchar.h>
#include <stdint.h>
#include <vector>
#include <wrl.h>
#include <d3dcompiler.h>
#include "render/frontend/renderer.h"
#include "render/graph/render_graph.h"

#define USE_PIX    // or _DEBUG / PROFILE / PROFILE_BUILD
#include "pix3.h"  // provided by winpixevent package

using Microsoft::WRL::ComPtr;

static const wchar_t* kWndClass = L"SandboxWindowClass";

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

static const char* kVS = R"(
struct VSIn { float3 pos: POSITION; float3 col: COLOR; float2 uv: TEXCOORD; };
struct VSOut { float4 pos: SV_Position; float3 col: COLOR; float2 uv: TEXCOORD; };
VSOut main(VSIn v) {
    VSOut o; o.pos = float4(v.pos, 1.0); o.col = v.col; o.uv = v.uv; return o;
}
)";

static const char* kPS = R"(
Texture2D t0 : register(t0);
SamplerState s0 : register(s0);
struct PSIn { float4 pos: SV_Position; float3 col: COLOR; float2 uv: TEXCOORD; };
float4 main(PSIn i) : SV_Target {
    float4 tex = t0.Sample(s0, i.uv);
    return float4(i.col, 1.0) * tex;
}
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

    Renderer renderer;
    if (!renderer.initialize(GfxBackend::D3D12, hwnd, 3)) {
        MessageBox(hwnd, L"Failed to initialize renderer.", L"Error", MB_ICONERROR);
        return -2;
    }

    SwapchainDesc swapDesc { {1280u, 720u}, TextureFormat::BGRA8_UNORM, PresentMode::Fifo };
    SwapchainHandle swap = renderer.gfx.create_swapchain(&swapDesc);

    // Textured quad vertex buffer (pos.xyz, col.xyz, uv.xy) - 2 triangles
    struct Vtx { float px,py,pz, cx,cy,cz, u,v; };
    Vtx quad[6] = {
        { -0.6f,  0.6f, 0.0f, 1,1,1, 0,0 },
        {  0.6f,  0.6f, 0.0f, 1,1,1, 1,0 },
        {  0.6f, -0.6f, 0.0f, 1,1,1, 1,1 },
        { -0.6f,  0.6f, 0.0f, 1,1,1, 0,0 },
        {  0.6f, -0.6f, 0.0f, 1,1,1, 1,1 },
        { -0.6f, -0.6f, 0.0f, 1,1,1, 0,1 },
    };
    BufferDesc vbDesc{ sizeof(quad), BufferUsage_Vertex };
    BufferHandle vb = renderer.gfx.create_buffer(&vbDesc, nullptr); // upload below

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

    // Pipeline (POSITION@0 off 0, COLOR@1 off 12, TEXCOORD@2 off 24, stride 32)
    VertexAttributeDesc attrs[3] = { {0,0,0}, {1,0,12}, {2,0,24} };
    VertexLayoutDesc vtxLayout{ 32, attrs, 3 };

    GraphicsPipelineDesc pdesc{
    vsh, psh, PrimitiveTopology::TriangleList, vtxLayout, TextureFormat::BGRA8_UNORM
    };
    PipelineHandle pso = renderer.gfx.create_graphics_pipeline(&pdesc);

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
    TextureDesc td{ TextureFormat::RGBA8_UNORM, W, H, 1, 1, 0 };
    TextureHandle tex = renderer.gfx.create_texture(&td, pixels.data());
    SamplerDesc sd{};
    sd.filter = SamplerFilter::Linear;
    sd.address_u = AddressMode::Repeat; sd.address_v = AddressMode::Repeat; sd.address_w = AddressMode::Repeat;
    sd.mip_lod_bias = 0.0f; sd.min_lod = 0.0f; sd.max_lod = 1000.0f;
    sd.border_color[0]=sd.border_color[1]=sd.border_color[2]=0.0f; sd.border_color[3]=1.0f;
    SamplerHandle samp = renderer.gfx.create_sampler(&sd);

    // Bind-group layout (binding 0: texture SRV @ PS; binding 1: sampler @ PS)
    BindGroupLayoutEntry le[2] = {
        { 0u, 1u, (uint32_t)ShaderStage::Fragment }, // SRV
        { 1u, 2u, (uint32_t)ShaderStage::Fragment }  // Sampler
    };
    BindGroupLayoutDesc ldesc{ le, 2 };
    BindGroupLayoutHandle bgl = renderer.gfx.create_bind_group_layout(&ldesc);
    BindGroupEntry be[2]{};
    be[0].binding = 0; be[0].texture = tex;
    be[1].binding = 1; be[1].sampler = samp;
    BindGroupDesc bgd{ bgl, be, 2 };
    BindGroupHandle bg = renderer.gfx.create_bind_group(&bgd);

    bool uploaded = false;
    bool running = true;
    MSG msg{};
    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        // Build a small render graph: Clear -> Forward
        rg::RenderGraph graph;

        // 1) Clear pass
        {
            rg::RGColorTarget ct{};
            ct.tex = renderer.gfx.get_current_backbuffer(swap);
            ct.clear_rgba[0] = 0.07f;
            ct.clear_rgba[1] = 0.08f;
            ct.clear_rgba[2] = 0.12f;
            ct.clear_rgba[3] = 1.0f;
            graph.add_pass("Clear", { ct }, rg::RGDepthTarget{}, nullptr);
        }
        // 2) Forward pass
        {
            rg::RGColorTarget ct{};
            ct.tex = renderer.gfx.get_current_backbuffer(swap);
            graph.add_pass("Forward", { ct }, rg::RGDepthTarget{},
                [&](const rg::RGPassContext& ctx) {
                    ctx.gfx->cmd_set_pipeline(ctx.cmd, pso);
                    ctx.gfx->cmd_set_bind_group(ctx.cmd, 0, bg);
                    ctx.gfx->cmd_set_vertex_buffer(ctx.cmd, 0, vb, 0);
                    ctx.gfx->cmd_draw(ctx.cmd, 6, 1, 0, 0);
                }
            );
        }

        // Execute
        graph.compile();
        graph.execute(renderer.gfx, swap, [&](RendererAPI& gfx) {
            if (!uploaded) {
                gfx.update_buffer(vb, 0, quad, sizeof(quad));
                uploaded = true;
            }
        });
    }

    // Optional cleanup
    renderer.gfx.destroy_pipeline(pso);
    renderer.gfx.destroy_shader_module(psh);
    renderer.gfx.destroy_shader_module(vsh);
    renderer.gfx.destroy_buffer(vb);
    renderer.gfx.destroy_bind_group(bg);
    renderer.gfx.destroy_bind_group_layout(bgl);
    renderer.gfx.destroy_sampler(samp);
    renderer.gfx.destroy_texture(tex);
    renderer.shutdown();

    return 0;
}
