#include "tags_module_impl.h"
#include "tags_module_detail.h"

namespace {

double TagResolverPerfNowMs() {
    static const double s_invFrequencyMs = [] {
        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
            return 0.0;
        }
        return 1000.0 / static_cast<double>(frequency.QuadPart);
    }();

    if (s_invFrequencyMs <= 0.0) {
        return static_cast<double>(GetTickCount64());
    }

    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) * s_invFrequencyMs;
}

bool IsClosestCandidatePedValid(CPed* ped) {
    if (!ped || !CPools::ms_pPedPool || !CPools::ms_pPedPool->IsObjectValid(ped)) {
        return false;
    }
    return IsPedPointerValid(ped);
}

bool IsFinitePosition(const CVector& position) {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

} // namespace

TagsModule::Impl::OwnedEvaluationContext TagsModule::Impl::MakeOwnedContext(const EvaluationContext& context, SampApi* fallbackSampApi) {
    OwnedEvaluationContext owned;
    owned.sampApi = context.sampApi ? context.sampApi : fallbackSampApi;
    owned.activationSource = std::string(context.activationSource);
    owned.activationText = std::string(context.activationText);
    owned.bindCommand = std::string(context.bindCommand);
    owned.allowSideEffects = context.allowSideEffects;
    owned.runningBindRuntimeId = context.runningBindRuntimeId;
    return owned;
}

TagsModule::Impl::EvaluationContext TagsModule::Impl::MakeViewContext(const OwnedEvaluationContext& context) {
    return EvaluationContext{
        context.sampApi,
        context.activationSource,
        context.activationText,
        context.bindCommand,
        context.allowSideEffects,
        context.runningBindRuntimeId,
    };
}

void TagsModule::Impl::PushContext(const EvaluationContext& context) const {
    g_activeContextStack.push_back(MakeOwnedContext(context, sampApi_));
}

void TagsModule::Impl::PopContext() const {
    if (!g_activeContextStack.empty()) {
        g_activeContextStack.pop_back();
    }
}

std::optional<int> TagsModule::Impl::ConsumePendingBindDelayOverride(std::uint64_t runtimeId) const {
    if (runtimeId == 0) {
        return std::nullopt;
    }

    const auto it = std::find_if(
        pendingBindDelayOverrides_.begin(),
        pendingBindDelayOverrides_.end(),
        [&](const PendingBindDelayOverride& entry) { return entry.runtimeId == runtimeId; });
    if (it == pendingBindDelayOverrides_.end()) {
        return std::nullopt;
    }

    const int delayMs = it->delayMs;
    pendingBindDelayOverrides_.erase(it);
    return delayMs;
}

bool TagsModule::Impl::ConsumeCurrentDispatchBlocked(std::uint64_t runtimeId) const {
    if (runtimeId == 0) {
        return false;
    }

    const auto it = std::find(blockedCurrentDispatchRuntimes_.begin(), blockedCurrentDispatchRuntimes_.end(), runtimeId);
    if (it == blockedCurrentDispatchRuntimes_.end()) {
        return false;
    }

    blockedCurrentDispatchRuntimes_.erase(it);
    return true;
}

void TagsModule::Impl::Tick(bool sampReady) {
    const std::uint64_t now = GetTickCount64();
    if (++tickGeneration_ == 0) {
        tickGeneration_ = 1;
    }
    codevars::Runtime::Instance().Tick(sampReady);
    MaybeLogExternalTagPerf(now);
    UpdateTargetTracker();
    ProcessPendingDialogWaits();
    ProcessPendingArzDialogQueryWaits();
    if (!activeVirtualKeyHolds_.empty()) {
        for (auto it = activeVirtualKeyHolds_.begin(); it != activeVirtualKeyHolds_.end();) {
            ActiveVirtualKeyHold& hold = *it;
            if (!hold.pressed && now >= hold.pressAtMs) {
                hold.pressed = SendVirtualKeyEvent(hold.keyCode, false);
                if (!hold.pressed) {
                    ClearPendingKeyHoldWaitsByKeyCode(hold.keyCode);
                    it = activeVirtualKeyHolds_.erase(it);
                    continue;
                }
            }

            if (hold.pressed && now >= hold.releaseAtMs) {
                ReleaseVirtualKeyHold(hold);
                it = activeVirtualKeyHolds_.erase(it);
                continue;
            }

            ++it;
        }
    }

    ProcessPendingKeyHoldWaits();
}

void TagsModule::Impl::QueuePendingDialogWait(
    std::uint64_t runtimeId,
    PendingDialogWaitKind kind,
    std::uint64_t deadlineAtMs,
    int expectedDialogId) {
    if (runtimeId == 0) {
        return;
    }

    if (auto it = std::find_if(
            pendingDialogWaits_.begin(),
            pendingDialogWaits_.end(),
            [&](const PendingDialogWait& wait) { return wait.runtimeId == runtimeId; });
        it != pendingDialogWaits_.end()) {
        it->kind = kind;
        it->deadlineAtMs = deadlineAtMs;
        it->expectedDialogId = expectedDialogId;
        return;
    }

    pendingDialogWaits_.push_back(PendingDialogWait{ runtimeId, kind, deadlineAtMs, expectedDialogId });
}

