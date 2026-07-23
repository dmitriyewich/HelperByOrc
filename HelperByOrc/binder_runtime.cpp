#include "binder_module_impl.h"

namespace {

void EnsureTextTriggerRuntimeCache(TextTrigger& trigger) {
    if (trigger.runtimeCacheReady
        && trigger.runtimeCacheText == trigger.text
        && trigger.runtimeCachePattern == trigger.pattern) {
        return;
    }

    trigger.runtimeCacheText = trigger.text;
    trigger.runtimeCachePattern = trigger.pattern;
    trigger.runtimeCacheReady = true;
    trigger.runtimePatternInvalid = false;
    trigger.runtimeNormalizedText = NormalizeTriggerText(trigger.text);
    trigger.runtimePattern.reset();

    if (!trigger.pattern || trigger.runtimeNormalizedText.empty()) {
        return;
    }

    text_pattern::CompileResult compiled = text_pattern::Compile(trigger.text, false);
    if (compiled.program) {
        trigger.runtimePattern = std::shared_ptr<text_pattern::Program>(std::move(compiled.program));
    } else {
        trigger.runtimePatternInvalid = true;
    }
}

} // namespace

void BinderModule::Impl::EnsureInitialized() {
    if (configLoaded) {
        ConnectHooks();
        EnsureRootFolder();
        return;
    }

    LoadConfig();
    configLoaded = true;
    EnsureRootFolder();
    RefreshNumbers();
    ConnectHooks();
}

void BinderModule::Impl::OnProcessAttach(HMODULE moduleHandle) {
    (void)moduleHandle;
}

void BinderModule::Impl::SetSampApi(SampApi* api) {
    sampApi = api;
    ConnectHooks();
}

void BinderModule::Impl::SetSampHooks(SampHooks* hooks) {
    sampHooks = hooks;
    ConnectHooks();
}

void BinderModule::Impl::SetSampRakHooks(SampRakHooks* hooks) {
    sampRakHooks = hooks;
    ConnectHooks();
}

void BinderModule::Impl::SetIncomingMessageRouter(IncomingMessageRouter* router) {
    incomingMessageRouter = router;
    ConnectHooks();
}

void BinderModule::Impl::SetNotificationManager(NotificationManager* manager) {
    notificationManager = manager;
}

void BinderModule::Impl::SetTagsModule(TagsModule* module) {
    tagsModule = module;
}

void BinderModule::Impl::ConnectHooks() {
    if (!incomingMessageRouterBound && incomingMessageRouter) {
        incomingMessageRouter->AddOnMessageHandler([this](const IncomingMessageEvent& message) {
            OnIncomingMessage(message);
        });
        incomingMessageRouterBound = true;
    }

    if (!rakHooksBound && sampRakHooks) {
        sampRakHooks->AddOnSendCommandHandler([this](std::string& text, const SampCallContext&) {
            const bool handled = OnOutgoingCommand(ToUtf8ForDisplay(text));
            return !handled;
        });
        sampRakHooks->AddOnSendChatHandler([this](std::string& text, const SampCallContext&) {
            const bool handled = OnOutgoingChat(ToUtf8ForDisplay(text));
            return !handled;
        });
        rakHooksBound = true;
    }
}

