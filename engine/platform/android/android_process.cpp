#include "android_process.h"
#include "common/logging.h"

namespace jaeng::platform {

result<std::unique_ptr<IProcess>> AndroidProcessManager::spawn(const ProcessDesc& desc) {
    JAENG_LOG_ERROR("[Android] Process spawning is heavily restricted on Android via fork/exec. Cannot spawn: {}", desc.command);
    return Error::fromMessage(-1, "Process spawning is restricted on Android");
}

} // namespace jaeng::platform
