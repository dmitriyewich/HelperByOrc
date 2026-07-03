#include "imgui_overlay.h"

#include "debug_log.h"
#include "font_awesome7_data.h"
#include "icon_registry.h"
#include "minhook_utils.h"
#include "ui_icons.h"
#include "ui_settings.h"

#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>

#include <windowsx.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

namespace {

constexpr char kDummyWindowClassName[] = "HelperByOrcDummyWindow";
constexpr float kOverlayFontSize = 18.0f;

enum class UiDebugProfile {
    ProductionDebug,
    VerboseAudit,
};

constexpr UiDebugProfile kUiDebugProfile = UiDebugProfile::ProductionDebug;
constexpr bool kVerboseUiTraceEnabled = kUiDebugProfile == UiDebugProfile::VerboseAudit;
constexpr uint64_t kDeepUiTraceIntervalMs = 400;
constexpr uint64_t kAnomalyClickTraceIntervalMs = 150;
constexpr uint64_t kPostRenderHealthTraceIntervalMs =
    (kUiDebugProfile == UiDebugProfile::ProductionDebug) ? 5000 : 3500;
constexpr uint64_t kResetTraceIntervalMs = 1000;
constexpr uint64_t kStateBlockFailTraceIntervalMs = 1000;
constexpr uint64_t kSetCursorTraceIntervalMs = 1500;
constexpr uint64_t kWheelTraceIntervalMs = 100;
constexpr uint64_t kWndProcTraceIntervalMs = 1500;
constexpr float kHelperWindowHitTestPadding = 4.0f;
constexpr uint64_t kRenderStatsTraceIntervalMs =
    (kUiDebugProfile == UiDebugProfile::ProductionDebug) ? 5000 : 1000;
constexpr uint64_t kNonPrimarySkipTraceIntervalMs = 1500;
constexpr uint64_t kSlowFrameTraceThresholdMs =
    (kUiDebugProfile == UiDebugProfile::ProductionDebug) ? 40 : 8;
constexpr uintptr_t kGtaWindowHandleAddress = 0x00C8CF88u;

bool TryGetModuleForAddress(const void* address, HMODULE* outModule, WCHAR* outPath, DWORD outPathCapacity) {
    if (outModule) {
        *outModule = nullptr;
    }
    if (outPath && outPathCapacity > 0) {
        outPath[0] = L'\0';
    }

    if (!address) {
        return false;
    }

    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address),
            &module)) {
        return false;
    }

    if (outModule) {
        *outModule = module;
    }
    if (outPath && outPathCapacity > 0) {
        GetModuleFileNameW(module, outPath, outPathCapacity);
    }
    return true;
}

const WCHAR* BaseNameFromPath(const WCHAR* path) {
    if (!path) {
        return L"";
    }

    const WCHAR* baseName = path;
    for (const WCHAR* cursor = path; *cursor; ++cursor) {
        if (*cursor == L'\\' || *cursor == L'/') {
            baseName = cursor + 1;
        }
    }
    return baseName;
}

bool IsAddressInModuleNamed(const void* address, const WCHAR* expectedBaseName, WCHAR* outPath, DWORD outPathCapacity) {
    HMODULE module = nullptr;
    if (!TryGetModuleForAddress(address, &module, outPath, outPathCapacity)) {
        return false;
    }

    return lstrcmpiW(BaseNameFromPath(outPath), expectedBaseName) == 0;
}

void TraceModuleForAddress(const char* label, const void* address) {
    HMODULE module = nullptr;
    WCHAR path[MAX_PATH]{};
    if (TryGetModuleForAddress(address, &module, path, static_cast<DWORD>(std::size(path)))) {
        const auto rva = static_cast<unsigned long long>(
            reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module));
        debuglog::WriteInfo("[ui][d3d] target %s=%p module=%ls rva=0x%llX", label, address, path, rva);
        return;
    }

    debuglog::WriteInfo("[ui][d3d] target %s=%p module=<unknown>", label, address);
}

std::string WideToUtf8Lossy(const WCHAR* text) {
    if (!text || !*text) {
        return {};
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }

    std::string result(static_cast<std::size_t>(required - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), required, nullptr, nullptr);
    return result;
}

std::string ModuleNameForAddress(const void* address) {
    HMODULE module = nullptr;
    WCHAR path[MAX_PATH]{};
    if (!TryGetModuleForAddress(address, &module, path, static_cast<DWORD>(std::size(path)))) {
        return address ? "<unknown>" : "<null>";
    }
    return WideToUtf8Lossy(BaseNameFromPath(path));
}

HWND ReadGtaWindowHandle() {
    HWND hwnd = nullptr;
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(
            GetCurrentProcess(),
            reinterpret_cast<LPCVOID>(kGtaWindowHandleAddress),
            &hwnd,
            sizeof(hwnd),
            &bytesRead)
        || bytesRead != sizeof(hwnd)) {
        return nullptr;
    }
    return IsWindow(hwnd) ? hwnd : nullptr;
}

void TraceWindowDetails(const char* source, HWND hwnd) {
    WCHAR title[256]{};
    WCHAR className[128]{};
    DWORD processId = 0;
    const DWORD threadId = GetWindowThreadProcessId(hwnd, &processId);
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));
    debuglog::WriteInfo(
        "[ui] window source=%s hwnd=%p class=%ls title=%ls pid=%lu tid=%lu valid=%d",
        source ? source : "<null>",
        hwnd,
        className,
        title,
        processId,
        threadId,
        IsWindow(hwnd) ? 1 : 0);
}

void TraceRenderPathCounters(const char* sourceTag, bool rendered) {
    if constexpr (!kVerboseUiTraceEnabled) {
        (void)sourceTag;
        (void)rendered;
        return;
    }

    static uint64_t s_windowStartMs = 0;
    static unsigned s_presentCalls = 0;
    static unsigned s_endSceneCalls = 0;
    static unsigned s_renderFromPresent = 0;
    static unsigned s_renderFromEndScene = 0;
    static unsigned s_skippedByNonPrimary = 0;

    const uint64_t now = GetTickCount64();
    if (s_windowStartMs == 0) {
        s_windowStartMs = now;
    }

    const bool fromPresent = std::strcmp(sourceTag, "present") == 0;
    if (fromPresent) {
        ++s_presentCalls;
        if (rendered) {
            ++s_renderFromPresent;
        }
    } else {
        ++s_endSceneCalls;
        if (rendered) {
            ++s_renderFromEndScene;
        }
    }
    if (!rendered) {
        ++s_skippedByNonPrimary;
    }

    if (now - s_windowStartMs >= kRenderStatsTraceIntervalMs) {
        debuglog::WriteInfo(
            "[ui] render-stats 1s presentCalls=%u endSceneCalls=%u renderedPresent=%u renderedEndScene=%u skippedNonPrimary=%u",
            s_presentCalls,
            s_endSceneCalls,
            s_renderFromPresent,
            s_renderFromEndScene,
            s_skippedByNonPrimary);
        s_presentCalls = 0;
        s_endSceneCalls = 0;
        s_renderFromPresent = 0;
        s_renderFromEndScene = 0;
        s_skippedByNonPrimary = 0;
        s_windowStartMs = now;
    }
}

double PerfNowMs() {
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

struct UiFramePerf {
    bool fullUi = false;
    bool drawSkipped = false;
    double stateBackupMs = 0.0;
    double backendNewFrameMs = 0.0;
    double prepareFrameMs = 0.0;
    double renderUiMs = 0.0;
    double imguiRenderMs = 0.0;
    double renderDrawMs = 0.0;
    double stateRestoreMs = 0.0;
    double totalMs = 0.0;
    int windows = 0;
    int vertices = 0;
    int indices = 0;
    int cmdLists = 0;
};

void CaptureDrawDataStats(UiFramePerf& perf, ImDrawData* drawData) {
    if (GImGui != nullptr) {
        perf.windows = GImGui->Windows.Size;
    }
    if (!drawData) {
        return;
    }
    perf.vertices = drawData->TotalVtxCount;
    perf.indices = drawData->TotalIdxCount;
    perf.cmdLists = drawData->CmdListsCount;
}

void AccumulateUiFramePerf(const UiFramePerf& perf, bool slow) {
    static uint64_t s_windowStartMs = 0;
    static unsigned s_fullFrames = 0;
    static unsigned s_idleFrames = 0;
    static unsigned s_slowFrames = 0;
    static unsigned s_drawSkippedFrames = 0;
    static double s_fullTotalMs = 0.0;
    static double s_idleTotalMs = 0.0;
    static double s_maxFullMs = 0.0;
    static double s_maxIdleMs = 0.0;
    static double s_maxStateBackupMs = 0.0;
    static double s_maxBackendNewFrameMs = 0.0;
    static double s_maxPrepareFrameMs = 0.0;
    static double s_maxRenderUiMs = 0.0;
    static double s_maxImGuiRenderMs = 0.0;
    static double s_maxRenderDrawMs = 0.0;
    static double s_maxStateRestoreMs = 0.0;
    static int s_maxWindows = 0;
    static int s_maxVertices = 0;
    static int s_maxIndices = 0;
    static int s_maxCmdLists = 0;

    const uint64_t now = GetTickCount64();
    if (s_windowStartMs == 0) {
        s_windowStartMs = now;
    }

    if (perf.fullUi) {
        ++s_fullFrames;
        s_fullTotalMs += perf.totalMs;
        s_maxFullMs = std::max(s_maxFullMs, perf.totalMs);
    } else {
        ++s_idleFrames;
        s_idleTotalMs += perf.totalMs;
        s_maxIdleMs = std::max(s_maxIdleMs, perf.totalMs);
    }
    if (slow) {
        ++s_slowFrames;
    }
    if (perf.drawSkipped) {
        ++s_drawSkippedFrames;
    }

    s_maxStateBackupMs = std::max(s_maxStateBackupMs, perf.stateBackupMs);
    s_maxBackendNewFrameMs = std::max(s_maxBackendNewFrameMs, perf.backendNewFrameMs);
    s_maxPrepareFrameMs = std::max(s_maxPrepareFrameMs, perf.prepareFrameMs);
    s_maxRenderUiMs = std::max(s_maxRenderUiMs, perf.renderUiMs);
    s_maxImGuiRenderMs = std::max(s_maxImGuiRenderMs, perf.imguiRenderMs);
    s_maxRenderDrawMs = std::max(s_maxRenderDrawMs, perf.renderDrawMs);
    s_maxStateRestoreMs = std::max(s_maxStateRestoreMs, perf.stateRestoreMs);
    s_maxWindows = std::max(s_maxWindows, perf.windows);
    s_maxVertices = std::max(s_maxVertices, perf.vertices);
    s_maxIndices = std::max(s_maxIndices, perf.indices);
    s_maxCmdLists = std::max(s_maxCmdLists, perf.cmdLists);

    if (now - s_windowStartMs < kRenderStatsTraceIntervalMs) {
        return;
    }

    if (s_fullFrames > 0 || s_slowFrames > 0) {
        debuglog::WriteInfo(
            "[ui][perf] 5s full=%u idle=%u slow=%u drawSkip=%u avgFull=%.2fms maxFull=%.2fms avgIdle=%.2fms maxIdle=%.2fms maxState=%.2fms maxNew=%.2fms maxPrep=%.2fms maxUi=%.2fms maxRender=%.2fms maxDraw=%.2fms maxRestore=%.2fms maxWin=%d maxVtx=%d maxIdx=%d maxCmdLists=%d",
            s_fullFrames,
            s_idleFrames,
            s_slowFrames,
            s_drawSkippedFrames,
            s_fullFrames > 0 ? s_fullTotalMs / static_cast<double>(s_fullFrames) : 0.0,
            s_maxFullMs,
            s_idleFrames > 0 ? s_idleTotalMs / static_cast<double>(s_idleFrames) : 0.0,
            s_maxIdleMs,
            s_maxStateBackupMs,
            s_maxBackendNewFrameMs,
            s_maxPrepareFrameMs,
            s_maxRenderUiMs,
            s_maxImGuiRenderMs,
            s_maxRenderDrawMs,
            s_maxStateRestoreMs,
            s_maxWindows,
            s_maxVertices,
            s_maxIndices,
            s_maxCmdLists);
    }

    s_windowStartMs = now;
    s_fullFrames = 0;
    s_idleFrames = 0;
    s_slowFrames = 0;
    s_drawSkippedFrames = 0;
    s_fullTotalMs = 0.0;
    s_idleTotalMs = 0.0;
    s_maxFullMs = 0.0;
    s_maxIdleMs = 0.0;
    s_maxStateBackupMs = 0.0;
    s_maxBackendNewFrameMs = 0.0;
    s_maxPrepareFrameMs = 0.0;
    s_maxRenderUiMs = 0.0;
    s_maxImGuiRenderMs = 0.0;
    s_maxRenderDrawMs = 0.0;
    s_maxStateRestoreMs = 0.0;
    s_maxWindows = 0;
    s_maxVertices = 0;
    s_maxIndices = 0;
    s_maxCmdLists = 0;
}

bool IsPopupTransitionNoCaptureExpected(const ImGuiIO& io) {
    if (GImGui == nullptr) {
        return false;
    }
    const ImGuiContext& g = *GImGui;
    // Классический переходный кадр popup: popup уже открыт, BeginPopup ещё не зашёл в окно.
    // В этот момент WantCaptureMouse может быть 0 при активном UI-курсоре.
    return g.OpenPopupStack.Size > 0
        && g.BeginPopupStack.Size == 0
        && !io.MouseDown[0]
        && !ImGui::IsAnyItemHovered()
        && !ImGui::IsAnyItemActive();
}

bool HasMouseButtonDown(const ImGuiIO& io) {
    for (bool down : io.MouseDown) {
        if (down) {
            return true;
        }
    }
    return false;
}

std::uint32_t MouseLatchMaskForMessage(UINT message, WPARAM wparam) {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
        return 0x01u;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_RBUTTONUP:
        return 0x02u;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_MBUTTONUP:
        return 0x04u;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
    case WM_XBUTTONUP:
        return GET_XBUTTON_WPARAM(wparam) == XBUTTON2 ? 0x10u : 0x08u;
    default:
        return 0;
    }
}

