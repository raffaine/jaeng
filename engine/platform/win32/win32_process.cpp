#include "win32_process.h"
#include "common/logging.h"
#include <algorithm>

namespace jaeng::platform {

Win32Process::Win32Process(HANDLE processHandle, uint32_t processId)
    : processHandle_(processHandle), processId_(processId) {}

Win32Process::~Win32Process() {
    if (processHandle_ != NULL) {
        CloseHandle(processHandle_);
    }
}

void Win32Process::kill() {
    if (processHandle_ != NULL && !exited_) {
        TerminateProcess(processHandle_, 1);
        exited_ = true;
    }
}

bool Win32Process::is_running() {
    if (exited_) return false;
    DWORD exitCode;
    if (GetExitCodeProcess(processHandle_, &exitCode)) {
        if (exitCode == STILL_ACTIVE) {
            return true;
        } else {
            exited_ = true;
            exitCode_ = static_cast<int32_t>(exitCode);
            return false;
        }
    }
    exited_ = true;
    return false;
}

int32_t Win32Process::get_exit_code() {
    is_running(); // update state
    return exitCode_;
}

Win32ProcessManager::Win32ProcessManager() {
    jobObject_ = CreateJobObjectA(NULL, NULL);
    if (jobObject_) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = { 0 };
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(jobObject_, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) {
            JAENG_LOG_ERROR("Failed to set Job Object information: {}", GetLastError());
            CloseHandle(jobObject_);
            jobObject_ = NULL;
        }
    } else {
        JAENG_LOG_ERROR("Failed to create Job Object: {}", GetLastError());
    }
}

Win32ProcessManager::~Win32ProcessManager() {
    if (jobObject_) {
        // Closing the job object handle will terminate all processes in the job
        // due to JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        CloseHandle(jobObject_);
    }
}

result<std::unique_ptr<IProcess>> Win32ProcessManager::spawn(const ProcessDesc& desc) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    std::string commandLine = desc.command;
    for (const auto& arg : desc.args) {
        commandLine += " " + arg;
    }

    if (!CreateProcessA(
            NULL,
            const_cast<char*>(commandLine.c_str()),
            NULL,
            NULL,
            FALSE,
            CREATE_BREAKAWAY_FROM_JOB, // Allow us to assign it to our job object
            NULL,
            desc.workingDir.empty() ? NULL : desc.workingDir.c_str(),
            &si,
            &pi)
    ) {
        // Fallback if CREATE_BREAKAWAY_FROM_JOB fails (it can fail if parent is not allowed to breakaway)
        if (!CreateProcessA(
                NULL,
                const_cast<char*>(commandLine.c_str()),
                NULL,
                NULL,
                FALSE,
                0,
                NULL,
                desc.workingDir.empty() ? NULL : desc.workingDir.c_str(),
                &si,
                &pi)
        ) {
            return { Error::fromLastError() };
        }
    }

    if (!desc.detached && jobObject_) {
        if (!AssignProcessToJobObject(jobObject_, pi.hProcess)) {
            JAENG_LOG_WARN("Failed to assign process to job object: {}", GetLastError());
        }
    }

    CloseHandle(pi.hThread);
    return { std::make_unique<Win32Process>(pi.hProcess, pi.dwProcessId) };
}

} // namespace jaeng::platform
