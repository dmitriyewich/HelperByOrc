#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "hotkey_utils.h"
#include "ped_outline_renderer.h"

#include <functional>
#include <string>
#include <vector>

class CPed;
class NotificationManager;
class SampApi;
class TagsModule;

class PlayerTargetSelector {
public:
    using HotkeyConflictCallback = std::function<bool(const std::vector<unsigned int>&, std::string&)>;

    void SetSampApi(SampApi* sampApi);
    void SetTagsModule(TagsModule* tagsModule);
    void SetNotificationManager(NotificationManager* notificationManager);
    void SetHotkeyConflictCallback(HotkeyConflictCallback callback);

    void Tick(bool sampReady, bool gameForeground, bool helperUiBlocked);
    bool OnWindowMessage(UINT message, WPARAM wparam, LPARAM lparam);
    void DrawOverlay(IDirect3DDevice9* device);
    void OnDeviceLost();
    void OnDeviceReset();
    void Shutdown();
    void OnProfileChanged();

    bool IsActive() const;
    bool WantsOverlayRender() const;
    bool WantsInputCapture() const;
    std::string HotkeyText() const;
    void BeginHotkeyCapture();
    void CancelHotkeyCapture();
    void DrawHotkeyCapturePopup();

private:
    bool IsHotkeyDown() const;
    bool IsRuntimeInputBlocked() const;
    bool CanApplyHotkey(const std::vector<unsigned int>& keys, std::string* description = nullptr) const;
    bool ApplyHotkey(const std::vector<unsigned int>& keys);
    bool HandleHotkeyCaptureMessage(UINT message, WPARAM wparam);
    bool ResolveCursorCandidate(CPed*& ped, int& playerId) const;
    void ResetRejectedCandidatesForCursor(float cursorX, float cursorY);
    bool ValidateHoveredPlayer() const;
    void Activate();
    void Cancel(const char* reason);
    void SelectHoveredPlayer();
    void DrawFullscreenHitSurface() const;
    void DrawInstructionOverlay() const;

    SampApi* sampApi_ = nullptr;
    TagsModule* tagsModule_ = nullptr;
    NotificationManager* notificationManager_ = nullptr;
    HotkeyConflictCallback hotkeyConflictCallback_{};
    PedOutlineRenderer outline_{};
    hotkeys::Capture hotkeyCapture_{};
    hotkeys::CapturePopupState hotkeyCapturePopup_{};
    CPed* hoveredPed_ = nullptr;
    int hoveredPlayerId_ = -1;
    int hoveredCursorX_ = -1;
    int hoveredCursorY_ = -1;
    std::string hoveredPlayerName_{};
    std::vector<CPed*> rejectedCandidates_{};
    int rejectedCursorX_ = -1;
    int rejectedCursorY_ = -1;
    bool active_ = false;
    bool hotkeyWasDown_ = false;
    bool hadForeground_ = true;
};
