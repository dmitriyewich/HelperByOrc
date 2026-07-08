#include "tags_module_impl.h"
#include "tags_module_detail.h"

#include <cmath>
#include <cwchar>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <vector>

namespace {
struct LocalVehicleContext {
    CPed* playerPed = nullptr;
    CVehicle* vehicle = nullptr;
};

struct MyCarOccupant {
    CPed* ped = nullptr;
    int id = -1;
    bool passenger = false;
    bool localPlayer = false;
};

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

const char* CityNameRu(eLevelName level) {
    switch (level) {
    case LEVEL_NAME_LOS_SANTOS:
        return "Лос-Сантос";
    case LEVEL_NAME_SAN_FIERRO:
        return "Сан-Фиерро";
    case LEVEL_NAME_LAS_VENTURAS:
        return "Лас-Вентурас";
    case LEVEL_NAME_COUNTRY_SIDE:
    default:
        return "Округ";
    }
}

const char* CityNameEn(eLevelName level) {
    switch (level) {
    case LEVEL_NAME_LOS_SANTOS:
        return "Los Santos";
    case LEVEL_NAME_SAN_FIERRO:
        return "San Fierro";
    case LEVEL_NAME_LAS_VENTURAS:
        return "Las Venturas";
    case LEVEL_NAME_COUNTRY_SIDE:
    default:
        return "Countryside";
    }
}

eLevelName ResolveLocalPlayerCityLevel() {
    const std::optional<CVector> position = ResolveLocalPlayerPosition();
    if (!position) {
        return LEVEL_NAME_COUNTRY_SIDE;
    }

    return CTheZones::GetLevelFromPosition(&*position);
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    const int utf8Length = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (utf8Length <= 0) {
        return {};
    }

    std::string utf8(static_cast<std::size_t>(utf8Length), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            0,
            text.data(),
            static_cast<int>(text.size()),
            utf8.data(),
            utf8Length,
            nullptr,
            nullptr) <= 0) {
        return {};
    }
    return utf8;
}

std::string ReadClipboardUtf8Text() {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(nullptr)) {
        return {};
    }

    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (!handle) {
        CloseClipboard();
        return {};
    }

    const auto* wideText = static_cast<const wchar_t*>(GlobalLock(handle));
    if (!wideText) {
        CloseClipboard();
        return {};
    }

    std::size_t length = 0;
    while (length < kClipboardTagMaxLength && wideText[length] != L'\0') {
        ++length;
    }

    std::string result = WideToUtf8(std::wstring_view(wideText, length));
    GlobalUnlock(handle);
    CloseClipboard();
    return result;
}

std::optional<LocalVehicleContext> ResolveLocalVehicleContext() {
    CPed* const playerPed = FindPlayerPed();
    if (!playerPed) {
        return std::nullopt;
    }

    if (CVehicle* const vehicle = FindPlayerVehicle(-1, false); IsVehiclePointerValid(vehicle)) {
        return LocalVehicleContext{playerPed, vehicle};
    }

    if (IsVehiclePointerValid(playerPed->m_pVehicle)) {
        return LocalVehicleContext{playerPed, playerPed->m_pVehicle};
    }

    return std::nullopt;
}

std::optional<int> ResolveSampIdForOccupant(SampApi& sampApi, CPed* ped, const CPed* localPed) {
    if (!ped || !IsPedPointerValid(ped)) {
        return std::nullopt;
    }

    if (ped == localPed) {
        const int localId = sampApi.Local_ID();
        if (localId >= 0 && sampApi.IsConnected(localId)) {
            return localId;
        }
    }

    const auto [resolved, id] = sampApi.getPedID(ped);
    if (!resolved || id < 0 || id > 1003 || !sampApi.IsConnected(id)) {
        return std::nullopt;
    }
    return id;
}

std::vector<MyCarOccupant> CollectMyCarOccupants(SampApi& sampApi, const LocalVehicleContext& vehicleContext) {
    std::vector<MyCarOccupant> occupants;
    occupants.reserve(9);

    const auto appendOccupant = [&](CPed* ped, bool passenger) {
        if (!ped) {
            return;
        }
        if (std::find_if(occupants.begin(), occupants.end(), [ped](const MyCarOccupant& occupant) {
                return occupant.ped == ped;
            }) != occupants.end()) {
            return;
        }

        const std::optional<int> id = ResolveSampIdForOccupant(sampApi, ped, vehicleContext.playerPed);
        if (!id.has_value()) {
            return;
        }
        occupants.push_back(MyCarOccupant{
            ped,
            *id,
            passenger,
            ped == vehicleContext.playerPed,
        });
    };

    appendOccupant(vehicleContext.vehicle->m_pDriver, false);
    for (CPed* const passenger : vehicleContext.vehicle->m_apPassengers) {
        appendOccupant(passenger, true);
    }

    return occupants;
}