int BinderModule::Impl::FindHotkeyIndexByOrderId(std::string_view orderId) const {
    if (orderId.empty()) {
        return -1;
    }
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        if (hotkeys[i].orderId == orderId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void BinderModule::Impl::Notify(
    NotificationGroup group,
    NotificationSeverity severity,
    std::string_view text,
    double durationMs) {
    if (!notificationManager || text.empty()) {
        return;
    }

    notificationManager->Notify(group, severity, text, durationMs);
}

void BinderModule::Impl::ShowUserPopup(std::string_view text, double durationMs) {
    if (!notificationManager || text.empty()) {
        return;
    }

    notificationManager->ShowUserPopup(text, NotificationSeverity::Success, durationMs);
}

ConditionRuntimeContext BinderModule::Impl::MakeConditionContext(bool quickMenuContext) const {
    ConditionRuntimeContext context{};
    context.helperUiActive = quickMenuContext ? false : helperUiActive_;
    context.helperUiCursorActive = quickMenuContext || helperUiActive_;
    context.gameWindowForeground = gameInputForeground_;
    if (quickMenuContext) {
        context.sampCursorActiveOverride = quickMenuSampCursorActiveAtOpen;
        context.windowsCursorActiveOverride = quickMenuWindowsCursorActiveAtOpen;
    }
    return context;
}

bool BinderModule::Impl::IsSilentActivationSource(std::string_view source) const {
    return source == "incoming_server"
        || source == "outgoing_chat"
        || source == "outgoing_command";
}

bool BinderModule::Impl::IsManualActivationSource(std::string_view source) const {
    return source == "manual";
}

bool BinderModule::Impl::CanToggleRunningHotkeyActivation(std::string_view source) const {
    return source != "incoming_server" && source != "bind_tag";
}

bool BinderModule::Impl::ShouldNotifyRunningHotkeyToggle(std::string_view source) const {
    return source == "hotkey"
        || source == "manual"
        || source == "quick_menu"
        || source == "command"
        || source == "outgoing_chat"
        || source == "outgoing_command";
}

bool BinderModule::Impl::ShouldBlockHelperCursorActivation(const HotkeyEntry& hotkey, std::string_view source) const {
    return helperUiActive_ && !IsManualActivationSource(source) && HasCursorCondition(hotkey.conditions);
}

bool BinderModule::Impl::ConditionsBlockHotkeyStart(
    const HotkeyEntry& hotkey,
    std::string_view source,
    std::string* message) const {
    if (ShouldBlockHelperCursorActivation(hotkey, source)) {
        if (message) {
            *message = UiSettings::Instance().Text(UiText::ConditionHelperCursorBlocked);
        }
        return true;
    }

    const ConditionRuntimeContext context = MakeConditionContext(source == "quick_menu");
    const ConditionCheckMode checkMode = IsManualActivationSource(source)
        ? ConditionCheckMode::IgnoreCursorConditions
        : ConditionCheckMode::Normal;
    return ConditionsBlocked(hotkey.conditions, hotkey.conditionsCombine, sampApi, &context, message, checkMode);
}

void BinderModule::Impl::ResetInputState() {
    keyTracker.Reset();
    pressedKeys.clear();
    asyncKeysDown.fill(false);
    capture.Stop();
    hotkeys::ResetCapturePopupState(capturePopupState);
    captureTarget = CaptureTarget::None;
    captureHotkeyIndex = -1;
    capturePopupInEditor = false;
    quickMenuOpen = false;
    ClearQuickMenuConditionSnapshot();
    quickMenuToggleLatch = false;
    quickMenuReopenBlocked = false;
    ResetQuickMenuVisualState();

    if (inputDialog) {
        if (inputDialog->hotkeyIndex >= 0 && inputDialog->hotkeyIndex < static_cast<int>(hotkeys.size())) {
            hotkeys[inputDialog->hotkeyIndex].awaitingInput = false;
        }
        inputDialog.reset();
    }
}

void BinderModule::Impl::SyncPressedKeysWithAsyncState() {
    auto isKeyDown = [](UINT key) {
        switch (key) {
        case VK_CONTROL:
            return (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0 || (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0;
        case VK_SHIFT:
            return (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0 || (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
        case VK_MENU:
            return (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0 || (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
        default:
            return (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0;
        }
    };

    bool changed = false;
    for (UINT key = 1; key <= 0xFF; ++key) {
        if (::hotkeys::NormalizeKey(key) != key || !::hotkeys::IsHotkeyKey(key)) {
            continue;
        }

        const bool down = isKeyDown(key);
        bool& wasDown = asyncKeysDown[static_cast<std::size_t>(key)];
        if (down == wasDown) {
            continue;
        }

        wasDown = down;
        if (down) {
            keyTracker.KeyDown(key);
        } else {
            keyTracker.KeyUp(key);
        }
        changed = true;
    }

    if (changed) {
        pressedKeys = keyTracker.Ordered();
    }
}

void BinderModule::Impl::Tick() {
    EnsureInitialized();
    FlushPendingConfigSave(false);
    PruneOutgoingGuards();
    PruneIncomingChatEchoGuards();
    ExpireTextConfirmations();

    if (prevFrameGameInputForeground_ && !gameInputForeground_) {
        ResetInputState();
    }
    if (!gameInputForeground_) {
        ProcessRunningBinds();
        prevFrameGameInputForeground_ = gameInputForeground_;
        return;
    }

    SyncPressedKeysWithAsyncState();
    const bool quickWasOpen = quickMenuOpen;
    const bool quickWasBlocked = quickMenuReopenBlocked;
    UpdateQuickMenuState();
    if (!quickWasOpen && quickMenuOpen) {
        CaptureQuickMenuConditionSnapshot();
    } else if (!quickMenuOpen) {
        ClearQuickMenuConditionSnapshot();
    }
    if (quickMenuOpen != quickWasOpen || quickMenuReopenBlocked != quickWasBlocked) {
        debuglog::WriteInfo(
            "[ui] quickmenu open %d->%d blocked %d->%d activation=%s comboHeld=%d overlayRender=%d",
            quickWasOpen ? 1 : 0,
            quickMenuOpen ? 1 : 0,
            quickWasBlocked ? 1 : 0,
            quickMenuReopenBlocked ? 1 : 0,
            quickMenuActivationMode == QuickMenuActivationMode::Toggle ? "toggle" : "hold",
            IsQuickMenuComboPressed() ? 1 : 0,
            WantsOverlayRender() ? 1 : 0);
    }
    if (!quickWasOpen && quickMenuOpen) {
        quickMenuFocusPending = true;
        quickMenuFocusReassertFrames = kQuickMenuFocusReassertFrames;
        CenterQuickMenuCursorOnGameWindow();
    } else if (!quickMenuOpen) {
        quickMenuFocusPending = false;
        quickMenuFocusReassertFrames = 0;
    }
    ProcessHotkeys();
    ProcessRunningBinds();
    prevFrameGameInputForeground_ = gameInputForeground_;
}

void BinderModule::Impl::Shutdown(bool flushPendingConfig) {
    if (flushPendingConfig) {
        FlushPendingConfigSave(true);
    } else {
        configSavePending_ = false;
        configSaveRequestedAtMs_ = 0;
        pendingConfigMutationCount_ = 0;
    }

    if (inputDialog && inputDialog->hotkeyIndex >= 0 && inputDialog->hotkeyIndex < static_cast<int>(hotkeys.size())) {
        hotkeys[inputDialog->hotkeyIndex].awaitingInput = false;
    }

    editor.active = false;
    inputDialog.reset();
    runningBinds.clear();
    outgoingGuards.clear();
    incomingChatEchoGuards.clear();
    pendingBindTagActions.clear();
    pendingCommandDispatches.clear();
    pendingSelfRestarts.clear();
    folderDeleteTarget = nullptr;
    folderDeletePopupPending = false;
    folderConditionsTarget = nullptr;
    folderConditionsPopupPending = false;
    folderIconTarget = nullptr;
    folderIconPopupPending = false;
    iconPickerState = {};
    bindDeleteTarget = -1;
    bindDeletePopupPending = false;
    moveBindTarget = -1;
    moveBindPopupPending = false;
    moveFolderTarget = -1;
    moveFolderPopupPending = false;
    bindLinesTarget = -1;
    bindLinesPopupPending = false;
    ResetInputState();
}

void BinderModule::Impl::ReloadConfig() {
    debuglog::WriteInfo("BinderModule::ReloadConfig begin");
    Shutdown(false);
    LoadConfig();
    configLoaded = true;
    EnsureRootFolder();
    RefreshNumbers();
    ConnectHooks();
    debuglog::WriteInfo(
        "BinderModule::ReloadConfig done categories=%llu hotkeys=%llu",
        static_cast<unsigned long long>(categories.size()),
        static_cast<unsigned long long>(hotkeys.size()));
}

bool BinderModule::Impl::WantsOverlayRender() const {
    return quickMenuOpen
        || inputDialog.has_value()
        || capture.Active()
        || bindLinesPopupPending
        || bindLinesTarget >= 0;
}

bool BinderModule::Impl::WantsInputCapture() const {
    return inputDialog.has_value()
        || capture.Active()
        || bindLinesPopupPending
        || bindLinesTarget >= 0;
}

bool BinderModule::Impl::WantsInputRouting() const {
    return quickMenuOpen
        || WantsInputCapture()
        || std::any_of(hotkeys.begin(), hotkeys.end(), [](const HotkeyEntry& hotkey) {
            return hotkey.waitingTextConfirmation;
        });
}

bool BinderModule::Impl::IsQuickMenuOpen() const {
    return quickMenuOpen;
}

bool BinderModule::Impl::OnWindowMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    EnsureInitialized();

    const WORD activateState = LOWORD(static_cast<DWORD>(wparam));
    const bool lostFocus = message == WM_KILLFOCUS
        || (message == WM_ACTIVATEAPP && wparam == 0)
        || (message == WM_ACTIVATE && activateState == WA_INACTIVE);
    const bool gainedFocus = message == WM_SETFOCUS
        || (message == WM_ACTIVATEAPP && wparam != 0)
        || (message == WM_ACTIVATE && (activateState == WA_ACTIVE || activateState == WA_CLICKACTIVE));

    if (lostFocus || gainedFocus) {
        ResetInputState();
        return false;
    }

    if (quickMenuOpen && IsQuickMenuMouseButtonMessage(message)) {
        const int clientX = GET_X_LPARAM(lparam);
        const int clientY = GET_Y_LPARAM(lparam);
        quickMenuMouseClientPos = ImVec2(static_cast<float>(clientX), static_cast<float>(clientY));
        quickMenuMouseClientPosValid = true;
        const bool hasQuickMenuRect = quickMenuSize.x > 0.0f && quickMenuSize.y > 0.0f;
        const bool insideQuickMenu = hasQuickMenuRect
            && static_cast<float>(clientX) >= quickMenuPos.x
            && static_cast<float>(clientY) >= quickMenuPos.y
            && static_cast<float>(clientX) < quickMenuPos.x + quickMenuSize.x
            && static_cast<float>(clientY) < quickMenuPos.y + quickMenuSize.y;

        quickMenuMouseEventPending = true;
        quickMenuMouseEventInside = quickMenuMouseEventInside || insideQuickMenu;
        if (insideQuickMenu) {
            debuglog::WriteInfo(
                "[ui] quickmenu mouse msg=%u client=(%d,%d) rect=(%.1f,%.1f %.1fx%.1f) comboHeld=%d",
                static_cast<unsigned>(message),
                clientX,
                clientY,
                quickMenuPos.x,
                quickMenuPos.y,
                quickMenuSize.x,
                quickMenuSize.y,
                IsQuickMenuComboPressed() ? 1 : 0);
        }
    }

    bool canceled = false;
    bool saved = false;
    std::vector<UINT> capturedKeys;
    if (capture.Active() && capture.OnWindowMessage(message, wparam, canceled, saved, capturedKeys)) {
        if (saved) {
            if (!ApplyCapturedKeys(capturedKeys)) {
                capture.Start(capturedKeys);
            }
        } else if (canceled) {
            hotkeys::ResetCapturePopupState(capturePopupState);
            captureTarget = CaptureTarget::None;
            captureHotkeyIndex = -1;
            capturePopupInEditor = false;
        }
        return true;
    }

    const auto keyInfo = ::hotkeys::GetMessageKeyInfo(message, wparam);
    if (keyInfo && keyInfo->isDown && ActivatePendingTextConfirmations(keyInfo->keyCode)) {
        return true;
    }

    if (keyTracker.OnWindowMessage(message, wparam)) {
        pressedKeys = keyTracker.Ordered();
    }
    return false;
}

bool BinderModule::Impl::ApplyCapturedKeys(const std::vector<UINT>& keys) {
    UiSettings& ui = UiSettings::Instance();
    switch (captureTarget) {
    case CaptureTarget::BindHotkey:
        if (editor.active) {
            editor.draft.keys = ::hotkeys::NormalizeCombo(keys, editor.draft.hotkeyMode);
        }
        break;
    case CaptureTarget::QuickMenuHotkey:
        if (std::string description; DescribeQuickMenuConflictWithMenuToggleHotkey(keys, description)) {
            Notify(
                NotificationGroup::BinderErrors,
                NotificationSeverity::Error,
                ui.Format(UiText::HotkeyConflictFormat, description.c_str()),
                2800.0);
            return false;
        }
        quickMenuHotkey = ::hotkeys::NormalizeCombo(keys, HotkeyMode::ModifierTrigger);
        SaveConfig();
        break;
    case CaptureTarget::ConfirmKey:
        if (editor.active) {
            editor.draft.textConfirmation.key = PickSingleCapturedKey(keys, kDefaultConfirmKey);
        }
        break;
    case CaptureTarget::CancelKey:
        if (editor.active) {
            editor.draft.textConfirmation.cancelKey = PickSingleCapturedKey(keys, kDefaultCancelKey);
        }
        break;
    case CaptureTarget::None:
        return false;
    }

    captureTarget = CaptureTarget::None;
    captureHotkeyIndex = -1;
    hotkeys::ResetCapturePopupState(capturePopupState);
    return true;
}

bool BinderModule::Impl::DescribeMainWindowHotkeyConflict(const std::vector<UINT>& keys, std::string& description) {
    EnsureInitialized();
    description.clear();

    const auto menuHotkey = ::hotkeys::NormalizeCombo(keys, HotkeyMode::ModifierTrigger);
    if (!::hotkeys::HasTriggerKey(menuHotkey)) {
        return false;
    }

    const auto quickMenuCombo = CurrentQuickMenuHotkey();
    if (::hotkeys::ContainsCombo(menuHotkey, quickMenuCombo, HotkeyMode::ModifierTrigger)) {
        description = UiSettings::Instance().Format(
            UiText::HotkeyConflictQuickMenuFormat,
            ::hotkeys::ToString(quickMenuCombo).c_str());
        return true;
    }

    for (const HotkeyEntry& hotkey : hotkeys) {
        if (!IsHotkeyEffectivelyEnabled(hotkey) || hotkey.keys.empty()) {
            continue;
        }
        if (!::hotkeys::CombosConflict(menuHotkey, HotkeyMode::ModifierTrigger, hotkey.keys, hotkey.hotkeyMode)) {
            continue;
        }

        const std::string label = BuildBindDisplayLabel(hotkey);
        description = UiSettings::Instance().Format(
            UiText::HotkeyConflictBindFormat,
            label.c_str(),
            ::hotkeys::ToString(hotkey.keys, hotkey.hotkeyMode).c_str());
        return true;
    }

    return false;
}

bool BinderModule::Impl::DescribeConflictWithMenuToggleHotkey(
    const std::vector<UINT>& keys,
    HotkeyMode mode,
    std::string& description) const {
    description.clear();

    const auto candidate = ::hotkeys::NormalizeCombo(keys, mode);
    const auto menuHotkey = ::hotkeys::NormalizeCombo(UiSettings::Instance().MenuToggleHotkey(), HotkeyMode::ModifierTrigger);
    if (candidate.empty() || !::hotkeys::HasTriggerKey(menuHotkey)) {
        return false;
    }
    if (!::hotkeys::CombosConflict(candidate, mode, menuHotkey, HotkeyMode::ModifierTrigger)) {
        return false;
    }

    description = UiSettings::Instance().Format(
        UiText::HotkeyConflictMainWindowFormat,
        ::hotkeys::ToString(menuHotkey).c_str());
    return true;
}

bool BinderModule::Impl::DescribeQuickMenuConflictWithMenuToggleHotkey(
    const std::vector<UINT>& keys,
    std::string& description) const {
    description.clear();

    std::vector<UINT> quickMenuCombo = ::hotkeys::NormalizeCombo(keys, HotkeyMode::ModifierTrigger);
    if (quickMenuCombo.empty()) {
        quickMenuCombo = { kDefaultQuickMenuFallback };
    }

    const auto menuHotkey = ::hotkeys::NormalizeCombo(UiSettings::Instance().MenuToggleHotkey(), HotkeyMode::ModifierTrigger);
    if (!::hotkeys::HasTriggerKey(menuHotkey)) {
        return false;
    }
    if (!::hotkeys::ContainsCombo(menuHotkey, quickMenuCombo, HotkeyMode::ModifierTrigger)) {
        return false;
    }

    description = UiSettings::Instance().Format(
        UiText::HotkeyConflictMainWindowFormat,
        ::hotkeys::ToString(menuHotkey).c_str());
    return true;
}

std::vector<UINT> BinderModule::Impl::CurrentQuickMenuHotkey() const {
    if (!quickMenuHotkey.empty()) {
        return ::hotkeys::NormalizeCombo(quickMenuHotkey, HotkeyMode::ModifierTrigger);
    }
    return { kDefaultQuickMenuFallback };
}

std::string BinderModule::Impl::QuickMenuHotkeyText() const {
    return ::hotkeys::ToString(CurrentQuickMenuHotkey());
}

bool BinderModule::Impl::IsQuickMenuComboPressed() const {
    return ::hotkeys::ContainsCombo(pressedKeys, CurrentQuickMenuHotkey(), HotkeyMode::ModifierTrigger);
}

bool BinderModule::Impl::IsMainWindowHotkeyPressed() const {
    return ::hotkeys::ComboMatch(pressedKeys, UiSettings::Instance().MenuToggleHotkey(), HotkeyMode::ModifierTrigger);
}

bool BinderModule::Impl::CaptureUsesEditorPopup() const {
    return capturePopupInEditor;
}

void BinderModule::Impl::BeginCapture(CaptureTarget target) {
    captureTarget = target;
    captureHotkeyIndex = editor.hotkeyIndex;
    capturePopupInEditor = editor.active
        && (target == CaptureTarget::BindHotkey || target == CaptureTarget::ConfirmKey || target == CaptureTarget::CancelKey);

    std::vector<UINT> initial;
    switch (target) {
    case CaptureTarget::BindHotkey:
        if (editor.active) {
            initial = editor.draft.keys;
        }
        break;
    case CaptureTarget::QuickMenuHotkey:
        initial = quickMenuHotkey;
        break;
    case CaptureTarget::ConfirmKey:
        if (editor.active && editor.draft.textConfirmation.key != 0) {
            initial = { editor.draft.textConfirmation.key };
        }
        break;
    case CaptureTarget::CancelKey:
        if (editor.active && editor.draft.textConfirmation.cancelKey != 0) {
            initial = { editor.draft.textConfirmation.cancelKey };
        }
        break;
    case CaptureTarget::None:
        break;
    }

    capture.Start(initial);
    hotkeys::OpenCapturePopupCenteredOnCurrentWindow(capturePopupState);
}

void BinderModule::Impl::ProcessHotkeys() {
    const double now = static_cast<double>(GetTickCount64());

    if (pressedKeys.empty()) {
        for (HotkeyEntry& hotkey : hotkeys) {
            hotkey.comboActive = false;
            hotkey.lastRepeatPressed.clear();
        }
        return;
    }

    if (quickMenuOpen || IsQuickMenuComboPressed() || inputDialog.has_value() || IsMainWindowHotkeyPressed()) {
        return;
    }

    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        HotkeyEntry& hotkey = hotkeys[i];
        if (!IsHotkeyEffectivelyEnabled(hotkey) || hotkey.keys.empty()) {
            hotkey.comboActive = false;
            hotkey.lastRepeatPressed.clear();
            continue;
        }

        const bool comboNow = ::hotkeys::ComboMatch(pressedKeys, hotkey.keys, hotkey.hotkeyMode);
        if (hotkey.repeatMode) {
            if (comboNow) {
                const bool freshPress = hotkey.lastRepeatPressed.empty()
                    || !::hotkeys::ComboMatch(hotkey.lastRepeatPressed, hotkey.keys, hotkey.hotkeyMode);
                if (IsHotkeyRunning(static_cast<int>(i))) {
                    if (freshPress) {
                        TryEnqueueHotkey(static_cast<int>(i), 0, "hotkey", "");
                        hotkey.lastActivatedAtMs = now;
                    }
                    hotkey.lastRepeatPressed = pressedKeys;
                    continue;
                }

                const int interval = std::max(hotkey.repeatIntervalMs, kMinMessageIntervalMs);
                if (freshPress || now >= hotkey.lastActivatedAtMs + interval) {
                    TryEnqueueHotkey(static_cast<int>(i), 0, "hotkey", "");
                    hotkey.lastActivatedAtMs = now;
                    hotkey.lastRepeatPressed = pressedKeys;
                }
            } else {
                hotkey.lastRepeatPressed.clear();
                hotkey.comboActive = false;
            }
            continue;
        }

        if (comboNow && !hotkey.comboActive) {
            if (now >= hotkey.debounceUntilMs) {
                TryEnqueueHotkey(static_cast<int>(i), 0, "hotkey", "");
                hotkey.debounceUntilMs = now + kHotkeyDebounceMs;
            }
            hotkey.comboActive = true;
        } else if (!comboNow) {
            hotkey.comboActive = false;
        }
    }
}

void BinderModule::Impl::ProcessRunningBinds() {
    const double now = static_cast<double>(GetTickCount64());
    for (std::size_t i = 0; i < runningBinds.size();) {
        const std::uint64_t currentRuntimeId = runningBinds[i].hotkeyRuntimeId;
        RunningBind& running = runningBinds[i];
        const int hotkeyIndex = FindHotkeyIndexByRuntimeId(currentRuntimeId);
        if (hotkeyIndex < 0) {
            runningBinds.erase(runningBinds.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }

        HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(hotkeyIndex)];
        if (running.messageIndex >= hotkey.messages.size()) {
            runningBinds.erase(runningBinds.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }

        if (running.paused) {
            ++i;
            continue;
        }

        if (now < running.nextAtMs) {
            ++i;
            continue;
        }

        const HotkeyMessage& message = hotkey.messages[running.messageIndex];
        const std::string finalText = ApplyInputValues(message.text, running.inputValues);
        bool dispatched = true;
        if (!Trim(finalText).empty()) {
            if (tagsModule) {
                tagsModule->PushContext(TagsModule::EvaluationContext{
                    sampApi,
                    running.activationSource,
                    running.activationText,
                    running.bindCommand,
                    true,
                    running.hotkeyRuntimeId,
                });
                dispatched = DoSend(finalText, message.method, currentRuntimeId);
                tagsModule->PopContext();
            } else {
                dispatched = DoSend(finalText, message.method, currentRuntimeId);
            }
        }
        if (!dispatched) {
            ++i;
            continue;
        }

        ++running.messageIndex;
        const bool hasNextMessage = running.messageIndex < hotkey.messages.size();
        if (hasNextMessage) {
            const std::optional<int> delayOverrideMs =
                tagsModule ? tagsModule->ConsumePendingBindDelayOverride(currentRuntimeId) : std::nullopt;
            running.remainingDelayMs = 0.0;
            const int nextDelayMs = delayOverrideMs.has_value() ? *delayOverrideMs : message.intervalMs;
            running.nextAtMs = now + std::max(nextDelayMs, kMinMessageIntervalMs);
        }

        ExecutePendingBindTagActions(currentRuntimeId);
        ExecutePendingCommandDispatches(currentRuntimeId);

        const bool currentStillRunning = i < runningBinds.size() && runningBinds[i].hotkeyRuntimeId == currentRuntimeId;
        if (!hasNextMessage) {
            if (currentStillRunning) {
                runningBinds.erase(runningBinds.begin() + static_cast<std::ptrdiff_t>(i));
            }
            StartPendingSelfRestart(currentRuntimeId);
            continue;
        }

        if (!currentStillRunning) {
            continue;
        }

        ++i;
    }
}

RunningBind* BinderModule::Impl::FindRunningBind(std::uint64_t hotkeyRuntimeId) {
    return FindRunningBindByRuntimeId(runningBinds, hotkeyRuntimeId);
}

const RunningBind* BinderModule::Impl::FindRunningBind(std::uint64_t hotkeyRuntimeId) const {
    return FindRunningBindByRuntimeId(runningBinds, hotkeyRuntimeId);
}

RunningBind* BinderModule::Impl::FindRunningBindForHotkey(int index) {
    return FindRunningBind(HotkeyRuntimeIdAt(hotkeys, index));
}

const RunningBind* BinderModule::Impl::FindRunningBindForHotkey(int index) const {
    return FindRunningBind(HotkeyRuntimeIdAt(hotkeys, index));
}

bool BinderModule::Impl::IsHotkeyRunning(int index) const {
    return FindRunningBindForHotkey(index) != nullptr;
}

bool BinderModule::Impl::IsHotkeyPaused(int index) const {
    if (const RunningBind* running = FindRunningBindForHotkey(index)) {
        return running->paused;
    }
    return false;
}

bool BinderModule::Impl::TryToggleRunningHotkeyActivation(int index, std::string_view source, double now) {
    if (!CanToggleRunningHotkeyActivation(source) || index < 0 || index >= static_cast<int>(hotkeys.size())) {
        return false;
    }

    HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
    if (!IsHotkeyEffectivelyEnabled(index) || hotkey.awaitingInput || hotkey.waitingTextConfirmation) {
        return false;
    }

    RunningBind* running = FindRunningBindForHotkey(index);
    if (!running) {
        return false;
    }

    if (now < hotkey.debounceUntilMs) {
        return true;
    }

    const bool resume = running->paused;
    const bool changed = resume ? ResumeHotkey(index) : PauseHotkey(index);
    hotkey.debounceUntilMs = now + kHotkeyDebounceMs;
    if (!changed) {
        return true;
    }

    const std::string label = BuildBindDisplayLabel(hotkey);
    debuglog::WriteInfo(
        "[binder] bind %s by activation source=%.*s runtime=%llu label=%s",
        resume ? "resumed" : "paused",
        static_cast<int>(source.size()),
        source.data(),
        static_cast<unsigned long long>(hotkey.runtimeId),
        label.c_str());

    if (ShouldNotifyRunningHotkeyToggle(source)) {
        UiSettings& ui = UiSettings::Instance();
        Notify(
            NotificationGroup::BinderEvents,
            resume ? NotificationSeverity::Success : NotificationSeverity::Info,
            ui.Format(resume ? UiText::ToastBindResumed : UiText::ToastBindPaused, label.c_str()),
            1800.0);
    }

    return true;
}

bool BinderModule::Impl::PauseHotkey(int index) {
    RunningBind* running = FindRunningBindForHotkey(index);
    if (!running || running->paused) {
        return false;
    }

    const double now = static_cast<double>(GetTickCount64());
    running->remainingDelayMs = std::max(0.0, running->nextAtMs - now);
    running->paused = true;
    return true;
}

bool BinderModule::Impl::ResumeHotkey(int index) {
    RunningBind* running = FindRunningBindForHotkey(index);
    if (!running || !running->paused) {
        return false;
    }

    const double now = static_cast<double>(GetTickCount64());
    running->paused = false;
    running->nextAtMs = now + std::max(running->remainingDelayMs, 0.0);
    running->remainingDelayMs = 0.0;
    return true;
}

int BinderModule::Impl::StopAllHotkeys() {
    std::vector<std::uint64_t> runtimeIds;
    runtimeIds.reserve(hotkeys.size());
    for (const HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.awaitingInput || hotkey.waitingTextConfirmation || FindRunningBind(hotkey.runtimeId) != nullptr) {
            runtimeIds.push_back(hotkey.runtimeId);
        }
    }

    std::sort(runtimeIds.begin(), runtimeIds.end());
    runtimeIds.erase(std::unique(runtimeIds.begin(), runtimeIds.end()), runtimeIds.end());
    for (const std::uint64_t runtimeId : runtimeIds) {
        StopHotkeyByRuntimeId(runtimeId);
    }

    return static_cast<int>(runtimeIds.size());
}

void BinderModule::Impl::StopHotkeyByRuntimeId(std::uint64_t runtimeId) {
    if (runtimeId == 0) {
        return;
    }

    if (inputDialog
        && inputDialog->hotkeyIndex >= 0
        && inputDialog->hotkeyIndex < static_cast<int>(hotkeys.size())
        && hotkeys[static_cast<std::size_t>(inputDialog->hotkeyIndex)].runtimeId == runtimeId) {
        hotkeys[static_cast<std::size_t>(inputDialog->hotkeyIndex)].awaitingInput = false;
        inputDialog.reset();
    }

    if (const int hotkeyIndex = FindHotkeyIndexByRuntimeId(runtimeId); hotkeyIndex >= 0) {
        HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(hotkeyIndex)];
        hotkey.awaitingInput = false;
        hotkey.waitingTextConfirmation = false;
        hotkey.textConfirmationDeadlineMs = 0.0;
        hotkey.pendingConfirmationKey = kDefaultConfirmKey;
        hotkey.pendingConfirmationCancelKey = kDefaultCancelKey;
        hotkey.pendingTriggerText.clear();
        hotkey.pendingTriggerSource.clear();
    }

    runningBinds.erase(
        std::remove_if(runningBinds.begin(), runningBinds.end(), [&](const RunningBind& running) {
            return running.hotkeyRuntimeId == runtimeId;
        }),
        runningBinds.end());

    pendingBindTagActions.erase(
        std::remove_if(pendingBindTagActions.begin(), pendingBindTagActions.end(), [&](const PendingBindTagAction& pending) {
            return pending.sourceRuntimeId == runtimeId;
        }),
        pendingBindTagActions.end());

    pendingCommandDispatches.erase(
        std::remove_if(pendingCommandDispatches.begin(), pendingCommandDispatches.end(), [&](const PendingCommandDispatch& pending) {
            return pending.sourceRuntimeId == runtimeId;
        }),
        pendingCommandDispatches.end());

    pendingSelfRestarts.erase(
        std::remove(pendingSelfRestarts.begin(), pendingSelfRestarts.end(), runtimeId),
        pendingSelfRestarts.end());
}

bool BinderModule::Impl::StopHotkey(int index) {
    if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
        return false;
    }

    HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
    const bool hadWork = hotkey.awaitingInput || hotkey.waitingTextConfirmation || FindRunningBind(hotkey.runtimeId) != nullptr;
    StopHotkeyByRuntimeId(hotkey.runtimeId);
    return hadWork;
}

void BinderModule::Impl::ExecutePendingBindTagActions(std::uint64_t sourceRuntimeId) {
    std::vector<PendingBindTagAction> localActions;
    for (std::size_t i = 0; i < pendingBindTagActions.size();) {
        if (pendingBindTagActions[i].sourceRuntimeId != sourceRuntimeId) {
            ++i;
            continue;
        }

        localActions.push_back(std::move(pendingBindTagActions[i]));
        pendingBindTagActions.erase(pendingBindTagActions.begin() + static_cast<std::ptrdiff_t>(i));
    }

    for (PendingBindTagAction& pending : localActions) {
        BinderModule::TagActionResult result =
            ExecuteBindTagActionNow(pending.action, pending.targetIndices, pending.actionName, pending.sourceRuntimeId);
        if (!result.success && !result.error.empty()) {
            Notify(
                NotificationGroup::TagErrors,
                NotificationSeverity::Error,
                DescribeBindTagError(pending.actionName, result.error),
                2600.0);
        }
        debuglog::WriteInfo(
            "[binder][bind-tag] executed pending action=%s success=%d affected=%d error=%s selector=\"%s\" source={%s} targets={%s}",
            pending.actionName.c_str(),
            result.success ? 1 : 0,
            result.affected,
            result.error.c_str(),
            EscapeBindTagLogValue(pending.selector).c_str(),
            DescribeBindTagLogSource(pending.sourceRuntimeId).c_str(),
            DescribeBindTagLogTargets(pending.targetIndices).c_str());
    }
}

void BinderModule::Impl::ExecutePendingCommandDispatches(std::uint64_t sourceRuntimeId) {
    std::vector<PendingCommandDispatch> localDispatches;
    for (std::size_t i = 0; i < pendingCommandDispatches.size();) {
        if (pendingCommandDispatches[i].sourceRuntimeId != sourceRuntimeId) {
            ++i;
            continue;
        }

        localDispatches.push_back(std::move(pendingCommandDispatches[i]));
        pendingCommandDispatches.erase(pendingCommandDispatches.begin() + static_cast<std::ptrdiff_t>(i));
    }

    for (const PendingCommandDispatch& pending : localDispatches) {
        if (!DispatchCommandTrigger(pending.command, kNestedBindStartDelayMs, false)) {
            SendExpandedText(pending.command, pending.method);
        }
    }
}

void BinderModule::Impl::StartRunningBind(
    const HotkeyEntry& hotkey,
    std::map<std::string, std::string> inputValues,
    int startDelayMs,
    std::string activationSource,
    std::string activationText,
    std::string bindCommand) {
    const bool alreadyRunning = FindRunningBind(hotkey.runtimeId) != nullptr;
    if (hotkey.runtimeId == 0 || alreadyRunning) {
        return;
    }

    const BinderCategory* category = FindCategoryById(hotkey.categoryId);
    runningBinds.push_back(RunningBind{
        hotkey.runtimeId,
        hotkey.categoryId,
        category ? category->name : std::string{},
        std::move(inputValues),
        0,
        static_cast<double>(GetTickCount64() + std::max(startDelayMs, 0)),
        std::move(activationSource),
        std::move(activationText),
        std::move(bindCommand),
        false,
        0.0,
    });
}

void BinderModule::Impl::PruneOutgoingGuards() {
    const double now = static_cast<double>(GetTickCount64());
    outgoingGuards.erase(
        std::remove_if(outgoingGuards.begin(), outgoingGuards.end(), [&](const OutgoingGuard& guard) {
            return guard.expiresAtMs <= now;
        }),
        outgoingGuards.end());
}

void BinderModule::Impl::RegisterOutgoingGuard(std::string kind, std::string text) {
    text = NormalizeActivationText(text);
    if (kind == "command" && !text.empty() && text.front() != '/') {
        text.insert(text.begin(), '/');
    }
    if (text.empty()) {
        return;
    }

    outgoingGuards.push_back(OutgoingGuard{
        std::move(kind),
        std::move(text),
        static_cast<double>(GetTickCount64() + kOutgoingGuardTimeoutMs),
    });
    while (outgoingGuards.size() > 64) {
        outgoingGuards.erase(outgoingGuards.begin());
    }
}

bool BinderModule::Impl::ConsumeOutgoingGuard(std::string_view kind, std::string_view text) {
    std::string normalized = NormalizeActivationText(text);
    if (kind == "command" && !normalized.empty() && normalized.front() != '/') {
        normalized.insert(normalized.begin(), '/');
    }
    if (normalized.empty()) {
        return false;
    }

    for (auto it = outgoingGuards.begin(); it != outgoingGuards.end(); ++it) {
        if (it->kind == kind && it->text == normalized) {
            outgoingGuards.erase(it);
            return true;
        }
    }
    return false;
}

void BinderModule::Impl::PruneIncomingChatEchoGuards() {
    const double now = static_cast<double>(GetTickCount64());
    incomingChatEchoGuards.erase(
        std::remove_if(incomingChatEchoGuards.begin(), incomingChatEchoGuards.end(), [&](const IncomingChatEchoGuard& guard) {
            return guard.expiresAtMs <= now;
        }),
        incomingChatEchoGuards.end());
}

std::string BinderModule::Impl::CurrentLocalPlayerName() const {
    if (!sampApi) {
        return {};
    }

    const int localId = sampApi->Local_ID();
    if (localId < 0) {
        return {};
    }

    const std::string localName = Trim(sampApi->GetNameID(localId));
    return EqualNoCase(localName, "UNKNOWN") ? std::string() : localName;
}

void BinderModule::Impl::RegisterIncomingChatEchoGuard(std::string text) {
    text = NormalizeTriggerText(text);
    if (text.empty()) {
        return;
    }

    incomingChatEchoGuards.push_back(IncomingChatEchoGuard{
        std::move(text),
        CurrentLocalPlayerName(),
        static_cast<double>(GetTickCount64() + kIncomingChatEchoGuardTimeoutMs),
    });
    while (incomingChatEchoGuards.size() > 64) {
        incomingChatEchoGuards.erase(incomingChatEchoGuards.begin());
    }
}

bool BinderModule::Impl::ConsumeIncomingChatEchoGuard(
    std::string_view normalizedText,
    std::string_view normalizedPrefixedText) {
    const auto matchesGuard = [](std::string_view candidate, const IncomingChatEchoGuard& guard) {
        if (candidate.empty() || guard.text.empty()) {
            return false;
        }
        if (candidate == guard.text) {
            return true;
        }
        if (guard.localPlayerName.empty()) {
            return false;
        }
        return ContainsNoCase(candidate, guard.localPlayerName) && EndsWith(candidate, guard.text);
    };

    for (auto it = incomingChatEchoGuards.begin(); it != incomingChatEchoGuards.end(); ++it) {
        if (matchesGuard(normalizedText, *it) || matchesGuard(normalizedPrefixedText, *it)) {
            incomingChatEchoGuards.erase(it);
            return true;
        }
    }
    return false;
}

std::string BinderModule::Impl::NormalizeActivationText(std::string_view text) const {
    return Trim(NormalizeLineEndings(text));
}

bool BinderModule::Impl::MatchesActivationCommand(std::string_view input, std::string_view command) const {
    std::string normalizedInput = NormalizeActivationText(input);
    std::string normalizedCommand = NormalizeActivationText(command);
    if (normalizedInput.empty() || normalizedCommand.empty()) {
        return false;
    }

    const bool inputIsSlashCommand = normalizedInput.front() == '/';
    const bool commandIsSlashCommand = normalizedCommand.front() == '/';
    if (inputIsSlashCommand != commandIsSlashCommand) {
        return false;
    }

    return StartsWith(normalizedInput, normalizedCommand)
        && (normalizedInput.size() == normalizedCommand.size()
            || std::isspace(static_cast<unsigned char>(normalizedInput[normalizedCommand.size()])) != 0);
}

bool BinderModule::Impl::HasCommandTriggerCandidate(const std::string& normalizedCommand, double now) const {
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        const HotkeyEntry& hotkey = hotkeys[i];
        if (!IsHotkeyEffectivelyEnabled(hotkey) || !hotkey.commandEnabled || hotkey.command.empty() || hotkey.awaitingInput) {
            continue;
        }
        if (!MatchesActivationCommand(normalizedCommand, hotkey.command)) {
            continue;
        }
        if (IsHotkeyRunning(static_cast<int>(i))) {
            continue;
        }
        if (now < hotkey.debounceUntilMs) {
            continue;
        }
        return true;
    }
    return false;
}

bool BinderModule::Impl::DispatchCommandTrigger(
    const std::string& normalizedCommand,
    int startDelayMs,
    bool allowRunningToggle) {
    const double now = static_cast<double>(GetTickCount64());
    bool handled = false;
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        HotkeyEntry& hotkey = hotkeys[i];
        if (!IsHotkeyEffectivelyEnabled(hotkey) || !hotkey.commandEnabled || hotkey.command.empty() || hotkey.awaitingInput) {
            continue;
        }
        if (!MatchesActivationCommand(normalizedCommand, hotkey.command)) {
            continue;
        }
        if (allowRunningToggle && TryToggleRunningHotkeyActivation(static_cast<int>(i), "command", now)) {
            handled = true;
            continue;
        }
        if (IsHotkeyRunning(static_cast<int>(i))) {
            continue;
        }
        if (now < hotkey.debounceUntilMs) {
            continue;
        }
        hotkey.debounceUntilMs = now + kHotkeyDebounceMs;
        handled = true;
        if (hotkey.commandConfirmation.enabled
            && TryBeginPendingConfirmation(hotkey, "command", normalizedCommand, hotkey.commandConfirmation.waitForResolution)) {
            continue;
        }
        TryEnqueueHotkey(static_cast<int>(i), startDelayMs, "command", normalizedCommand);
    }
    return handled;
}

bool BinderModule::Impl::QueueCommandDispatchFromRunningBind(
    const std::string& command,
    std::uint64_t sourceRuntimeId,
    int method) {
    std::string normalizedCommand = NormalizeActivationText(command);
    if (sourceRuntimeId == 0 || normalizedCommand.empty()) {
        return false;
    }
    if (!HasCommandTriggerCandidate(normalizedCommand, static_cast<double>(GetTickCount64()))) {
        return false;
    }

    pendingCommandDispatches.push_back(PendingCommandDispatch{
        sourceRuntimeId,
        normalizedCommand,
        method,
    });
    return true;
}

bool BinderModule::Impl::TryBeginPendingConfirmation(
    HotkeyEntry& hotkey,
    std::string_view sourceKind,
    const std::string& sourceText,
    bool waitForResolution) {
    std::string conditionMessage;
    if (!IsHotkeyEffectivelyEnabled(hotkey) || hotkey.waitingTextConfirmation || hotkey.awaitingInput
        || ConditionsBlockHotkeyStart(hotkey, sourceKind, &conditionMessage)) {
        return false;
    }

    const double now = static_cast<double>(GetTickCount64());
    hotkey.waitingTextConfirmation = true;
    hotkey.pendingConfirmationKey = ::hotkeys::IsHotkeyKey(hotkey.textConfirmation.key)
        ? ::hotkeys::NormalizeKey(hotkey.textConfirmation.key)
        : kDefaultConfirmKey;
    hotkey.pendingConfirmationCancelKey = ::hotkeys::IsHotkeyKey(hotkey.textConfirmation.cancelKey)
        ? ::hotkeys::NormalizeKey(hotkey.textConfirmation.cancelKey)
        : kDefaultCancelKey;
    hotkey.pendingTriggerText = sourceText;
    hotkey.pendingTriggerSource = std::string(sourceKind);
    if (waitForResolution && sourceKind != "command") {
        hotkey.textConfirmationDeadlineMs = now + textConfirmationWaitTimeoutMs;
    } else {
        hotkey.textConfirmationDeadlineMs = waitForResolution ? 0.0 : now + kTextConfirmTimeoutMs;
    }

    UiSettings& ui = UiSettings::Instance();
    const std::string confirmText = ui.Format(
        UiText::ToastConfirmPrompt,
        ConfirmationSourceLabel(sourceKind),
        BuildBindDisplayLabel(hotkey).c_str(),
        ::hotkeys::KeyName(hotkey.pendingConfirmationKey).c_str(),
        ::hotkeys::KeyName(hotkey.pendingConfirmationCancelKey).c_str());
    Notify(
        NotificationGroup::Confirmation,
        NotificationSeverity::Warning,
        confirmText,
        waitForResolution ? 4000.0 : 2500.0);
    return true;
}

bool BinderModule::Impl::OnOutgoingCommand(const std::string& text) {
    std::string normalized = NormalizeActivationText(text);
    if (!normalized.empty() && normalized.front() != '/') {
        normalized.insert(normalized.begin(), '/');
    }
    if (normalized.empty()) {
        return false;
    }

    const bool consumedGuard = ConsumeOutgoingGuard("command", normalized);
    bool handled = false;
    if (!consumedGuard) {
        handled = OnTextTriggerEvent(normalized, "outgoing_command");
    }

    handled = DispatchCommandTrigger(normalized, 0) || handled;
    return handled;
}

bool BinderModule::Impl::OnOutgoingChat(const std::string& text) {
    const std::string normalized = NormalizeActivationText(text);
    if (normalized.empty() || ConsumeOutgoingGuard("chat", normalized)) {
        return false;
    }
    if (!normalized.empty() && normalized.front() == '/') {
        return false;
    }
    if (DispatchCommandTrigger(normalized, 0)) {
        return true;
    }
    const bool handled = OnTextTriggerEvent(normalized, "outgoing_chat");
    if (handled) {
        RegisterIncomingChatEchoGuard(normalized);
    }
    return false;
}

void BinderModule::Impl::OnIncomingMessage(const IncomingMessageEvent& message) {
    const std::string normalizedText = NormalizeTriggerText(message.text);
    std::string normalizedPrefixedText;
    if (!Trim(message.prefix).empty()) {
        normalizedPrefixedText = NormalizeTriggerText(message.prefix + " " + message.text);
    }

    if (normalizedText.empty() || ConsumeOutgoingGuard("echo", normalizedText)) {
        return;
    }
    if (ConsumeIncomingChatEchoGuard(normalizedText, normalizedPrefixedText)) {
        return;
    }

    const double now = static_cast<double>(GetTickCount64());
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        HotkeyEntry& hotkey = hotkeys[i];
        if (!IsHotkeyEffectivelyEnabled(hotkey)) {
            continue;
        }

        const std::string* matchedSource = nullptr;
        if (MatchTextTrigger(normalizedText, hotkey)) {
            matchedSource = &normalizedText;
        } else if (!normalizedPrefixedText.empty() && MatchTextTrigger(normalizedPrefixedText, hotkey)) {
            matchedSource = &normalizedPrefixedText;
        }

        if (!matchedSource) {
            continue;
        }

        TryDispatchTextTriggerMatch(static_cast<int>(i), hotkey, *matchedSource, "incoming_server", now);
    }
}

void BinderModule::Impl::ExpireTextConfirmations() {
    const double now = static_cast<double>(GetTickCount64());
    for (HotkeyEntry& hotkey : hotkeys) {
        if (!hotkey.waitingTextConfirmation || hotkey.textConfirmationDeadlineMs <= 0.0) {
            continue;
        }
        if (now >= hotkey.textConfirmationDeadlineMs) {
            const char* confirmationLabel = ConfirmationSourceLabel(hotkey.pendingTriggerSource);
            hotkey.waitingTextConfirmation = false;
            hotkey.textConfirmationDeadlineMs = 0.0;
            hotkey.pendingConfirmationKey = kDefaultConfirmKey;
            hotkey.pendingConfirmationCancelKey = kDefaultCancelKey;
            hotkey.pendingTriggerText.clear();
            hotkey.pendingTriggerSource.clear();
            Notify(
                NotificationGroup::Confirmation,
                NotificationSeverity::Warning,
                UiSettings::Instance().Format(
                    UiText::ToastBindConfirmExpired,
                    confirmationLabel,
                    BuildBindDisplayLabel(hotkey).c_str()),
                2500.0);
        }
    }
}

bool BinderModule::Impl::ActivatePendingTextConfirmations(UINT keyCode) {
    bool handled = false;
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        HotkeyEntry& hotkey = hotkeys[i];
        if (!hotkey.waitingTextConfirmation) {
            continue;
        }
        if (!IsHotkeyEffectivelyEnabled(hotkey)) {
            hotkey.waitingTextConfirmation = false;
            hotkey.textConfirmationDeadlineMs = 0.0;
            hotkey.pendingConfirmationKey = kDefaultConfirmKey;
            hotkey.pendingConfirmationCancelKey = kDefaultCancelKey;
            hotkey.pendingTriggerText.clear();
            hotkey.pendingTriggerSource.clear();
            continue;
        }

        if (keyCode == hotkey.pendingConfirmationKey) {
            const std::string pendingText = hotkey.pendingTriggerText;
            const std::string pendingSource = hotkey.pendingTriggerSource;
            hotkey.waitingTextConfirmation = false;
            hotkey.textConfirmationDeadlineMs = 0.0;
            hotkey.pendingConfirmationKey = kDefaultConfirmKey;
            hotkey.pendingConfirmationCancelKey = kDefaultCancelKey;
            hotkey.pendingTriggerText.clear();
            hotkey.pendingTriggerSource.clear();
            TryEnqueueHotkey(static_cast<int>(i), 0, pendingSource, pendingText);
            handled = true;
        } else if (keyCode == hotkey.pendingConfirmationCancelKey) {
            const char* confirmationLabel = ConfirmationSourceLabel(hotkey.pendingTriggerSource);
            hotkey.waitingTextConfirmation = false;
            hotkey.textConfirmationDeadlineMs = 0.0;
            hotkey.pendingConfirmationKey = kDefaultConfirmKey;
            hotkey.pendingConfirmationCancelKey = kDefaultCancelKey;
            hotkey.pendingTriggerText.clear();
            hotkey.pendingTriggerSource.clear();
            Notify(
                NotificationGroup::Confirmation,
                NotificationSeverity::Warning,
                UiSettings::Instance().Format(
                    UiText::ToastBindCanceled,
                    confirmationLabel,
                    BuildBindDisplayLabel(hotkey).c_str()),
                2200.0);
            handled = true;
        }
    }
    return handled;
}

bool BinderModule::Impl::MatchTextTrigger(const std::string& source, HotkeyEntry& hotkey) {
    TextTrigger& trigger = hotkey.textTrigger;
    if (!trigger.enabled || Trim(trigger.text).empty()) {
        return false;
    }

    EnsureTextTriggerRuntimeCache(trigger);
    const std::string normalizedSource = NormalizeTriggerText(source);
    const std::string& normalizedTarget = trigger.runtimeNormalizedText;
    if (normalizedTarget.empty()) {
        return false;
    }

    if (!trigger.pattern) {
        return normalizedSource == normalizedTarget;
    }

    if (trigger.runtimePattern) {
        return trigger.runtimePattern->Match(normalizedSource).status == text_pattern::MatchStatus::Match
            || normalizedSource == normalizedTarget;
    }
    if (trigger.runtimePatternInvalid) {
        return normalizedSource == normalizedTarget;
    }

    return normalizedSource == normalizedTarget;
}

bool BinderModule::Impl::TryDispatchTextTriggerMatch(
    int index,
    HotkeyEntry& hotkey,
    const std::string& sourceText,
    std::string_view sourceKind,
    double now) {
    if (TryToggleRunningHotkeyActivation(index, sourceKind, now)) {
        return true;
    }
    if (IsHotkeyRunning(index)) {
        return false;
    }

    if (now < hotkey.debounceUntilMs) {
        return false;
    }

    hotkey.debounceUntilMs = now + kHotkeyDebounceMs;
    if (hotkey.textConfirmation.enabled
        && TryBeginPendingConfirmation(hotkey, sourceKind, sourceText, hotkey.textConfirmation.waitForResolution)) {
        return true;
    }

    TryEnqueueHotkey(index, 0, sourceKind, sourceText);
    return true;
}

bool BinderModule::Impl::OnTextTriggerEvent(const std::string& sourceText, std::string_view sourceKind) {
    const double now = static_cast<double>(GetTickCount64());
    bool handled = false;
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        HotkeyEntry& hotkey = hotkeys[i];
        if (!IsHotkeyEffectivelyEnabled(hotkey) || !MatchTextTrigger(sourceText, hotkey)) {
            continue;
        }
        handled = TryDispatchTextTriggerMatch(static_cast<int>(i), hotkey, sourceText, sourceKind, now) || handled;
    }
    return handled;
}

