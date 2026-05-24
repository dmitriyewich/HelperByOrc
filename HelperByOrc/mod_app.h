#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

#include <imgui.h>

#include "binder_module.h"
#include "hud_module.h"
#include "imgui_overlay.h"
#include "incoming_message_router.h"
#include "notification_manager.h"
#include "notepad_module.h"
#include "samp_api.h"
#include "samp_hooks.h"
#include "samp_rak_hooks.h"
#include "tags_module.h"

enum class MainTab : std::uint8_t {
    Home = 0,
    Binder,
    Hud,
    Misc,
    Notepad,
    Settings,
};

class ModApp {
public:
    ModApp();
    static ModApp& Instance();

    void OnProcessAttach(HMODULE module);
    void Shutdown();

private:
    struct MenuAnimationState {
        float alpha = 0.0f;
        float shift = 0.0f;
    };

    void HandleOverlayInputCaptureChanged(bool captured);
    void UpdateOverlayCursorMode();
    static DWORD WINAPI DeferredOverlayThreadProc(LPVOID param);
    void StartDeferredOverlayThread();
    void StopDeferredOverlayThread();
    void RequestOverlayAttachOnce(const char* reason);
    bool RefreshSampGate();
    void Tick();
    void PrepareUiForImGuiNewFrame(IDirect3DDevice9* device);
    void RenderUi(IDirect3DDevice9* device);
    void ApplyMainStyle(float scale) const;
    void LoadShellState();
    void QueueShellStateSave() const;
    void ReloadConfigAfterProfileChange();
    void SetSidebarCollapsed(bool collapsed);
    void EnsureLogoTexture(IDirect3DDevice9* device);
    void ReleaseUiResources();
    MainTab DrawAnimatedMenu(float width);
    void DrawSectionCard(const char* id, const char* title, const char* description, const ImVec4& accent) const;
    void DrawHomeTab() const;
    void DrawBinderTab() const;
    void DrawHudTab(IDirect3DDevice9* device);
    void DrawMiscTab();
    void DrawGameFixesCard();
    void DrawNotepadTab(IDirect3DDevice9* device);
    void DrawSettingsTab();
    void DrawSettingsSummaryBar();
    void DrawSettingsGeneralSection();
    void DrawSettingsHotkeysSection();
    void DrawSettingsNotificationsSection();
    void DrawSettingsProfilesSection();
    void DrawSettingsDiagnosticsSection();

    HMODULE module_ = nullptr;
    ImGuiOverlay overlay_;
    MainTab currentTab_ = MainTab::Home;
    std::array<MenuAnimationState, 6> menuAnimations_{};
    bool sidebarCollapsed_ = false;
    bool mainWindowInitialized_ = false;
    std::string profileNameBuffer_{};
    std::string profileNameBufferProfileId_{};
    std::string profileUiError_{};
    std::string profileDeleteTargetId_{};
    bool profileDeletePopupPending_ = false;
    ImVec2 mainWindowPos_{ 60.0f, 60.0f };
    ImVec2 mainWindowSize_{ 1100.0f, 720.0f };
    float appliedUiScale_ = 1.0f;
    bool logoLoadAttempted_ = false;
    IDirect3DTexture9* logoTexture_ = nullptr;
    std::uint32_t logoWidth_ = 0;
    std::uint32_t logoHeight_ = 0;
    std::uint64_t nextSampRefreshAtMs_ = 0;
    std::uint64_t nextRuntimeModuleSnapshotAtMs_ = 0;
    std::uint64_t sampNotReadySinceMs_ = 0;
    std::uint64_t nextSampStuckTraceAtMs_ = 0;
    int overlayCursorMode_ = -1;
    bool overlayCursorEnabled_ = false;
    bool overlayLastUiHold_ = false;
    std::uint64_t overlayCursorLastApplyMs_ = 0;
    bool sampUiPipelineReady_ = false;
    std::uint64_t sampUiPipelineLastProbeMs_ = 0;
    HANDLE deferredOverlayThread_ = nullptr;
    std::atomic_bool deferredOverlayThreadStop_{ false };
    std::atomic_bool overlayAttachRequested_{ false };
    bool minHookInitialized_ = false;
    SampApi sampApi_{};
    SampHooks sampHooks_{};
    SampRakHooks sampRakHooks_{};
    IncomingMessageRouter incomingMessageRouter_{};
    NotificationManager notifications_{};
    BinderModule binder_{};
    NotepadModule notepad_{};
    HudModule hud_{};
    TagsModule tags_{};
};