void TagsModule::Impl::ClearPendingDialogWait(std::uint64_t runtimeId) {
    if (runtimeId == 0) {
        return;
    }

    pendingDialogWaits_.erase(
        std::remove_if(pendingDialogWaits_.begin(), pendingDialogWaits_.end(), [&](const PendingDialogWait& wait) {
            return wait.runtimeId == runtimeId;
        }),
        pendingDialogWaits_.end());
}

void TagsModule::Impl::QueuePendingKeyHoldWait(std::uint64_t runtimeId, unsigned int keyCode, std::uint64_t releaseAtMs) const {
    if (runtimeId == 0 || keyCode == 0 || releaseAtMs == 0) {
        return;
    }

    pendingKeyHoldWaits_.push_back(PendingKeyHoldWait{ runtimeId, keyCode, releaseAtMs });
}

void TagsModule::Impl::ClearPendingKeyHoldWaitsByKeyCode(unsigned int keyCode) const {
    if (keyCode == 0 || pendingKeyHoldWaits_.empty()) {
        return;
    }

    std::vector<std::uint64_t> runtimesToResume;
    for (const PendingKeyHoldWait& wait : pendingKeyHoldWaits_) {
        if (wait.keyCode == keyCode && wait.runtimeId != 0) {
            runtimesToResume.push_back(wait.runtimeId);
        }
    }

    pendingKeyHoldWaits_.erase(
        std::remove_if(
            pendingKeyHoldWaits_.begin(),
            pendingKeyHoldWaits_.end(),
            [&](const PendingKeyHoldWait& wait) { return wait.keyCode == keyCode; }),
        pendingKeyHoldWaits_.end());

    if (!binderModule_) {
        return;
    }

    for (const std::uint64_t runtimeId : runtimesToResume) {
        if (runtimeId == 0
            || !binderModule_->IsRuntimeActive(runtimeId)
            || !binderModule_->IsRuntimePaused(runtimeId)
            || HasPendingKeyHoldWait(runtimeId)
            || HasPendingDialogWait(runtimeId)) {
            continue;
        }

        binderModule_->ResumeRuntime(runtimeId);
    }
}

bool TagsModule::Impl::HasPendingDialogWait(std::uint64_t runtimeId) const {
    return runtimeId != 0
        && std::any_of(
            pendingDialogWaits_.begin(),
            pendingDialogWaits_.end(),
            [&](const PendingDialogWait& wait) { return wait.runtimeId == runtimeId; });
}

bool TagsModule::Impl::HasPendingKeyHoldWait(std::uint64_t runtimeId) const {
    return runtimeId != 0
        && std::any_of(
            pendingKeyHoldWaits_.begin(),
            pendingKeyHoldWaits_.end(),
            [&](const PendingKeyHoldWait& wait) { return wait.runtimeId == runtimeId; });
}

void TagsModule::Impl::QueuePendingArzDialogQueryWait(
    std::uint64_t runtimeId,
    PendingArzDialogQueryKind kind,
    std::uint64_t deadlineAtMs) const {
    if (runtimeId == 0) {
        return;
    }

    if (auto it = std::find_if(
            pendingArzDialogQueryWaits_.begin(),
            pendingArzDialogQueryWaits_.end(),
            [&](const PendingArzDialogQueryWait& wait) {
                return wait.runtimeId == runtimeId && wait.kind == kind;
            });
        it != pendingArzDialogQueryWaits_.end()) {
        it->deadlineAtMs = deadlineAtMs;
        return;
    }

    pendingArzDialogQueryWaits_.push_back(PendingArzDialogQueryWait{ runtimeId, kind, deadlineAtMs });
}

bool TagsModule::Impl::HasPendingArzDialogQueryWait(std::uint64_t runtimeId) const {
    return runtimeId != 0
        && std::any_of(
            pendingArzDialogQueryWaits_.begin(),
            pendingArzDialogQueryWaits_.end(),
            [&](const PendingArzDialogQueryWait& wait) { return wait.runtimeId == runtimeId; });
}

bool TagsModule::Impl::ConsumeReadyArzDialogQuery(std::uint64_t runtimeId, PendingArzDialogQueryKind kind) const {
    if (runtimeId == 0) {
        return false;
    }

    const auto it = std::find_if(
        readyArzDialogQueries_.begin(),
        readyArzDialogQueries_.end(),
        [&](const ReadyArzDialogQuery& ready) {
            return ready.runtimeId == runtimeId && ready.kind == kind;
        });
    if (it == readyArzDialogQueries_.end()) {
        return false;
    }

    readyArzDialogQueries_.erase(it);
    return true;
}

void TagsModule::Impl::MarkCurrentDispatchBlocked(std::uint64_t runtimeId) const {
    if (runtimeId == 0) {
        return;
    }

    if (std::find(blockedCurrentDispatchRuntimes_.begin(), blockedCurrentDispatchRuntimes_.end(), runtimeId)
        == blockedCurrentDispatchRuntimes_.end()) {
        blockedCurrentDispatchRuntimes_.push_back(runtimeId);
    }
}