bool BinderModule::Impl::TryEnqueueHotkey(
    HotkeyEntry& hotkey,
    int startDelayMs,
    std::string_view source,
    const std::string& sourceText) {
    const auto it = std::find_if(hotkeys.begin(), hotkeys.end(), [&](const HotkeyEntry& item) { return &item == &hotkey; });
    if (it == hotkeys.end()) {
        return false;
    }
    return TryEnqueueHotkey(static_cast<int>(std::distance(hotkeys.begin(), it)), startDelayMs, source, sourceText);
}

bool BinderModule::Impl::TryEnqueueHotkey(
    int index,
    int startDelayMs,
    std::string_view source,
    const std::string& sourceText) {
    if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
        return false;
    }

    HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
    const double now = static_cast<double>(GetTickCount64());
    if (TryToggleRunningHotkeyActivation(index, source, now)) {
        return true;
    }

    const bool isRunning = IsHotkeyRunning(index);
    if (!IsHotkeyEffectivelyEnabled(hotkey) || hotkey.awaitingInput || hotkey.waitingTextConfirmation || isRunning) {
        return false;
    }

    std::string conditionMessage;
    if (ConditionsBlockHotkeyStart(hotkey, source, &conditionMessage)) {
        if (!conditionMessage.empty() && !IsSilentActivationSource(source)) {
            Notify(
                NotificationGroup::BinderErrors,
                NotificationSeverity::Warning,
                UiSettings::Instance().Format(UiText::ToastConditionBlocked, conditionMessage.c_str()),
                2200.0);
        }
        return false;
    }

    if (!hotkey.inputs.empty()) {
        if (inputDialog.has_value() && inputDialog->hotkeyIndex != index) {
            Notify(
                NotificationGroup::BinderErrors,
                NotificationSeverity::Warning,
                UiSettings::Instance().Text(UiText::ToastFinishActiveInput),
                2500.0);
            return false;
        }

        InputDialogState dialog;
        dialog.hotkeyIndex = index;
        dialog.startDelayMs = startDelayMs;
        dialog.activationSource = std::string(source);
        dialog.activationText = sourceText;
        dialog.bindCommand = hotkey.command;
        dialog.fields.reserve(hotkey.inputs.size());
        for (const HotkeyInput& input : hotkey.inputs) {
            InputDialogField field;
            field.input = input;
            dialog.fields.push_back(std::move(field));
        }
        inputDialog = std::move(dialog);
        hotkey.awaitingInput = true;
        return true;
    }

    if (hotkey.messages.empty()) {
        return false;
    }

    StartRunningBind(hotkey, {}, startDelayMs, std::string(source), sourceText, hotkey.command);
    hotkey.awaitingInput = false;
    return true;
}

