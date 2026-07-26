#include "tags_module_impl.h"
#include "tags_module_detail.h"

#include "binder_module.h"
#include "conditions_module.h"
#include "debug_log.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>

namespace {

constexpr int kWaitIfMaxPlayerId = 1003;
constexpr unsigned int kWaitIfMaxVirtualKey = 0xFF;
constexpr std::uint64_t kWaitIfPollIntervalMs = 50;

std::optional<double> ParseWaitIfNumber(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    double value = 0.0;
    const auto parsed = std::from_chars(
        text.data(),
        text.data() + text.size(),
        value,
        std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

bool EqualsAsciiNoCase(std::string_view left, std::string_view right) {
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(), [](unsigned char a, unsigned char b) {
            const auto lower = [](unsigned char ch) {
                return ch >= 'A' && ch <= 'Z' ? static_cast<unsigned char>(ch - 'A' + 'a') : ch;
            };
            return lower(a) == lower(b);
        });
}

std::string ProtectWaitIfPlayerSelectors(std::string_view source) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 4> selectors{
        std::pair{ std::string_view{ "{myid}" }, std::string_view{ "__waitif_myid" } },
        std::pair{ std::string_view{ "{targetid}" }, std::string_view{ "__waitif_targetid" } },
        std::pair{ std::string_view{ "{closestid}" }, std::string_view{ "__waitif_closestid" } },
        std::pair{ std::string_view{ "{closestdriverid}" }, std::string_view{ "__waitif_closestdriverid" } },
    };

    std::string result;
    result.reserve(source.size());
    for (std::size_t offset = 0; offset < source.size();) {
        bool replaced = false;
        for (const auto& [token, sentinel] : selectors) {
            if (offset + token.size() <= source.size()
                && EqualsAsciiNoCase(source.substr(offset, token.size()), token)) {
                result.append(sentinel);
                offset += token.size();
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            result.push_back(source[offset++]);
        }
    }
    return result;
}

bool IsWaitIfPedValid(const CPed* ped) {
    return ped && CPools::ms_pPedPool
        && CPools::ms_pPedPool->IsObjectValid(const_cast<CPed*>(ped))
        && IsPedPointerValid(const_cast<CPed*>(ped));
}

} // namespace

class TagsModule::Impl::WaitIfResolver final : public waitif::Resolver {
public:
    WaitIfResolver(
        const Impl& owner,
        const EvaluationContext& context,
        WaitIfRuntimeState& state)
        : owner_(owner),
          context_(context),
          state_(state) {}

    void BeginEvaluation() {
        conditionContext_ = {};
        keyInputAvailable_ = owner_.binderModule_ && owner_.binderModule_->IsGameInputForeground();
        if (!keyInputAvailable_) {
            for (WaitIfKeyState& key : state_.keys) {
                key.initialized = false;
                key.previousDown = false;
                key.currentDown = false;
                key.pressed = false;
                key.released = false;
            }
            return;
        }
        if (state_.keySampleGeneration == owner_.tickGeneration_) {
            return;
        }
        state_.keySampleGeneration = owner_.tickGeneration_;
        for (WaitIfKeyState& key : state_.keys) {
            if (key.initialized) {
                key.previousDown = key.currentDown;
            }
            const SHORT sample = ::GetAsyncKeyState(static_cast<int>(key.keyCode));
            key.currentDown = (sample & 0x8000) != 0;
            if (!key.initialized) {
                key.initialized = true;
                key.previousDown = key.currentDown;
                key.pressed = false;
                key.released = false;
                continue;
            }
            const bool pressedSinceLastSample = (sample & 0x0001) != 0;
            key.pressed = (!key.previousDown && key.currentDown) || pressedSinceLastSample;
            key.released = (key.previousDown && !key.currentDown)
                || (!key.previousDown && !key.currentDown && pressedSinceLastSample);
        }
    }

