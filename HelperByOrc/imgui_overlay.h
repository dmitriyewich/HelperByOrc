#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d9.h>

#include <functional>
#include <string>
#include <vector>

#include "hotkey_utils.h"

class ImGuiOverlay {
public:
    using RenderCallback = std::function<void(IDirect3DDevice9*)>;
    /// Вызывается после `ImGui_ImplWin32_NewFrame`, до `ImGui::NewFrame` (масштаб IO, стили и т.п.).
    using PrepareFrameCallback = std::function<void(IDirect3DDevice9*)>;
    using UpdateCallback = std::function<void()>;
    using WindowMessageCallback = std::function<bool(UINT, WPARAM, LPARAM)>;
    using VisibilityCallback = std::function<bool()>;
    using GateCallback = std::function<bool()>;
    using InputCaptureChangedCallback = std::function<void(bool)>;
    using HotkeyConflictCallback = std::function<bool(const std::vector<unsigned int>& keys, std::string& description)>;

    void SetRenderCallback(RenderCallback callback);
    void SetPrepareFrameCallback(PrepareFrameCallback callback);
    void SetUpdateCallback(UpdateCallback callback);
    void SetWindowMessageCallback(WindowMessageCallback callback);
    void SetAuxiliaryUiVisibleCallback(VisibilityCallback callback);
    void SetAuxiliaryInputCaptureCallback(VisibilityCallback callback);
    void SetAuxiliaryInputRoutingCallback(VisibilityCallback callback);
    void SetInputPipelineGateCallback(GateCallback callback);
    void SetInputCaptureChangedCallback(InputCaptureChangedCallback callback);
    void SetMenuToggleHotkeyConflictCallback(HotkeyConflictCallback callback);
    void SetMenuOpen(bool open);
    /// HWND, передаваемый в `ImGui_ImplWin32_Init` (окно GTA), либо `nullptr` до инициализации.
    HWND GetGameWindow() const;
    /// Окно GTA на переднем плане (или дочернее с фокусом), как для `Set_CursorMode`. До появления `gameWindow_` — `true`.
    bool IsGameWindowForeground() const;
    bool IsMenuOpen() const;
    bool IsTextInputActive() const;
    bool WantsUiCursor() const;
    bool WantsInputRouting() const;
    void SetInputRoutingAllowed(bool allowed);
    void SetInputDecision(bool routingAllowed, bool drawHelperCursor);
    std::string MenuToggleHotkeyText() const;
    void BeginMenuToggleHotkeyCapture();
    bool IsMenuToggleHotkeyCaptureActive() const;
    void CancelMenuToggleHotkeyCapture();
    void DrawMenuToggleHotkeyCapturePopup();
    void OnProcessAttach();
    void Shutdown();

private:
    using EndSceneFn = HRESULT(__stdcall*)(IDirect3DDevice9*);
    using PresentFn = HRESULT(__stdcall*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
    using ResetFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

    static DWORD WINAPI InitializeThread(LPVOID param);
    static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    static HRESULT __stdcall EndSceneDetour(IDirect3DDevice9* device);
    static HRESULT __stdcall PresentDetour(IDirect3DDevice9* device, const RECT* sourceRect, const RECT* destRect, HWND overrideWindow, const RGNDATA* dirtyRegion);
    static HRESULT __stdcall ResetDetour(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* presentationParameters);

    bool InstallGraphicsHooks();
    bool CreateDummyDevice(IDirect3DDevice9** outDevice, HWND* outWindow, bool* outRegisteredWindowClass) const;
    void InitializeImGuiIfNeeded(IDirect3DDevice9* device);
    void CleanupImGui();
    void RestoreWindowProc();
    void UpdateHotkeyState();
    bool HandleMenuToggleHotkeyCaptureMessage(UINT message, WPARAM wparam);
    void UpdateInputCaptureState();
    void ApplyInputCaptureState(bool captured);
    void AdvanceImGuiFrameWithoutUi(IDirect3DDevice9* device);
    void TraceUiRenderAndInputSnapshot(const char* frameTag);
    void RenderFrame(IDirect3DDevice9* device);
    void SyncOsMouseToImGui() const;
    bool HandleTextInputMessage(UINT message, WPARAM wparam, LPARAM lparam) const;
    bool IsMouseMessage(UINT message) const;
    HWND ResolveGameWindow(IDirect3DDevice9* device) const;
    bool IsPrimaryRenderTarget(IDirect3DDevice9* device) const;
    bool IsAuxiliaryUiVisible() const;
    bool IsMenuToggleComboDown() const;
    bool CanApplyMenuToggleHotkeyCapture(const std::vector<unsigned int>& keys, std::string* description = nullptr) const;
    bool ApplyMenuToggleHotkeyCapture(const std::vector<unsigned int>& keys);
    bool CanRouteInput() const;
    bool WantsAuxiliaryUiCursor() const;
    bool WantsAuxiliaryInputRouting() const;
    bool WantsTextInputCapture() const;
    bool IsInputPipelineEnabled() const;
    bool IsKeyboardMessage(UINT message) const;
    bool EnsureWndProcHookInstalled();

    static inline ImGuiOverlay* self_ = nullptr;

    HANDLE initThread_ = nullptr;
    bool shuttingDown_ = false;
    bool hooksInstalled_ = false;
    bool imguiInitialized_ = false;
    bool menuOpen_ = false;
    bool menuToggleWasDown_ = false;
    HWND gameWindow_ = nullptr;
    WNDPROC originalWndProc_ = nullptr;
    RenderCallback renderCallback_;
    PrepareFrameCallback prepareFrameCallback_;
    UpdateCallback updateCallback_;
    WindowMessageCallback windowMessageCallback_;
    VisibilityCallback auxiliaryUiVisibleCallback_;
    VisibilityCallback auxiliaryInputCaptureCallback_;
    VisibilityCallback auxiliaryInputRoutingCallback_;
    GateCallback inputPipelineGateCallback_;
    InputCaptureChangedCallback inputCaptureChangedCallback_;
    HotkeyConflictCallback menuToggleHotkeyConflictCallback_;
    bool inputCaptureActive_ = false;
    bool inputRoutingAllowed_ = false;
    bool drawHelperCursor_ = false;
    hotkeys::Capture menuToggleHotkeyCapture_{};
    hotkeys::CapturePopupState menuToggleHotkeyCapturePopup_{};

    bool traceLastMenuOpen_ = false;
    bool traceLastAuxVisible_ = false;
    bool traceLastIdleFrame_ = true;
    bool lastWantsInputRouting_ = false;
    bool lastInputResetMenuOpen_ = false;
    bool lastInputResetAuxVisible_ = false;
    uint64_t traceLastUiDiagTick_ = 0;

    void* endSceneTarget_ = nullptr;
    void* presentTarget_ = nullptr;
    void* resetTarget_ = nullptr;
    static inline EndSceneFn originalEndScene_ = nullptr;
    static inline PresentFn originalPresent_ = nullptr;
    static inline ResetFn originalReset_ = nullptr;
};
