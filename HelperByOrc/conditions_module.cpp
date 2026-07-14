#include "conditions_module.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "samp_api.h"

#include <game_sa/CMenuManager.h>
#include <game_sa/CCamera.h>
#include <game_sa/CExplosion.h>
#include <game_sa/CModelInfo.h>
#include <game_sa/CPed.h>
#include <game_sa/CPools.h>
#include <game_sa/CTheScripts.h>
#include <game_sa/CVehicle.h>
#include <game_sa/common.h>
#include <extensions/ScriptCommands.h>
#include <game_sa/eScriptCommands.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cwchar>

namespace {

constexpr std::uintptr_t kGameHudVisibleFlagAddress = 0xBA6769;

enum class ConditionCategory : std::size_t {
    Player = 0,
    Interface,
    Game,
    Vehicle,
    Count,
};

struct ConditionDefinition {
    ConditionId id;
    UiText label;
    ConditionCategory category;
};

constexpr std::array kConditionDefinitions = {
    ConditionDefinition{ConditionId::Dead, UiText::ConditionDead, ConditionCategory::Player},
    ConditionDefinition{ConditionId::OnFoot, UiText::ConditionOnFoot, ConditionCategory::Player},
    ConditionDefinition{ConditionId::WithWeapon, UiText::ConditionWithWeapon, ConditionCategory::Player},
    ConditionDefinition{ConditionId::WithoutWeapon, UiText::ConditionWithoutWeapon, ConditionCategory::Player},
    ConditionDefinition{ConditionId::InWater, UiText::ConditionInWater, ConditionCategory::Player},
    ConditionDefinition{ConditionId::NotInWater, UiText::ConditionNotInWater, ConditionCategory::Player},
    ConditionDefinition{ConditionId::InAir, UiText::ConditionInAir, ConditionCategory::Player},
    ConditionDefinition{ConditionId::NotInAir, UiText::ConditionNotInAir, ConditionCategory::Player},
    ConditionDefinition{ConditionId::ChatOpened, UiText::ConditionChatOpened, ConditionCategory::Interface},
    ConditionDefinition{ConditionId::ChatClosed, UiText::ConditionChatClosed, ConditionCategory::Interface},
    ConditionDefinition{ConditionId::ChatVisible, UiText::ConditionChatVisible, ConditionCategory::Interface},
    ConditionDefinition{ConditionId::ChatHidden, UiText::ConditionChatHidden, ConditionCategory::Interface},
    ConditionDefinition{ConditionId::DialogOpened, UiText::ConditionDialogOpened, ConditionCategory::Interface},
    ConditionDefinition{ConditionId::DialogClosed, UiText::ConditionDialogClosed, ConditionCategory::Interface},
    ConditionDefinition{ConditionId::ScoreboardOpen, UiText::ConditionScoreboardOpen, ConditionCategory::Interface},
    ConditionDefinition{ConditionId::ScoreboardClosed, UiText::ConditionScoreboardClosed, ConditionCategory::Interface},
    ConditionDefinition{ConditionId::SampCursorActive, UiText::ConditionSampCursorActive, ConditionCategory::Interface},
    ConditionDefinition{ConditionId::SampCursorInactive, UiText::ConditionSampCursorInactive, ConditionCategory::Interface},
    ConditionDefinition{ConditionId::WindowsCursorActive, UiText::ConditionWindowsCursorActive, ConditionCategory::Interface},
    ConditionDefinition{ConditionId::WindowsCursorInactive, UiText::ConditionWindowsCursorInactive, ConditionCategory::Interface},
    ConditionDefinition{ConditionId::GtaMenuOpen, UiText::ConditionGtaMenuOpen, ConditionCategory::Interface},
    ConditionDefinition{ConditionId::GtaMenuClosed, UiText::ConditionGtaMenuClosed, ConditionCategory::Interface},
    ConditionDefinition{ConditionId::GameHudVisible, UiText::ConditionGameHudVisible, ConditionCategory::Game},
    ConditionDefinition{ConditionId::GameHudHidden, UiText::ConditionGameHudHidden, ConditionCategory::Game},
    ConditionDefinition{ConditionId::CameraLookingAtPlayer, UiText::ConditionCameraLookingAtPlayer, ConditionCategory::Game},
    ConditionDefinition{ConditionId::CameraNotLookingAtPlayer, UiText::ConditionCameraNotLookingAtPlayer, ConditionCategory::Game},
    ConditionDefinition{ConditionId::AnyExplosionActive, UiText::ConditionAnyExplosionActive, ConditionCategory::Game},
    ConditionDefinition{ConditionId::NoActiveExplosion, UiText::ConditionNoActiveExplosion, ConditionCategory::Game},
    ConditionDefinition{ConditionId::ServerConnected, UiText::ConditionServerConnected, ConditionCategory::Game},
    ConditionDefinition{ConditionId::ServerDisconnected, UiText::ConditionServerDisconnected, ConditionCategory::Game},
    ConditionDefinition{ConditionId::InAnyCar, UiText::ConditionInAnyCar, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::Driver, UiText::ConditionDriver, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::Passenger, UiText::ConditionPassenger, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::PassengerDriveByOn, UiText::ConditionPassengerDriveByOn, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::PassengerDriveByOff, UiText::ConditionPassengerDriveByOff, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::VehicleEngineOn, UiText::ConditionVehicleEngineOn, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::VehicleEngineOff, UiText::ConditionVehicleEngineOff, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::VehicleLightsOn, UiText::ConditionVehicleLightsOn, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::VehicleLightsOff, UiText::ConditionVehicleLightsOff, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::VehicleSirenOn, UiText::ConditionVehicleSirenOn, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::VehicleSirenOff, UiText::ConditionVehicleSirenOff, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::InCar, UiText::ConditionVehicleCar, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::NotInCar, UiText::ConditionNotVehicleCar, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::InBoat, UiText::ConditionVehicleBoat, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::NotInBoat, UiText::ConditionNotVehicleBoat, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::InHeli, UiText::ConditionVehicleHeli, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::NotInHeli, UiText::ConditionNotVehicleHeli, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::InPlane, UiText::ConditionVehiclePlane, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::NotInPlane, UiText::ConditionNotVehiclePlane, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::InFakePlane, UiText::ConditionVehicleFakePlane, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::NotInFakePlane, UiText::ConditionNotVehicleFakePlane, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::InTrain, UiText::ConditionVehicleTrain, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::NotInTrain, UiText::ConditionNotVehicleTrain, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::InBike, UiText::ConditionVehicleBike, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::NotInBike, UiText::ConditionNotVehicleBike, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::InBicycle, UiText::ConditionVehicleBicycle, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::NotInBicycle, UiText::ConditionNotVehicleBicycle, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::InMonsterTruck, UiText::ConditionVehicleMonsterTruck, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::NotInMonsterTruck, UiText::ConditionNotVehicleMonsterTruck, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::InQuadBike, UiText::ConditionVehicleQuadBike, ConditionCategory::Vehicle},
    ConditionDefinition{ConditionId::NotInQuadBike, UiText::ConditionNotVehicleQuadBike, ConditionCategory::Vehicle},
};

constexpr std::array<UiText, static_cast<std::size_t>(ConditionCategory::Count)> kConditionCategoryLabelIds = {
    UiText::ConditionCategoryPlayer,
    UiText::ConditionCategoryInterface,
    UiText::ConditionCategoryGame,
    UiText::ConditionCategoryVehicle,
};

std::size_t ConditionIndex(ConditionId condition) {
    return static_cast<std::size_t>(condition);
}

const ConditionDefinition* ConditionDefinitionFor(ConditionId condition) {
    for (const ConditionDefinition& definition : kConditionDefinitions) {
        if (definition.id == condition) {
            return &definition;
        }
    }
    return nullptr;
}

bool IsSelectableCondition(ConditionId condition) {
    return ConditionDefinitionFor(condition) != nullptr;
}

std::int8_t ReadGameInt8(std::uintptr_t address) {
    __try {
        return *reinterpret_cast<volatile const std::int8_t*>(address);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

std::string LowerAscii(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const unsigned char ch : text) {
        result.push_back(static_cast<char>(std::tolower(ch)));
    }
    return result;
}

std::string ToSearchKey(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    const int wideLength = ::MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (wideLength <= 0) {
        return LowerAscii(text);
    }

    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    ::MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        wide.data(),
        wideLength);
    ::CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));

    const int utf8Length = ::WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.data(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (utf8Length <= 0) {
        return LowerAscii(text);
    }

    std::string result(static_cast<std::size_t>(utf8Length), '\0');
    ::WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.data(),
        static_cast<int>(wide.size()),
        result.data(),
        utf8Length,
        nullptr,
        nullptr);
    return result;
}

