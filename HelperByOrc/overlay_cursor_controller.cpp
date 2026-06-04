#include "overlay_cursor_controller.h"

#include "debug_log.h"
#include "samp_api.h"

#include <cstdio>

namespace {

constexpr int kSampCursorModeNone = 0;
constexpr int kSampCursorModeLockCamAndControl = 2;
constexpr std::uint64_t kCursorReassertIntervalMs = 200;
constexpr std::uint64_t kCursorReassertTraceIntervalMs = 2500;
constexpr std::uint64_t kCursorTraceIntervalMs = 700;
constexpr std::uint64_t kCursorUnavailableTraceIntervalMs = 1500;
constexpr std::uint64_t kDeferredExternalReleaseCleanupWindowMs = 1500;

bool ModeActive(const std::optional<int>& mode) {
    return mode.has_value() && *mode != kSampCursorModeNone;
}

bool ModeLockCamAndControl(const std::optional<int>& mode) {
    return mode.has_value() && *mode == kSampCursorModeLockCamAndControl;
}

bool HasKnownExternalOwner(const OverlayCursorController::Inputs& inputs) {
    return inputs.chatOpen
        || inputs.dialogOpen
        || inputs.externalCursorActive
        || inputs.cefShown
        || !inputs.externalOwnerName.empty();
}

std::string ModeText(const std::optional<int>& mode) {
    if (!mode.has_value()) {
        return "n/a";
    }

    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%d", *mode);
    return buffer;
}

} // namespace

void OverlayCursorController::SetSampApi(SampApi* sampApi) {
    sampApi_ = sampApi;
}