bool IsMouseButtonDownMessage(UINT message) {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
        return true;
    default:
        return false;
    }
}

bool IsMouseButtonUpMessage(UINT message) {
    switch (message) {
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
        return true;
    default:
        return false;
    }
}

bool WindowOrAncestorHasNoMouseInputs(const ImGuiWindow* window) {
    for (const ImGuiWindow* current = window; current != nullptr; current = current->ParentWindow) {
        if ((current->Flags & ImGuiWindowFlags_NoMouseInputs) != 0) {
            return true;
        }
    }
    return false;
}

void TraceWindowAndDpi(HWND hwnd) {
    UINT dpi = 96;
    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32 != nullptr) {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
        if (getDpiForWindow != nullptr && hwnd != nullptr) {
            dpi = getDpiForWindow(hwnd);
        }
    }
    debuglog::WriteInfo("[ui] game window resolved hwnd=%p dpi=%u", hwnd, dpi);
    TraceWindowDetails("resolved", hwnd);
}

const char* DbgImGuiWindowName(const ImGuiWindow* w) {
    if (w == nullptr || w->Name == nullptr) {
        return "(null)";
    }
    return w->Name;
}

void TraceWheelMessage(UINT message, WPARAM wparam, bool wantsUiCursor, const ImGuiIO& io) {
    if (!wantsUiCursor || (message != WM_MOUSEWHEEL && message != WM_MOUSEHWHEEL)) {
        return;
    }

    static uint64_t s_lastWheelTraceMs = 0;
    const uint64_t now = GetTickCount64();
    if (now - s_lastWheelTraceMs < kWheelTraceIntervalMs) {
        return;
    }
    s_lastWheelTraceMs = now;

    const ImGuiContext* ctx = GImGui;
    const ImGuiWindow* hoveredWindow = ctx ? ctx->HoveredWindow : nullptr;
    const ImGuiWindow* wheelingWindow = ctx ? ctx->WheelingWindow : nullptr;
    debuglog::WriteInfo(
        "[ui] wheel msg=%u delta=%d wantsUiCur=%d WantCapMouse=%d HovWin=\"%s\" WheelWin=\"%s\" OpenPop=%d BeginPop=%d ioWheel=(%.2f,%.2f)",
        static_cast<unsigned>(message),
        static_cast<int>(GET_WHEEL_DELTA_WPARAM(wparam)),
        wantsUiCursor ? 1 : 0,
        io.WantCaptureMouse ? 1 : 0,
        DbgImGuiWindowName(hoveredWindow),
        DbgImGuiWindowName(wheelingWindow),
        ctx ? ctx->OpenPopupStack.Size : -1,
        ctx ? ctx->BeginPopupStack.Size : -1,
        io.MouseWheelH,
        io.MouseWheel);
}

void DestroyDummyWindow(HWND window, bool registeredWindowClass) {
    if (window) {
        DestroyWindow(window);
    }

    if (registeredWindowClass) {
        SetLastError(0);
        if (!UnregisterClassA(kDummyWindowClassName, GetModuleHandleA(nullptr)) && GetLastError() != ERROR_CLASS_DOES_NOT_EXIST) {
            debuglog::WriteError("[ui][d3d] UnregisterClassA(%s) failed: %lu", kDummyWindowClassName, GetLastError());
        }
    }
}

void DbgTraceImGuiInternalState(const char* reason) {
    if (GImGui == nullptr) {
        debuglog::WriteError("[ui][dbg] %s: GImGui=null", reason);
        return;
    }
    ImGuiContext& g = *GImGui;
    const ImGuiIO& io = g.IO;
    const bool mouseValid = ImGui::IsMousePosValid(&io.MousePos);
    const bool popupAny =
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const ImGuiWindow* wheel = g.WheelingWindow;

    debuglog::WriteInfo(
        "[ui][dbg] %s mp=(%.1f,%.1f) mpValid=%d dsp=(%.1f,%.1f) fontScale=%.3f WantCapM=%d WantCapK=%d "
        "NavWin=\"%s\" HovWin=\"%s\" HovUnderMove=\"%s\" MoveWin=\"%s\" WheelWin=\"%s\" "
        "popAny=%d OpenPop=%d BeginPop=%d ActId=0x%08X HovId=0x%08X NavId=0x%08X NavHighlightUnderNav=%d",
        reason,
        io.MousePos.x,
        io.MousePos.y,
        mouseValid ? 1 : 0,
        io.DisplaySize.x,
        io.DisplaySize.y,
        io.FontGlobalScale,
        io.WantCaptureMouse ? 1 : 0,
        io.WantCaptureKeyboard ? 1 : 0,
        DbgImGuiWindowName(g.NavWindow),
        DbgImGuiWindowName(g.HoveredWindow),
        DbgImGuiWindowName(g.HoveredWindowUnderMovingWindow),
        DbgImGuiWindowName(g.MovingWindow),
        DbgImGuiWindowName(wheel),
        popupAny ? 1 : 0,
        g.OpenPopupStack.Size,
        g.BeginPopupStack.Size,
        static_cast<unsigned>(g.ActiveId),
        static_cast<unsigned>(g.HoveredId),
        static_cast<unsigned>(g.NavId),
        g.NavHighlightItemUnderNav ? 1 : 0);
}

bool MergeFontAwesomeIcons(ImGuiIO& io) {
    ImFontConfig iconConfig{};
    iconConfig.MergeMode = true;
    iconConfig.PixelSnapH = true;

    ImFont* solidIcons = io.Fonts->AddFontFromMemoryCompressedTTF(
        FontAwesome7Data::kSolidCompressedData,
        static_cast<int>(FontAwesome7Data::kSolidCompressedSize),
        kOverlayFontSize,
        &iconConfig,
        icon_registry::SolidRanges());
    if (!solidIcons) {
        debuglog::WriteError("Failed to merge Font Awesome 7 solid icon font");
        return false;
    }

    ImFont* brandsIcons = io.Fonts->AddFontFromMemoryCompressedTTF(
        FontAwesome7Data::kBrandsCompressedData,
        static_cast<int>(FontAwesome7Data::kBrandsCompressedSize),
        kOverlayFontSize,
        &iconConfig,
        icon_registry::BrandsRanges());
    if (!brandsIcons) {
        debuglog::WriteError("Failed to merge Font Awesome 7 brands icon font");
        return false;
    }

    debuglog::WriteInfo("Merged Font Awesome 7 solid and brands icon fonts");
    return true;
}

ImFont* LoadOverlayFont(ImGuiIO& io) {
    static constexpr const char* kFontCandidates[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
    };

    const ImWchar* glyphRanges = io.Fonts->GetGlyphRangesCyrillic();
    for (const char* path : kFontCandidates) {
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
            continue;
        }

        ImFont* font = io.Fonts->AddFontFromFileTTF(path, kOverlayFontSize, nullptr, glyphRanges);
        if (font) {
            debuglog::WriteInfo("Loaded ImGui font: %s", path);
            MergeFontAwesomeIcons(io);
            return font;
        }
    }

    debuglog::WriteError("Failed to load a Cyrillic-capable ImGui font, using default font");
    ImFontConfig fallbackConfig{};
    fallbackConfig.SizePixels = kOverlayFontSize;
    ImFont* fallbackFont = io.Fonts->AddFontDefault(&fallbackConfig);
    MergeFontAwesomeIcons(io);
    return fallbackFont;
}

} // namespace