bool LabelMatchesSearch(const char* label, std::string_view searchKey) {
    return searchKey.empty() || ToSearchKey(label).find(searchKey) != std::string::npos;
}

bool IsVehiclePointerValid(const CVehicle* vehicle) {
    if (!vehicle) {
        return false;
    }

    auto* const vehiclePool = CPools::ms_pVehiclePool;
    return vehiclePool != nullptr && vehiclePool->IsObjectValid(const_cast<CVehicle*>(vehicle));
}

CVehicle* ResolvePlayerVehicle(CPed* player) {
    if (CVehicle* const vehicle = FindPlayerVehicle(-1, false); IsVehiclePointerValid(vehicle)) {
        return vehicle;
    }
    if (player && IsVehiclePointerValid(player->m_pVehicle)) {
        return player->m_pVehicle;
    }
    return nullptr;
}

bool CheckVehicleType(ConditionId condition, int modelId) {
    switch (condition) {
    case ConditionId::InBoat:
        return CModelInfo::IsBoatModel(modelId);
    case ConditionId::InCar:
        return CModelInfo::IsCarModel(modelId);
    case ConditionId::InTrain:
        return CModelInfo::IsTrainModel(modelId);
    case ConditionId::InHeli:
        return CModelInfo::IsHeliModel(modelId);
    case ConditionId::InPlane:
        return CModelInfo::IsPlaneModel(modelId);
    case ConditionId::InBike:
        return CModelInfo::IsBikeModel(modelId);
    case ConditionId::InFakePlane:
        return CModelInfo::IsFakePlaneModel(modelId);
    case ConditionId::InMonsterTruck:
        return CModelInfo::IsMonsterTruckModel(modelId);
    case ConditionId::InQuadBike:
        return CModelInfo::IsQuadBikeModel(modelId);
    case ConditionId::InBicycle:
        return CModelInfo::IsBmxModel(modelId);
    default:
        return false;
    }
}