void TagsModule::Impl::ProcessPendingKeyHoldWaits() {
    if (pendingKeyHoldWaits_.empty() || !binderModule_) {
        return;
    }

    const std::uint64_t now = GetTickCount64();
    for (auto it = pendingKeyHoldWaits_.begin(); it != pendingKeyHoldWaits_.end();) {
        const std::uint64_t runtimeId = it->runtimeId;
        if (runtimeId == 0 || !binderModule_->IsRuntimeActive(runtimeId)) {
            it = pendingKeyHoldWaits_.erase(it);
            continue;
        }

        if (now < it->releaseAtMs) {
            if (!binderModule_->IsRuntimePaused(runtimeId)) {
                binderModule_->PauseRuntime(runtimeId);
            }
            ++it;
            continue;
        }

        it = pendingKeyHoldWaits_.erase(it);
        if (binderModule_->IsRuntimeActive(runtimeId)
            && binderModule_->IsRuntimePaused(runtimeId)
            && !HasPendingKeyHoldWait(runtimeId)
            && !HasPendingDialogWait(runtimeId)) {
            binderModule_->ResumeRuntime(runtimeId);
        }
    }
}

void TagsModule::Impl::ProcessPendingArzDialogQueryWaits() {
    if (pendingArzDialogQueryWaits_.empty() || !binderModule_) {
        return;
    }

    const std::uint64_t now = GetTickCount64();
    for (auto it = pendingArzDialogQueryWaits_.begin(); it != pendingArzDialogQueryWaits_.end();) {
        const std::uint64_t runtimeId = it->runtimeId;
        if (runtimeId == 0 || !binderModule_->IsRuntimeActive(runtimeId)) {
            it = pendingArzDialogQueryWaits_.erase(it);
            continue;
        }

        bool pending = false;
        if (arizonaCefDialogs_) {
            switch (it->kind) {
            case PendingArzDialogQueryKind::InputText:
                pending = arizonaCefDialogs_->HasPendingInputTextQuery();
                break;
            case PendingArzDialogQueryKind::ListItem:
                pending = arizonaCefDialogs_->HasPendingListItemQuery();
                break;
            case PendingArzDialogQueryKind::ListItems:
                pending = arizonaCefDialogs_->HasPendingListItemsQuery();
                break;
            }
        }

        if (pending && it->deadlineAtMs != 0 && now < it->deadlineAtMs) {
            if (!binderModule_->IsRuntimePaused(runtimeId)) {
                binderModule_->PauseRuntime(runtimeId);
            }
            ++it;
            continue;
        }

        readyArzDialogQueries_.push_back(ReadyArzDialogQuery{ runtimeId, it->kind });
        it = pendingArzDialogQueryWaits_.erase(it);
        if (binderModule_->IsRuntimeActive(runtimeId)
            && binderModule_->IsRuntimePaused(runtimeId)
            && !HasPendingKeyHoldWait(runtimeId)
            && !HasPendingDialogWait(runtimeId)
            && !HasPendingArzDialogQueryWait(runtimeId)) {
            binderModule_->ResumeRuntime(runtimeId);
        }
    }
}

void TagsModule::Impl::ProcessPendingDialogWaits() {
    if (pendingDialogWaits_.empty() || !binderModule_) {
        return;
    }

    const bool dialogActive = sampApi_ && sampApi_->sampModule() && sampApi_->isSupportedVersion() && sampApi_->isDialogActive();
    const int activeDialogId = dialogActive ? sampApi_->SAMP_DIALOG_ID() : -1;
    const std::uint64_t now = GetTickCount64();
    for (auto it = pendingDialogWaits_.begin(); it != pendingDialogWaits_.end();) {
        const std::uint64_t runtimeId = it->runtimeId;
        if (runtimeId == 0 || !binderModule_->IsRuntimeActive(runtimeId)) {
            it = pendingDialogWaits_.erase(it);
            continue;
        }

        if (it->kind == PendingDialogWaitKind::Open) {
            if (dialogActive) {
                if (binderModule_->IsRuntimePaused(runtimeId) && !HasPendingKeyHoldWait(runtimeId)) {
                    binderModule_->ResumeRuntime(runtimeId);
                }
                it = pendingDialogWaits_.erase(it);
                continue;
            }

            if (it->deadlineAtMs != 0 && now >= it->deadlineAtMs) {
                NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogWaitOpenTimedOut), 3000.0);
                binderModule_->StopRuntime(runtimeId);
                it = pendingDialogWaits_.erase(it);
                continue;
            }

            if (!binderModule_->IsRuntimePaused(runtimeId)) {
                binderModule_->PauseRuntime(runtimeId);
            }
            ++it;
            continue;
        }

        if (it->kind == PendingDialogWaitKind::SpecificId) {
            if (dialogActive && activeDialogId == it->expectedDialogId) {
                if (binderModule_->IsRuntimePaused(runtimeId) && !HasPendingKeyHoldWait(runtimeId)) {
                    binderModule_->ResumeRuntime(runtimeId);
                }
                it = pendingDialogWaits_.erase(it);
                continue;
            }

            if (it->deadlineAtMs != 0 && now >= it->deadlineAtMs) {
                NotifyDialogError(
                    UiSettings::Instance().Format(
                        UiText::ToastDialogWaitIdTimedOut,
                        std::to_string(std::max(it->expectedDialogId, 0)).c_str()),
                    3000.0);
                binderModule_->StopRuntime(runtimeId);
                it = pendingDialogWaits_.erase(it);
                continue;
            }

            if (!binderModule_->IsRuntimePaused(runtimeId)) {
                binderModule_->PauseRuntime(runtimeId);
            }
            ++it;
            continue;
        }

        if (!dialogActive) {
            if (binderModule_->IsRuntimePaused(runtimeId) && !HasPendingKeyHoldWait(runtimeId)) {
                binderModule_->ResumeRuntime(runtimeId);
            }
            it = pendingDialogWaits_.erase(it);
            continue;
        }

        if (!binderModule_->IsRuntimePaused(runtimeId)) {
            binderModule_->PauseRuntime(runtimeId);
        }
        ++it;
    }
}

