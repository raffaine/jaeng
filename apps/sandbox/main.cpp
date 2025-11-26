#include <windows.h>
#include <tchar.h>
#include <stdint.h>
#include <array>
#include <vector>
#include <wrl.h>
#include <d3dcompiler.h>
#include "render/frontend/renderer.h"
#include "render/graph/render_graph.h"

#include "basic_reflect.h"

#define USE_PIX
#include "pix3.h"  // provided by winpixevent package

using Microsoft::WRL::ComPtr;

static const wchar_t* kWndClass = L"SandboxWindowClass";

Renderer renderer {};
SwapchainHandle swap = 0;

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {        
        case WM_SIZE: {
            UINT w = LOWORD(lParam), h = HIWORD(lParam);
            if (swap > 0 && w > 0 && h > 0) {
                renderer.gfx.resize_swapchain(swap, { w, h });
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

    if (!renderer.initialize(GfxBackend::D3D12, hwnd, 3)) {
        MessageBox(hwnd, L"Failed to initialize renderer.", L"Error", MB_ICONERROR);
        return -2;
    }

    // SwapChain and Default Depth Buffer
    DepthStencilDesc depthDesc{};   // No depth/stencil for now
    depthDesc.depth_enable = true;
    depthDesc.depth_format = TextureFormat::D32F;
    SwapchainDesc swapDesc { {1280u, 720u}, TextureFormat::BGRA8_UNORM, depthDesc, PresentMode::Fifo };
    swap = renderer.gfx.create_swapchain(&swapDesc);

    // Mesh: a colored quad (two triangles)
    struct Vtx { float px, py, pz; float r, g, b; float u,v; };
    const std::array<Vtx,4> vertices = {{
        { -0.5f, -0.5f, .0f, 1,0,0, 0,0 },
        {  0.5f, -0.5f, .0f, 0,1,0, 1,0 },
        {  0.5f,  0.5f, .0f, 0,0,1, 1,1 },
        { -0.5f,  0.5f, .0f, 1,1,0, 0,1 },
    }};
    const std::array<uint32_t,6> indices = {{ 0,1,2, 0,2,3 }};

    // Vertex and Index Buffer Resources
    BufferDesc vbd{};
    vbd.size_bytes = sizeof(vertices);
    vbd.usage      = BufferUsage_Vertex;
    BufferHandle vb = renderer.gfx.create_buffer(&vbd, vertices.data());

    BufferDesc ibd{};
    ibd.size_bytes = sizeof(indices);
    ibd.usage      = BufferUsage_Index;
    BufferHandle ib = renderer.gfx.create_buffer(&ibd, indices.data());

    // Use Pre-compiled Shaders and Reflected Headers
    ShaderModuleHandle vsh, psh;
    LoadShaders(&renderer.gfx, vsh, psh);

    // Creates Pipeline (also with reflecte header for Vertex Layout)
    GraphicsPipelineDesc pdesc{
    vsh, psh, PrimitiveTopology::TriangleList, ShaderReflection::vertexLayout, TextureFormat::BGRA8_UNORM
    };
    PipelineHandle pso = renderer.gfx.create_graphics_pipeline(&pdesc);

    // Constant Buffer for VS/PS
    const CBTransform cbData[4] = {
        Translate(-0.25f, -0.25f, 0.5),
        Translate( 0.25f, -0.25f, 0.5),
        Translate(-0.25f,  0.25f, 0.5),
        Translate( 0.25f,  0.25f, 0.5)
    };

    BufferDesc   cbDesc{256, BufferUsage_Uniform};
    BufferHandle cb = renderer.gfx.create_buffer(&cbDesc, nullptr);

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

    // Create Texture and Sampler Resources
    TextureDesc td{ TextureFormat::RGBA8_UNORM, W, H, 1, 1, 0 };
    TextureHandle tex = renderer.gfx.create_texture(&td, pixels.data());
    SamplerDesc sd{};
    sd.filter = SamplerFilter::Linear;
    sd.address_u = AddressMode::Repeat; sd.address_v = AddressMode::Repeat; sd.address_w = AddressMode::Repeat;
    sd.mip_lod_bias = 0.0f; sd.min_lod = 0.0f; sd.max_lod = 1000.0f;
    sd.border_color[0]=sd.border_color[1]=sd.border_color[2]=0.0f; sd.border_color[3]=1.0f;
    SamplerHandle samp = renderer.gfx.create_sampler(&sd);

    // Use Bind Group Layout from Reflection and created Resources as Group Entries
    BindGroupLayoutHandle bgl = renderer.gfx.create_bind_group_layout(&ShaderReflection::bindGroupLayout);
    BindGroupEntry be[] {
        { .type = BindGroupEntryType::Texture, .texture = tex },
        { .type = BindGroupEntryType::Sampler, .sampler = samp },
        { .type = BindGroupEntryType::UniformBuffer, .buffer = cb, .offset = 0, .size = 256 }
    };
    BindGroupDesc bgd{ bgl, be, 3 };
    BindGroupHandle bg = renderer.gfx.create_bind_group(&bgd);

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
            .tex = renderer.gfx.get_current_backbuffer(swap),
            .clear_rgba = { 0.07f, 0.08f, 0.12f, 1.0f }
        } }, { .tex = 1, .clear_depth = 1.0f }, nullptr);
        // 2) Forward pass
        graph.add_pass("Forward", 
            { { .tex = renderer.gfx.get_current_backbuffer(swap) } }, { .tex = 1 /*enable depth*/ },
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
        graph.execute(renderer.gfx, swap, 0, nullptr);
    }

    // Releases renderer resources
    renderer.shutdown();

    return 0;
}
