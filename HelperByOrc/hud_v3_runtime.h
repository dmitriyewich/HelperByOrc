#pragma once

#include "conditions_module.h"

#include <vector>

class SampApi;

namespace hud_v3 {

class ConditionSnapshot {
public:
    void BeginFrame();
    bool IsBlocked(const std::vector<bool>& flags, SampApi* sampApi) const;

private:
    mutable std::vector<signed char> values_;
    mutable ConditionRuntimeContext context_{};
};

} // namespace hud_v3
