#include "binder_module_impl.h"

namespace {
bool EditableMessagesEqual(const std::vector<HotkeyMessage>& left, const std::vector<HotkeyMessage>& right) {
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(), [](const HotkeyMessage& a, const HotkeyMessage& b) {
               return a.text == b.text && a.intervalMs == b.intervalMs && a.method == b.method;
           });
}

bool EditableButtonsEqual(const std::vector<InputButton>& left, const std::vector<InputButton>& right) {
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(), [](const InputButton& a, const InputButton& b) {
               return a.label == b.label && a.text == b.text && a.hint == b.hint && a.when == b.when;
           });
}

bool EditableInputsEqual(const std::vector<HotkeyInput>& left, const std::vector<HotkeyInput>& right) {
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(), [](const HotkeyInput& a, const HotkeyInput& b) {
               return a.key == b.key
                   && a.label == b.label
                   && a.hint == b.hint
                   && a.mode == b.mode
                   && EditableButtonsEqual(a.buttons, b.buttons)
                   && a.multiSelect == b.multiSelect
                   && a.multiSeparator == b.multiSeparator
                   && a.cascadeParentKey == b.cascadeParentKey;
           });
}

bool EditableHotkeysEqual(const HotkeyEntry& left, const HotkeyEntry& right) {
    return left.label == right.label
        && left.iconId == right.iconId
        && left.keys == right.keys
        && left.hotkeyMode == right.hotkeyMode
        && EditableMessagesEqual(left.messages, right.messages)
        && EditableInputsEqual(left.inputs, right.inputs)
        && left.textTrigger.text == right.textTrigger.text
        && left.textTrigger.enabled == right.textTrigger.enabled
        && left.textTrigger.pattern == right.textTrigger.pattern
        && left.textConfirmation.enabled == right.textConfirmation.enabled
        && left.textConfirmation.key == right.textConfirmation.key
        && left.textConfirmation.cancelKey == right.textConfirmation.cancelKey
        && left.textConfirmation.waitForResolution == right.textConfirmation.waitForResolution
        && left.commandConfirmation.enabled == right.commandConfirmation.enabled
        && left.commandConfirmation.waitForResolution == right.commandConfirmation.waitForResolution
        && left.conditions == right.conditions
        && left.conditionsCombine == right.conditionsCombine
        && left.repeatMode == right.repeatMode
        && left.repeatIntervalMs == right.repeatIntervalMs
        && left.enabled == right.enabled
        && left.quickMenu == right.quickMenu
        && left.command == right.command
        && left.commandEnabled == right.commandEnabled
        && left.categoryId == right.categoryId
        && left.folderPath == right.folderPath
        && left.orderId == right.orderId;
}

void ClearEditorVariableInsertTarget(binder_editor::State& editor) {
    editor.variablesInsertTarget = binder_editor::State::VariableInsertTarget::None;
    editor.variablesInsertMessageIndex = -1;
    editor.variablesInsertCursorByte = -1;
    editor.variablesInsertSelectionStartByte = -1;
    editor.variablesInsertSelectionEndByte = -1;
}

void SetNextResponsiveEditorPopupSize(const ImVec2& preferredSize, const ImVec2& minimumSize) {
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImVec2 margin = ScaleUi(24.0f, 24.0f);
    const ImVec2 maximumSize(
        std::max(ScaleUi(320.0f), displaySize.x - margin.x),
        std::max(ScaleUi(240.0f), displaySize.y - margin.y));
    const ImVec2 clampedMinimum(
        std::min(minimumSize.x, maximumSize.x),
        std::min(minimumSize.y, maximumSize.y));
    ImGui::SetNextWindowSizeConstraints(clampedMinimum, maximumSize);
    ImGui::SetNextWindowSize(
        ImVec2(std::min(preferredSize.x, maximumSize.x), std::min(preferredSize.y, maximumSize.y)),
        ImGuiCond_Appearing);
}
} // namespace

void BinderModule::Impl::StartEditing(int index, bool isNew) {
    editor = {};
    editor.active = true;
    editor.isNew = isNew;
    editor.hotkeyIndex = index;
    editor.selectedInputIndex = -1;
    editor.selectedInputButtonIndex = -1;
    editor.draft = (isNew || index < 0 || index >= static_cast<int>(hotkeys.size())) ? MakeDefaultHotkey() : hotkeys[index];
    editor.draft.comboActive = false;
    editor.draft.awaitingInput = false;
    editor.draft.waitingTextConfirmation = false;
    editor.draft.textConfirmationDeadlineMs = 0.0;
    editor.draft.lastRepeatPressed.clear();
    editor.draft.pendingConfirmationKey = kDefaultConfirmKey;
    editor.draft.pendingConfirmationCancelKey = kDefaultCancelKey;
    editor.draft.pendingTriggerText.clear();
    editor.draft.pendingTriggerSource.clear();
    editor.draft.lastActivatedAtMs = 0.0;
    editor.draft.debounceUntilMs = 0.0;
    editor.draft.textTrigger.InvalidateRuntimeCache();

    if (editor.isNew) {
        editor.draft.folderPath = CurrentFolderPath();
        editor.draft.label.clear();
    }

    binder_editor::NormalizeDraftForOpen(editor);
    editor.draft = binder_editor::BuildComparableDraft(editor);

    editor.inputButtonsBulkDrafts.reserve(editor.draft.inputs.size());
    for (const HotkeyInput& input : editor.draft.inputs) {
        editor.inputButtonsBulkDrafts.push_back(SerializeButtonsText(input.buttons));
    }

    if (!editor.draft.inputs.empty()) {
        editor.selectedInputIndex = 0;
        editor.selectedButtonsText = editor.inputButtonsBulkDrafts[0];
        editor.selectedInputButtonIndex = editor.draft.inputs[0].buttons.empty() ? -1 : 0;
    }

    if (!editor.draft.messages.empty()) {
        editor.bulkMethod = editor.draft.messages.front().method;
        editor.bulkIntervalMs = std::max(editor.draft.messages.front().intervalMs, 0);
    }

    SyncEditorMessagesToMulti();
    editor.activeTab = binder_editor::State::Tab::Scenario;
    editor.tabSelectionPending = true;
    editor.baseline = editor.draft;
    editor.focusNamePending = true;
    editor.pendingAction = binder_editor::State::PendingAction::None;
    editor.pendingTargetIndex = -1;
    if (index >= 0) {
        SelectExplorerBind(index);
    } else {
        ClearExplorerSelection();
    }
}

std::vector<HotkeyMessage> BinderModule::Impl::ParseEditorMultiMessages(const std::vector<HotkeyMessage>& reference) const {
    std::vector<HotkeyMessage> messages;
    std::istringstream stream(NormalizeLineEndings(editor.multiText));
    std::string line;
    std::size_t lineIndex = 0;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }

        HotkeyMessage message;
        message.text = line;
        message.intervalMs = std::max(editor.bulkIntervalMs, 0);
        message.method = editor.bulkMethod;
        if (lineIndex < reference.size() && reference[lineIndex].text == line) {
            message.intervalMs = std::max(reference[lineIndex].intervalMs, 0);
            message.method = reference[lineIndex].method;
        }
        messages.push_back(std::move(message));
        ++lineIndex;
    }

    const bool allReferenceEmpty = !reference.empty() && std::all_of(reference.begin(), reference.end(), [](const HotkeyMessage& item) {
        return Trim(item.text).empty();
    });
    if (messages.empty() && allReferenceEmpty) {
        return reference;
    }

    return messages;
}

void BinderModule::Impl::SyncEditorMessagesToMulti() {
    std::ostringstream stream;
    for (std::size_t i = 0; i < editor.draft.messages.size(); ++i) {
        if (i != 0) {
            stream << '\n';
        }
        stream << editor.draft.messages[i].text;
    }
    editor.multiText = stream.str();
    if (!editor.draft.messages.empty()) {
        editor.bulkMethod = editor.draft.messages.front().method;
        editor.bulkIntervalMs = std::max(editor.draft.messages.front().intervalMs, 0);
    }
}

void BinderModule::Impl::ApplyEditorMultiToDraft(bool applyBulkToAll) {
    editor.draft.messages = ParseEditorMultiMessages(editor.draft.messages);
    if (applyBulkToAll) {
        for (HotkeyMessage& message : editor.draft.messages) {
            message.intervalMs = std::max(editor.bulkIntervalMs, 0);
            message.method = editor.bulkMethod;
        }
    }
    ClearEditorVariableInsertTarget(editor);
    editor.scenarioMessageFocusIndex = -1;
    editor.scenarioMessageFocusCaretByte = -1;
}

HotkeyEntry BinderModule::Impl::BuildEditorComparableDraft() const {
    return binder_editor::BuildComparableDraft(editor);
}

bool BinderModule::Impl::EditorHasUnsavedChanges() const {
    if (!editor.active) {
        return false;
    }

    return !EditableHotkeysEqual(BuildEditorComparableDraft(), editor.baseline);
}