HRESULT __stdcall ImGuiOverlay::EndSceneDetour(IDirect3DDevice9* device) {
    if (self_ && !self_->shuttingDown_) {
        if constexpr (kVerboseUiTraceEnabled) {
            static uint64_t s_lastEndSceneProbeMs = 0;
            const uint64_t probeNow = GetTickCount64();
            if (probeNow - s_lastEndSceneProbeMs >= 2000) {
                s_lastEndSceneProbeMs = probeNow;
                debuglog::WriteInfo(
                    "[ui][probe] EndScene ts=%llums init=%d gate=%d",
                    static_cast<unsigned long long>(probeNow),
                    self_->imguiInitialized_ ? 1 : 0,
                    self_->IsInputPipelineEnabled() ? 1 : 0);
            }
        }
        self_->InitializeImGuiIfNeeded(device);
        // Fallback path only: when Present detour is unavailable, render from EndScene.
        const bool isPrimary = self_->IsPrimaryRenderTarget(device);
        const bool rendered = !originalPresent_ && isPrimary;
        if (rendered) {
            self_->UpdateHotkeyState();
            if (self_->updateCallback_) {
                self_->updateCallback_();
            }
            self_->RenderFrame(device);
        }
        TraceRenderPathCounters("endscene", rendered);
        if (!rendered) {
            static uint64_t s_lastSkipTraceMs = 0;
            const uint64_t now = GetTickCount64();
            const bool shouldLogSkip = kVerboseUiTraceEnabled && !isPrimary;
            if (shouldLogSkip && now - s_lastSkipTraceMs >= kNonPrimarySkipTraceIntervalMs) {
                s_lastSkipTraceMs = now;
                debuglog::WriteInfo(
                    "[ui] EndScene render skipped presentHook=%d primary=%d",
                    originalPresent_ ? 1 : 0,
                    isPrimary ? 1 : 0);
            }
        }
    } else if (self_ && self_->shuttingDown_) {
        static bool s_reportedEndSceneDuringShutdown = false;
        if (!s_reportedEndSceneDuringShutdown) {
            s_reportedEndSceneDuringShutdown = true;
            debuglog::WriteInfo("[ui] EndSceneDetour observed after shutdown flag");
        }
    }

    return originalEndScene_ ? originalEndScene_(device) : D3D_OK;
}

HRESULT __stdcall ImGuiOverlay::PresentDetour(
    IDirect3DDevice9* device,
    const RECT* sourceRect,
    const RECT* destRect,
    HWND overrideWindow,
    const RGNDATA* dirtyRegion) {
    if (self_ && !self_->shuttingDown_) {
        if constexpr (kVerboseUiTraceEnabled) {
            static uint64_t s_lastPresentProbeMs = 0;
            const uint64_t probeNow = GetTickCount64();
            if (probeNow - s_lastPresentProbeMs >= 2000) {
                s_lastPresentProbeMs = probeNow;
                debuglog::WriteInfo(
                    "[ui][probe] Present ts=%llums init=%d gate=%d",
                    static_cast<unsigned long long>(probeNow),
                    self_->imguiInitialized_ ? 1 : 0,
                    self_->IsInputPipelineEnabled() ? 1 : 0);
            }
        }
        self_->InitializeImGuiIfNeeded(device);
        // Primary and stable render path for overlay.
        const bool isPrimary = self_->IsPrimaryRenderTarget(device);
        const bool rendered = isPrimary;
        if (rendered) {
            self_->UpdateHotkeyState();
            if (self_->updateCallback_) {
                self_->updateCallback_();
            }
            self_->RenderFrame(device);
        }
        TraceRenderPathCounters("present", rendered);
        if (!rendered) {
            static uint64_t s_lastSkipTraceMs = 0;
            const uint64_t now = GetTickCount64();
            if (now - s_lastSkipTraceMs >= kNonPrimarySkipTraceIntervalMs) {
                s_lastSkipTraceMs = now;
                debuglog::WriteInfo("[ui] Present render skipped due to non-primary target");
            }
        }
    } else if (self_ && self_->shuttingDown_) {
        static bool s_reportedPresentDuringShutdown = false;
        if (!s_reportedPresentDuringShutdown) {
            s_reportedPresentDuringShutdown = true;
            debuglog::WriteInfo("[ui] PresentDetour observed after shutdown flag");
        }
    }

    return originalPresent_ ? originalPresent_(device, sourceRect, destRect, overrideWindow, dirtyRegion) : D3D_OK;
}

HRESULT __stdcall ImGuiOverlay::ResetDetour(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* presentationParameters) {
    if (self_ && self_->imguiInitialized_) {
        static uint64_t s_lastResetBeginTraceMs = 0;
        const uint64_t now = GetTickCount64();
        if (now - s_lastResetBeginTraceMs >= kResetTraceIntervalMs) {
            s_lastResetBeginTraceMs = now;
            debuglog::WriteInfo("[ui] ResetDetour begin (imgui initialized)");
        }
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }

    if (!originalReset_) {
        return D3DERR_INVALIDCALL;
    }

    const HRESULT result = originalReset_(device, presentationParameters);
    if (SUCCEEDED(result) && self_ && self_->imguiInitialized_) {
        ImGui_ImplDX9_CreateDeviceObjects();
        static uint64_t s_lastResetOkTraceMs = 0;
        const uint64_t now = GetTickCount64();
        if (now - s_lastResetOkTraceMs >= kResetTraceIntervalMs) {
            s_lastResetOkTraceMs = now;
            debuglog::WriteInfo("[ui] ResetDetour ok: device objects recreated");
        }
    } else if (FAILED(result)) {
        static uint64_t s_lastResetFailTraceMs = 0;
        const uint64_t now = GetTickCount64();
        if (now - s_lastResetFailTraceMs >= kResetTraceIntervalMs) {
            s_lastResetFailTraceMs = now;
            debuglog::WriteError("[ui] ResetDetour failed: hr=0x%08lX", static_cast<unsigned long>(result));
        }
    }

    return result;
}

void ImGuiOverlay::SetRenderCallback(RenderCallback callback) {
    renderCallback_ = std::move(callback);
}

void ImGuiOverlay::SetPrepareFrameCallback(PrepareFrameCallback callback) {
    prepareFrameCallback_ = std::move(callback);
}

void ImGuiOverlay::SetUpdateCallback(UpdateCallback callback) {
    updateCallback_ = std::move(callback);
}

void ImGuiOverlay::SetWindowMessageCallback(WindowMessageCallback callback) {
    windowMessageCallback_ = std::move(callback);
}

void ImGuiOverlay::SetAuxiliaryUiVisibleCallback(VisibilityCallback callback) {
    auxiliaryUiVisibleCallback_ = std::move(callback);
}

void ImGuiOverlay::SetAuxiliaryInputCaptureCallback(VisibilityCallback callback) {
    auxiliaryInputCaptureCallback_ = std::move(callback);
}

void ImGuiOverlay::SetAuxiliaryInputRoutingCallback(VisibilityCallback callback) {
    auxiliaryInputRoutingCallback_ = std::move(callback);
}

void ImGuiOverlay::SetInputPipelineGateCallback(GateCallback callback) {
    inputPipelineGateCallback_ = std::move(callback);
}

void ImGuiOverlay::SetInputCaptureChangedCallback(InputCaptureChangedCallback callback) {
    inputCaptureChangedCallback_ = std::move(callback);
}

void ImGuiOverlay::SetMenuToggleHotkeyConflictCallback(HotkeyConflictCallback callback) {
    menuToggleHotkeyConflictCallback_ = std::move(callback);
}

void ImGuiOverlay::SetMenuOpen(bool open) {
    if (!open) {
        CancelMenuToggleHotkeyCapture();
    }
    menuOpen_ = open;
}

bool ImGuiOverlay::IsMenuOpen() const {
    return menuOpen_;
}

HWND ImGuiOverlay::GetGameWindow() const {
    return gameWindow_;
}

bool ImGuiOverlay::IsGameWindowForeground() const {
    if (!gameWindow_ || !IsWindow(gameWindow_)) {
        return true;
    }
    const HWND fg = GetForegroundWindow();
    if (!fg) {
        return false;
    }
    return fg == gameWindow_ || IsChild(gameWindow_, fg) != FALSE;
}

bool ImGuiOverlay::IsTextInputActive() const {
    return inputCaptureActive_;
}

bool ImGuiOverlay::WantsUiCursor() const {
    return menuOpen_ || WantsAuxiliaryUiCursor();
}

bool ImGuiOverlay::WantsInputRouting() const {
    return menuOpen_ || WantsAuxiliaryUiCursor() || WantsAuxiliaryInputRouting() || WantsTextInputCapture();
}

bool ImGuiOverlay::ShouldSwallowMouseInput() const {
    return inputSwallowMouse_ && CanRouteInput();
}

void ImGuiOverlay::SetInputRoutingAllowed(bool allowed) {
    SetInputDecision(allowed, drawHelperCursor_, inputSwallowMouse_);
}

void ImGuiOverlay::SetInputDecision(bool routingAllowed, bool drawHelperCursor, bool swallowMouse) {
    const bool effectiveDrawHelperCursor = routingAllowed && drawHelperCursor;
    const bool effectiveSwallowMouse = routingAllowed && swallowMouse;
    const bool previousSwallowMouse = inputSwallowMouse_;
    if (inputRoutingAllowed_ == routingAllowed
        && drawHelperCursor_ == effectiveDrawHelperCursor
        && inputSwallowMouse_ == effectiveSwallowMouse) {
        return;
    }

    inputRoutingAllowed_ = routingAllowed;
    drawHelperCursor_ = effectiveDrawHelperCursor;
    inputSwallowMouse_ = effectiveSwallowMouse;
    debuglog::WriteInfo(
        "[ui] input routing decision: route=%s drawHelperCursor=%s swallowMouse=%s",
        routingAllowed ? "yes" : "no",
        drawHelperCursor_ ? "yes" : "no",
        inputSwallowMouse_ ? "yes" : "no");
    if (inputSwallowMouse_) {
        if (!previousSwallowMouse) {
            helperWindowHitRects_.clear();
            helperWindowHitTestReady_ = false;
            mouseSwallowLatchMask_ = 0;
        }
    } else {
        helperWindowHitRects_.clear();
        helperWindowHitTestReady_ = false;
        mouseSwallowLatchMask_ = 0;
    }
    if (!routingAllowed) {
        ApplyInputCaptureState(false);
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui::GetIO().MouseDrawCursor = false;
        }
    }
}

std::string ImGuiOverlay::MenuToggleHotkeyText() const {
    return hotkeys::ToString(UiSettings::Instance().MenuToggleHotkey());
}

void ImGuiOverlay::BeginMenuToggleHotkeyCapture() {
    menuToggleHotkeyCapture_.Start(hotkeys::NormalizeCombo(UiSettings::Instance().MenuToggleHotkey(), HotkeyMode::ModifierTrigger));
    hotkeys::OpenCapturePopupCenteredOnCurrentWindow(menuToggleHotkeyCapturePopup_);
    menuToggleWasDown_ = IsMenuToggleComboDown();
}

