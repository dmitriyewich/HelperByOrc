#include "mod_app.h"

#include "app_config.h"
#include "debug_log.h"
#include "feature_flags.h"
#include "minhook_utils.h"
#include "resource.h"
#include "ui_icons.h"
#include "ui_settings.h"

#include <GameVersion.h>
#include <d3dx9tex.h>
#include <imgui.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <tlhelp32.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr float kSidebarExpandedWidth = 148.0f;
constexpr float kSidebarCollapsedWidth = 50.0f;
constexpr float kLogoExpandedSize = 128.0f;
constexpr float kLogoCollapsedSize = 50.0f;
constexpr float kWindowMargin = 12.0f;
constexpr bool kHelperMouseSuppressionTraceEnabled = false;
constexpr std::string_view kShellSectionName = "shell";
constexpr char kShellMainWindowName[] = "main_window";
constexpr ImGuiChildFlags kBorderedChildFlags = ImGuiChildFlags_Borders;
constexpr ImGuiChildFlags kPlainChildFlags = ImGuiChildFlags_None;
constexpr int kHelperMouseSuppressionReleaseFrames = 2;
constexpr std::uint8_t kHelperSuppressibleMouseButtons =
    static_cast<std::uint8_t>(SampHooks::MouseButtonLeft)
    | static_cast<std::uint8_t>(SampHooks::MouseButtonRight)
    | static_cast<std::uint8_t>(SampHooks::MouseButtonMiddle);
constexpr std::uint64_t kUiModulePerfTraceIntervalMs = 5000;
constexpr std::uint64_t kUiModuleSlowTraceIntervalMs = 1000;
constexpr double kUiModuleSlowFrameMs = 8.0;

namespace fs = std::filesystem;

struct TabDefinition {
    MainTab tab;
    UiText label;
    UiText compactLabel;
    const char* icon;
};

struct SettingsSectionDefinition {
    UiSettingsSection section;
    UiText label;
};

double UiPerfNowMs() {
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

const char* FrameSurfaceName(ImGuiOverlay::FrameSurface surface) {
    switch (surface) {
    case ImGuiOverlay::FrameSurface::Idle:
        return "idle";
    case ImGuiOverlay::FrameSurface::HudOnly:
        return "hud_only";
    case ImGuiOverlay::FrameSurface::QuickMenu:
        return "quick_menu";
    case ImGuiOverlay::FrameSurface::MainMenu:
        return "main_menu";
    case ImGuiOverlay::FrameSurface::Mixed:
        return "mixed";
    case ImGuiOverlay::FrameSurface::Auxiliary:
    default:
        return "auxiliary";
    }
}

struct RenderUiPerf {
    ImGuiOverlay::FrameSurface surface = ImGuiOverlay::FrameSurface::Idle;
    HudModule::OverlayStats hudStats{};
    HudModule::EditorStats hudEditorStats{};
    NotepadModule::RenderStats notepadStats{};
    bool menuOpen = false;
    int tab = -1;
    double hudOverlayMs = 0.0;
    double mainWindowShellMs = 0.0;
    double activeTabMs = 0.0;
    double binderOverlayMs = 0.0;
    double notificationsMs = 0.0;
    double configWritesMs = 0.0;
    double totalMs = 0.0;
};

void MaxHudStats(HudModule::OverlayStats& target, const HudModule::OverlayStats& source) {
    target.widgets = std::max(target.widgets, source.widgets);
    target.enabledWidgets = std::max(target.enabledWidgets, source.enabledWidgets);
    target.visibleWidgets = std::max(target.visibleWidgets, source.visibleWidgets);
    target.refreshEveryFrameWidgets = std::max(target.refreshEveryFrameWidgets, source.refreshEveryFrameWidgets);
    target.elements = std::max(target.elements, source.elements);
    target.visibleElements = std::max(target.visibleElements, source.visibleElements);
    target.refreshedWidgets = std::max(target.refreshedWidgets, source.refreshedWidgets);
    target.expandedElements = std::max(target.expandedElements, source.expandedElements);
    target.staticRefreshSkips = std::max(target.staticRefreshSkips, source.staticRefreshSkips);
}

void MaxNotepadStats(NotepadModule::RenderStats& target, const NotepadModule::RenderStats& source) {
    target.folders = std::max(target.folders, source.folders);
    target.notes = std::max(target.notes, source.notes);
    target.editing = target.editing || source.editing;
    target.copyLineMode = target.copyLineMode || source.copyLineMode;
    target.applyTags = target.applyTags || source.applyTags;
    target.renderedBytes = std::max(target.renderedBytes, source.renderedBytes);
    target.previewCacheHits = std::max(target.previewCacheHits, source.previewCacheHits);
    target.previewCacheMisses = std::max(target.previewCacheMisses, source.previewCacheMisses);
    target.totalMs = std::max(target.totalMs, source.totalMs);
    target.loadMs = std::max(target.loadMs, source.loadMs);
    target.shortcutsMs = std::max(target.shortcutsMs, source.shortcutsMs);
    target.leftPanelMs = std::max(target.leftPanelMs, source.leftPanelMs);
    target.rightPanelMs = std::max(target.rightPanelMs, source.rightPanelMs);
    target.readPreviewMs = std::max(target.readPreviewMs, source.readPreviewMs);
    target.editPreviewMs = std::max(target.editPreviewMs, source.editPreviewMs);
    target.copyLinesMs = std::max(target.copyLinesMs, source.copyLinesMs);
    target.tagsMs = std::max(target.tagsMs, source.tagsMs);
    target.drawPreviewMs = std::max(target.drawPreviewMs, source.drawPreviewMs);
    target.modalsMs = std::max(target.modalsMs, source.modalsMs);
}

void MaxHudEditorStats(HudModule::EditorStats& target, const HudModule::EditorStats& source) {
    target.widgets = std::max(target.widgets, source.widgets);
    target.elements = std::max(target.elements, source.elements);
    target.selectedElements = std::max(target.selectedElements, source.selectedElements);
    target.totalMs = std::max(target.totalMs, source.totalMs);
    target.loadMs = std::max(target.loadMs, source.loadMs);
    target.beginFrameMs = std::max(target.beginFrameMs, source.beginFrameMs);
    target.toolbarMs = std::max(target.toolbarMs, source.toolbarMs);
    target.workspaceMs = std::max(target.workspaceMs, source.workspaceMs);
    target.widgetListMs = std::max(target.widgetListMs, source.widgetListMs);
    target.layersMs = std::max(target.layersMs, source.layersMs);
    target.canvasMs = std::max(target.canvasMs, source.canvasMs);
    target.inspectorMs = std::max(target.inspectorMs, source.inspectorMs);
    target.variablesPopupMs = std::max(target.variablesPopupMs, source.variablesPopupMs);
}

void AccumulateRenderUiPerf(const RenderUiPerf& perf) {
    static std::uint64_t s_windowStartMs = 0;
    static std::uint64_t s_lastSlowTraceMs = 0;
    static unsigned s_frames = 0;
    static unsigned s_menuFrames = 0;
    static unsigned s_slowFrames = 0;
    static unsigned s_surfaceHudOnly = 0;
    static unsigned s_surfaceQuickMenu = 0;
    static unsigned s_surfaceMainMenu = 0;
    static unsigned s_surfaceMixed = 0;
    static unsigned s_surfaceAuxiliary = 0;
    static double s_totalMs = 0.0;
    static double s_maxTotalMs = 0.0;
    static double s_maxHudOverlayMs = 0.0;
    static double s_maxShellMs = 0.0;
    static double s_maxActiveTabMs = 0.0;
    static double s_maxBinderOverlayMs = 0.0;
    static double s_maxNotificationsMs = 0.0;
    static double s_maxConfigWritesMs = 0.0;
    static int s_maxActiveTab = -1;
    static HudModule::OverlayStats s_maxHudStats{};
    static HudModule::EditorStats s_maxHudEditorStats{};
    static NotepadModule::RenderStats s_maxNotepadStats{};

    const std::uint64_t now = GetTickCount64();
    if (s_windowStartMs == 0) {
        s_windowStartMs = now;
    }

    ++s_frames;
    s_totalMs += perf.totalMs;
    if (perf.menuOpen) {
        ++s_menuFrames;
    }
    if (perf.totalMs >= kUiModuleSlowFrameMs) {
        ++s_slowFrames;
        if (now - s_lastSlowTraceMs >= kUiModuleSlowTraceIntervalMs) {
            s_lastSlowTraceMs = now;
            debuglog::WriteInfo(
                "[ui][perf][modules] slow total=%.2fms surface=%s menu=%d tab=%d hud=%.2fms shell=%.2fms tabMs=%.2fms binder=%.2fms notifications=%.2fms config=%.2fms hudWidgets=%d hudEnabled=%d hudVisible=%d hudRefresh0=%d hudElements=%d hudVisibleElements=%d hudRefreshed=%d hudExpanded=%d hudStaticSkip=%d heTotal=%.2fms heToolbar=%.2fms heWorkspace=%.2fms heList=%.2fms heLayers=%.2fms heCanvas=%.2fms heInspector=%.2fms heVars=%.2fms heWidgets=%d heElements=%d heSelected=%d npTotal=%.2fms npLoad=%.2fms npShort=%.2fms npLeft=%.2fms npRight=%.2fms npRead=%.2fms npEdit=%.2fms npCopy=%.2fms npTags=%.2fms npDraw=%.2fms npModals=%.2fms npNotes=%d npBytes=%zu npHit=%d npMiss=%d npEditing=%d",
                perf.totalMs,
                FrameSurfaceName(perf.surface),
                perf.menuOpen ? 1 : 0,
                perf.tab,
                perf.hudOverlayMs,
                perf.mainWindowShellMs,
                perf.activeTabMs,
                perf.binderOverlayMs,
                perf.notificationsMs,
                perf.configWritesMs,
                perf.hudStats.widgets,
                perf.hudStats.enabledWidgets,
                perf.hudStats.visibleWidgets,
                perf.hudStats.refreshEveryFrameWidgets,
                perf.hudStats.elements,
                perf.hudStats.visibleElements,
                perf.hudStats.refreshedWidgets,
                perf.hudStats.expandedElements,
                perf.hudStats.staticRefreshSkips,
                perf.hudEditorStats.totalMs,
                perf.hudEditorStats.toolbarMs,
                perf.hudEditorStats.workspaceMs,
                perf.hudEditorStats.widgetListMs,
                perf.hudEditorStats.layersMs,
                perf.hudEditorStats.canvasMs,
                perf.hudEditorStats.inspectorMs,
                perf.hudEditorStats.variablesPopupMs,
                perf.hudEditorStats.widgets,
                perf.hudEditorStats.elements,
                perf.hudEditorStats.selectedElements,
                perf.notepadStats.totalMs,
                perf.notepadStats.loadMs,
                perf.notepadStats.shortcutsMs,
                perf.notepadStats.leftPanelMs,
                perf.notepadStats.rightPanelMs,
                perf.notepadStats.readPreviewMs,
                perf.notepadStats.editPreviewMs,
                perf.notepadStats.copyLinesMs,
                perf.notepadStats.tagsMs,
                perf.notepadStats.drawPreviewMs,
                perf.notepadStats.modalsMs,
                perf.notepadStats.notes,
                perf.notepadStats.renderedBytes,
                perf.notepadStats.previewCacheHits,
                perf.notepadStats.previewCacheMisses,
                perf.notepadStats.editing ? 1 : 0);
        }
    }

    switch (perf.surface) {
    case ImGuiOverlay::FrameSurface::HudOnly:
        ++s_surfaceHudOnly;
        break;
    case ImGuiOverlay::FrameSurface::QuickMenu:
        ++s_surfaceQuickMenu;
        break;
    case ImGuiOverlay::FrameSurface::MainMenu:
        ++s_surfaceMainMenu;
        break;
    case ImGuiOverlay::FrameSurface::Mixed:
        ++s_surfaceMixed;
        break;
    case ImGuiOverlay::FrameSurface::Auxiliary:
        ++s_surfaceAuxiliary;
        break;
    case ImGuiOverlay::FrameSurface::Idle:
    default:
        break;
    }

    if (perf.totalMs > s_maxTotalMs) {
        s_maxTotalMs = perf.totalMs;
        s_maxActiveTab = perf.tab;
    }
    s_maxHudOverlayMs = std::max(s_maxHudOverlayMs, perf.hudOverlayMs);
    s_maxShellMs = std::max(s_maxShellMs, perf.mainWindowShellMs);
    s_maxActiveTabMs = std::max(s_maxActiveTabMs, perf.activeTabMs);
    s_maxBinderOverlayMs = std::max(s_maxBinderOverlayMs, perf.binderOverlayMs);
    s_maxNotificationsMs = std::max(s_maxNotificationsMs, perf.notificationsMs);
    s_maxConfigWritesMs = std::max(s_maxConfigWritesMs, perf.configWritesMs);
    MaxHudStats(s_maxHudStats, perf.hudStats);
    MaxHudEditorStats(s_maxHudEditorStats, perf.hudEditorStats);
    MaxNotepadStats(s_maxNotepadStats, perf.notepadStats);

    if (now - s_windowStartMs < kUiModulePerfTraceIntervalMs) {
        return;
    }

    debuglog::WriteInfo(
        "[ui][perf][modules] 5s frames=%u menu=%u slow=%u surfaceHud=%u surfaceQuick=%u surfaceMenu=%u surfaceMixed=%u surfaceAux=%u avg=%.2fms max=%.2fms maxHud=%.2fms maxShell=%.2fms maxTab=%.2fms maxBinder=%.2fms maxNotifications=%.2fms maxConfig=%.2fms maxTabId=%d hudWidgets=%d hudEnabled=%d hudVisible=%d hudRefresh0=%d hudElements=%d hudVisibleElements=%d hudRefreshed=%d hudExpanded=%d hudStaticSkip=%d heMax=%.2fms heToolbar=%.2fms heWorkspace=%.2fms heList=%.2fms heLayers=%.2fms heCanvas=%.2fms heInspector=%.2fms heVars=%.2fms heWidgets=%d heElements=%d heSelected=%d npMax=%.2fms npLoad=%.2fms npShort=%.2fms npLeft=%.2fms npRight=%.2fms npRead=%.2fms npEdit=%.2fms npCopy=%.2fms npTags=%.2fms npDraw=%.2fms npModals=%.2fms npNotes=%d npBytes=%zu npHit=%d npMiss=%d npEditing=%d",
        s_frames,
        s_menuFrames,
        s_slowFrames,
        s_surfaceHudOnly,
        s_surfaceQuickMenu,
        s_surfaceMainMenu,
        s_surfaceMixed,
        s_surfaceAuxiliary,
        s_frames > 0 ? s_totalMs / static_cast<double>(s_frames) : 0.0,
        s_maxTotalMs,
        s_maxHudOverlayMs,
        s_maxShellMs,
        s_maxActiveTabMs,
        s_maxBinderOverlayMs,
        s_maxNotificationsMs,
        s_maxConfigWritesMs,
        s_maxActiveTab,
        s_maxHudStats.widgets,
        s_maxHudStats.enabledWidgets,
        s_maxHudStats.visibleWidgets,
        s_maxHudStats.refreshEveryFrameWidgets,
        s_maxHudStats.elements,
        s_maxHudStats.visibleElements,
        s_maxHudStats.refreshedWidgets,
        s_maxHudStats.expandedElements,
        s_maxHudStats.staticRefreshSkips,
        s_maxHudEditorStats.totalMs,
        s_maxHudEditorStats.toolbarMs,
        s_maxHudEditorStats.workspaceMs,
        s_maxHudEditorStats.widgetListMs,
        s_maxHudEditorStats.layersMs,
        s_maxHudEditorStats.canvasMs,
        s_maxHudEditorStats.inspectorMs,
        s_maxHudEditorStats.variablesPopupMs,
        s_maxHudEditorStats.widgets,
        s_maxHudEditorStats.elements,
        s_maxHudEditorStats.selectedElements,
        s_maxNotepadStats.totalMs,
        s_maxNotepadStats.loadMs,
        s_maxNotepadStats.shortcutsMs,
        s_maxNotepadStats.leftPanelMs,
        s_maxNotepadStats.rightPanelMs,
        s_maxNotepadStats.readPreviewMs,
        s_maxNotepadStats.editPreviewMs,
        s_maxNotepadStats.copyLinesMs,
        s_maxNotepadStats.tagsMs,
        s_maxNotepadStats.drawPreviewMs,
        s_maxNotepadStats.modalsMs,
        s_maxNotepadStats.notes,
        s_maxNotepadStats.renderedBytes,
        s_maxNotepadStats.previewCacheHits,
        s_maxNotepadStats.previewCacheMisses,
        s_maxNotepadStats.editing ? 1 : 0);

    s_windowStartMs = now;
    s_frames = 0;
    s_menuFrames = 0;
    s_slowFrames = 0;
    s_surfaceHudOnly = 0;
    s_surfaceQuickMenu = 0;
    s_surfaceMainMenu = 0;
    s_surfaceMixed = 0;
    s_surfaceAuxiliary = 0;
    s_totalMs = 0.0;
    s_maxTotalMs = 0.0;
    s_maxHudOverlayMs = 0.0;
    s_maxShellMs = 0.0;
    s_maxActiveTabMs = 0.0;
    s_maxBinderOverlayMs = 0.0;
    s_maxNotificationsMs = 0.0;
    s_maxConfigWritesMs = 0.0;
    s_maxActiveTab = -1;
    s_maxHudStats = {};
    s_maxHudEditorStats = {};
    s_maxNotepadStats = {};
}

const std::array<TabDefinition, 6> kTabs = {{
    { MainTab::Home, UiText::TabHome, UiText::TabHomeCompact, ui_icons::House },
    { MainTab::Binder, UiText::TabBinder, UiText::TabBinderCompact, ui_icons::Keyboard },
    { MainTab::Hud, UiText::TabHud, UiText::TabHudCompact, ui_icons::Sliders },
    { MainTab::Misc, UiText::TabMisc, UiText::TabMiscCompact, ui_icons::Cubes },
    { MainTab::Notepad, UiText::TabNotepad, UiText::TabNotepadCompact, ui_icons::Book },
    { MainTab::Settings, UiText::TabSettings, UiText::TabSettingsCompact, ui_icons::Gear },
}};

const std::array<SettingsSectionDefinition, 7> kSettingsSections = {{
    { UiSettingsSection::General, UiText::SettingsSectionGeneral },
    { UiSettingsSection::Binder, UiText::SettingsSectionBinder },
    { UiSettingsSection::QuickMenu, UiText::SettingsSectionQuickMenu },
    { UiSettingsSection::Notifications, UiText::SettingsSectionNotifications },
    { UiSettingsSection::Profiles, UiText::SettingsSectionProfiles },
    { UiSettingsSection::Hotkeys, UiText::SettingsSectionHotkeys },
    { UiSettingsSection::Diagnostics, UiText::SettingsSectionDiagnostics },
}};

std::uint8_t MouseButtonMaskFromWindowMessage(UINT message, WPARAM wparam) {
    UNREFERENCED_PARAMETER(wparam);

    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
        return static_cast<std::uint8_t>(SampHooks::MouseButtonLeft);
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_RBUTTONUP:
        return static_cast<std::uint8_t>(SampHooks::MouseButtonRight);
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_MBUTTONUP:
        return static_cast<std::uint8_t>(SampHooks::MouseButtonMiddle);
    default:
        return 0;
    }
}