std::pair<int, int> BinderModule::Impl::EditorNeighborIndices() const {
    if (editor.hotkeyIndex < 0 || editor.hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        return { -1, -1 };
    }

    const HotkeyEntry& current = hotkeys[static_cast<std::size_t>(editor.hotkeyIndex)];
    const BinderCategory* category = FindCategoryById(current.categoryId);
    if (!category) {
        return { -1, -1 };
    }

    FolderNode* folder = current.folderPath.empty() ? nullptr : FindFolderByPath(category->folders, current.folderPath);
    if (!current.folderPath.empty() && !folder) {
        return { -1, -1 };
    }

    const std::vector<ExplorerItem>& items = folder ? folder->items : category->rootItems;
    int previous = -1;
    int next = -1;
    bool currentFound = false;
    for (const ExplorerItem& item : items) {
        if (item.kind != ExplorerItemKind::Bind) {
            continue;
        }

        const int index = FindHotkeyIndexByOrderId(item.key);
        if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
            continue;
        }

        const HotkeyEntry& candidate = hotkeys[static_cast<std::size_t>(index)];
        if (candidate.categoryId != current.categoryId || candidate.folderPath != current.folderPath) {
            continue;
        }

        if (index == editor.hotkeyIndex) {
            currentFound = true;
        } else if (!currentFound) {
            previous = index;
        } else {
            next = index;
            break;
        }
    }
    return currentFound ? std::pair<int, int>{ previous, next } : std::pair<int, int>{ -1, -1 };
}

void BinderModule::Impl::RequestEditorAction(binder_editor::State::PendingAction action, int targetIndex) {
    if (!editor.active) {
        return;
    }

    if (action == binder_editor::State::PendingAction::Navigate
        && (targetIndex < 0 || targetIndex >= static_cast<int>(hotkeys.size()))) {
        return;
    }

    editor.pendingAction = action;
    editor.pendingTargetIndex = targetIndex;
    if (EditorHasUnsavedChanges()) {
        editor.discardPopupPending = true;
        return;
    }

    ExecuteEditorPendingAction();
}

void BinderModule::Impl::ExecuteEditorPendingAction() {
    const binder_editor::State::PendingAction action = editor.pendingAction;
    const int targetIndex = editor.pendingTargetIndex;

    editor.pendingAction = binder_editor::State::PendingAction::None;
    editor.pendingTargetIndex = -1;
    editor.discardPopupPending = false;

    if (capturePopupInEditor) {
        capture.Stop();
        hotkeys::ResetCapturePopupState(capturePopupState);
        captureTarget = CaptureTarget::None;
        captureHotkeyIndex = -1;
        capturePopupInEditor = false;
    }

    switch (action) {
    case binder_editor::State::PendingAction::Close:
        editor = {};
        return;
    case binder_editor::State::PendingAction::Navigate:
        if (targetIndex >= 0 && targetIndex < static_cast<int>(hotkeys.size())) {
            StartEditing(targetIndex, false);
        }
        return;
    case binder_editor::State::PendingAction::None:
        return;
    }
}

bool BinderModule::Impl::ValidateEditor(std::vector<std::string>& errors) {
    UiSettings& ui = UiSettings::Instance();
    HotkeyEntry current = BuildEditorComparableDraft();
    binder_editor::NormalizeDraftForSave(current);
    const std::string triggerText = Trim(current.textTrigger.text);
    if (!HasRequiredFirstMessage(current)) {
        errors.push_back(ui.Text(UiText::ValidationFirstMessageRequired));
    }

    const BinderCategory* editorCategory = FindCategoryById(current.categoryId);
    if (!editorCategory) {
        editorCategory = &ActiveCategory();
    }
    if (!current.folderPath.empty() && !FindFolderByPath(editorCategory->folders, current.folderPath)) {
        errors.push_back(ui.Text(UiText::ValidationExistingFolderRequired));
    }

    if (current.repeatMode && current.repeatIntervalMs < kMinMessageIntervalMs) {
        errors.push_back(ui.Text(UiText::ValidationRepeatInterval));
    }

    if ((current.textConfirmation.enabled || current.commandConfirmation.enabled)
        && current.textConfirmation.key == current.textConfirmation.cancelKey) {
        errors.push_back(ui.Text(UiText::ValidationConfirmCancelKeysDifferent));
    }

    if (current.enabled && !current.keys.empty()) {
        if (std::string description; DescribeConflictWithMenuToggleHotkey(current.keys, current.hotkeyMode, description)) {
            errors.push_back(ui.Format(UiText::HotkeyConflictFormat, description.c_str()));
        }
    }

    std::set<std::string> inputKeys;
    for (const HotkeyInput& input : current.inputs) {
        const std::string key = NormalizeInputKey(input.key);
        if (key.empty()) {
            errors.push_back(ui.Text(UiText::ValidationInputKeyRequired));
            continue;
        }
        if (!inputKeys.insert(key).second) {
            errors.push_back(ui.Text(UiText::ValidationInputKeyUnique));
        }
        if (InputModeUsesButtons(input.mode)) {
            if (input.buttons.empty()) {
                errors.push_back(ui.Text(UiText::ValidationButtonsRequired));
                continue;
            }

            const bool hasValueText = std::any_of(input.buttons.begin(), input.buttons.end(), [](const InputButton& button) {
                return !Trim(button.text).empty();
            });
            if (!hasValueText) {
                errors.push_back(ui.Text(UiText::ValidationButtonsTextRequired));
            }
        }
    }

    if (current.textTrigger.enabled && current.textTrigger.pattern && !triggerText.empty()) {
        text_pattern::CompileResult compiled = text_pattern::Compile(triggerText, false);
        if (!compiled.program) {
            errors.push_back(ui.Format(
                UiText::ValidationInvalidRegex,
                text_pattern_ui::FormatCompilePosition(triggerText, compiled.errorOffset).c_str()));
        }
    }

    return errors.empty();
}

void BinderModule::Impl::SaveEditor() {
    const bool replacingExisting =
        !editor.isNew && editor.hotkeyIndex >= 0 && editor.hotkeyIndex < static_cast<int>(hotkeys.size());
    const std::string previousOrderId = replacingExisting
        ? hotkeys[static_cast<std::size_t>(editor.hotkeyIndex)].orderId
        : std::string{};
    const std::vector<std::string> previousFolderPath = replacingExisting
        ? hotkeys[static_cast<std::size_t>(editor.hotkeyIndex)].folderPath
        : std::vector<std::string>{};
    HotkeyEntry saved = BuildEditorComparableDraft();
    saved.label = Trim(saved.label);
    saved.keys = ::hotkeys::NormalizeCombo(saved.keys, saved.hotkeyMode);
    saved.command = Trim(saved.command);
    saved.textTrigger.text = Trim(saved.textTrigger.text);
    binder_editor::NormalizeDraftForSave(saved);
    saved.textTrigger.InvalidateRuntimeCache();
    saved.conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    saved.conditionsCombine = ConditionCombineMode::RequireAny;
    saved.comboActive = false;
    saved.awaitingInput = false;
    saved.waitingTextConfirmation = false;
    saved.textConfirmationDeadlineMs = 0.0;
    saved.lastRepeatPressed.clear();
    saved.pendingConfirmationKey = kDefaultConfirmKey;
    saved.pendingConfirmationCancelKey = kDefaultCancelKey;
    saved.pendingTriggerText.clear();
    saved.pendingTriggerSource.clear();
    saved.lastActivatedAtMs = 0.0;
    saved.debounceUntilMs = 0.0;
    if (saved.categoryId.empty() || !FindCategoryById(saved.categoryId)) {
        saved.categoryId = ActiveCategory().id;
    }
    if (replacingExisting) {
        saved.runtimeId = hotkeys[static_cast<std::size_t>(editor.hotkeyIndex)].runtimeId;
        saved.orderId = previousOrderId;
    }
    if (saved.orderId.empty()) {
        saved.orderId = AllocateHotkeyOrderId();
    }

    if (editor.isNew || editor.hotkeyIndex < 0 || editor.hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        hotkeys.push_back(std::move(saved));
        selectedBindIndex = static_cast<int>(hotkeys.size() - 1);
    } else {
        hotkeys[editor.hotkeyIndex] = std::move(saved);
        selectedBindIndex = editor.hotkeyIndex;
    }

    if (selectedBindIndex >= 0 && selectedBindIndex < static_cast<int>(hotkeys.size())) {
        HotkeyEntry& selected = hotkeys[static_cast<std::size_t>(selectedBindIndex)];
        BinderCategory* category = FindCategoryById(selected.categoryId);
        if (!category) {
            selected.categoryId = ActiveCategory().id;
            category = &ActiveCategory();
        }
        if (category->id != ActiveCategory().id) {
            SelectCategory(category->id);
        }
        FolderNode* folder = selected.folderPath.empty() ? nullptr : FindFolderByPath(category->folders, selected.folderPath);
        if (!selected.folderPath.empty() && !folder) {
            selected.folderPath.clear();
        }
        if (!replacingExisting || previousFolderPath != selected.folderPath) {
            RemoveBindFromExplorerOrders(selected.orderId);
        }
        AppendExplorerItemIfMissing(folder, ExplorerItem{ ExplorerItemKind::Bind, selected.orderId });
        currentFolder = folder;
        SelectExplorerBind(selectedBindIndex, true);
    }

    RefreshNumbers();
    SaveConfig();
    editor = {};
}

bool BinderModule::Impl::CopyTextToClipboard(std::string_view text, bool showSuccessToast) {
    if (!SetClipboardUtf8Text(text)) {
        Notify(
            NotificationGroup::BinderErrors,
            NotificationSeverity::Error,
            UiSettings::Instance().Text(UiText::ToastClipboardFailed),
            2500.0);
        return false;
    }

    if (showSuccessToast) {
        Notify(
            NotificationGroup::Success,
            NotificationSeverity::Success,
            UiSettings::Instance().Text(UiText::ToastClipboardCopied),
            1400.0);
    }
    return true;
}