ConditionId PositiveVehicleConditionFor(ConditionId condition) {
    switch (condition) {
    case ConditionId::NotInBoat:
        return ConditionId::InBoat;
    case ConditionId::NotInTrain:
        return ConditionId::InTrain;
    case ConditionId::NotInPlane:
        return ConditionId::InPlane;
    case ConditionId::NotInFakePlane:
        return ConditionId::InFakePlane;
    case ConditionId::NotInCar:
        return ConditionId::InCar;
    case ConditionId::NotInHeli:
        return ConditionId::InHeli;
    case ConditionId::NotInBike:
        return ConditionId::InBike;
    case ConditionId::NotInMonsterTruck:
        return ConditionId::InMonsterTruck;
    case ConditionId::NotInBicycle:
        return ConditionId::InBicycle;
    case ConditionId::NotInQuadBike:
        return ConditionId::InQuadBike;
    default:
        return ConditionId::Count;
    }
}

bool CheckNotVehicleType(ConditionId condition, CPed* player) {
    const ConditionId positiveCondition = PositiveVehicleConditionFor(condition);
    if (positiveCondition == ConditionId::Count) {
        return false;
    }

    CVehicle* const vehicle = ResolvePlayerVehicle(player);
    return !vehicle || !CheckVehicleType(positiveCondition, vehicle->m_nModelIndex);
}

