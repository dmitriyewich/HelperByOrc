#pragma once

#include "ui_settings.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class SampApi;

enum class ConditionCombineMode {
    RequireAll,
    RequireAny,
};

enum class ConditionId : std::size_t {
    InWater = 0,
    Dead,
    InAir,
    InAnyCar,
    WithoutWeapon,
    WithWeapon,
    OnFoot,
    ChatOpened,
    DialogOpened,
    SampCursorActive,
    WindowsCursorActive,
    Count,
};

struct ConditionRuntimeContext {
    bool helperUiCursorActive = false;
    bool gameWindowForeground = true;
    std::optional<bool> sampCursorActiveOverride{};
    std::optional<bool> windowsCursorActiveOverride{};
};

std::size_t ConditionCount();
void NormalizeConditionFlags(std::vector<bool>& flags);
const char* ConditionLabel(ConditionId condition);
ConditionCombineMode NormalizeConditionCombineMode(std::string_view value);
std::string ConditionCombineModeId(ConditionCombineMode mode);
bool HasSelectedCondition(const std::vector<bool>& flags);
bool IsWindowsCursorActiveForConditions(const ConditionRuntimeContext* context = nullptr);
bool CheckCondition(ConditionId condition, SampApi* sampApi, const ConditionRuntimeContext* context = nullptr);
bool ConditionsBlocked(
    const std::vector<bool>& flags,
    ConditionCombineMode mode,
    SampApi* sampApi,
    const ConditionRuntimeContext* context = nullptr,
    std::string* message = nullptr);
bool DrawConditionFlagsPopup(
    const char* popupId,
    bool& popupPending,
    UiText titleText,
    std::vector<bool>& flags,
    ConditionCombineMode* combineMode);
