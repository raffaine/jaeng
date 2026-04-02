#include "platform/public/platform_api.h"
#include "sandbox_app.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using namespace jaeng::platform;

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    auto platform = create_platform();
    auto app = std::make_unique<SandboxApp>(*platform);
    
    return platform->run(std::move(app));
}