bool IsGameHudVisible() {
    return ReadGameInt8(kGameHudVisibleFlagAddress) != 0 && CTheScripts::bDisplayHud;
}

bool IsCameraLookingAtPlayer(const ConditionRuntimeContext* context) {
    if (context && context->cameraLookingAtPlayerCache.has_value()) {
        return *context->cameraLookingAtPlayerCache;
    }

    const bool lookingAtPlayer = TheCamera.m_bLookingAtPlayer;
    if (context) {
        context->cameraLookingAtPlayerCache = lookingAtPlayer;
    }
    return lookingAtPlayer;
}

bool HasActiveExplosion(const ConditionRuntimeContext* context) {
    if (context && context->anyExplosionActiveCache.has_value()) {
        return *context->anyExplosionActiveCache;
    }

    bool active = false;
    const unsigned int explosionCount = MAX_EXPLOSIONS;
    for (unsigned int index = 0; index < explosionCount; ++index) {
        if (aExplosions[index].m_bIsActive) {
            active = true;
            break;
        }
    }

    if (context) {
        context->anyExplosionActiveCache = active;
    }
    return active;
}

bool IsGtaMenuOpen() {
    return FrontEndMenuManager.m_bMenuActive || FrontEndMenuManager.m_bSaveMenuActive;
}

int CountSelectedConditions(const std::vector<bool>& flags) {
    int selected = 0;
    for (const ConditionDefinition& definition : kConditionDefinitions) {
        const std::size_t index = ConditionIndex(definition.id);
        if (index < flags.size() && flags[index]) {
            ++selected;
        }
    }
    return selected;
}

void DrawSelectedConditionChips(std::vector<bool>& flags, bool& changed) {
    const float contentRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    bool hasChipOnLine = false;

    for (const ConditionDefinition& definition : kConditionDefinitions) {
        const std::size_t index = ConditionIndex(definition.id);
        if (index >= flags.size() || !flags[index]) {
            continue;
        }

        const std::string label = std::string("x ") + ConditionLabel(definition.id)
            + "##condition_chip_" + std::to_string(index);
        const ImVec2 textSize = ImGui::CalcTextSize(label.c_str(), nullptr, true);
        const float buttonWidth = textSize.x + ImGui::GetStyle().FramePadding.x * 2.0f;

        if (hasChipOnLine) {
            if (ImGui::GetCursorScreenPos().x + ImGui::GetStyle().ItemSpacing.x + buttonWidth > contentRight) {
                hasChipOnLine = false;
            } else {
                ImGui::SameLine();
            }
        }

        if (ImGui::SmallButton(label.c_str())) {
            flags[index] = false;
            changed = true;
        }
        hasChipOnLine = true;
    }
}

} // namespace

std::size_t ConditionCount() {
    return static_cast<std::size_t>(ConditionId::Count);
}

void NormalizeConditionFlags(std::vector<bool>& flags) {
    flags.resize(ConditionCount(), false);
    for (std::size_t i = 0; i < flags.size(); ++i) {
        if (!IsSelectableCondition(static_cast<ConditionId>(i))) {
            flags[i] = false;
        }
    }
}

void SetConditionFlag(std::vector<bool>& flags, ConditionId condition, bool value) {
    NormalizeConditionFlags(flags);
    if (!IsSelectableCondition(condition)) {
        return;
    }
    flags[ConditionIndex(condition)] = value;
}

const char* ConditionLabel(ConditionId condition) {
    if (const ConditionDefinition* definition = ConditionDefinitionFor(condition)) {
        return UiSettings::Instance().Text(definition->label);
    }
    return UiSettings::Instance().Text(UiText::ConditionDeprecated);
}