void BinderModule::Impl::SendExpandedText(
    const std::string& expandedText,
    int method,
    const TagsModule::CursorIntents* cursorIntents) {
    switch (method) {
    case 0: {
        const std::string decoratedText = DecorateDialogLocalChatText(expandedText, sampApi);
        const auto [messageText, color] = ParseLeadingChatColor(decoratedText);
        if (!sampApi || !sampApi->memoryAddMessageSamp(messageText, color, true)) {
            Notify(NotificationGroup::BinderErrors, NotificationSeverity::Error, UiSettings::Instance().Text(UiText::ToastSendLocalFailed), 2500.0);
        }
        break;
    }
    case 1:
    {
        RegisterOutgoingGuard(!expandedText.empty() && expandedText.front() == '/' ? "command" : "chat", expandedText);
        RegisterOutgoingGuard("echo", NormalizeTriggerText(expandedText));
        if (!sampApi || !sampApi->process_chat_input(expandedText, true)) {
            Notify(NotificationGroup::BinderErrors, NotificationSeverity::Error, UiSettings::Instance().Text(UiText::ToastSendSampFailed), 2500.0);
        }
        break;
    }
    case 2:
    {
        const auto previewText = [](std::string_view value) {
            constexpr std::size_t kMaxPreviewLength = 96;
            if (value.size() <= kMaxPreviewLength) {
                return std::string(value);
            }
            return std::string(value.substr(0, kMaxPreviewLength - 3)) + "...";
        };
        const char* const sendKind = !expandedText.empty() && expandedText.front() == '/' ? "command" : "chat";
        const std::string preview = previewText(expandedText);
        debuglog::WriteInfo(
            "Binder DoSend via_samp begin kind=%s len=%llu text=%s",
            sendKind,
            static_cast<unsigned long long>(expandedText.size()),
            preview.c_str());
        RegisterOutgoingGuard(sendKind, expandedText);
        RegisterOutgoingGuard("echo", NormalizeTriggerText(expandedText));
        const bool ok = sampApi && sampApi->send_chat(expandedText, true);
        if (!ok) {
            const std::string error = sampApi ? sampApi->lastError() : "SampApi is null";
            debuglog::WriteError(
                "Binder DoSend via_samp failed kind=%s len=%llu text=%s error=%s",
                sendKind,
                static_cast<unsigned long long>(expandedText.size()),
                preview.c_str(),
                error.c_str());
            Notify(NotificationGroup::BinderErrors, NotificationSeverity::Error, UiSettings::Instance().Text(UiText::ToastSendSampFailed), 2500.0);
        } else {
            debuglog::WriteInfo(
                "Binder DoSend via_samp ok kind=%s len=%llu text=%s",
                sendKind,
                static_cast<unsigned long long>(expandedText.size()),
                preview.c_str());
        }
        break;
    }
    case 3:
        break;
    case 4:
        if (!sampApi || !sampApi->Set_ChatInputText(expandedText, false, true)) {
            Notify(NotificationGroup::BinderErrors, NotificationSeverity::Error, UiSettings::Instance().Text(UiText::ToastInsertChatFailed), 2500.0);
        } else if (cursorIntents) {
            if (cursorIntents->sampChat.valid
                && !sampApi->sampSetChatInputCursor(cursorIntents->sampChat.start, cursorIntents->sampChat.finish)) {
                debuglog::WriteError(
                    "Binder chat cursor failed method=%d start=%d finish=%d error=%s",
                    method,
                    cursorIntents->sampChat.start,
                    cursorIntents->sampChat.finish,
                    sampApi->lastError().c_str());
            }
            if (cursorIntents->arizonaChat.valid
                && !sampApi->SetChatAsiInputCursor(cursorIntents->arizonaChat.start, cursorIntents->arizonaChat.finish)) {
                debuglog::WriteError(
                    "Binder ARZ chat cursor failed method=%d start=%d finish=%d error=%s",
                    method,
                    cursorIntents->arizonaChat.start,
                    cursorIntents->arizonaChat.finish,
                    sampApi->lastError().c_str());
            }
        }
        break;
    case 5:
        if (!sampApi || !sampApi->Set_ChatInputText(expandedText, true, true)) {
            Notify(NotificationGroup::BinderErrors, NotificationSeverity::Error, UiSettings::Instance().Text(UiText::ToastOpenChatFailed), 2500.0);
        } else if (cursorIntents) {
            if (cursorIntents->sampChat.valid
                && !sampApi->sampSetChatInputCursor(cursorIntents->sampChat.start, cursorIntents->sampChat.finish)) {
                debuglog::WriteError(
                    "Binder chat cursor failed method=%d start=%d finish=%d error=%s",
                    method,
                    cursorIntents->sampChat.start,
                    cursorIntents->sampChat.finish,
                    sampApi->lastError().c_str());
            }
            if (cursorIntents->arizonaChat.valid
                && !sampApi->SetChatAsiInputCursor(cursorIntents->arizonaChat.start, cursorIntents->arizonaChat.finish)) {
                debuglog::WriteError(
                    "Binder ARZ chat cursor failed method=%d start=%d finish=%d error=%s",
                    method,
                    cursorIntents->arizonaChat.start,
                    cursorIntents->arizonaChat.finish,
                    sampApi->lastError().c_str());
            }
        }
        break;
    case 6:
        if (!tagsModule) {
            Notify(NotificationGroup::SampDialogErrors, NotificationSeverity::Error, UiSettings::Instance().Text(UiText::ToastInsertDialogFailed), 2500.0);
            break;
        }
        {
            const TagsModule::DialogInputSetResult result =
                tagsModule->SetActiveDialogInputTextAuto(expandedText, cursorIntents, true);
            if (!result.ok) {
                debuglog::WriteError("Binder dialog input auto-send failed: %s", result.error.c_str());
                Notify(NotificationGroup::SampDialogErrors, NotificationSeverity::Error, UiSettings::Instance().Text(UiText::ToastInsertDialogFailed), 2500.0);
            }
        }
        break;
    case 7:
        if (!SetClipboardUtf8Text(expandedText)) {
            Notify(NotificationGroup::BinderErrors, NotificationSeverity::Error, UiSettings::Instance().Text(UiText::ToastClipboardFailed), 2500.0);
        }
        break;
    case 8:
        debuglog::WriteInfo("Binder log: %s", expandedText.c_str());
        break;
    case 9:
        ShowUserPopup(expandedText, 2200.0);
        break;
    default:
        Notify(
            NotificationGroup::BinderErrors,
            NotificationSeverity::Error,
            UiSettings::Instance().Format(UiText::ToastUnknownSendMethod, method),
            2500.0);
        break;
    }
}

