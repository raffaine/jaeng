#pragma once

#include "common/result.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace jaeng::platform {

struct ProcessDesc {
    std::string command;
    std::vector<std::string> args;
    std::string workingDir;
    bool detached = false; // If true, process survives engine exit
};

class IProcess {
public:
    virtual ~IProcess() = default;
    virtual void kill() = 0;
    virtual bool is_running() = 0;
    virtual int32_t get_exit_code() = 0;
    virtual uint32_t get_id() const = 0;
};

class IProcessManager {
public:
    virtual ~IProcessManager() = default;
    virtual result<std::unique_ptr<IProcess>> spawn(const ProcessDesc& desc) = 0;
};

} // namespace jaeng::platform
