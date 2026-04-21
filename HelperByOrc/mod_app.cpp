#include "mod_app.h"

#include "app_config.h"
#include "debug_log.h"
#include "minhook_utils.h"
#include "resource.h"
#include "ui_settings.h"

#include <GameVersion.h>
#include <d3dx9tex.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <string>

namespace {

constexpr float kSidebarExpandedWidth = 148.0f;
constexpr float kSidebarCollapsedWidth = 50.0f;
constexpr float kLogoExpandedSize = 128.0f;
constexpr float kLogoCollapsedSize = 50.0f;
constexpr float kWindowMargin = 12.0f;
constexpr int kSampCursorModeNone = 0;
constexpr int kSampCursorModeLockCam = 3;
constexpr uint64_t kCursorReassertIntervalMs = 200;
constexpr uint64_t kCursorTraceIntervalMs = 700;
constexpr uint64_t kCursorUnavailableTraceIntervalMs = 1500;
constexpr uint64_t kUiScaleTraceIntervalMs = 2000;
constexpr std::string_view kShellSectionName = "shell";
constexpr ImGuiChildFlags kBorderedChildFlags = ImGuiChildFlags_Borders;
constexpr ImGuiChildFlags kPlainChildFlags = ImGuiChildFlags_None;
constexpr char kIconHouse[] = "\xEF\xA0\x8C";
constexpr char kIconKeyboard[] = "\xEF\x84\x9C";
constexpr char kIconNewspaper[] = "\xEF\x87\xAA";
constexpr char kIconBook[] = "\xEF\x80\xAD";
constexpr char kIconCubes[] = "\xEF\x86\xB3";
constexpr char kIconGear[] = "\xEF\x80\x93";

struct TabDefinition {
    MainTab tab;
    UiText label;
    UiText compactLabel;
    const char* icon;
};

const std::array<TabDefinition, 6> kTabs = {{
    { MainTab::Home, UiText::TabHome, UiText::TabHomeCompact, kIconHouse },
    { MainTab::Binder, UiText::TabBinder, UiText::TabBinderCompact, kIconKeyboard },
    { MainTab::SmiHelper, UiText::TabSmiHelper, UiText::TabSmiHelperCompact, kIconNewspaper },
    { MainTab::Misc, UiText::TabMisc, UiText::TabMiscCompact, kIconCubes },
    { MainTab::Notepad, UiText::TabNotepad, UiText::TabNotepadCompact, kIconBook },
    { MainTab::Settings, UiText::TabSettings, UiText::TabSettingsCompact, kIconGear },
}};

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
    const float maxWidth = std::max(360.0f * scale, displaySize.x - margin * 2.0f);
    const float maxHeight = std::max(280.0f * scale, displaySize.y - margin * 2.0f);
    const float minWidth = std::min(840.0f * scale, maxWidth);
    const float minHeight = std::min(560.0f * scale, maxHeight);

    size.x = std::clamp(size.x, minWidth, maxWidth);
    size.y = std::clamp(size.y, minHeight, maxHeight);
    position.x = std::clamp(position.x, margin, displaySize.x - size.x - margin);
    position.y = std::clamp(position.y, margin, displaySize.y - size.y - margin);
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
    LoadShellState();

    tags_.SetSampApi(&sampApi_);
    tags_.OnProcessAttach();
    sampApi_.attachModules([this](std::string_view text) { return tags_.ExpandText(text); });
    sampHooks_.SetSampApi(&sampApi_);
    sampHooks_.SetHotkeyBlockCallback([this]() {
        return overlay_.IsTextInputActive();
    });
    sampHooks_.AddOnSendCommandHandler([this](std::string& text) {
        text = tags_.ExpandOutgoingText(text, "command", text);
        return true;
    });
    sampHooks_.AddOnSendChatHandler([this](std::string& text) {
        text = tags_.ExpandOutgoingText(text, "chat", text);
        return true;
    });
    sampRakHooks_.SetSampApi(&sampApi_);
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
    incomingMessageRouter_.SetSampHooks(&sampHooks_);
    incomingMessageRouter_.SetSampRakHooks(&sampRakHooks_);
    binder_.OnProcessAttach(module);
    binder_.SetSampApi(&sampApi_);
    binder_.SetSampHooks(&sampHooks_);
    binder_.SetSampRakHooks(&sampRakHooks_);
    binder_.SetIncomingMessageRouter(&incomingMessageRouter_);
    binder_.SetTagsModule(&tags_);
    tags_.SetBinderModule(&binder_);

