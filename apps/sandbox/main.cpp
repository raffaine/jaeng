#include "platform/public/platform_api.h"
#include "sandbox_app.h"

#if defined(JAENG_WIN32) && !defined(JAENG_USE_VULKAN)
#include "pix3.h"
#define JAENG_USE_PIX 1
#else
#define JAENG_USE_PIX 0
#endif

#ifdef JAENG_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

using namespace jaeng::platform;

#ifdef JAENG_WIN32
int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
#if JAENG_USE_PIX
    HMODULE hGpuCap = PIXLoadLatestWinPixGpuCapturerLibrary();
#else
    HMODULE hGpuCap {0};
#endif

    auto platform = create_platform();

    if (JAENG_USE_PIX && !hGpuCap) {
        platform->show_message_box("Warning", "Failed to load WinPixGpuCapturer.dll. PIX GPU capture will not be available.", MessageBoxType::Warning);
    }

    auto app = std::make_unique<SandboxApp>(*platform);
    return platform->run(std::move(app));
}
#else
int main(int argc, char* argv[]) {
    auto platform = create_platform();
    auto app = std::make_unique<SandboxApp>(*platform);
    return platform->run(std::move(app));
}
#endif
