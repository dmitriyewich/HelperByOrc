#include "tags_module_impl.h"
#include "tags_module_detail.h"

#include <cmath>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>

namespace {
std::optional<CVector> ResolveLocalPlayerPosition() {
    const CPlayerPed* const playerPed = FindPlayerPed();
    if (!playerPed) {
        return std::nullopt;
    }

    const CVector position = playerPed->GetPosition();
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        return std::nullopt;
    }
    return position;
}

std::string FormatCoordinateValue(float value) {
    if (std::fabs(value) < 0.005f) {
        value = 0.0f;
    }

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(2) << value;
    return stream.str();
}
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinIdTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }
    return std::to_string(sampApi->Local_ID());
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinNickTag(const EvaluationContext& context) const {
    return ResolveLocalNick(context);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinThisbindTag(const EvaluationContext& context) const {
    if (!binderModule_ || context.runningBindRuntimeId == 0) {
        return std::string();
    }
    return binderModule_->GetThisbindTagValue(context.runningBindRuntimeId);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinThisbindNameTag(const EvaluationContext& context) const {
    if (!binderModule_ || context.runningBindRuntimeId == 0) {
        return std::string();
    }
    return binderModule_->GetThisbindNameTagValue(context.runningBindRuntimeId);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinThisbindFolderTag(const EvaluationContext& context) const {
    if (!binderModule_ || context.runningBindRuntimeId == 0) {
        return std::string();
    }
    return binderModule_->GetThisbindFolderTagValue(context.runningBindRuntimeId);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinThiscategoryTag(const EvaluationContext& context) const {
    if (!binderModule_ || context.runningBindRuntimeId == 0) {
        return std::string();
    }
    return binderModule_->GetThiscategoryTagValue(context.runningBindRuntimeId);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinBindStopAllTag(const EvaluationContext& context) const {
    if (!context.allowSideEffects || !binderModule_ || context.runningBindRuntimeId == 0) {
        return std::string();
    }
    static_cast<void>(binderModule_->ExecuteTagAction("stopall", {}, context.runningBindRuntimeId));
    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinTargetIdTag(const EvaluationContext& context) const {
    if (targetTracker_.lastId < 0) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->IsConnected(targetTracker_.lastId)) {
        return std::string();
    }

    return std::to_string(targetTracker_.lastId);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinTargetNickTag(const EvaluationContext& context) const {
    return ResolveLastTargetNick(context);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinTargetRpNickTag(const EvaluationContext& context) const {
    return MakeRpNick(ResolveLastTargetNick(context));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinTargetNameTag(const EvaluationContext& context) const {
    return ExtractName(ResolveLastTargetNick(context));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinTargetSurnameTag(const EvaluationContext& context) const {
    return ExtractSurname(ResolveLastTargetNick(context));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinTargetHealthTag(const EvaluationContext& context) const {
    if (targetTracker_.lastId < 0) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const SampApi::HealthAndArmour stats = sampApi->GetHealthAndArmour(targetTracker_.lastId);
    if (!stats.valid) {
        return std::string();
    }

    return FormatWholeStatValue(stats.health);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinTargetArmourTag(const EvaluationContext& context) const {
    if (targetTracker_.lastId < 0) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const SampApi::HealthAndArmour stats = sampApi->GetHealthAndArmour(targetTracker_.lastId);
    if (!stats.valid) {
        return std::string();
    }

    return FormatWholeStatValue(stats.armour);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinClosestIdTag(const EvaluationContext& context) const {
    const ClosestPlayerQueryResult result = QueryClosestPlayers(context);
    if (result.nearestId < 0) {
        return std::string();
    }
    return std::to_string(result.nearestId);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinClosestIdToCenterTag(const EvaluationContext& context) const {
    const ClosestPlayerQueryResult result = QueryClosestPlayers(context);
    if (result.nearestToCenterId < 0) {
        return std::string();
    }
    return std::to_string(result.nearestToCenterId);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinClosestNameTag(const EvaluationContext& context) const {
    const ClosestPlayerQueryResult result = QueryClosestPlayers(context);
    if (result.nearestId < 0) {
        return std::string();
    }
    return ExtractName(ResolvePlayerNickById(result.nearestId, context));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinClosestSurnameTag(const EvaluationContext& context) const {
    const ClosestPlayerQueryResult result = QueryClosestPlayers(context);
    if (result.nearestId < 0) {
        return std::string();
    }
    return ExtractSurname(ResolvePlayerNickById(result.nearestId, context));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArmourTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string("0");
    }

    const int localId = sampApi->Local_ID();
    if (localId < 0) {
        return std::string("0");
    }

    const SampApi::HealthAndArmour stats = sampApi->GetHealthAndArmour(localId);
    if (!stats.valid) {
        return std::string("0");
    }
    return FormatWholeStatValue(stats.armour);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinHealthTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string("0");
    }

    const int localId = sampApi->Local_ID();
    if (localId < 0) {
        return std::string("0");
    }

    const SampApi::HealthAndArmour stats = sampApi->GetHealthAndArmour(localId);
    if (!stats.valid) {
        return std::string("0");
    }
    return FormatWholeStatValue(stats.health);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinPingTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const int localId = sampApi->Local_ID();
    if (localId < 0) {
        return std::string();
    }

    const std::optional<int> ping = sampApi->GetPlayerPing(localId);
    return ping.has_value() ? std::to_string(*ping) : std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinMyXTag(const EvaluationContext&) const {
    const std::optional<CVector> position = ResolveLocalPlayerPosition();
    if (!position) {
        return std::string();
    }
    return FormatCoordinateValue(position->x);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinMyYTag(const EvaluationContext&) const {
    const std::optional<CVector> position = ResolveLocalPlayerPosition();
    if (!position) {
        return std::string();
    }
    return FormatCoordinateValue(position->y);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinMyZTag(const EvaluationContext&) const {
    const std::optional<CVector> position = ResolveLocalPlayerPosition();
    if (!position) {
        return std::string();
    }
    return FormatCoordinateValue(position->z);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinMyPosTag(const EvaluationContext&) const {
    const std::optional<CVector> position = ResolveLocalPlayerPosition();
    if (!position) {
        return std::string();
    }

    return FormatCoordinateValue(position->x)
        + ", "
        + FormatCoordinateValue(position->y)
        + ", "
        + FormatCoordinateValue(position->z);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinDateTag(const EvaluationContext&) const {
    return FormatCurrentTime("%d.%m.%Y");
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinMySkinTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    CPed* playerPed = FindPlayerPed();
    if (!playerPed) {
        return std::string();
    }
    return std::to_string(playerPed->m_nModelIndex);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinMyWeaponTag(const EvaluationContext&) const {
    const CWeapon* const weapon = FindLocalWeapon();
    if (!weapon) {
        return std::string();
    }
    return GetWeaponDisplayName(weapon->m_eWeaponType);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinMyWeaponIdTag(const EvaluationContext&) const {
    const CWeapon* const weapon = FindLocalWeapon();
    if (!weapon) {
        return std::string();
    }
    return std::to_string(static_cast<unsigned int>(weapon->m_eWeaponType));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinMyWeaponClipTag(const EvaluationContext&) const {
    const CWeapon* const weapon = FindLocalWeapon();
    if (!weapon) {
        return std::string("0");
    }
    return std::to_string(weapon->m_nAmmoInClip);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinMyMoneyTag(const EvaluationContext&) const {
    CPlayerPed* const playerPed = FindPlayerPed();
    if (!playerPed) {
        return std::string();
    }

    CPlayerInfo* const playerInfo = playerPed->GetPlayerInfoForThisPlayerPed();
    if (!playerInfo) {
        return std::string();
    }

    return std::to_string(playerInfo->m_nMoney);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinFpsTag(const EvaluationContext&) const {
    if (!ImGui::GetCurrentContext()) {
        return std::string("0");
    }

    const float fps = std::max(0.0f, ImGui::GetIO().Framerate);
    return std::to_string(std::lround(fps));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinGetVehTypeTag(const EvaluationContext&) const {
    if (CVehicle* const vehicle = FindPlayerVehicle(-1, false); IsVehiclePointerValid(vehicle)) {
        return GetVehicleTypeName(vehicle->m_nModelIndex);
    }

    return ResolveVehicleTypeForPed(FindPlayerPed());
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinNickRpTag(const EvaluationContext& context) const {
    return MakeRpNick(ResolveLocalNick(context));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinNameTag(const EvaluationContext& context) const {
    return ExtractName(ResolveLocalNick(context));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinSurnameTag(const EvaluationContext& context) const {
    return ExtractSurname(ResolveLocalNick(context));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinTimeTag(const EvaluationContext&) const {
    return FormatCurrentTime("%H:%M:%S");
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinTimeNoSecTag(const EvaluationContext&) const {
    return FormatCurrentTime("%H:%M");
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinNickFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }
    return ResolvePlayerNickById(*id, context);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinRpNickFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }
    return MakeRpNick(ResolvePlayerNickById(*id, context));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinNameFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }
    return ExtractName(ResolvePlayerNickById(*id, context));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinSurnameFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }
    return ExtractSurname(ResolvePlayerNickById(*id, context));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinParamcmdFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (context.activationSource != "command") {
        return std::string();
    }

    std::string command = Trim(context.bindCommand);
    std::string input = Trim(context.activationText);
    if (!command.empty() && command.front() != '/') {
        command.insert(command.begin(), '/');
    }
    if (!input.empty() && input.front() != '/') {
        input.insert(input.begin(), '/');
    }

    if (command.empty() || input.empty() || !StartsWith(input, command)) {
        return std::string();
    }
    if (input.size() > command.size()
        && std::isspace(static_cast<unsigned char>(input[command.size()])) == 0) {
        return std::string();
    }

    const std::string argsText = Trim(input.substr(command.size()));
    const std::vector<std::string> args = SplitCommandArgs(argsText);
    if (args.empty()) {
        return std::string();
    }

    std::string selector = Trim(param);
    selector.erase(
        std::remove_if(selector.begin(), selector.end(), [](const unsigned char ch) {
            return std::isspace(ch) != 0;
        }),
        selector.end());
    if (selector.empty()) {
        return std::string();
    }

    const auto parsePositiveIndex = [](std::string_view raw) -> int {
        if (raw.empty()) {
            return -1;
        }
        int value = 0;
        for (const unsigned char ch : raw) {
            if (std::isdigit(ch) == 0) {
                return -1;
            }
            value = value * 10 + static_cast<int>(ch - '0');
        }
        return value;
    };

    if (const int single = parsePositiveIndex(selector); single >= 0) {
        if (single < 1 || single > static_cast<int>(args.size())) {
            return std::string();
        }
        return args[static_cast<std::size_t>(single - 1)];
    }

    if (selector.back() == '+' && selector.size() > 1) {
        const int from = parsePositiveIndex(std::string_view(selector).substr(0, selector.size() - 1));
        if (from < 1 || from > static_cast<int>(args.size())) {
            return std::string();
        }

        std::string joined;
        for (int i = from - 1; i < static_cast<int>(args.size()); ++i) {
            if (!joined.empty()) {
                joined += ' ';
            }
            joined += args[static_cast<std::size_t>(i)];
        }
        return joined;
    }

    if (selector.back() == '-' && selector.size() > 1) {
        const int upto = parsePositiveIndex(std::string_view(selector).substr(0, selector.size() - 1));
        if (upto < 1) {
            return std::string();
        }

        const int clamped = std::min(upto, static_cast<int>(args.size()));
        std::string joined;
        for (int i = 0; i < clamped; ++i) {
            if (!joined.empty()) {
                joined += ' ';
            }
            joined += args[static_cast<std::size_t>(i)];
        }
        return joined;
    }

    if (const std::size_t dashPos = selector.find('-'); dashPos != std::string::npos) {
        const int from = parsePositiveIndex(std::string_view(selector).substr(0, dashPos));
        const int to = parsePositiveIndex(std::string_view(selector).substr(dashPos + 1));
        if (from < 1 || to < from || from > static_cast<int>(args.size())) {
            return std::string();
        }

        const int clampedTo = std::min(to, static_cast<int>(args.size()));
        std::string joined;
        for (int i = from - 1; i < clampedTo; ++i) {
            if (!joined.empty()) {
                joined += ' ';
            }
            joined += args[static_cast<std::size_t>(i)];
        }
        return joined;
    }

    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinMathFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    const std::string expression = Trim(param);
    if (expression.empty()) {
        return std::string();
    }

    MathExpressionParser parser(expression);
    const std::optional<double> result = parser.Evaluate();
    if (!result.has_value()) {
        return std::string();
    }
    return FormatMathResult(*result);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinNumberWithDotsFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    return FormatNumberWithDots(param);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArmourFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }

    const SampApi::HealthAndArmour stats = sampApi->GetHealthAndArmour(*id);
    if (!stats.valid) {
        return std::string();
    }
    return FormatWholeStatValue(stats.armour);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinHealthFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }

    const SampApi::HealthAndArmour stats = sampApi->GetHealthAndArmour(*id);
    if (!stats.valid) {
        return std::string();
    }
    return FormatWholeStatValue(stats.health);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinPingFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const std::string selector = Unquote(Trim(param));
    if (selector.empty()) {
        return std::string();
    }

    std::optional<int> id = ParseInteger(selector);
    if (!id.has_value()) {
        id = sampApi->GetIDByName(selector);
    }
    if (!id.has_value()) {
        return std::string();
    }

    const std::optional<int> ping = sampApi->GetPlayerPing(*id);
    return ping.has_value() ? std::to_string(*ping) : std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinSkinFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }

    const CPed* const ped = FindPlayerPedBySampId(*sampApi, *id);
    if (!ped) {
        return std::string();
    }

    return std::to_string(ped->m_nModelIndex);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinNickColorFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string("{FFFFFF}");
    }
    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value() || !sampApi->IsConnected(*id)) {
        return std::string("{FFFFFF}");
    }

    const std::optional<std::uint32_t> color = sampApi->GetPlayerColor(*id);
    return color.has_value() ? FormatSampColorTag(*color) : std::string("{FFFFFF}");
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinCarFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }

    return ResolveVehicleNameForPed(FindPlayerPedBySampId(*sampApi, *id));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinCarHealthFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }

    const CPed* const ped = FindPlayerPedBySampId(*sampApi, *id);
    if (!ped || !IsVehiclePointerValid(ped->m_pVehicle)) {
        return std::string();
    }
    return FormatWholeStatValue(ped->m_pVehicle->m_fHealth);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinStrLowFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    return ToLowerUtf8(param);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinAddTimeFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    const std::optional<std::int64_t> deltaSeconds = ParseTimeOffsetSeconds(param);
    if (!deltaSeconds.has_value()) {
        return std::string();
    }

    const std::time_t now = std::time(nullptr);
    return FormatCurrentTimeForTimestamp(now + static_cast<std::time_t>(*deltaSeconds), "%H:%M:%S");
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinRandomFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    const std::vector<std::string_view> rawOptions = SplitTopLevelDelimitedParts(param, ';');
    if (rawOptions.size() > 1) {
        return Unquote(Trim(rawOptions[RandomIndex(rawOptions.size())]));
    }

    const std::string rawValue = Unquote(Trim(param));
    if (rawValue.empty()) {
        return std::to_string(RandomIntInclusive(kRandomMinInt, kRandomMaxInt));
    }

    if (const std::optional<std::pair<int, int>> range = ParseRandomIntegerRange(rawValue); range.has_value()) {
        return std::to_string(RandomIntInclusive(range->first, range->second));
    }

    if (const std::optional<int> parsed = ParseInteger(rawValue); parsed.has_value()) {
        if (*parsed == 0) {
            return std::string("0");
        }

        int minValue = 1;
        int maxValue = *parsed;
        if (maxValue < minValue) {
            std::swap(minValue, maxValue);
        }

        return std::to_string(RandomIntInclusive(minValue, maxValue));
    }

    return rawValue;
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinIfAndOrFunctionTag(
    std::string_view rawParam,
    const EvaluationContext& context,
    int depth) const {
    const IfAndOrSplitResult split = SplitIfAndOrParam(rawParam);
    if (!split.valid) {
        if (context.allowSideEffects && binderModule_) {
            NotifyTagError(UiSettings::Instance().Text(UiText::ToastIfAndOrInvalidSyntax), 2800.0);
        }
        return std::string();
    }

    const std::string trimmedCondition(TrimAsciiWhitespace(split.condition));
    if (trimmedCondition.empty()) {
        if (context.allowSideEffects && binderModule_) {
            NotifyTagError(UiSettings::Instance().Text(UiText::ToastIfAndOrEmptyCondition), 2800.0);
        }
        return std::string();
    }

    EvaluationContext conditionContext = context;
    conditionContext.allowSideEffects = false;
    const std::string expandedCondition = ExpandTextRecursive(trimmedCondition, conditionContext, depth + 1);
    std::string conditionError;
    const std::optional<bool> conditionResult = EvaluateConditionExpression(expandedCondition, conditionError);
    if (!conditionResult.has_value()) {
        if (context.allowSideEffects && binderModule_) {
            NotifyTagError(
                UiSettings::Instance().Format(UiText::ToastIfAndOrConditionFailed, conditionError.c_str()),
                3200.0);
        }
        return std::string();
    }

    const std::string_view branch = *conditionResult ? split.whenTrue : split.whenFalse;
    return ExpandTextRecursive(branch, context, depth + 1);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinTimefFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const TimeFormatParseResult parsed = ParseTimefFormat(param);
    if (!parsed.Ok()) {
        if (context.allowSideEffects && binderModule_) {
            NotifyTagError(DescribeTimeFormatError(parsed.error, parsed.invalidToken), 2800.0);
        }
        return std::string();
    }

    const std::string formatted = FormatCurrentTime(parsed.format);
    if (formatted.empty()) {
        if (context.allowSideEffects && binderModule_) {
            NotifyTagError(DescribeTimeFormatError(TimeFormatError::FormatFailed, {}), 2800.0);
        }
        return std::string();
    }
    return formatted;
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinGetVehTypeFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }

    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }

    return ResolveVehicleTypeForPed(FindPlayerPedBySampId(*sampApi, *id));
}
