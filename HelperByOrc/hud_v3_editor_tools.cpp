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

WorkspaceLayout CalculateWorkspaceLayout(float availableWidth, float uiScale) {
    const float scale = std::max(0.5f, uiScale);
    const float normalizedWidth = std::max(0.0f, availableWidth) / scale;
    constexpr float kWideThreshold = 1040.0f;
    if (normalizedWidth < kWideThreshold) {
        return WorkspaceLayout{ WorkspaceMode::Compact, 0.0f, std::max(0.0f, availableWidth), 0.0f, 0.0f };
    }

    const float gap = 8.0f * scale;
    const float navigationWidth = std::clamp(normalizedWidth * 0.20f, 210.0f, 260.0f) * scale;
    const float inspectorWidth = std::clamp(normalizedWidth * 0.27f, 300.0f, 370.0f) * scale;
    const float canvasWidth = std::max(1.0f, availableWidth - navigationWidth - inspectorWidth - gap * 2.0f);
    return WorkspaceLayout{ WorkspaceMode::Wide, navigationWidth, canvasWidth, inspectorWidth, gap };
}

float CalculateNavigationSplit(
    float availableHeight,
    float uiScale,
    std::size_t widgetCount,
    float gap) {
    const float scale = std::max(0.5f, uiScale);
    const float safeHeight = std::max(1.0f, availableHeight);
    const float safeGap = std::max(0.0f, gap);
    const float layersReserve = 150.0f * scale;
    const float maximum = std::max(1.0f, safeHeight - layersReserve - safeGap);
    const float minimum = std::min(140.0f * scale, maximum);
    const float visibleRows = static_cast<float>(std::min<std::size_t>(widgetCount, 5));
    const float contentHeight = (64.0f + visibleRows * 42.0f) * scale;
    return std::clamp(contentHeight, minimum, std::max(minimum, std::min(300.0f * scale, maximum)));
}

} // namespace hud_v3