bool ImGuiOverlay::IsMenuToggleHotkeyCaptureActive() const {
    return menuToggleHotkeyCapture_.Active();
}

void ImGuiOverlay::CancelMenuToggleHotkeyCapture() {
    menuToggleHotkeyCapture_.Stop();
    hotkeys::ResetCapturePopupState(menuToggleHotkeyCapturePopup_);
    menuToggleWasDown_ = IsMenuToggleComboDown();
}

void ImGuiOverlay::DrawMenuToggleHotkeyCapturePopup() {
    std::string hotkeyConflict;
    const bool canSave = CanApplyMenuToggleHotkeyCapture(menuToggleHotkeyCapture_.Draft(), &hotkeyConflict);
    hotkeys::DrawCapturePopupModal(
        "##menu_toggle_hotkey_capture_popup",
        menuToggleHotkeyCapturePopup_,
        menuToggleHotkeyCapture_,
        [this](const std::vector<UINT>& keys) { return ApplyMenuToggleHotkeyCapture(keys); },
        canSave,
        HotkeyMode::ModifierTrigger,
        [&](const std::vector<UINT>&) {
            if (hotkeyConflict.empty()) {
                return;
            }

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.35f, 0.35f, 1.00f));
            ImGui::TextWrapped("%s", UiSettings::Instance().Format(UiText::HotkeyConflictFormat, hotkeyConflict.c_str()).c_str());
            ImGui::PopStyleColor();
        },
        [this]() { CancelMenuToggleHotkeyCapture(); });
}

void ImGuiOverlay::OnProcessAttach() {
    self_ = this;
    shuttingDown_ = false;

    debuglog::WriteInfo("ImGuiOverlay attached (tid=%lu)", GetCurrentThreadId());

    initThread_ = CreateThread(nullptr, 0, &InitializeThread, this, 0, nullptr);
    if (!initThread_) {
        debuglog::WriteError("CreateThread failed: %lu", GetLastError());
    }
}

DWORD WINAPI ImGuiOverlay::InitializeThread(LPVOID param) {
    auto* self = static_cast<ImGuiOverlay*>(param);
    if (!self) {
        return 0;
    }

    for (int attempt = 1; attempt <= 30 && !self->shuttingDown_; ++attempt) {
        if (self->InstallGraphicsHooks()) {
            return 0;
        }

        if (attempt == 1 || attempt % 5 == 0) {
            debuglog::WriteError("D3D9 hook attempt %d failed, retrying", attempt);
        }
        Sleep(1000);
    }

    debuglog::WriteError("D3D9 hooks were not installed");
    return 0;
}

bool ImGuiOverlay::CreateDummyDevice(IDirect3DDevice9** outDevice, HWND* outWindow, bool* outRegisteredWindowClass) const {
    if (!outDevice || !outWindow || !outRegisteredWindowClass) {
        return false;
    }

    *outDevice = nullptr;
    *outWindow = nullptr;
    *outRegisteredWindowClass = false;

    WNDCLASSEXA windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DefWindowProcA;
    windowClass.hInstance = GetModuleHandleA(nullptr);
    windowClass.lpszClassName = kDummyWindowClassName;

    SetLastError(0);
    const ATOM classAtom = RegisterClassExA(&windowClass);
    const DWORD registerError = GetLastError();
    if (!classAtom && registerError != ERROR_CLASS_ALREADY_EXISTS) {
        debuglog::WriteError("RegisterClassExA failed: %lu", registerError);
        return false;
    }
    *outRegisteredWindowClass = classAtom != 0;
    debuglog::WriteInfo(
        "[ui][d3d] dummy window class atom=%u registered=%d existing=%d gle=%lu",
        static_cast<unsigned>(classAtom),
        *outRegisteredWindowClass ? 1 : 0,
        registerError == ERROR_CLASS_ALREADY_EXISTS ? 1 : 0,
        static_cast<unsigned long>(registerError));

    HWND window = CreateWindowExA(0, kDummyWindowClassName, kDummyWindowClassName,
        WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!window) {
        debuglog::WriteError("CreateWindowExA failed: %lu", GetLastError());
        DestroyDummyWindow(nullptr, *outRegisteredWindowClass);
        *outRegisteredWindowClass = false;
        return false;
    }
    TraceWindowDetails("d3d_dummy", window);

    const uint64_t d3dCreateBeginMs = GetTickCount64();
    debuglog::WriteInfo("[ui][d3d] Direct3DCreate9 begin");
    IDirect3D9* d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d9) {
        debuglog::WriteError(
            "Direct3DCreate9 failed elapsed=%llums",
            static_cast<unsigned long long>(GetTickCount64() - d3dCreateBeginMs));
        DestroyDummyWindow(window, *outRegisteredWindowClass);
        *outRegisteredWindowClass = false;
        return false;
    }
    debuglog::WriteInfo(
        "[ui][d3d] Direct3DCreate9 ok d3d9=%p elapsed=%llums",
        d3d9,
        static_cast<unsigned long long>(GetTickCount64() - d3dCreateBeginMs));

    D3DPRESENT_PARAMETERS parameters{};
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = window;
    parameters.BackBufferFormat = D3DFMT_UNKNOWN;

    struct DummyDeviceAttempt {
        D3DDEVTYPE deviceType;
        DWORD behaviorFlags;
        const char* label;
    };

    static constexpr DummyDeviceAttempt kAttempts[] = {
        { D3DDEVTYPE_NULLREF, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT, "nullref-disable-driver-management" },
        { D3DDEVTYPE_HAL, D3DCREATE_SOFTWARE_VERTEXPROCESSING, "hal-software-vp-fallback" },
    };

    HRESULT result = D3DERR_INVALIDCALL;
    const char* selectedAttempt = nullptr;
    for (const auto& attempt : kAttempts) {
        const uint64_t createDeviceBeginMs = GetTickCount64();
        debuglog::WriteInfo(
            "[ui][d3d] dummy CreateDevice begin mode=%s hwnd=%p flags=0x%08lX",
            attempt.label,
            window,
            static_cast<unsigned long>(attempt.behaviorFlags));
        result = d3d9->CreateDevice(
            D3DADAPTER_DEFAULT,
            attempt.deviceType,
            window,
            attempt.behaviorFlags,
            &parameters,
            outDevice);
        debuglog::WriteInfo(
            "[ui][d3d] dummy CreateDevice end mode=%s hr=0x%08lX device=%p elapsed=%llums",
            attempt.label,
            static_cast<unsigned long>(result),
            *outDevice,
            static_cast<unsigned long long>(GetTickCount64() - createDeviceBeginMs));

        if (SUCCEEDED(result) && *outDevice) {
            selectedAttempt = attempt.label;
            break;
        }

        if (*outDevice) {
            (*outDevice)->Release();
            *outDevice = nullptr;
        }
    }

    d3d9->Release();

    if (FAILED(result) || !*outDevice) {
        debuglog::WriteError("IDirect3D9::CreateDevice failed: 0x%08lX", static_cast<unsigned long>(result));
        DestroyDummyWindow(window, *outRegisteredWindowClass);
        *outRegisteredWindowClass = false;
        return false;
    }

    debuglog::WriteInfo("[ui][d3d] dummy device selected mode=%s", selectedAttempt ? selectedAttempt : "<unknown>");
    *outWindow = window;
    return true;
}

bool ImGuiOverlay::InstallGraphicsHooks() {
    if (hooksInstalled_) {
        return true;
    }

    IDirect3DDevice9* dummyDevice = nullptr;
    HWND dummyWindow = nullptr;
    bool dummyWindowClassRegistered = false;
    if (!CreateDummyDevice(&dummyDevice, &dummyWindow, &dummyWindowClassRegistered)) {
        return false;
    }

    const auto cleanupDummyDevice = [&]() {
        if (dummyDevice) {
            dummyDevice->Release();
            dummyDevice = nullptr;
        }
        DestroyDummyWindow(dummyWindow, dummyWindowClassRegistered);
        dummyWindow = nullptr;
        dummyWindowClassRegistered = false;
    };

    void** vtable = *reinterpret_cast<void***>(dummyDevice);
    if (!vtable) {
        debuglog::WriteError("IDirect3DDevice9 vtable is null");
        cleanupDummyDevice();
        return false;
    }

    endSceneTarget_ = vtable[42];
    presentTarget_ = vtable[17];
    resetTarget_ = vtable[16];
    debuglog::WriteInfo(
        "[ui][d3d] vtable snapshot device=%p vtable=%p Reset[16]=%p Present[17]=%p EndScene[42]=%p",
        dummyDevice,
        vtable,
        resetTarget_,
        presentTarget_,
        endSceneTarget_);
    TraceModuleForAddress("IDirect3DDevice9::Reset", resetTarget_);
    TraceModuleForAddress("IDirect3DDevice9::Present", presentTarget_);
    TraceModuleForAddress("IDirect3DDevice9::EndScene", endSceneTarget_);
    originalEndScene_ = nullptr;
    originalPresent_ = nullptr;
    originalReset_ = nullptr;
    WCHAR resetModulePath[MAX_PATH]{};
    const bool skipResetHook = IsAddressInModuleNamed(
        resetTarget_,
        L"apphelp.dll",
        resetModulePath,
        static_cast<DWORD>(std::size(resetModulePath)));
    debuglog::WriteInfo(
        "[ui][d3d] hook policy EndScene=install Present=install Reset=%s resetAppCompatShim=%d",
        skipResetHook ? "skip-appcompat-shim" : "install",
        skipResetHook ? 1 : 0);

    if (!minhook::CreateAndEnableHook(
            endSceneTarget_,
            reinterpret_cast<void*>(&EndSceneDetour),
            &originalEndScene_,
            "IDirect3DDevice9::EndScene")) {
        endSceneTarget_ = nullptr;
        presentTarget_ = nullptr;
        resetTarget_ = nullptr;
        cleanupDummyDevice();
        return false;
    }

    if (!minhook::CreateAndEnableHook(
            presentTarget_,
            reinterpret_cast<void*>(&PresentDetour),
            &originalPresent_,
            "IDirect3DDevice9::Present")) {
        minhook::DisableAndRemoveHook(endSceneTarget_, "IDirect3DDevice9::EndScene");
        endSceneTarget_ = nullptr;
        originalEndScene_ = nullptr;
        presentTarget_ = nullptr;
        resetTarget_ = nullptr;
        cleanupDummyDevice();
        return false;
    }

    if (skipResetHook) {
        debuglog::WriteError(
            "[ui][d3d] IDirect3DDevice9::Reset hook skipped: target is Windows appcompat shim module=%ls target=%p; "
            "overlay remains active via Present/EndScene",
            resetModulePath,
            resetTarget_);
        originalReset_ = nullptr;
        resetTarget_ = nullptr;
    } else if (!minhook::CreateAndEnableHook(
                   resetTarget_,
                   reinterpret_cast<void*>(&ResetDetour),
                   &originalReset_,
                   "IDirect3DDevice9::Reset")) {
        debuglog::WriteError(
            "[ui][d3d] IDirect3DDevice9::Reset hook unavailable; overlay remains active via Present/EndScene");
        originalReset_ = nullptr;
        resetTarget_ = nullptr;
    }

    hooksInstalled_ = true;
    debuglog::WriteInfo(
        "D3D9 hooks installed via MinHook. EndScene=%p Present=%p Reset=%p ResetHooked=%d",
        endSceneTarget_,
        presentTarget_,
        resetTarget_,
        (resetTarget_ && originalReset_) ? 1 : 0);

    cleanupDummyDevice();
    return true;
}