std::uint8_t HeldMouseButtonMask() {
    std::uint8_t mask = 0;
    if ((::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) {
        mask |= static_cast<std::uint8_t>(SampHooks::MouseButtonLeft);
    }
    if ((::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0) {
        mask |= static_cast<std::uint8_t>(SampHooks::MouseButtonRight);
    }
    if ((::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0) {
        mask |= static_cast<std::uint8_t>(SampHooks::MouseButtonMiddle);
    }
    return mask;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }

    std::string result(static_cast<std::size_t>(required - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), required, nullptr, nullptr);
    return result;
}

std::string PathToUtf8(const fs::path& path) {
    return WideToUtf8(path.wstring());
}

struct ImGuiStringUserData {
    std::string* value = nullptr;
};

int ImGuiStringResizeCallback(ImGuiInputTextCallbackData* data) {
    auto* userData = static_cast<ImGuiStringUserData*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize && userData && userData->value) {
        userData->value->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = userData->value->data();
    }
    return 0;
}

bool InputTextString(const char* label, std::string& value, ImGuiInputTextFlags flags = 0, std::size_t minBuffer = 128) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value };
    return ImGui::InputText(
        label,
        value.data(),
        value.capacity() + 1,
        flags | ImGuiInputTextFlags_CallbackResize,
        ImGuiStringResizeCallback,
        &userData);
}

std::string TrimAsciiWhitespace(std::string_view value) {
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

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string NormalizePathForCompare(std::string value) {
    std::replace(value.begin(), value.end(), '/', '\\');
    return LowerAscii(std::move(value));
}

bool StartsWithNoCase(const std::string& value, const std::string& prefix) {
    const std::string lowerValue = LowerAscii(value);
    const std::string lowerPrefix = LowerAscii(prefix);
    return lowerValue.size() >= lowerPrefix.size() && lowerValue.compare(0, lowerPrefix.size(), lowerPrefix) == 0;
}

std::string JoinTags(const std::vector<const char*>& tags) {
    if (tags.empty()) {
        return "-";
    }

    std::string result;
    for (const char* tag : tags) {
        if (!result.empty()) {
            result += ",";
        }
        result += tag;
    }
    return result;
}

std::vector<const char*> ConflictTagsForPath(const std::string& path) {
    const std::string lower = LowerAscii(path);
    std::vector<const char*> tags;

    const auto addIf = [&](const char* token, const char* tag) {
        if (lower.find(token) != std::string::npos) {
            tags.push_back(tag);
        }
    };

    addIf("sampfuncs", "sampfuncs");
    addIf("moonloader", "moonloader");
    addIf("cleo", "cleo");
    addIf("modloader", "modloader");
    addIf("mousefix", "mousefix");
    addIf("silentpatch", "silentpatch");
    addIf("ginput", "input-hook");
    addIf("cursor", "cursor-hook");
    addIf("rawinput", "input-hook");
    addIf("widescreen", "widescreen");
    addIf("skygfx", "graphics-hook");
    addIf("reshade", "graphics-hook");
    addIf("enb", "graphics-hook");
    addIf("dxvk", "graphics-hook");
    addIf("d3d9.dll", "d3d9-proxy");
    addIf("dinput8.dll", "input-proxy");
    addIf("ddraw.dll", "graphics-proxy");
    addIf("dxgi.dll", "graphics-proxy");
    addIf("winmm.dll", "loader-proxy");
    addIf("vorbishooked", "loader-proxy");
    addIf("crash", "crashfix");
    addIf("anticrash", "crashfix");
    addIf("samp addon", "samp-addon");
    addIf("sampaddon", "samp-addon");
    if constexpr (feature_flags::kEnableArizonaIntegration) {
        addIf("_chat.asi", "chat-hook");
        addIf("chat.asi", "chat-hook");
        addIf("libcef.asi", "cef-ui");
        addIf("\\cef\\loader.dll", "cef-ui");
    }
    addIf("rivatuner", "overlay");
    addIf("rtss", "overlay");
    addIf("discord", "overlay");
    addIf("gameoverlayrenderer", "overlay");
    addIf("steam", "overlay");
    addIf("overwolf", "overlay");
    addIf("fraps", "overlay");
    addIf("msiafterburner", "overlay");
    addIf("obs", "overlay");

    return tags;
}

std::vector<const char*> AppCompatTagsForData(const std::string& data) {
    const std::string lower = LowerAscii(data);
    std::vector<const char*> tags;

    const auto addIf = [&](const char* token, const char* tag) {
        if (lower.find(token) != std::string::npos) {
            tags.push_back(tag);
        }
    };

    addIf("disabledxmaximizedwindowedmode", "disable-fullscreen-optimizations");
    addIf("dwm8and16bitmitigation", "dwm8-16bit-mitigation");
    addIf("runasadmin", "run-as-admin");
    addIf("win7rtm", "win7-compat");
    addIf("win8rtm", "win8-compat");
    addIf("winxpsp", "xp-compat");
    addIf("vista", "vista-compat");
    addIf("highdpiaware", "high-dpi-aware");
    addIf("dpiunaware", "dpi-unaware");
    addIf("disablethemes", "disable-themes");
    addIf("disabledwm", "disable-dwm");
    addIf("ignorefreelibrary", "ignore-free-library");
    addIf("256color", "256-color");
    addIf("640x480", "640x480");
    if (lower.find('$') != std::string::npos) {
        tags.push_back("shim-db");
    }

    return tags;
}

const char* RegistryTypeName(DWORD type) {
    switch (type) {
    case REG_NONE:
        return "REG_NONE";
    case REG_SZ:
        return "REG_SZ";
    case REG_EXPAND_SZ:
        return "REG_EXPAND_SZ";
    case REG_BINARY:
        return "REG_BINARY";
    case REG_DWORD:
        return "REG_DWORD";
    case REG_MULTI_SZ:
        return "REG_MULTI_SZ";
    case REG_QWORD:
        return "REG_QWORD";
    default:
        return "REG_OTHER";
    }
}

std::string GetModulePathUtf8(HMODULE module) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(module, path, MAX_PATH)) {
        return {};
    }
    return WideToUtf8(path);
}

fs::path GetModulePathFs(HMODULE module) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(module, path, MAX_PATH)) {
        return {};
    }
    return fs::path(path);
}

fs::path GetModuleDirectory(HMODULE module) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(module, path, MAX_PATH)) {
        return {};
    }
    fs::path modulePath(path);
    return modulePath.parent_path();
}

std::string FileTimeToText(const FILETIME& fileTime) {
    SYSTEMTIME utc{};
    SYSTEMTIME local{};
    if (!FileTimeToSystemTime(&fileTime, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
        return "unknown";
    }

    char buffer[64]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%04u-%02u-%02u %02u:%02u:%02u",
        local.wYear,
        local.wMonth,
        local.wDay,
        local.wHour,
        local.wMinute,
        local.wSecond);
    return buffer;
}

std::uint64_t Fnva64File(const fs::path& path, bool& ok) {
    ok = false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }

    std::uint64_t hash = 14695981039346656037ull;
    std::array<std::uint8_t, 64 * 1024> buffer{};
    DWORD bytesRead = 0;
    while (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0) {
        for (DWORD i = 0; i < bytesRead; ++i) {
            hash ^= buffer[i];
            hash *= 1099511628211ull;
        }
    }

    ok = GetLastError() == ERROR_HANDLE_EOF || bytesRead == 0;
    CloseHandle(file);
    return hash;
}

void LogPeFileDetails(const fs::path& path, const char* label) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        debuglog::WriteError("[diag][file] %s PE open failed gle=%lu path=\"%s\"",
            label,
            static_cast<unsigned long>(GetLastError()),
            PathToUtf8(path).c_str());
        return;
    }

    IMAGE_DOS_HEADER dos{};
    DWORD bytesRead = 0;
    bool ok = ReadFile(file, &dos, sizeof(dos), &bytesRead, nullptr) && bytesRead == sizeof(dos)
        && dos.e_magic == IMAGE_DOS_SIGNATURE;
    IMAGE_NT_HEADERS32 nt{};
    if (ok) {
        SetFilePointer(file, dos.e_lfanew, nullptr, FILE_BEGIN);
        ok = ReadFile(file, &nt, sizeof(nt), &bytesRead, nullptr) && bytesRead == sizeof(nt)
            && nt.Signature == IMAGE_NT_SIGNATURE;
    }

    if (ok) {
        debuglog::WriteInfo(
            "[diag][file] %s PE entry=0x%08X imageBase=0x%08X sizeOfImage=0x%X timestamp=0x%08X checksum=0x%08X sections=%u path=\"%s\"",
            label,
            static_cast<unsigned>(nt.OptionalHeader.AddressOfEntryPoint),
            static_cast<unsigned>(nt.OptionalHeader.ImageBase),
            static_cast<unsigned>(nt.OptionalHeader.SizeOfImage),
            static_cast<unsigned>(nt.FileHeader.TimeDateStamp),
            static_cast<unsigned>(nt.OptionalHeader.CheckSum),
            static_cast<unsigned>(nt.FileHeader.NumberOfSections),
            PathToUtf8(path).c_str());
    } else {
        debuglog::WriteError("[diag][file] %s PE parse failed path=\"%s\"", label, PathToUtf8(path).c_str());
    }

    CloseHandle(file);
}

void LogFileFingerprint(const fs::path& path, const char* label) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        debuglog::WriteInfo(
            "[diag][file] %s missing gle=%lu path=\"%s\"",
            label,
            static_cast<unsigned long>(GetLastError()),
            PathToUtf8(path).c_str());
        return;
    }

    const std::uint64_t size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32u) | data.nFileSizeLow;
    bool hashOk = false;
    const std::uint64_t hash = Fnva64File(path, hashOk);
    const std::string pathText = PathToUtf8(path);
    const std::string tags = JoinTags(ConflictTagsForPath(pathText));
    debuglog::WriteInfo(
        "[diag][file] %s size=%llu mtime=\"%s\" fnv64=%016llX hashOk=%d tags=%s path=\"%s\"",
        label,
        static_cast<unsigned long long>(size),
        FileTimeToText(data.ftLastWriteTime).c_str(),
        static_cast<unsigned long long>(hash),
        hashOk ? 1 : 0,
        tags.c_str(),
        pathText.c_str());
}

bool ShouldInventoryFile(const fs::path& path) {
    const std::string lowerName = LowerAscii(PathToUtf8(path.filename()));
    const std::string lowerExt = LowerAscii(PathToUtf8(path.extension()));
    static constexpr const char* kExtensions[] = {
        ".asi", ".dll", ".exe", ".cs", ".cleo", ".lua", ".luac", ".sf", ".asi.disabled", ".dll.disabled",
    };
    for (const char* ext : kExtensions) {
        if (lowerExt == ext || lowerName.ends_with(ext)) {
            return true;
        }
    }
    return !ConflictTagsForPath(lowerName).empty();
}

void LogDirectoryInventory(const fs::path& directory, const char* label, bool recursive, std::size_t limit) {
    std::error_code ec;
    if (!fs::exists(directory, ec)) {
        debuglog::WriteInfo("[diag][inventory] %s missing path=\"%s\"", label, PathToUtf8(directory).c_str());
        return;
    }

    std::size_t matched = 0;
    std::size_t logged = 0;
    const auto logPath = [&](const fs::path& path) {
        if (!ShouldInventoryFile(path)) {
            return;
        }

        ++matched;
        if (logged >= limit) {
            return;
        }

        ++logged;
        LogFileFingerprint(path, label);
    };

    if (recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(directory, fs::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                break;
            }
            if (entry.is_regular_file(ec)) {
                logPath(entry.path());
            }
        }
    } else {
        for (const auto& entry : fs::directory_iterator(directory, fs::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                break;
            }
            if (entry.is_regular_file(ec)) {
                logPath(entry.path());
            }
        }
    }

    debuglog::WriteInfo(
        "[diag][inventory] %s done matched=%llu logged=%llu limit=%llu recursive=%d path=\"%s\"",
        label,
        static_cast<unsigned long long>(matched),
        static_cast<unsigned long long>(logged),
        static_cast<unsigned long long>(limit),
        recursive ? 1 : 0,
        PathToUtf8(directory).c_str());
}

std::string ModuleOrigin(const std::string& path, const std::string& gameDir) {
    wchar_t windowsDir[MAX_PATH]{};
    GetWindowsDirectoryW(windowsDir, MAX_PATH);
    const std::string winDir = WideToUtf8(windowsDir);
    if (!gameDir.empty() && StartsWithNoCase(path, gameDir)) {
        return "game";
    }
    if (!winDir.empty() && StartsWithNoCase(path, winDir)) {
        return "system";
    }
    return "other";
}

void LogLoadedModuleSnapshot(const char* label, bool onlyNew, const fs::path& gameDir) {
    static std::unordered_set<std::string> s_seenModules;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        debuglog::WriteError("[diag][modules] %s snapshot failed gle=%lu", label, static_cast<unsigned long>(GetLastError()));
        return;
    }

    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    std::size_t total = 0;
    std::size_t logged = 0;
    std::size_t suspects = 0;
    const std::string gameDirText = PathToUtf8(gameDir);

    if (Module32FirstW(snapshot, &module)) {
        do {
            ++total;
            const std::string path = WideToUtf8(module.szExePath);
            const std::string key = LowerAscii(path);
            const bool inserted = s_seenModules.insert(key).second;
            if (onlyNew && !inserted) {
                continue;
            }

            const std::vector<const char*> tags = ConflictTagsForPath(path);
            const std::string origin = ModuleOrigin(path, gameDirText);
            const std::string lowerName = LowerAscii(WideToUtf8(module.szModule));
            const bool coreInteresting = lowerName == "samp.dll" || lowerName == "gta_sa.exe"
                || lowerName == "helperbyorc.asi" || lowerName == "d3d9.dll" || lowerName == "dinput8.dll";
            const bool interesting = coreInteresting || !tags.empty() || origin != "system" || !onlyNew;
            if (!interesting) {
                continue;
            }

            if (!tags.empty()) {
                ++suspects;
            }
            ++logged;
            debuglog::WriteInfo(
                "[diag][module] %s new=%d origin=%s base=0x%08X size=0x%X tags=%s name=\"%s\" path=\"%s\"",
                label,
                inserted ? 1 : 0,
                origin.c_str(),
                static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(module.modBaseAddr)),
                static_cast<unsigned>(module.modBaseSize),
                JoinTags(tags).c_str(),
                WideToUtf8(module.szModule).c_str(),
                path.c_str());
        } while (Module32NextW(snapshot, &module));
    }

    CloseHandle(snapshot);
    debuglog::WriteInfo(
        "[diag][modules] %s done total=%llu logged=%llu suspects=%llu onlyNew=%d",
        label,
        static_cast<unsigned long long>(total),
        static_cast<unsigned long long>(logged),
        static_cast<unsigned long long>(suspects),
        onlyNew ? 1 : 0);
}