std::optional<float> ResolveVehicleSpeedKmh(const CVehicle& vehicle) {
    const CVector speed = vehicle.m_vecMoveSpeed;
    const float magnitude = std::sqrt(speed.x * speed.x + speed.y * speed.y + speed.z * speed.z);
    if (!std::isfinite(magnitude)) {
        return std::nullopt;
    }
    return magnitude * 180.0f;
}
}

TagsModule::Impl::MyCarSnapshotCache& TagsModule::Impl::QueryMyCarSnapshot(
    const EvaluationContext& context,
    bool requireOccupants) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    const bool sampReady = sampApi && sampApi->sampModule() && sampApi->isSupportedVersion();
    const int localId = sampReady ? sampApi->Local_ID() : -1;

    const std::optional<LocalVehicleContext> vehicleContext = ResolveLocalVehicleContext();
    const std::uintptr_t localPedAddress =
        vehicleContext ? reinterpret_cast<std::uintptr_t>(vehicleContext->playerPed) : 0;
    const std::uintptr_t vehicleAddress =
        vehicleContext ? reinterpret_cast<std::uintptr_t>(vehicleContext->vehicle) : 0;
    const std::uint64_t now = GetTickCount64();

    if (myCarSnapshotCache_.valid
        && myCarSnapshotCache_.localPed == localPedAddress
        && myCarSnapshotCache_.vehicle == vehicleAddress
        && myCarSnapshotCache_.localId == localId
        && myCarSnapshotCache_.sampReady == sampReady
        && (!requireOccupants || myCarSnapshotCache_.occupantsResolved)
        && now >= myCarSnapshotCache_.updatedAtMs
        && now - myCarSnapshotCache_.updatedAtMs <= kMyCarSnapshotCacheTtlMs) {
        RecordMyCarSnapshotPerf(
            true,
            requireOccupants,
            myCarSnapshotCache_.hasVehicle,
            myCarSnapshotCache_.sampReady,
            myCarSnapshotCache_.occupants.size(),
            0);
        return myCarSnapshotCache_;
    }

    const std::uint64_t queryStartedAtMs = now;
    MyCarSnapshotCache snapshot;
    snapshot.lastSlowLogAtMs = myCarSnapshotCache_.lastSlowLogAtMs;
    snapshot.localPed = localPedAddress;
    snapshot.vehicle = vehicleAddress;
    snapshot.localId = localId;
    snapshot.sampReady = sampReady;
    snapshot.valid = true;
    snapshot.occupantsResolved = requireOccupants;

    if (vehicleContext) {
        snapshot.hasVehicle = true;
        snapshot.health = FormatWholeStatValue(vehicleContext->vehicle->m_fHealth);

        const std::optional<float> speed = ResolveVehicleSpeedKmh(*vehicleContext->vehicle);
        snapshot.speed = speed.has_value() ? FormatWholeStatValue(*speed) : std::string();

        if (requireOccupants && sampReady && localId >= 0) {
            const std::vector<MyCarOccupant> occupants = CollectMyCarOccupants(*sampApi, *vehicleContext);
            snapshot.occupants.reserve(occupants.size());
            for (const MyCarOccupant& occupant : occupants) {
                snapshot.occupants.push_back(MyCarSnapshotOccupant{
                    occupant.id,
                    occupant.passenger,
                    occupant.localPlayer,
                });
            }
        }
    }

    snapshot.updatedAtMs = GetTickCount64();
    const std::uint64_t elapsedMs = snapshot.updatedAtMs - queryStartedAtMs;
    if (elapsedMs >= kMyCarSnapshotSlowQueryLogMs
        && (snapshot.lastSlowLogAtMs == 0
            || snapshot.updatedAtMs - snapshot.lastSlowLogAtMs >= kMyCarSnapshotSlowQueryLogThrottleMs)) {
        snapshot.lastSlowLogAtMs = snapshot.updatedAtMs;
        debuglog::WriteInfo(
            "[tags][mycar] slow snapshot elapsed=%llums vehicle=%d occupants=%zu resolved=%d",
            static_cast<unsigned long long>(elapsedMs),
            snapshot.hasVehicle ? 1 : 0,
            snapshot.occupants.size(),
            snapshot.occupantsResolved ? 1 : 0);
    }

    RecordMyCarSnapshotPerf(
        false,
        requireOccupants,
        snapshot.hasVehicle,
        snapshot.sampReady,
        snapshot.occupants.size(),
        elapsedMs);

    myCarSnapshotCache_ = std::move(snapshot);
    return myCarSnapshotCache_;
}