HWND ImGuiOverlay::ResolveGameWindow(IDirect3DDevice9* device) const {
    if (gameWindow_ && IsWindow(gameWindow_)) {
        return gameWindow_;
    }

    if (HWND gtaWindow = ReadGtaWindowHandle()) {
        TraceWindowDetails("gta_global_0x00C8CF88", gtaWindow);
        return gtaWindow;
    }

    if (device) {
        D3DDEVICE_CREATION_PARAMETERS parameters{};
        if (SUCCEEDED(device->GetCreationParameters(&parameters)) && IsWindow(parameters.hFocusWindow)) {
            TraceWindowDetails("d3d_creation_params", parameters.hFocusWindow);
            return parameters.hFocusWindow;
        }
    }

    HWND foreground = GetForegroundWindow();
    if (foreground && IsWindow(foreground)) {
        DWORD processId = 0;
        GetWindowThreadProcessId(foreground, &processId);
        if (processId == GetCurrentProcessId()) {
            TraceWindowDetails("foreground_same_process", foreground);
            return foreground;
        }
        static uint64_t s_lastForegroundSkipTraceMs = 0;
        const uint64_t now = GetTickCount64();
        if (now - s_lastForegroundSkipTraceMs >= 1000) {
            s_lastForegroundSkipTraceMs = now;
            TraceWindowDetails("foreground_skipped_other_process", foreground);
        }
    }

    return nullptr;
}

void ImGuiOverlay::InitializeImGuiIfNeeded(IDirect3DDevice9* device) {
    if (imguiInitialized_ || !device) {
        return;
    }

    gameWindow_ = ResolveGameWindow(device);
    if (!gameWindow_) {
        static uint64_t s_lastNoWindowTraceMs = 0;
        const uint64_t now = GetTickCount64();
        if (now - s_lastNoWindowTraceMs >= 1000) {
            s_lastNoWindowTraceMs = now;
            debuglog::WriteInfo("[ui] InitializeImGuiIfNeeded waiting for game window");
        }
        return;
    }
    TraceWindowAndDpi(gameWindow_);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    // SA:MP overlay: клавиатурная навигация ImGui давала WantCaptureKB + пустой HovWin у BeginMenu.
    // Для теста отключено; при необходимости вернуть NavEnableKeyboard и ConfigNavCaptureKeyboard = true.
    io.ConfigNavCaptureKeyboard = false;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.MouseDrawCursor = false;
    io.FontDefault = LoadOverlayFont(io);

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(gameWindow_)) {
        debuglog::WriteError("ImGui_ImplWin32_Init failed");
        ImGui::DestroyContext();
        return;
    }

    if (!ImGui_ImplDX9_Init(device)) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        debuglog::WriteError("ImGui_ImplDX9_Init failed");
        return;
    }

    if (IsInputPipelineEnabled()) {
        if (!EnsureWndProcHookInstalled(true)) {
            ImGui_ImplDX9_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            return;
        }
    } else {
        debuglog::WriteInfo("[ui] WndProc hook deferred until SA:MP init gate");
    }
    imguiInitialized_ = true;

    debuglog::WriteInfo("ImGui initialized. Game window=%p", gameWindow_);
}

void ImGuiOverlay::RestoreWindowProc() {
    if (!gameWindow_ || !originalWndProc_) {
        return;
    }

    const LONG_PTR currentWndProc = GetWindowLongPtrA(gameWindow_, GWLP_WNDPROC);
    if (currentWndProc != reinterpret_cast<LONG_PTR>(&OverlayWndProc)) {
        debuglog::WriteInfo(
            "[ui] RestoreWindowProc skipped: top=%s previous=%s",
            ModuleNameForAddress(reinterpret_cast<const void*>(currentWndProc)).c_str(),
            ModuleNameForAddress(reinterpret_cast<const void*>(originalWndProc_)).c_str());
        originalWndProc_ = nullptr;
        fallbackWndProc_ = nullptr;
        return;
    }

    WNDPROC restoreTarget = fallbackWndProc_ ? fallbackWndProc_ : originalWndProc_;
    SetLastError(0);
    const LONG_PTR result = SetWindowLongPtrA(gameWindow_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(restoreTarget));
    if (result == 0 && GetLastError() != 0) {
        debuglog::WriteError("[ui] RestoreWindowProc failed: %lu", GetLastError());
    } else {
        debuglog::WriteInfo(
            "[ui] RestoreWindowProc ok hwnd=%p restored=%s",
            gameWindow_,
            ModuleNameForAddress(reinterpret_cast<const void*>(restoreTarget)).c_str());
    }
    originalWndProc_ = nullptr;
    fallbackWndProc_ = nullptr;
}

void ImGuiOverlay::CleanupImGui() {
    if (!imguiInitialized_) {
        return;
    }

    debuglog::WriteInfo("[ui] CleanupImGui begin");

    ImGui::GetIO().MouseDrawCursor = false;
    inputRoutingAllowed_ = false;
    drawHelperCursor_ = false;
    inputSwallowMouse_ = false;
    RestoreWindowProc();
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    imguiInitialized_ = false;
    gameWindow_ = nullptr;
    debuglog::WriteInfo("[ui] CleanupImGui done");
}

void ImGuiOverlay::UpdateHotkeyState() {
    static bool hadForeground = true;
    const bool appFg = IsGameWindowForeground();
    if (!appFg) {
        hadForeground = false;
        return;
    }
    if (!hadForeground) {
        hadForeground = true;
        menuToggleWasDown_ = IsMenuToggleComboDown();
        return;
    }

    const bool comboDown = IsMenuToggleComboDown();
    if (!menuToggleHotkeyCapture_.Active() && comboDown && !menuToggleWasDown_) {
        menuOpen_ = !menuOpen_;
        const bool aux = IsAuxiliaryUiVisible();
        debuglog::WriteInfo(
            "[ui] Menu toggled: %s aux=%d wantRoute=%d routeAllowed=%d canRoute=%d wantUiCursor=%d wantAuxCursor=%d",
            menuOpen_ ? "open" : "closed",
            aux ? 1 : 0,
            WantsInputRouting() ? 1 : 0,
            inputRoutingAllowed_ ? 1 : 0,
            CanRouteInput() ? 1 : 0,
            WantsUiCursor() ? 1 : 0,
            WantsAuxiliaryUiCursor() ? 1 : 0);
    }

    menuToggleWasDown_ = comboDown;
}

bool ImGuiOverlay::HandleMenuToggleHotkeyCaptureMessage(UINT message, WPARAM wparam) {
    bool canceled = false;
    bool saved = false;
    std::vector<UINT> capturedKeys;
    if (!menuToggleHotkeyCapture_.Active()
        || !menuToggleHotkeyCapture_.OnWindowMessage(message, wparam, canceled, saved, capturedKeys)) {
        return false;
    }

    if (saved) {
        if (!ApplyMenuToggleHotkeyCapture(capturedKeys)) {
            menuToggleHotkeyCapture_.Start(capturedKeys);
        }
    } else if (canceled) {
        CancelMenuToggleHotkeyCapture();
    }

    return true;
}

bool ImGuiOverlay::IsMenuToggleComboDown() const {
    return hotkeys::ComboMatch(
        hotkeys::CollectPressedKeys(),
        UiSettings::Instance().MenuToggleHotkey(),
        HotkeyMode::ModifierTrigger);
}

bool ImGuiOverlay::CanApplyMenuToggleHotkeyCapture(const std::vector<unsigned int>& keys, std::string* description) const {
    if (description) {
        description->clear();
    }

    const auto normalized = hotkeys::NormalizeCombo(keys, HotkeyMode::ModifierTrigger);
    if (!hotkeys::HasTriggerKey(normalized)) {
        return false;
    }

    std::string conflict;
    if (menuToggleHotkeyConflictCallback_ && menuToggleHotkeyConflictCallback_(normalized, conflict)) {
        if (description) {
            *description = conflict;
        }
        return false;
    }

    return true;
}

bool ImGuiOverlay::ApplyMenuToggleHotkeyCapture(const std::vector<unsigned int>& keys) {
    if (!CanApplyMenuToggleHotkeyCapture(keys)) {
        return false;
    }

    UiSettings::Instance().SetMenuToggleHotkey(hotkeys::NormalizeCombo(keys, HotkeyMode::ModifierTrigger));
    hotkeys::ResetCapturePopupState(menuToggleHotkeyCapturePopup_);
    menuToggleWasDown_ = IsMenuToggleComboDown();
    return true;
}

bool ImGuiOverlay::IsPrimaryRenderTarget(IDirect3DDevice9* device) const {
    if (!device) {
        return false;
    }

    IDirect3DSurface9* currentRenderTarget = nullptr;
    IDirect3DSurface9* backBuffer = nullptr;
    IUnknown* currentIdentity = nullptr;
    IUnknown* backBufferIdentity = nullptr;
    const HRESULT currentHr = device->GetRenderTarget(0, &currentRenderTarget);
    const HRESULT backBufferHr = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
    bool isPrimary = false;
    if (SUCCEEDED(currentHr) && SUCCEEDED(backBufferHr) && currentRenderTarget && backBuffer) {
        if (SUCCEEDED(currentRenderTarget->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&currentIdentity)))
            && SUCCEEDED(backBuffer->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&backBufferIdentity)))) {
            isPrimary = currentIdentity == backBufferIdentity;
        }
    }

    if (backBufferIdentity) {
        backBufferIdentity->Release();
    }
    if (currentIdentity) {
        currentIdentity->Release();
    }
    if (backBuffer) {
        backBuffer->Release();
    }
    if (currentRenderTarget) {
        currentRenderTarget->Release();
    }

    return isPrimary;
}

bool ImGuiOverlay::IsAuxiliaryUiVisible() const {
    return auxiliaryUiVisibleCallback_ ? auxiliaryUiVisibleCallback_() : false;
}

bool ImGuiOverlay::CanRouteInput() const {
    return inputRoutingAllowed_ && WantsInputRouting();
}

bool ImGuiOverlay::WantsAuxiliaryUiCursor() const {
    return auxiliaryInputCaptureCallback_ ? auxiliaryInputCaptureCallback_() : false;
}

bool ImGuiOverlay::WantsAuxiliaryInputRouting() const {
    return auxiliaryInputRoutingCallback_ ? auxiliaryInputRoutingCallback_() : false;
}