std::vector<variables_picker::Entry> BinderModule::Impl::BuildEditorVariablePickerEntries() const {
    UiSettings& ui = UiSettings::Instance();
    std::vector<variables_picker::Entry> pickerEntries;
    if (tagsModule) {
        pickerEntries.reserve(tagsModule->CatalogEntries().size() + tagsModule->CustomVariables().size() + editor.draft.inputs.size());
    }

    for (std::size_t i = 0; i < editor.draft.inputs.size(); ++i) {
        const HotkeyInput& input = editor.draft.inputs[i];
        const std::string normalizedKey = NormalizeInputKey(input.key);
        const std::string indexToken = "{{" + std::to_string(i + 1) + "}}";
        const std::string primaryToken = normalizedKey.empty() ? indexToken : "{{" + normalizedKey + "}}";
        const std::string label = input.label.empty() ? std::string(ui.Text(UiText::UnnamedField)) : input.label;

        variables_picker::Entry entry;
        entry.kind = variables_picker::EntryKind::Parameter;
        entry.category = variables_picker::Category::Parameters;
        entry.id = variables_picker::MakeEntryId(variables_picker::EntryKind::Parameter, primaryToken);
        entry.name = normalizedKey.empty() ? std::to_string(i + 1) : normalizedKey;
        entry.token = primaryToken;
        entry.example = !normalizedKey.empty() && primaryToken != indexToken ? indexToken : primaryToken;
        entry.description = input.hint.empty()
            ? ui.Format(UiText::InputFieldPlaceholderFormat, entry.name.c_str())
            : label + "\n" + input.hint;
        pickerEntries.push_back(std::move(entry));
    }

    if (!tagsModule) {
        return pickerEntries;
    }

    for (const auto& entry : tagsModule->CatalogEntries()) {
        const variables_picker::EntryKind kind = entry.kind == TagsModule::TagKind::Function
            ? variables_picker::EntryKind::Function
            : variables_picker::EntryKind::Simple;
        variables_picker::Entry pickerEntry;
        pickerEntry.kind = kind;
        pickerEntry.category = TagsModule::ToPickerCategory(entry.category);
        pickerEntry.id = variables_picker::MakeEntryId(kind, entry.token);
        pickerEntry.name = entry.name;
        pickerEntry.token = entry.token;
        pickerEntry.example = entry.example;
        pickerEntry.descriptionText = entry.descriptionText;
        pickerEntry.description = entry.description;
        pickerEntry.action = entry.action;
        pickerEntries.push_back(std::move(pickerEntry));
    }

    for (const auto& [name, value] : tagsModule->CustomVariables()) {
        variables_picker::Entry entry;
        entry.kind = variables_picker::EntryKind::Custom;
        entry.category = variables_picker::Category::Custom;
        entry.id = variables_picker::MakeEntryId(variables_picker::EntryKind::Custom, name);
        entry.name = name;
        entry.token = "{" + name + "}";
        entry.example = entry.token;
        entry.value = value;
        entry.description = value;
        pickerEntries.push_back(std::move(entry));
    }

    return pickerEntries;
}

void BinderModule::Impl::RememberEditorVariableInsertTarget(
    binder_editor::State::VariableInsertTarget target,
    int messageIndex,
    int cursorByte,
    int selectionStartByte,
    int selectionEndByte) {
    editor.variablesInsertTarget = target;
    editor.variablesInsertMessageIndex = messageIndex;
    editor.variablesInsertCursorByte = cursorByte;
    editor.variablesInsertSelectionStartByte = selectionStartByte;
    editor.variablesInsertSelectionEndByte = selectionEndByte;
}

bool BinderModule::Impl::InsertTextIntoEditorVariableTarget(std::string_view text) {
    const auto replaceSelection = [&](std::string& value) {
        const int fallbackCursor = editor.variablesInsertCursorByte < 0
            ? static_cast<int>(value.size())
            : std::clamp(editor.variablesInsertCursorByte, 0, static_cast<int>(value.size()));
        const bool hasSelection = editor.variablesInsertSelectionStartByte >= 0
            && editor.variablesInsertSelectionEndByte >= 0;
        const int selectionStart = hasSelection
            ? std::clamp(
                  std::min(editor.variablesInsertSelectionStartByte, editor.variablesInsertSelectionEndByte),
                  0,
                  static_cast<int>(value.size()))
            : fallbackCursor;
        const int selectionEnd = hasSelection
            ? std::clamp(
                  std::max(editor.variablesInsertSelectionStartByte, editor.variablesInsertSelectionEndByte),
                  selectionStart,
                  static_cast<int>(value.size()))
            : fallbackCursor;

        value.replace(
            static_cast<std::size_t>(selectionStart),
            static_cast<std::size_t>(selectionEnd - selectionStart),
            text.data(),
            text.size());
        editor.variablesInsertCursorByte = selectionStart + static_cast<int>(text.size());
        editor.variablesInsertSelectionStartByte = editor.variablesInsertCursorByte;
        editor.variablesInsertSelectionEndByte = editor.variablesInsertCursorByte;
    };

    switch (editor.variablesInsertTarget) {
    case binder_editor::State::VariableInsertTarget::ScenarioMessage:
        if (editor.variablesInsertMessageIndex < 0
            || editor.variablesInsertMessageIndex >= static_cast<int>(editor.draft.messages.size())) {
            return false;
        }
        replaceSelection(editor.draft.messages[static_cast<std::size_t>(editor.variablesInsertMessageIndex)].text);
        editor.scenarioMessageFocusIndex = editor.variablesInsertMessageIndex;
        editor.scenarioMessageFocusCaretByte = editor.variablesInsertCursorByte;
        return true;
    case binder_editor::State::VariableInsertTarget::ScenarioAppend:
        replaceSelection(editor.scenarioAppendText);
        return CommitEditorScenarioAppendText();
    case binder_editor::State::VariableInsertTarget::None:
    default:
        return false;
    }
}

bool BinderModule::Impl::CommitEditorScenarioAppendText() {
    std::vector<std::string> lines;
    std::istringstream stream(NormalizeLineEndings(editor.scenarioAppendText));
    std::string line;
    while (std::getline(stream, line)) {
        if (!Trim(line).empty()) {
            lines.push_back(std::move(line));
        }
    }
    if (lines.empty()) {
        return false;
    }

    HotkeyMessage inherited;
    if (!editor.draft.messages.empty()) {
        inherited = editor.draft.messages.back();
    } else {
        inherited.intervalMs = std::max(editor.bulkIntervalMs, 0);
        inherited.method = editor.bulkMethod;
    }

    const bool replacePlaceholder =
        editor.draft.messages.size() == 1 && Trim(editor.draft.messages.front().text).empty();
    int lastAddedIndex = -1;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        HotkeyMessage message;
        message.text = std::move(lines[i]);
        message.intervalMs = std::max(inherited.intervalMs, 0);
        message.method = inherited.method;

        if (replacePlaceholder && i == 0) {
            editor.draft.messages.front() = std::move(message);
            lastAddedIndex = 0;
        } else {
            editor.draft.messages.push_back(std::move(message));
            lastAddedIndex = static_cast<int>(editor.draft.messages.size()) - 1;
        }
    }

    editor.scenarioAppendText.clear();
    editor.scenarioMessageFocusIndex = lastAddedIndex;
    editor.scenarioMessageFocusCaretByte = static_cast<int>(
        editor.draft.messages[static_cast<std::size_t>(lastAddedIndex)].text.size());
    return true;
}

void BinderModule::Impl::HandleEditorVariablePickerRequest(const variables_picker::Request& request) {
    switch (request.type) {
    case variables_picker::RequestType::Copy:
        CopyTextToClipboard(request.text);
        break;
    case variables_picker::RequestType::Insert:
        if (!InsertTextIntoEditorVariableTarget(request.text)) {
            CopyTextToClipboard(request.text);
        }
        break;
    case variables_picker::RequestType::OpenKeyEmulatePicker:
        editor.variablesKeyPickerPopupPending = true;
        break;
    case variables_picker::RequestType::OpenDialogItemPicker:
        if (tagsModule) {
            tagsModule->OpenDialogItemPicker();
        }
        break;
    case variables_picker::RequestType::OpenArizonaDialogItemPicker:
        if (tagsModule) {
            tagsModule->OpenArizonaDialogItemPicker();
        }
        break;
    case variables_picker::RequestType::OpenDialogTextPicker:
        if (tagsModule) {
            tagsModule->OpenSampDialogTextPicker();
        }
        break;
    case variables_picker::RequestType::OpenArizonaDialogTextPicker:
        if (tagsModule) {
            tagsModule->OpenArizonaDialogTextPicker();
        }
        break;
    case variables_picker::RequestType::OpenBindSelectorBuilder:
        if (tagsModule) {
            tagsModule->OpenBindSelectorBuilder(request.name);
        }
        break;
    case variables_picker::RequestType::None:
    case variables_picker::RequestType::SaveCustom:
    case variables_picker::RequestType::DeleteCustom:
    default:
        break;
    }
}