ConditionCombineMode NormalizeConditionCombineMode(std::string_view value) {
    (void)value;
    return ConditionCombineMode::RequireAny;
}

std::string ConditionCombineModeId(ConditionCombineMode mode) {
    (void)mode;
    return "require_any";
}

bool HasSelectedCondition(const std::vector<bool>& flags) {
    for (const ConditionDefinition& definition : kConditionDefinitions) {
        const std::size_t index = ConditionIndex(definition.id);
        if (index < flags.size() && flags[index]) {
            return true;
        }
    }
    return false;
}

bool IsCursorCondition(ConditionId condition) {
    return condition == ConditionId::SampCursorActive
        || condition == ConditionId::SampCursorInactive
        || condition == ConditionId::WindowsCursorActive
        || condition == ConditionId::WindowsCursorInactive;
}

bool HasCursorCondition(const std::vector<bool>& flags) {
    for (const ConditionDefinition& definition : kConditionDefinitions) {
        if (!IsCursorCondition(definition.id)) {
            continue;
        }
        const std::size_t index = ConditionIndex(definition.id);
        if (index < flags.size() && flags[index]) {
            return true;
        }
    }
    return false;
}

bool TryGetWindowsCursorActiveForConditions(const ConditionRuntimeContext* context, bool& active) {
    active = false;
    if (context && !context->gameWindowForeground) {
        return false;
    }
    if (context && context->helperUiCursorActive) {
        active = true;
        return true;
    }

    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (::GetCursorInfo(&info) == FALSE) {
        return false;
    }
    active = (info.flags & CURSOR_SHOWING) != 0;
    return true;
}

bool TryGetSampCursorActiveForConditions(SampApi* sampApi, const ConditionRuntimeContext* context, bool& active) {
    active = false;
    if (context && context->sampCursorActiveOverride.has_value()) {
        active = *context->sampCursorActiveOverride;
        return true;
    }
    if (!sampApi) {
        return false;
    }
    if (sampApi->is_chat_opened() || sampApi->isDialogActive()) {
        active = true;
        return true;
    }
    if (context && context->helperUiCursorActive) {
        active = true;
        return true;
    }

    active = sampApi->IsSampCursorActive();
    return true;
}

bool IsWindowsCursorActiveForConditions(const ConditionRuntimeContext* context) {
    bool active = false;
    return TryGetWindowsCursorActiveForConditions(context, active) && active;
}