void LogKnownProxyFiles(const fs::path& gameDir) {
    static constexpr const wchar_t* kProxyNames[] = {
        L"d3d9.dll",
        L"dinput8.dll",
        L"ddraw.dll",
        L"dxgi.dll",
        L"winmm.dll",
        L"vorbisHooked.dll",
        L"vorbisFile.dll",
        L"bass.dll",
    };

    for (const wchar_t* name : kProxyNames) {
        const fs::path path = gameDir / name;
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            LogFileFingerprint(path, "proxy-candidate");
        }
    }
}

constexpr const wchar_t* kAppCompatLayersKey =
    L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";

void LogAppCompatExactLayer(HKEY root, const char* rootName, const fs::path& currentExe) {
    HKEY key = nullptr;
    const LONG openResult = RegOpenKeyExW(root, kAppCompatLayersKey, 0, KEY_READ, &key);
    if (openResult != ERROR_SUCCESS) {
        debuglog::WriteInfo(
            "[diag][appcompat] root=%s exact-current-exe open=0x%08lX found=0 key='%ls' exe='%s'",
            rootName,
            static_cast<unsigned long>(openResult),
            kAppCompatLayersKey,
            PathToUtf8(currentExe).c_str());
        return;
    }

    DWORD valueType = 0;
    DWORD valueBytes = 0;
    LONG queryResult = RegQueryValueExW(
        key,
        currentExe.c_str(),
        nullptr,
        &valueType,
        nullptr,
        &valueBytes);

    std::string valueText;
    if (queryResult == ERROR_SUCCESS && valueBytes > 0) {
        std::vector<BYTE> buffer(valueBytes + sizeof(wchar_t), 0);
        queryResult = RegQueryValueExW(
            key,
            currentExe.c_str(),
            nullptr,
            &valueType,
            buffer.data(),
            &valueBytes);
        if (queryResult == ERROR_SUCCESS && (valueType == REG_SZ || valueType == REG_EXPAND_SZ)) {
            valueText = WideToUtf8(reinterpret_cast<const wchar_t*>(buffer.data()));
        } else if (queryResult == ERROR_SUCCESS) {
            valueText = "<non-string>";
        }
    }

    RegCloseKey(key);
    debuglog::WriteInfo(
        "[diag][appcompat] root=%s exact-current-exe result=0x%08lX found=%d type=%s(%lu) tags=%s exe='%s' data='%s'",
        rootName,
        static_cast<unsigned long>(queryResult),
        queryResult == ERROR_SUCCESS ? 1 : 0,
        RegistryTypeName(valueType),
        static_cast<unsigned long>(valueType),
        JoinTags(AppCompatTagsForData(valueText)).c_str(),
        PathToUtf8(currentExe).c_str(),
        valueText.c_str());
}

void LogAppCompatLayersForRoot(HKEY root, const char* rootName, const fs::path& gameDir, const fs::path& currentExe) {
    LogAppCompatExactLayer(root, rootName, currentExe);

    HKEY key = nullptr;
    const LONG openResult = RegOpenKeyExW(root, kAppCompatLayersKey, 0, KEY_READ, &key);
    if (openResult != ERROR_SUCCESS) {
        debuglog::WriteInfo(
            "[diag][appcompat] root=%s open=0x%08lX matches=0 key='%ls'",
            rootName,
            static_cast<unsigned long>(openResult),
            kAppCompatLayersKey);
        return;
    }

    const std::string gameDirText = NormalizePathForCompare(PathToUtf8(gameDir));
    const std::string currentExeText = NormalizePathForCompare(PathToUtf8(currentExe));
    std::size_t matches = 0;
    for (DWORD index = 0;; ++index) {
        wchar_t valueName[1024]{};
        wchar_t valueData[2048]{};
        DWORD valueNameChars = static_cast<DWORD>(std::size(valueName));
        DWORD valueDataBytes = sizeof(valueData);
        DWORD valueType = 0;

        const LONG enumResult = RegEnumValueW(
            key,
            index,
            valueName,
            &valueNameChars,
            nullptr,
            &valueType,
            reinterpret_cast<LPBYTE>(valueData),
            &valueDataBytes);
        if (enumResult == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (enumResult != ERROR_SUCCESS) {
            debuglog::WriteError(
                "[diag][appcompat] root=%s enum failed index=%lu result=0x%08lX",
                rootName,
                static_cast<unsigned long>(index),
                static_cast<unsigned long>(enumResult));
            break;
        }

        const std::string valueNameText = WideToUtf8(valueName);
        const std::string valueDataText = valueType == REG_SZ || valueType == REG_EXPAND_SZ ? WideToUtf8(valueData) : "<non-string>";
        const std::string lowerName = NormalizePathForCompare(valueNameText);
        std::vector<const char*> matchReasons;
        if (!currentExeText.empty() && lowerName == currentExeText) {
            matchReasons.push_back("exact-current-exe");
        }
        if (!gameDirText.empty() && StartsWithNoCase(lowerName, gameDirText)) {
            matchReasons.push_back("same-game-dir");
        }
        if (lowerName.find("gta_sa.exe") != std::string::npos) {
            matchReasons.push_back("gta-sa");
        }
        if (lowerName.find("samp.exe") != std::string::npos) {
            matchReasons.push_back("samp");
        }
        if (lowerName.find("main.exe") != std::string::npos) {
            matchReasons.push_back("main-exe");
        }
        if constexpr (feature_flags::kEnableArizonaIntegration) {
            if (lowerName.find("arizona") != std::string::npos) {
                matchReasons.push_back("arizona");
            }
        }
        const bool interesting = !matchReasons.empty();
        if (!interesting) {
            continue;
        }

        ++matches;
        debuglog::WriteInfo(
            "[diag][appcompat] root=%s value='%s' type=%s(%lu) match=%s tags=%s data='%s'",
            rootName,
            valueNameText.c_str(),
            RegistryTypeName(valueType),
            static_cast<unsigned long>(valueType),
            JoinTags(matchReasons).c_str(),
            JoinTags(AppCompatTagsForData(valueDataText)).c_str(),
            valueDataText.c_str());
    }

    RegCloseKey(key);
    debuglog::WriteInfo(
        "[diag][appcompat] root=%s matches=%llu gameDir='%s' currentExe='%s'",
        rootName,
        static_cast<unsigned long long>(matches),
        PathToUtf8(gameDir).c_str(),
        PathToUtf8(currentExe).c_str());
}

void LogAppCompatEnvironment() {
    wchar_t compatLayer[4096]{};
    const DWORD compatLayerChars = GetEnvironmentVariableW(
        L"__COMPAT_LAYER",
        compatLayer,
        static_cast<DWORD>(std::size(compatLayer)));
    if (compatLayerChars == 0) {
        debuglog::WriteInfo(
            "[diag][appcompat] env __COMPAT_LAYER present=0 gle=%lu",
            static_cast<unsigned long>(GetLastError()));
        return;
    }

    const bool truncated = compatLayerChars >= std::size(compatLayer);
    const std::string value = WideToUtf8(compatLayer);
    debuglog::WriteInfo(
        "[diag][appcompat] env __COMPAT_LAYER present=1 truncated=%d tags=%s data='%s'",
        truncated ? 1 : 0,
        JoinTags(AppCompatTagsForData(value)).c_str(),
        value.c_str());
}

void LogAppCompatShimModules() {
    static constexpr const wchar_t* kShimModules[] = {
        L"apphelp.dll",
        L"AcLayers.dll",
        L"AcGenral.dll",
        L"AcSpecfc.dll",
        L"AcXtrnal.dll",
        L"AcDwm.dll",
        L"AcRes.dll",
    };

    for (const wchar_t* moduleName : kShimModules) {
        HMODULE module = GetModuleHandleW(moduleName);
        if (!module) {
            debuglog::WriteInfo("[diag][appcompat] shim module=%ls loaded=0", moduleName);
            continue;
        }

        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(module, path, MAX_PATH);
        debuglog::WriteInfo(
            "[diag][appcompat] shim module=%ls loaded=1 base=0x%08X path='%s'",
            moduleName,
            static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(module)),
            WideToUtf8(path).c_str());
    }
}

void LogAppCompatLayers(const fs::path& gameDir, const fs::path& currentExe) {
    LogAppCompatEnvironment();
    LogAppCompatLayersForRoot(HKEY_CURRENT_USER, "HKCU", gameDir, currentExe);
    LogAppCompatLayersForRoot(HKEY_LOCAL_MACHINE, "HKLM", gameDir, currentExe);
    LogAppCompatShimModules();
}

void LogStartupDiagnostics(HMODULE module) {
    const fs::path gameDir = GetModuleDirectory(nullptr);
    const fs::path currentExe = GetModulePathFs(nullptr);
    wchar_t currentDir[MAX_PATH]{};
    GetCurrentDirectoryW(MAX_PATH, currentDir);

    debuglog::WriteInfo(
        "[diag][startup] pid=%lu tick=%llums exe=\"%s\" module=\"%s\" cwd=\"%s\" cmd=\"%s\"",
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long long>(GetTickCount64()),
        GetModulePathUtf8(nullptr).c_str(),
        GetModulePathUtf8(module).c_str(),
        WideToUtf8(currentDir).c_str(),
        GetCommandLineA());

    LogFileFingerprint(gameDir / L"gta_sa.exe", "gta_sa.exe");
    LogPeFileDetails(gameDir / L"gta_sa.exe", "gta_sa.exe");
    LogFileFingerprint(gameDir / L"samp.dll", "samp.dll");
    LogPeFileDetails(gameDir / L"samp.dll", "samp.dll");
    LogFileFingerprint(gameDir / L"samp.exe", "samp.exe");
    LogFileFingerprint(GetModulePathUtf8(module), "HelperByOrc");

    LogKnownProxyFiles(gameDir);
    LogAppCompatLayers(gameDir, currentExe);
    LogDirectoryInventory(gameDir, "root-plugin", false, 200);
    LogDirectoryInventory(gameDir / L"scripts", "scripts", true, 300);
    LogDirectoryInventory(gameDir / L"cleo", "cleo", true, 300);
    LogDirectoryInventory(gameDir / L"moonloader", "moonloader", true, 300);
    LogDirectoryInventory(gameDir / L"modloader", "modloader", true, 500);
    LogDirectoryInventory(gameDir / L"SAMPFUNCS", "SAMPFUNCS", true, 300);
    LogLoadedModuleSnapshot("startup", false, gameDir);
}

std::size_t ToTabIndex(MainTab tab) {
    return static_cast<std::size_t>(tab);
}

const TabDefinition& GetTabDefinition(MainTab tab) {
    return kTabs[ToTabIndex(tab)];
}

const char* GetTabLabel(MainTab tab, bool compact = false) {
    const TabDefinition& definition = GetTabDefinition(tab);
    return UiSettings::Instance().Text(compact ? definition.compactLabel : definition.label);
}

const char* GetTabIcon(MainTab tab) {
    return GetTabDefinition(tab).icon;
}

std::string FormatTabLabelWithIcon(MainTab tab) {
    return std::string(GetTabIcon(tab)) + " " + GetTabLabel(tab);
}

debuglog::Level ToDebugLogLevel(UiLogLevel level) {
    switch (level) {
    case UiLogLevel::Off:
        return debuglog::Level::Off;
    case UiLogLevel::Error:
        return debuglog::Level::Error;
    case UiLogLevel::Info:
    default:
        return debuglog::Level::Info;
    }
}

const char* ToUiLogLevelName(UiLogLevel level) {
    switch (level) {
    case UiLogLevel::Off:
        return "off";
    case UiLogLevel::Error:
        return "error";
    case UiLogLevel::Info:
    default:
        return "info";
    }
}

float Scale(float value) {
    return UiSettings::Instance().Scale(value);
}

ImVec2 ScaleVec(float x, float y) {
    return UiSettings::Instance().Scale(ImVec2(x, y));
}

fs::path DebugLogPath(HMODULE module) {
    if (!module) {
        return {};
    }
    return GetModulePathFs(module).parent_path() / L"HelperByOrc.log";
}

bool OpenFilesystemPath(const fs::path& path) {
    if (path.empty()) {
        return false;
    }

    const auto result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

void CopyPathToClipboard(const fs::path& path) {
    const std::string text = PathToUtf8(path);
    ImGui::SetClipboardText(text.c_str());
}

struct SettingsHeaderTabAnimation {
    float hoverAmount = 0.0f;
    float selectedAmount = 0.0f;
};

float AnimateTo(float value, float target, float speed) {
    const float step = std::clamp(ImGui::GetIO().DeltaTime * speed, 0.0f, 1.0f);
    return value + (target - value) * step;
}

ImVec4 MixColor(const ImVec4& from, const ImVec4& to, float amount) {
    const float t = std::clamp(amount, 0.0f, 1.0f);
    return ImVec4(
        from.x + (to.x - from.x) * t,
        from.y + (to.y - from.y) * t,
        from.z + (to.z - from.z) * t,
        from.w + (to.w - from.w) * t);
}

bool DrawSettingsHeaderTab(UiText labelId, bool selected) {
    static std::unordered_map<ImGuiID, SettingsHeaderTabAnimation> animations;

    UiSettings& ui = UiSettings::Instance();
    const char* label = ui.Text(labelId);
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 tabSize(textSize.x + Scale(24.0f), Scale(34.0f));
    const ImGuiID id = ImGui::GetID("##settings_header_tab");

    SettingsHeaderTabAnimation& animation = animations[id];
    if (animation.selectedAmount == 0.0f && animation.hoverAmount == 0.0f && selected) {
        animation.selectedAmount = 1.0f;
    }

    const ImVec2 p1 = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##settings_header_tab", tabSize);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 p2 = ImGui::GetItemRectMax();

    animation.hoverAmount = AnimateTo(animation.hoverAmount, hovered && !selected ? 1.0f : 0.0f, 14.0f);
    animation.selectedAmount = AnimateTo(animation.selectedAmount, selected ? 1.0f : 0.0f, 12.0f);

    const ImVec4 idleColor = style.Colors[ImGuiCol_TextDisabled];
    const ImVec4 hoverColor = style.Colors[ImGuiCol_Text];
    const ImVec4 accentColor = style.Colors[ImGuiCol_ButtonActive];
    const ImVec4 textColor = MixColor(MixColor(idleColor, hoverColor, animation.hoverAmount), accentColor, animation.selectedAmount);

    const ImVec2 textPos(
        p1.x + (tabSize.x - textSize.x) * 0.5f,
        p1.y + (tabSize.y - textSize.y) * 0.5f - Scale(1.0f));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddText(textPos, ImGui::ColorConvertFloat4ToU32(textColor), label);

    const float lineAmount = std::max(animation.selectedAmount, animation.hoverAmount);
    if (lineAmount > 0.01f) {
        ImVec4 lineColor = accentColor;
        lineColor.w *= std::clamp(animation.selectedAmount + animation.hoverAmount * 0.65f, 0.0f, 1.0f);

        const float centerX = (p1.x + p2.x) * 0.5f;
        const float lineY = p2.y - Scale(4.0f);
        const float halfWidth = (textSize.x * 0.5f) * lineAmount;
        drawList->AddLine(
            ImVec2(centerX - halfWidth, lineY),
            ImVec2(centerX + halfWidth, lineY),
            ImGui::ColorConvertFloat4ToU32(lineColor),
            Scale(2.5f));
    }

    return clicked;
}

void DrawSummaryCell(const char* label, const std::string& value) {
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(0.0f, Scale(4.0f));
    ImGui::TextWrapped("%s", value.empty() ? "-" : value.c_str());
}

void DrawDiagnosticValue(const char* label, const std::string& value) {
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", value.empty() ? "-" : value.c_str());
}

void DrawPathDiagnosticRow(UiText labelId, const fs::path& path, UiText openActionId) {
    UiSettings& ui = UiSettings::Instance();
    const std::string pathText = PathToUtf8(path);

    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", ui.Text(labelId));
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", pathText.empty() ? "-" : pathText.c_str());

    ImGui::PushID(static_cast<int>(labelId));
    if (ImGui::Button(ui.Text(openActionId))) {
        if (!OpenFilesystemPath(path)) {
            debuglog::WriteError("[ui] failed to open path: %s", pathText.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::SettingsCopyPath))) {
        CopyPathToClipboard(path);
    }
    ImGui::PopID();
}

bool LoadBinaryResource(HMODULE module, int resourceId, const void** data, DWORD* size) {
    if (!module || !data || !size) {
        return false;
    }

    *data = nullptr;
    *size = 0;

    const HRSRC resource = FindResourceA(module, MAKEINTRESOURCEA(resourceId), RT_RCDATA);
    if (!resource) {
        return false;
    }

    const HGLOBAL loadedResource = LoadResource(module, resource);
    if (!loadedResource) {
        return false;
    }

    const DWORD resourceSize = SizeofResource(module, resource);
    if (resourceSize == 0) {
        return false;
    }

    const void* resourceData = LockResource(loadedResource);
    if (!resourceData) {
        return false;
    }

    *data = resourceData;
    *size = resourceSize;
    return true;
}

void ClampWindowRect(const ImVec2& displaySize, ImVec2& position, ImVec2& size, float scale) {
    const float margin = kWindowMargin * scale;
    const float maxWidth = std::max(360.0f, displaySize.x - margin * 2.0f);
    const float maxHeight = std::max(280.0f, displaySize.y - margin * 2.0f);
    const float minWidth = std::min(840.0f, maxWidth);
    const float minHeight = std::min(560.0f, maxHeight);

    size.x = std::clamp(size.x, minWidth, maxWidth);
    size.y = std::clamp(size.y, minHeight, maxHeight);
    const float maxPositionX = std::max(margin, displaySize.x - size.x - margin);
    const float maxPositionY = std::max(margin, displaySize.y - size.y - margin);
    position.x = std::clamp(position.x, margin, maxPositionX);
    position.y = std::clamp(position.y, margin, maxPositionY);
}

bool TryReadFiniteRectNumber(const jsonutil::JsonObject& object, const char* key, float& out) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return false;
    }

    const double* number = it->second.TryNumber();
    if (number == nullptr || !std::isfinite(*number)) {
        return false;
    }

    out = static_cast<float>(*number);
    return std::isfinite(out);
}