std::string_view TagsModule::Impl::TrimView(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(begin, end - begin);
}

std::string TagsModule::Impl::Trim(std::string_view value) {
    return std::string(TrimView(value));
}

std::string TagsModule::Impl::ToLower(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

bool TagsModule::Impl::StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::vector<std::string> TagsModule::Impl::SplitCommandArgs(std::string_view value) {
    std::vector<std::string> result;
    std::size_t pos = 0;
    while (pos < value.size()) {
        while (pos < value.size() && std::isspace(static_cast<unsigned char>(value[pos])) != 0) {
            ++pos;
        }
        if (pos >= value.size()) {
            break;
        }

        std::size_t end = pos;
        while (end < value.size() && std::isspace(static_cast<unsigned char>(value[end])) == 0) {
            ++end;
        }
        result.emplace_back(value.substr(pos, end - pos));
        pos = end;
    }
    return result;
}

std::optional<int> TagsModule::Impl::ParseInteger(std::string_view value) {
    std::string_view trimmed = TrimView(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    if (trimmed.front() == '+') {
        trimmed.remove_prefix(1);
        if (trimmed.empty() || std::isdigit(static_cast<unsigned char>(trimmed.front())) == 0) {
            return std::nullopt;
        }
    }

    int parsed = 0;
    const auto [end, error] = std::from_chars(
        trimmed.data(),
        trimmed.data() + trimmed.size(),
        parsed,
        10);
    if (error != std::errc{} || end != trimmed.data() + trimmed.size()) {
        return std::nullopt;
    }
    return parsed;
}

TagsModule::Impl::EvaluationContext TagsModule::Impl::ResolveActiveContext(
    std::string_view defaultSource,
    std::string_view defaultText) const {
    if (!g_activeContextStack.empty()) {
        return MakeViewContext(g_activeContextStack.back());
    }

    return EvaluationContext{
        sampApi_,
        defaultSource,
        defaultText,
        {},
        true,
        0,
    };
}

void TagsModule::Impl::RecordPlayerNamePerf(bool failed, bool unknown, double elapsedMs) const {
    const std::uint64_t now = GetTickCount64();
    PlayerNamePerfStats& stats = playerNamePerfStats_;
    if (stats.windowStartMs == 0 || now < stats.windowStartMs) {
        stats.windowStartMs = now;
    }

    ++stats.calls;
    if (failed) {
        ++stats.failed;
    }
    if (unknown) {
        ++stats.unknown;
    }
    stats.totalMs += elapsedMs;
    stats.maxMs = std::max(stats.maxMs, elapsedMs);
    MaybeLogPlayerNamePerf(now);
}

void TagsModule::Impl::MaybeLogPlayerNamePerf(std::uint64_t nowMs) const {
    PlayerNamePerfStats& stats = playerNamePerfStats_;
    if (stats.windowStartMs == 0 || nowMs < stats.windowStartMs) {
        stats.windowStartMs = nowMs;
        return;
    }

    const std::uint64_t windowMs = nowMs - stats.windowStartMs;
    if (windowMs < kPlayerNamePerfTelemetryWindowMs) {
        return;
    }
    if (stats.calls > 0) {
        debuglog::WriteInfo(
            "[tags][name][perf] window=%llums calls=%llu avg=%.3fms max=%.2fms failed=%llu unknown=%llu",
            static_cast<unsigned long long>(windowMs),
            static_cast<unsigned long long>(stats.calls),
            stats.totalMs / static_cast<double>(stats.calls),
            stats.maxMs,
            static_cast<unsigned long long>(stats.failed),
            static_cast<unsigned long long>(stats.unknown));
    }

    stats = PlayerNamePerfStats{};
    stats.windowStartMs = nowMs;
}

std::string TagsModule::Impl::ResolvePlayerNickById(int id, const EvaluationContext& context) const {
    const bool telemetryEnabled = debuglog::GetLevel() == debuglog::Level::Info;
    const double beginMs = telemetryEnabled ? TagResolverPerfNowMs() : 0.0;
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || id < 0) {
        if (telemetryEnabled) {
            RecordPlayerNamePerf(true, false, TagResolverPerfNowMs() - beginMs);
        }
        return std::string();
    }

    const std::string nick = sampApi->GetNameID(id);
    const bool unknown = nick.empty() || nick == "UNKNOWN";
    if (telemetryEnabled) {
        RecordPlayerNamePerf(false, unknown, TagResolverPerfNowMs() - beginMs);
    }
    return unknown ? std::string() : nick;
}

std::string TagsModule::Impl::ResolveLocalNick(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return {};
    }
    return ResolvePlayerNickById(sampApi->Local_ID(), context);
}

std::string TagsModule::Impl::ResolveLastTargetNick(const EvaluationContext& context) const {
    if (targetTracker_.lastId < 0) {
        return {};
    }
    return ResolvePlayerNickById(targetTracker_.lastId, context);
}

void TagsModule::Impl::ResetTargetTracker() {
    targetTracker_ = TargetTrackerState{};
    closestPlayerCache_ = ClosestPlayerCache{};
    closestPlayerPerfStats_ = ClosestPlayerPerfStats{};
    playerNamePerfStats_ = PlayerNamePerfStats{};
    myCarSnapshotCache_.valid = false;
    myCarSnapshotPerfStats_ = MyCarSnapshotPerfStats{};
}

