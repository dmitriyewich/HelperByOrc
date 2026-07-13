#include "hud_v3_editor_tools.h"

#include <algorithm>
#include <cmath>

namespace hud_v3 {

GuideSnapResult SnapAxisToGuides(
    const std::vector<float>& candidates,
    float begin,
    float center,
    float end,
    float rawDelta,
    float threshold) {
    GuideSnapResult result{ rawDelta, std::nullopt };
    const float sources[] = { begin + rawDelta, center + rawDelta, end + rawDelta };
    float bestDistance = threshold + 1.0f;
    for (float candidate : candidates) {
        for (float source : sources) {
            const float distance = std::abs(candidate - source);
            if (distance <= threshold && distance < bestDistance) {
                bestDistance = distance;
                result.delta = rawDelta + candidate - source;
                result.guide = candidate;
            }
        }
    }
    return result;
}

void NormalizeGuideCandidates(std::vector<float>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end(), [](float left, float right) {
        return std::abs(left - right) < 0.001f;
    }), values.end());
}

} // namespace hud_v3