    waitif::ResolveResult ResolveIdentifier(std::string_view name) override {
        SampApi* const api = Samp();
        if (name == "__waitif_myid" || name == "__waitif_targetid"
            || name == "__waitif_closestid" || name == "__waitif_closestdriverid") {
            if (!SampAvailable(api)) {
                return waitif::ResolveResult::Unavailable();
            }

            std::optional<std::string> text;
            if (name == "__waitif_myid") {
                const int id = api->Local_ID();
                return id >= 0 ? Number(id) : waitif::ResolveResult::Unavailable();
            }
            if (name == "__waitif_targetid") {
                text = owner_.ResolveBuiltinTargetIdTag(context_);
            } else if (name == "__waitif_closestid") {
                text = owner_.ResolveBuiltinClosestIdTag(context_);
            } else {
                text = owner_.ResolveBuiltinClosestDriverIdTag(context_);
            }
            const std::optional<double> id = text.has_value() ? ParseWaitIfNumber(*text) : std::nullopt;
            return id.has_value() ? Number(*id) : waitif::ResolveResult::Unavailable();
        }
        if (name == "chatinputactive") {
            return SampState(api, api ? api->is_chat_opened() : false);
        }
        if (name == "chatvisible") {
            return SampState(api, api ? api->IsChatVisible() : false);
        }
        if (name == "dialogactive") {
            return SampState(api, api ? api->isDialogActive() : false);
        }
        if (name == "scoreboardactive") {
            return SampState(api, api ? api->IsScoreboardOpen() : false);
        }
        if (name == "sampcursoractive") {
            return SampState(api, CheckCondition(ConditionId::SampCursorActive, api, &conditionContext_));
        }
        if (name == "windowscursoractive") {
            return Boolean(CheckCondition(ConditionId::WindowsCursorActive, api, &conditionContext_));
        }
        if (name == "gamehudvisible") {
            return Boolean(CheckCondition(ConditionId::GameHudVisible, api, &conditionContext_));
        }
        if (name == "cameralookingatplayer") {
            return Boolean(CheckCondition(ConditionId::CameraLookingAtPlayer, api, &conditionContext_));
        }
        if (name == "anyexplosionactive") {
            return Boolean(CheckCondition(ConditionId::AnyExplosionActive, api, &conditionContext_));
        }
        if (name == "serverconnected") {
            return SampState(api, api ? api->IsServerConnected() : false);
        }
        if (name == "passengerdrivebyactive") {
            return SampState(api, CheckCondition(ConditionId::PassengerDriveByOn, api, &conditionContext_));
        }
        if (name == "gtamenuactive") {
            return Boolean(CheckCondition(ConditionId::GtaMenuOpen, api, &conditionContext_));
        }
        return waitif::ResolveResult::Unknown("unknown waitif identifier: " + std::string(name));
    }