TagsModule::Impl::ClosestPlayerCache& TagsModule::Impl::QueryClosestPlayers(const EvaluationContext& context) const {
    ClosestPlayerCache& snapshot = closestPlayerCache_;
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    const bool telemetryEnabled = debuglog::GetLevel() == debuglog::Level::Info;
    const ClosestPlayerQueryStats emptyStats{};
    const auto clearSnapshot = [&snapshot] {
        snapshot.result = ClosestPlayerQueryResult{};
        snapshot.nearestDetails = ClosestPlayerDetails{};
        snapshot.driverDetails = ClosestPlayerDetails{};
        snapshot.localPed = 0;
        snapshot.viewportWidth = 0;
        snapshot.viewportHeight = 0;
        snapshot.localId = -1;
        snapshot.valid = false;
    };

    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        clearSnapshot();
        MaybeLogClosestPlayersSnapshot(snapshot, emptyStats, 0.0, context);
        RecordClosestPlayersPerf(false, false, false, emptyStats, 0.0);
        return snapshot;
    }

    const int localId = sampApi->Local_ID();
    CPed* const localPed = FindPlayerPed();
    if (localId < 0 || !IsClosestCandidatePedValid(localPed)) {
        clearSnapshot();
        MaybeLogClosestPlayersSnapshot(snapshot, emptyStats, 0.0, context);
        RecordClosestPlayersPerf(false, true, false, emptyStats, 0.0);
        return snapshot;
    }

    const std::uintptr_t localPedAddress = reinterpret_cast<std::uintptr_t>(localPed);
    if (snapshot.valid
        && snapshot.tickGeneration == tickGeneration_
        && snapshot.localId == localId
        && snapshot.localPed == localPedAddress
        && snapshot.viewportWidth == RsGlobal.maximumWidth
        && snapshot.viewportHeight == RsGlobal.maximumHeight) {
        RecordClosestPlayersPerf(true, true, true, emptyStats, 0.0);
        return snapshot;
    }

    const double queryStartedAtMs = telemetryEnabled ? TagResolverPerfNowMs() : 0.0;
    const CVector localPosition = localPed->GetPosition();
    if (!IsFinitePosition(localPosition)) {
        clearSnapshot();
        MaybeLogClosestPlayersSnapshot(snapshot, emptyStats, 0.0, context);
        RecordClosestPlayersPerf(false, true, false, emptyStats, 0.0);
        return snapshot;
    }

    const bool hasScreenMetrics = RsGlobal.maximumWidth > 0 && RsGlobal.maximumHeight > 0;
    const float screenCenterX = static_cast<float>(RsGlobal.maximumWidth) * 0.5f;
    const float screenCenterY = static_cast<float>(RsGlobal.maximumHeight) * 0.5f;

    ClosestPlayerQueryResult result;
    if (hasScreenMetrics) {
        result.viewportWidth = RsGlobal.maximumWidth;
        result.viewportHeight = RsGlobal.maximumHeight;
        result.screenCenterX = screenCenterX;
        result.screenCenterY = screenCenterY;
    }
    ClosestPlayerQueryStats queryStats;
    float bestDistanceSq = std::numeric_limits<float>::max();
    float bestCenterDistanceSq = std::numeric_limits<float>::max();
    float bestDriverDistanceSq = std::numeric_limits<float>::max();

    for (int id = 0; id <= kMaxSampPlayerId; ++id) {
        if (id == localId || !sampApi->IsConnected(id)) {
            continue;
        }

        CPed* const candidatePed =
            reinterpret_cast<CPed*>(const_cast<void*>(sampApi->GetPlayerPedPointer(id, false, nullptr, false)));
        if (!candidatePed) {
            ++queryStats.notStreamed;
            continue;
        }
        if (candidatePed == localPed || !IsClosestCandidatePedValid(candidatePed)) {
            ++queryStats.invalidPed;
            continue;
        }

        const CVector& position = candidatePed->GetPosition();
        if (!IsFinitePosition(position)) {
            ++queryStats.invalidPosition;
            continue;
        }
        ++queryStats.candidates;

        const float dx = position.x - localPosition.x;
        const float dy = position.y - localPosition.y;
        const float dz = position.z - localPosition.z;
        const float distanceSq = dx * dx + dy * dy + dz * dz;

        if (IsBetterClosestCandidate(distanceSq, id, bestDistanceSq, result.nearestId)) {
            bestDistanceSq = distanceSq;
            result.nearestId = id;
            result.nearestDistanceSq = distanceSq;
        }

        CVehicle* const vehicle = candidatePed->m_pVehicle;
        if (IsVehiclePointerValid(vehicle) && vehicle->m_pDriver == candidatePed) {
            ++queryStats.driverCandidates;
            if (IsBetterClosestCandidate(distanceSq, id, bestDriverDistanceSq, result.nearestDriverId)) {
                bestDriverDistanceSq = distanceSq;
                result.nearestDriverId = id;
                result.nearestDriverDistanceSq = distanceSq;
            }
        }

        if (!hasScreenMetrics) {
            ++queryStats.projectionFailed;
            continue;
        }

        RwV3d worldPosition = {
            position.x,
            position.y,
            position.z + kClosestScreenTargetZOffset,
        };
        RwV3d screenPosition{};
        float width = 0.0f;
        float height = 0.0f;
        if (!CSprite::CalcScreenCoors(worldPosition, &screenPosition, &width, &height, true, true)) {
            ++queryStats.projectionFailed;
            continue;
        }
        if (!std::isfinite(screenPosition.x) || !std::isfinite(screenPosition.y)) {
            ++queryStats.projectionFailed;
            continue;
        }
        if (screenPosition.x < 0.0f
            || screenPosition.y < 0.0f
            || screenPosition.x >= static_cast<float>(RsGlobal.maximumWidth)
            || screenPosition.y >= static_cast<float>(RsGlobal.maximumHeight)) {
            ++queryStats.offscreen;
            continue;
        }

        const float screenDx = screenPosition.x - screenCenterX;
        const float screenDy = screenPosition.y - screenCenterY;
        const float centerDistanceSq = screenDx * screenDx + screenDy * screenDy;
        if (IsBetterClosestCandidate(
                centerDistanceSq,
                id,
                bestCenterDistanceSq,
                result.nearestToCenterId)) {
            bestCenterDistanceSq = centerDistanceSq;
            result.nearestToCenterId = id;
            result.nearestToCenterDistanceSq = centerDistanceSq;
            result.nearestToCenterScreenX = screenPosition.x;
            result.nearestToCenterScreenY = screenPosition.y;
        }
    }

    snapshot.result = result;
    snapshot.nearestDetails = ClosestPlayerDetails{};
    snapshot.driverDetails = ClosestPlayerDetails{};
    snapshot.tickGeneration = tickGeneration_;
    snapshot.updatedAtMs = GetTickCount64();
    snapshot.localPed = localPedAddress;
    snapshot.viewportWidth = RsGlobal.maximumWidth;
    snapshot.viewportHeight = RsGlobal.maximumHeight;
    snapshot.localId = localId;
    snapshot.valid = true;

    const double elapsedMs = telemetryEnabled
        ? std::max(0.0, TagResolverPerfNowMs() - queryStartedAtMs)
        : 0.0;
    MaybeLogClosestPlayersSnapshot(snapshot, queryStats, elapsedMs, context);
    if (elapsedMs >= kClosestPlayerSlowQueryLogMs
        && (snapshot.lastSlowLogAtMs == 0
            || snapshot.updatedAtMs - snapshot.lastSlowLogAtMs >= kClosestPlayerSlowQueryLogThrottleMs)) {
        snapshot.lastSlowLogAtMs = snapshot.updatedAtMs;
        debuglog::WriteInfo(
            "[tags][closest] slow query elapsed=%.3fms nearest=%d center=%d driver=%d candidates=%zu drivers=%zu",
            elapsedMs,
            result.nearestId,
            result.nearestToCenterId,
            result.nearestDriverId,
            queryStats.candidates,
            queryStats.driverCandidates);
    }

    RecordClosestPlayersPerf(false, true, true, queryStats, elapsedMs);
    return snapshot;
}