bool ImGuiOverlay::WantsTextInputCapture() const {
    return inputCaptureActive_;
}

bool ImGuiOverlay::IsInputPipelineEnabled() const {
    return inputPipelineGateCallback_ ? inputPipelineGateCallback_() : true;
}

void ImGuiOverlay::SyncOsMouseToImGui() const {
    if (gameWindow_ == nullptr || ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    POINT pt{};
    if (::GetCursorPos(&pt) != 0 && ::ScreenToClient(gameWindow_, &pt) != 0) {
        ImGui::GetIO().AddMousePosEvent(static_cast<float>(pt.x), static_cast<float>(pt.y));
    }
}

bool ImGuiOverlay::MouseMessageClientPos(UINT message, LPARAM lparam, ImVec2& outPos) const {
    if (!IsMouseMessage(message)) {
        return false;
    }

    POINT pt{};
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
        if (gameWindow_ == nullptr) {
            return false;
        }
        pt.x = GET_X_LPARAM(lparam);
        pt.y = GET_Y_LPARAM(lparam);
        if (::ScreenToClient(gameWindow_, &pt) == 0) {
            return false;
        }
    } else {
        pt.x = GET_X_LPARAM(lparam);
        pt.y = GET_Y_LPARAM(lparam);
    }

    outPos = ImVec2(static_cast<float>(pt.x), static_cast<float>(pt.y));
    return true;
}

bool ImGuiOverlay::IsPointInsideHelperWindow(const ImVec2& point) const {
    for (const HelperWindowHitRect& rect : helperWindowHitRects_) {
        if (point.x >= rect.min.x && point.x < rect.max.x && point.y >= rect.min.y && point.y < rect.max.y) {
            return true;
        }
    }
    return false;
}

bool ImGuiOverlay::HasMouseSwallowLatch() const {
    return mouseSwallowLatchMask_ != 0;
}

bool ImGuiOverlay::ShouldSwallowMouseMessage(UINT message, LPARAM lparam) const {
    if (!ShouldSwallowMouseInput() || !IsMouseMessage(message)) {
        return false;
    }
    if (HasMouseSwallowLatch()) {
        return true;
    }
    if (!helperWindowHitTestReady_) {
        return true;
    }

    ImVec2 clientPos{};
    if (!MouseMessageClientPos(message, lparam, clientPos)) {
        return true;
    }

    return IsPointInsideHelperWindow(clientPos);
}

void ImGuiOverlay::UpdateMouseSwallowLatch(UINT message, WPARAM wparam, bool swallowed) {
    const std::uint32_t mask = MouseLatchMaskForMessage(message, wparam);
    if (mask == 0) {
        return;
    }
    if (IsMouseButtonUpMessage(message)) {
        mouseSwallowLatchMask_ &= ~mask;
        return;
    }
    if (swallowed && IsMouseButtonDownMessage(message)) {
        mouseSwallowLatchMask_ |= mask;
    }
}

void ImGuiOverlay::RefreshHelperWindowHitRects() {
    helperWindowHitRects_.clear();

    if (!ShouldSwallowMouseInput() || ImGui::GetCurrentContext() == nullptr) {
        helperWindowHitTestReady_ = false;
        if (traceHelperWindowHitTestReady_ || traceHelperWindowRectCount_ != 0) {
            traceHelperWindowHitTestReady_ = false;
            traceHelperWindowRectCount_ = 0;
            debuglog::WriteInfo("[ui] helper hit rects ready=0 count=0");
        }
        return;
    }

    ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const float displayMaxX = std::max(0.0f, displaySize.x);
    const float displayMaxY = std::max(0.0f, displaySize.y);

    for (ImGuiWindow* window : g.Windows) {
        if (window == nullptr
            || window->IsFallbackWindow
            || !window->Active
            || window->Hidden
            || window->LastFrameActive != g.FrameCount
            || WindowOrAncestorHasNoMouseInputs(window)) {
            continue;
        }

        ImRect rect = window->Rect();
        if (rect.GetWidth() <= 0.0f || rect.GetHeight() <= 0.0f) {
            continue;
        }

        rect.Expand(kHelperWindowHitTestPadding);
        rect.Min.x = std::clamp(rect.Min.x, 0.0f, displayMaxX);
        rect.Min.y = std::clamp(rect.Min.y, 0.0f, displayMaxY);
        rect.Max.x = std::clamp(rect.Max.x, 0.0f, displayMaxX);
        rect.Max.y = std::clamp(rect.Max.y, 0.0f, displayMaxY);
        if (rect.GetWidth() <= 0.0f || rect.GetHeight() <= 0.0f) {
            continue;
        }

        helperWindowHitRects_.push_back(HelperWindowHitRect{ rect.Min, rect.Max });
    }

    helperWindowHitTestReady_ = true;
    if (traceHelperWindowHitTestReady_ != helperWindowHitTestReady_
        || traceHelperWindowRectCount_ != helperWindowHitRects_.size()) {
        traceHelperWindowHitTestReady_ = helperWindowHitTestReady_;
        traceHelperWindowRectCount_ = helperWindowHitRects_.size();
        debuglog::WriteInfo(
            "[ui] helper hit rects ready=%d count=%zu",
            helperWindowHitTestReady_ ? 1 : 0,
            helperWindowHitRects_.size());
    }
}

LRESULT ImGuiOverlay::CallPreviousWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) const {
    static thread_local bool callingPrevious = false;

    WNDPROC target = callingPrevious && fallbackWndProc_ ? fallbackWndProc_ : originalWndProc_;
    if (target == reinterpret_cast<WNDPROC>(&OverlayWndProc)) {
        target = fallbackWndProc_;
    }
    if (!target || target == reinterpret_cast<WNDPROC>(&OverlayWndProc)) {
        return DefWindowProc(hwnd, message, wparam, lparam);
    }

    const bool wasCallingPrevious = callingPrevious;
    callingPrevious = true;
    const LRESULT result = CallWindowProc(target, hwnd, message, wparam, lparam);
    callingPrevious = wasCallingPrevious;
    return result;
}

bool ImGuiOverlay::EnsureWndProcHookInstalled(bool forceTop) {
    if (!gameWindow_) {
        return false;
    }

    const LONG_PTR helperWndProc = reinterpret_cast<LONG_PTR>(&OverlayWndProc);
    const LONG_PTR currentWndProc = GetWindowLongPtrA(gameWindow_, GWLP_WNDPROC);
    if (currentWndProc == helperWndProc) {
        return true;
    }

    if (originalWndProc_ && !forceTop) {
        return true;
    }

    SetLastError(0);
    const LONG_PTR previousWndProc = SetWindowLongPtrA(
        gameWindow_, GWLP_WNDPROC, helperWndProc);
    if (previousWndProc == 0 && GetLastError() != 0) {
        debuglog::WriteError("SetWindowLongPtrA failed: %lu", GetLastError());
        return false;
    }

    WNDPROC previous = reinterpret_cast<WNDPROC>(previousWndProc);
    if (originalWndProc_ && previous != originalWndProc_) {
        fallbackWndProc_ = fallbackWndProc_ ? fallbackWndProc_ : originalWndProc_;
        static std::uint64_t s_lastWndProcRehookTraceMs = 0;
        const std::uint64_t now = GetTickCount64();
        if (now - s_lastWndProcRehookTraceMs >= kWndProcTraceIntervalMs) {
            s_lastWndProcRehookTraceMs = now;
            debuglog::WriteInfo(
                "[ui] WndProc watchdog reinstalled Helper top: previous=%s fallback=%s",
                ModuleNameForAddress(reinterpret_cast<const void*>(previous)).c_str(),
                ModuleNameForAddress(reinterpret_cast<const void*>(fallbackWndProc_)).c_str());
        }
    } else {
        debuglog::WriteInfo(
            "[ui] WndProc hook installed hwnd=%p previous=%s",
            gameWindow_,
            ModuleNameForAddress(reinterpret_cast<const void*>(previous)).c_str());
    }

    originalWndProc_ = previous;
    return true;
}

void ImGuiOverlay::ApplyInputCaptureState(bool captured) {
    if (captured == inputCaptureActive_) {
        return;
    }

    inputCaptureActive_ = captured;
    if (inputCaptureChangedCallback_) {
        inputCaptureChangedCallback_(captured);
    }

    debuglog::WriteInfo("ImGui text input capture: %s", captured ? "enabled" : "disabled");
}

void ImGuiOverlay::UpdateInputCaptureState() {
    bool captured = false;
    if (imguiInitialized_ && ImGui::GetCurrentContext() != nullptr && CanRouteInput()) {
        captured = ImGui::GetIO().WantTextInput;
    }

    ApplyInputCaptureState(captured);
}