    overlay_.SetPrepareFrameCallback([this](IDirect3DDevice9* device) { PrepareUiForImGuiNewFrame(device); });
    overlay_.SetRenderCallback([this](IDirect3DDevice9* device) { RenderUi(device); });
    overlay_.SetUpdateCallback([this]() { Tick(); });
    overlay_.SetWindowMessageCallback([this](UINT message, WPARAM wparam, LPARAM lparam) {
        return binder_.OnWindowMessage(message, wparam, lparam);
    });
    overlay_.SetAuxiliaryUiVisibleCallback([this]() { return binder_.WantsOverlayRender(); });
    overlay_.SetAuxiliaryInputCaptureCallback([this]() {
        return binder_.WantsQuickMenuCursor() || binder_.WantsInputCapture();
    });
    overlay_.SetInputCaptureChangedCallback([this](bool captured) { HandleOverlayInputCaptureChanged(captured); });
    overlay_.SetMenuToggleHotkeyConflictCallback([this](const std::vector<unsigned int>& keys, std::string& description) {
        return binder_.DescribeMainWindowHotkeyConflict(keys, description);
    });
    debuglog::WriteInfo("Overlay callbacks configured");
    overlay_.OnProcessAttach();
    debuglog::WriteInfo("Overlay attach requested");
}

void ModApp::Shutdown() {
    debuglog::WriteInfo("ModApp shutdown begin");
    ::ClipCursor(nullptr);
    ::ReleaseCapture();
    overlayLastUiHold_ = false;

    sampApi_.Refresh();
    if (sampApi_.sampModule() && sampApi_.isSupportedVersion()) {
        sampApi_.Set_CursorMode(kSampCursorModeNone, false);
    }
    overlayCursorMode_ = kSampCursorModeNone;
    overlayCursorEnabled_ = false;

    overlay_.Shutdown();
    debuglog::WriteInfo("Overlay shutdown done");
    incomingMessageRouter_.Shutdown();
    debuglog::WriteInfo("Incoming router shutdown done");
    binder_.Shutdown();
    debuglog::WriteInfo("Binder shutdown done");
    tags_.Shutdown();
    debuglog::WriteInfo("Tags shutdown done");
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
    debuglog::Shutdown();
}

void ModApp::HandleOverlayInputCaptureChanged(bool captured) {
    (void)captured;
    UpdateOverlayCursorMode();
}