void TagsModule::Impl::ResolveClosestPlayerDetails(
    ClosestPlayerCache& snapshot,
    bool driver,
    bool requireNick,
    bool requireColor,
    bool requireVehicle,
    const EvaluationContext& context) const {
    ClosestPlayerDetails& details = driver ? snapshot.driverDetails : snapshot.nearestDetails;
    const int id = driver ? snapshot.result.nearestDriverId : snapshot.result.nearestId;
    if (id < 0) {
        return;
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (requireNick && !details.nickResolved) {
        details.nickResolved = true;
        details.nick = ResolvePlayerNickById(id, context);
    }
    if (requireColor && !details.colorResolved) {
        details.colorResolved = true;
        const std::optional<std::uint32_t> color = sampApi ? sampApi->GetPlayerColor(id) : std::nullopt;
        details.color = color.has_value() ? FormatSampColorTag(*color) : std::string("{FFFFFF}");
    }
    if (driver && requireVehicle && !details.vehicleResolved) {
        details.vehicleResolved = true;
        if (!sampApi) {
            return;
        }

        const CPed* const ped = FindPlayerPedBySampId(*sampApi, id);
        if (!IsClosestCandidatePedValid(const_cast<CPed*>(ped))) {
            return;
        }
        const CVehicle* const vehicle = ped->m_pVehicle;
        if (!IsVehiclePointerValid(vehicle) || vehicle->m_pDriver != ped) {
            return;
        }
        details.vehicle = ResolveVehicleDisplayName(vehicle);
    }
}

void TagsModule::Impl::MaybeLogClosestPlayersSnapshot(
    ClosestPlayerCache& snapshot,
    const ClosestPlayerQueryStats& queryStats,
    double elapsedMs,
    const EvaluationContext& context) const {
    if (snapshot.snapshotLogged
        && snapshot.result.nearestId == snapshot.lastLoggedResult.nearestId
        && snapshot.result.nearestToCenterId == snapshot.lastLoggedResult.nearestToCenterId
        && snapshot.result.nearestDriverId == snapshot.lastLoggedResult.nearestDriverId
        && snapshot.result.viewportWidth == snapshot.lastLoggedResult.viewportWidth
        && snapshot.result.viewportHeight == snapshot.lastLoggedResult.viewportHeight) {
        return;
    }

    ResolveClosestPlayerDetails(snapshot, false, true, true, false, context);
    ResolveClosestPlayerDetails(snapshot, true, true, true, true, context);
    const auto distance = [](float distanceSq) {
        return distanceSq >= 0.0f && std::isfinite(distanceSq) ? std::sqrt(distanceSq) : -1.0f;
    };

    debuglog::WriteInfo(
        "[tags][closest] snapshot nearest=%d nick=\"%s\" dist=%.2f color=%s "
        "viewport=%dx%d center=(%.1f,%.1f) centerId=%d centerProjected=(%.1f,%.1f) centerPx=%.2f "
        "driver=%d driverNick=\"%s\" driverDist=%.2f driverColor=%s car=\"%s\" "
        "candidates=%zu drivers=%zu notStreamed=%zu invalidPed=%zu invalidPos=%zu projectionFailed=%zu offscreen=%zu "
        "elapsed=%.3fms",
        snapshot.result.nearestId,
        snapshot.nearestDetails.nick.c_str(),
        distance(snapshot.result.nearestDistanceSq),
        snapshot.nearestDetails.color.c_str(),
        snapshot.result.viewportWidth,
        snapshot.result.viewportHeight,
        snapshot.result.screenCenterX,
        snapshot.result.screenCenterY,
        snapshot.result.nearestToCenterId,
        snapshot.result.nearestToCenterScreenX,
        snapshot.result.nearestToCenterScreenY,
        distance(snapshot.result.nearestToCenterDistanceSq),
        snapshot.result.nearestDriverId,
        snapshot.driverDetails.nick.c_str(),
        distance(snapshot.result.nearestDriverDistanceSq),
        snapshot.driverDetails.color.c_str(),
        snapshot.driverDetails.vehicle.c_str(),
        queryStats.candidates,
        queryStats.driverCandidates,
        queryStats.notStreamed,
        queryStats.invalidPed,
        queryStats.invalidPosition,
        queryStats.projectionFailed,
        queryStats.offscreen,
        elapsedMs);

    snapshot.lastLoggedResult = snapshot.result;
    snapshot.snapshotLogged = true;
}

void TagsModule::Impl::RecordClosestPlayersPerf(
    bool cacheHit,
    bool sampReady,
    bool localReady,
    const ClosestPlayerQueryStats& queryStats,
    double elapsedMs) const {
    const std::uint64_t now = GetTickCount64();
    ClosestPlayerPerfStats& stats = closestPlayerPerfStats_;
    if (stats.windowStartMs == 0 || now < stats.windowStartMs) {
        stats.windowStartMs = now;
    }

    ++stats.requests;
    if (cacheHit) {
        ++stats.cacheHits;
    } else if (sampReady && localReady) {
        ++stats.rebuilds;
        stats.totalRebuildMs += elapsedMs;
        stats.maxRebuildMs = std::max(stats.maxRebuildMs, elapsedMs);
    }
    if (!sampReady) {
        ++stats.noSamp;
    }
    if (!localReady) {
        ++stats.noLocal;
    }
    stats.maxCandidates = std::max(stats.maxCandidates, queryStats.candidates);
    stats.maxDriverCandidates = std::max(stats.maxDriverCandidates, queryStats.driverCandidates);
    stats.notStreamed += queryStats.notStreamed;
    stats.invalidPed += queryStats.invalidPed;
    stats.invalidPosition += queryStats.invalidPosition;
    stats.projectionFailed += queryStats.projectionFailed;
    stats.offscreen += queryStats.offscreen;

    MaybeLogClosestPlayersPerf(now);
}

void TagsModule::Impl::MaybeLogClosestPlayersPerf(std::uint64_t nowMs) const {
    ClosestPlayerPerfStats& stats = closestPlayerPerfStats_;
    if (stats.windowStartMs == 0 || nowMs < stats.windowStartMs) {
        stats.windowStartMs = nowMs;
        return;
    }

    const std::uint64_t windowMs = nowMs - stats.windowStartMs;
    if (windowMs < kClosestPlayerPerfTelemetryWindowMs) {
        return;
    }

    if (stats.requests == 0) {
        stats = ClosestPlayerPerfStats{};
        stats.windowStartMs = nowMs;
        return;
    }

    const double avgRebuildMs =
        stats.rebuilds > 0 ? static_cast<double>(stats.totalRebuildMs) / static_cast<double>(stats.rebuilds) : 0.0;
    debuglog::WriteInfo(
        "[tags][closest][perf] window=%llums requests=%llu hits=%llu rebuilds=%llu noSamp=%llu noLocal=%llu "
        "avgRebuild=%.3fms maxRebuild=%.3fms maxCandidates=%zu maxDrivers=%zu notStreamed=%llu invalidPed=%llu "
        "invalidPos=%llu projectionFailed=%llu offscreen=%llu",
        static_cast<unsigned long long>(windowMs),
        static_cast<unsigned long long>(stats.requests),
        static_cast<unsigned long long>(stats.cacheHits),
        static_cast<unsigned long long>(stats.rebuilds),
        static_cast<unsigned long long>(stats.noSamp),
        static_cast<unsigned long long>(stats.noLocal),
        avgRebuildMs,
        stats.maxRebuildMs,
        stats.maxCandidates,
        stats.maxDriverCandidates,
        static_cast<unsigned long long>(stats.notStreamed),
        static_cast<unsigned long long>(stats.invalidPed),
        static_cast<unsigned long long>(stats.invalidPosition),
        static_cast<unsigned long long>(stats.projectionFailed),
        static_cast<unsigned long long>(stats.offscreen));

    stats = ClosestPlayerPerfStats{};
    stats.windowStartMs = nowMs;
}

void TagsModule::Impl::UpdateTargetTracker() {
    SampApi* sampApi = sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        if (targetTracker_.sessionActive) {
            ResetTargetTracker();
        }
        return;
    }

    CPlayerPed* playerPed = FindPlayerPed();
    const int localId = sampApi->Local_ID();
    const bool sessionReady = playerPed != nullptr && localId >= 0;
    if (!sessionReady) {
        if (targetTracker_.sessionActive) {
            ResetTargetTracker();
        }
        return;
    }

    if (!targetTracker_.sessionActive) {
        ResetTargetTracker();
        targetTracker_.sessionActive = true;
    }

    targetTracker_.currentId = -1;

    auto logTargetState = [&](CPed* ped, int resolvedId, bool valid, const char* reason) {
        const std::uintptr_t pedAddress = reinterpret_cast<std::uintptr_t>(ped);
        if (targetTracker_.lastLoggedPed == pedAddress && targetTracker_.lastLoggedResolvedId == resolvedId
            && targetTracker_.lastLoggedValid == valid) {
            return;
        }

        targetTracker_.lastLoggedPed = pedAddress;
        targetTracker_.lastLoggedResolvedId = resolvedId;
        targetTracker_.lastLoggedValid = valid;
        debuglog::WriteInfo(
            "[tags][target] ped=0x%08X resolvedId=%d valid=%d reason=%s",
            static_cast<unsigned int>(pedAddress),
            resolvedId,
            valid ? 1 : 0,
            reason);
    };

    CPed* targetedPed = playerPed->m_pPlayerTargettedPed;
    if (!targetedPed) {
        logTargetState(nullptr, -1, false, "none");
        return;
    }

    const auto [resolved, targetId] = sampApi->TryResolvePlayerIdByPedFast(targetedPed);
    if (!resolved || targetId < 0) {
        logTargetState(targetedPed, targetId, false, "unresolved");
        return;
    }

    if (targetId > 1003 || !sampApi->IsConnected(targetId)) {
        logTargetState(targetedPed, targetId, false, "invalid-or-disconnected");
        return;
    }

    targetTracker_.currentId = targetId;
    targetTracker_.lastId = targetId;
    logTargetState(targetedPed, targetId, true, "ok");
}