    waitif::ResolveResult ResolveFunction(
        std::string_view name,
        std::span<const waitif::Value> arguments) override {
        if (name == "waskeypressed" || name == "waskeyreleased" || name == "iskeydown") {
            return ResolveKey(name, arguments);
        }

        const std::string_view catalogName = name == "charlnanycar"
            ? std::string_view{ "charinanycar" }
            : name;
        const auto& definitions = WaitIfActionDefinitions();
        const auto definitionIt = std::find_if(
            definitions.begin(),
            definitions.end(),
            [&](const WaitIfActionDefinition& definition) {
                return EqualsAsciiNoCase(definition.name, catalogName);
            });
        if (definitionIt == definitions.end()
            || definitionIt->kind == WaitIfActionKind::State
            || definitionIt->kind == WaitIfActionKind::KeyBoolean) {
            return waitif::ResolveResult::Unknown("unknown waitif function: " + std::string(name));
        }

        const bool acceptsOptionalValue = definitionIt->kind == WaitIfActionKind::PlayerBooleanWithValue;
        if (arguments.size() != 1 && !(acceptsOptionalValue && arguments.size() == 2)) {
            return waitif::ResolveResult::InvalidArguments(
                std::string(name)
                + (acceptsOptionalValue
                    ? " expects a player selector and optional weapon ID"
                    : " expects one player selector"));
        }

        SampApi* const api = Samp();
        if (!SampAvailable(api)) {
            return waitif::ResolveResult::Unavailable();
        }

        bool selectorValid = false;
        const std::optional<int> playerId = ResolvePlayerId(arguments.front(), *api, selectorValid);
        if (!selectorValid) {
            return waitif::ResolveResult::InvalidArguments("invalid player selector");
        }
        if (name == "playerconnected") {
            return Boolean(playerId.has_value() && api->IsConnected(*playerId));
        }
        if (name == "playerstreamed") {
            return Boolean(
                playerId.has_value()
                && IsWaitIfPedValid(FindPlayerPedBySampId(*api, *playerId)));
        }
        if (!playerId.has_value()) {
            return waitif::ResolveResult::Unavailable();
        }

        const int id = *playerId;
        if (name == "getcharhealth" || name == "getchararmour") {
            const SampApi::HealthAndArmour stats = api->GetHealthAndArmour(id);
            if (!stats.valid) {
                return waitif::ResolveResult::Unavailable();
            }
            return Number(name == "getcharhealth" ? stats.health : stats.armour);
        }
        if (name == "getplayerping") {
            const std::optional<int> value = api->GetPlayerPing(id);
            return value.has_value() ? Number(*value) : waitif::ResolveResult::Unavailable();
        }
        if (name == "getplayerscore") {
            const std::optional<int> value = api->GetPlayerScore(id);
            return value.has_value() ? Number(*value) : waitif::ResolveResult::Unavailable();
        }
        if (name == "getchardistance") {
            const std::optional<std::string> text =
                owner_.ResolveBuiltinDistanceFunctionTag(std::to_string(id), context_);
            const std::optional<double> value = text.has_value() ? ParseWaitIfNumber(*text) : std::nullopt;
            return value.has_value() ? Number(*value) : waitif::ResolveResult::Unavailable();
        }

        const CPed* const ped = FindPlayerPedBySampId(*api, id);
        if (!IsWaitIfPedValid(ped)) {
            return waitif::ResolveResult::Unavailable();
        }
        CPed* const mutablePed = const_cast<CPed*>(ped);

        if (name == "playerdead") {
            return Boolean(plugin::Command<plugin::Commands::IS_CHAR_DEAD>(mutablePed));
        }
        if (name == "charinwater") {
            return Boolean(plugin::Command<plugin::Commands::IS_CHAR_IN_WATER>(mutablePed));
        }
        if (name == "charinair") {
            return Boolean(plugin::Command<plugin::Commands::IS_CHAR_IN_AIR>(mutablePed));
        }
        if (name == "charinanycar" || name == "charlnanycar") {
            return Boolean(plugin::Command<plugin::Commands::IS_CHAR_IN_ANY_CAR>(mutablePed));
        }
        if (name == "charonfoot") {
            return Boolean(plugin::Command<plugin::Commands::IS_CHAR_ON_FOOT>(mutablePed));
        }
        if (name == "charisdriver" || name == "charispassenger") {
            const CVehicle* const vehicle = ValidVehicle(ped);
            if (!vehicle) {
                return Boolean(false);
            }
            CVehicle* const mutableVehicle = const_cast<CVehicle*>(vehicle);
            return Boolean(
                name == "charisdriver"
                    ? mutableVehicle->IsDriver(const_cast<CPed*>(ped))
                    : mutableVehicle->IsPassenger(const_cast<CPed*>(ped)));
        }
        if (name == "charhasweapon") {
            int weapon = 0;
            plugin::Command<plugin::Commands::GET_CURRENT_CHAR_WEAPON>(mutablePed, &weapon);
            if (arguments.size() == 1) {
                return Boolean(weapon != 0);
            }
            const std::optional<int> expected = ToInteger(arguments[1], 0, 46);
            return expected.has_value()
                ? Boolean(weapon == *expected)
                : waitif::ResolveResult::InvalidArguments("CharHasWeapon weapon must be 0..46");
        }
        if (name == "getcharweapon") {
            int weapon = 0;
            plugin::Command<plugin::Commands::GET_CURRENT_CHAR_WEAPON>(mutablePed, &weapon);
            return Number(weapon);
        }
        if (name == "getcharskin") {
            return Number(ped->m_nModelIndex);
        }
        if (name == "getcharspeed") {
            float speed = 0.0f;
            plugin::Command<plugin::Commands::GET_CHAR_SPEED>(mutablePed, &speed);
            return Number(speed);
        }

        const CVehicle* const vehicle = ValidVehicle(ped);
        if (name == "charincar" || name == "charinboat" || name == "charinheli"
            || name == "charinplane" || name == "charinfakeplane" || name == "charintrain" || name == "charinbike"
            || name == "charinbicycle" || name == "charinquadbike" || name == "charinmonstertruck") {
            if (!vehicle) {
                return Boolean(false);
            }
            const int model = vehicle->m_nModelIndex;
            if (name == "charincar") {
                return Boolean(CModelInfo::IsCarModel(model));
            }
            if (name == "charinboat") {
                return Boolean(CModelInfo::IsBoatModel(model));
            }
            if (name == "charinheli") {
                return Boolean(CModelInfo::IsHeliModel(model));
            }
            if (name == "charinplane") {
                return Boolean(CModelInfo::IsPlaneModel(model));
            }
            if (name == "charinfakeplane") {
                return Boolean(CModelInfo::IsFakePlaneModel(model));
            }
            if (name == "charintrain") {
                return Boolean(CModelInfo::IsTrainModel(model));
            }
            if (name == "charinbike") {
                return Boolean(CModelInfo::IsBikeModel(model));
            }
            if (name == "charinbicycle") {
                return Boolean(CModelInfo::IsBmxModel(model));
            }
            if (name == "charinquadbike") {
                return Boolean(CModelInfo::IsQuadBikeModel(model));
            }
            return Boolean(CModelInfo::IsMonsterTruckModel(model));
        }
        if (name == "vehicleengineon" || name == "vehiclelightson" || name == "vehiclesirenon") {
            if (!vehicle) {
                return Boolean(false);
            }
            if (name == "vehicleengineon") {
                return Boolean(vehicle->bEngineOn);
            }
            if (name == "vehiclelightson") {
                return Boolean(vehicle->bLightsOn);
            }
            return Boolean(vehicle->bSirenOrAlarm);
        }
        if (name == "getcharvehiclehealth") {
            return vehicle ? Number(vehicle->m_fHealth) : waitif::ResolveResult::Unavailable();
        }
        if (name == "getcharvehiclemodel") {
            return vehicle ? Number(vehicle->m_nModelIndex) : waitif::ResolveResult::Unavailable();
        }
        if (name == "getcharvehicleid") {
            if (!vehicle) {
                return waitif::ResolveResult::Unavailable();
            }
            const std::optional<int> vehicleId = api->FindVehicleIdByPointer(vehicle);
            return vehicleId.has_value() ? Number(*vehicleId) : waitif::ResolveResult::Unavailable();
        }
        if (name == "getcharvehiclespeed") {
            if (!vehicle) {
                return waitif::ResolveResult::Unavailable();
            }
            float speed = 0.0f;
            plugin::Command<plugin::Commands::GET_CAR_SPEED>(const_cast<CVehicle*>(vehicle), &speed);
            return Number(speed);
        }

        return waitif::ResolveResult::Unknown("unknown waitif function: " + std::string(name));
    }

private:
    static waitif::ResolveResult Boolean(bool value) {
        return waitif::ResolveResult::Resolved(waitif::Value::Boolean(value));
    }

