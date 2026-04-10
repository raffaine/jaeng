#include "wayland_process.h"
#include "common/logging.h"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <signal.h>
#include <algorithm>
#include <cstring>

namespace jaeng::platform {

WaylandProcess::WaylandProcess(pid_t pid) : pid_(pid) {}

WaylandProcess::~WaylandProcess() {}

void WaylandProcess::kill() {
    if (pid_ > 0 && !exited_) {
        ::kill(pid_, SIGTERM);
    }
}

bool WaylandProcess::is_running() {
    if (exited_) return false;
    int status;
    pid_t result = waitpid(pid_, &status, WNOHANG);
    if (result == 0) {
        return true;
    } else if (result == pid_) {
        exited_ = true;
        if (WIFEXITED(status)) {
            exitCode_ = WEXITSTATUS(status);
        }
        return false;
    } else {
        exited_ = true;
        return false;
    }
}

int32_t WaylandProcess::get_exit_code() {
    is_running(); // update state
    return exitCode_;
}

result<std::unique_ptr<IProcess>> WaylandProcessManager::spawn(const ProcessDesc& desc) {
    // Prepare args in parent to avoid allocations in child
    std::vector<char*> args;
    args.push_back(const_cast<char*>(desc.command.c_str()));
    for (const auto& arg : desc.args) {
        args.push_back(const_cast<char*>(arg.c_str()));
    }
    args.push_back(nullptr);

    const char* cmd = desc.command.c_str();
    const char* wd = desc.workingDir.empty() ? nullptr : desc.workingDir.c_str();
    bool detached = desc.detached;

    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        if (!detached) {
            // Kernel sends SIGTERM to child if parent dies
            if (prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
                const char* msg = "CHILD ERROR: prctl failed\n";
                write(2, msg, strlen(msg));
            }
        }

        if (wd) {
            if (chdir(wd) != 0) {
                const char* msg = "CHILD ERROR: chdir failed\n";
                write(2, msg, strlen(msg));
                _exit(1);
            }
        }

        execvp(cmd, args.data());
        
        // If execvp returns, it failed
        const char* msg = "CHILD ERROR: execvp failed\n";
        write(2, msg, strlen(msg));
        _exit(1);
    } else if (pid > 0) {
        // Parent process
        return { std::make_unique<WaylandProcess>(pid) };
    } else {
        return { jaeng::Error::fromMessage((int)jaeng::error_code::platform_error, "Failed to fork") };
    }
}

} // namespace jaeng::platform