std::uint64_t TagsModule::Impl::QueueVirtualKeyHold(unsigned int keyCode, int startDelayMs, int holdDurationMs) const {
    if (keyCode == 0 || keyCode > 0xFF) {
        return 0;
    }

    const std::uint64_t now = GetTickCount64();
    const std::uint64_t pressAtMs = now + static_cast<std::uint64_t>(std::max(startDelayMs, 0));
    const std::uint64_t releaseAtMs = pressAtMs + static_cast<std::uint64_t>(std::max(holdDurationMs, 1));

    for (auto it = activeVirtualKeyHolds_.begin(); it != activeVirtualKeyHolds_.end();) {
        if (it->keyCode == keyCode) {
            ReleaseVirtualKeyHold(*it);
            ClearPendingKeyHoldWaitsByKeyCode(keyCode);
            it = activeVirtualKeyHolds_.erase(it);
            continue;
        }
        ++it;
    }

    activeVirtualKeyHolds_.push_back(ActiveVirtualKeyHold{
        keyCode,
        pressAtMs,
        releaseAtMs,
        false,
    });
    return releaseAtMs;
}

void TagsModule::Impl::QueuePendingBindDelayOverride(std::uint64_t runtimeId, int delayMs) const {
    if (runtimeId == 0) {
        return;
    }

    if (auto it = std::find_if(
            pendingBindDelayOverrides_.begin(),
            pendingBindDelayOverrides_.end(),
            [&](const PendingBindDelayOverride& entry) { return entry.runtimeId == runtimeId; });
        it != pendingBindDelayOverrides_.end()) {
        it->delayMs = delayMs;
        return;
    }

    pendingBindDelayOverrides_.push_back(PendingBindDelayOverride{ runtimeId, delayMs });
}