void TagsModule::Impl::ResolveMyCarOccupantNames(
    MyCarSnapshotCache& snapshot,
    const EvaluationContext& context) const {
    const std::uint64_t startedAtMs = GetTickCount64();
    const std::size_t requested = snapshot.occupants.size();
    std::size_t resolved = 0;

    for (MyCarSnapshotOccupant& occupant : snapshot.occupants) {
        if (occupant.nickResolved) {
            continue;
        }

        occupant.nick = ResolvePlayerNickById(occupant.id, context);
        occupant.name = ExtractName(occupant.nick);
        occupant.surname = ExtractSurname(occupant.nick);
        occupant.rpNick = MakeRpNick(occupant.nick);
        occupant.nickResolved = true;
        ++resolved;
    }

    RecordMyCarNameResolvePerf(requested, resolved, GetTickCount64() - startedAtMs);
}

void TagsModule::Impl::RecordMyCarSnapshotPerf(
    bool cacheHit,
    bool requireOccupants,
    bool hasVehicle,
    bool sampReady,
    std::size_t occupants,
    std::uint64_t elapsedMs) const {
    const std::uint64_t now = GetTickCount64();
    MyCarSnapshotPerfStats& stats = myCarSnapshotPerfStats_;
    if (stats.windowStartMs == 0 || now < stats.windowStartMs) {
        stats.windowStartMs = now;
    }

    ++stats.requests;
    if (cacheHit) {
        ++stats.cacheHits;
    } else {
        ++stats.rebuilds;
        stats.totalRebuildMs += elapsedMs;
        stats.maxRebuildMs = std::max(stats.maxRebuildMs, elapsedMs);
        if (requireOccupants) {
            ++stats.occupantRebuilds;
        }
    }

    if (requireOccupants) {
        ++stats.occupantRequests;
        if (!sampReady) {
            ++stats.noSamp;
        }
    }
    if (!hasVehicle) {
        ++stats.noVehicle;
    }
    stats.maxOccupants = std::max(stats.maxOccupants, occupants);

    MaybeLogMyCarPerf(now);
}

void TagsModule::Impl::RecordMyCarNameResolvePerf(
    std::size_t requested,
    std::size_t resolved,
    std::uint64_t elapsedMs) const {
    const std::uint64_t now = GetTickCount64();
    MyCarSnapshotPerfStats& stats = myCarSnapshotPerfStats_;
    if (stats.windowStartMs == 0 || now < stats.windowStartMs) {
        stats.windowStartMs = now;
    }

    stats.nameRequests += requested;
    stats.nameResolved += resolved;
    stats.nameCacheHits += requested >= resolved ? requested - resolved : 0;
    stats.totalNameMs += elapsedMs;
    stats.maxNameMs = std::max(stats.maxNameMs, elapsedMs);

    MaybeLogMyCarPerf(now);
}