bool BinderModule::Impl::DrawEditorVariableKeyPickerPopup() {
    if (!tagsModule) {
        return false;
    }

    UiSettings& ui = UiSettings::Instance();
    SetNextResponsiveEditorPopupSize(ScaleUi(560.0f, 520.0f), ScaleUi(400.0f, 320.0f));
    if (!ImGui::BeginPopup("##binder_editor_variable_keypicker")) {
        return false;
    }

    bool closeParentPopup = false;
    ImGui::TextUnformatted(ui.Text(UiText::MiscVariablesKeyPickerTitle));
    ImGui::TextWrapped("%s", ui.Text(UiText::MiscVariablesKeyPickerIntro));
    ImGui::Separator();

    InputTextWithHintString(
        "##binder_editor_variable_keypicker_search",
        ui.Text(UiText::MiscVariablesKeyPickerSearchHint),
        editor.variablesKeyPickerSearch,
        ImGuiInputTextFlags_AutoSelectAll,
        96);
    ImGui::Spacing();

    const std::string filter = ToLower(editor.variablesKeyPickerSearch);
    bool hasMatches = false;
    const float keyListHeight = std::max(
        ScaleUi(120.0f),
        std::min(ScaleUi(360.0f), ImGui::GetContentRegionAvail().y - ScaleUi(72.0f)));
    if (ImGui::BeginChild("##binder_editor_variable_keypicker_list", ImVec2(0.0f, keyListHeight), ImGuiChildFlags_Borders)) {
        for (const auto& entry : tagsModule->VirtualKeyPickerEntries()) {
            if (!filter.empty() && entry.search.find(filter) == std::string::npos) {
                continue;
            }

            hasMatches = true;
            if (ImGui::Selectable(entry.label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                const std::string token = TagsModule::MakeKeyEmulateToken(entry.code);
                if (!InsertTextIntoEditorVariableTarget(token)) {
                    CopyTextToClipboard(token);
                }
                editor.variablesKeyPickerSearch.clear();
                closeParentPopup = true;
                ImGui::CloseCurrentPopup();
            }
        }

        if (!hasMatches) {
            ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesKeyPickerEmpty));
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::TextDisabled("%s", ui.Text(UiText::EditorVariablesKeyPickerInsertHint));
    ImGui::Spacing();
    if (ImGui::Button(ui.Text(UiText::Cancel))) {
        editor.variablesKeyPickerSearch.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    return closeParentPopup;
}

void BinderModule::Impl::DrawInputEditor() {
    UiSettings& ui = UiSettings::Instance();
    const ImVec2 actionButtonSize = ScaleUi(25.0f, 25.0f);
    const auto drawTextTooltip = [](const char* text) {
        if (text && text[0] != '\0' && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s", text);
        }
    };

    auto ensureBulkStateStorage = [&]() {
        if (editor.inputButtonsBulkDrafts.size() < editor.draft.inputs.size()) {
            const std::size_t oldSize = editor.inputButtonsBulkDrafts.size();
            editor.inputButtonsBulkDrafts.resize(editor.draft.inputs.size());
            for (std::size_t i = oldSize; i < editor.draft.inputs.size(); ++i) {
                editor.inputButtonsBulkDrafts[i] = SerializeButtonsText(editor.draft.inputs[i].buttons);
            }
        } else if (editor.inputButtonsBulkDrafts.size() > editor.draft.inputs.size()) {
            editor.inputButtonsBulkDrafts.resize(editor.draft.inputs.size());
        }
    };

    auto selectInput = [&](int index) {
        ensureBulkStateStorage();
        if (index < 0 || index >= static_cast<int>(editor.draft.inputs.size())) {
            editor.selectedInputIndex = -1;
            editor.selectedInputButtonIndex = -1;
            editor.expandedInputHintIndex = -1;
            editor.selectedButtonsText.clear();
            return;
        }

        editor.selectedInputIndex = index;
        editor.expandedInputHintIndex = -1;
        editor.selectedButtonsText = editor.inputButtonsBulkDrafts[static_cast<std::size_t>(index)];
        editor.selectedInputButtonIndex = -1;
    };

    auto clampSelectedInput = [&]() {
        ensureBulkStateStorage();
        if (editor.draft.inputs.empty()) {
            editor.selectedInputIndex = -1;
            editor.selectedInputButtonIndex = -1;
            editor.expandedInputHintIndex = -1;
            editor.selectedButtonsText.clear();
            return;
        }

        if (editor.selectedInputIndex < 0 || editor.selectedInputIndex >= static_cast<int>(editor.draft.inputs.size())) {
            editor.selectedInputIndex = 0;
            editor.selectedButtonsText = editor.inputButtonsBulkDrafts.front();
        }

        const HotkeyInput& input = editor.draft.inputs[static_cast<std::size_t>(editor.selectedInputIndex)];
        if (input.buttons.empty()) {
            editor.selectedInputButtonIndex = -1;
        } else if (editor.selectedInputButtonIndex >= static_cast<int>(input.buttons.size())) {
            editor.selectedInputButtonIndex = -1;
        }
    };

    auto syncSelectedButtonsText = [&](const HotkeyInput& input) {
        ensureBulkStateStorage();
        if (editor.selectedInputIndex >= 0 && editor.selectedInputIndex < static_cast<int>(editor.inputButtonsBulkDrafts.size())) {
            const std::string text = SerializeButtonsText(input.buttons);
            editor.inputButtonsBulkDrafts[static_cast<std::size_t>(editor.selectedInputIndex)] = text;
            editor.selectedButtonsText = text;
        } else {
            editor.selectedButtonsText.clear();
        }
        if (input.buttons.empty()) {
            editor.selectedInputButtonIndex = -1;
        } else if (editor.selectedInputButtonIndex >= static_cast<int>(input.buttons.size())) {
            editor.selectedInputButtonIndex = -1;
        }
    };

    auto inputDisplayName = [&](const HotkeyInput& input, int index) {
        const std::string label = Trim(input.label);
        if (!label.empty()) {
            return label;
        }
        return ui.Format(UiText::FieldLabelFormat, index + 1);
    };
    struct InputKeyIssue {
        bool empty = false;
        bool invalid = false;
        bool duplicate = false;
        std::string normalized;
    };

    auto buildInputKeyIssues = [&]() {
        std::vector<InputKeyIssue> issues(editor.draft.inputs.size());
        std::map<std::string, int> usage;
        for (std::size_t i = 0; i < editor.draft.inputs.size(); ++i) {
            const std::string raw = Trim(editor.draft.inputs[i].key);
            const std::string normalized = NormalizeInputKey(raw);
            issues[i].empty = raw.empty();
            issues[i].invalid = !raw.empty() && normalized != raw;
            issues[i].normalized = normalized;
            if (!normalized.empty()) {
                ++usage[normalized];
            }
        }
        for (std::size_t i = 0; i < issues.size(); ++i) {
            if (!issues[i].normalized.empty() && usage[issues[i].normalized] > 1) {
                issues[i].duplicate = true;
            }
        }
        return issues;
    };

    if (ImGui::Button(ui.Text(UiText::AddField))) {
        HotkeyInput input;
        input.key = "FIELD_" + std::to_string(editor.draft.inputs.size() + 1);
        input.label = ui.Format(UiText::FieldLabelFormat, static_cast<int>(editor.draft.inputs.size() + 1));
        editor.draft.inputs.push_back(std::move(input));
        editor.inputButtonsBulkDrafts.push_back({});
        selectInput(static_cast<int>(editor.draft.inputs.size() - 1));
    }
    if (editor.draft.inputs.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", ui.Text(UiText::InputFieldsEmpty));
        return;
    }

    clampSelectedInput();
    std::vector<InputKeyIssue> inputIssues = buildInputKeyIssues();

    ImGui::SameLine();
    const int currentIndexBeforeActions = editor.selectedInputIndex;
    const std::string currentTitle = inputDisplayName(
        editor.draft.inputs[static_cast<std::size_t>(currentIndexBeforeActions)],
        currentIndexBeforeActions);
    const std::string selectorPreview = ui.Format(
        UiText::ParameterSelectorFormat,
        currentTitle.c_str(),
        currentIndexBeforeActions + 1,
        static_cast<int>(editor.draft.inputs.size()));
    const float selectorWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.45f, ScaleUi(210.0f), ScaleUi(440.0f));
    ImGui::SetNextItemWidth(selectorWidth);
    if (ImGui::BeginCombo("##binder_input_selector", selectorPreview.c_str())) {
        for (std::size_t i = 0; i < editor.draft.inputs.size(); ++i) {
            const std::string itemTitle = inputDisplayName(editor.draft.inputs[i], static_cast<int>(i));
            const std::string itemLabel = ui.Format(
                UiText::ParameterSelectorFormat,
                itemTitle.c_str(),
                static_cast<int>(i + 1),
                static_cast<int>(editor.draft.inputs.size()));
            if (ImGui::Selectable(itemLabel.c_str(), editor.selectedInputIndex == static_cast<int>(i))) {
                selectInput(static_cast<int>(i));
            }
        }
        ImGui::EndCombo();
    }

    int currentIndex = editor.selectedInputIndex;
    ImGui::SameLine();
    ImGui::BeginDisabled(currentIndex <= 0);
    const bool moveInputUp = SmallIconActionButton(ui_icons::AngleUp, "##binder_input_move_up", ui.Text(UiText::MoveUp), actionButtonSize);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(currentIndex + 1 >= static_cast<int>(editor.draft.inputs.size()));
    const bool moveInputDown = SmallIconActionButton(ui_icons::AngleDown, "##binder_input_move_down", ui.Text(UiText::MoveDown), actionButtonSize);
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool duplicateInput = SmallIconActionButton(ui_icons::Clone, "##binder_input_duplicate", ui.Text(UiText::ActionDuplicate), actionButtonSize);
    ImGui::SameLine();
    const bool deleteInput = SmallIconActionButton(ui_icons::Delete, "##binder_input_delete", ui.Text(UiText::Delete), actionButtonSize);

    if (moveInputUp && currentIndex > 0) {
        std::swap(editor.draft.inputs[static_cast<std::size_t>(currentIndex)], editor.draft.inputs[static_cast<std::size_t>(currentIndex - 1)]);
        std::swap(
            editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex)],
            editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex - 1)]);
        selectInput(currentIndex - 1);
    } else if (moveInputDown && currentIndex + 1 < static_cast<int>(editor.draft.inputs.size())) {
        std::swap(editor.draft.inputs[static_cast<std::size_t>(currentIndex)], editor.draft.inputs[static_cast<std::size_t>(currentIndex + 1)]);
        std::swap(
            editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex)],
            editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex + 1)]);
        selectInput(currentIndex + 1);
    } else if (duplicateInput) {
        HotkeyInput duplicate = editor.draft.inputs[static_cast<std::size_t>(currentIndex)];
        editor.draft.inputs.insert(editor.draft.inputs.begin() + currentIndex + 1, std::move(duplicate));
        editor.inputButtonsBulkDrafts.insert(
            editor.inputButtonsBulkDrafts.begin() + currentIndex + 1,
            editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex)]);
        selectInput(currentIndex + 1);
    } else if (deleteInput) {
        editor.draft.inputs.erase(editor.draft.inputs.begin() + currentIndex);
        editor.inputButtonsBulkDrafts.erase(editor.inputButtonsBulkDrafts.begin() + currentIndex);
        if (editor.draft.inputs.empty()) {
            editor.selectedInputIndex = -1;
            editor.selectedInputButtonIndex = -1;
            editor.expandedInputHintIndex = -1;
            editor.selectedButtonsText.clear();
            return;
        }
        selectInput(std::min(currentIndex, static_cast<int>(editor.draft.inputs.size()) - 1));
    }

    clampSelectedInput();
    inputIssues = buildInputKeyIssues();
    currentIndex = editor.selectedInputIndex;
    HotkeyInput& input = editor.draft.inputs[static_cast<std::size_t>(currentIndex)];
    const InputKeyIssue currentIssue = inputIssues[static_cast<std::size_t>(currentIndex)];

    const bool wideForm = ImGui::GetContentRegionAvail().x >= ScaleUi(680.0f);
    const auto drawFormField = [&](const char* id, UiText labelId, const auto& drawValue) {
        if (wideForm && ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, ScaleUi(175.0f));
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", ui.Text(labelId));
            ImGui::TableSetColumnIndex(1);
            drawValue();
            ImGui::EndTable();
            return;
        }
        ImGui::TextDisabled("%s", ui.Text(labelId));
        drawValue();
    };

    ImGui::Spacing();
    ImGui::Separator();
    drawFormField("##binder_input_name_row", UiText::ParameterPrompt, [&]() {
        ImGui::SetNextItemWidth(-FLT_MIN);
        InputTextString("##binder_input_name", input.label, ImGuiInputTextFlags_AutoSelectAll, 128);
    });

    drawFormField("##binder_input_mode_row", UiText::ParameterResponseType, [&]() {
        const InputMode modes[] = { InputMode::Text, InputMode::ButtonsList, InputMode::ButtonsListText };
        const char* modeLabels[] = {
            InputModeLabel(InputMode::Text),
            InputModeLabel(InputMode::ButtonsList),
            InputModeLabel(InputMode::ButtonsListText),
        };
        int modeIndex = 0;
        for (int i = 0; i < IM_ARRAYSIZE(modes); ++i) {
            if (input.mode == modes[i]) {
                modeIndex = i;
                break;
            }
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##binder_input_mode", &modeIndex, modeLabels, IM_ARRAYSIZE(modeLabels))) {
            input.mode = modes[modeIndex];
        }
    });

    if (InputModeUsesButtons(input.mode)) {
        drawFormField("##binder_input_multi_row", UiText::ParameterAllowMultiple, [&]() {
            ImGui::Checkbox("##binder_input_multi", &input.multiSelect);
        });
        if (input.multiSelect) {
            drawFormField("##binder_input_separator_row", UiText::ParameterJoinSeparator, [&]() {
                ImGui::SetNextItemWidth(-FLT_MIN);
                InputTextString("##binder_input_separator", input.multiSeparator, ImGuiInputTextFlags_AutoSelectAll, 64);
            });
        }
    }

    const std::string normalizedKey = NormalizeInputKey(input.key);
    if (!normalizedKey.empty()) {
        const std::string placeholder = "{{" + normalizedKey + "}}";
        if (ImGui::Button((placeholder + "##binder_input_placeholder").c_str())) {
            ImGui::SetClipboardText(placeholder.c_str());
        }
        drawTextTooltip(ui.Text(UiText::CopyPlaceholder));
        ImGui::SameLine();
        if (SmallIconActionButton(ui_icons::Copy, "##binder_copy_input_placeholder", ui.Text(UiText::CopyPlaceholder), actionButtonSize)) {
            ImGui::SetClipboardText(placeholder.c_str());
        }
    }

    if (!input.hint.empty() || editor.expandedInputHintIndex == currentIndex) {
        drawFormField("##binder_input_hint_row", UiText::ParameterHintText, [&]() {
            ImGui::SetNextItemWidth(-FLT_MIN);
            InputTextString("##binder_input_hint", input.hint, ImGuiInputTextFlags_AutoSelectAll, 256);
        });
    } else if (ImGui::Button(ui.Text(UiText::ParameterAddHint))) {
        editor.expandedInputHintIndex = currentIndex;
    }

    const bool keyHasIssue = currentIssue.empty || currentIssue.invalid || currentIssue.duplicate;
    const std::string generatedKey = "FIELD_" + std::to_string(currentIndex + 1);
    const bool hasAdvancedValue = !Trim(input.cascadeParentKey).empty() || normalizedKey != generatedKey;
    if (keyHasIssue) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    } else if (hasAdvancedValue) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
    }
    std::string advancedLabel = ui.Text(UiText::ParameterAdvancedCompact);
    if (hasAdvancedValue) {
        advancedLabel += " *";
    }
    if (ImGui::TreeNodeEx("##binder_input_advanced", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", advancedLabel.c_str())) {
        drawFormField("##binder_input_key_row", UiText::ParameterSystemKey, [&]() {
            ImGui::SetNextItemWidth(-FLT_MIN);
            InputTextString("##binder_input_key", input.key, ImGuiInputTextFlags_AutoSelectAll, 64);
            input.key = NormalizeInputKey(input.key);
        });
        if (InputModeUsesButtons(input.mode)) {
            drawFormField("##binder_input_cascade_row", UiText::ParameterDependsOn, [&]() {
                ImGui::SetNextItemWidth(-FLT_MIN);
                InputTextString("##binder_input_cascade", input.cascadeParentKey, ImGuiInputTextFlags_AutoSelectAll, 64);
                input.cascadeParentKey = NormalizeInputKey(input.cascadeParentKey);
            });
        }
        ImGui::TextDisabled("%s", ui.Text(UiText::ParameterAdvancedHint));
        ImGui::TreePop();
    }
    if (currentIssue.empty || currentIssue.invalid) {
        ImGui::TextDisabled("%s", ui.Text(UiText::ValidationInputKeyRequired));
    } else if (currentIssue.duplicate) {
        ImGui::TextDisabled("%s", ui.Text(UiText::ValidationInputKeyUnique));
    }

    if (!InputModeUsesButtons(input.mode)) {
        return;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted(
        ui.Format(UiText::ParameterVariantsCountFormat, static_cast<int>(input.buttons.size())).c_str());
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::ParameterBulkEdit))) {
        editor.selectedButtonsText = SerializeButtonsText(input.buttons);
        editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex)] = editor.selectedButtonsText;
        ImGui::OpenPopup("###binder_parameter_bulk_popup");
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::AddButton))) {
        input.buttons.push_back(InputButton{});
        editor.selectedInputButtonIndex = static_cast<int>(input.buttons.size() - 1);
        syncSelectedButtonsText(input);
    }
    ImGui::TextDisabled("%s", ui.Text(UiText::ParameterVariantsHint));

    if (input.buttons.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::ButtonsEmpty));
    } else if (ImGui::BeginTable(
                   "##binder_options_table",
                   3,
                   ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp
                       | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn(ui.Text(UiText::OptionName), ImGuiTableColumnFlags_WidthStretch, 0.56f);
        ImGui::TableSetupColumn(ui.Text(UiText::OptionValue), ImGuiTableColumnFlags_WidthStretch, 0.34f);
        ImGui::TableSetupColumn(ui.Text(UiText::ColumnActions), ImGuiTableColumnFlags_WidthFixed, ScaleUi(62.0f));
        ImGui::TableHeadersRow();

        enum class OptionAction { None, MoveUp, MoveDown, Duplicate, Delete };
        OptionAction pendingAction = OptionAction::None;
        int pendingActionIndex = -1;
        bool structuredChanged = false;

        for (std::size_t buttonIndex = 0; buttonIndex < input.buttons.size(); ++buttonIndex) {
            InputButton& button = input.buttons[buttonIndex];
            ImGui::PushID(static_cast<int>(buttonIndex));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-FLT_MIN);
            structuredChanged |= InputTextString("##option_label", button.label, ImGuiInputTextFlags_AutoSelectAll, 128);

            ImGui::TableSetColumnIndex(1);
            const bool multilineValue = button.text.find_first_of("\r\n") != std::string::npos;
            if (multilineValue) {
                std::string preview = button.text;
                std::replace(preview.begin(), preview.end(), '\r', ' ');
                std::replace(preview.begin(), preview.end(), '\n', ' ');
                preview = EllipsizeText(preview, std::max(ScaleUi(80.0f), ImGui::GetContentRegionAvail().x - ScaleUi(8.0f)));
                if (ImGui::Button((preview + "###option_value_preview").c_str(), ImVec2(-FLT_MIN, 0.0f))) {
                    editor.selectedInputButtonIndex = static_cast<int>(buttonIndex);
                }
                drawTextTooltip(button.text.c_str());
            } else {
                ImGui::SetNextItemWidth(-FLT_MIN);
                structuredChanged |= InputTextString("##option_value", button.text, ImGuiInputTextFlags_AutoSelectAll, 512);
            }

            ImGui::TableSetColumnIndex(2);
            if (SmallIconActionButton(
                    ui_icons::Sliders,
                    "##option_details",
                    ui.Text(UiText::ButtonPropertiesTitle),
                    actionButtonSize)) {
                const int index = static_cast<int>(buttonIndex);
                editor.selectedInputButtonIndex = editor.selectedInputButtonIndex == index ? -1 : index;
            }
            ImGui::SameLine();
            if (SmallIconActionButton(
                    ui_icons::Bars,
                    "##option_actions",
                    ui.Text(UiText::ParameterOptionActions),
                    actionButtonSize)) {
                ImGui::OpenPopup("##option_actions_popup");
            }
            if (ImGui::BeginPopup("##option_actions_popup")) {
                if (ImGui::MenuItem(ui.Text(UiText::MoveUp), nullptr, false, buttonIndex > 0)) {
                    pendingAction = OptionAction::MoveUp;
                    pendingActionIndex = static_cast<int>(buttonIndex);
                }
                if (ImGui::MenuItem(ui.Text(UiText::MoveDown), nullptr, false, buttonIndex + 1 < input.buttons.size())) {
                    pendingAction = OptionAction::MoveDown;
                    pendingActionIndex = static_cast<int>(buttonIndex);
                }
                if (ImGui::MenuItem(ui.Text(UiText::ActionDuplicate))) {
                    pendingAction = OptionAction::Duplicate;
                    pendingActionIndex = static_cast<int>(buttonIndex);
                }
                if (ImGui::MenuItem(ui.Text(UiText::Delete))) {
                    pendingAction = OptionAction::Delete;
                    pendingActionIndex = static_cast<int>(buttonIndex);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();

        if (pendingAction != OptionAction::None && pendingActionIndex >= 0) {
            const std::size_t index = static_cast<std::size_t>(pendingActionIndex);
            if (pendingAction == OptionAction::MoveUp && index > 0) {
                std::swap(input.buttons[index], input.buttons[index - 1]);
                editor.selectedInputButtonIndex = static_cast<int>(index - 1);
            } else if (pendingAction == OptionAction::MoveDown && index + 1 < input.buttons.size()) {
                std::swap(input.buttons[index], input.buttons[index + 1]);
                editor.selectedInputButtonIndex = static_cast<int>(index + 1);
            } else if (pendingAction == OptionAction::Duplicate && index < input.buttons.size()) {
                InputButton duplicate = input.buttons[index];
                input.buttons.insert(input.buttons.begin() + pendingActionIndex + 1, std::move(duplicate));
                editor.selectedInputButtonIndex = pendingActionIndex + 1;
            } else if (pendingAction == OptionAction::Delete && index < input.buttons.size()) {
                input.buttons.erase(input.buttons.begin() + pendingActionIndex);
                editor.selectedInputButtonIndex = -1;
            }
            structuredChanged = true;
        }
        if (structuredChanged) {
            syncSelectedButtonsText(input);
        }
    }

    if (editor.selectedInputButtonIndex >= 0
        && editor.selectedInputButtonIndex < static_cast<int>(input.buttons.size())) {
        ImGui::PushID(editor.selectedInputButtonIndex);
        InputButton& button = input.buttons[static_cast<std::size_t>(editor.selectedInputButtonIndex)];
        ImGui::Spacing();
        ImGui::SeparatorText(ui.Text(UiText::ButtonPropertiesTitle));
        bool detailsChanged = false;
        ImGui::TextDisabled("%s", ui.Text(UiText::OptionValue));
        ImGui::SetNextItemWidth(-FLT_MIN);
        detailsChanged |= InputTextString("##option_value_details", button.text, ImGuiInputTextFlags_AutoSelectAll, 512);
        ImGui::TextDisabled("%s", ui.Text(UiText::OptionHint));
        ImGui::SetNextItemWidth(-FLT_MIN);
        detailsChanged |= InputTextString("##option_hint_details", button.hint, ImGuiInputTextFlags_AutoSelectAll, 256);
        if (detailsChanged) {
            syncSelectedButtonsText(input);
        }
        ImGui::PopID();
    }

    const std::string bulkPopupTitle = std::string(ui.Text(UiText::ParameterBulkTitle)) + "###binder_parameter_bulk_popup";
    bool bulkPopupOpen = true;
    const float bulkPopupMaxWidth = std::max(ScaleUi(360.0f), ImGui::GetIO().DisplaySize.x - ScaleUi(24.0f));
    ImGui::SetNextWindowSize(ImVec2(std::min(ScaleUi(720.0f), bulkPopupMaxWidth), 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(bulkPopupTitle.c_str(), &bulkPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!bulkPopupOpen) {
            editor.selectedButtonsText = SerializeButtonsText(input.buttons);
            editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex)] = editor.selectedButtonsText;
            ImGui::CloseCurrentPopup();
        } else {
            if (ImGui::Button(ui.Text(UiText::ButtonsBulkAddLine))) {
                AppendButtonsBulkTemplateLines(editor.selectedButtonsText, 1);
            }
            ImGui::SameLine();
            if (ImGui::Button(ui.Text(UiText::ButtonsBulkAddFiveLines))) {
                AppendButtonsBulkTemplateLines(editor.selectedButtonsText, 5);
            }
            ImGui::SameLine();
            if (ImGui::Button(ui.Text(UiText::ButtonsBulkNormalize))) {
                editor.selectedButtonsText = SerializeButtonsText(ParseButtonsText(editor.selectedButtonsText));
            }

            ImGui::TextDisabled("%s", ui.Text(UiText::ButtonsFormatHint));
            const bool hasCascadeRules = std::any_of(input.buttons.begin(), input.buttons.end(), [](const InputButton& button) {
                return !Trim(button.when).empty();
            });
            if (!Trim(input.cascadeParentKey).empty() || hasCascadeRules) {
                ImGui::TextDisabled("%s", ui.Text(UiText::ButtonsCascadeFormatHint));
            }
            ImGui::TextDisabled("%s", ui.Text(UiText::ButtonsBulkEscapingHint));
            InputTextMultilineString(
                "##binder_input_buttons_bulk",
                editor.selectedButtonsText,
                ImVec2(-FLT_MIN, ScaleUi(260.0f)));

            ButtonsTextParseStats stats;
            const std::vector<InputButton> parsedButtons = ParseButtonsTextEx(editor.selectedButtonsText, &stats);
            ImGui::TextDisabled(
                "%s",
                ui.Format(
                      UiText::ButtonsBulkPreviewFormat,
                      stats.total,
                      stats.used,
                      stats.ignored,
                      static_cast<int>(parsedButtons.size()))
                    .c_str());
            if (stats.extraPipes > 0) {
                ImGui::TextDisabled("%s", ui.Text(UiText::ButtonsBulkExtraPipesHint));
            }

            ImGui::Separator();
            if (ImGui::Button(ui.Text(UiText::ButtonsBulkApply))) {
                input.buttons = parsedButtons;
                editor.selectedInputButtonIndex = -1;
                syncSelectedButtonsText(input);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(ui.Text(UiText::Cancel))) {
                editor.selectedButtonsText = SerializeButtonsText(input.buttons);
                editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex)] = editor.selectedButtonsText;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void BinderModule::Impl::DrawEditorConditionsPopup() {
    (void)DrawConditionFlagsPopup(
        "##binder_editor_conditions",
        editor.conditionsPopupPending,
        UiText::BlockingConditions,
        editor.draft.conditions,
        &editor.draft.conditionsCombine);
}

void BinderModule::Impl::DrawEditorVariablesPopup() {
    if (editor.variablesPopupPending) {
        editor.variablesPicker.search.clear();
        editor.variablesKeyPickerPopupPending = false;
        if (!editor.draft.inputs.empty()) {
            editor.variablesPicker.activeCategory = variables_picker::Category::Parameters;
        } else if (editor.variablesPicker.activeCategory == variables_picker::Category::Parameters) {
            editor.variablesPicker.activeCategory = variables_picker::Category::All;
        }
        editor.variablePickerEntries = BuildEditorVariablePickerEntries();
        ImGui::OpenPopup(kEditorVariablesPopupId);
        editor.variablesPopupPending = false;
    }

    UiSettings& ui = UiSettings::Instance();
    const std::string popupTitle = std::string(ui.Text(UiText::EditorVariablesTitle)) + kEditorVariablesPopupId;
    bool popupOpen = true;
    SetNextResponsiveEditorPopupSize(ScaleUi(960.0f, 700.0f), ScaleUi(680.0f, 460.0f));
    if (!ImGui::BeginPopupModal(
            popupTitle.c_str(),
            &popupOpen,
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        return;
    }

    bool closeRequested = !popupOpen;

    if (editor.variablesKeyPickerPopupPending) {
        ImGui::OpenPopup("##binder_editor_variable_keypicker");
        editor.variablesKeyPickerPopupPending = false;
    }

    variables_picker::Options pickerOptions{
        variables_picker::Mode::Insert,
        "binder_editor_variables_picker",
        editor.variablesInsertTarget != binder_editor::State::VariableInsertTarget::None,
        false,
        true,
        ImGui::GetContentRegionAvail(),
    };
    pickerOptions.allowCopyInInsert = true;
    const variables_picker::Request request = variables_picker::Draw(
        editor.variablesPicker,
        editor.variablePickerEntries,
        pickerOptions);
    HandleEditorVariablePickerRequest(request);
    closeRequested |= request.closePopupAfterAction;

    if (tagsModule) {
        tagsModule->DrawVariableHelperPopups([&](std::string_view token) {
            if (!InsertTextIntoEditorVariableTarget(token)) {
                CopyTextToClipboard(token);
            }
            closeRequested = true;
        });
    }
    closeRequested |= DrawEditorVariableKeyPickerPopup();
    if (closeRequested) {
        editor.variablesKeyPickerSearch.clear();
        editor.variablePickerEntries.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void BinderModule::Impl::DrawEditorDiscardPopup() {
    if (editor.discardPopupPending) {
        ImGui::OpenPopup("##binder_editor_discard");
        editor.discardPopupPending = false;
    }

    if (!ImGui::BeginPopupModal("##binder_editor_discard", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    ImGui::TextUnformatted(ui.Text(UiText::EditorDiscardTitle));
    ImGui::Separator();
    ImGui::TextWrapped("%s", ui.Text(UiText::EditorDiscardMessage));
    ImGui::Spacing();
    if (ImGui::Button(ui.Text(UiText::EditorDiscardAction), ScaleUi(170.0f, 0.0f))) {
        ExecuteEditorPendingAction();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::EditorStay), ScaleUi(120.0f, 0.0f))) {
        editor.pendingAction = binder_editor::State::PendingAction::None;
        editor.pendingTargetIndex = -1;
        editor.discardPopupPending = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void BinderModule::Impl::DrawEditorScenarioTab() {
    UiSettings& ui = UiSettings::Instance();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float dragHandleWidth = std::ceil(ImGui::CalcTextSize(ui_icons::MoveRows).x + ScaleUi(4.0f));
    const float dragHandleHeight = ImGui::GetFrameHeight();
    const float dragColumnWidth = std::ceil(dragHandleWidth + style.CellPadding.x * 2.0f);
    const float destinationColumnWidth = std::ceil(
        ImGui::CalcTextSize(ui.Text(UiText::SendDirect)).x + style.FramePadding.x * 2.0f + ImGui::GetFrameHeight());
    const ImVec2 actionButtonSize(ScaleUi(24.0f), ScaleUi(24.0f));
    const float actionButtonsWidth = actionButtonSize.x;
    const float actionsColumnWidth = std::ceil(actionButtonsWidth + style.CellPadding.x * 2.0f);
    const bool onlyEmptyPlaceholder =
        editor.draft.messages.size() == 1 && Trim(editor.draft.messages.front().text).empty();
    const std::size_t visibleMessageCount = onlyEmptyPlaceholder ? 0 : editor.draft.messages.size();
    if (editor.scenarioMessageFocusIndex >= static_cast<int>(visibleMessageCount)) {
        editor.scenarioMessageFocusIndex = -1;
        editor.scenarioMessageFocusCaretByte = -1;
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", ui.Text(UiText::EditorScenarioHint));
    ImGui::Spacing();

    int removeIndex = -1;
    int moveSourceIndex = -1;
    int moveTargetIndex = -1;
    const ImVec2 stepsTableOuterSize(
        ImGui::GetContentRegionAvail().x,
        std::max(ScaleUi(180.0f), ImGui::GetContentRegionAvail().y));
    if (ImGui::BeginTable(
            "##binder_editor_steps",
            5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp
                | ImGuiTableFlags_ScrollY,
            stepsTableOuterSize)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##drag", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, dragColumnWidth);
        ImGui::TableSetupColumn(
            UiSettings::Instance().Text(UiText::EditorColumnMessage),
            ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize);
        ImGui::TableSetupColumn(
            UiSettings::Instance().Text(UiText::EditorColumnPauseMs),
            ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
            ScaleUi(110.0f));
        ImGui::TableSetupColumn(
            UiSettings::Instance().Text(UiText::EditorColumnDestination),
            ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
            destinationColumnWidth);
        ImGui::TableSetupColumn(
            "##actions",
            ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
            actionsColumnWidth);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < visibleMessageCount; ++i) {
            HotkeyMessage& message = editor.draft.messages[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableSetColumnIndex(0);
            CenterNextItemHorizontally(dragHandleWidth);
            IconOnlyButton(ui_icons::MoveRows, "##step_drag", ui.Text(UiText::EditorMoveStep), ImVec2(dragHandleWidth, dragHandleHeight));
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
                const int payloadIndex = static_cast<int>(i);
                ImGui::SetDragDropPayload("BINDER_EDITOR_STEP", &payloadIndex, sizeof(payloadIndex));
                ImGui::Text("%s %d", ui.Text(UiText::EditorScenarioTab), static_cast<int>(i + 1));
                std::string previewText = Trim(message.text);
                if (previewText.empty()) {
                    previewText = "...";
                } else {
                    while (previewText.size() > 77) {
                        previewText = Utf8TrimLastChar(previewText);
                    }
                }
                if (previewText != Trim(message.text)) {
                    previewText += "...";
                }
                ImGui::TextDisabled("%s", previewText.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            const int messageIndex = static_cast<int>(i);
            const bool focusMessageText = editor.scenarioMessageFocusIndex == messageIndex;
            if (focusMessageText) {
                ImGui::SetKeyboardFocusHere();
            }
            InputTextSetCaretData focusData{ editor.scenarioMessageFocusCaretByte, false };
            const ImGuiInputTextFlags textFlags = focusMessageText
                ? ImGuiInputTextFlags_CallbackAlways
                : 0;
            InputTextString(
                "##step_text",
                message.text,
                textFlags,
                256,
                focusMessageText ? SetInputTextCaretCallback : nullptr,
                focusMessageText ? &focusData : nullptr);
            const ImGuiID stepTextId = ImGui::GetItemID();
            if (ImGui::IsItemActive() || ImGui::IsItemFocused()) {
                int cursorByte = -1;
                int selectionStartByte = -1;
                int selectionEndByte = -1;
                if (ImGuiInputTextState* inputState = ImGui::GetInputTextState(stepTextId)) {
                    cursorByte = inputState->GetCursorPos();
                    selectionStartByte = inputState->GetSelectionStart();
                    selectionEndByte = inputState->GetSelectionEnd();
                }
                RememberEditorVariableInsertTarget(
                    binder_editor::State::VariableInsertTarget::ScenarioMessage,
                    messageIndex,
                    cursorByte,
                    selectionStartByte,
                    selectionEndByte);
            }
            if (focusMessageText && focusData.applied) {
                editor.scenarioMessageFocusIndex = -1;
                editor.scenarioMessageFocusCaretByte = -1;
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BINDER_EDITOR_STEP")) {
                    if (payload->Delivery && payload->DataSize == sizeof(int)) {
                        const int payloadIndex = *static_cast<const int*>(payload->Data);
                        if (payloadIndex >= 0 && payloadIndex < static_cast<int>(visibleMessageCount)
                            && payloadIndex != static_cast<int>(i)) {
                            moveSourceIndex = payloadIndex;
                            moveTargetIndex = static_cast<int>(i);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputInt("##step_delay", &message.intervalMs);
            if (message.intervalMs < 0) {
                message.intervalMs = 0;
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##step_method", SendMethodLabel(message.method))) {
                for (int method = 0; method <= 9; ++method) {
                    const bool selected = method == message.method;
                    if (ImGui::Selectable(SendMethodLabel(method), selected)) {
                        message.method = method;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::TableSetColumnIndex(4);
            const float actionButtonsOffsetY = std::max(0.0f, std::floor((ImGui::GetFrameHeight() - actionButtonSize.y) * 0.5f));
            if (actionButtonsOffsetY > 0.0f) {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + actionButtonsOffsetY);
            }
            CenterNextItemHorizontally(actionButtonsWidth);
            if (SmallIconActionButton(ui_icons::Delete, "##step_delete", ui.Text(UiText::Delete), actionButtonSize)) {
                removeIndex = static_cast<int>(i);
            }
            ImGui::PopID();
        }

        ImGui::TableNextRow();
        ImGui::PushID("append");

        ImGui::TableSetColumnIndex(0);
        CenterNextItemHorizontally(dragHandleWidth);
        ImGui::BeginDisabled();
        IconOnlyButton(
            ui_icons::MoveRows,
            "##step_append_drag",
            ui.Text(UiText::EditorMoveStep),
            ImVec2(dragHandleWidth, dragHandleHeight),
            false);
        ImGui::EndDisabled();

        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        const ImGuiID appendTextId = ImGui::GetID("##step_append_text");
        if (editor.scenarioAppendFocusPending) {
            ImGui::SetKeyboardFocusHere();
            editor.scenarioAppendFocusPending = false;
        }
        bool handledMultilinePaste = false;
        if (ImGui::GetActiveID() == appendTextId
            && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
            const char* clipboardText = ImGui::GetClipboardText();
            if (clipboardText && std::string_view(clipboardText).find_first_of("\r\n") != std::string_view::npos) {
                int selectionStart = static_cast<int>(editor.scenarioAppendText.size());
                int selectionEnd = selectionStart;
                if (ImGuiInputTextState* inputState = ImGui::GetInputTextState(appendTextId)) {
                    selectionStart = std::clamp(
                        std::min(inputState->GetSelectionStart(), inputState->GetSelectionEnd()),
                        0,
                        static_cast<int>(editor.scenarioAppendText.size()));
                    selectionEnd = std::clamp(
                        std::max(inputState->GetSelectionStart(), inputState->GetSelectionEnd()),
                        selectionStart,
                        static_cast<int>(editor.scenarioAppendText.size()));
                }
                editor.scenarioAppendText.replace(
                    static_cast<std::size_t>(selectionStart),
                    static_cast<std::size_t>(selectionEnd - selectionStart),
                    clipboardText);
                (void)CommitEditorScenarioAppendText();
                if (!editor.scenarioAppendText.empty()) {
                    editor.scenarioAppendText.clear();
                }
                if (ImGuiInputTextState* inputState = ImGui::GetInputTextState(appendTextId)) {
                    inputState->ReloadUserBufAndMoveToEnd();
                }
                ImGui::SetKeyOwner(
                    ImGuiKey_V,
                    ImGui::GetID("##step_append_multiline_paste"),
                    ImGuiInputFlags_LockThisFrame);
                handledMultilinePaste = true;
            }
        }
        const bool appendChanged = InputTextWithHintString(
            "##step_append_text",
            ui.Text(UiText::EditorAppendStepHint),
            editor.scenarioAppendText,
            0,
            256);
        if (ImGui::IsItemActive() || ImGui::IsItemFocused()) {
            int cursorByte = -1;
            int selectionStartByte = -1;
            int selectionEndByte = -1;
            if (ImGuiInputTextState* inputState = ImGui::GetInputTextState(appendTextId)) {
                cursorByte = inputState->GetCursorPos();
                selectionStartByte = inputState->GetSelectionStart();
                selectionEndByte = inputState->GetSelectionEnd();
            }
            RememberEditorVariableInsertTarget(
                binder_editor::State::VariableInsertTarget::ScenarioAppend,
                -1,
                cursorByte,
                selectionStartByte,
                selectionEndByte);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s", ui.Text(UiText::EditorAppendStepTooltip));
        }
        if (!handledMultilinePaste && appendChanged && CommitEditorScenarioAppendText()) {
            if (ImGuiInputTextState* inputState = ImGui::GetInputTextState(appendTextId)) {
                inputState->ReloadUserBufAndMoveToEnd();
            }
            ImGui::ClearActiveID();
        }

        ImGui::TableSetColumnIndex(2);
        char disabledDelayText[1] = "";
        ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText(
            "##step_append_delay",
            disabledDelayText,
            IM_ARRAYSIZE(disabledDelayText),
            ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();

        ImGui::TableSetColumnIndex(3);
        ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##step_append_method", "")) {
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();

        ImGui::TableSetColumnIndex(4);
        const float appendButtonOffsetY = std::max(0.0f, std::floor((ImGui::GetFrameHeight() - actionButtonSize.y) * 0.5f));
        if (appendButtonOffsetY > 0.0f) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + appendButtonOffsetY);
        }
        CenterNextItemHorizontally(actionButtonsWidth);
        if (SmallIconActionButton(ui_icons::Plus, "##step_append_add", ui.Text(UiText::EditorAppendStepTooltip), actionButtonSize)
            && !CommitEditorScenarioAppendText()) {
            editor.scenarioAppendFocusPending = true;
        }
        ImGui::PopID();

        ImGui::EndTable();
    }

    if (moveSourceIndex >= 0 && moveTargetIndex >= 0 && moveSourceIndex != moveTargetIndex) {
        HotkeyMessage moved = editor.draft.messages[static_cast<std::size_t>(moveSourceIndex)];
        editor.draft.messages.erase(editor.draft.messages.begin() + moveSourceIndex);
        editor.draft.messages.insert(editor.draft.messages.begin() + moveTargetIndex, std::move(moved));

        const auto remapMovedIndex = [&](int index) {
            if (index < 0 || index == moveSourceIndex) {
                return index < 0 ? index : moveTargetIndex;
            }
            if (moveSourceIndex < moveTargetIndex && index > moveSourceIndex && index <= moveTargetIndex) {
                return index - 1;
            }
            if (moveSourceIndex > moveTargetIndex && index >= moveTargetIndex && index < moveSourceIndex) {
                return index + 1;
            }
            return index;
        };
        if (editor.variablesInsertTarget == binder_editor::State::VariableInsertTarget::ScenarioMessage) {
            editor.variablesInsertMessageIndex = remapMovedIndex(editor.variablesInsertMessageIndex);
        }
        editor.scenarioMessageFocusIndex = remapMovedIndex(editor.scenarioMessageFocusIndex);
    }
    if (removeIndex >= 0 && removeIndex < static_cast<int>(editor.draft.messages.size())) {
        if (editor.draft.messages.size() > 1) {
            editor.draft.messages.erase(editor.draft.messages.begin() + removeIndex);
            if (editor.variablesInsertTarget == binder_editor::State::VariableInsertTarget::ScenarioMessage) {
                if (editor.variablesInsertMessageIndex == removeIndex) {
                    ClearEditorVariableInsertTarget(editor);
                } else if (editor.variablesInsertMessageIndex > removeIndex) {
                    --editor.variablesInsertMessageIndex;
                }
            }
            if (editor.scenarioMessageFocusIndex == removeIndex) {
                editor.scenarioMessageFocusIndex = -1;
                editor.scenarioMessageFocusCaretByte = -1;
            } else if (editor.scenarioMessageFocusIndex > removeIndex) {
                --editor.scenarioMessageFocusIndex;
            }
        } else {
            editor.draft.messages.front() = HotkeyMessage{ "", 0, editor.bulkMethod };
            if (editor.variablesInsertTarget == binder_editor::State::VariableInsertTarget::ScenarioMessage) {
                editor.variablesInsertCursorByte = 0;
                editor.variablesInsertSelectionStartByte = 0;
                editor.variablesInsertSelectionEndByte = 0;
            }
            editor.scenarioMessageFocusIndex = -1;
            editor.scenarioMessageFocusCaretByte = -1;
        }
    }
}

void BinderModule::Impl::DrawEditorMultiInputPopup() {
    UiSettings& ui = UiSettings::Instance();
    const std::string title = std::string(ui.Text(UiText::EditorMultiInputTitle)) + "##binder_editor_multi_input_popup";
    if (editor.multiInputPopupPending) {
        SyncEditorMessagesToMulti();
        ImGui::OpenPopup(title.c_str());
        editor.multiInputPopupPending = false;
    }
    SetNextResponsiveEditorPopupSize(ScaleUi(640.0f, 460.0f), ScaleUi(420.0f, 300.0f));
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    bool bulkChanged = false;
    if (ImGui::BeginTable("##binder_editor_bulk_meta_popup", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("method", ImGuiTableColumnFlags_WidthStretch, 0.70f);
        ImGui::TableSetupColumn("interval", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorColumnDestination));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##binder_editor_bulk_method_popup", SendMethodLabel(editor.bulkMethod))) {
            for (int method = 0; method <= 9; ++method) {
                const bool selected = method == editor.bulkMethod;
                if (ImGui::Selectable(SendMethodLabel(method), selected)) {
                    editor.bulkMethod = method;
                    bulkChanged = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorColumnPauseMs));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputInt("##binder_editor_bulk_interval_popup", &editor.bulkIntervalMs)) {
            bulkChanged = true;
        }
        if (editor.bulkIntervalMs < 0) {
            editor.bulkIntervalMs = 0;
            bulkChanged = true;
        }

        ImGui::EndTable();
    }

    const float footerReserve = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing() + ScaleUi(12.0f);
    const bool textChanged = InputTextMultilineString(
        "##binder_editor_multi_text_popup",
        editor.multiText,
        ImVec2(-FLT_MIN, std::max(ScaleUi(180.0f), ImGui::GetContentRegionAvail().y - footerReserve)),
        0,
        2048);
    if (bulkChanged || textChanged) {
        ApplyEditorMultiToDraft(bulkChanged);
    }
    ImGui::TextDisabled("%s", ui.Text(UiText::EditorMultiInputHint));
    ImGui::Spacing();
    const float doneWidth = ScaleUi(120.0f);
    const float cursorX = ImGui::GetCursorPosX();
    const float availX = ImGui::GetContentRegionAvail().x;
    if (availX > doneWidth) {
        ImGui::SetCursorPosX(cursorX + availX - doneWidth);
    }
    if (ImGui::Button(ui.Text(UiText::Done), ImVec2(doneWidth, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void BinderModule::Impl::DrawEditorInline() {
    binder_editor::DrawInline(editor, binder_editor::ShellActions{
        [&]() { return EditorHasUnsavedChanges(); },
        [&]() { return EditorNeighborIndices(); },
        [&](binder_editor::State::PendingAction action, int targetIndex) { RequestEditorAction(action, targetIndex); },
        [&]() { DrawEditorScenarioTab(); },
        [&]() { DrawInputEditor(); },
        [&]() {
            UiSettings& ui = UiSettings::Instance();
            std::vector<std::string> errors;
            if (ValidateEditor(errors)) {
                SaveEditor();
                Notify(NotificationGroup::Success, NotificationSeverity::Success, ui.Text(UiText::ToastBindSaved), 1800.0);
            } else {
                for (const std::string& error : errors) {
                    Notify(NotificationGroup::Validation, NotificationSeverity::Error, error, 2800.0);
                }
            }
        },
        [&]() { DrawCapturePopup(true); },
        [&]() { DrawEditorConditionsPopup(); },
        [&]() { DrawEditorVariablesPopup(); },
        [&]() { DrawEditorMultiInputPopup(); },
        [&]() { DrawEditorDiscardPopup(); },
        binder_editor::LaunchPanelActions{
            [&]() { BeginCapture(CaptureTarget::BindHotkey); },
            [&]() { BeginCapture(CaptureTarget::ConfirmKey); },
            [&]() { BeginCapture(CaptureTarget::CancelKey); },
        },
    });
}

void BinderModule::Impl::DrawEditor() {
    if (editor.active) {
        DrawEditorInline();
    }
}