void TagsModule::Impl::ReleaseVirtualKeyHold(ActiveVirtualKeyHold& hold) const {
    if (!hold.pressed) {
        return;
    }
    SendVirtualKeyEvent(hold.keyCode, true);
    hold.pressed = false;
}

std::string TagsModule::Impl::FormatCurrentTime(std::string_view format) {
    return FormatCurrentTimeForTimestamp(std::time(nullptr), format);
}

std::string TagsModule::Impl::FormatWholeStatValue(float value) {
    const long long rounded = std::llround(std::max(0.0f, value));
    return std::to_string(rounded);
}

std::string TagsModule::Impl::MakeRpNick(std::string_view nick) {
    std::string result;
    result.reserve(nick.size());
    for (const char ch : nick) {
        result.push_back(ch == '_' ? ' ' : ch);
    }
    return result;
}

std::string TagsModule::Impl::ExtractName(std::string_view nick) {
    const std::size_t separator = nick.find('_');
    return std::string(nick.substr(0, separator));
}

std::string TagsModule::Impl::ExtractSurname(std::string_view nick) {
    const std::size_t separator = nick.find('_');
    if (separator == std::string_view::npos || separator + 1 >= nick.size()) {
        return {};
    }
    return std::string(nick.substr(separator + 1));
}
