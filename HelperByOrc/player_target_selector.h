#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "hotkey_utils.h"
#include "ped_outline_renderer.h"
#include "feature_flags.h"

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
    struct PendingClick {
        void Reset() noexcept {
            queued = false;
            cursorX = 0;
            cursorY = 0;
        }

        bool queued = false;
        int cursorX = 0;
        int cursorY = 0;
    };

    struct PendingSelection {
        void Reset() noexcept {
            active = false;
            ped = nullptr;
            playerId = -1;
            cursorX = 0;
            cursorY = 0;
        }

        bool active = false;
        CPed* ped = nullptr;
        int playerId = -1;
        int cursorX = 0;
        int cursorY = 0;
    };

    bool IsHotkeyDown() const;
    bool IsRuntimeInputBlocked() const;
    bool CanApplyHotkey(const std::vector<unsigned int>& keys, std::string* description = nullptr) const;
    bool ApplyHotkey(const std::vector<unsigned int>& keys);
    bool HandleHotkeyCaptureMessage(UINT message, WPARAM wparam);
    bool ResolveCursorCandidate(
        float cursorX,
        float cursorY,
        CPed*& ped,
        int& playerId,
        const std::vector<CPed*>* excludedPeds = nullptr) const;
#if HELPERBYORC_TARGET_DIAGNOSTICS
    bool ResolveDiagnosticCandidate(float cursorX, float cursorY, CPed*& ped, int& playerId,
        const std::vector<CPed*>* excludedPeds = nullptr) const;
    bool ResolveRaycastCandidate(float cursorX, float cursorY, CPed*& ped, int& playerId,
        const std::vector<CPed*>* excludedPeds) const;
    bool diagnosticIdentityInvalidated_ = false;
#endif
    void UpdateCandidate(CPed* ped, int playerId);
    bool ValidatePlayer(CPed* ped, int playerId) const;
    bool ValidateSelectionCandidate(CPed* ped, int playerId, int cursorX, int cursorY) const;
    bool BeginPendingSelection(int cursorX, int cursorY);
    void ProcessPendingSelection(PedOutlineRenderer::CursorHitStatus cursorHit);
    void SelectPendingPlayer();
    void Activate();
    void Cancel(const char* reason);
    void DrawFullscreenHitSurface() const;
    void DrawInstructionOverlay() const;

    SampApi* sampApi_ = nullptr;
    TagsModule* tagsModule_ = nullptr;
    NotificationManager* notificationManager_ = nullptr;
    HotkeyConflictCallback hotkeyConflictCallback_{};
    PedOutlineRenderer outline_{};
    hotkeys::Capture hotkeyCapture_{};
    hotkeys::CapturePopupState hotkeyCapturePopup_{};
    PendingClick pendingClick_{};
    PendingSelection pendingSelection_{};
    CPed* candidatePed_ = nullptr;
    int candidatePlayerId_ = -1;
    std::string candidatePlayerName_{};
    std::vector<CPed*> clickRejectedCandidates_{};
    std::uint64_t pendingClickStartedAtMs_ = 0;
    bool active_ = false;
    bool hotkeyWasDown_ = false;
    bool hadForeground_ = true;
};
