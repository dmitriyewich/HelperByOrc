#include "imgui_overlay.h"

#include "debug_log.h"
#include "font_awesome7_data.h"
#include "minhook_utils.h"
#include "ui_settings.h"

#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <cstdio>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

namespace {

constexpr char kDummyWindowClassName[] = "HelperByOrcDummyWindow";
constexpr float kOverlayFontSize = 18.0f;

bool MergeFontAwesomeIcons(ImGuiIO& io) {
    static constexpr ImWchar kIconRanges[] = {
        0xF044, 0xF044, // pen-to-square
        0xF04B, 0xF04B, // play
        0xF2ED, 0xF2ED, // trash-can
        0,
    };

    ImFontConfig iconConfig{};
    iconConfig.MergeMode = true;
    iconConfig.PixelSnapH = true;

    ImFont* icons = io.Fonts->AddFontFromMemoryCompressedBase85TTF(
        FontAwesome7Data::kSolidCompressedBase85,
        kOverlayFontSize,
        &iconConfig,
        kIconRanges);
    if (!icons) {
        debuglog::Write("Failed to merge Font Awesome 7 icon font");
        return false;
    }

    debuglog::Write("Merged Font Awesome 7 icon font");
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
            debuglog::Write("Loaded ImGui font: %s", path);
            MergeFontAwesomeIcons(io);
            return font;
        }
    }

    debuglog::Write("Failed to load a Cyrillic-capable ImGui font, using default font");
    ImFontConfig fallbackConfig{};
    fallbackConfig.SizePixels = kOverlayFontSize;
    ImFont* fallbackFont = io.Fonts->AddFontDefault(&fallbackConfig);
    MergeFontAwesomeIcons(io);
    return fallbackFont;
}

} // namespace

HRESULT __stdcall ImGuiOverlay::EndSceneDetour(IDirect3DDevice9* device) {
    if (self_ && !self_->shuttingDown_) {
        self_->InitializeImGuiIfNeeded(device);
        if (self_->IsPrimaryRenderTarget(device)) {
            self_->UpdateHotkeyState();
            if (self_->updateCallback_) {
                self_->updateCallback_();
            }
            self_->UpdateInputCaptureState();
            self_->RenderFrame(device);
        }
    }

    return originalEndScene_ ? originalEndScene_(device) : D3D_OK;
}

HRESULT __stdcall ImGuiOverlay::ResetDetour(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* presentationParameters) {
    if (self_ && self_->imguiInitialized_) {
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }

    if (!originalReset_) {
        return D3DERR_INVALIDCALL;
    }

    const HRESULT result = originalReset_(device, presentationParameters);
    if (SUCCEEDED(result) && self_ && self_->imguiInitialized_) {
        ImGui_ImplDX9_CreateDeviceObjects();
    }

    return result;
}

void ImGuiOverlay::SetRenderCallback(RenderCallback callback) {
    renderCallback_ = std::move(callback);
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

    debuglog::Write("ImGuiOverlay attached");

    initThread_ = CreateThread(nullptr, 0, &InitializeThread, this, 0, nullptr);
    if (!initThread_) {
        debuglog::Write("CreateThread failed: %lu", GetLastError());
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
            debuglog::Write("D3D9 hook attempt %d failed, retrying", attempt);
        }
        Sleep(1000);
    }

    debuglog::Write("D3D9 hooks were not installed");
    return 0;
}

bool ImGuiOverlay::CreateDummyDevice(IDirect3DDevice9** outDevice, HWND* outWindow) const {
    if (!outDevice || !outWindow) {
        return false;
    }

    *outDevice = nullptr;
    *outWindow = nullptr;

    WNDCLASSEXA windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DefWindowProcA;
    windowClass.hInstance = GetModuleHandleA(nullptr);
    windowClass.lpszClassName = kDummyWindowClassName;

    if (!RegisterClassExA(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        debuglog::Write("RegisterClassExA failed: %lu", GetLastError());
        return false;
    }

    HWND window = CreateWindowExA(0, kDummyWindowClassName, kDummyWindowClassName,
        WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!window) {
        debuglog::Write("CreateWindowExA failed: %lu", GetLastError());
        return false;
    }

    IDirect3D9* d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d9) {
        debuglog::Write("Direct3DCreate9 failed");
        DestroyWindow(window);
        return false;
    }

    D3DPRESENT_PARAMETERS parameters{};
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = window;
    parameters.BackBufferFormat = D3DFMT_UNKNOWN;

    HRESULT result = d3d9->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        window,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,
        &parameters,
        outDevice);

    d3d9->Release();

    if (FAILED(result) || !*outDevice) {
        debuglog::Write("IDirect3D9::CreateDevice failed: 0x%08lX", static_cast<unsigned long>(result));
        DestroyWindow(window);
        return false;
    }

    *outWindow = window;
    return true;
}

