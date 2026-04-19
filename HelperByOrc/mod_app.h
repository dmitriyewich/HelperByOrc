#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstdint>

#include <imgui.h>

#include "imgui_overlay.h"
#include "binder_module.h"
#include "incoming_message_router.h"
#include "samp_api.h"
#include "samp_hooks.h"
#include "samp_rak_hooks.h"
#include "tags_module.h"

enum class MainTab : std::uint8_t {
    Home = 0,
    Binder,
    SmiHelper,
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
    void Tick();
    void RenderUi(IDirect3DDevice9* device);
    void ApplyMainStyle(float scale) const;
    void LoadShellState();
    void QueueShellStateSave() const;
    void SetSidebarCollapsed(bool collapsed);
    void EnsureLogoTexture(IDirect3DDevice9* device);
    void ReleaseUiResources();
    MainTab DrawAnimatedMenu(float width);
    void DrawSectionCard(const char* id, const char* title, const char* description, const ImVec4& accent) const;
    void DrawHomeTab() const;
    void DrawBinderTab() const;
    void DrawSmiHelperTab() const;
    void DrawMiscTab();
    void DrawNotepadTab() const;
    void DrawSettingsTab();

    HMODULE module_ = nullptr;
    ImGuiOverlay overlay_;
    MainTab currentTab_ = MainTab::Home;
    std::array<MenuAnimationState, 6> menuAnimations_{};
    bool sidebarCollapsed_ = false;
    bool mainWindowInitialized_ = false;
    ImVec2 mainWindowPos_{ 60.0f, 60.0f };
    ImVec2 mainWindowSize_{ 1100.0f, 720.0f };
    float appliedUiScale_ = 1.0f;
    bool logoLoadAttempted_ = false;
    IDirect3DTexture9* logoTexture_ = nullptr;
    std::uint32_t logoWidth_ = 0;
    std::uint32_t logoHeight_ = 0;
    std::uint64_t nextSampRefreshAtMs_ = 0;
    int overlayCursorMode_ = -1;
    bool overlayCursorEnabled_ = false;
    bool overlayLastUiHold_ = false;
    bool minHookInitialized_ = false;
    SampApi sampApi_{};
    SampHooks sampHooks_{};
    SampRakHooks sampRakHooks_{};
    IncomingMessageRouter incomingMessageRouter_{};
    BinderModule binder_{};
    TagsModule tags_{};
};