bool IsUsableMainWindowRect(const ImVec2& position, const ImVec2& size) {
    return std::isfinite(position.x)
        && std::isfinite(position.y)
        && std::isfinite(size.x)
        && std::isfinite(size.y)
        && size.x >= 360.0f
        && size.y >= 280.0f;
}

bool TryReadMainWindowRect(const jsonutil::JsonObject& section, ImVec2& position, ImVec2& size) {
    const jsonutil::JsonObject* object = jsonutil::JsonObjectOrNull(&section, kShellMainWindowName);
    if (object == nullptr) {
        return false;
    }

    ImVec2 loadedPosition{};
    ImVec2 loadedSize{};
    if (!TryReadFiniteRectNumber(*object, "x", loadedPosition.x)
        || !TryReadFiniteRectNumber(*object, "y", loadedPosition.y)
        || !TryReadFiniteRectNumber(*object, "w", loadedSize.x)
        || !TryReadFiniteRectNumber(*object, "h", loadedSize.y)
        || !IsUsableMainWindowRect(loadedPosition, loadedSize)) {
        return false;
    }

    position = loadedPosition;
    size = loadedSize;
    return true;
}

void WriteMainWindowRect(jsonutil::JsonObject& section, const ImVec2& position, const ImVec2& size) {
    jsonutil::JsonObject object;
    object["x"] = static_cast<double>(position.x);
    object["y"] = static_cast<double>(position.y);
    object["w"] = static_cast<double>(size.x);
    object["h"] = static_cast<double>(size.y);
    section[kShellMainWindowName] = jsonutil::JsonValue(std::move(object));
}

bool SameWindowRect(const ImVec2& leftPosition, const ImVec2& leftSize, const ImVec2& rightPosition, const ImVec2& rightSize) {
    constexpr float kEpsilon = 0.5f;
    return std::abs(leftPosition.x - rightPosition.x) < kEpsilon
        && std::abs(leftPosition.y - rightPosition.y) < kEpsilon
        && std::abs(leftSize.x - rightSize.x) < kEpsilon
        && std::abs(leftSize.y - rightSize.y) < kEpsilon;
}

void DrawLogoZoom(
    IDirect3DTexture9* texture,
    std::uint32_t textureWidth,
    std::uint32_t textureHeight,
    MainTab tab,
    const ImVec2& size,
    float zoom) {
    if (!texture || textureWidth == 0 || textureHeight == 0) {
        return;
    }

    const float cellWidth = static_cast<float>(textureWidth) / 3.0f;
    const float cellHeight = static_cast<float>(textureHeight) / 2.0f;
    const float safeZoom = std::max(0.1f, zoom);
    const int tabIndex = static_cast<int>(ToTabIndex(tab));
    const int column = tabIndex % 3;
    const int row = tabIndex / 3;
    const float centerX = (static_cast<float>(column) + 0.5f) * cellWidth;
    const float centerY = (static_cast<float>(row) + 0.5f) * cellHeight;
    const float zoomWidth = cellWidth / safeZoom;
    const float zoomHeight = cellHeight / safeZoom;
    const float x0 = centerX - zoomWidth * 0.5f;
    const float y0 = centerY - zoomHeight * 0.5f;
    const float x1 = centerX + zoomWidth * 0.5f;
    const float y1 = centerY + zoomHeight * 0.5f;

    const ImVec2 uv0(x0 / static_cast<float>(textureWidth), y0 / static_cast<float>(textureHeight));
    const ImVec2 uv1(x1 / static_cast<float>(textureWidth), y1 / static_cast<float>(textureHeight));
    ImGui::Image(reinterpret_cast<ImTextureID>(texture), size, uv0, uv1);
}

} // namespace

ModApp::ModApp() = default;

ModApp& ModApp::Instance() {
    static ModApp instance;
    return instance;
}

void ModApp::OnProcessAttach(HMODULE module) {
    module_ = module;
    debuglog::Initialize(module);
    debuglog::WriteInfo("ModApp attached");
    LogStartupDiagnostics(module);
    minHookInitialized_ = minhook::Initialize();
    if (!minHookInitialized_) {
        debuglog::WriteError("MinHook initialization failed");
    } else {
        debuglog::WriteInfo("MinHook initialized");
    }
    AppConfig::Instance().OnProcessAttach(module);
    debuglog::WriteInfo("AppConfig attached");
    UiSettings::Instance().Load();
    debuglog::SetLevel(ToDebugLogLevel(UiSettings::Instance().LogLevel()));
    debuglog::WriteInfo("UI settings loaded (log_level=%s)", ToUiLogLevelName(UiSettings::Instance().LogLevel()));
    notifications_.LoadConfig();
    unwanted_.OnProcessAttach();
    LoadShellState();

    sampRakHooks_.SetSampApi(&sampApi_);
    arizonaCefDialogs_.SetSampApi(&sampApi_);
    arizonaCefDialogs_.SetSampRakHooks(&sampRakHooks_);
    arizonaCefDialogs_.OnProcessAttach();
    tags_.SetSampApi(&sampApi_);
    tags_.SetArizonaCefDialogs(&arizonaCefDialogs_);
    tags_.OnProcessAttach();
    sampApi_.attachModules([this](std::string_view text) { return tags_.ExpandText(text); });
    sampHooks_.SetSampApi(&sampApi_);
    sampHooks_.SetApplyDamageProtectionEnabled(UiSettings::Instance().ApplyDamageProtectionEnabled());
    sampHooks_.SetHotkeyBlockCallback([this]() {
        return overlay_.IsTextInputActive();
    });
    sampHooks_.SetMouseButtonBlockCallback([this]() {
        return CurrentHelperMouseSuppressionMask();
    });
    sampHooks_.AddChatMessageFilter([this](
                                        SampHooks::ChatMessageSource source,
                                        int type,
                                        const std::string& text,
                                        const std::string& prefix,
                                        std::uint32_t textColor,
                                        std::uint32_t prefixColor) {
        UnwantedMessageContext context;
        context.source = source == SampHooks::ChatMessageSource::AddMessage
            ? UnwantedMessageSource::CChatAddMessage
            : source == SampHooks::ChatMessageSource::AddChatMessage
            ? UnwantedMessageSource::CChatAddChatMessage
            : UnwantedMessageSource::CChatAddEntry;
        context.chatType = type;
        context.text = text;
        context.prefix = prefix;
        context.textColor = textColor;
        context.prefixColor = prefixColor;
        return !unwanted_.ShouldBlock(context);
    });
    sampHooks_.AddOnSendCommandHandler([this](std::string& text) {
        text = tags_.ExpandOutgoingText(text, "command", text);
        return true;
    });
    sampHooks_.AddOnSendChatHandler([this](std::string& text) {
        text = tags_.ExpandOutgoingText(text, "chat", text);
        return true;
    });
    sampRakHooks_.AddOnSendCommandHandler([this](std::string& text) {
        if (!SampHooks::IsOutgoingInputTransformActive()) {
            text = tags_.ExpandOutgoingText(text, "command", text);
        }
        return true;
    });
    sampRakHooks_.AddOnSendChatHandler([this](std::string& text) {
        if (!SampHooks::IsOutgoingInputTransformActive()) {
            text = tags_.ExpandOutgoingText(text, "chat", text);
        }
        return true;
    });
    sampRakHooks_.AddServerMessageFilter([this](std::int32_t color, const std::string& text) {
        UnwantedMessageContext context;
        context.source = UnwantedMessageSource::RakClientMessage;
        context.text = text;
        context.textColor = static_cast<std::uint32_t>(color);
        return !unwanted_.ShouldBlock(context);
    });
    sampRakHooks_.AddPlayerChatFilter([this](std::uint16_t playerId, const std::string& playerName, const std::string& text) {
        UnwantedMessageContext context;
        context.source = UnwantedMessageSource::RakChat;
        context.playerId = playerId;
        context.playerName = playerName;
        context.text = text;
        context.prefix = playerName;
        return !unwanted_.ShouldBlock(context);
    });
    sampRakHooks_.AddChatBubbleFilter([this](
                                          std::uint16_t playerId,
                                          const std::string& playerName,
                                          std::uint32_t color,
                                          float drawDistance,
                                          std::uint32_t durationMs,
                                          const std::string& text) {
        UNREFERENCED_PARAMETER(drawDistance);
        UNREFERENCED_PARAMETER(durationMs);
        UnwantedMessageContext context;
        context.source = UnwantedMessageSource::RakChatBubble;
        context.playerId = playerId;
        context.playerName = playerName;
        context.text = text;
        context.prefix = playerName;
        context.textColor = color;
        return !unwanted_.ShouldBlock(context);
    });
    incomingMessageRouter_.SetSampHooks(&sampHooks_);
    incomingMessageRouter_.SetSampRakHooks(&sampRakHooks_);
    binder_.OnProcessAttach(module);
    binder_.SetSampApi(&sampApi_);
    binder_.SetSampHooks(&sampHooks_);
    binder_.SetSampRakHooks(&sampRakHooks_);
    binder_.SetIncomingMessageRouter(&incomingMessageRouter_);
    binder_.SetNotificationManager(&notifications_);
    binder_.SetTagsModule(&tags_);
    notepad_.OnProcessAttach(module);
    notepad_.SetTagsModule(&tags_);
    hud_.OnProcessAttach(module);
    hud_.SetTagsModule(&tags_);
    hud_.SetNotepadModule(&notepad_);
    hud_.SetSampApi(&sampApi_);
    overlayCursor_.SetSampApi(&sampApi_);
    tags_.SetBinderModule(&binder_);
    tags_.SetNotificationManager(&notifications_);

    overlay_.SetPrepareFrameCallback([this](IDirect3DDevice9* device) { PrepareUiForImGuiNewFrame(device); });
    overlay_.SetRenderCallback([this](IDirect3DDevice9* device) { RenderUi(device); });
    overlay_.SetUpdateCallback([this]() { Tick(); });
    overlay_.SetFrameSurfaceCallback([this]() { return CurrentOverlayFrameSurface(); });
    overlay_.SetWindowMessageCallback([this](UINT message, WPARAM wparam, LPARAM lparam) {
        const bool quickMenuOpen = binder_.IsQuickMenuOpen();
        const bool binderWantsRouting = binder_.WantsInputRouting();
        if (overlay_.ShouldSwallowMouseMessage(message, lparam)) {
            MarkHelperMouseButtonsForSuppression(message, wparam);
        }
        hud_.SetPlacementInputBlocked(quickMenuOpen);
        if (quickMenuOpen || binderWantsRouting) {
            return binder_.OnWindowMessage(message, wparam, lparam) || hud_.OnWindowMessage(message, wparam, lparam);
        }
        return hud_.OnWindowMessage(message, wparam, lparam) || binder_.OnWindowMessage(message, wparam, lparam);
    });
    overlay_.SetAuxiliaryUiVisibleCallback([this]() {
        return hud_.WantsOverlayRender() || binder_.WantsOverlayRender() || notifications_.WantsOverlayRender();
    });
    overlay_.SetAuxiliaryInputCaptureCallback([this]() {
        const bool quickMenuOpen = binder_.IsQuickMenuOpen();
        hud_.SetPlacementInputBlocked(quickMenuOpen);
        return quickMenuOpen || binder_.WantsInputCapture() || hud_.WantsInputCapture();
    });
    overlay_.SetAuxiliaryInputRoutingCallback([this]() {
        return binder_.WantsInputRouting() || hud_.WantsInputCapture();
    });
    overlay_.SetInputPipelineGateCallback([this]() { return sampUiPipelineReady_; });
    overlay_.SetInputCaptureChangedCallback([this](bool captured) { HandleOverlayInputCaptureChanged(captured); });
    overlay_.SetMenuToggleHotkeyConflictCallback([this](const std::vector<unsigned int>& keys, std::string& description) {
        return binder_.DescribeMainWindowHotkeyConflict(keys, description);
    });
    debuglog::WriteInfo("Overlay callbacks configured");
    StartDeferredOverlayThread();
    debuglog::WriteInfo("[ui][d3d] overlay attach deferred until SA:MP full-ready gate");
}

void ModApp::Shutdown() {
    debuglog::WriteInfo("[probe] shutdown begin ts=%llums", static_cast<unsigned long long>(GetTickCount64()));
    debuglog::WriteInfo("ModApp shutdown begin");
    StopDeferredOverlayThread();
    ::ClipCursor(nullptr);

    SaveShellStateIfDirty();
    sampApi_.Refresh();
    overlayCursor_.Shutdown();

    overlay_.Shutdown();
    debuglog::WriteInfo("Overlay shutdown done");
    incomingMessageRouter_.Shutdown();
    debuglog::WriteInfo("Incoming router shutdown done");
    binder_.Shutdown();
    debuglog::WriteInfo("Binder shutdown done");
    notifications_.Shutdown();
    debuglog::WriteInfo("Notifications shutdown done");
    notepad_.Shutdown();
    debuglog::WriteInfo("Notepad shutdown done");
    hud_.Shutdown();
    debuglog::WriteInfo("HUD shutdown done");
    unwanted_.Shutdown();
    debuglog::WriteInfo("Unwanted messages shutdown done");
    tags_.Shutdown();
    debuglog::WriteInfo("Tags shutdown done");
    arizonaCefDialogs_.Shutdown();
    debuglog::WriteInfo("Arizona CEF dialogs shutdown done");
    AppConfig::Instance().Shutdown();
    debuglog::WriteInfo("AppConfig shutdown done");
    sampRakHooks_.Shutdown();
    sampHooks_.Shutdown();
    debuglog::WriteInfo("SAMP hooks shutdown done");
    sampApi_.onTerminate();
    debuglog::WriteInfo("SampApi terminated");
    ReleaseUiResources();
    if (minHookInitialized_) {
        minhook::Uninitialize();
        minHookInitialized_ = false;
        debuglog::WriteInfo("MinHook uninitialized");
    }
    debuglog::WriteInfo("ModApp shutdown");
    debuglog::WriteInfo("[probe] shutdown end ts=%llums", static_cast<unsigned long long>(GetTickCount64()));
    debuglog::Shutdown();
}

void ModApp::HandleOverlayInputCaptureChanged(bool captured) {
    (void)captured;
    UpdateOverlayCursorMode();
}