bool ImGuiOverlay::InstallGraphicsHooks() {
    if (hooksInstalled_) {
        return true;
    }

    IDirect3DDevice9* dummyDevice = nullptr;
    HWND dummyWindow = nullptr;
    if (!CreateDummyDevice(&dummyDevice, &dummyWindow)) {
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(dummyDevice);
    if (!vtable) {
        debuglog::Write("IDirect3DDevice9 vtable is null");
        dummyDevice->Release();
        DestroyWindow(dummyWindow);
        return false;
    }

    endSceneTarget_ = vtable[42];
    resetTarget_ = vtable[16];
    originalEndScene_ = nullptr;
    originalReset_ = nullptr;

    if (!minhook::CreateAndEnableHook(
            endSceneTarget_,
            reinterpret_cast<void*>(&EndSceneDetour),
            &originalEndScene_,
            "IDirect3DDevice9::EndScene")) {
        endSceneTarget_ = nullptr;
        resetTarget_ = nullptr;
        dummyDevice->Release();
        DestroyWindow(dummyWindow);
        return false;
    }

    if (!minhook::CreateAndEnableHook(
            resetTarget_,
            reinterpret_cast<void*>(&ResetDetour),
            &originalReset_,
            "IDirect3DDevice9::Reset")) {
        minhook::DisableAndRemoveHook(endSceneTarget_, "IDirect3DDevice9::EndScene");
        originalEndScene_ = nullptr;
        originalReset_ = nullptr;
        resetTarget_ = nullptr;
        dummyDevice->Release();
        DestroyWindow(dummyWindow);
        return false;
    }

    hooksInstalled_ = true;
    debuglog::Write("D3D9 hooks installed via MinHook. EndScene=%p Reset=%p", endSceneTarget_, resetTarget_);

    dummyDevice->Release();
    DestroyWindow(dummyWindow);
    return true;
}

HWND ImGuiOverlay::ResolveGameWindow(IDirect3DDevice9* device) const {
    if (gameWindow_ && IsWindow(gameWindow_)) {
        return gameWindow_;
    }

    if (device) {
        D3DDEVICE_CREATION_PARAMETERS parameters{};
        if (SUCCEEDED(device->GetCreationParameters(&parameters)) && IsWindow(parameters.hFocusWindow)) {
            return parameters.hFocusWindow;
        }
    }

    HWND foreground = GetForegroundWindow();
    if (foreground && IsWindow(foreground)) {
        return foreground;
    }

    return nullptr;
}

void ImGuiOverlay::InitializeImGuiIfNeeded(IDirect3DDevice9* device) {
    if (imguiInitialized_ || !device) {
        return;
    }

    gameWindow_ = ResolveGameWindow(device);
    if (!gameWindow_) {
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.MouseDrawCursor = false;
    io.FontDefault = LoadOverlayFont(io);

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(gameWindow_)) {
        debuglog::Write("ImGui_ImplWin32_Init failed");
        ImGui::DestroyContext();
        return;
    }

    if (!ImGui_ImplDX9_Init(device)) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        debuglog::Write("ImGui_ImplDX9_Init failed");
        return;
    }

    SetLastError(0);
    const LONG_PTR previousWndProc = SetWindowLongPtrA(
        gameWindow_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&OverlayWndProc));
    if (previousWndProc == 0 && GetLastError() != 0) {
        debuglog::Write("SetWindowLongPtrA failed: %lu", GetLastError());
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return;
    }

    originalWndProc_ = reinterpret_cast<WNDPROC>(previousWndProc);
    imguiInitialized_ = true;

    debuglog::Write("ImGui initialized. Game window=%p", gameWindow_);
}

void ImGuiOverlay::RestoreWindowProc() {
    if (!gameWindow_ || !originalWndProc_) {
        return;
    }

    SetWindowLongPtrA(gameWindow_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWndProc_));
    originalWndProc_ = nullptr;
}

void ImGuiOverlay::CleanupImGui() {
    if (!imguiInitialized_) {
        return;
    }

    RestoreWindowProc();
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    imguiInitialized_ = false;
    gameWindow_ = nullptr;
}