void ImGuiOverlay::TraceUiRenderAndInputSnapshot(const char* frameTag) {
    const bool auxVisible = IsAuxiliaryUiVisible();
    const bool idle = !menuOpen_ && !auxVisible;
    const bool pathChanged = menuOpen_ != traceLastMenuOpen_
        || auxVisible != traceLastAuxVisible_ || idle != traceLastIdleFrame_;
    if (pathChanged) {
        traceLastMenuOpen_ = menuOpen_;
        traceLastAuxVisible_ = auxVisible;
        traceLastIdleFrame_ = idle;
        debuglog::WriteInfo(
            "[ui] RenderFrame %s: idle=%d menu=%d aux=%d wantRoute=%d routeAllowed=%d canRoute=%d swallowMouse=%d drawCur=%d wantUi=%d wantAuxCur=%d wantTextCap=%d",
            frameTag,
            idle ? 1 : 0,
            menuOpen_ ? 1 : 0,
            auxVisible ? 1 : 0,
            WantsInputRouting() ? 1 : 0,
            inputRoutingAllowed_ ? 1 : 0,
            CanRouteInput() ? 1 : 0,
            ShouldSwallowMouseInput() ? 1 : 0,
            drawHelperCursor_ ? 1 : 0,
            WantsUiCursor() ? 1 : 0,
            WantsAuxiliaryUiCursor() ? 1 : 0,
            WantsTextInputCapture() ? 1 : 0);
    }

    if (std::strcmp(frameTag, "after_present_ui") != 0) {
        return;
    }
    if (!CanRouteInput() || ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const bool wantsUiCursor = WantsUiCursor();
    const bool hasActiveInputOrHover = io.WantCaptureMouse || io.WantCaptureKeyboard || io.WantTextInput
        || ImGui::IsAnyItemActive() || ImGui::IsAnyItemHovered();
    const bool popupTransitionExpected = wantsUiCursor && !io.WantCaptureMouse && IsPopupTransitionNoCaptureExpected(io);
    const bool anomaly = wantsUiCursor && !io.WantCaptureMouse && !popupTransitionExpected;
    if (!hasActiveInputOrHover && !anomaly && !popupTransitionExpected) {
        return;
    }
    const uint64_t now = GetTickCount64();
    if (now - traceLastUiDiagTick_ < kPostRenderHealthTraceIntervalMs) {
        return;
    }
    traceLastUiDiagTick_ = now;
    debuglog::WriteInfo(
        "[ui] diag post-render uiCur=%d anomaly=%d popupTransitionExpected=%d WantCaptureMouse=%d WantCaptureKB=%d MouseDown0=%d AnyItemActive=%d AnyItemHovered=%d WantText=%d",
        wantsUiCursor ? 1 : 0,
        anomaly ? 1 : 0,
        popupTransitionExpected ? 1 : 0,
        io.WantCaptureMouse ? 1 : 0,
        io.WantCaptureKeyboard ? 1 : 0,
        io.MouseDown[0] ? 1 : 0,
        ImGui::IsAnyItemActive() ? 1 : 0,
        ImGui::IsAnyItemHovered() ? 1 : 0,
        io.WantTextInput ? 1 : 0);

    static uint64_t s_lastDeepTraceMs = 0;
    if ((anomaly || popupTransitionExpected) && now - s_lastDeepTraceMs >= kDeepUiTraceIntervalMs) {
        s_lastDeepTraceMs = now;
        if (popupTransitionExpected) {
            debuglog::WriteInfo("[ui] popup transition explains WantCaptureMouse=0 on this frame");
        }
        DbgTraceImGuiInternalState("after_present");
    }
}

void ImGuiOverlay::AdvanceImGuiFrameWithoutUi(IDirect3DDevice9* device) {
    if (!device || ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    TraceUiRenderAndInputSnapshot("idle_tick");

    ImGui::GetIO().MouseDrawCursor = false;
    ApplyInputCaptureState(false);

    UiFramePerf perf{};
    const double beginMs = PerfNowMs();
    double stageBeginMs = beginMs;
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    perf.backendNewFrameMs = PerfNowMs() - stageBeginMs;
    if (prepareFrameCallback_) {
        stageBeginMs = PerfNowMs();
        prepareFrameCallback_(device);
        perf.prepareFrameMs = PerfNowMs() - stageBeginMs;
    }
    stageBeginMs = PerfNowMs();
    ImGui::NewFrame();
    perf.backendNewFrameMs += PerfNowMs() - stageBeginMs;
    stageBeginMs = PerfNowMs();
    ImGui::EndFrame();
    ImGui::Render();
    perf.imguiRenderMs = PerfNowMs() - stageBeginMs;
    ImDrawData* drawData = ImGui::GetDrawData();
    CaptureDrawDataStats(perf, drawData);
    if (drawData && drawData->TotalVtxCount > 0) {
        stageBeginMs = PerfNowMs();
        ImGui_ImplDX9_RenderDrawData(drawData);
        perf.renderDrawMs = PerfNowMs() - stageBeginMs;
    } else {
        perf.drawSkipped = true;
    }
    perf.totalMs = PerfNowMs() - beginMs;
    const bool slow = perf.totalMs >= static_cast<double>(kSlowFrameTraceThresholdMs);
    AccumulateUiFramePerf(perf, slow);
    if (slow) {
        debuglog::WriteInfo(
            "[ui][perf] idle slow total=%.2fms new=%.2fms prep=%.2fms render=%.2fms draw=%.2fms drawSkip=%d win=%d vtx=%d idx=%d",
            perf.totalMs,
            perf.backendNewFrameMs,
            perf.prepareFrameMs,
            perf.imguiRenderMs,
            perf.renderDrawMs,
            perf.drawSkipped ? 1 : 0,
            perf.windows,
            perf.vertices,
            perf.indices);
    }
}

void ImGuiOverlay::RenderFrame(IDirect3DDevice9* device) {
    const bool auxiliaryVisible = IsAuxiliaryUiVisible();
    if (!imguiInitialized_ || !device) {
        return;
    }

    if (IsInputPipelineEnabled()) {
        EnsureWndProcHookInstalled(WantsInputRouting());
    }

    const bool hasOverlayUi = menuOpen_ || auxiliaryVisible;
    const bool wantsUiCursor = WantsUiCursor();
    const bool wantsInputRouting = WantsInputRouting();
    const bool switchedUiSurface = wantsInputRouting
        && lastWantsInputRouting_
        && (menuOpen_ != lastInputResetMenuOpen_ || auxiliaryVisible != lastInputResetAuxVisible_);
    const bool resetMouseInputState = (!lastWantsInputRouting_ && wantsInputRouting) || switchedUiSurface;
    lastWantsInputRouting_ = wantsInputRouting;
    lastInputResetMenuOpen_ = menuOpen_;
    lastInputResetAuxVisible_ = auxiliaryVisible;

    if (!hasOverlayUi) {
        RefreshHelperWindowHitRects();
        mouseSwallowLatchMask_ = 0;
        AdvanceImGuiFrameWithoutUi(device);
        return;
    }

    UiFramePerf perf{};
    perf.fullUi = true;
    const double beginMs = PerfNowMs();
    double stageBeginMs = beginMs;
    TraceUiRenderAndInputSnapshot("full_ui");

    IDirect3DStateBlock9* stateBlock = nullptr;
    IDirect3DVertexDeclaration9* vertexDeclaration = nullptr;
    IDirect3DVertexShader9* vertexShader = nullptr;

    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &stateBlock))) {
        stateBlock = nullptr;
        static uint64_t s_lastStateBlockFailTraceMs = 0;
        const uint64_t now = GetTickCount64();
        if (now - s_lastStateBlockFailTraceMs >= kStateBlockFailTraceIntervalMs) {
            s_lastStateBlockFailTraceMs = now;
            debuglog::WriteError("[ui] CreateStateBlock(D3DSBT_ALL) failed");
        }
    } else {
        stateBlock->Capture();
    }

    device->GetVertexDeclaration(&vertexDeclaration);
    device->GetVertexShader(&vertexShader);
    perf.stateBackupMs = PerfNowMs() - stageBeginMs;

    stageBeginMs = PerfNowMs();
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    perf.backendNewFrameMs = PerfNowMs() - stageBeginMs;

    // До NewFrame(): выровнять MousePos с реальным курсором в клиентских координатах окна игры.
    // ImGui Win32 иногда оставляет координаты без OS-fallback (фокус на дочернем HWND, MouseTrackedArea),
    // при этом WndProc по-прежнему перехватывает клики пока открыт overlay — без этого ImGui остаётся
    // без hover (WantCaptureMouse=0), а клики «съедаются».
    if (gameWindow_ != nullptr) {
        ImGuiIO& io = ImGui::GetIO();
        if (resetMouseInputState && CanRouteInput()) {
            if (!HasMouseButtonDown(io)) {
                io.ClearInputMouse();
                debuglog::WriteInfo(
                    "[ui] input routing mouse state reset: switchedSurface=%d menu=%d aux=%d",
                    switchedUiSurface ? 1 : 0,
                    menuOpen_ ? 1 : 0,
                    auxiliaryVisible ? 1 : 0);
            } else {
                debuglog::WriteInfo(
                    "[ui] input routing mouse reset skipped: mouse button down switchedSurface=%d menu=%d aux=%d",
                    switchedUiSurface ? 1 : 0,
                    menuOpen_ ? 1 : 0,
                    auxiliaryVisible ? 1 : 0);
            }
        }
        SyncOsMouseToImGui();
    }
    const bool drawHelperCursorThisFrame = drawHelperCursor_ && wantsUiCursor && CanRouteInput();
    ImGui::GetIO().MouseDrawCursor = drawHelperCursorThisFrame;
    if (drawHelperCursorThisFrame) {
        ::SetCursor(nullptr);
    }

    if (prepareFrameCallback_) {
        stageBeginMs = PerfNowMs();
        prepareFrameCallback_(device);
        perf.prepareFrameMs = PerfNowMs() - stageBeginMs;
    }

    stageBeginMs = PerfNowMs();
    ImGui::NewFrame();
    if (wantsUiCursor && CanRouteInput()) {
        // При активном overlay-курcоре хотим стабильный mouse-capture и на следующем кадре тоже,
        // чтобы исключить кратковременные провалы WantCaptureMouse=0 между WndProc/NewFrame.
        ImGui::SetNextFrameWantCaptureMouse(true);
    }
    perf.backendNewFrameMs += PerfNowMs() - stageBeginMs;

    if (renderCallback_) {
        stageBeginMs = PerfNowMs();
        renderCallback_(device);
        perf.renderUiMs = PerfNowMs() - stageBeginMs;
    }
    RefreshHelperWindowHitRects();
    UpdateInputCaptureState();

    // Helper draws its own software cursor only while it owns mouse routing.
    // SA:MP cursor mode is now only a game-control lock, not the Helper UI cursor.
    {
        ImGuiIO& ioFrame = ImGui::GetIO();
        ioFrame.MouseDrawCursor = drawHelperCursorThisFrame;
    }

    stageBeginMs = PerfNowMs();
    ImGui::EndFrame();
    ImGui::Render();
    perf.imguiRenderMs = PerfNowMs() - stageBeginMs;
    ImDrawData* drawData = ImGui::GetDrawData();
    CaptureDrawDataStats(perf, drawData);
    if (drawData && drawData->TotalVtxCount > 0) {
        stageBeginMs = PerfNowMs();
        ImGui_ImplDX9_RenderDrawData(drawData);
        perf.renderDrawMs = PerfNowMs() - stageBeginMs;
    } else {
        perf.drawSkipped = true;
    }

    TraceUiRenderAndInputSnapshot("after_present_ui");

    stageBeginMs = PerfNowMs();
    if (vertexShader) {
        device->SetVertexShader(vertexShader);
        vertexShader->Release();
    }

    if (vertexDeclaration) {
        device->SetVertexDeclaration(vertexDeclaration);
        vertexDeclaration->Release();
    }

    if (stateBlock) {
        stateBlock->Apply();
        stateBlock->Release();
    }
    perf.stateRestoreMs = PerfNowMs() - stageBeginMs;
    perf.totalMs = PerfNowMs() - beginMs;
    const bool slow = perf.totalMs >= static_cast<double>(kSlowFrameTraceThresholdMs);
    AccumulateUiFramePerf(perf, slow);
    if (slow) {
        const bool firstSlowFrame = !slowFrameSeen_;
        slowFrameSeen_ = true;
        debuglog::WriteInfo(
            "[ui][perf] full_ui slow total=%.2fms first=%d state=%.2fms new=%.2fms prep=%.2fms ui=%.2fms render=%.2fms draw=%.2fms restore=%.2fms drawSkip=%d win=%d vtx=%d idx=%d cmdLists=%d menu=%d aux=%d wantRoute=%d routeAllowed=%d canRoute=%d swallowMouse=%d drawCur=%d helperRectsReady=%d helperRects=%zu resetMouse=%d switchedSurface=%d",
            perf.totalMs,
            firstSlowFrame ? 1 : 0,
            perf.stateBackupMs,
            perf.backendNewFrameMs,
            perf.prepareFrameMs,
            perf.renderUiMs,
            perf.imguiRenderMs,
            perf.renderDrawMs,
            perf.stateRestoreMs,
            perf.drawSkipped ? 1 : 0,
            perf.windows,
            perf.vertices,
            perf.indices,
            perf.cmdLists,
            menuOpen_ ? 1 : 0,
            auxiliaryVisible ? 1 : 0,
            wantsInputRouting ? 1 : 0,
            inputRoutingAllowed_ ? 1 : 0,
            CanRouteInput() ? 1 : 0,
            ShouldSwallowMouseInput() ? 1 : 0,
            drawHelperCursorThisFrame ? 1 : 0,
            helperWindowHitTestReady_ ? 1 : 0,
            helperWindowHitRects_.size(),
            resetMouseInputState ? 1 : 0,
            switchedUiSurface ? 1 : 0);
    }
}