void ModApp::UpdateOverlayCursorMode() {
    const bool wantsUi = overlay_.WantsUiCursor();
    HWND gameHw = overlay_.GetGameWindow();
    HWND fg = GetForegroundWindow();
    const bool appHasFocus = gameHw && fg && IsWindow(gameHw)
        && (fg == gameHw || IsChild(gameHw, fg) != FALSE);
    const uint64_t now = GetTickCount64();

    bool chatOrDialogActive = false;
    sampApi_.Refresh();
    if (sampApi_.sampModule() && sampApi_.isSupportedVersion()) {
        chatOrDialogActive = sampApi_.is_chat_opened() || sampApi_.isDialogActive();
    }

    const bool rmbHeld = (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const bool shouldHoldUi = appHasFocus && (wantsUi || chatOrDialogActive);

    static bool s_traceWantsUi = false;
    static bool s_traceFocus = false;
    static bool s_traceRmb = false;
    static bool s_traceChatDialog = false;
    static bool s_traceHold = false;
    static uint64_t s_lastCursorTraceMs = 0;
    const bool changedCore = wantsUi != s_traceWantsUi || appHasFocus != s_traceFocus
        || chatOrDialogActive != s_traceChatDialog || shouldHoldUi != s_traceHold;
    const bool changedRmbOnly = !changedCore && (rmbHeld != s_traceRmb);
    const bool allowRmbSpamSafeTrace = changedRmbOnly && (now - s_lastCursorTraceMs >= kCursorTraceIntervalMs);
    if (changedCore || allowRmbSpamSafeTrace) {
        s_traceWantsUi = wantsUi;
        s_traceFocus = appHasFocus;
        s_traceRmb = rmbHeld;
        s_traceChatDialog = chatOrDialogActive;
        s_traceHold = shouldHoldUi;
        s_lastCursorTraceMs = now;
        debuglog::WriteInfo(
            "[ui] cursor wantsUi=%d chatOpen=%d dialogOpen=%d chatOrDialog=%d fg=%d rmb=%d shouldHold=%d gameHw=%p fgHw=%p sampMode=%d sampEn=%d",
            wantsUi ? 1 : 0,
            (chatOrDialogActive && sampApi_.is_chat_opened()) ? 1 : 0,
            (chatOrDialogActive && sampApi_.isDialogActive()) ? 1 : 0,
            chatOrDialogActive ? 1 : 0,
            appHasFocus ? 1 : 0,
            rmbHeld ? 1 : 0,
            shouldHoldUi ? 1 : 0,
            gameHw,
            fg,
            overlayCursorMode_,
            overlayCursorEnabled_ ? 1 : 0);
    }

    if (overlayLastUiHold_ && !shouldHoldUi) {
        ::ReleaseCapture();
        debuglog::WriteInfo("[ui] ReleaseCapture due to UI-hold end");
    }
    overlayLastUiHold_ = shouldHoldUi;

    const int desiredMode = shouldHoldUi ? kSampCursorModeLockCam : kSampCursorModeNone;
    const bool desiredEnabled = shouldHoldUi;
    const bool desiredSameAsCache = overlayCursorMode_ == desiredMode && overlayCursorEnabled_ == desiredEnabled;
    const bool shouldReassert = desiredEnabled && (now - overlayCursorLastApplyMs_ >= kCursorReassertIntervalMs);

    if (desiredSameAsCache && !shouldReassert) {
        return;
    }

    if (!sampApi_.sampModule() || !sampApi_.isSupportedVersion()) {
        static uint64_t s_lastCursorUnavailableTraceMs = 0;
        if (wantsUi && now - s_lastCursorUnavailableTraceMs >= kCursorUnavailableTraceIntervalMs) {
            s_lastCursorUnavailableTraceMs = now;
            debuglog::WriteInfo(
                "[ui] cursor apply skipped: sampModule=%d supported=%d",
                sampApi_.sampModule() ? 1 : 0,
                sampApi_.isSupportedVersion() ? 1 : 0);
        }
        return;
    }

    if (!sampApi_.Set_CursorMode(desiredMode, desiredEnabled)) {
        debuglog::WriteError(
            "[ui] Set_CursorMode FAILED want mode=%d en=%d: %s",
            desiredMode,
            desiredEnabled ? 1 : 0,
            sampApi_.lastError().c_str());
        return;
    }

    debuglog::WriteInfo(
        "[ui] Set_CursorMode ok mode=%d en=%d (was %d / %d reassert=%d)",
        desiredMode,
        desiredEnabled ? 1 : 0,
        overlayCursorMode_,
        overlayCursorEnabled_ ? 1 : 0,
        shouldReassert ? 1 : 0);

    overlayCursorMode_ = desiredMode;
    overlayCursorEnabled_ = desiredEnabled;
    overlayCursorLastApplyMs_ = now;
}

void ModApp::Tick() {
    AppConfig::Instance().ProcessPendingWrites();
    incomingMessageRouter_.Tick();
    binder_.Tick();
    tags_.Tick();

    const std::uint64_t now = GetTickCount64();
    if (now >= nextSampRefreshAtMs_) {
        sampApi_.Refresh();
        sampHooks_.Refresh();
        sampRakHooks_.Refresh();
        nextSampRefreshAtMs_ = now + 1000;
    }

    UpdateOverlayCursorMode();
}

void ModApp::ApplyMainStyle(float scale) const {
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
    debuglog::WriteInfo("Shell state loaded (sidebar_collapsed=%d)", sidebarCollapsed_ ? 1 : 0);
}

void ModApp::QueueShellStateSave() const {
    const bool sidebarCollapsed = sidebarCollapsed_;
    debuglog::WriteInfo("Queue shell state save (sidebar_collapsed=%d)", sidebarCollapsed ? 1 : 0);
    AppConfig::Instance().QueueMutation([sidebarCollapsed](jsonutil::JsonObject& root) {
        jsonutil::JsonObject section;
        const auto existing = root.find(std::string(kShellSectionName));
        if (existing != root.end()) {
            if (const jsonutil::JsonObject* object = existing->second.TryObject()) {
                section = *object;
            }
        }

        section["sidebar_collapsed"] = sidebarCollapsed;
        root[std::string(kShellSectionName)] = jsonutil::JsonValue(std::move(section));
    });
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

void ModApp::DrawSmiHelperTab() const {
    const UiSettings& ui = UiSettings::Instance();
    ImGui::SeparatorText(ui.Text(UiText::TabSmiHelper));
    ImGui::TextWrapped("%s", ui.Text(UiText::SmiHelperIntro));
    ImGui::Spacing();

    DrawSectionCard(
        "smi_shell",
        ui.Text(UiText::SmiHelperShellTitle),
        ui.Text(UiText::SmiHelperShellDesc),
        ImVec4(0.75f, 0.90f, 1.0f, 1.0f));
}

void ModApp::DrawMiscTab() {
    tags_.DrawMiscTab();
}

void ModApp::DrawNotepadTab() const {
    const UiSettings& ui = UiSettings::Instance();
    ImGui::SeparatorText(ui.Text(UiText::TabNotepad));
    ImGui::TextWrapped("%s", ui.Text(UiText::NotepadIntro));
}

void ModApp::DrawSettingsTab() {
    UiSettings& ui = UiSettings::Instance();
    ImGui::SeparatorText(ui.Text(UiText::TabSettings));
    ImGui::TextWrapped("%s", ui.Text(UiText::SettingsIntro));
    ImGui::Spacing();

    const UiLanguage languages[] = { UiLanguage::Russian, UiLanguage::English };
    const char* languageLabels[] = {
        ui.LanguageDisplayName(UiLanguage::Russian),
        ui.LanguageDisplayName(UiLanguage::English),
    };
    int languageIndex = ui.Language() == UiLanguage::English ? 1 : 0;
    if (ImGui::Combo(ui.Text(UiText::SettingsLanguage), &languageIndex, languageLabels, IM_ARRAYSIZE(languageLabels))) {
        ui.SetLanguage(languages[languageIndex]);
        debuglog::WriteInfo("Settings changed: language=%s", languageIndex == 1 ? "en" : "ru");
    }

    const UiLogLevel logLevels[] = { UiLogLevel::Off, UiLogLevel::Error, UiLogLevel::Info };
    const char* logLevelLabels[] = {
        ui.Text(UiText::SettingsLogLevelOff),
        ui.Text(UiText::SettingsLogLevelError),
        ui.Text(UiText::SettingsLogLevelInfo),
    };
    int logLevelIndex = static_cast<int>(ui.LogLevel());
    if (ImGui::Combo(ui.Text(UiText::SettingsLogLevel), &logLevelIndex, logLevelLabels, IM_ARRAYSIZE(logLevelLabels))) {
        const UiLogLevel selected = logLevels[std::clamp(logLevelIndex, 0, 2)];
        ui.SetLogLevel(selected);
        debuglog::SetLevel(ToDebugLogLevel(selected));
        debuglog::WriteInfo("Settings changed: log_level=%s", ToUiLogLevelName(selected));
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(ui.Text(UiText::SettingsMainWindowHotkey));
    ImGui::Text("%s", ui.Format(UiText::HotkeyFormat, overlay_.MenuToggleHotkeyText().c_str()).c_str());
    if (ImGui::Button(ui.Text(UiText::ChangeHotkey))) {
        overlay_.BeginMenuToggleHotkeyCapture();
    }

    bool autoScale = ui.AutoScaleEnabled();
    if (ImGui::Checkbox(ui.Text(UiText::SettingsAutoScale), &autoScale)) {
        ui.SetAutoScaleEnabled(autoScale);
        debuglog::WriteInfo("Settings changed: auto_scale=%d", autoScale ? 1 : 0);
    }

    float scaleMultiplier = ui.ScaleMultiplier();
    if (ImGui::SliderFloat(ui.Text(UiText::SettingsScaleMultiplier), &scaleMultiplier, 0.75f, 2.0f, "%.2fx")) {
        ui.SetScaleMultiplier(scaleMultiplier);
        debuglog::WriteInfo("Settings changed: scale_multiplier=%.2f", scaleMultiplier);
    }

    ImGui::Text("%s: %.2fx", ui.Text(UiText::SettingsEffectiveScale), ui.CurrentScale());
    ImGui::TextWrapped("%s", ui.Text(UiText::SettingsScaleHint));

    if (ImGui::Button(ui.Text(UiText::SettingsResetDefaults))) {
        ui.ResetToDefaults();
        debuglog::SetLevel(ToDebugLogLevel(ui.LogLevel()));
        debuglog::WriteInfo("Settings reset to defaults");
        overlay_.CancelMenuToggleHotkeyCapture();
    }

    ImGui::Spacing();
    const_cast<BinderModule&>(binder_).DrawSettingsSection();

    const std::string configPath = AppConfig::Instance().ConfigPath().string();
    ImGui::TextWrapped("%s: %s", ui.Text(UiText::SettingsConfigPath), configPath.c_str());
    ImGui::Text("%s", ui.Format(UiText::GtaVersionFormat, plugin::GetGameVersionName()).c_str());
}

void ModApp::PrepareUiForImGuiNewFrame(IDirect3DDevice9* device) {
    ImGuiIO& io = ImGui::GetIO();
    const float uiScale = UiSettings::Instance().UpdateScale(io.DisplaySize);
    static float s_lastLoggedScale = 0.0f;
    static uint64_t s_lastScaleTraceMs = 0;
    const uint64_t now = GetTickCount64();
    if (std::abs(uiScale - s_lastLoggedScale) > 0.001f || now - s_lastScaleTraceMs >= kUiScaleTraceIntervalMs) {
        s_lastLoggedScale = uiScale;
        s_lastScaleTraceMs = now;
        debuglog::WriteInfo(
            "[ui] frame prep scale=%.3f display=(%.1f,%.1f)",
            uiScale,
            io.DisplaySize.x,
            io.DisplaySize.y);
    }
    io.FontGlobalScale = uiScale;
    ApplyMainStyle(uiScale);
    EnsureLogoTexture(device);
}

void ModApp::RenderUi(IDirect3DDevice9* device) {
    ImGuiIO& io = ImGui::GetIO();
    const float uiScale = io.FontGlobalScale;

    const bool showMainWindow = overlay_.IsMenuOpen();
    static bool s_lastShowMainWindow = false;
    if (showMainWindow != s_lastShowMainWindow) {
        s_lastShowMainWindow = showMainWindow;
        debuglog::WriteInfo("[ui] main window visibility -> %d", showMainWindow ? 1 : 0);
    }
    if (!showMainWindow) {
        binder_.DrawOverlay();
        AppConfig::Instance().ProcessPendingWrites();
        return;
    }

    if (io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f) {
        if (!mainWindowInitialized_) {
            const float margin = kWindowMargin * uiScale;
            mainWindowSize_.x = std::min(1100.0f * uiScale, io.DisplaySize.x - margin * 2.0f);
            mainWindowSize_.y = std::min(720.0f * uiScale, io.DisplaySize.y - margin * 2.0f);
            mainWindowPos_.x = std::max(margin, (io.DisplaySize.x - mainWindowSize_.x) * 0.5f);
            mainWindowPos_.y = std::max(margin, (io.DisplaySize.y - mainWindowSize_.y) * 0.5f);
            mainWindowInitialized_ = true;
            appliedUiScale_ = uiScale;
        } else {
            const float scaleDelta = uiScale - appliedUiScale_;
            if (scaleDelta > 0.001f || scaleDelta < -0.001f) {
                const float ratio = uiScale / std::max(appliedUiScale_, 0.001f);
                mainWindowPos_.x *= ratio;
                mainWindowPos_.y *= ratio;
                mainWindowSize_.x *= ratio;
                mainWindowSize_.y *= ratio;
                appliedUiScale_ = uiScale;
            }
        }

        ClampWindowRect(io.DisplaySize, mainWindowPos_, mainWindowSize_, uiScale);
    }

    ImGui::SetNextWindowPos(mainWindowPos_, ImGuiCond_Always);
    ImGui::SetNextWindowSize(mainWindowSize_, ImGuiCond_Always);

    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin("HelperByOrc##main_window", nullptr, windowFlags)) {
        mainWindowPos_ = ImGui::GetWindowPos();
        mainWindowSize_ = ImGui::GetWindowSize();

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
            mainWindowPos_.x += io.MouseDelta.x;
            mainWindowPos_.y += io.MouseDelta.y;
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
            switch (currentTab_) {
            case MainTab::Home:
                DrawHomeTab();
                break;
            case MainTab::Binder:
                DrawBinderTab();
                break;
            case MainTab::SmiHelper:
                DrawSmiHelperTab();
                break;
            case MainTab::Misc:
                DrawMiscTab();
                break;
            case MainTab::Notepad:
                DrawNotepadTab();
                break;
            case MainTab::Settings:
                DrawSettingsTab();
                break;
            }
        }
        ImGui::EndChild();
    }

    ImGui::End();
    overlay_.DrawMenuToggleHotkeyCapturePopup();
    binder_.DrawOverlay();
    AppConfig::Instance().ProcessPendingWrites();
}
