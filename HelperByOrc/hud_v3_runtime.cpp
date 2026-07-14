#include "hud_v3_runtime.h"

#include "samp_api.h"

#include <algorithm>

namespace hud_v3 {

void ConditionSnapshot::BeginFrame() {
    values_.assign(ConditionCount(), static_cast<signed char>(-1));
    context_ = {};
}

bool ConditionSnapshot::IsBlocked(const std::vector<bool>& flags, SampApi* sampApi) const {
    if (!HasSelectedCondition(flags)) {
        return false;
    }
    if (values_.size() != ConditionCount()) {
        values_.assign(ConditionCount(), static_cast<signed char>(-1));
    }
    const std::size_t count = std::min(flags.size(), ConditionCount());
    for (std::size_t index = 0; index < count; ++index) {
        if (!flags[index]) {
            continue;
        }
        signed char& cached = values_[index];
        if (cached < 0) {
            cached = CheckCondition(static_cast<ConditionId>(index), sampApi, &context_) ? 1 : 0;
        }
        if (cached != 0) {
            return true;
        }
    }
    return false;
}

} // namespace hud_v3
