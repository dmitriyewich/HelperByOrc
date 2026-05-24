#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>

class SampApi;

class OverlayCursorController {
public:
    struct Inputs {
        bool sampUiPipelineReady = false;
        bool wantsUiCursor = false;
        bool chatOpen = false;
        bool dialogOpen = false;
        bool appHasFocus = false;
        HWND gameWindow = nullptr;
        HWND foregroundWindow = nullptr;
    };

    void SetSampApi(SampApi* sampApi);
    void Apply(const Inputs& inputs);
    void Shutdown();

private:
    void ReleaseHold(const char* reason);
    void TraceState(const Inputs& inputs, bool shouldHoldUi, bool rmbHeld, std::uint64_t now);

    SampApi* sampApi_ = nullptr;
    int cursorMode_ = -1;
    bool cursorEnabled_ = false;
    bool lastUiHold_ = false;
    std::uint64_t lastApplyMs_ = 0;
    std::uint64_t lastGateTraceMs_ = 0;
    std::uint64_t lastUnavailableTraceMs_ = 0;
    std::uint64_t lastReassertTraceMs_ = 0;
    bool traceWantsUi_ = false;
    bool traceFocus_ = false;
    bool traceRmb_ = false;
    bool traceChatOpen_ = false;
    bool traceDialogOpen_ = false;
    bool traceHold_ = false;
    std::uint64_t lastCursorTraceMs_ = 0;
};
