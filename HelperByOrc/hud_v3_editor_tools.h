#pragma once

#include <optional>
#include <vector>

namespace hud_v3 {

struct GuideSnapResult {
    float delta = 0.0f;
    std::optional<float> guide{};
};

GuideSnapResult SnapAxisToGuides(
    const std::vector<float>& candidates,
    float begin,
    float center,
    float end,
    float rawDelta,
    float threshold);

void NormalizeGuideCandidates(std::vector<float>& values);

} // namespace hud_v3
