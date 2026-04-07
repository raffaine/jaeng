#include "win32_platform.h"
#include <windowsx.h>

namespace jaeng::platform {

Win32Platform* Win32Platform::instance_ = nullptr;

static KeyCode map_win32_key(WPARAM wParam) {
    switch (wParam) {
        case VK_ESCAPE: return KeyCode::Escape;
        case VK_SPACE:  return KeyCode::Space;
        case 'W':       return KeyCode::W;
        case 'A':       return KeyCode::A;
        case 'S':       return KeyCode::S;
        case 'D':       return KeyCode::D;
        case 'E':       return KeyCode::E;
        case 'Q':       return KeyCode::Q;
        case VK_OEM_PLUS: return KeyCode::Plus;
        case VK_OEM_MINUS: return KeyCode::Minus;
        case VK_ADD:      return KeyCode::Plus;
        case VK_SUBTRACT: return KeyCode::Minus;
        default:        return KeyCode::Unknown;
    }
}

Win32Platform::Win32Platform() {
    instance_ = this;
}

Win32Platform::~Win32Platform() {
    instance_ = nullptr;
}

jaeng::result<std::unique_ptr<IWindow>> Win32Platform::create_window(const WindowDesc& desc) {
    HINSTANCE hInst = GetModuleHandle(nullptr);
    const wchar_t* className = L"JaengWindowClass";

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = className;
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    RECT r{0, 0, (LONG)desc.width, (LONG)desc.height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    std::wstring wtitle(desc.title.begin(), desc.title.end());

    HWND hwnd = CreateWindowExW(0, className, wtitle.c_str(), 
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, 
                                CW_USEDEFAULT, CW_USEDEFAULT, 
                                r.right - r.left, r.bottom - r.top, 
                                nullptr, nullptr, hInst, nullptr);

    if (!hwnd) {
        return jaeng::Error::fromLastError();
    }

    std::unique_ptr<IWindow> window = std::make_unique<Win32Window>(hwnd, desc.width, desc.height);
    return window;
}

bool Win32Platform::poll_events() {
    MSG msg{};
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

void Win32Platform::show_message_box(const std::string& title, const std::string& content, MessageBoxType type) {
    UINT uType = MB_OK;
    switch (type) {
        case MessageBoxType::Info:    uType |= MB_ICONINFORMATION; break;
        case MessageBoxType::Warning: uType |= MB_ICONWARNING; break;
        case MessageBoxType::Error:   uType |= MB_ICONERROR; break;
    }
    std::wstring wtitle(title.begin(), title.end());
    std::wstring wcontent(content.begin(), content.end());
    MessageBoxW(NULL, wcontent.c_str(), wtitle.c_str(), uType);
}

int Win32Platform::run(std::unique_ptr<IApplication> app) {
    if (!app->init()) return -1;

    app->start_engine_threads();

    while (poll_events() && !app->should_close()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    app->stop_engine_threads();
    app->shutdown();
    return 0;
}

LRESULT CALLBACK Win32Platform::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!instance_) return DefWindowProcW(hwnd, msg, wParam, lParam);

    Event ev{};
    switch (msg) {
        case WM_SIZE: {
            uint32_t w = LOWORD(lParam);
            uint32_t h = HIWORD(lParam);
            ev.type = Event::Type::WindowResize;
            ev.resize.width = w;
            ev.resize.height = h;
            if (instance_->eventCallback_) instance_->eventCallback_(ev);
            return 0;
        }
        case WM_CLOSE:
            ev.type = Event::Type::WindowClose;
            if (instance_->eventCallback_) instance_->eventCallback_(ev);
            // Fall through or handle close explicitly
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            KeyCode code = map_win32_key(wParam);
            instance_->input_.set_key_state(code, true);
            ev.type = Event::Type::KeyDown;
            ev.key.code = code;
            if (instance_->eventCallback_) instance_->eventCallback_(ev);
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            KeyCode code = map_win32_key(wParam);
            instance_->input_.set_key_state(code, false);
            ev.type = Event::Type::KeyUp;
            ev.key.code = code;
            if (instance_->eventCallback_) instance_->eventCallback_(ev);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int32_t x = GET_X_LPARAM(lParam);
            int32_t y = GET_Y_LPARAM(lParam);
            instance_->input_.set_mouse_pos(x, y);
            ev.type = Event::Type::MouseMove;
            ev.mouse.x = x;
            ev.mouse.y = y;
            if (instance_->eventCallback_) instance_->eventCallback_(ev);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            ev.type = Event::Type::MouseDown;
            ev.mouse.x = GET_X_LPARAM(lParam);
            ev.mouse.y = GET_Y_LPARAM(lParam);
            ev.mouse.button = (msg == WM_LBUTTONDOWN) ? 272 : (msg == WM_RBUTTONDOWN) ? 273 : 274;
            if (instance_->eventCallback_) instance_->eventCallback_(ev);
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            ev.type = Event::Type::MouseUp;
            ev.mouse.x = GET_X_LPARAM(lParam);
            ev.mouse.y = GET_Y_LPARAM(lParam);
            ev.mouse.button = (msg == WM_LBUTTONUP) ? 272 : (msg == WM_RBUTTONUP) ? 273 : 274;
            if (instance_->eventCallback_) instance_->eventCallback_(ev);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            ev.type = Event::Type::MouseScroll;
            ev.scroll.delta = (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
            if (instance_->eventCallback_) instance_->eventCallback_(ev);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool Win32Input::is_key_down(KeyCode code) const {
    auto it = keys_.find(code);
    return (it != keys_.end()) ? it->second : false;
}

MousePos Win32Input::get_mouse_pos() const {
    return mousePos_;
}

std::unique_ptr<IPlatform> create_platform() {
    return std::make_unique<Win32Platform>();
}

} // namespace jaeng::platform