void ImGuiOverlay::UpdateHotkeyState() {
    const bool comboDown = IsMenuToggleComboDown();
    if (!menuToggleHotkeyCapture_.Active() && comboDown && !menuToggleWasDown_) {
        menuOpen_ = !menuOpen_;
        debuglog::Write("Menu toggled: %s", menuOpen_ ? "open" : "closed");
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

bool ImGuiOverlay::WantsInputRouting() const {
    return menuOpen_ || IsAuxiliaryUiVisible();
}

bool ImGuiOverlay::WantsInputCapture() const {
    const bool auxiliaryCapture = auxiliaryInputCaptureCallback_ ? auxiliaryInputCaptureCallback_() : false;
    return menuOpen_ || auxiliaryCapture;
}

void ImGuiOverlay::ApplyInputCaptureState(bool captured) {
    if (captured == inputCaptureActive_) {
        return;
    }

    inputCaptureActive_ = captured;
    if (inputCaptureChangedCallback_) {
        inputCaptureChangedCallback_(captured);
    }

    debuglog::Write("ImGui input capture: %s", captured ? "enabled" : "disabled");
}

void ImGuiOverlay::UpdateInputCaptureState() {
    ApplyInputCaptureState(WantsInputCapture());
}

void ImGuiOverlay::RenderFrame(IDirect3DDevice9* device) {
    const bool auxiliaryVisible = IsAuxiliaryUiVisible();
    if (!imguiInitialized_ || !device || (!menuOpen_ && !auxiliaryVisible)) {
        return;
    }

    IDirect3DStateBlock9* stateBlock = nullptr;
    IDirect3DVertexDeclaration9* vertexDeclaration = nullptr;
    IDirect3DVertexShader9* vertexShader = nullptr;

    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &stateBlock))) {
        stateBlock = nullptr;
    } else {
        stateBlock->Capture();
    }

    device->GetVertexDeclaration(&vertexDeclaration);
    device->GetVertexShader(&vertexShader);

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (renderCallback_) {
        renderCallback_(device);
    }

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

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
}

bool ImGuiOverlay::HandleTextInputMessage(UINT message, WPARAM wparam, LPARAM lparam) const {
    if (!imguiInitialized_ || !WantsInputCapture() || ImGui::GetCurrentContext() == nullptr) {
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

bool ImGuiOverlay::ShouldBlockGameInput(UINT message) const {
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
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
        return true;
    default:
        return false;
    }
}

LRESULT CALLBACK ImGuiOverlay::OverlayWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (self_ && self_->imguiInitialized_) {
        if (self_->HandleMenuToggleHotkeyCaptureMessage(message, wparam)) {
            return TRUE;
        }

        if (self_->windowMessageCallback_ && self_->windowMessageCallback_(message, wparam, lparam)) {
            return TRUE;
        }

        if (self_->WantsInputRouting()) {
            const bool wantsCapture = self_->WantsInputCapture();
            if (wantsCapture && self_->HandleTextInputMessage(message, wparam, lparam)) {
                return TRUE;
            }

            const bool imguiHandled = ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam) != 0;
            if (wantsCapture) {
                if (imguiHandled || self_->ShouldBlockGameInput(message)) {
                    return TRUE;
                }
            } else if (ImGui::GetCurrentContext() != nullptr) {
                const ImGuiIO& io = ImGui::GetIO();
                if (io.WantCaptureMouse && self_->IsMouseMessage(message)) {
                    return TRUE;
                }
            }
        }
    }

    if (self_ && self_->originalWndProc_) {
        return CallWindowProc(self_->originalWndProc_, hwnd, message, wparam, lparam);
    }

    return DefWindowProc(hwnd, message, wparam, lparam);
}

void ImGuiOverlay::Shutdown() {
    shuttingDown_ = true;
    menuOpen_ = false;
    CancelMenuToggleHotkeyCapture();
    ApplyInputCaptureState(false);

    CleanupImGui();

    minhook::DisableAndRemoveHook(endSceneTarget_, "IDirect3DDevice9::EndScene");
    minhook::DisableAndRemoveHook(resetTarget_, "IDirect3DDevice9::Reset");
    originalEndScene_ = nullptr;
    originalReset_ = nullptr;
    hooksInstalled_ = false;

    if (initThread_) {
        CloseHandle(initThread_);
        initThread_ = nullptr;
    }

    menuToggleWasDown_ = false;
    inputCaptureChangedCallback_ = nullptr;
    self_ = nullptr;
}
