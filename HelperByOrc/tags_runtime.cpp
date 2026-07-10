#include "tags_module_impl.h"
#include "tags_module_detail.h"

namespace {

double PlayerNamePerfNowMs() {
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

void TagsModule::Impl::Tick() {
    UpdateTargetTracker();
    ProcessPendingDialogWaits();
    ProcessPendingArzDialogQueryWaits();
    if (!activeVirtualKeyHolds_.empty()) {
        const std::uint64_t now = GetTickCount64();
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

std::string TagsModule::Impl::Trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
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
    const std::string trimmed = Trim(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    int sign = 1;
    std::size_t pos = 0;
    if (trimmed.front() == '+') {
        pos = 1;
    } else if (trimmed.front() == '-') {
        sign = -1;
        pos = 1;
    }

    if (pos >= trimmed.size()) {
        return std::nullopt;
    }

    int parsed = 0;
    for (; pos < trimmed.size(); ++pos) {
        const unsigned char ch = static_cast<unsigned char>(trimmed[pos]);
        if (std::isdigit(ch) == 0) {
            return std::nullopt;
        }
        parsed = parsed * 10 + static_cast<int>(ch - '0');
    }

    return parsed * sign;
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
    const double beginMs = PlayerNamePerfNowMs();
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || id < 0) {
        RecordPlayerNamePerf(true, false, PlayerNamePerfNowMs() - beginMs);
        return std::string();
    }

    const std::string nick = sampApi->GetNameID(id);
    const bool unknown = nick.empty() || nick == "UNKNOWN";
    RecordPlayerNamePerf(false, unknown, PlayerNamePerfNowMs() - beginMs);
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
    closestPlayerCache_.valid = false;
    closestPlayerPerfStats_ = ClosestPlayerPerfStats{};
    playerNamePerfStats_ = PlayerNamePerfStats{};
    myCarSnapshotCache_.valid = false;
    myCarSnapshotPerfStats_ = MyCarSnapshotPerfStats{};
}

TagsModule::Impl::ClosestPlayerQueryResult TagsModule::Impl::QueryClosestPlayers(const EvaluationContext& context) const {
    ClosestPlayerQueryResult result;

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    const std::uint64_t now = GetTickCount64();
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        RecordClosestPlayersPerf(false, false, false, 0, 0);
        return result;
    }

    const int localId = sampApi->Local_ID();
    CPed* const localPed = FindPlayerPed();
    if (localId < 0 || !localPed) {
        closestPlayerCache_.valid = false;
        RecordClosestPlayersPerf(false, true, false, 0, 0);
        return result;
    }

    const std::uintptr_t localPedAddress = reinterpret_cast<std::uintptr_t>(localPed);
    if (closestPlayerCache_.valid
        && closestPlayerCache_.localId == localId
        && closestPlayerCache_.localPed == localPedAddress
        && now >= closestPlayerCache_.updatedAtMs
        && now - closestPlayerCache_.updatedAtMs <= kClosestPlayerCacheTtlMs) {
        RecordClosestPlayersPerf(true, true, true, 0, 0);
        return closestPlayerCache_.result;
    }

    const std::uint64_t queryStartedAtMs = now;
    const CVector localPosition = localPed->GetPosition();
    const bool hasScreenMetrics = RsGlobal.maximumWidth > 0 && RsGlobal.maximumHeight > 0;
    const float screenCenterX = static_cast<float>(RsGlobal.maximumWidth) * 0.5f;
    const float screenCenterY = static_cast<float>(RsGlobal.maximumHeight) * 0.5f;

    float bestDistanceSq = std::numeric_limits<float>::max();
    float bestCenterDistanceSq = std::numeric_limits<float>::max();
    std::size_t candidates = 0;

    for (int id = 0; id <= kMaxSampPlayerId; ++id) {
        if (id == localId) {
            continue;
        }

        CPed* const candidatePed =
            reinterpret_cast<CPed*>(const_cast<void*>(sampApi->GetPlayerPedPointer(id, false, nullptr, false)));
        if (!candidatePed || candidatePed == localPed) {
            continue;
        }
        ++candidates;

        const CVector& position = candidatePed->GetPosition();
        const float dx = position.x - localPosition.x;
        const float dy = position.y - localPosition.y;
        const float dz = position.z - localPosition.z;
        const float distanceSq = dx * dx + dy * dy + dz * dz;

        if (IsBetterClosestCandidate(distanceSq, id, bestDistanceSq, result.nearestId)) {
            bestDistanceSq = distanceSq;
            result.nearestId = id;
        }

        if (!hasScreenMetrics) {
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
        }
    }

    closestPlayerCache_.result = result;
    closestPlayerCache_.updatedAtMs = GetTickCount64();
    closestPlayerCache_.localPed = localPedAddress;
    closestPlayerCache_.localId = localId;
    closestPlayerCache_.valid = true;

    const std::uint64_t elapsedMs = closestPlayerCache_.updatedAtMs - queryStartedAtMs;
    if (elapsedMs >= kClosestPlayerSlowQueryLogMs
        && (closestPlayerCache_.lastSlowLogAtMs == 0
            || closestPlayerCache_.updatedAtMs - closestPlayerCache_.lastSlowLogAtMs >= kClosestPlayerSlowQueryLogThrottleMs)) {
        closestPlayerCache_.lastSlowLogAtMs = closestPlayerCache_.updatedAtMs;
        debuglog::WriteInfo(
            "[tags][closest] slow query elapsed=%llums nearest=%d center=%d",
            static_cast<unsigned long long>(elapsedMs),
            result.nearestId,
            result.nearestToCenterId);
    }

    RecordClosestPlayersPerf(false, true, true, candidates, elapsedMs);
    return result;
}

void TagsModule::Impl::RecordClosestPlayersPerf(
    bool cacheHit,
    bool sampReady,
    bool localReady,
    std::size_t candidates,
    std::uint64_t elapsedMs) const {
    const std::uint64_t now = GetTickCount64();
    ClosestPlayerPerfStats& stats = closestPlayerPerfStats_;
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
    }
    if (!sampReady) {
        ++stats.noSamp;
    }
    if (!localReady) {
        ++stats.noLocal;
    }
    stats.maxCandidates = std::max(stats.maxCandidates, candidates);

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
        "avgRebuild=%.2fms maxRebuild=%llums maxCandidates=%zu",
        static_cast<unsigned long long>(windowMs),
        static_cast<unsigned long long>(stats.requests),
        static_cast<unsigned long long>(stats.cacheHits),
        static_cast<unsigned long long>(stats.rebuilds),
        static_cast<unsigned long long>(stats.noSamp),
        static_cast<unsigned long long>(stats.noLocal),
        avgRebuildMs,
        static_cast<unsigned long long>(stats.maxRebuildMs),
        stats.maxCandidates);

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
    if (format.empty()) {
        return {};
    }

    std::time_t now = std::time(nullptr);
    std::tm localTime{};
    if (localtime_s(&localTime, &now) != 0) {
        return {};
    }

    const std::string formatString(format);
    std::size_t bufferSize = std::max<std::size_t>(128, formatString.size() * 8 + 32);
    for (int attempt = 0; attempt < 6; ++attempt) {
        std::string buffer(bufferSize, '\0');
        const std::size_t written = std::strftime(buffer.data(), buffer.size(), formatString.c_str(), &localTime);
        if (written != 0) {
            buffer.resize(written);
            return buffer;
        }
        bufferSize *= 2;
    }
    return {};
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
