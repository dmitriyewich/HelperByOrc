#include "external_cursor_detector.h"

#include "debug_log.h"
#include "samp_api.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string_view>

namespace {

using CefProbeFn = int(__cdecl*)();

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string ModulePath(HMODULE module) {
    char path[MAX_PATH]{};
    if (!module || !GetModuleFileNameA(module, path, MAX_PATH)) {
        return {};
    }
    return path;
}

std::string BaseName(std::string_view path) {
    const std::size_t slash = path.find_last_of("\\/");
    if (slash == std::string_view::npos) {
        return std::string(path);
    }
    return std::string(path.substr(slash + 1));
}

bool IsCefLoaderModule(HMODULE module) {
    if (!module) {
        return false;
    }

    const std::string path = LowerAscii(ModulePath(module));
    return path.find("\\cef\\loader.dll") != std::string::npos
        || (GetProcAddress(module, "isAnyControlled") != nullptr
            && GetProcAddress(module, "getBrowserShowState") != nullptr
            && GetProcAddress(module, "getBrowserControlState") != nullptr);
}

bool CallCefProbe(FARPROC proc, int& value) {
    if (!proc) {
        return false;
    }

    __try {
        value = reinterpret_cast<CefProbeFn>(proc)();
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        value = 0;
        return false;
    }
}

std::string ModuleFromAddress(std::uintptr_t address) {
    if (address == 0) {
        return {};
    }

    HMODULE module = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(address),
            &module)
        || !module) {
        return {};
    }

    return BaseName(ModulePath(module));
}

std::string WindowSummary(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return {};
    }

    char className[96]{};
    char title[96]{};
    GetClassNameA(hwnd, className, static_cast<int>(sizeof(className)));
    GetWindowTextA(hwnd, title, static_cast<int>(sizeof(title)));

    std::string procModule;
    const LONG_PTR wndProc = GetWindowLongPtrA(hwnd, GWLP_WNDPROC);
    if (wndProc != 0) {
        procModule = ModuleFromAddress(static_cast<std::uintptr_t>(wndProc));
    }

    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(hwnd, &pid);

    char buffer[512]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "hwnd=%p class=\"%s\" title=\"%s\" tid=%lu pid=%lu wndprocModule=\"%s\"",
        hwnd,
        className,
        title,
        static_cast<unsigned long>(tid),
        static_cast<unsigned long>(pid),
        procModule.c_str());
    return buffer;
}

std::string LoadedRiskModules() {
    struct RiskModule {
        const char* name;
        const char* tag;
    };

    static constexpr RiskModule kRiskModules[] = {
        { "SAMPFUNCS.asi", "sampfuncs" },
        { "MoonLoader.asi", "moonloader" },
        { "_chat.asi", "chat-hook" },
        { "chat.asi", "chat-hook" },
        { "mousefix.asi", "mousefix" },
        { "libcef.asi", "cef-ui" },
        { "loader.dll", "cef-loader" },
    };

    std::string result;
    for (const RiskModule& item : kRiskModules) {
        HMODULE module = GetModuleHandleA(item.name);
        if (!module) {
            continue;
        }
        if (std::string_view(item.name) == "loader.dll" && !IsCefLoaderModule(module)) {
            continue;
        }

        if (!result.empty()) {
            result += ',';
        }
        result += item.tag;
        result += ':';
        result += BaseName(ModulePath(module));
    }

    return result;
}

void ProbeCef(ExternalCursorSnapshot& snapshot) {
    HMODULE loader = GetModuleHandleA("loader.dll");
    if (!IsCefLoaderModule(loader)) {
        return;
    }

    snapshot.cefKnown = true;

    int value = 0;
    if (CallCefProbe(GetProcAddress(loader, "isAnyControlled"), value)) {
        snapshot.cefControlled = value != 0;
    }
    if (CallCefProbe(GetProcAddress(loader, "getBrowserShowState"), value)) {
        snapshot.cefShown = value != 0;
    }
    if (!snapshot.cefControlled && CallCefProbe(GetProcAddress(loader, "getBrowserControlState"), value)) {
        snapshot.cefControlled = value != 0;
    }
}

} // namespace

ExternalCursorSnapshot ExternalCursorDetector::Detect(SampApi& sampApi, HWND gameWindow, HWND foregroundWindow) {
    ExternalCursorSnapshot snapshot{};

    if (sampApi.sampModule() && sampApi.isSupportedVersion()) {
        snapshot.sampCursorMode = sampApi.GetCursorMode();
        snapshot.chatOpen = sampApi.is_chat_opened();
        snapshot.dialogOpen = sampApi.isDialogActive();
    }

    CURSORINFO cursorInfo{};
    cursorInfo.cbSize = sizeof(cursorInfo);
    if (GetCursorInfo(&cursorInfo) != FALSE) {
        snapshot.cursorVisible = (cursorInfo.flags & CURSOR_SHOWING) != 0;
    }

    snapshot.captureWindow = GetCapture();
    if (snapshot.captureWindow) {
        snapshot.captureOwnerModule = WindowSummary(snapshot.captureWindow);
    }

    ProbeCef(snapshot);
    snapshot.riskModules = LoadedRiskModules();

    if (snapshot.riskModules != lastRiskModules_) {
        lastRiskModules_ = snapshot.riskModules;
        debuglog::WriteInfo(
            "[ui] cursor risk modules changed: %s",
            snapshot.riskModules.empty() ? "<none>" : snapshot.riskModules.c_str());
    }

    if (snapshot.chatOpen) {
        snapshot.externalCursorActive = true;
        snapshot.externalOwnerName = "samp-chat";
    } else if (snapshot.dialogOpen) {
        snapshot.externalCursorActive = true;
        snapshot.externalOwnerName = "samp-dialog";
    } else if (snapshot.cefShown) {
        snapshot.externalCursorActive = true;
        snapshot.externalOwnerName = "arizona-cef-visible";
    } else if (snapshot.captureWindow && snapshot.captureWindow != gameWindow) {
        snapshot.externalCursorActive = true;
        snapshot.externalOwnerName = "foreign-capture";
    }

    if (foregroundWindow && gameWindow && foregroundWindow != gameWindow && IsChild(gameWindow, foregroundWindow) != FALSE
        && snapshot.externalOwnerName.empty()) {
        snapshot.externalOwnerName = "game-child-window";
    }

    return snapshot;
}
