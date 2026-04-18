#include "apple_process.h"
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#if !defined(JAENG_IOS)
#include <spawn.h>
extern char **environ;
#endif

namespace jaeng::platform {

AppleProcess::AppleProcess(pid_t pid) : pid_(pid) {}

AppleProcess::~AppleProcess() {
    if (is_running()) {
        kill();
    }
}

void AppleProcess::kill() {
    if (pid_ > 0) {
        ::kill(pid_, SIGKILL);
    }
}

bool AppleProcess::is_running() {
    if (exited_) return false;
    
    int status;
    pid_t result = waitpid(pid_, &status, WNOHANG);
    if (result == 0) return true;
    if (result == pid_) {
        exited_ = true;
        if (WIFEXITED(status)) {
            exitCode_ = WEXITSTATUS(status);
        }
        return false;
    }
    return false;
}

int32_t AppleProcess::get_exit_code() {
    is_running();
    return exitCode_;
}

AppleProcessManager::AppleProcessManager() {}
AppleProcessManager::~AppleProcessManager() {}

result<std::unique_ptr<IProcess>> AppleProcessManager::spawn(const ProcessDesc& desc) {
#if defined(JAENG_IOS)
    return jaeng::Error::fromMessage(-1, "Process spawning is not supported on iOS");
#else
    pid_t pid;
    std::vector<char*> args;
    args.push_back(const_cast<char*>(desc.command.c_str()));
    for (const auto& arg : desc.args) {
        args.push_back(const_cast<char*>(arg.c_str()));
    }
    args.push_back(nullptr);

    int status = posix_spawn(&pid, desc.command.c_str(), nullptr, nullptr, args.data(), environ);
    if (status != 0) {
        return jaeng::Error::fromMessage(status, "Failed to spawn process");
    }

    return jaeng::result<std::unique_ptr<IProcess>>(std::make_unique<AppleProcess>(pid));
#endif
}

} // namespace jaeng::platform
