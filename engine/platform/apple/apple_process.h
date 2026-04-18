#pragma once

#include "platform/public/process.h"
#include <sys/types.h>

namespace jaeng::platform {

class AppleProcess : public IProcess {
public:
    AppleProcess(pid_t pid);
    ~AppleProcess() override;

    void kill() override;
    bool is_running() override;
    int32_t get_exit_code() override;
    uint32_t get_id() const override { return static_cast<uint32_t>(pid_); }

private:
    pid_t pid_;
    bool exited_ = false;
    int32_t exitCode_ = -1;
};

class AppleProcessManager : public IProcessManager {
public:
    AppleProcessManager();
    ~AppleProcessManager() override;

    result<std::unique_ptr<IProcess>> spawn(const ProcessDesc& desc) override;
};

} // namespace jaeng::platform
