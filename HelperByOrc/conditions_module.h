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

enum class ConditionCheckMode {
    Normal,
    IgnoreCursorConditions,
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
    HelperActive,
    GameHudVisible,
    CameraAttached,
    Driver,
    Passenger,
    GtaMenuOpen,
    InBoat,
    InCar,
    InTrain,
    InHeli,
    InPlane,
    InBike,
    InFakePlane,
    InMonsterTruck,
    InQuadBike,
    InBicycle,
    DeprecatedInTrailer,
    GameHudHidden,
    CameraDetached,
    GtaMenuClosed,
    SampCursorInactive,
    WindowsCursorInactive,
    ChatClosed,
    DialogClosed,
    NotInBoat,
    NotInTrain,
    NotInPlane,
    NotInFakePlane,
    NotInCar,
    NotInHeli,
    NotInBike,
    NotInMonsterTruck,
    NotInBicycle,
    NotInQuadBike,
    NotInWater,
    NotInAir,
    ScoreboardOpen,
    ScoreboardClosed,
    ChatVisible,
    ChatHidden,
    ServerConnected,
    ServerDisconnected,
    VehicleSirenOn,
    VehicleSirenOff,
    VehicleEngineOn,
    VehicleEngineOff,
    Count,
};

struct ConditionRuntimeContext {
    bool helperUiActive = false;
    bool helperUiCursorActive = false;
    bool gameWindowForeground = true;
    std::optional<bool> sampCursorActiveOverride{};
    std::optional<bool> windowsCursorActiveOverride{};
};

std::size_t ConditionCount();
void NormalizeConditionFlags(std::vector<bool>& flags);
void SetConditionFlag(std::vector<bool>& flags, ConditionId condition, bool value);
const char* ConditionLabel(ConditionId condition);
ConditionCombineMode NormalizeConditionCombineMode(std::string_view value);
std::string ConditionCombineModeId(ConditionCombineMode mode);
bool HasSelectedCondition(const std::vector<bool>& flags);
bool IsCursorCondition(ConditionId condition);
bool HasCursorCondition(const std::vector<bool>& flags);
bool IsWindowsCursorActiveForConditions(const ConditionRuntimeContext* context = nullptr);
bool CheckCondition(ConditionId condition, SampApi* sampApi, const ConditionRuntimeContext* context = nullptr);
bool ConditionsBlocked(
    const std::vector<bool>& flags,
    ConditionCombineMode mode,
    SampApi* sampApi,
    const ConditionRuntimeContext* context = nullptr,
    std::string* message = nullptr,
    ConditionCheckMode checkMode = ConditionCheckMode::Normal);
bool DrawConditionFlagsPopup(
    const char* popupId,
    bool& popupPending,
    UiText titleText,
    std::vector<bool>& flags,
    ConditionCombineMode* combineMode);