bool CheckCondition(ConditionId condition, SampApi* sampApi, const ConditionRuntimeContext* context) {
    auto* player = FindPlayerPed();
    const bool needsPlayer = condition != ConditionId::ChatOpened
        && condition != ConditionId::ChatClosed
        && condition != ConditionId::ChatVisible
        && condition != ConditionId::ChatHidden
        && condition != ConditionId::DialogOpened
        && condition != ConditionId::DialogClosed
        && condition != ConditionId::ScoreboardOpen
        && condition != ConditionId::ScoreboardClosed
        && condition != ConditionId::SampCursorActive
        && condition != ConditionId::SampCursorInactive
        && condition != ConditionId::WindowsCursorActive
        && condition != ConditionId::WindowsCursorInactive
        && condition != ConditionId::HelperActive
        && condition != ConditionId::GameHudVisible
        && condition != ConditionId::GameHudHidden
        && condition != ConditionId::CameraLookingAtPlayer
        && condition != ConditionId::CameraNotLookingAtPlayer
        && condition != ConditionId::AnyExplosionActive
        && condition != ConditionId::NoActiveExplosion
        && condition != ConditionId::ServerConnected
        && condition != ConditionId::ServerDisconnected
        && condition != ConditionId::GtaMenuOpen
        && condition != ConditionId::GtaMenuClosed
        && condition != ConditionId::DeprecatedInTrailer;
    if (needsPlayer && !player) {
        return false;
    }

    switch (condition) {
    case ConditionId::InWater:
        return plugin::Command<plugin::Commands::IS_CHAR_IN_WATER>(player);
    case ConditionId::Dead:
        return plugin::Command<plugin::Commands::IS_CHAR_DEAD>(player);
    case ConditionId::InAir:
        return plugin::Command<plugin::Commands::IS_CHAR_IN_AIR>(player);
    case ConditionId::NotInAir:
        return !plugin::Command<plugin::Commands::IS_CHAR_IN_AIR>(player);
    case ConditionId::InAnyCar:
        return plugin::Command<plugin::Commands::IS_CHAR_IN_ANY_CAR>(player);
    case ConditionId::WithoutWeapon: {
        int weapon = 0;
        plugin::Command<plugin::Commands::GET_CURRENT_CHAR_WEAPON>(player, &weapon);
        return weapon == 0;
    }
    case ConditionId::WithWeapon: {
        int weapon = 0;
        plugin::Command<plugin::Commands::GET_CURRENT_CHAR_WEAPON>(player, &weapon);
        return weapon != 0;
    }
    case ConditionId::OnFoot:
        return plugin::Command<plugin::Commands::IS_CHAR_ON_FOOT>(player);
    case ConditionId::ChatOpened:
        return sampApi ? sampApi->is_chat_opened() : false;
    case ConditionId::ChatClosed:
        return sampApi ? !sampApi->is_chat_opened() : false;
    case ConditionId::ChatVisible:
        return sampApi ? sampApi->IsChatVisible() : false;
    case ConditionId::ChatHidden:
        return sampApi ? !sampApi->IsChatVisible() : false;
    case ConditionId::DialogOpened:
        return sampApi ? sampApi->isDialogActive() : false;
    case ConditionId::DialogClosed:
        return sampApi ? !sampApi->isDialogActive() : false;
    case ConditionId::ScoreboardOpen:
        return sampApi ? sampApi->IsScoreboardOpen() : false;
    case ConditionId::ScoreboardClosed:
        return sampApi ? !sampApi->IsScoreboardOpen() : false;
    case ConditionId::SampCursorActive: {
        bool active = false;
        return TryGetSampCursorActiveForConditions(sampApi, context, active) && active;
    }
    case ConditionId::SampCursorInactive: {
        bool active = false;
        return TryGetSampCursorActiveForConditions(sampApi, context, active) && !active;
    }
    case ConditionId::WindowsCursorActive: {
        bool active = false;
        if (context && context->windowsCursorActiveOverride.has_value()) {
            active = *context->windowsCursorActiveOverride;
            return active;
        }
        return TryGetWindowsCursorActiveForConditions(context, active) && active;
    }
    case ConditionId::WindowsCursorInactive: {
        bool active = false;
        if (context && context->windowsCursorActiveOverride.has_value()) {
            active = *context->windowsCursorActiveOverride;
            return !active;
        }
        return TryGetWindowsCursorActiveForConditions(context, active) && !active;
    }
    case ConditionId::HelperActive:
        return context ? context->helperUiActive : false;
    case ConditionId::GameHudVisible:
        return IsGameHudVisible();
    case ConditionId::GameHudHidden:
        return !IsGameHudVisible();
    case ConditionId::CameraLookingAtPlayer:
        return IsCameraLookingAtPlayer(context);
    case ConditionId::CameraNotLookingAtPlayer:
        return !IsCameraLookingAtPlayer(context);
    case ConditionId::AnyExplosionActive:
        return HasActiveExplosion(context);
    case ConditionId::NoActiveExplosion:
        return !HasActiveExplosion(context);
    case ConditionId::ServerConnected:
        return sampApi ? sampApi->IsServerConnected() : false;
    case ConditionId::ServerDisconnected:
        return sampApi ? !sampApi->IsServerConnected() : false;
    case ConditionId::Driver: {
        CVehicle* const vehicle = ResolvePlayerVehicle(player);
        return vehicle && vehicle->IsDriver(player);
    }
    case ConditionId::Passenger: {
        CVehicle* const vehicle = ResolvePlayerVehicle(player);
        return vehicle && vehicle->IsPassenger(player);
    }
    case ConditionId::PassengerDriveByOn:
    case ConditionId::PassengerDriveByOff: {
        CVehicle* const vehicle = ResolvePlayerVehicle(player);
        if (!vehicle || !vehicle->IsPassenger(player) || !sampApi) {
            return false;
        }

        const std::optional<bool> driveByState = sampApi->GetLocalPassengerDriveByState();
        return driveByState.has_value()
            && *driveByState == (condition == ConditionId::PassengerDriveByOn);
    }
    case ConditionId::VehicleSirenOn: {
        CVehicle* const vehicle = ResolvePlayerVehicle(player);
        return vehicle && vehicle->bSirenOrAlarm;
    }
    case ConditionId::VehicleSirenOff: {
        CVehicle* const vehicle = ResolvePlayerVehicle(player);
        return vehicle && !vehicle->bSirenOrAlarm;
    }
    case ConditionId::VehicleEngineOn: {
        CVehicle* const vehicle = ResolvePlayerVehicle(player);
        return vehicle && vehicle->bEngineOn;
    }
    case ConditionId::VehicleEngineOff: {
        CVehicle* const vehicle = ResolvePlayerVehicle(player);
        return vehicle && !vehicle->bEngineOn;
    }
    case ConditionId::VehicleLightsOn: {
        CVehicle* const vehicle = ResolvePlayerVehicle(player);
        return vehicle && vehicle->bLightsOn;
    }
    case ConditionId::VehicleLightsOff: {
        CVehicle* const vehicle = ResolvePlayerVehicle(player);
        return vehicle && !vehicle->bLightsOn;
    }
    case ConditionId::GtaMenuOpen:
        return IsGtaMenuOpen();
    case ConditionId::GtaMenuClosed:
        return !IsGtaMenuOpen();
    case ConditionId::InBoat:
    case ConditionId::InCar:
    case ConditionId::InTrain:
    case ConditionId::InHeli:
    case ConditionId::InPlane:
    case ConditionId::InBike:
    case ConditionId::InFakePlane:
    case ConditionId::InMonsterTruck:
    case ConditionId::InQuadBike:
    case ConditionId::InBicycle: {
        CVehicle* const vehicle = ResolvePlayerVehicle(player);
        return vehicle && CheckVehicleType(condition, vehicle->m_nModelIndex);
    }
    case ConditionId::NotInBoat:
    case ConditionId::NotInTrain:
    case ConditionId::NotInPlane:
    case ConditionId::NotInFakePlane:
    case ConditionId::NotInCar:
    case ConditionId::NotInHeli:
    case ConditionId::NotInBike:
    case ConditionId::NotInMonsterTruck:
    case ConditionId::NotInBicycle:
    case ConditionId::NotInQuadBike:
        return CheckNotVehicleType(condition, player);
    case ConditionId::NotInWater:
        return !plugin::Command<plugin::Commands::IS_CHAR_IN_WATER>(player);
    case ConditionId::DeprecatedInTrailer:
    case ConditionId::Count:
        break;
    }

    return false;
}