    static waitif::ResolveResult Number(double value) {
        return waitif::ResolveResult::Resolved(waitif::Value::Number(value));
    }

    static bool SampAvailable(const SampApi* api) {
        return api && api->sampModule() && api->isSupportedVersion();
    }

    static waitif::ResolveResult SampState(const SampApi* api, bool value) {
        return SampAvailable(api) ? Boolean(value) : waitif::ResolveResult::Unavailable();
    }

    SampApi* Samp() const {
        return context_.sampApi ? context_.sampApi : owner_.sampApi_;
    }

    static std::optional<int> ToInteger(const waitif::Value& value, int minimum, int maximum) {
        if (value.kind != waitif::ValueKind::Number || !std::isfinite(value.number)) {
            return std::nullopt;
        }
        const double rounded = std::round(value.number);
        if (rounded != value.number || rounded < minimum || rounded > maximum) {
            return std::nullopt;
        }
        return static_cast<int>(rounded);
    }

    static std::optional<int> ResolvePlayerId(
        const waitif::Value& selector,
        SampApi& api,
        bool& valid) {
        valid = true;
        if (selector.kind == waitif::ValueKind::Number) {
            const std::optional<int> id = ToInteger(selector, 0, kWaitIfMaxPlayerId);
            if (!id.has_value()) {
                valid = false;
            }
            return id;
        }
        if (selector.kind != waitif::ValueKind::String) {
            valid = false;
            return std::nullopt;
        }
        if (selector.string.empty()) {
            return std::nullopt;
        }

        int numericId = 0;
        const auto parsed = std::from_chars(
            selector.string.data(),
            selector.string.data() + selector.string.size(),
            numericId);
        if (parsed.ec == std::errc{} && parsed.ptr == selector.string.data() + selector.string.size()) {
            if (numericId < 0 || numericId > kWaitIfMaxPlayerId) {
                valid = false;
                return std::nullopt;
            }
            return numericId;
        }
        return api.GetIDByName(selector.string);
    }

