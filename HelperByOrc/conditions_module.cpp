#include "conditions_module.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "samp_api.h"

#include <game_sa/CPed.h>
#include <game_sa/common.h>
#include <extensions/ScriptCommands.h>
#include <game_sa/eScriptCommands.h>

#include <imgui.h>

#include <array>

namespace {

constexpr std::array<UiText, static_cast<std::size_t>(ConditionId::Count)> kConditionLabelIds = {
    UiText::ConditionInWater,
    UiText::ConditionDead,
    UiText::ConditionInAir,
    UiText::ConditionInAnyCar,
    UiText::ConditionWithoutWeapon,
    UiText::ConditionWithWeapon,
    UiText::ConditionOnFoot,
    UiText::ConditionChatOpened,
    UiText::ConditionDialogOpened,
    UiText::ConditionSampCursorActive,
    UiText::ConditionWindowsCursorActive,
};

} // namespace

std::size_t ConditionCount() {
    return static_cast<std::size_t>(ConditionId::Count);
}

void NormalizeConditionFlags(std::vector<bool>& flags) {
    flags.resize(ConditionCount(), false);
}

const char* ConditionLabel(ConditionId condition) {
    return UiSettings::Instance().Text(kConditionLabelIds[static_cast<std::size_t>(condition)]);
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
    for (std::size_t i = 0; i < flags.size() && i < ConditionCount(); ++i) {
        if (flags[i]) {
            return true;
        }
    }
    return false;
}

bool IsWindowsCursorActiveForConditions(const ConditionRuntimeContext* context) {
    if (context && !context->gameWindowForeground) {
        return false;
    }
    if (context && context->helperUiCursorActive) {
        return false;
    }

    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (::GetCursorInfo(&info) == FALSE) {
        return false;
    }
    return (info.flags & CURSOR_SHOWING) != 0;
}

bool CheckCondition(ConditionId condition, SampApi* sampApi, const ConditionRuntimeContext* context) {
    auto* player = FindPlayerPed();
    const bool needsPlayer = condition != ConditionId::ChatOpened
        && condition != ConditionId::DialogOpened
        && condition != ConditionId::SampCursorActive
        && condition != ConditionId::WindowsCursorActive;
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
    case ConditionId::DialogOpened:
        return sampApi ? sampApi->isDialogActive() : false;
    case ConditionId::SampCursorActive:
        if (context && context->sampCursorActiveOverride.has_value()) {
            return *context->sampCursorActiveOverride;
        }
        if (!sampApi) {
            return false;
        }
        if (sampApi->is_chat_opened() || sampApi->isDialogActive()) {
            return true;
        }
        if (context && context->helperUiCursorActive) {
            return false;
        }
        return sampApi->IsSampCursorActive();
    case ConditionId::WindowsCursorActive:
        if (context && context->windowsCursorActiveOverride.has_value()) {
            return *context->windowsCursorActiveOverride;
        }
        return IsWindowsCursorActiveForConditions(context);
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
    std::string* message) {
    (void)mode;
    if (!HasSelectedCondition(flags)) {
        return false;
    }

    for (std::size_t i = 0; i < flags.size() && i < ConditionCount(); ++i) {
        if (!flags[i]) {
            continue;
        }
        if (CheckCondition(static_cast<ConditionId>(i), sampApi, context)) {
            if (message) {
                *message = ConditionLabel(static_cast<ConditionId>(i));
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
    if (popupPending) {
        ImGui::OpenPopup(popupId);
        popupPending = false;
    }

    if (!ImGui::BeginPopup(popupId)) {
        return false;
    }

    NormalizeConditionFlags(flags);
    bool changed = false;
    UiSettings& ui = UiSettings::Instance();
    ImGui::TextUnformatted(ui.Text(titleText));
    ImGui::Separator();
    if (combineMode && *combineMode != ConditionCombineMode::RequireAny) {
        *combineMode = ConditionCombineMode::RequireAny;
        changed = true;
    }
    ImGui::TextDisabled("%s", ui.Text(UiText::ConditionBlockHint));
    ImGui::Spacing();
    for (std::size_t i = 0; i < ConditionCount(); ++i) {
        bool value = flags[i];
        if (ImGui::Checkbox(ConditionLabel(static_cast<ConditionId>(i)), &value)) {
            flags[i] = value;
            changed = true;
        }
    }
    ImGui::Spacing();
    if (ImGui::Button(ui.Text(UiText::Save))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return changed;
}
