#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace hud_v3 {

enum class WorkspaceMode {
    Compact,
    Wide,
};

struct WorkspaceLayout {
    WorkspaceMode mode = WorkspaceMode::Compact;
    float navigationWidth = 0.0f;
    float canvasWidth = 0.0f;
    float inspectorWidth = 0.0f;
    float gap = 0.0f;
};

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

WorkspaceLayout CalculateWorkspaceLayout(float availableWidth, float uiScale);

float CalculateNavigationSplit(
    float availableHeight,
    float uiScale,
    std::size_t widgetCount,
    float gap);

} // namespace hud_v3
