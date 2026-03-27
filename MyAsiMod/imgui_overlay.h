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
    using UpdateCallback = std::function<void()>;
    using WindowMessageCallback = std::function<bool(UINT, WPARAM, LPARAM)>;
    using VisibilityCallback = std::function<bool()>;
    using InputCaptureChangedCallback = std::function<void(bool)>;

    void SetRenderCallback(RenderCallback callback);
    void SetUpdateCallback(UpdateCallback callback);
    void SetWindowMessageCallback(WindowMessageCallback callback);
    void SetAuxiliaryUiVisibleCallback(VisibilityCallback callback);
    void SetAuxiliaryInputCaptureCallback(VisibilityCallback callback);
    void SetInputCaptureChangedCallback(InputCaptureChangedCallback callback);
    void SetMenuOpen(bool open);
    bool IsMenuOpen() const;
    std::string MenuToggleHotkeyText() const;
    void BeginMenuToggleHotkeyCapture();
    bool IsMenuToggleHotkeyCaptureActive() const;
    std::string MenuToggleHotkeyCaptureText() const;
    const std::vector<unsigned int>& MenuToggleHotkeyCaptureDraft() const;
    bool CanSaveMenuToggleHotkeyCapture() const;
    void SaveMenuToggleHotkeyCapture();
    void CancelMenuToggleHotkeyCapture();
    void OnProcessAttach();
    void Shutdown();

private:
    using EndSceneFn = HRESULT(__stdcall*)(IDirect3DDevice9*);
    using ResetFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

    static DWORD WINAPI InitializeThread(LPVOID param);
    static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    static HRESULT __stdcall EndSceneDetour(IDirect3DDevice9* device);
    static HRESULT __stdcall ResetDetour(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* presentationParameters);

    bool InstallGraphicsHooks();
    bool CreateDummyDevice(IDirect3DDevice9** outDevice, HWND* outWindow) const;
    void InitializeImGuiIfNeeded(IDirect3DDevice9* device);
    void CleanupImGui();
    void RestoreWindowProc();
    void UpdateHotkeyState();
    bool HandleMenuToggleHotkeyCaptureMessage(UINT message, WPARAM wparam);
    void UpdateInputCaptureState();
    void ApplyInputCaptureState(bool captured);
    void RenderFrame(IDirect3DDevice9* device);
    bool HandleTextInputMessage(UINT message, WPARAM wparam, LPARAM lparam) const;
    bool IsMouseMessage(UINT message) const;
    bool ShouldBlockGameInput(UINT message) const;
    HWND ResolveGameWindow(IDirect3DDevice9* device) const;
    bool IsPrimaryRenderTarget(IDirect3DDevice9* device) const;
    bool IsAuxiliaryUiVisible() const;
    bool IsMenuToggleComboDown() const;
    bool WantsInputRouting() const;
    bool WantsInputCapture() const;

    static inline ImGuiOverlay* self_ = nullptr;

    HANDLE initThread_ = nullptr;
    bool shuttingDown_ = false;
    bool hooksInstalled_ = false;
    bool imguiInitialized_ = false;
    bool menuOpen_ = false;
    bool menuToggleWasDown_ = false;
    bool menuToggleHotkeyCaptureActive_ = false;
    HWND gameWindow_ = nullptr;
    WNDPROC originalWndProc_ = nullptr;
    RenderCallback renderCallback_;
    UpdateCallback updateCallback_;
    WindowMessageCallback windowMessageCallback_;
    VisibilityCallback auxiliaryUiVisibleCallback_;
    VisibilityCallback auxiliaryInputCaptureCallback_;
    InputCaptureChangedCallback inputCaptureChangedCallback_;
    bool inputCaptureActive_ = false;
    std::vector<unsigned int> menuToggleHotkeyCaptureDraft_{};
    hotkeys::KeyTracker menuToggleHotkeyCaptureTracker_{};

    void* endSceneTarget_ = nullptr;
    void* resetTarget_ = nullptr;
    static inline EndSceneFn originalEndScene_ = nullptr;
    static inline ResetFn originalReset_ = nullptr;
};
