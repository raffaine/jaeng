#pragma once
#include "platform/public/process.h"

namespace jaeng::platform {

class AndroidProcessManager : public IProcessManager {
public:
    AndroidProcessManager() = default;
    ~AndroidProcessManager() override = default;

    result<std::unique_ptr<IProcess>> spawn(const ProcessDesc& desc) override;
};

} // namespace jaeng::platform
