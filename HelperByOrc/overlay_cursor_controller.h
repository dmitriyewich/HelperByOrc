#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>

class SampApi;

class OverlayCursorController {
public:
    enum class Owner {
        Unavailable,
        Game,
        Helper,
        External,
    };

    struct Inputs {
        bool sampUiPipelineReady = false;
        bool helperWantsCursor = false;
        bool helperWantsInputRouting = false;
        std::optional<int> sampCursorMode{};
        bool chatOpen = false;
        bool dialogOpen = false;
        bool externalCursorActive = false;
        std::string externalOwnerName;
        bool cursorVisible = false;
        HWND captureWindow = nullptr;
        std::string captureOwnerModule;
        bool cefKnown = false;
        bool cefControlled = false;
        bool cefShown = false;
        std::string riskModules;
        bool appHasFocus = false;
        HWND gameWindow = nullptr;
        HWND foregroundWindow = nullptr;
    };

    struct Result {
        Owner owner = Owner::Unavailable;
        bool routingAllowed = false;
        bool sampModeApplied = false;
        std::string reason;
    };

    void SetSampApi(SampApi* sampApi);
    Result Apply(const Inputs& inputs);
    void Shutdown();

private:
    static const char* OwnerName(Owner owner);
    void ReleaseHold(const char* reason);
    void DeferHelperReleaseForExternal(const Inputs& inputs, std::uint64_t now);
    void ClearDeferredExternalRelease();
    bool ApplySampCursorMode(int desiredMode, bool desiredEnabled, bool reassert, std::uint64_t now);
    void TraceState(const Inputs& inputs, const Result& result, bool rmbHeld, std::uint64_t now);

    SampApi* sampApi_ = nullptr;
    int cursorMode_ = -1;
    bool cursorEnabled_ = false;
    bool helperModeActive_ = false;
    bool lastUiHold_ = false;
    bool deferredExternalReleasePending_ = false;
    bool deferredExternalReleaseFailureLogged_ = false;
    Owner lastOwner_ = Owner::Unavailable;
    std::string lastReason_;
    std::uint64_t lastApplyMs_ = 0;
    std::uint64_t deferredExternalReleaseCleanupUntilMs_ = 0;
    std::uint64_t lastGateTraceMs_ = 0;
    std::uint64_t lastUnavailableTraceMs_ = 0;
    std::uint64_t lastReassertTraceMs_ = 0;
    bool traceHelperWantsCursor_ = false;
    bool traceHelperWantsRouting_ = false;
    bool traceFocus_ = false;
    bool traceRmb_ = false;
    bool traceChatOpen_ = false;
    bool traceDialogOpen_ = false;
    bool traceExternalActive_ = false;
    bool traceCefControlled_ = false;
    bool traceCefShown_ = false;
    bool traceCursorVisible_ = false;
    HWND traceCaptureWindow_ = nullptr;
    Owner traceOwner_ = Owner::Unavailable;
    std::uint64_t lastCursorTraceMs_ = 0;
};
