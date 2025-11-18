#include <windows.h>
#include <tchar.h>
#include <stdint.h>
#include "render/frontend/renderer.h"

static const wchar_t* kWndClass = L"SandboxWindowClass";

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
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

    bool running = true;
    MSG msg{};
    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        auto cmd = renderer.gfx.begin_commands();

        float clear[4] = {0.07f, 0.08f, 0.12f, 1.0f};
        TextureHandle rt = renderer.gfx.get_current_backbuffer(swap);
        renderer.gfx.cmd_begin_rendering(cmd, &rt, 1, clear);
        renderer.gfx.cmd_end_rendering(cmd);

        renderer.gfx.end_commands(cmd);
        renderer.gfx.submit(&cmd, 1);
        renderer.gfx.present(swap);

    }

    renderer.shutdown();
    return 0;
}