    static const CVehicle* ValidVehicle(const CPed* ped) {
        return ped && IsVehiclePointerValid(ped->m_pVehicle) ? ped->m_pVehicle : nullptr;
    }

    waitif::ResolveResult ResolveKey(
        std::string_view name,
        std::span<const waitif::Value> arguments) {
        if (!keyInputAvailable_) {
            return waitif::ResolveResult::Unavailable();
        }
        if (arguments.size() != 1) {
            return waitif::ResolveResult::InvalidArguments(
                std::string(name) + " expects one virtual-key code");
        }
        const std::optional<int> keyCode = ToInteger(arguments.front(), 1, kWaitIfMaxVirtualKey);
        if (!keyCode.has_value()) {
            return waitif::ResolveResult::InvalidArguments("virtual-key code must be 1..255");
        }

        auto it = std::find_if(state_.keys.begin(), state_.keys.end(), [&](const WaitIfKeyState& key) {
            return key.keyCode == static_cast<unsigned int>(*keyCode);
        });
        if (it == state_.keys.end()) {
            WaitIfKeyState key;
            key.keyCode = static_cast<unsigned int>(*keyCode);
            const SHORT sample = ::GetAsyncKeyState(*keyCode);
            key.initialized = true;
            key.currentDown = (sample & 0x8000) != 0;
            key.previousDown = key.currentDown;
            state_.keys.push_back(key);
            it = std::prev(state_.keys.end());
        }

        if (name == "waskeypressed") {
            return Boolean(it->pressed);
        }
        if (name == "waskeyreleased") {
            return Boolean(it->released);
        }
        return Boolean(it->currentDown);
    }