void TagsModule::Impl::MaybeLogMyCarPerf(std::uint64_t nowMs) const {
    MyCarSnapshotPerfStats& stats = myCarSnapshotPerfStats_;
    if (stats.windowStartMs == 0 || nowMs < stats.windowStartMs) {
        stats.windowStartMs = nowMs;
        return;
    }

    const std::uint64_t windowMs = nowMs - stats.windowStartMs;
    if (windowMs < kMyCarPerfTelemetryWindowMs) {
        return;
    }

    if (stats.requests == 0 && stats.nameRequests == 0) {
        stats = MyCarSnapshotPerfStats{};
        stats.windowStartMs = nowMs;
        return;
    }

    const double avgRebuildMs =
        stats.rebuilds > 0 ? static_cast<double>(stats.totalRebuildMs) / static_cast<double>(stats.rebuilds) : 0.0;
    const double avgNameMs =
        stats.nameRequests > 0
            ? static_cast<double>(stats.totalNameMs) / static_cast<double>(stats.nameRequests)
            : 0.0;

    debuglog::WriteInfo(
        "[tags][mycar][perf] window=%llums requests=%llu hits=%llu rebuilds=%llu occupantReq=%llu "
        "occupantRebuilds=%llu noVehicle=%llu noSamp=%llu avgRebuild=%.2fms maxRebuild=%llums "
        "maxOccupants=%zu nameReq=%llu nameCached=%llu nameResolved=%llu avgName=%.2fms maxName=%llums",
        static_cast<unsigned long long>(windowMs),
        static_cast<unsigned long long>(stats.requests),
        static_cast<unsigned long long>(stats.cacheHits),
        static_cast<unsigned long long>(stats.rebuilds),
        static_cast<unsigned long long>(stats.occupantRequests),
        static_cast<unsigned long long>(stats.occupantRebuilds),
        static_cast<unsigned long long>(stats.noVehicle),
        static_cast<unsigned long long>(stats.noSamp),
        avgRebuildMs,
        static_cast<unsigned long long>(stats.maxRebuildMs),
        stats.maxOccupants,
        static_cast<unsigned long long>(stats.nameRequests),
        static_cast<unsigned long long>(stats.nameCacheHits),
        static_cast<unsigned long long>(stats.nameResolved),
        avgNameMs,
        static_cast<unsigned long long>(stats.maxNameMs));

    stats = MyCarSnapshotPerfStats{};
    stats.windowStartMs = nowMs;
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

std::optional<std::string> TagsModule::Impl::ResolveBuiltinCityTag(const EvaluationContext&) const {
    return std::string(CityNameRu(ResolveLocalPlayerCityLevel()));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinCityEnTag(const EvaluationContext&) const {
    return std::string(CityNameEn(ResolveLocalPlayerCityLevel()));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinClipboardTag(const EvaluationContext&) const {
    const std::uint64_t now = GetTickCount64();
    if (clipboardCache_.valid && now - clipboardCache_.updatedAtMs <= kClipboardCacheTtlMs) {
        return clipboardCache_.text;
    }

    clipboardCache_.text = ReadClipboardUtf8Text();
    clipboardCache_.updatedAtMs = now;
    clipboardCache_.valid = true;
    return clipboardCache_.text;
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinMyColorTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string("{FFFFFF}");
    }

    const int localId = sampApi->Local_ID();
    if (localId < 0 || !sampApi->IsConnected(localId)) {
        return std::string("{FFFFFF}");
    }

    const std::optional<std::uint32_t> color = sampApi->GetPlayerColor(localId);
    return color.has_value() ? FormatSampColorTag(*color) : std::string("{FFFFFF}");
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinMyCarHealthTag(const EvaluationContext& context) const {
    const MyCarSnapshotCache& snapshot = QueryMyCarSnapshot(context, false);
    if (!snapshot.hasVehicle) {
        return std::string();
    }
    return snapshot.health;
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinMyCarSpeedTag(const EvaluationContext& context) const {
    const MyCarSnapshotCache& snapshot = QueryMyCarSnapshot(context, false);
    if (!snapshot.hasVehicle) {
        return std::string();
    }
    return snapshot.speed;
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinMyCarOccupantsTag(
    MyCarOccupantScope scope,
    MyCarOccupantField field,
    const EvaluationContext& context) const {
    MyCarSnapshotCache& snapshot = QueryMyCarSnapshot(context, true);
    std::vector<std::string> values;
    values.reserve(snapshot.occupants.size());
    const auto shouldIncludeOccupant = [scope](const MyCarSnapshotOccupant& occupant) {
        switch (scope) {
        case MyCarOccupantScope::Players:
            return !occupant.localPlayer;
        case MyCarOccupantScope::Passengers:
            return occupant.passenger && !occupant.localPlayer;
        case MyCarOccupantScope::AllPlayers:
            return true;
        case MyCarOccupantScope::AllPassengers:
            return occupant.passenger;
        default:
            return false;
        }
    };

    if (!snapshot.hasVehicle || !snapshot.sampReady) {
        return std::string();
    }

    if (field != MyCarOccupantField::Id) {
        ResolveMyCarOccupantNames(snapshot, context);
    }

    for (const MyCarSnapshotOccupant& occupant : snapshot.occupants) {
        if (!shouldIncludeOccupant(occupant)) {
            continue;
        }

        std::string value;
        if (field == MyCarOccupantField::Id) {
            value = std::to_string(occupant.id);
        } else {
            switch (field) {
            case MyCarOccupantField::Name:
                value = occupant.name;
                break;
            case MyCarOccupantField::Surname:
                value = occupant.surname;
                break;
            case MyCarOccupantField::Nick:
                value = occupant.nick;
                break;
            case MyCarOccupantField::RpNick:
                value = occupant.rpNick;
                break;
            default:
                value.clear();
                break;
            }
        }

        if (!value.empty()) {
            values.push_back(std::move(value));
        }
    }

    std::string result;
    for (const std::string& value : values) {
        if (!result.empty()) {
            result += ", ";
        }
        result += value;
    }
    return result;
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