OverlayCursorController::Result OverlayCursorController::Apply(const Inputs& inputs) {
    const std::uint64_t now = ::GetTickCount64();
    const bool rmbHeld = (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

    Result result{};
    result.owner = Owner::Unavailable;
    result.reason = "unavailable";

    if (!inputs.sampUiPipelineReady) {
        result.reason = "samp-ui-gate";
        if (lastUiHold_) {
            ReleaseHold("[ui] cursor pipeline gated: released capture while SA:MP is not fully initialized");
        }
        if (helperModeActive_) {
            ApplySampCursorMode(kSampCursorModeNone, false, false, now);
            helperModeActive_ = false;
        }
        if (cursorMode_ != kSampCursorModeNone || cursorEnabled_) {
            cursorMode_ = kSampCursorModeNone;
            cursorEnabled_ = false;
            lastApplyMs_ = now;
        }
        ClearDeferredExternalRelease();
        if (now - lastGateTraceMs_ >= kCursorUnavailableTraceIntervalMs) {
            lastGateTraceMs_ = now;
            debuglog::WriteInfo("[ui] cursor pipeline gated: waiting for full SA:MP initialization");
        }
        TraceState(inputs, result, rmbHeld, now);
        lastOwner_ = result.owner;
        lastReason_ = result.reason;
        return result;
    }

    if (!inputs.appHasFocus) {
        result.reason = "focus-lost";
        if (lastUiHold_) {
            ReleaseHold("[ui] ReleaseCapture due to focus loss");
        }
        TraceState(inputs, result, rmbHeld, now);
        lastOwner_ = result.owner;
        lastReason_ = result.reason;
        return result;
    }

    const bool helperWants = inputs.helperWantsInputRouting || inputs.helperWantsCursor;
    const bool sampCursorActive = ModeActive(inputs.sampCursorMode);
    const bool sampUiExternalActive = inputs.chatOpen || inputs.dialogOpen;
    const bool blockingExternalActive = (inputs.externalCursorActive && !sampUiExternalActive)
        || inputs.cefShown;
    const bool orphanedSampCursorMode = deferredExternalReleasePending_
        && now <= deferredExternalReleaseCleanupUntilMs_
        && !helperModeActive_
        && !helperWants
        && ModeLockCamAndControl(inputs.sampCursorMode)
        && !HasKnownExternalOwner(inputs);
    if (orphanedSampCursorMode) {
        result.owner = Owner::Game;
        result.reason = "orphaned-samp-cursor";
        debuglog::WriteInfo(
            "[ui] clearing orphaned SAMP cursor mode after external handoff: sampMode=%s osCursor=%d",
            ModeText(inputs.sampCursorMode).c_str(),
            inputs.cursorVisible ? 1 : 0);
        result.sampModeApplied = ApplySampCursorMode(kSampCursorModeNone, false, false, now);
        if (result.sampModeApplied) {
            helperModeActive_ = false;
            ClearDeferredExternalRelease();
        } else {
            result.owner = Owner::Unavailable;
            result.reason = "orphaned-samp-cursor-clear-failed";
            if (!deferredExternalReleaseFailureLogged_) {
                deferredExternalReleaseFailureLogged_ = true;
                debuglog::WriteError("[ui] orphaned SAMP cursor cleanup failed after external handoff");
            }
        }
        TraceState(inputs, result, rmbHeld, now);
        lastOwner_ = result.owner;
        lastReason_ = result.reason;
        return result;
    }
    if (deferredExternalReleasePending_
        && now > deferredExternalReleaseCleanupUntilMs_
        && !helperModeActive_) {
        ClearDeferredExternalRelease();
    }

    const bool passiveSampCursorOwner = sampCursorActive && !helperModeActive_ && !helperWants;
    const bool externalActive = blockingExternalActive
        || (!helperWants && (sampUiExternalActive || passiveSampCursorOwner));

    if (externalActive) {
        result.owner = Owner::External;
        result.reason = inputs.externalOwnerName.empty() ? "samp-cursor-mode" : inputs.externalOwnerName;
        if (lastUiHold_) {
            ReleaseHold("[ui] ReleaseCapture due to external cursor owner");
        }
        if (helperModeActive_) {
            DeferHelperReleaseForExternal(inputs, now);
        }
        TraceState(inputs, result, rmbHeld, now);
        lastOwner_ = result.owner;
        lastReason_ = result.reason;
        return result;
    }

    if (helperWants) {
        result.owner = Owner::Helper;
        result.reason = inputs.helperWantsInputRouting ? "helper-routing" : "helper-cursor";

        const bool actualModeMatches =
            !inputs.sampCursorMode.has_value() || *inputs.sampCursorMode == kSampCursorModeLockCamAndControl;
        const bool desiredSameAsCache =
            cursorMode_ == kSampCursorModeLockCamAndControl && cursorEnabled_ && helperModeActive_ && actualModeMatches;
        const bool shouldReassert = desiredSameAsCache && (now - lastApplyMs_ >= kCursorReassertIntervalMs);

        if (!desiredSameAsCache || shouldReassert) {
            if (!ApplySampCursorMode(kSampCursorModeLockCamAndControl, true, shouldReassert, now)) {
                result.owner = Owner::Unavailable;
                result.reason = "set-cursor-mode-failed";
                TraceState(inputs, result, rmbHeld, now);
                lastOwner_ = result.owner;
                lastReason_ = result.reason;
                return result;
            }
            result.sampModeApplied = true;
            helperModeActive_ = true;
        }

        lastUiHold_ = true;
        result.routingAllowed = true;
        TraceState(inputs, result, rmbHeld, now);
        lastOwner_ = result.owner;
        lastReason_ = result.reason;
        return result;
    }

    result.owner = Owner::Game;
    result.reason = "game";
    if (lastUiHold_) {
        ReleaseHold("[ui] ReleaseCapture due to UI-hold end");
    }
    if (helperModeActive_) {
        result.sampModeApplied = ApplySampCursorMode(kSampCursorModeNone, false, false, now);
        if (result.sampModeApplied) {
            helperModeActive_ = false;
            if (deferredExternalReleasePending_) {
                deferredExternalReleaseCleanupUntilMs_ = now + kDeferredExternalReleaseCleanupWindowMs;
            }
        }
    }

    TraceState(inputs, result, rmbHeld, now);
    lastOwner_ = result.owner;
    lastReason_ = result.reason;
    return result;
}

void OverlayCursorController::Shutdown() {
    if (lastUiHold_) {
        ReleaseHold("[ui] ReleaseCapture during cursor controller shutdown");
    } else {
        ::ReleaseCapture();
    }

    if (helperModeActive_) {
        ApplySampCursorMode(kSampCursorModeNone, false, false, ::GetTickCount64());
    }

    cursorMode_ = kSampCursorModeNone;
    cursorEnabled_ = false;
    helperModeActive_ = false;
    ClearDeferredExternalRelease();
    lastOwner_ = Owner::Unavailable;
    lastReason_.clear();
    lastApplyMs_ = 0;
}

const char* OverlayCursorController::OwnerName(Owner owner) {
    switch (owner) {
    case Owner::Unavailable:
        return "unavailable";
    case Owner::Game:
        return "game";
    case Owner::Helper:
        return "helper";
    case Owner::External:
        return "external";
    default:
        return "unknown";
    }
}

void OverlayCursorController::ReleaseHold(const char* reason) {
    ::ReleaseCapture();
    lastUiHold_ = false;
    if (reason && *reason) {
        debuglog::WriteInfo("%s", reason);
    }
}

void OverlayCursorController::DeferHelperReleaseForExternal(const Inputs& inputs, std::uint64_t now) {
    if (!deferredExternalReleasePending_) {
        debuglog::WriteInfo(
            "[ui] deferring Helper cursor release while external owner is active: owner=\"%s\" chat=%d dialog=%d cefShown=%d sampMode=%s",
            inputs.externalOwnerName.empty() ? "<unknown>" : inputs.externalOwnerName.c_str(),
            inputs.chatOpen ? 1 : 0,
            inputs.dialogOpen ? 1 : 0,
            inputs.cefShown ? 1 : 0,
            ModeText(inputs.sampCursorMode).c_str());
    }
    deferredExternalReleasePending_ = true;
    deferredExternalReleaseFailureLogged_ = false;
    deferredExternalReleaseCleanupUntilMs_ = now + kDeferredExternalReleaseCleanupWindowMs;
}

void OverlayCursorController::ClearDeferredExternalRelease() {
    deferredExternalReleasePending_ = false;
    deferredExternalReleaseFailureLogged_ = false;
    deferredExternalReleaseCleanupUntilMs_ = 0;
}

bool OverlayCursorController::ApplySampCursorMode(int desiredMode, bool desiredEnabled, bool reassert, std::uint64_t now) {
    if (!sampApi_ || !sampApi_->sampModule() || !sampApi_->isSupportedVersion()) {
        if (now - lastUnavailableTraceMs_ >= kCursorUnavailableTraceIntervalMs) {
            lastUnavailableTraceMs_ = now;
            debuglog::WriteInfo(
                "[ui] cursor apply skipped: sampModule=%d supported=%d",
                (sampApi_ && sampApi_->sampModule()) ? 1 : 0,
                (sampApi_ && sampApi_->isSupportedVersion()) ? 1 : 0);
        }
        return false;
    }

    if (!sampApi_->Set_CursorMode(desiredMode, desiredEnabled)) {
        debuglog::WriteError(
            "[ui] Set_CursorMode FAILED want mode=%d en=%d: %s",
            desiredMode,
            desiredEnabled ? 1 : 0,
            sampApi_->lastError().c_str());
        return false;
    }

    bool shouldLogApply = true;
    if (reassert && cursorMode_ == desiredMode && cursorEnabled_ == desiredEnabled) {
        shouldLogApply = (now - lastReassertTraceMs_) >= kCursorReassertTraceIntervalMs;
        if (shouldLogApply) {
            lastReassertTraceMs_ = now;
        }
    }
    if (shouldLogApply) {
        debuglog::WriteInfo(
            "[ui] Set_CursorMode ok mode=%d en=%d (was %d / %d reassert=%d)",
            desiredMode,
            desiredEnabled ? 1 : 0,
            cursorMode_,
            cursorEnabled_ ? 1 : 0,
            reassert ? 1 : 0);
    }

    cursorMode_ = desiredMode;
    cursorEnabled_ = desiredEnabled;
    lastApplyMs_ = now;
    return true;
}

void OverlayCursorController::TraceState(const Inputs& inputs, const Result& result, bool rmbHeld, std::uint64_t now) {
    const bool changedCore = inputs.helperWantsCursor != traceHelperWantsCursor_
        || inputs.helperWantsInputRouting != traceHelperWantsRouting_
        || inputs.appHasFocus != traceFocus_
        || inputs.chatOpen != traceChatOpen_
        || inputs.dialogOpen != traceDialogOpen_
        || inputs.externalCursorActive != traceExternalActive_
        || inputs.cefControlled != traceCefControlled_
        || inputs.cefShown != traceCefShown_
        || inputs.cursorVisible != traceCursorVisible_
        || inputs.captureWindow != traceCaptureWindow_
        || result.owner != traceOwner_
        || result.reason != lastReason_;
    const bool changedRmbOnly = !changedCore && (rmbHeld != traceRmb_);
    const bool allowRmbSpamSafeTrace = changedRmbOnly && (now - lastCursorTraceMs_ >= kCursorTraceIntervalMs);
    if (!changedCore && !allowRmbSpamSafeTrace) {
        return;
    }

    traceHelperWantsCursor_ = inputs.helperWantsCursor;
    traceHelperWantsRouting_ = inputs.helperWantsInputRouting;
    traceFocus_ = inputs.appHasFocus;
    traceRmb_ = rmbHeld;
    traceChatOpen_ = inputs.chatOpen;
    traceDialogOpen_ = inputs.dialogOpen;
    traceExternalActive_ = inputs.externalCursorActive;
    traceCefControlled_ = inputs.cefControlled;
    traceCefShown_ = inputs.cefShown;
    traceCursorVisible_ = inputs.cursorVisible;
    traceCaptureWindow_ = inputs.captureWindow;
    traceOwner_ = result.owner;
    lastCursorTraceMs_ = now;

    debuglog::WriteInfo(
        "[ui] cursor owner=%s reason=%s route=%d helperCur=%d helperRoute=%d chat=%d dialog=%d external=%d extOwner=\"%s\" cefKnown=%d cefCtl=%d cefShown=%d osCursor=%d sampMode=%s helperMode=%d fg=%d rmb=%d gameHw=%p fgHw=%p capHw=%p capOwner=\"%s\" risks=\"%s\"",
        OwnerName(result.owner),
        result.reason.c_str(),
        result.routingAllowed ? 1 : 0,
        inputs.helperWantsCursor ? 1 : 0,
        inputs.helperWantsInputRouting ? 1 : 0,
        inputs.chatOpen ? 1 : 0,
        inputs.dialogOpen ? 1 : 0,
        inputs.externalCursorActive ? 1 : 0,
        inputs.externalOwnerName.c_str(),
        inputs.cefKnown ? 1 : 0,
        inputs.cefControlled ? 1 : 0,
        inputs.cefShown ? 1 : 0,
        inputs.cursorVisible ? 1 : 0,
        ModeText(inputs.sampCursorMode).c_str(),
        helperModeActive_ ? 1 : 0,
        inputs.appHasFocus ? 1 : 0,
        rmbHeld ? 1 : 0,
        inputs.gameWindow,
        inputs.foregroundWindow,
        inputs.captureWindow,
        inputs.captureOwnerModule.c_str(),
        inputs.riskModules.c_str());
}
