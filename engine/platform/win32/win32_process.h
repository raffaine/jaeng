#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "platform/public/process.h"
#include <mutex>

namespace jaeng::platform {

class Win32Process : public IProcess {
public:
    Win32Process(HANDLE processHandle, uint32_t processId);
    ~Win32Process() override;

    void kill() override;
    bool is_running() override;
    int32_t get_exit_code() override;
    uint32_t get_id() const override { return processId_; }

private:
    HANDLE processHandle_;
    uint32_t processId_;
    bool exited_ = false;
    int32_t exitCode_ = -1;
};

class Win32ProcessManager : public IProcessManager {
public:
    Win32ProcessManager();
    ~Win32ProcessManager() override;

    result<std::unique_ptr<IProcess>> spawn(const ProcessDesc& desc) override;

private:
    HANDLE jobObject_ = NULL;
};

} // namespace jaeng::platform