bool BinderModule::Impl::DoSend(const std::string& text, int method, std::uint64_t sourceRuntimeId) {
    TagsModule::ExpandedText expandedWithCursor;
    const TagsModule::CursorIntents* cursorIntents = nullptr;
    std::string expandedText;
    const bool cursorInputMethod = method == 4 || method == 5 || method == 6;
    if (cursorInputMethod && tagsModule) {
        expandedWithCursor = tagsModule->ExpandTextWithCursorIntents(text);
        expandedText = expandedWithCursor.text;
        cursorIntents = &expandedWithCursor.cursors;
    } else {
        const bool expandTags = method == 0 || method == 1 || method == 2 || method == 3 || method == 7 || method == 8 || method == 9;
        expandedText = expandTags && tagsModule ? tagsModule->ExpandText(text) : text;
    }
    if (tagsModule && sourceRuntimeId != 0 && tagsModule->ConsumeCurrentDispatchBlocked(sourceRuntimeId)) {
        return false;
    }

    if ((method == 1 || method == 2)
        && QueueCommandDispatchFromRunningBind(expandedText, sourceRuntimeId, method)) {
        if (method == 2) {
            constexpr std::size_t kMaxPreviewLength = 96;
            const std::string preview = expandedText.size() <= kMaxPreviewLength
                ? expandedText
                : std::string(expandedText.substr(0, kMaxPreviewLength - 3)) + "...";
            debuglog::WriteInfo(
                "Binder DoSend local command dispatch queued len=%llu text=%s",
                static_cast<unsigned long long>(expandedText.size()),
                preview.c_str());
        }
        return true;
    }

    SendExpandedText(expandedText, method, cursorIntents);
    return true;
}