    const Impl& owner_;
    EvaluationContext context_;
    WaitIfRuntimeState& state_;
    ConditionRuntimeContext conditionContext_{};
    bool keyInputAvailable_ = false;
};

const std::vector<TagsModule::Impl::WaitIfActionDefinition>& TagsModule::Impl::WaitIfActionDefinitions() {
    using Kind = WaitIfActionKind;
    static const std::vector<WaitIfActionDefinition> definitions{
        { "ChatinputActive", "Чат ввода открыт", "Chat input is open", Kind::State },
        { "ChatVisible", "Чат видим", "Chat is visible", Kind::State },
        { "DialogActive", "Диалог открыт", "Dialog is open", Kind::State },
        { "ScoreboardActive", "Таблица игроков открыта", "Scoreboard is open", Kind::State },
        { "SampCursorActive", "Курсор SA:MP активен", "SA:MP cursor is active", Kind::State },
        { "WindowsCursorActive", "Курсор Windows активен", "Windows cursor is active", Kind::State },
        { "GameHudVisible", "HUD игры видим", "Game HUD is visible", Kind::State },
        { "CameraLookingAtPlayer", "Камера смотрит на игрока", "Camera looks at player", Kind::State },
        { "AnyExplosionActive", "Есть активный взрыв", "An explosion is active", Kind::State },
        { "ServerConnected", "Подключение к серверу установлено", "Server is connected", Kind::State },
        { "PassengerDriveByActive", "Прицеливание с места пассажира", "Passenger drive-by aiming is active", Kind::State },
        { "GtaMenuActive", "Меню GTA открыто", "GTA menu is open", Kind::State },
        { "PlayerConnected", "Игрок подключён", "Player is connected", Kind::PlayerBoolean },
        { "PlayerStreamed", "Игрок застримлен", "Player is streamed", Kind::PlayerBoolean },
        { "PlayerDead", "Игрок умер", "Player is dead", Kind::PlayerBoolean },
        { "CharInWater", "Игрок в воде", "Player is in water", Kind::PlayerBoolean },
        { "CharInAir", "Игрок в воздухе", "Player is in air", Kind::PlayerBoolean },
        { "CharInAnyCar", "Игрок в любом транспорте", "Player is in any vehicle", Kind::PlayerBoolean },
        { "CharOnFoot", "Игрок пешком", "Player is on foot", Kind::PlayerBoolean },
        { "CharIsDriver", "Игрок — водитель", "Player is the driver", Kind::PlayerBoolean },
        { "CharIsPassenger", "Игрок — пассажир", "Player is a passenger", Kind::PlayerBoolean },
        { "CharHasWeapon", "У игрока выбран тип оружия", "Player has selected weapon type", Kind::PlayerBooleanWithValue, "==", "24" },
        { "CharInCar", "Игрок в автомобиле", "Player is in a car", Kind::PlayerBoolean },
        { "CharInBoat", "Игрок в лодке", "Player is in a boat", Kind::PlayerBoolean },
        { "CharInHeli", "Игрок в вертолёте", "Player is in a helicopter", Kind::PlayerBoolean },
        { "CharInPlane", "Игрок в самолёте", "Player is in a plane", Kind::PlayerBoolean },
        { "CharInFakePlane", "Игрок в псевдосамолёте", "Player is in a fake plane", Kind::PlayerBoolean },
        { "CharInTrain", "Игрок в поезде", "Player is in a train", Kind::PlayerBoolean },
        { "CharInBike", "Игрок на мотоцикле", "Player is on a bike", Kind::PlayerBoolean },
        { "CharInBicycle", "Игрок на велосипеде", "Player is on a bicycle", Kind::PlayerBoolean },
        { "CharInQuadBike", "Игрок на квадроцикле", "Player is on a quad bike", Kind::PlayerBoolean },
        { "CharInMonsterTruck", "Игрок в монстр-траке", "Player is in a monster truck", Kind::PlayerBoolean },
        { "VehicleEngineOn", "Двигатель транспорта включён", "Vehicle engine is on", Kind::PlayerBoolean },
        { "VehicleLightsOn", "Фары транспорта включены", "Vehicle lights are on", Kind::PlayerBoolean },
        { "VehicleSirenOn", "Сирена транспорта включена", "Vehicle siren is on", Kind::PlayerBoolean },
        { "getCharHealth", "Здоровье игрока", "Player health", Kind::PlayerNumber, "<", "100" },
        { "getCharArmour", "Броня игрока", "Player armour", Kind::PlayerNumber, "<", "100" },
        { "getCharWeapon", "Текущее оружие игрока", "Player weapon", Kind::PlayerNumber, "==", "24" },
        { "getCharSkin", "Скин игрока", "Player skin", Kind::PlayerNumber, "==", "0" },
        { "getCharSpeed", "Скорость игрока", "Player speed", Kind::PlayerNumber, ">", "0" },
        { "getCharDistance", "Дистанция до игрока", "Distance to player", Kind::PlayerNumber, "<", "10" },
        { "getPlayerPing", "Пинг игрока", "Player ping", Kind::PlayerNumber, ">", "100" },
        { "getPlayerScore", "Очки игрока", "Player score", Kind::PlayerNumber, ">=", "1" },
        { "getCharVehicleHealth", "Здоровье транспорта игрока", "Player vehicle health", Kind::PlayerNumber, "<", "1000" },
        { "getCharVehicleModel", "Модель транспорта игрока", "Player vehicle model", Kind::PlayerNumber, "==", "411" },
        { "getCharVehicleId", "SA:MP ID транспорта игрока", "Player vehicle SA:MP ID", Kind::PlayerNumber, "==", "0" },
        { "getCharVehicleSpeed", "Скорость транспорта игрока", "Player vehicle speed", Kind::PlayerNumber, ">", "0" },
        { "wasKeyPressed", "Клавиша нажата", "Key was pressed", Kind::KeyBoolean },
        { "wasKeyReleased", "Клавиша отпущена", "Key was released", Kind::KeyBoolean },
        { "isKeyDown", "Клавиша удерживается", "Key is held", Kind::KeyBoolean },
    };
    return definitions;
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinWaitIfFunctionTag(
    std::string_view rawParam,
    const EvaluationContext& context,
    int depth) const {
    if (!context.allowSideEffects || context.runningBindRuntimeId == 0 || !binderModule_) {
        return std::string();
    }

    const std::uint64_t runtimeId = context.runningBindRuntimeId;
    auto stateIt = std::find_if(waitIfRuntimeStates_.begin(), waitIfRuntimeStates_.end(), [&](const WaitIfRuntimeState& state) {
        return state.runtimeId == runtimeId && state.rawExpression == rawParam;
    });
    if (stateIt == waitIfRuntimeStates_.end()) {
        WaitIfRuntimeState state;
        state.runtimeId = runtimeId;
        state.rawExpression.assign(rawParam);
        state.preparedExpression = ProtectWaitIfPlayerSelectors(rawParam);
        state.hasNestedTags =
            state.preparedExpression.find('{') != std::string::npos
            || state.preparedExpression.find('[') != std::string::npos;
        state.startedAtMs = GetTickCount64();
        waitIfRuntimeStates_.push_back(std::move(state));
        stateIt = std::prev(waitIfRuntimeStates_.end());
    }

    WaitIfRuntimeState& state = *stateIt;
    if (state.completed) {
        MarkCurrentDispatchSkipIfEmpty(runtimeId);
        return std::string();
    }

    const std::uint64_t now = GetTickCount64();
    if (state.nextEvaluationAtMs != 0 && now < state.nextEvaluationAtMs) {
        MarkCurrentDispatchBlocked(runtimeId);
        return std::string();
    }
    state.nextEvaluationAtMs = now + kWaitIfPollIntervalMs;

    EvaluationContext pureContext = context;
    pureContext.allowSideEffects = false;
    pureContext.runningBindRuntimeId = 0;
    std::string expandedStorage;
    std::string_view expanded = state.preparedExpression;
    if (state.hasNestedTags) {
        expandedStorage = ExpandTextRecursive(state.preparedExpression, pureContext, depth + 1);
        expanded = expandedStorage;
    }
    if (!state.compiled.has_value() || state.expandedExpression != expanded) {
        state.expandedExpression.assign(expanded);
        std::string error;
        state.compiled = waitif::CompiledExpression::Compile(expanded, error);
        if (!state.compiled.has_value()) {
            const std::string message = "waitif: " + error;
            NotifyTagError(message);
            debuglog::WriteError(
                "[tags][waitif] compile failed runtime=%llu expression=%s error=%s",
                static_cast<unsigned long long>(runtimeId),
                state.expandedExpression.c_str(),
                error.c_str());
            MarkCurrentDispatchSkipped(runtimeId);
            waitIfRuntimeStates_.erase(stateIt);
            return std::string();
        }
    }

    WaitIfResolver resolver(*this, context, state);
    resolver.BeginEvaluation();
    const waitif::EvaluationResult result = state.compiled->Evaluate(resolver);

    if (result.status == waitif::EvaluationStatus::Error) {
        const std::string message = "waitif: " + result.error;
        NotifyTagError(message);
        debuglog::WriteError(
            "[tags][waitif] evaluation failed runtime=%llu expression=%s error=%s",
            static_cast<unsigned long long>(runtimeId),
            state.expandedExpression.c_str(),
            result.error.c_str());
        MarkCurrentDispatchSkipped(runtimeId);
        waitIfRuntimeStates_.erase(stateIt);
        return std::string();
    }

    if (result.status == waitif::EvaluationStatus::True) {
        const std::uint64_t elapsedMs = now - state.startedAtMs;
        debuglog::WriteInfo(
            "[tags][waitif] completed runtime=%llu elapsedMs=%llu expression=%s",
            static_cast<unsigned long long>(runtimeId),
            static_cast<unsigned long long>(elapsedMs),
            state.expandedExpression.c_str());
        state.completed = true;
        MarkCurrentDispatchSkipIfEmpty(runtimeId);
        return std::string();
    }

    if (!state.waitingLogged) {
        state.waitingLogged = true;
        debuglog::WriteInfo(
            "[tags][waitif] waiting runtime=%llu expression=%s",
            static_cast<unsigned long long>(runtimeId),
            state.expandedExpression.c_str());
    }
    MarkCurrentDispatchBlocked(runtimeId);
    return std::string();
}
