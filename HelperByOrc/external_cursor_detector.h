#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <optional>
#include <string>

class SampApi;

struct ExternalCursorSnapshot {
    std::optional<int> sampCursorMode{};
    bool chatOpen = false;
    bool dialogOpen = false;
    bool cursorVisible = false;
    HWND captureWindow = nullptr;
    std::string captureOwnerModule;
    bool cefKnown = false;
    bool cefControlled = false;
    bool cefShown = false;
    bool externalCursorActive = false;
    std::string externalOwnerName;
    std::string riskModules;
};

class ExternalCursorDetector {
public:
    ExternalCursorSnapshot Detect(SampApi& sampApi, HWND gameWindow, HWND foregroundWindow);

private:
    std::string lastRiskModules_;
};
