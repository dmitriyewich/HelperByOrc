#include "overlay_cursor_controller.h"

#include "debug_log.h"
#include "samp_api.h"

namespace {

constexpr int kSampCursorModeNone = 0;
constexpr int kSampCursorModeLockCamAndControl = 2;
constexpr std::uint64_t kCursorReassertIntervalMs = 200;
constexpr std::uint64_t kCursorReassertTraceIntervalMs = 2500;
constexpr std::uint64_t kCursorTraceIntervalMs = 700;
constexpr std::uint64_t kCursorUnavailableTraceIntervalMs = 1500;

} // namespace

void OverlayCursorController::SetSampApi(SampApi* sampApi) {
    sampApi_ = sampApi;
}

void OverlayCursorController::Apply(const Inputs& inputs) {
    const std::uint64_t now = ::GetTickCount64();
    if (!inputs.sampUiPipelineReady) {
        if (lastUiHold_) {
            ReleaseHold("[ui] cursor pipeline gated: released capture while SA:MP is not fully initialized");
        }
        if (cursorMode_ != kSampCursorModeNone || cursorEnabled_) {
            cursorMode_ = kSampCursorModeNone;
            cursorEnabled_ = false;
            lastApplyMs_ = now;
        }
        if (now - lastGateTraceMs_ >= kCursorUnavailableTraceIntervalMs) {
            lastGateTraceMs_ = now;
            debuglog::WriteInfo("[ui] cursor pipeline gated: waiting for full SA:MP initialization");
        }
        return;
    }

    const bool rmbHeld = (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const bool shouldHoldUi = inputs.appHasFocus && (inputs.wantsUiCursor || inputs.chatOpen || inputs.dialogOpen);
    TraceState(inputs, shouldHoldUi, rmbHeld, now);

    if (lastUiHold_ && !shouldHoldUi) {
        ReleaseHold("[ui] ReleaseCapture due to UI-hold end");
    }
    lastUiHold_ = shouldHoldUi;

    const int desiredMode = shouldHoldUi ? kSampCursorModeLockCamAndControl : kSampCursorModeNone;
    const bool desiredEnabled = shouldHoldUi;
    const bool desiredSameAsCache = cursorMode_ == desiredMode && cursorEnabled_ == desiredEnabled;
    const bool shouldReassert = desiredEnabled && (now - lastApplyMs_ >= kCursorReassertIntervalMs);

    if (desiredSameAsCache && !shouldReassert) {
        return;
    }

    if (!sampApi_ || !sampApi_->sampModule() || !sampApi_->isSupportedVersion()) {
        if (inputs.wantsUiCursor && now - lastUnavailableTraceMs_ >= kCursorUnavailableTraceIntervalMs) {
            lastUnavailableTraceMs_ = now;
            debuglog::WriteInfo(
                "[ui] cursor apply skipped: sampModule=%d supported=%d",
                (sampApi_ && sampApi_->sampModule()) ? 1 : 0,
                (sampApi_ && sampApi_->isSupportedVersion()) ? 1 : 0);
        }
        return;
    }

    if (!sampApi_->Set_CursorMode(desiredMode, desiredEnabled)) {
        debuglog::WriteError(
            "[ui] Set_CursorMode FAILED want mode=%d en=%d: %s",
            desiredMode,
            desiredEnabled ? 1 : 0,
            sampApi_->lastError().c_str());
        return;
    }

    bool shouldLogApply = true;
    if (shouldReassert && desiredSameAsCache) {
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
            shouldReassert ? 1 : 0);
    }

    cursorMode_ = desiredMode;
    cursorEnabled_ = desiredEnabled;
    lastApplyMs_ = now;
}

void OverlayCursorController::Shutdown() {
    if (lastUiHold_) {
        ReleaseHold("[ui] ReleaseCapture during cursor controller shutdown");
    } else {
        ::ReleaseCapture();
    }

    if (sampApi_ && sampApi_->sampModule() && sampApi_->isSupportedVersion()) {
        sampApi_->Set_CursorMode(kSampCursorModeNone, false);
    }

    cursorMode_ = kSampCursorModeNone;
    cursorEnabled_ = false;
    lastApplyMs_ = 0;
}

void OverlayCursorController::ReleaseHold(const char* reason) {
    ::ReleaseCapture();
    lastUiHold_ = false;
    if (reason && *reason) {
        debuglog::WriteInfo("%s", reason);
    }
}

void OverlayCursorController::TraceState(const Inputs& inputs, bool shouldHoldUi, bool rmbHeld, std::uint64_t now) {
    const bool changedCore = inputs.wantsUiCursor != traceWantsUi_
        || inputs.appHasFocus != traceFocus_
        || inputs.chatOpen != traceChatOpen_
        || inputs.dialogOpen != traceDialogOpen_
        || shouldHoldUi != traceHold_;
    const bool changedRmbOnly = !changedCore && (rmbHeld != traceRmb_);
    const bool allowRmbSpamSafeTrace = changedRmbOnly && (now - lastCursorTraceMs_ >= kCursorTraceIntervalMs);
    if (!changedCore && !allowRmbSpamSafeTrace) {
        return;
    }

    traceWantsUi_ = inputs.wantsUiCursor;
    traceFocus_ = inputs.appHasFocus;
    traceRmb_ = rmbHeld;
    traceChatOpen_ = inputs.chatOpen;
    traceDialogOpen_ = inputs.dialogOpen;
    traceHold_ = shouldHoldUi;
    lastCursorTraceMs_ = now;

    debuglog::WriteInfo(
        "[ui] cursor wantsUi=%d chatOpen=%d dialogOpen=%d chatOrDialog=%d fg=%d rmb=%d shouldHold=%d gameHw=%p fgHw=%p sampMode=%d sampEn=%d",
        inputs.wantsUiCursor ? 1 : 0,
        inputs.chatOpen ? 1 : 0,
        inputs.dialogOpen ? 1 : 0,
        (inputs.chatOpen || inputs.dialogOpen) ? 1 : 0,
        inputs.appHasFocus ? 1 : 0,
        rmbHeld ? 1 : 0,
        shouldHoldUi ? 1 : 0,
        inputs.gameWindow,
        inputs.foregroundWindow,
        cursorMode_,
        cursorEnabled_ ? 1 : 0);
}