bool ImGuiOverlay::HandleTextInputMessage(UINT message, WPARAM wparam, LPARAM lparam) const {
    if (!imguiInitialized_ || !WantsTextInputCapture() || ImGui::GetCurrentContext() == nullptr) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();

    switch (message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 || (GetKeyState(VK_MENU) & 0x8000) != 0) {
            return false;
        }

        switch (wparam) {
        case VK_BACK:
        case VK_TAB:
        case VK_RETURN:
        case VK_ESCAPE:
        case VK_DELETE:
            return false;
        default:
            break;
        }

        BYTE keyboardState[256]{};
        if (!GetKeyboardState(keyboardState)) {
            return false;
        }

        const UINT scanCode = static_cast<UINT>((lparam >> 16) & 0xFF);
        WCHAR translated[8]{};
        const int translatedCount = ToUnicodeEx(
            static_cast<UINT>(wparam),
            scanCode,
            keyboardState,
            translated,
            static_cast<int>(std::size(translated)),
            0,
            GetKeyboardLayout(0));

        if (translatedCount < 0) {
            WCHAR clearState[8]{};
            ToUnicodeEx(
                static_cast<UINT>(wparam),
                scanCode,
                keyboardState,
                clearState,
                static_cast<int>(std::size(clearState)),
                0,
                GetKeyboardLayout(0));
            return false;
        }

        for (int i = 0; i < translatedCount; ++i) {
            if (translated[i] != 0) {
                io.AddInputCharacterUTF16(static_cast<unsigned short>(translated[i]));
            }
        }

        return false;
    }
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_IME_CHAR:
    case WM_IME_COMPOSITION:
        return true;
    default:
        return false;
    }
}

bool ImGuiOverlay::IsMouseMessage(UINT message) const {
    switch (message) {
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
        return true;
    default:
        return false;
    }
}

bool ImGuiOverlay::IsKeyboardMessage(UINT message) const {
    switch (message) {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_IME_CHAR:
    case WM_IME_COMPOSITION:
        return true;
    default:
        return false;
    }
}

LRESULT CALLBACK ImGuiOverlay::OverlayWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (self_ && self_->imguiInitialized_) {
        if (!self_->IsInputPipelineEnabled()) {
            if (self_->originalWndProc_) {
                return self_->CallPreviousWndProc(hwnd, message, wparam, lparam);
            }
            return DefWindowProc(hwnd, message, wparam, lparam);
        }
        const bool appFg = self_->IsGameWindowForeground();
        if (appFg && self_->HandleMenuToggleHotkeyCaptureMessage(message, wparam)) {
            return TRUE;
        }

        const bool canRouteInput = appFg && self_->CanRouteInput();
        const bool mouseMessage = self_->IsMouseMessage(message);
        const bool swallowMouseMessage = canRouteInput && mouseMessage && self_->ShouldSwallowMouseMessage(message, lparam);

        if (canRouteInput
            && (!mouseMessage || swallowMouseMessage)
            && self_->windowMessageCallback_
            && self_->windowMessageCallback_(message, wparam, lparam)) {
            if (mouseMessage) {
                self_->UpdateMouseSwallowLatch(message, wparam, swallowMouseMessage);
            }
            if constexpr (kVerboseUiTraceEnabled) {
                if (message != WM_MOUSEMOVE) {
                    debuglog::WriteInfo(
                        "[ui] binder swallowed msg=%u wParam=%p",
                        static_cast<unsigned>(message),
                        reinterpret_cast<void*>(static_cast<uintptr_t>(wparam)));
                }
            }
            return TRUE;
        }

        if (canRouteInput) {
            const bool wantsUiCursor = self_->WantsUiCursor();
            const bool wantsTextInput = self_->WantsTextInputCapture();
            if (wantsTextInput && self_->HandleTextInputMessage(message, wparam, lparam)) {
                return TRUE;
            }

            if (mouseMessage && !swallowMouseMessage) {
                self_->UpdateMouseSwallowLatch(message, wparam, false);
                return self_->CallPreviousWndProc(hwnd, message, wparam, lparam);
            }

            if (self_->WantsInputRouting() && mouseMessage) {
                self_->SyncOsMouseToImGui();
            }
            ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam);
            if (ImGui::GetCurrentContext() != nullptr) {
                const ImGuiIO& io = ImGui::GetIO();
                // Swallow only mouse messages that hit a Helper window, or a drag/release latched from one.
                const bool wantsKeyboardCapture = wantsTextInput || io.WantCaptureKeyboard;
                TraceWheelMessage(message, wparam, wantsUiCursor, io);
                if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_RBUTTONDOWN
                    || message == WM_RBUTTONUP || message == WM_MBUTTONDOWN || message == WM_MBUTTONUP) {
                    if constexpr (kVerboseUiTraceEnabled) {
                        const bool eatMouse = mouseMessage && swallowMouseMessage;
                        const int clientX = GET_X_LPARAM(lparam);
                        const int clientY = GET_Y_LPARAM(lparam);
                        debuglog::WriteInfo(
                            "[ui] wnd btn msg=%u client=(%d,%d) swallowed=%d helperHit=%d latch=0x%X wantsUiCur=%d WantCapMouse=%d wantTxt=%d",
                            static_cast<unsigned>(message),
                            clientX,
                            clientY,
                            eatMouse ? 1 : 0,
                            swallowMouseMessage ? 1 : 0,
                            static_cast<unsigned>(self_->mouseSwallowLatchMask_),
                            wantsUiCursor ? 1 : 0,
                            io.WantCaptureMouse ? 1 : 0,
                            wantsTextInput ? 1 : 0);
                    }
                    // UI курсор есть, а ImGui не просит мышь — типичный «тусклый» кадр; снимок внутреннего состояния.
                    static uint64_t s_lastAnomalyMs = 0;
                    const uint64_t nowBtn = GetTickCount64();
                    const bool popupTransitionExpected =
                        wantsUiCursor && !io.WantCaptureMouse && IsPopupTransitionNoCaptureExpected(io);
                    if (wantsUiCursor && !io.WantCaptureMouse && !popupTransitionExpected && mouseMessage
                        && nowBtn - s_lastAnomalyMs >= kAnomalyClickTraceIntervalMs) {
                        s_lastAnomalyMs = nowBtn;
                        DbgTraceImGuiInternalState("wnd_btn_anomaly");
                    }
                }
                if (mouseMessage && swallowMouseMessage) {
                    self_->UpdateMouseSwallowLatch(message, wparam, true);
                    return TRUE;
                }
                if (message == WM_SETCURSOR && self_->drawHelperCursor_) {
                    ::SetCursor(nullptr);
                    if constexpr (kVerboseUiTraceEnabled) {
                        static uint64_t s_lastSetCursorTraceMs = 0;
                        const uint64_t now = GetTickCount64();
                        if (now - s_lastSetCursorTraceMs >= kSetCursorTraceIntervalMs) {
                            s_lastSetCursorTraceMs = now;
                            debuglog::WriteInfo(
                                "[ui] WM_SETCURSOR hidden for Helper software cursor (WantCapMouse=%d, wantText=%d)",
                                io.WantCaptureMouse ? 1 : 0,
                                wantsTextInput ? 1 : 0);
                        }
                    }
                    return TRUE;
                }
                if (message == WM_SETCURSOR && wantsUiCursor) {
                    if constexpr (kVerboseUiTraceEnabled) {
                        static uint64_t s_lastSetCursorTraceMs = 0;
                        const uint64_t now = GetTickCount64();
                        if (now - s_lastSetCursorTraceMs >= kSetCursorTraceIntervalMs) {
                            s_lastSetCursorTraceMs = now;
                            debuglog::WriteInfo(
                                "[ui] WM_SETCURSOR pass-through without Helper software cursor (WantCapMouse=%d, wantText=%d)",
                                io.WantCaptureMouse ? 1 : 0,
                                wantsTextInput ? 1 : 0);
                        }
                    }
                }
                if (wantsKeyboardCapture && self_->IsKeyboardMessage(message)) {
                    return TRUE;
                }
            } else if (mouseMessage && swallowMouseMessage) {
                self_->UpdateMouseSwallowLatch(message, wparam, true);
                return TRUE;
            }
        }
    }

    if (self_ && self_->originalWndProc_) {
        return self_->CallPreviousWndProc(hwnd, message, wparam, lparam);
    }

    return DefWindowProc(hwnd, message, wparam, lparam);
}

void ImGuiOverlay::Shutdown() {
    debuglog::WriteInfo("[ui] ImGuiOverlay::Shutdown begin");
    shuttingDown_ = true;
    menuOpen_ = false;
    inputRoutingAllowed_ = false;
    drawHelperCursor_ = false;
    inputSwallowMouse_ = false;
    CancelMenuToggleHotkeyCapture();
    ApplyInputCaptureState(false);

    CleanupImGui();

    debuglog::WriteInfo(
        "[ui][d3d] removing D3D hooks installed=%d EndScene=%p Present=%p Reset=%p",
        hooksInstalled_ ? 1 : 0,
        endSceneTarget_,
        presentTarget_,
        resetTarget_);
    minhook::DisableAndRemoveHook(endSceneTarget_, "IDirect3DDevice9::EndScene");
    minhook::DisableAndRemoveHook(presentTarget_, "IDirect3DDevice9::Present");
    minhook::DisableAndRemoveHook(resetTarget_, "IDirect3DDevice9::Reset");
    debuglog::WriteInfo("[ui][d3d] D3D hooks removed");
    originalEndScene_ = nullptr;
    originalPresent_ = nullptr;
    originalReset_ = nullptr;
    endSceneTarget_ = nullptr;
    presentTarget_ = nullptr;
    resetTarget_ = nullptr;
    hooksInstalled_ = false;

    if (initThread_) {
        CloseHandle(initThread_);
        initThread_ = nullptr;
    }

    menuToggleWasDown_ = false;
    inputCaptureChangedCallback_ = nullptr;
    slowFrameSeen_ = false;
    self_ = nullptr;
    debuglog::WriteInfo("[ui] ImGuiOverlay::Shutdown done");
}
