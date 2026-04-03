#include "platform/public/platform_api.h"
#include "sandbox_app.h"
#include "pix3.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using namespace jaeng::platform;

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    HMODULE hGpuCap = PIXLoadLatestWinPixGpuCapturerLibrary();
    
    auto platform = create_platform();

    if (!hGpuCap) {
        platform->show_message_box("Warning", "Failed to load WinPixGpuCapturer.dll. PIX GPU capture will not be available.", MessageBoxType::Warning);
    }

    auto app = std::make_unique<SandboxApp>(*platform);
    return platform->run(std::move(app));
}