void ModApp::MarkHelperMouseButtonsForSuppression(UINT message, WPARAM wparam) {
    const std::uint8_t mask = MouseButtonMaskFromWindowMessage(message, wparam);
    if (mask == 0) {
        return;
    }

    const std::uint32_t previous = helperMouseSuppressionMask_.fetch_or(mask, std::memory_order_relaxed);
    helperMouseSuppressionReleaseFrames_ = kHelperMouseSuppressionReleaseFrames;
    if (kHelperMouseSuppressionTraceEnabled && (previous & mask) != mask) {
        debuglog::WriteInfo(
            "[ui] helper game mouse suppression armed mask=0x%02X msg=%u",
            static_cast<unsigned>(previous | mask),
            static_cast<unsigned>(message));
    }
}

void ModApp::UpdateHelperMouseSuppression() {
    std::uint32_t mask = helperMouseSuppressionMask_.load(std::memory_order_relaxed);
    const std::uint8_t heldMask = HeldMouseButtonMask();

    if (mask == 0) {
        helperMouseSuppressionReleaseFrames_ = 0;
        return;
    }

    const std::uint32_t heldTrackedMask = mask & heldMask;
    if (heldTrackedMask != 0) {
        helperMouseSuppressionMask_.store(mask, std::memory_order_relaxed);
        helperMouseSuppressionReleaseFrames_ = kHelperMouseSuppressionReleaseFrames;
        return;
    }

    if (helperMouseSuppressionReleaseFrames_ > 0) {
        --helperMouseSuppressionReleaseFrames_;
        helperMouseSuppressionMask_.store(mask, std::memory_order_relaxed);
        return;
    }

    helperMouseSuppressionMask_.store(0, std::memory_order_relaxed);
    if (kHelperMouseSuppressionTraceEnabled) {
        debuglog::WriteInfo("[ui] helper game mouse suppression cleared mask=0x%02X", static_cast<unsigned>(mask));
    }
}

std::uint8_t ModApp::CurrentHelperMouseSuppressionMask() const {
    const std::uint32_t mask = helperMouseSuppressionMask_.load(std::memory_order_relaxed);
    return static_cast<std::uint8_t>(mask & kHelperSuppressibleMouseButtons);
}

void ModApp::UpdateOverlayCursorMode() {
    HWND gameHw = overlay_.GetGameWindow();
    HWND fg = GetForegroundWindow();
    const bool appHasFocus = gameHw && fg && IsWindow(gameHw)
        && (fg == gameHw || IsChild(gameHw, fg) != FALSE);

    sampApi_.Refresh();
    const ExternalCursorSnapshot cursorSnapshot = externalCursorDetector_.Detect(sampApi_, gameHw, fg);
    const bool quickMenuOpen = binder_.IsQuickMenuOpen();
    const bool binderInputCapture = binder_.WantsInputCapture();
    const bool binderKeyRouting = binder_.WantsInputRouting() && !quickMenuOpen && !binderInputCapture;
    const bool hudInputCapture = hud_.WantsInputCapture();

    OverlayCursorController::Inputs inputs{};
    inputs.sampUiPipelineReady = sampUiPipelineReady_;
    constexpr int kSampCursorModeNone = 0;
    constexpr int kSampCursorModeLockCamAndControl = 2;
    constexpr int kSampCursorModeLockCamNoCursor = 4;
    const auto addSurface = [&inputs](
                                OverlayCursorController::SurfaceId id,
                                bool visible,
                                bool wantsMouse,
                                bool wantsKeyboard,
                                bool locksGameControl,
                                int sampCursorMode,
                                int priority) {
        inputs.surfaces.push_back({ id, visible, wantsMouse, wantsKeyboard, locksGameControl, sampCursorMode, priority });
    };
    addSurface(
        OverlayCursorController::SurfaceId::MainMenu,
        overlay_.IsMenuOpen(),
        true,
        true,
        true,
        kSampCursorModeLockCamAndControl,
        100);
    addSurface(
        OverlayCursorController::SurfaceId::Modal,
        binderInputCapture,
        true,
        true,
        true,
        kSampCursorModeLockCamAndControl,
        90);
    addSurface(
        OverlayCursorController::SurfaceId::HudPlacement,
        hudInputCapture,
        true,
        true,
        true,
        kSampCursorModeLockCamAndControl,
        80);
    addSurface(
        OverlayCursorController::SurfaceId::QuickMenu,
        quickMenuOpen,
        true,
        false,
        false,
        kSampCursorModeLockCamNoCursor,
        70);
    addSurface(
        OverlayCursorController::SurfaceId::Modal,
        binderKeyRouting,
        false,
        true,
        false,
        kSampCursorModeNone,
        60);
    addSurface(
        OverlayCursorController::SurfaceId::Notifications,
        notifications_.WantsOverlayRender(),
        false,
        false,
        false,
        kSampCursorModeNone,
        10);
    inputs.sampCursorMode = cursorSnapshot.sampCursorMode;
    inputs.chatOpen = cursorSnapshot.chatOpen;
    inputs.dialogOpen = cursorSnapshot.dialogOpen;
    inputs.externalCursorActive = cursorSnapshot.externalCursorActive;
    inputs.externalOwnerName = cursorSnapshot.externalOwnerName;
    inputs.cursorVisible = cursorSnapshot.cursorVisible;
    inputs.captureWindow = cursorSnapshot.captureWindow;
    inputs.captureOwnerModule = cursorSnapshot.captureOwnerModule;
    inputs.cefKnown = cursorSnapshot.cefKnown;
    inputs.cefControlled = cursorSnapshot.cefControlled;
    inputs.cefShown = cursorSnapshot.cefShown;
    inputs.riskModules = cursorSnapshot.riskModules;
    inputs.appHasFocus = appHasFocus;
    inputs.gameWindow = gameHw;
    inputs.foregroundWindow = fg;
    const OverlayCursorController::Result result = overlayCursor_.Apply(inputs);
    overlay_.SetInputDecision(result.routingAllowed, result.drawHelperCursor, result.swallowMouse);
}

DWORD WINAPI ModApp::DeferredOverlayThreadProc(LPVOID param) {
    auto* self = static_cast<ModApp*>(param);
    if (!self) {
        return 0;
    }

    debuglog::WriteInfo("[ui][d3d] deferred overlay thread started");
    while (!self->deferredOverlayThreadStop_.load()) {
        if (self->RefreshSampGate()) {
            self->RequestOverlayAttachOnce("SA:MP full-ready gate");
            break;
        }
        Sleep(50);
    }
    debuglog::WriteInfo("[ui][d3d] deferred overlay thread finished");
    return 0;
}

void ModApp::StartDeferredOverlayThread() {
    if (overlayAttachRequested_.load() || deferredOverlayThread_) {
        return;
    }

    deferredOverlayThreadStop_.store(false);
    deferredOverlayThread_ = CreateThread(nullptr, 0, &DeferredOverlayThreadProc, this, 0, nullptr);
    if (!deferredOverlayThread_) {
        debuglog::WriteError("[ui][d3d] deferred overlay thread creation failed: %lu", GetLastError());
    }
}

void ModApp::StopDeferredOverlayThread() {
    deferredOverlayThreadStop_.store(true);
    if (!deferredOverlayThread_) {
        return;
    }

    if (GetCurrentThreadId() != GetThreadId(deferredOverlayThread_)) {
        WaitForSingleObject(deferredOverlayThread_, 5000);
    }
    CloseHandle(deferredOverlayThread_);
    deferredOverlayThread_ = nullptr;
}

void ModApp::RequestOverlayAttachOnce(const char* reason) {
    if (overlayAttachRequested_.exchange(true)) {
        return;
    }

    debuglog::WriteInfo(
        "[ui][d3d] overlay attach requested after %s; installing D3D hooks now",
        reason ? reason : "gate");
    overlay_.OnProcessAttach();
}

bool ModApp::RefreshSampGate() {
    const std::uint64_t now = GetTickCount64();
    if (now < nextSampRefreshAtMs_) {
        return sampUiPipelineReady_;
    }

    sampApi_.Refresh();
    const bool readyBeforeHooks = sampApi_.isSAMPInitilizeLua();
    sampHooks_.Refresh();
    sampRakHooks_.Refresh();
    const bool readyAfterHooks = sampApi_.isSAMPInitilizeLua();
    const bool gateChanged = readyBeforeHooks != readyAfterHooks || sampUiPipelineReady_ != readyAfterHooks;
    if (!readyAfterHooks || gateChanged) {
        sampApi_.LogReadinessDiagnostics("tick");
    }
    if (!readyAfterHooks && now >= nextRuntimeModuleSnapshotAtMs_) {
        LogLoadedModuleSnapshot("runtime-new", true, GetModuleDirectory(nullptr));
        nextRuntimeModuleSnapshotAtMs_ = now + 2000;
    }
    if (!readyAfterHooks && sampApi_.sampModule() && sampApi_.isSupportedVersion()) {
        if (sampNotReadySinceMs_ == 0) {
            sampNotReadySinceMs_ = now;
            nextSampStuckTraceAtMs_ = now + 8000;
        } else if (now >= nextSampStuckTraceAtMs_) {
            debuglog::WriteError(
                "[probe][stuck] SA:MP stayed before full-ready gate for %llums; "
                "check [samp][diag] transfer owners, [diag][appcompat], apphelp/skygfx/d3d9-proxy/MoonLoader modules. lastError=\"%s\"",
                static_cast<unsigned long long>(now - sampNotReadySinceMs_),
                sampApi_.lastError().c_str());
            sampApi_.LogReadinessDiagnostics("stuck");
            LogLoadedModuleSnapshot("runtime-stuck", true, GetModuleDirectory(nullptr));
            nextSampStuckTraceAtMs_ = now + 8000;
        }
    } else {
        sampNotReadySinceMs_ = 0;
        nextSampStuckTraceAtMs_ = 0;
    }
    if (sampUiPipelineReady_ != readyAfterHooks) {
        debuglog::WriteInfo(
            "[probe] SA:MP input gate changed %d -> %d ts=%llums",
            sampUiPipelineReady_ ? 1 : 0,
            readyAfterHooks ? 1 : 0,
            static_cast<unsigned long long>(GetTickCount64()));
    }
    sampUiPipelineReady_ = readyAfterHooks;
    if (gateChanged) {
        debuglog::WriteInfo(
            "[probe] Refresh state ts=%llums sampReady(beforeHooks=%d afterHooks=%d) module=%d supported=%d",
            static_cast<unsigned long long>(GetTickCount64()),
            readyBeforeHooks ? 1 : 0,
            readyAfterHooks ? 1 : 0,
            sampApi_.sampModule() ? 1 : 0,
            sampApi_.isSupportedVersion() ? 1 : 0);
    }
    nextSampRefreshAtMs_ = now + 1000;
    return readyAfterHooks;
}

void ModApp::Tick() {
    AppConfig::Instance().ProcessPendingWrites();
    incomingMessageRouter_.Tick();
    binder_.SetGameInputForeground(overlay_.IsGameWindowForeground());
    binder_.SetHelperUiActive(overlay_.IsMenuOpen());
    binder_.Tick();
    UpdateHelperMouseSuppression();
    hud_.SetPlacementInputBlocked(binder_.IsQuickMenuOpen());
    tags_.Tick();

    RefreshSampGate();
    arizonaCefDialogs_.Tick();

    UpdateOverlayCursorMode();
}

ImGuiOverlay::FrameSurface ModApp::CurrentOverlayFrameSurface() {
    const bool menuOpen = overlay_.IsMenuOpen();
    const bool quickMenuOpen = binder_.IsQuickMenuOpen();
    const bool hudVisible = hud_.WantsOverlayRender();
    const bool notificationVisible = notifications_.WantsOverlayRender();
    const bool binderOverlayVisible = binder_.WantsOverlayRender();
    const bool otherAuxVisible = notificationVisible || (binderOverlayVisible && !quickMenuOpen);
    const int visibleSurfaces = (menuOpen ? 1 : 0)
        + (quickMenuOpen ? 1 : 0)
        + (hudVisible ? 1 : 0)
        + (otherAuxVisible ? 1 : 0);

    if (visibleSurfaces <= 0) {
        return ImGuiOverlay::FrameSurface::Idle;
    }
    if (visibleSurfaces > 1) {
        return ImGuiOverlay::FrameSurface::Mixed;
    }
    if (menuOpen) {
        return ImGuiOverlay::FrameSurface::MainMenu;
    }
    if (quickMenuOpen) {
        return ImGuiOverlay::FrameSurface::QuickMenu;
    }
    if (hudVisible) {
        return ImGuiOverlay::FrameSurface::HudOnly;
    }
    return ImGuiOverlay::FrameSurface::Auxiliary;
}

void ModApp::ApplyMainStyle(float scale) const {
    static ImGuiContext* s_appliedContext = nullptr;
    static float s_appliedScale = -1.0f;
    ImGuiContext* currentContext = ImGui::GetCurrentContext();
    if (s_appliedContext == currentContext && std::abs(s_appliedScale - scale) <= 0.0005f) {
        return;
    }
    s_appliedContext = currentContext;
    s_appliedScale = scale;

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    ImVec4* colors = style.Colors;

    style.WindowPadding = ScaleVec(5.0f, 5.0f);
    style.FramePadding = ScaleVec(5.0f, 3.0f);
    style.ItemSpacing = ScaleVec(5.0f, 4.0f);
    style.ItemInnerSpacing = ScaleVec(3.0f, 3.0f);
    style.IndentSpacing = 8.0f * scale;
    style.ScrollbarSize = 10.0f * scale;
    style.GrabMinSize = 12.0f * scale;
    style.WindowBorderSize = 2.0f * scale;
    style.ChildBorderSize = 1.0f * scale;
    style.PopupBorderSize = 2.0f * scale;
    style.FrameBorderSize = 2.0f * scale;
    style.TabBorderSize = 1.5f * scale;
    style.WindowRounding = 6.0f * scale;
    style.ChildRounding = 6.0f * scale;
    style.FrameRounding = 4.0f * scale;
    style.PopupRounding = 5.0f * scale;
    style.ScrollbarRounding = 4.0f * scale;
    style.GrabRounding = 4.0f * scale;
    style.TabRounding = 4.0f * scale;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.5f, 0.5f);
    style.SeparatorTextBorderSize = 3.0f * scale;
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextPadding = ImVec2(8.0f * scale, style.FramePadding.y);

    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.92f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.39f, 0.46f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.12f, 0.15f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.14f, 0.18f, 0.98f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.14f, 0.16f, 0.20f, 0.97f);
    colors[ImGuiCol_Border] = ImVec4(0.34f, 0.39f, 0.48f, 0.82f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.30f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.19f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.23f, 0.28f, 0.38f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.27f, 0.36f, 0.51f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.34f, 0.39f, 0.48f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.47f, 0.60f, 0.80f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.62f, 0.72f, 0.92f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.47f, 0.60f, 0.80f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.62f, 0.72f, 0.92f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.13f, 0.16f, 0.21f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.19f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.13f, 0.16f, 0.21f, 0.75f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.13f, 0.15f, 0.19f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.17f, 0.19f, 0.23f, 0.90f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.26f, 0.29f, 0.38f, 0.75f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.34f, 0.39f, 0.48f, 0.90f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.47f, 0.60f, 0.80f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.22f, 0.26f, 0.34f, 0.78f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.34f, 0.41f, 0.58f, 0.90f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.47f, 0.60f, 0.80f, 0.96f);
    colors[ImGuiCol_Separator] = ImVec4(0.27f, 0.33f, 0.44f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.47f, 0.60f, 0.80f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.62f, 0.72f, 0.92f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.47f, 0.60f, 0.80f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.47f, 0.60f, 0.80f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.62f, 0.72f, 0.92f, 0.95f);
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.17f, 0.21f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.34f, 0.39f, 0.48f, 0.90f);
    colors[ImGuiCol_TabActive] = ImVec4(0.47f, 0.60f, 0.80f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.20f, 0.29f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.14f, 0.17f, 0.22f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.24f, 0.29f, 0.38f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.17f, 0.20f, 0.25f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.035f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.47f, 0.60f, 0.80f, 0.35f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.47f, 0.60f, 0.80f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.47f, 0.60f, 0.80f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.15f, 0.18f, 0.22f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.15f, 0.18f, 0.22f, 0.70f);
}

void ModApp::LoadShellState() {
    const jsonutil::JsonObject section = AppConfig::Instance().ReadSectionObject(kShellSectionName);
    sidebarCollapsed_ = jsonutil::JsonBoolOr(&section, "sidebar_collapsed", false);
    mainWindowRectLoaded_ = TryReadMainWindowRect(section, mainWindowPos_, mainWindowSize_);
    mainWindowRectKnown_ = mainWindowRectLoaded_;
    mainWindowRectDirty_ = false;
    mainWindowInitialized_ = false;
    debuglog::WriteInfo(
        "Shell state loaded (sidebar_collapsed=%d main_window=%d pos=%.1f,%.1f size=%.1f,%.1f)",
        sidebarCollapsed_ ? 1 : 0,
        mainWindowRectLoaded_ ? 1 : 0,
        mainWindowPos_.x,
        mainWindowPos_.y,
        mainWindowSize_.x,
        mainWindowSize_.y);
}