bool ConditionsBlocked(
    const std::vector<bool>& flags,
    ConditionCombineMode mode,
    SampApi* sampApi,
    const ConditionRuntimeContext* context,
    std::string* message,
    ConditionCheckMode checkMode) {
    (void)mode;
    if (!HasSelectedCondition(flags)) {
        return false;
    }

    for (const ConditionDefinition& definition : kConditionDefinitions) {
        if (checkMode == ConditionCheckMode::IgnoreCursorConditions && IsCursorCondition(definition.id)) {
            continue;
        }
        const std::size_t index = ConditionIndex(definition.id);
        if (index >= flags.size() || !flags[index]) {
            continue;
        }
        if (CheckCondition(definition.id, sampApi, context)) {
            if (message) {
                *message = ConditionLabel(definition.id);
            }
            return true;
        }
    }
    return false;
}

bool DrawConditionFlagsPopup(
    const char* popupId,
    bool& popupPending,
    UiText titleText,
    std::vector<bool>& flags,
    ConditionCombineMode* combineMode) {
    static std::array<char, 128> searchBuffer{};
    static int activeCategory = static_cast<int>(ConditionCategory::Player);

    if (popupPending) {
        searchBuffer.fill('\0');
        activeCategory = static_cast<int>(ConditionCategory::Player);
        ImGui::OpenPopup(popupId);
        popupPending = false;
    }

    const float scale = std::max(0.5f, UiSettings::Instance().CurrentScale());
    ImGui::SetNextWindowSize(ImVec2(620.0f * scale, 440.0f * scale), ImGuiCond_Always);
    if (!ImGui::BeginPopup(popupId, ImGuiWindowFlags_NoResize)) {
        return false;
    }

    NormalizeConditionFlags(flags);
    bool changed = false;
    UiSettings& ui = UiSettings::Instance();

    ImGui::Text("%s", ui.Text(titleText));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ui.Format(UiText::ConditionSelectedCount, CountSelectedConditions(flags)).c_str());
    ImGui::Separator();

    if (combineMode && *combineMode != ConditionCombineMode::RequireAny) {
        *combineMode = ConditionCombineMode::RequireAny;
        changed = true;
    }
    ImGui::TextDisabled("%s", ui.Text(UiText::ConditionBlockHint));
    ImGui::Spacing();

    ImGui::SetNextItemWidth(std::max(80.0f * scale, ImGui::GetContentRegionAvail().x - 112.0f * scale));
    ImGui::InputTextWithHint("##condition_search", ui.Text(UiText::ConditionSearchHint), searchBuffer.data(), searchBuffer.size());
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::ConditionReset), ImVec2(0.0f, 0.0f))) {
        std::fill(flags.begin(), flags.end(), false);
        changed = true;
    }

    if (HasSelectedCondition(flags)) {
        if (ImGui::BeginChild("##condition_selected_chips", ImVec2(0.0f, 58.0f * scale), ImGuiChildFlags_None)) {
            DrawSelectedConditionChips(flags, changed);
        }
        ImGui::EndChild();
    } else {
        ImGui::TextDisabled("%s", ui.Text(UiText::ConditionSelectedNone));
    }
    ImGui::Spacing();

    const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const float pickerHeight = std::max(120.0f * scale, ImGui::GetContentRegionAvail().y - footerHeight);
    if (ImGui::BeginChild("##condition_categories", ImVec2(150.0f * scale, pickerHeight), ImGuiChildFlags_FrameStyle)) {
        for (std::size_t i = 0; i < static_cast<std::size_t>(ConditionCategory::Count); ++i) {
            const bool selected = activeCategory == static_cast<int>(i);
            if (ImGui::Selectable(ui.Text(kConditionCategoryLabelIds[i]), selected)) {
                activeCategory = static_cast<int>(i);
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    const std::string searchKey = ToSearchKey(searchBuffer.data());
    if (ImGui::BeginChild("##condition_items", ImVec2(0.0f, pickerHeight), ImGuiChildFlags_FrameStyle)) {
        int visibleCount = 0;
        if (ImGui::BeginTable("##condition_table", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) {
            for (const ConditionDefinition& definition : kConditionDefinitions) {
                if (static_cast<int>(definition.category) != activeCategory) {
                    continue;
                }

                const char* const label = ui.Text(definition.label);
                if (!LabelMatchesSearch(label, searchKey)) {
                    continue;
                }

                ++visibleCount;
                const std::size_t index = ConditionIndex(definition.id);
                bool value = flags[index];
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::Checkbox(label, &value)) {
                    flags[index] = value;
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (visibleCount == 0) {
            ImGui::TextDisabled("%s", ui.Text(UiText::ConditionNoMatches));
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::Button(ui.Text(UiText::Save))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    return changed;
}