void ModApp::QueueShellStateSave() {
    const bool sidebarCollapsed = sidebarCollapsed_;
    const bool includeMainWindow = mainWindowRectKnown_ && IsUsableMainWindowRect(mainWindowPos_, mainWindowSize_);
    const ImVec2 mainWindowPos = mainWindowPos_;
    const ImVec2 mainWindowSize = mainWindowSize_;
    debuglog::WriteInfo(
        "Queue shell state save (sidebar_collapsed=%d main_window=%d pos=%.1f,%.1f size=%.1f,%.1f)",
        sidebarCollapsed ? 1 : 0,
        includeMainWindow ? 1 : 0,
        mainWindowPos.x,
        mainWindowPos.y,
        mainWindowSize.x,
        mainWindowSize.y);
    AppConfig::Instance().QueueMutation([sidebarCollapsed, includeMainWindow, mainWindowPos, mainWindowSize](jsonutil::JsonObject& root) {
        jsonutil::JsonObject section;
        const auto existing = root.find(std::string(kShellSectionName));
        if (existing != root.end()) {
            if (const jsonutil::JsonObject* object = existing->second.TryObject()) {
                section = *object;
            }
        }

        section["sidebar_collapsed"] = sidebarCollapsed;
        if (includeMainWindow) {
            WriteMainWindowRect(section, mainWindowPos, mainWindowSize);
        }
        root[std::string(kShellSectionName)] = jsonutil::JsonValue(std::move(section));
    });
    if (mainWindowRectDirty_) {
        mainWindowRectDirty_ = false;
    }
}

void ModApp::SaveShellStateIfDirty() {
    if (!mainWindowRectDirty_) {
        return;
    }

    QueueShellStateSave();
}

void ModApp::ReloadConfigAfterProfileChange() {
    overlay_.CancelMenuToggleHotkeyCapture();
    UiSettings::Instance().Load();
    notifications_.LoadConfig();
    debuglog::SetLevel(ToDebugLogLevel(UiSettings::Instance().LogLevel()));
    LoadShellState();
    tags_.ReloadConfig();
    unwanted_.ReloadConfig();
    binder_.ReloadConfig();
    notepad_.ReloadConfig();
    hud_.ReloadConfig();
    sampHooks_.SetApplyDamageProtectionEnabled(UiSettings::Instance().ApplyDamageProtectionEnabled());
    debuglog::WriteInfo(
        "[profiles] active profile applied id=%s path=%ls",
        AppConfig::Instance().ActiveProfileId().c_str(),
        AppConfig::Instance().ConfigPath().c_str());
}

void ModApp::SetSidebarCollapsed(bool collapsed) {
    if (sidebarCollapsed_ == collapsed) {
        return;
    }

    debuglog::WriteInfo("Sidebar collapsed changed %d -> %d", sidebarCollapsed_ ? 1 : 0, collapsed ? 1 : 0);
    sidebarCollapsed_ = collapsed;
    QueueShellStateSave();
}

void ModApp::EnsureLogoTexture(IDirect3DDevice9* device) {
    if (logoTexture_ || logoLoadAttempted_ || !device || !module_) {
        return;
    }

    logoLoadAttempted_ = true;

    const void* resourceData = nullptr;
    DWORD resourceSize = 0;
    if (!LoadBinaryResource(module_, IDR_MAIN_LOGO, &resourceData, &resourceSize)) {
        debuglog::WriteError("Failed to locate embedded logo resource");
        return;
    }

    D3DXIMAGE_INFO imageInfo{};
    const HRESULT infoResult = D3DXGetImageInfoFromFileInMemory(resourceData, resourceSize, &imageInfo);
    if (FAILED(infoResult)) {
        debuglog::WriteError("D3DXGetImageInfoFromFileInMemory failed: 0x%08lX", static_cast<unsigned long>(infoResult));
        return;
    }

    IDirect3DTexture9* texture = nullptr;
    const HRESULT textureResult = D3DXCreateTextureFromFileInMemory(device, resourceData, resourceSize, &texture);
    if (FAILED(textureResult) || !texture) {
        debuglog::WriteError("D3DXCreateTextureFromFileInMemory failed: 0x%08lX", static_cast<unsigned long>(textureResult));
        return;
    }

    logoTexture_ = texture;
    logoWidth_ = imageInfo.Width;
    logoHeight_ = imageInfo.Height;
    debuglog::WriteInfo("Logo texture loaded (%ux%u)", logoWidth_, logoHeight_);
}

void ModApp::ReleaseUiResources() {
    notepad_.ReleaseDeviceResources();
    hud_.ReleaseDeviceResources();
    if (logoTexture_) {
        logoTexture_->Release();
        logoTexture_ = nullptr;
    }

    logoWidth_ = 0;
    logoHeight_ = 0;
    logoLoadAttempted_ = false;
}

MainTab ModApp::DrawAnimatedMenu(float width) {
    const float buttonPadding = Scale(8.0f);
    const float buttonHeight = Scale(38.0f);
    const float cornerRadius = Scale(7.0f);
    constexpr float alphaSpeed = 12.0f;
    constexpr float shiftSpeed = 8.0f;

    const float maxShift = sidebarCollapsed_ ? 0.0f : Scale(18.0f);
    const ImVec4 hoverColor(0.35f, 0.52f, 0.74f, 0.33f);
    const ImVec4 selectedColor(0.17f, 0.32f, 0.46f, 0.74f);
    const ImVec4 textColor(0.88f, 0.88f, 0.88f, 0.98f);
    const ImVec4 activeTextColor(1.0f, 1.0f, 1.0f, 1.0f);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, buttonPadding));

    for (std::size_t i = 0; i < kTabs.size(); ++i) {
        const TabDefinition& tab = kTabs[i];
        MenuAnimationState& animation = menuAnimations_[i];
        const bool isSelected = currentTab_ == tab.tab;

        ImGui::PushID(static_cast<int>(i));
        const ImVec2 itemSize(width, buttonHeight);
        const ImVec2 itemPosition = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("##tab", itemSize);
        const bool hovered = ImGui::IsItemHovered();

        const float targetAlpha = (hovered || isSelected) ? 1.0f : 0.0f;
        const float targetShift = (hovered || isSelected) ? maxShift : 0.0f;
        animation.alpha += (targetAlpha - animation.alpha) * std::min(io.DeltaTime * alphaSpeed, 1.0f);
        animation.shift += (targetShift - animation.shift) * std::min(io.DeltaTime * shiftSpeed, 1.0f);

        if (animation.alpha > 0.01f) {
            ImVec4 fill = isSelected ? selectedColor : hoverColor;
            fill.w *= animation.alpha;
            draw->AddRectFilled(
                itemPosition,
                ImVec2(itemPosition.x + itemSize.x, itemPosition.y + itemSize.y),
                ImGui::GetColorU32(fill),
                cornerRadius);
        }

        const char* text = GetTabIcon(tab.tab);
        std::string expandedLabel;
        if (!sidebarCollapsed_) {
            expandedLabel = FormatTabLabelWithIcon(tab.tab);
            text = expandedLabel.c_str();
        }
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const float textX = sidebarCollapsed_
            ? itemPosition.x + (width - textSize.x) * 0.5f
            : itemPosition.x + Scale(16.0f) + animation.shift;
        const float textY = itemPosition.y + (buttonHeight - textSize.y) * 0.5f;

        draw->AddText(
            ImVec2(textX, textY),
            ImGui::GetColorU32((isSelected || hovered) ? activeTextColor : textColor),
            text);

        if (sidebarCollapsed_ && hovered) {
            ImGui::SetTooltip("%s", GetTabLabel(tab.tab));
        }

        if (pressed) {
            currentTab_ = tab.tab;
        }

        ImGui::PopID();
    }

    ImGui::PopStyleVar();
    ImGui::EndGroup();
    return currentTab_;
}

void ModApp::DrawSectionCard(const char* id, const char* title, const char* description, const ImVec4& accent) const {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyle().Colors[ImGuiCol_ChildBg]);
    if (ImGui::BeginChild(id, ImVec2(0.0f, Scale(86.0f)), ImGuiChildFlags_FrameStyle)) {
        ImGui::TextColored(accent, "%s", title);
        ImGui::Spacing();
        ImGui::TextWrapped("%s", description);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void ModApp::DrawHomeTab() const {
    const UiSettings& ui = UiSettings::Instance();
    ImGui::SeparatorText(ui.Text(UiText::TabHome));
    ImGui::TextWrapped("%s", ui.Text(UiText::HomeIntro));
    ImGui::Spacing();

    DrawSectionCard(
        "home_shell",
        ui.Text(UiText::HomeInterfaceTitle),
        ui.Text(UiText::HomeInterfaceDesc),
        ImVec4(0.75f, 0.90f, 1.0f, 1.0f));
    DrawSectionCard(
        "home_tabs",
        ui.Text(UiText::HomeTabsTitle),
        ui.Text(UiText::HomeTabsDesc),
        ImVec4(0.60f, 1.0f, 0.72f, 1.0f));
}

void ModApp::DrawBinderTab() const {
    const_cast<BinderModule&>(binder_).DrawMainTab();
}

void ModApp::DrawHudTab(IDirect3DDevice9* device) {
    hud_.DrawMainTab(device);
}

void ModApp::DrawMiscTab() {
    if (unwanted_.IsMiscPageOpen()) {
        unwanted_.DrawMainPage();
        return;
    }

    tags_.DrawMiscTab();
    if (tags_.IsMiscHomePage()) {
        ImGui::Spacing();
        unwanted_.DrawMiscCard();
        ImGui::Spacing();
        DrawGameFixesCard();
    }
}

void ModApp::DrawGameFixesCard() {
    UiSettings& ui = UiSettings::Instance();
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Scale(8.0f));
    if (ImGui::BeginChild("##misc_game_fixes", ImVec2(0.0f, Scale(126.0f)), ImGuiChildFlags_FrameStyle)) {
        ImGui::TextColored(ImVec4(0.75f, 0.92f, 0.62f, 1.0f), "%s", ui.Text(UiText::MiscGameFixesTitle));
        ImGui::Spacing();
        ImGui::TextWrapped("%s", ui.Text(UiText::MiscGameFixesDesc));
        ImGui::Spacing();

        bool applyDamageProtectionEnabled = ui.ApplyDamageProtectionEnabled();
        if (ImGui::Checkbox(ui.Text(UiText::SettingsApplyDamageProtection), &applyDamageProtectionEnabled)) {
            ui.SetApplyDamageProtectionEnabled(applyDamageProtectionEnabled);
            sampHooks_.SetApplyDamageProtectionEnabled(applyDamageProtectionEnabled);
            debuglog::WriteInfo(
                "Settings changed: apply_damage_protection=%d",
                applyDamageProtectionEnabled ? 1 : 0);
        }
        ImGui::TextDisabled("%s", ui.Text(UiText::SettingsApplyDamageProtectionDesc));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void ModApp::DrawNotepadTab(IDirect3DDevice9* device) {
    notepad_.DrawMainTab(device);
}

void ModApp::DrawSettingsSummaryBar() {
    UiSettings& ui = UiSettings::Instance();
    AppConfig& config = AppConfig::Instance();

    std::string activeProfileName = config.ActiveProfileId();
    for (const ConfigProfile& profile : config.Profiles()) {
        if (profile.active) {
            activeProfileName = profile.name.empty() ? profile.id : profile.name;
            break;
        }
    }

    const int columnCount = ImGui::GetContentRegionAvail().x < Scale(640.0f) ? 2 : 4;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ScaleVec(0.0f, 2.0f));
    if (ImGui::BeginTable(
            "##settings_summary_table",
            columnCount,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
        int cell = 0;
        const auto drawCell = [&](const char* label, const std::string& value) {
            if (cell % columnCount == 0) {
                ImGui::TableNextRow();
            }
            ImGui::TableSetColumnIndex(cell % columnCount);
            DrawSummaryCell(label, value);
            ++cell;
        };

        drawCell(ui.Text(UiText::SettingsSummaryProfile), activeProfileName);
        drawCell(ui.Text(UiText::SettingsSummaryLanguage), ui.LanguageDisplayName(ui.Language()));
        drawCell(ui.Text(UiText::SettingsSummaryMainWindow), overlay_.MenuToggleHotkeyText());
        drawCell(ui.Text(UiText::SettingsSummaryQuickMenu), binder_.QuickMenuHotkeyText());
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::Spacing();
    ImGui::Separator();
}

void ModApp::DrawSettingsGeneralSection() {
    UiSettings& ui = UiSettings::Instance();
    ImGui::SeparatorText(ui.Text(UiText::SettingsSectionGeneral));
    ImGui::TextWrapped("%s", ui.Text(UiText::SettingsGeneralIntro));
    ImGui::Spacing();

    const UiLanguage languages[] = { UiLanguage::Russian, UiLanguage::English };
    const char* languageLabels[] = {
        ui.LanguageDisplayName(UiLanguage::Russian),
        ui.LanguageDisplayName(UiLanguage::English),
    };
    int languageIndex = ui.Language() == UiLanguage::English ? 1 : 0;
    ImGui::SetNextItemWidth(Scale(260.0f));
    if (ImGui::Combo(ui.Text(UiText::SettingsLanguage), &languageIndex, languageLabels, IM_ARRAYSIZE(languageLabels))) {
        ui.SetLanguage(languages[languageIndex]);
        debuglog::WriteInfo("Settings changed: language=%s", languageIndex == 1 ? "en" : "ru");
    }

    ImGui::Spacing();
    ImGui::SeparatorText(ui.Text(UiText::SettingsUiScale));
    bool autoScale = ui.AutoScaleEnabled();
    if (ImGui::Checkbox(ui.Text(UiText::SettingsAutoScale), &autoScale)) {
        ui.SetAutoScaleEnabled(autoScale);
        debuglog::WriteInfo("Settings changed: auto_scale=%d", autoScale ? 1 : 0);
    }

    float scaleMultiplierDraft = ui.ScaleMultiplierDraft();
    ImGui::SetNextItemWidth(Scale(300.0f));
    if (ImGui::SliderFloat(
            ui.Text(UiText::SettingsScaleMultiplier),
            &scaleMultiplierDraft,
            0.75f,
            2.0f,
            "%.2fx",
            ImGuiSliderFlags_AlwaysClamp)) {
        ui.SetScaleMultiplierDraft(scaleMultiplierDraft);
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && ui.CommitScaleMultiplierDraft()) {
        debuglog::WriteInfo("Settings changed: scale_multiplier=%.2f", ui.ScaleMultiplier());
    }

    ImGui::Text("%s: %.2fx", ui.Text(UiText::SettingsEffectiveScale), ui.CurrentScale());
    ImGui::TextWrapped("%s", ui.Text(UiText::SettingsScaleHint));
    ImGui::Spacing();

    if (ImGui::Button(ui.Text(UiText::SettingsResetDefaults))) {
        ui.ResetToDefaults();
        debuglog::WriteInfo("Interface settings reset to defaults");
    }
}

void ModApp::DrawSettingsHotkeysSection() {
    UiSettings& ui = UiSettings::Instance();
    ImGui::SeparatorText(ui.Text(UiText::SettingsSectionHotkeys));
    ImGui::TextWrapped("%s", ui.Text(UiText::SettingsHotkeysIntro));
    ImGui::Spacing();

    if (ImGui::BeginTable(
            "##settings_hotkeys_table",
            3,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch, 0.36f);
        ImGui::TableSetupColumn("hotkey", ImGuiTableColumnFlags_WidthStretch, 0.34f);
        ImGui::TableSetupColumn("buttons", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextWrapped("%s", ui.Text(UiText::SettingsMainWindowHotkey));
        ImGui::TextDisabled("%s", ui.Text(UiText::SettingsMainWindowHotkeyHelp));

        ImGui::TableSetColumnIndex(1);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", ui.Format(UiText::HotkeyFormat, overlay_.MenuToggleHotkeyText().c_str()).c_str());

        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button(ui.Text(UiText::ChangeHotkey))) {
            overlay_.BeginMenuToggleHotkeyCapture();
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::SettingsResetHotkey))) {
            overlay_.CancelMenuToggleHotkeyCapture();
            ui.ResetMenuToggleHotkey();
            debuglog::WriteInfo("Settings changed: open_menu_hotkey reset");
        }
        ImGui::EndTable();
    }
}

void ModApp::DrawSettingsNotificationsSection() {
    UiSettings& ui = UiSettings::Instance();
    NotificationSettings settings = notifications_.Settings();
    bool changed = false;

    ImGui::SeparatorText(ui.Text(UiText::SettingsSectionNotifications));

    bool enabled = settings.enabled;
    if (ImGui::Checkbox(ui.Text(UiText::SettingsNotificationsEnabled), &enabled)) {
        settings.enabled = enabled;
        changed = true;
    }

    ImGui::Spacing();
    ImGui::SeparatorText(ui.Text(UiText::SettingsNotificationsChannel));
    int channel = static_cast<int>(settings.channel);
    if (ImGui::RadioButton(ui.Text(UiText::SettingsNotificationsChannelPopup), channel == static_cast<int>(NotificationChannel::Popup))) {
        channel = static_cast<int>(NotificationChannel::Popup);
        settings.channel = NotificationChannel::Popup;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(ui.Text(UiText::SettingsNotificationsChannelLog), channel == static_cast<int>(NotificationChannel::Log))) {
        channel = static_cast<int>(NotificationChannel::Log);
        settings.channel = NotificationChannel::Log;
        changed = true;
    }

    ImGui::Spacing();
    ImGui::SeparatorText(ui.Text(UiText::SettingsNotificationsGroups));
    const struct {
        NotificationGroup group;
        UiText label;
    } groupRows[] = {
        { NotificationGroup::BinderErrors, UiText::SettingsNotificationsGroupBinderErrors },
        { NotificationGroup::TagErrors, UiText::SettingsNotificationsGroupTagErrors },
        { NotificationGroup::SampDialogErrors, UiText::SettingsNotificationsGroupSampDialogErrors },
        { NotificationGroup::Success, UiText::SettingsNotificationsGroupSuccess },
        { NotificationGroup::Confirmation, UiText::SettingsNotificationsGroupConfirmation },
    };
    if (ImGui::BeginTable(
            "##settings_notification_groups",
            ImGui::GetContentRegionAvail().x < Scale(620.0f) ? 1 : 2,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
        int cell = 0;
        for (const auto& row : groupRows) {
            if (cell % ImGui::TableGetColumnCount() == 0) {
                ImGui::TableNextRow();
            }
            ImGui::TableSetColumnIndex(cell % ImGui::TableGetColumnCount());
            bool groupEnabled = settings.groups[NotificationGroupIndex(row.group)];
            if (ImGui::Checkbox(ui.Text(row.label), &groupEnabled)) {
                settings.groups[NotificationGroupIndex(row.group)] = groupEnabled;
                changed = true;
            }
            ++cell;
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText(ui.Text(UiText::SettingsNotificationsPosition));
    const NotificationPosition positions[] = {
        NotificationPosition::TopLeft,
        NotificationPosition::TopCenter,
        NotificationPosition::TopRight,
        NotificationPosition::MiddleLeft,
        NotificationPosition::MiddleCenter,
        NotificationPosition::MiddleRight,
        NotificationPosition::BottomLeft,
        NotificationPosition::BottomCenter,
        NotificationPosition::BottomRight,
    };
    const char* positionLabels[] = {
        ui.Text(UiText::SettingsNotificationsPositionTopLeft),
        ui.Text(UiText::SettingsNotificationsPositionTopCenter),
        ui.Text(UiText::SettingsNotificationsPositionTopRight),
        ui.Text(UiText::SettingsNotificationsPositionMiddleLeft),
        ui.Text(UiText::SettingsNotificationsPositionMiddleCenter),
        ui.Text(UiText::SettingsNotificationsPositionMiddleRight),
        ui.Text(UiText::SettingsNotificationsPositionBottomLeft),
        ui.Text(UiText::SettingsNotificationsPositionBottomCenter),
        ui.Text(UiText::SettingsNotificationsPositionBottomRight),
    };
    int positionIndex = static_cast<int>(settings.position);
    ImGui::SetNextItemWidth(Scale(260.0f));
    if (ImGui::Combo(ui.Text(UiText::SettingsNotificationsPosition), &positionIndex, positionLabels, IM_ARRAYSIZE(positionLabels))) {
        settings.position = positions[std::clamp(positionIndex, 0, static_cast<int>(IM_ARRAYSIZE(positions)) - 1)];
        changed = true;
    }

    ImGui::SetNextItemWidth(Scale(160.0f));
    if (ImGui::DragFloat(ui.Text(UiText::SettingsNotificationsOffsetX), &settings.offsetX, 1.0f, -500.0f, 500.0f, "%.0f")) {
        changed = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(Scale(160.0f));
    if (ImGui::DragFloat(ui.Text(UiText::SettingsNotificationsOffsetY), &settings.offsetY, 1.0f, -500.0f, 500.0f, "%.0f")) {
        changed = true;
    }

    ImGui::Spacing();
    ImGui::SeparatorText(ui.Text(UiText::SettingsNotificationsDisplay));
    int durationMs = static_cast<int>(settings.durationMs);
    ImGui::SetNextItemWidth(Scale(180.0f));
    if (ImGui::DragInt(ui.Text(UiText::SettingsNotificationsDurationMs), &durationMs, 25.0f, 500, 15000)) {
        settings.durationMs = static_cast<double>(durationMs);
        changed = true;
    }
    ImGui::SetNextItemWidth(Scale(180.0f));
    if (ImGui::DragFloat(ui.Text(UiText::SettingsNotificationsWidth), &settings.width, 2.0f, 200.0f, 560.0f, "%.0f")) {
        changed = true;
    }
    ImGui::SetNextItemWidth(Scale(180.0f));
    if (ImGui::SliderFloat(ui.Text(UiText::SettingsNotificationsOpacity), &settings.opacity, 0.20f, 1.0f, "%.2f")) {
        changed = true;
    }

    ImGui::Spacing();
    ImGui::SeparatorText(ui.Text(UiText::SettingsNotificationsAntiFlood));
    int dedupeMs = static_cast<int>(settings.dedupeWindowMs);
    ImGui::SetNextItemWidth(Scale(180.0f));
    if (ImGui::DragInt(ui.Text(UiText::SettingsNotificationsDedupeMs), &dedupeMs, 25.0f, 0, 10000)) {
        settings.dedupeWindowMs = static_cast<double>(dedupeMs);
        changed = true;
    }
    ImGui::SetNextItemWidth(Scale(180.0f));
    if (ImGui::DragInt(ui.Text(UiText::SettingsNotificationsMaxVisible), &settings.maxVisible, 0.1f, 1, 10)) {
        changed = true;
    }
    ImGui::SetNextItemWidth(Scale(180.0f));
    if (ImGui::DragInt(ui.Text(UiText::SettingsNotificationsMaxQueue), &settings.maxQueue, 0.5f, 1, 64)) {
        changed = true;
    }

    if (changed) {
        notifications_.ApplySettings(settings);
    }

    ImGui::Spacing();
    if (ImGui::Button(ui.Text(UiText::SettingsNotificationsTest))) {
        notifications_.Notify(
            NotificationGroup::Success,
            NotificationSeverity::Success,
            ui.Text(UiText::SettingsNotificationsTestText),
            settings.durationMs);
    }
}

void ModApp::DrawSettingsProfilesSection() {
    UiSettings& ui = UiSettings::Instance();
    AppConfig& config = AppConfig::Instance();
    std::vector<ConfigProfile> profiles = config.Profiles();
    std::string activeProfileId = config.ActiveProfileId();
    auto activeProfileIt = std::find_if(profiles.begin(), profiles.end(), [&](const ConfigProfile& profile) {
        return profile.id == activeProfileId;
    });
    if (activeProfileIt == profiles.end() && !profiles.empty()) {
        activeProfileIt = profiles.begin();
        activeProfileId = activeProfileIt->id;
    }

    const auto profileDisplayName = [](const ConfigProfile& profile) {
        return profile.name.empty() ? profile.id : profile.name;
    };

    std::unordered_map<std::string, int> profileNameCounts;
    for (const ConfigProfile& profile : profiles) {
        ++profileNameCounts[profileDisplayName(profile)];
    }

    const std::string activeProfileName =
        activeProfileIt == profiles.end() ? std::string() : profileDisplayName(*activeProfileIt);
    if (profileNameBufferProfileId_ != activeProfileId) {
        profileNameBuffer_ = activeProfileName;
        profileNameBufferProfileId_ = activeProfileId;
        profileUiError_.clear();
    }

    ImGui::SeparatorText(ui.Text(UiText::SettingsProfilesSection));
    ImGui::TextWrapped("%s", ui.Text(UiText::SettingsProfilesIntro));
    ImGui::Spacing();

    const auto flushShellBeforeProfileChange = [&]() {
        SaveShellStateIfDirty();
        hud_.FlushPendingSaves();
        AppConfig::Instance().ProcessPendingWrites();
    };

    ImGui::SetNextItemWidth(Scale(320.0f));
    if (ImGui::BeginCombo(ui.Text(UiText::SettingsActiveProfile), activeProfileName.c_str())) {
        for (const ConfigProfile& profile : profiles) {
            const bool selected = profile.id == activeProfileId;
            std::string label = profileDisplayName(profile);
            if (profileNameCounts[label] > 1) {
                label += " (" + profile.id + ")";
            }
            ImGui::PushID(profile.id.c_str());
            if (ImGui::Selectable(label.c_str(), selected)) {
                std::string error;
                flushShellBeforeProfileChange();
                notepad_.FlushPendingEdits();
                if (config.SwitchProfile(profile.id, &error)) {
                    profileNameBufferProfileId_.clear();
                    profileUiError_.clear();
                    ReloadConfigAfterProfileChange();
                    debuglog::WriteInfo("[profiles] UI switched profile id=%s", profile.id.c_str());
                } else {
                    profileUiError_ = ui.Text(UiText::SettingsProfileOperationFailed);
                    debuglog::WriteError("[profiles] UI switch failed id=%s error=%s", profile.id.c_str(), error.c_str());
                }
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    ImGui::SetNextItemWidth(Scale(320.0f));
    InputTextString(ui.Text(UiText::SettingsProfileName), profileNameBuffer_, ImGuiInputTextFlags_AutoSelectAll, 128);

    const auto requireProfileName = [&]() {
        if (TrimAsciiWhitespace(profileNameBuffer_).empty()) {
            profileUiError_ = ui.Text(UiText::SettingsProfileNameRequired);
            return false;
        }
        return true;
    };
    const auto failProfileOperation = [&](const char* action, const std::string& error) {
        profileUiError_ = ui.Text(UiText::SettingsProfileOperationFailed);
        debuglog::WriteError("[profiles] UI %s failed: %s", action, error.c_str());
    };

    if (ImGui::Button(ui.Text(UiText::SettingsProfileCreateEmpty))) {
        if (requireProfileName()) {
            std::string error;
            flushShellBeforeProfileChange();
            notepad_.FlushPendingEdits();
            if (config.CreateProfile(profileNameBuffer_, false, true, &error)) {
                profileNameBufferProfileId_.clear();
                profileUiError_.clear();
                ReloadConfigAfterProfileChange();
            } else {
                failProfileOperation("create", error);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::SettingsProfileDuplicate))) {
        if (requireProfileName()) {
            std::string error;
            flushShellBeforeProfileChange();
            notepad_.FlushPendingEdits();
            if (config.DuplicateProfile(activeProfileId, profileNameBuffer_, true, &error)) {
                profileNameBufferProfileId_.clear();
                profileUiError_.clear();
                ReloadConfigAfterProfileChange();
            } else {
                failProfileOperation("duplicate", error);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::SettingsProfileRename))) {
        if (requireProfileName()) {
            std::string error;
            if (config.RenameProfile(activeProfileId, profileNameBuffer_, &error)) {
                profileNameBufferProfileId_.clear();
                profileUiError_.clear();
            } else {
                failProfileOperation("rename", error);
            }
        }
    }

    const bool canDeleteProfile = profiles.size() > 1;
    if (!canDeleteProfile) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(ui.Text(UiText::SettingsProfileDelete))) {
        profileDeleteTargetId_ = activeProfileId;
        profileDeletePopupPending_ = true;
    }
    if (!canDeleteProfile) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ui.Text(UiText::SettingsProfileCannotDeleteLast));
    }

    if (!profileUiError_.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.30f, 1.0f), "%s", profileUiError_.c_str());
    }

    if (profileDeletePopupPending_) {
        ImGui::OpenPopup(ui.Text(UiText::SettingsProfileDeleteTitle));
        profileDeletePopupPending_ = false;
    }
    if (ImGui::BeginPopupModal(
            ui.Text(UiText::SettingsProfileDeleteTitle),
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const std::vector<ConfigProfile> currentProfiles = config.Profiles();
        const auto deleteProfileIt = std::find_if(currentProfiles.begin(), currentProfiles.end(), [&](const ConfigProfile& profile) {
            return profile.id == profileDeleteTargetId_;
        });
        const std::string deleteProfileName =
            deleteProfileIt == currentProfiles.end() ? activeProfileName : deleteProfileIt->name;
        ImGui::TextWrapped(
            "%s",
            ui.Format(UiText::SettingsProfileDeleteQuestionFormat, deleteProfileName.c_str()).c_str());
        if (ImGui::Button(ui.Text(UiText::Delete))) {
            const std::string previousActiveProfileId = config.ActiveProfileId();
            std::string error;
            flushShellBeforeProfileChange();
            notepad_.FlushPendingEdits();
            if (config.DeleteProfile(profileDeleteTargetId_, &error)) {
                profileDeleteTargetId_.clear();
                profileNameBufferProfileId_.clear();
                profileUiError_.clear();
                if (config.ActiveProfileId() != previousActiveProfileId) {
                    ReloadConfigAfterProfileChange();
                }
                ImGui::CloseCurrentPopup();
            } else {
                failProfileOperation("delete", error);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::Cancel))) {
            profileDeleteTargetId_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ModApp::DrawSettingsDiagnosticsSection() {
    UiSettings& ui = UiSettings::Instance();
    AppConfig& config = AppConfig::Instance();

    ImGui::SeparatorText(ui.Text(UiText::SettingsSectionDiagnostics));
    ImGui::TextWrapped("%s", ui.Text(UiText::SettingsDiagnosticsIntro));
    ImGui::Spacing();

    const UiLogLevel logLevels[] = { UiLogLevel::Off, UiLogLevel::Error, UiLogLevel::Info };
    const char* logLevelLabels[] = {
        ui.Text(UiText::SettingsLogLevelOff),
        ui.Text(UiText::SettingsLogLevelError),
        ui.Text(UiText::SettingsLogLevelInfo),
    };
    int logLevelIndex = static_cast<int>(ui.LogLevel());
    ImGui::SetNextItemWidth(Scale(220.0f));
    if (ImGui::Combo(ui.Text(UiText::SettingsLogLevel), &logLevelIndex, logLevelLabels, IM_ARRAYSIZE(logLevelLabels))) {
        const UiLogLevel selected = logLevels[std::clamp(logLevelIndex, 0, 2)];
        ui.SetLogLevel(selected);
        debuglog::SetLevel(ToDebugLogLevel(selected));
        debuglog::WriteInfo("Settings changed: log_level=%s", ToUiLogLevelName(selected));
    }

    std::string sampStatus = ui.Text(UiText::SettingsSampWaiting);
    if (sampApi_.sampModule()) {
        sampStatus = sampApi_.isSupportedVersion()
            ? (sampUiPipelineReady_ ? ui.Text(UiText::SettingsSampReady) : ui.Text(UiText::SettingsSampLoaded))
            : ui.Text(UiText::SettingsSampUnsupported);
    }
    const std::string sampVersion = sampApi_.sampModule() ? sampApi_.currentVersionName() : "-";
    const std::string chatAsiStatus =
        GetModuleHandleA("_chat.asi") ? ui.Text(UiText::SettingsChatAsiLoaded) : ui.Text(UiText::SettingsChatAsiNotLoaded);

    ImGui::Spacing();
    if (ImGui::BeginTable(
            "##settings_diagnostics_table",
            2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, Scale(190.0f));
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        DrawPathDiagnosticRow(UiText::SettingsProfilesPath, config.ProfilesRoot(), UiText::SettingsOpenProfilesFolder);
        ImGui::TableNextRow();
        DrawPathDiagnosticRow(UiText::SettingsConfigPath, config.ConfigPath(), UiText::SettingsOpenConfigFile);
        ImGui::TableNextRow();
        DrawPathDiagnosticRow(UiText::SettingsLogPath, DebugLogPath(module_), UiText::SettingsOpenLogFile);
        ImGui::TableNextRow();
        DrawPathDiagnosticRow(UiText::SettingsProfilesRegistryPath, config.ProfilesRegistryPath(), UiText::SettingsOpenRegistryFile);

        ImGui::TableNextRow();
        DrawDiagnosticValue(ui.Text(UiText::SettingsGtaVersion), plugin::GetGameVersionName());
        ImGui::TableNextRow();
        DrawDiagnosticValue(ui.Text(UiText::SettingsSampStatus), sampStatus);
        ImGui::TableNextRow();
        DrawDiagnosticValue(ui.Text(UiText::SettingsSampVersion), sampVersion);
        ImGui::TableNextRow();
        DrawDiagnosticValue(ui.Text(UiText::SettingsChatAsiStatus), chatAsiStatus);
        ImGui::TableNextRow();
        DrawDiagnosticValue(ui.Text(UiText::SettingsFallbackStatus), ui.Text(UiText::SettingsFallbackAvailable));
        ImGui::TableNextRow();
        DrawDiagnosticValue(ui.Text(UiText::SettingsHooksStatus), sampHooks_.statusText());
        ImGui::TableNextRow();
        DrawDiagnosticValue(ui.Text(UiText::SettingsRakHooksStatus), sampRakHooks_.statusText());

        ImGui::EndTable();
    }
}

void ModApp::DrawSettingsTab() {
    UiSettings& ui = UiSettings::Instance();
    ImGui::SeparatorText(ui.Text(UiText::TabSettings));

    const UiSettingsSection activeSection = ui.SettingsActiveSection();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Scale(30.0f), 0.0f));
    if (ImGui::BeginChild(
            "##settings_section_tabs",
            ImVec2(0.0f, Scale(56.0f)),
            ImGuiChildFlags_None,
            ImGuiWindowFlags_HorizontalScrollbar)) {
        for (std::size_t index = 0; index < kSettingsSections.size(); ++index) {
            const SettingsSectionDefinition& section = kSettingsSections[index];
            if (index > 0) {
                ImGui::SameLine();
            }

            ImGui::PushID(static_cast<int>(index));
            if (DrawSettingsHeaderTab(section.label, activeSection == section.section)) {
                ui.SetSettingsActiveSection(section.section);
                debuglog::WriteInfo("Settings section changed");
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    DrawSettingsSummaryBar();
    ImGui::Spacing();

    if (ImGui::BeginChild("##settings_content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None)) {
        switch (ui.SettingsActiveSection()) {
        case UiSettingsSection::Hotkeys:
            DrawSettingsHotkeysSection();
            break;
        case UiSettingsSection::Notifications:
            DrawSettingsNotificationsSection();
            break;
        case UiSettingsSection::Profiles:
            DrawSettingsProfilesSection();
            break;
        case UiSettingsSection::Binder:
            binder_.DrawBinderSettingsSection(true);
            break;
        case UiSettingsSection::QuickMenu:
            ImGui::SeparatorText(ui.Text(UiText::SettingsSectionQuickMenu));
            ImGui::TextWrapped("%s", ui.Text(UiText::SettingsQuickMenuIntro));
            ImGui::Spacing();
            binder_.DrawSettingsSection(false);
            break;
        case UiSettingsSection::Diagnostics:
            DrawSettingsDiagnosticsSection();
            break;
        case UiSettingsSection::General:
        default:
            DrawSettingsGeneralSection();
            break;
        }
    }
    ImGui::EndChild();
}

void ModApp::PrepareUiForImGuiNewFrame(IDirect3DDevice9* device) {
    (void)device;
    const double beginMs = UiPerfNowMs();
    double stageBeginMs = beginMs;
    ImGuiIO& io = ImGui::GetIO();
    const float uiScale = UiSettings::Instance().UpdateScale(io.DisplaySize);
    const double scaleMs = UiPerfNowMs() - stageBeginMs;
    static float s_lastLoggedScale = 0.0f;
    static ImVec2 s_lastLoggedDisplay{};
    if (std::abs(uiScale - s_lastLoggedScale) > 0.001f
        || std::abs(io.DisplaySize.x - s_lastLoggedDisplay.x) > 0.5f
        || std::abs(io.DisplaySize.y - s_lastLoggedDisplay.y) > 0.5f) {
        s_lastLoggedScale = uiScale;
        s_lastLoggedDisplay = io.DisplaySize;
        debuglog::WriteInfo(
            "[ui] frame prep scale=%.3f display=(%.1f,%.1f)",
            uiScale,
            io.DisplaySize.x,
            io.DisplaySize.y);
    }
    stageBeginMs = UiPerfNowMs();
    io.FontGlobalScale = uiScale;
    ApplyMainStyle(uiScale);
    const double styleMs = UiPerfNowMs() - stageBeginMs;
    const double logoMs = 0.0;
    const double totalMs = UiPerfNowMs() - beginMs;
    static bool s_firstPreparePerfLogged = false;
    if (!s_firstPreparePerfLogged || totalMs >= kUiModuleSlowFrameMs || logoMs >= 4.0) {
        debuglog::WriteInfo(
            "[ui][perf][prepare] total=%.2fms scale=%.2fms style=%.2fms logo=%.2fms first=%d logoAttempted=%d logoLoaded=%d display=(%.1f,%.1f)",
            totalMs,
            scaleMs,
            styleMs,
            logoMs,
            s_firstPreparePerfLogged ? 0 : 1,
            logoLoadAttempted_ ? 1 : 0,
            logoTexture_ ? 1 : 0,
            io.DisplaySize.x,
            io.DisplaySize.y);
        s_firstPreparePerfLogged = true;
    }
}

void ModApp::RenderUi(IDirect3DDevice9* device) {
    const double renderBeginMs = UiPerfNowMs();
    RenderUiPerf perf{};
    perf.surface = CurrentOverlayFrameSurface();

    ImGuiIO& io = ImGui::GetIO();
    const float uiScale = io.FontGlobalScale;

    const bool showMainWindow = overlay_.IsMenuOpen();
    perf.menuOpen = showMainWindow;
    perf.tab = showMainWindow ? static_cast<int>(currentTab_) : -1;

    static bool s_lastShowMainWindow = false;
    if (showMainWindow != s_lastShowMainWindow) {
        if (!showMainWindow) {
            SaveShellStateIfDirty();
        }
        s_lastShowMainWindow = showMainWindow;
        debuglog::WriteInfo("[ui] main window visibility -> %d", showMainWindow ? 1 : 0);
    }
    const bool quickMenuActive = binder_.IsQuickMenuOpen();
    hud_.SetPlacementInputBlocked(quickMenuActive);

    double stageBeginMs = UiPerfNowMs();
    hud_.DrawOverlay(device);
    perf.hudOverlayMs = UiPerfNowMs() - stageBeginMs;
    perf.hudStats = hud_.LastOverlayStats();

    if (!showMainWindow) {
        stageBeginMs = UiPerfNowMs();
        binder_.DrawOverlay();
        perf.binderOverlayMs = UiPerfNowMs() - stageBeginMs;

        stageBeginMs = UiPerfNowMs();
        notifications_.DrawOverlay();
        perf.notificationsMs = UiPerfNowMs() - stageBeginMs;

        stageBeginMs = UiPerfNowMs();
        AppConfig::Instance().ProcessPendingWrites();
        perf.configWritesMs = UiPerfNowMs() - stageBeginMs;
        perf.totalMs = UiPerfNowMs() - renderBeginMs;
        perf.mainWindowShellMs = std::max(
            0.0,
            perf.totalMs
                - perf.hudOverlayMs
                - perf.binderOverlayMs
                - perf.notificationsMs
                - perf.configWritesMs);
        AccumulateRenderUiPerf(perf);
        return;
    }

    if (io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f) {
        if (!mainWindowInitialized_) {
            if (!mainWindowRectLoaded_) {
                const float margin = kWindowMargin * uiScale;
                mainWindowSize_.x = std::min(1100.0f * uiScale, io.DisplaySize.x - margin * 2.0f);
                mainWindowSize_.y = std::min(720.0f * uiScale, io.DisplaySize.y - margin * 2.0f);
                mainWindowPos_.x = std::max(margin, (io.DisplaySize.x - mainWindowSize_.x) * 0.5f);
                mainWindowPos_.y = std::max(margin, (io.DisplaySize.y - mainWindowSize_.y) * 0.5f);
            }
            mainWindowRectKnown_ = true;
            mainWindowInitialized_ = true;
        }

        ClampWindowRect(io.DisplaySize, mainWindowPos_, mainWindowSize_, uiScale);
    }

    const ImVec2 mainWindowFramePos = mainWindowPos_;
    const ImVec2 mainWindowFrameSize = mainWindowSize_;
    ImGui::SetNextWindowPos(mainWindowPos_, ImGuiCond_Always);
    ImGui::SetNextWindowSize(mainWindowSize_, ImGuiCond_Always);

    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin("HelperByOrc##main_window", nullptr, windowFlags)) {
        EnsureLogoTexture(device);

        const ImVec2 imguiWindowPos = ImGui::GetWindowPos();
        const ImVec2 imguiWindowSize = ImGui::GetWindowSize();
        if (!SameWindowRect(mainWindowFramePos, mainWindowFrameSize, imguiWindowPos, imguiWindowSize)
            && (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Left))) {
            mainWindowRectDirty_ = true;
        }
        mainWindowPos_ = imguiWindowPos;
        mainWindowSize_ = imguiWindowSize;
        mainWindowRectKnown_ = true;

        ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 pad = style.WindowPadding;
        const float titleHeight = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;
        const ImVec2 winPos = ImGui::GetWindowPos();
        const ImVec2 winSize = ImGui::GetWindowSize();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##titlebar", ImVec2(winSize.x, titleHeight));

        const ImVec2 titleMin = ImGui::GetItemRectMin();
        const ImVec2 titleMax = ImGui::GetItemRectMax();
        draw->AddRectFilled(
            titleMin,
            titleMax,
            ImGui::GetColorU32(ImGui::IsItemActive() ? style.Colors[ImGuiCol_TitleBgActive] : style.Colors[ImGuiCol_TitleBg]),
            Scale(6.0f),
            ImDrawFlags_RoundCornersTop);
        draw->AddText(
            ImVec2(titleMin.x + pad.x, titleMin.y + style.FramePadding.y),
            ImGui::GetColorU32(style.Colors[ImGuiCol_Text]),
            UiSettings::Instance().Text(UiText::AppBrand));
        draw->AddText(
            ImVec2(titleMin.x + Scale(110.0f), titleMin.y + style.FramePadding.y),
            ImGui::GetColorU32(style.Colors[ImGuiCol_TextDisabled]),
            FormatTabLabelWithIcon(currentTab_).c_str());

        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
            if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
                mainWindowPos_.x += io.MouseDelta.x;
                mainWindowPos_.y += io.MouseDelta.y;
                mainWindowRectKnown_ = true;
                mainWindowRectDirty_ = true;
            }
        }

        const char* closeText = "X";
        const ImVec2 closeTextSize = ImGui::CalcTextSize(closeText);
        const float closeSide =
            std::max(titleHeight - Scale(6.0f), std::ceil(std::max(closeTextSize.x, ImGui::GetFontSize()) + Scale(4.0f)));
        const ImVec2 closePos(titleMax.x - pad.x - closeSide, titleMin.y + (titleHeight - closeSide) * 0.5f);
        ImGui::SetCursorScreenPos(closePos);
        const bool closePressed = ImGui::InvisibleButton("##close_main_window", ImVec2(closeSide, closeSide));
        const bool closeHovered = ImGui::IsItemHovered();
        const bool closeActive = ImGui::IsItemActive();
        const ImVec2 closeMin = ImGui::GetItemRectMin();
        const ImVec2 closeMax = ImGui::GetItemRectMax();

        if (closeHovered || closeActive) {
            const ImVec4 baseColor = closeActive ? style.Colors[ImGuiCol_ButtonActive] : style.Colors[ImGuiCol_ButtonHovered];
            draw->AddRectFilled(closeMin, closeMax, ImGui::GetColorU32(baseColor), std::max(Scale(3.0f), style.FrameRounding));
        }

        draw->AddText(
            ImVec2(closeMin.x + (closeSide - closeTextSize.x) * 0.5f, closeMin.y + (closeSide - closeTextSize.y) * 0.5f),
            ImGui::GetColorU32((closeHovered || closeActive) ? style.Colors[ImGuiCol_Text] : style.Colors[ImGuiCol_TextDisabled]),
            closeText);

        if (closePressed) {
            SaveShellStateIfDirty();
            overlay_.SetMenuOpen(false);
        }

        const float sidebarWidth = Scale(sidebarCollapsed_ ? kSidebarCollapsedWidth : kSidebarExpandedWidth);
        const float logoSize = Scale(sidebarCollapsed_ ? kLogoCollapsedSize : kLogoExpandedSize);

        ImGui::SetCursorPos(ImVec2(pad.x, pad.y + titleHeight));
        ImGui::BeginGroup();

        if (ImGui::BeginChild("logo_panel", ImVec2(sidebarWidth, logoSize), kPlainChildFlags)) {
            if (logoTexture_) {
                DrawLogoZoom(
                    logoTexture_,
                    logoWidth_,
                    logoHeight_,
                    currentTab_,
                    ImVec2(logoSize, logoSize),
                    sidebarCollapsed_ ? 0.9f : 1.2f);
            } else {
                ImGui::SetCursorPosY(std::max(0.0f, (logoSize - ImGui::GetTextLineHeight()) * 0.5f));
                ImGui::TextUnformatted(
                    UiSettings::Instance().Text(sidebarCollapsed_ ? UiText::AppBrandCompact : UiText::AppBrand));
            }

            const char* toggleIcon = sidebarCollapsed_ ? ">" : "<";
            const float toggleSide = std::min(Scale(18.0f), std::max(Scale(14.0f), logoSize - Scale(10.0f)));
            const float togglePad = Scale(4.0f);
            const ImVec2 togglePos(
                std::max(0.0f, sidebarWidth - toggleSide - togglePad),
                togglePad);
            ImGui::SetCursorPos(togglePos);
            ImGui::InvisibleButton("##sidebar_toggle", ImVec2(toggleSide, toggleSide));
            const bool toggleHovered = ImGui::IsItemHovered();
            const bool togglePressed = ImGui::IsItemClicked();
            const ImVec2 toggleMin = ImGui::GetItemRectMin();
            const ImVec2 toggleTextSize = ImGui::CalcTextSize(toggleIcon);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(toggleMin.x + (toggleSide - toggleTextSize.x) * 0.5f, toggleMin.y + (toggleSide - toggleTextSize.y) * 0.5f),
                ImGui::GetColorU32(toggleHovered ? style.Colors[ImGuiCol_Text] : style.Colors[ImGuiCol_TextDisabled]),
                toggleIcon);
            if (togglePressed) {
                SetSidebarCollapsed(!sidebarCollapsed_);
            }
        }
        ImGui::EndChild();

        if (ImGui::BeginChild("vertical_menu", ImVec2(sidebarWidth, 0.0f), kPlainChildFlags)) {
            DrawAnimatedMenu(sidebarWidth);
        }
        ImGui::EndChild();
        ImGui::EndGroup();

        ImGui::SameLine();
        if (ImGui::BeginChild("main_content", ImVec2(0.0f, 0.0f), kBorderedChildFlags)) {
            stageBeginMs = UiPerfNowMs();
            switch (currentTab_) {
            case MainTab::Home:
                DrawHomeTab();
                break;
            case MainTab::Binder:
                DrawBinderTab();
                break;
            case MainTab::Hud:
                DrawHudTab(device);
                perf.hudEditorStats = hud_.LastEditorStats();
                break;
            case MainTab::Misc:
                DrawMiscTab();
                break;
            case MainTab::Notepad:
                DrawNotepadTab(device);
                perf.notepadStats = notepad_.LastRenderStats();
                break;
            case MainTab::Settings:
                DrawSettingsTab();
                break;
            }
            perf.activeTabMs = UiPerfNowMs() - stageBeginMs;
        }
        ImGui::EndChild();
    }

    ImGui::End();
    if (mainWindowRectDirty_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        SaveShellStateIfDirty();
    }
    overlay_.DrawMenuToggleHotkeyCapturePopup();

    stageBeginMs = UiPerfNowMs();
    binder_.DrawOverlay();
    perf.binderOverlayMs = UiPerfNowMs() - stageBeginMs;

    stageBeginMs = UiPerfNowMs();
    notifications_.DrawOverlay();
    perf.notificationsMs = UiPerfNowMs() - stageBeginMs;

    stageBeginMs = UiPerfNowMs();
    AppConfig::Instance().ProcessPendingWrites();
    perf.configWritesMs = UiPerfNowMs() - stageBeginMs;
    perf.totalMs = UiPerfNowMs() - renderBeginMs;
    perf.mainWindowShellMs = std::max(
        0.0,
        perf.totalMs
            - perf.hudOverlayMs
            - perf.activeTabMs
            - perf.binderOverlayMs
            - perf.notificationsMs
            - perf.configWritesMs);
    AccumulateRenderUiPerf(perf);
}
