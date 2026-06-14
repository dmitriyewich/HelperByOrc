#include "binder_module_impl.h"

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
    editor.inputButtonsBulkPreviews.assign(editor.draft.inputs.size(), ButtonsBulkPreviewState{});

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
}

HotkeyEntry BinderModule::Impl::BuildEditorComparableDraft() const {
    return binder_editor::BuildComparableDraft(editor);
}

bool BinderModule::Impl::EditorHasUnsavedChanges() const {
    if (!editor.active) {
        return false;
    }

    std::string currentSerialized;
    std::string baselineSerialized;
    jsonutil::WriteJson(SerializeHotkey(BuildEditorComparableDraft()), currentSerialized);
    jsonutil::WriteJson(SerializeHotkey(editor.baseline), baselineSerialized);
    return currentSerialized != baselineSerialized;
}

std::pair<int, int> BinderModule::Impl::EditorNeighborIndices() const {
    if (editor.hotkeyIndex < 0 || editor.hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        return { -1, -1 };
    }

    const HotkeyEntry& current = hotkeys[static_cast<std::size_t>(editor.hotkeyIndex)];
    const std::vector<std::string>& folderPath = current.folderPath;
    const std::string& categoryId = current.categoryId;
    int previous = -1;
    int next = -1;
    int last = -1;
    for (int index = 0; index < static_cast<int>(hotkeys.size()); ++index) {
        if (hotkeys[static_cast<std::size_t>(index)].categoryId != categoryId
            || hotkeys[static_cast<std::size_t>(index)].folderPath != folderPath) {
            continue;
        }
        if (index == editor.hotkeyIndex) {
            previous = last;
            continue;
        }
        if (previous != -1 || index > editor.hotkeyIndex) {
            if (index > editor.hotkeyIndex) {
                next = index;
                break;
            }
        }
        last = index;
    }
    if (previous == -1) {
        last = -1;
        for (int index = 0; index < editor.hotkeyIndex; ++index) {
            if (hotkeys[static_cast<std::size_t>(index)].categoryId == categoryId
                && hotkeys[static_cast<std::size_t>(index)].folderPath == folderPath) {
                last = index;
            }
        }
        previous = last;
    }
    return { previous, next };
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
    const HotkeyEntry current = BuildEditorComparableDraft();
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
        try {
            std::regex test(triggerText);
            (void)test;
        } catch (const std::exception& ex) {
            errors.push_back(ui.Format(UiText::ValidationInvalidRegex, ex.what()));
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
        pickerEntry.category = variables_picker::ClassifyBuiltin(entry.name);
        pickerEntry.id = variables_picker::MakeEntryId(kind, entry.token);
        pickerEntry.name = entry.name;
        pickerEntry.token = entry.token;
        pickerEntry.example = entry.example;
        pickerEntry.descriptionText = entry.descriptionText;
        pickerEntry.action = variables_picker::IsActionBuiltin(entry.name);
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
    int cursorByte) {
    editor.variablesInsertTarget = target;
    editor.variablesInsertMessageIndex = messageIndex;
    editor.variablesInsertCursorByte = cursorByte;
}

bool BinderModule::Impl::InsertTextIntoEditorVariableTarget(std::string_view text) {
    const auto insertAt = [&](std::string& value) {
        const int clampedCursor = editor.variablesInsertCursorByte < 0
            ? static_cast<int>(value.size())
            : std::clamp(editor.variablesInsertCursorByte, 0, static_cast<int>(value.size()));
        value.insert(static_cast<std::size_t>(clampedCursor), text.data(), text.size());
        editor.variablesInsertCursorByte = clampedCursor + static_cast<int>(text.size());
    };

    switch (editor.variablesInsertTarget) {
    case binder_editor::State::VariableInsertTarget::ScenarioMessage:
        if (editor.variablesInsertMessageIndex < 0
            || editor.variablesInsertMessageIndex >= static_cast<int>(editor.draft.messages.size())) {
            return false;
        }
        insertAt(editor.draft.messages[static_cast<std::size_t>(editor.variablesInsertMessageIndex)].text);
        editor.scenarioMessageNoSelectFocusIndex = editor.variablesInsertMessageIndex;
        SyncEditorMessagesToMulti();
        return true;
    case binder_editor::State::VariableInsertTarget::ScenarioAppend:
        insertAt(editor.scenarioAppendText);
        editor.scenarioAppendFocusPending = true;
        return true;
    case binder_editor::State::VariableInsertTarget::None:
    default:
        return false;
    }
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
    case variables_picker::RequestType::None:
    case variables_picker::RequestType::SaveCustom:
    case variables_picker::RequestType::DeleteCustom:
    default:
        break;
    }
}

void BinderModule::Impl::DrawEditorVariableKeyPickerPopup() {
    if (!tagsModule) {
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    ImGui::SetNextWindowSize(ScaleUi(560.0f, 520.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("##binder_editor_variable_keypicker")) {
        return;
    }

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
    if (ImGui::BeginChild("##binder_editor_variable_keypicker_list", ScaleUi(0.0f, 360.0f), ImGuiChildFlags_Borders)) {
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
}

void BinderModule::Impl::DrawInputEditor() {
    UiSettings& ui = UiSettings::Instance();
    const ImVec2 actionButtonSize = ScaleUi(26.0f, 26.0f);

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

        if (editor.inputButtonsBulkPreviews.size() < editor.draft.inputs.size()) {
            editor.inputButtonsBulkPreviews.resize(editor.draft.inputs.size());
        } else if (editor.inputButtonsBulkPreviews.size() > editor.draft.inputs.size()) {
            editor.inputButtonsBulkPreviews.resize(editor.draft.inputs.size());
        }
    };

    auto selectInput = [&](int index) {
        ensureBulkStateStorage();
        if (index < 0 || index >= static_cast<int>(editor.draft.inputs.size())) {
            editor.selectedInputIndex = -1;
            editor.selectedInputButtonIndex = -1;
            editor.selectedButtonsText.clear();
            return;
        }

        editor.selectedInputIndex = index;
        editor.selectedButtonsText = editor.inputButtonsBulkDrafts[static_cast<std::size_t>(index)];
        editor.selectedInputButtonIndex =
            editor.draft.inputs[static_cast<std::size_t>(index)].buttons.empty() ? -1 : 0;
    };

    auto clampSelectedInput = [&]() {
        ensureBulkStateStorage();
        if (editor.draft.inputs.empty()) {
            editor.selectedInputIndex = -1;
            editor.selectedInputButtonIndex = -1;
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
        } else if (editor.selectedInputButtonIndex < 0 || editor.selectedInputButtonIndex >= static_cast<int>(input.buttons.size())) {
            editor.selectedInputButtonIndex = 0;
        }
    };

    auto syncSelectedButtonsText = [&](const HotkeyInput& input) {
        ensureBulkStateStorage();
        if (editor.selectedInputIndex >= 0 && editor.selectedInputIndex < static_cast<int>(editor.inputButtonsBulkDrafts.size())) {
            const std::string text = SerializeButtonsText(input.buttons);
            editor.inputButtonsBulkDrafts[static_cast<std::size_t>(editor.selectedInputIndex)] = text;
            editor.inputButtonsBulkPreviews[static_cast<std::size_t>(editor.selectedInputIndex)] = {};
            editor.selectedButtonsText = text;
        } else {
            editor.selectedButtonsText.clear();
        }
        if (input.buttons.empty()) {
            editor.selectedInputButtonIndex = -1;
        } else if (editor.selectedInputButtonIndex < 0 || editor.selectedInputButtonIndex >= static_cast<int>(input.buttons.size())) {
            editor.selectedInputButtonIndex = 0;
        }
    };

    auto inputDisplayName = [&](const HotkeyInput& input, int index) {
        const std::string label = Trim(input.label);
        if (!label.empty()) {
            return label;
        }
        return ui.Format(UiText::FieldLabelFormat, index + 1);
    };
    auto buttonDisplayName = [&](const InputButton& button, int index) {
        const std::string label = Trim(button.label);
        if (!label.empty()) {
            return label;
        }
        return ui.Format(UiText::ButtonLabelFormat, index + 1);
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

    const auto drawInspectorTable = [&](const char* tableId, const auto& drawRows) {
        if (!ImGui::BeginTable(
                tableId,
                2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            return;
        }

        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, ScaleUi(190.0f));
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        const auto drawRow = [&](UiText labelId, const auto& drawValue) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", ui.Text(labelId));
            ImGui::TableSetColumnIndex(1);
            drawValue();
        };

        drawRows(drawRow);
        ImGui::EndTable();
    };

    if (ImGui::Button(ui.Text(UiText::AddField))) {
        HotkeyInput input;
        input.key = "FIELD_" + std::to_string(editor.draft.inputs.size() + 1);
        input.label = ui.Format(UiText::FieldLabelFormat, static_cast<int>(editor.draft.inputs.size() + 1));
        editor.draft.inputs.push_back(std::move(input));
        editor.inputButtonsBulkDrafts.push_back({});
        editor.inputButtonsBulkPreviews.push_back({});
        selectInput(static_cast<int>(editor.draft.inputs.size() - 1));
    }

    ImGui::Separator();
    if (editor.draft.inputs.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::InputFieldsEmpty));
        return;
    }

    clampSelectedInput();
    std::vector<InputKeyIssue> inputIssues = buildInputKeyIssues();

    if (!ImGui::BeginTable(
            "##binder_input_editor_layout",
            2,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        return;
    }

    ImGui::TableSetupColumn("fields", ImGuiTableColumnFlags_WidthFixed, ScaleUi(240.0f));
    ImGui::TableSetupColumn("details", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    if (ImGui::BeginChild("##binder_input_fields_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        ImGui::TextDisabled("%s", ui.Text(UiText::InputFieldsListTitle));
        ImGui::Separator();

        for (std::size_t i = 0; i < editor.draft.inputs.size(); ++i) {
            const HotkeyInput& listInput = editor.draft.inputs[i];
            const InputKeyIssue& issue = inputIssues[i];
            const bool hasIssue = issue.empty || issue.invalid || issue.duplicate;
            const std::string title = (hasIssue ? "! " : "") + inputDisplayName(listInput, static_cast<int>(i));
            const float labelWidth = std::max(ScaleUi(60.0f), ImGui::GetContentRegionAvail().x - ScaleUi(24.0f));
            const std::string visibleLabel = EllipsizeText(title, labelWidth);
            const std::string selectableLabel = visibleLabel + "##binder_input_sel_" + std::to_string(i);

            if (ImGui::Selectable(
                    selectableLabel.c_str(),
                    editor.selectedInputIndex == static_cast<int>(i),
                    0,
                    ImVec2(0.0f, 0.0f))) {
                selectInput(static_cast<int>(i));
            }

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(title.c_str());
                ImGui::Separator();
                ImGui::TextDisabled("%s", ui.Text(UiText::ParameterResponseType));
                ImGui::SameLine();
                ImGui::TextUnformatted(InputModeLabel(listInput.mode));
                if (!Trim(listInput.key).empty()) {
                    ImGui::TextDisabled("%s", ui.Format(UiText::InputFieldPlaceholderFormat, NormalizeInputKey(listInput.key).c_str()).c_str());
                }
                if (issue.empty || issue.invalid) {
                    ImGui::TextDisabled("%s", ui.Text(UiText::ValidationInputKeyRequired));
                }
                if (issue.duplicate) {
                    ImGui::TextDisabled("%s", ui.Text(UiText::ValidationInputKeyUnique));
                }
                ImGui::EndTooltip();
            }
        }
    }
    ImGui::EndChild();

    ImGui::TableSetColumnIndex(1);
    if (ImGui::BeginChild("##binder_input_field_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        clampSelectedInput();
        if (editor.selectedInputIndex >= 0 && editor.selectedInputIndex < static_cast<int>(editor.draft.inputs.size())) {
            int currentIndex = editor.selectedInputIndex;
            std::string fieldTitle = inputDisplayName(editor.draft.inputs[static_cast<std::size_t>(currentIndex)], currentIndex);
            ImGui::TextWrapped("%s", fieldTitle.c_str());
            ImGui::Separator();

            if (SmallIconActionButton(ui_icons::AngleUp, "##binder_input_move_up", ui.Text(UiText::MoveUp), actionButtonSize)
                && currentIndex > 0) {
                std::swap(
                    editor.draft.inputs[static_cast<std::size_t>(currentIndex)],
                    editor.draft.inputs[static_cast<std::size_t>(currentIndex - 1)]);
                std::swap(
                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex)],
                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex - 1)]);
                std::swap(
                    editor.inputButtonsBulkPreviews[static_cast<std::size_t>(currentIndex)],
                    editor.inputButtonsBulkPreviews[static_cast<std::size_t>(currentIndex - 1)]);
                selectInput(currentIndex - 1);
            }
            ImGui::SameLine();
            if (SmallIconActionButton(ui_icons::AngleDown, "##binder_input_move_down", ui.Text(UiText::MoveDown), actionButtonSize)
                && currentIndex + 1 < static_cast<int>(editor.draft.inputs.size())) {
                std::swap(
                    editor.draft.inputs[static_cast<std::size_t>(currentIndex)],
                    editor.draft.inputs[static_cast<std::size_t>(currentIndex + 1)]);
                std::swap(
                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex)],
                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex + 1)]);
                std::swap(
                    editor.inputButtonsBulkPreviews[static_cast<std::size_t>(currentIndex)],
                    editor.inputButtonsBulkPreviews[static_cast<std::size_t>(currentIndex + 1)]);
                selectInput(currentIndex + 1);
            }
            ImGui::SameLine();
            if (SmallIconActionButton(ui_icons::Clone, "##binder_input_duplicate", ui.Text(UiText::ActionDuplicate), actionButtonSize)) {
                HotkeyInput duplicate = editor.draft.inputs[static_cast<std::size_t>(currentIndex)];
                editor.draft.inputs.insert(editor.draft.inputs.begin() + currentIndex + 1, std::move(duplicate));
                editor.inputButtonsBulkDrafts.insert(
                    editor.inputButtonsBulkDrafts.begin() + currentIndex + 1,
                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex)]);
                editor.inputButtonsBulkPreviews.insert(
                    editor.inputButtonsBulkPreviews.begin() + currentIndex + 1,
                    {});
                selectInput(currentIndex + 1);
            }
            ImGui::SameLine();
            if (SmallIconActionButton(ui_icons::Delete, "##binder_input_delete", ui.Text(UiText::Delete), actionButtonSize)) {
                editor.draft.inputs.erase(editor.draft.inputs.begin() + currentIndex);
                editor.inputButtonsBulkDrafts.erase(editor.inputButtonsBulkDrafts.begin() + currentIndex);
                editor.inputButtonsBulkPreviews.erase(editor.inputButtonsBulkPreviews.begin() + currentIndex);
                if (editor.draft.inputs.empty()) {
                    editor.selectedInputIndex = -1;
                    editor.selectedInputButtonIndex = -1;
                    editor.selectedButtonsText.clear();
                } else {
                    selectInput(std::min(currentIndex, static_cast<int>(editor.draft.inputs.size()) - 1));
                }
            }

            clampSelectedInput();
            if (editor.selectedInputIndex >= 0 && editor.selectedInputIndex < static_cast<int>(editor.draft.inputs.size())) {
                HotkeyInput& input = editor.draft.inputs[static_cast<std::size_t>(editor.selectedInputIndex)];
                const InputKeyIssue currentIssue = inputIssues[static_cast<std::size_t>(editor.selectedInputIndex)];
                const auto drawModeCombo = [&]() {
                    const InputMode modes[] = { InputMode::Text, InputMode::ButtonsList, InputMode::ButtonsListText };
                    const char* modeLabels[] = {
                        InputModeLabel(InputMode::Text),
                        InputModeLabel(InputMode::ButtonsList),
                        InputModeLabel(InputMode::ButtonsListText),
                    };
                    int modeIndex = 0;
                    for (int i = 0; i < 3; ++i) {
                        if (input.mode == modes[i]) {
                            modeIndex = i;
                            break;
                        }
                    }
                    if (ImGui::Combo("##binder_input_mode", &modeIndex, modeLabels, IM_ARRAYSIZE(modeLabels))) {
                        input.mode = modes[modeIndex];
                    }
                };

                ImGui::Spacing();
                ImGui::SeparatorText(ui.Text(UiText::ParameterQuestionSection));
                ImGui::TextDisabled("%s", ui.Text(UiText::ParameterQuestionHint));
                drawInspectorTable("##binder_input_question_table", [&](const auto& drawRow) {
                    drawRow(UiText::ParameterPrompt, [&]() {
                        InputTextString("##binder_input_name", input.label, ImGuiInputTextFlags_AutoSelectAll, 128);
                    });
                    drawRow(UiText::ParameterHintText, [&]() {
                        InputTextString("##binder_input_hint", input.hint, ImGuiInputTextFlags_AutoSelectAll, 256);
                    });
                });

                ImGui::Spacing();
                ImGui::SeparatorText(ui.Text(UiText::ParameterResponseSection));
                ImGui::TextDisabled("%s", ui.Text(UiText::ParameterResponseHint));
                drawInspectorTable("##binder_input_response_table", [&](const auto& drawRow) {
                    drawRow(UiText::ParameterResponseType, drawModeCombo);
                    if (InputModeUsesButtons(input.mode)) {
                        drawRow(UiText::ParameterAllowMultiple, [&]() {
                            ImGui::Checkbox("##binder_input_multi", &input.multiSelect);
                        });
                        if (input.multiSelect) {
                            drawRow(UiText::ParameterJoinSeparator, [&]() {
                                InputTextString(
                                    "##binder_input_separator",
                                    input.multiSeparator,
                                    ImGuiInputTextFlags_AutoSelectAll,
                                    64);
                            });
                        }
                    }
                });

                ImGui::Spacing();
                ImGui::SeparatorText(ui.Text(UiText::ParameterAdvancedSection));
                ImGui::TextDisabled("%s", ui.Text(UiText::ParameterAdvancedHint));
                drawInspectorTable("##binder_input_advanced_table", [&](const auto& drawRow) {
                    drawRow(UiText::ParameterSystemKey, [&]() {
                        InputTextString("##binder_input_key", input.key, ImGuiInputTextFlags_AutoSelectAll, 64);
                        input.key = NormalizeInputKey(input.key);
                    });
                    if (InputModeUsesButtons(input.mode)) {
                        drawRow(UiText::ParameterDependsOn, [&]() {
                            InputTextString("##binder_input_cascade", input.cascadeParentKey, ImGuiInputTextFlags_AutoSelectAll, 64);
                            input.cascadeParentKey = NormalizeInputKey(input.cascadeParentKey);
                        });
                    }
                });

                const std::string normalizedKey = NormalizeInputKey(input.key);
                if (!normalizedKey.empty()) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("%s", ui.Format(UiText::InputFieldPlaceholderFormat, normalizedKey.c_str()).c_str());
                    ImGui::SameLine();
                    if (SmallIconActionButton(ui_icons::Clone, "##binder_copy_input_placeholder", ui.Text(UiText::CopyPlaceholder), actionButtonSize)) {
                        ImGui::SetClipboardText(("{{" + normalizedKey + "}}").c_str());
                    }
                }
                if (currentIssue.empty || currentIssue.invalid) {
                    ImGui::TextDisabled("%s", ui.Text(UiText::ValidationInputKeyRequired));
                } else if (currentIssue.duplicate) {
                    ImGui::TextDisabled("%s", ui.Text(UiText::ValidationInputKeyUnique));
                }

                if (InputModeUsesButtons(input.mode)) {
                    ImGui::Spacing();
                    ImGui::SeparatorText(ui.Text(UiText::ParameterVariantsSection));
                    ImGui::TextDisabled("%s", ui.Text(UiText::ParameterVariantsHint));
                    if (ImGui::BeginTabBar("##binder_input_buttons_tabs")) {
                        if (ImGui::BeginTabItem(ui.Text(UiText::ButtonsStructuredTab))) {
                            if (ImGui::Button(ui.Text(UiText::AddButton))) {
                                input.buttons.push_back(InputButton{});
                                editor.selectedInputButtonIndex = static_cast<int>(input.buttons.size() - 1);
                                syncSelectedButtonsText(input);
                            }

                            const bool hasSelectedButton =
                                editor.selectedInputButtonIndex >= 0
                                && editor.selectedInputButtonIndex < static_cast<int>(input.buttons.size());
                            if (hasSelectedButton) {
                                ImGui::SameLine();
                                if (SmallIconActionButton(ui_icons::Clone, "##binder_button_duplicate", ui.Text(UiText::ActionDuplicate), actionButtonSize)) {
                                    const int selectedButtonIndex = editor.selectedInputButtonIndex;
                                    InputButton duplicate = input.buttons[static_cast<std::size_t>(selectedButtonIndex)];
                                    input.buttons.insert(input.buttons.begin() + selectedButtonIndex + 1, std::move(duplicate));
                                    editor.selectedInputButtonIndex = selectedButtonIndex + 1;
                                    syncSelectedButtonsText(input);
                                }
                                ImGui::SameLine();
                                if (SmallIconActionButton(ui_icons::AngleUp, "##binder_button_move_up", ui.Text(UiText::MoveUp), actionButtonSize)
                                    && editor.selectedInputButtonIndex > 0) {
                                    const int selectedButtonIndex = editor.selectedInputButtonIndex;
                                    std::swap(
                                        input.buttons[static_cast<std::size_t>(selectedButtonIndex)],
                                        input.buttons[static_cast<std::size_t>(selectedButtonIndex - 1)]);
                                    editor.selectedInputButtonIndex = selectedButtonIndex - 1;
                                    syncSelectedButtonsText(input);
                                }
                                ImGui::SameLine();
                                if (SmallIconActionButton(ui_icons::AngleDown, "##binder_button_move_down", ui.Text(UiText::MoveDown), actionButtonSize)
                                    && editor.selectedInputButtonIndex + 1 < static_cast<int>(input.buttons.size())) {
                                    const int selectedButtonIndex = editor.selectedInputButtonIndex;
                                    std::swap(
                                        input.buttons[static_cast<std::size_t>(selectedButtonIndex)],
                                        input.buttons[static_cast<std::size_t>(selectedButtonIndex + 1)]);
                                    editor.selectedInputButtonIndex = selectedButtonIndex + 1;
                                    syncSelectedButtonsText(input);
                                }
                                ImGui::SameLine();
                                if (SmallIconActionButton(ui_icons::Delete, "##binder_button_delete", ui.Text(UiText::Delete), actionButtonSize)) {
                                    input.buttons.erase(input.buttons.begin() + editor.selectedInputButtonIndex);
                                    if (input.buttons.empty()) {
                                        editor.selectedInputButtonIndex = -1;
                                    } else {
                                        editor.selectedInputButtonIndex = std::min(
                                            editor.selectedInputButtonIndex,
                                            static_cast<int>(input.buttons.size()) - 1);
                                    }
                                    syncSelectedButtonsText(input);
                                }
                            }

                            const float buttonEditorHeight = std::max(ScaleUi(180.0f), ImGui::GetContentRegionAvail().y - ScaleUi(6.0f));
                            if (ImGui::BeginTable(
                                    "##binder_button_editor_layout",
                                    2,
                                    ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
                                ImGui::TableSetupColumn("buttons", ImGuiTableColumnFlags_WidthFixed, ScaleUi(230.0f));
                                ImGui::TableSetupColumn("properties", ImGuiTableColumnFlags_WidthStretch);
                                ImGui::TableNextRow();

                                ImGui::TableSetColumnIndex(0);
                                if (ImGui::BeginChild("##binder_button_list", ImVec2(0.0f, buttonEditorHeight), ImGuiChildFlags_Borders)) {
                                    ImGui::TextDisabled("%s", ui.Text(UiText::ButtonListTitle));
                                    ImGui::Separator();
                                    if (input.buttons.empty()) {
                                        ImGui::TextDisabled("%s", ui.Text(UiText::ButtonsEmpty));
                                    } else {
                                        for (std::size_t buttonIndex = 0; buttonIndex < input.buttons.size(); ++buttonIndex) {
                                            const InputButton& button = input.buttons[buttonIndex];
                                            const std::string title = buttonDisplayName(button, static_cast<int>(buttonIndex));
                                            const float labelWidth = std::max(ScaleUi(60.0f), ImGui::GetContentRegionAvail().x - ScaleUi(24.0f));
                                            const std::string visibleLabel = EllipsizeText(title, labelWidth);
                                            const std::string selectableLabel = visibleLabel + "##binder_button_sel_" + std::to_string(buttonIndex);
                                            if (ImGui::Selectable(
                                                    selectableLabel.c_str(),
                                                    editor.selectedInputButtonIndex == static_cast<int>(buttonIndex),
                                                    0,
                                                    ImVec2(0.0f, 0.0f))) {
                                                editor.selectedInputButtonIndex = static_cast<int>(buttonIndex);
                                            }

                                            if (ImGui::IsItemHovered()) {
                                                ImGui::BeginTooltip();
                                                ImGui::TextUnformatted(title.c_str());
                                                ImGui::Separator();
                                                if (!Trim(button.text).empty()) {
                                                    ImGui::TextDisabled("%s", ui.Text(UiText::OptionValue));
                                                    ImGui::TextWrapped("%s", button.text.c_str());
                                                }
                                                if (!Trim(button.hint).empty()) {
                                                    ImGui::TextDisabled("%s", ui.Text(UiText::OptionHint));
                                                    ImGui::TextWrapped("%s", button.hint.c_str());
                                                }
                                                ImGui::EndTooltip();
                                            }
                                        }
                                    }
                                }
                                ImGui::EndChild();

                                ImGui::TableSetColumnIndex(1);
                                if (ImGui::BeginChild("##binder_button_properties", ImVec2(0.0f, buttonEditorHeight), ImGuiChildFlags_Borders)) {
                                    if (editor.selectedInputButtonIndex >= 0
                                        && editor.selectedInputButtonIndex < static_cast<int>(input.buttons.size())) {
                                        InputButton& button = input.buttons[static_cast<std::size_t>(editor.selectedInputButtonIndex)];
                                        ImGui::TextDisabled("%s", ui.Text(UiText::ButtonPropertiesTitle));
                                        ImGui::Separator();

                                        bool buttonsChanged = false;
                                        buttonsChanged |= InputTextString(
                                            ui.Text(UiText::OptionName),
                                            button.label,
                                            ImGuiInputTextFlags_AutoSelectAll,
                                            128);
                                        buttonsChanged |= InputTextMultilineString(
                                            ui.Text(UiText::OptionValue),
                                            button.text,
                                            ImVec2(-FLT_MIN, ScaleUi(92.0f)),
                                            0,
                                            512);
                                        buttonsChanged |= InputTextString(
                                            ui.Text(UiText::OptionHint),
                                            button.hint,
                                            ImGuiInputTextFlags_AutoSelectAll,
                                            256);
                                        buttonsChanged |= InputTextString(
                                            ui.Text(UiText::ButtonWhen),
                                            button.when,
                                            ImGuiInputTextFlags_AutoSelectAll,
                                            256);
                                        ImGui::TextDisabled("%s", ui.Text(UiText::ButtonWhenHint));

                                        if (buttonsChanged) {
                                            syncSelectedButtonsText(input);
                                        }
                                    } else {
                                        ImGui::TextDisabled("%s", ui.Text(UiText::ButtonsEmpty));
                                    }
                                }
                                ImGui::EndChild();

                                ImGui::EndTable();
                            }

                            ImGui::EndTabItem();
                        }

                        if (ImGui::BeginTabItem(ui.Text(UiText::ButtonsBulkTab))) {
                            ButtonsBulkPreviewState* previewState = nullptr;
                            if (editor.selectedInputIndex >= 0
                                && editor.selectedInputIndex < static_cast<int>(editor.inputButtonsBulkPreviews.size())) {
                                previewState = &editor.inputButtonsBulkPreviews[static_cast<std::size_t>(editor.selectedInputIndex)];
                            }

                            if (ImGui::Button(ui.Text(UiText::ButtonsBulkAddLine))) {
                                AppendButtonsBulkTemplateLines(editor.selectedButtonsText, 1);
                                if (editor.selectedInputIndex >= 0
                                    && editor.selectedInputIndex < static_cast<int>(editor.inputButtonsBulkDrafts.size())) {
                                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(editor.selectedInputIndex)] =
                                        editor.selectedButtonsText;
                                }
                                if (previewState) {
                                    *previewState = {};
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(ui.Text(UiText::ButtonsBulkAddFiveLines))) {
                                AppendButtonsBulkTemplateLines(editor.selectedButtonsText, 5);
                                if (editor.selectedInputIndex >= 0
                                    && editor.selectedInputIndex < static_cast<int>(editor.inputButtonsBulkDrafts.size())) {
                                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(editor.selectedInputIndex)] =
                                        editor.selectedButtonsText;
                                }
                                if (previewState) {
                                    *previewState = {};
                                }
                            }

                            ImGui::Spacing();
                            if (ImGui::Button(ui.Text(UiText::ButtonsBulkSync))) {
                                syncSelectedButtonsText(input);
                                if (previewState) {
                                    *previewState = {};
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(ui.Text(UiText::ButtonsBulkCheck))) {
                                ButtonsTextParseStats stats;
                                const auto buttonsPreview = ParseButtonsTextEx(editor.selectedButtonsText, &stats);
                                if (previewState) {
                                    previewState->active = true;
                                    previewState->count = static_cast<int>(buttonsPreview.size());
                                    previewState->stats = stats;
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(ui.Text(UiText::ButtonsBulkNormalize))) {
                                ButtonsTextParseStats stats;
                                const auto normalizedButtons = ParseButtonsTextEx(editor.selectedButtonsText, &stats);
                                editor.selectedButtonsText = SerializeButtonsText(normalizedButtons);
                                if (editor.selectedInputIndex >= 0
                                    && editor.selectedInputIndex < static_cast<int>(editor.inputButtonsBulkDrafts.size())) {
                                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(editor.selectedInputIndex)] =
                                        editor.selectedButtonsText;
                                }
                                if (previewState) {
                                    *previewState = {};
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(ui.Text(UiText::ButtonsBulkApply))) {
                                input.buttons = ParseButtonsText(editor.selectedButtonsText);
                                syncSelectedButtonsText(input);
                            }

                            ImGui::Spacing();
                            ImGui::TextDisabled("%s", ui.Text(UiText::ButtonsFormatHint));
                            ImGui::TextDisabled("%s", ui.Text(UiText::ButtonsBulkEscapingHint));
                            if (InputTextMultilineString(
                                "##binder_input_buttons_bulk",
                                editor.selectedButtonsText,
                                ImVec2(-FLT_MIN, ScaleUi(220.0f)))) {
                                if (editor.selectedInputIndex >= 0
                                    && editor.selectedInputIndex < static_cast<int>(editor.inputButtonsBulkDrafts.size())) {
                                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(editor.selectedInputIndex)] =
                                        editor.selectedButtonsText;
                                }
                                if (previewState) {
                                    *previewState = {};
                                }
                            }

                            if (previewState && previewState->active) {
                                ImGui::Spacing();
                                ImGui::TextDisabled(
                                    "%s",
                                    ui.Format(
                                          UiText::ButtonsBulkPreviewFormat,
                                          previewState->stats.total,
                                          previewState->stats.used,
                                          previewState->stats.ignored,
                                          previewState->count)
                                        .c_str());
                                if (previewState->stats.extraPipes > 0) {
                                    ImGui::TextDisabled("%s", ui.Text(UiText::ButtonsBulkExtraPipesHint));
                                }
                            }
                            ImGui::EndTabItem();
                        }

                        ImGui::EndTabBar();
                    }
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::EndTable();
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
        ImGui::OpenPopup(kEditorVariablesPopupId);
        editor.variablesPopupPending = false;
    }

    UiSettings& ui = UiSettings::Instance();
    const std::string popupTitle = std::string(ui.Text(UiText::EditorVariablesTitle)) + kEditorVariablesPopupId;
    bool popupOpen = true;
    ImGui::SetNextWindowSize(ScaleUi(920.0f, 640.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(popupTitle.c_str(), &popupOpen, ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    const bool closeRequested = !popupOpen;

    if (editor.variablesKeyPickerPopupPending) {
        ImGui::OpenPopup("##binder_editor_variable_keypicker");
        editor.variablesKeyPickerPopupPending = false;
    }

    const std::vector<variables_picker::Entry> pickerEntries = BuildEditorVariablePickerEntries();
    const variables_picker::Request request = variables_picker::Draw(
        editor.variablesPicker,
        pickerEntries,
        variables_picker::Options{
            variables_picker::Mode::Insert,
            "binder_editor_variables_picker",
            editor.variablesInsertTarget != binder_editor::State::VariableInsertTarget::None,
            false,
            ImGui::GetContentRegionAvail(),
        });
    HandleEditorVariablePickerRequest(request);

    if (tagsModule) {
        tagsModule->DrawVariableHelperPopups([&](std::string_view token) {
            if (!InsertTextIntoEditorVariableTarget(token)) {
                CopyTextToClipboard(token);
            }
        });
    }
    DrawEditorVariableKeyPickerPopup();
    if (closeRequested) {
        editor.variablesKeyPickerSearch.clear();
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
    }
    if (editor.scenarioMessageNoSelectFocusIndex >= static_cast<int>(visibleMessageCount)) {
        editor.scenarioMessageNoSelectFocusIndex = -1;
    }

    const auto appendScenarioMessage = [&]() -> bool {
        if (Trim(editor.scenarioAppendText).empty()) {
            return false;
        }

        HotkeyMessage message;
        message.text = editor.scenarioAppendText;
        if (visibleMessageCount > 0) {
            const HotkeyMessage& previous = editor.draft.messages[visibleMessageCount - 1];
            message.intervalMs = std::max(previous.intervalMs, 0);
            message.method = previous.method;
        } else if (!editor.draft.messages.empty()) {
            const HotkeyMessage& previous = editor.draft.messages.back();
            message.intervalMs = std::max(previous.intervalMs, 0);
            message.method = previous.method;
        } else {
            message.intervalMs = std::max(editor.bulkIntervalMs, 0);
            message.method = editor.bulkMethod;
        }

        int newIndex = 0;
        const bool replacePlaceholder =
            editor.draft.messages.size() == 1 && Trim(editor.draft.messages.front().text).empty();
        if (replacePlaceholder) {
            editor.draft.messages.front() = std::move(message);
        } else {
            editor.draft.messages.push_back(std::move(message));
            newIndex = static_cast<int>(editor.draft.messages.size()) - 1;
        }

        editor.scenarioAppendText.clear();
        editor.scenarioMessageFocusIndex = -1;
        editor.scenarioMessageNoSelectFocusIndex = newIndex;
        return true;
    };

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
            const bool noSelectFocusMessageText = editor.scenarioMessageNoSelectFocusIndex == messageIndex;
            const bool focusMessageText = editor.scenarioMessageFocusIndex == messageIndex || noSelectFocusMessageText;
            const ImGuiID stepTextId = ImGui::GetID("##step_text");
            if (focusMessageText) {
                ImGui::SetKeyboardFocusHere();
                if (noSelectFocusMessageText) {
                    PrepareInputTextNoSelectFocus(stepTextId, static_cast<int>(message.text.size()));
                }
            }
            InputTextMoveCaretToEndData noSelectFocusData{};
            const ImGuiInputTextFlags textFlags = noSelectFocusMessageText
                ? ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackCharFilter
                : 0;
            InputTextString(
                "##step_text",
                message.text,
                textFlags,
                256,
                noSelectFocusMessageText ? MoveCaretToEndInputTextCallback : nullptr,
                noSelectFocusMessageText ? &noSelectFocusData : nullptr);
            if (ImGui::IsItemActive() || ImGui::IsItemFocused()) {
                int cursorByte = -1;
                if (ImGuiInputTextState* inputState = ImGui::GetInputTextState(stepTextId)) {
                    cursorByte = inputState->GetCursorPos();
                }
                RememberEditorVariableInsertTarget(
                    binder_editor::State::VariableInsertTarget::ScenarioMessage,
                    messageIndex,
                    cursorByte);
            }
            if (focusMessageText) {
                if (ImGui::GetInputTextState(stepTextId)) {
                    if (noSelectFocusMessageText && !noSelectFocusData.applied) {
                        MoveInputTextCaretToEnd(stepTextId);
                    }
                    if (editor.scenarioMessageFocusIndex == messageIndex) {
                        editor.scenarioMessageFocusIndex = -1;
                    }
                    if (editor.scenarioMessageNoSelectFocusIndex == messageIndex) {
                        editor.scenarioMessageNoSelectFocusIndex = -1;
                    }
                }
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
        if (editor.scenarioAppendFocusPending) {
            ImGui::SetKeyboardFocusHere();
            editor.scenarioAppendFocusPending = false;
        }
        const bool appendChanged = InputTextWithHintString(
            "##step_append_text",
            ui.Text(UiText::EditorAppendStepHint),
            editor.scenarioAppendText,
            0,
            256);
        if (ImGui::IsItemActive() || ImGui::IsItemFocused()) {
            const ImGuiID appendTextId = ImGui::GetID("##step_append_text");
            int cursorByte = -1;
            if (ImGuiInputTextState* inputState = ImGui::GetInputTextState(appendTextId)) {
                cursorByte = inputState->GetCursorPos();
            }
            RememberEditorVariableInsertTarget(
                binder_editor::State::VariableInsertTarget::ScenarioAppend,
                -1,
                cursorByte);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s", ui.Text(UiText::EditorAppendStepTooltip));
        }
        if (appendChanged && appendScenarioMessage()) {
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
            && !appendScenarioMessage()) {
            editor.scenarioAppendFocusPending = true;
        }
        ImGui::PopID();

        ImGui::EndTable();
    }

    if (moveSourceIndex >= 0 && moveTargetIndex >= 0 && moveSourceIndex != moveTargetIndex) {
        HotkeyMessage moved = editor.draft.messages[static_cast<std::size_t>(moveSourceIndex)];
        editor.draft.messages.erase(editor.draft.messages.begin() + moveSourceIndex);
        editor.draft.messages.insert(editor.draft.messages.begin() + moveTargetIndex, std::move(moved));
    }
    if (removeIndex >= 0 && removeIndex < static_cast<int>(editor.draft.messages.size())) {
        if (editor.draft.messages.size() > 1) {
            editor.draft.messages.erase(editor.draft.messages.begin() + removeIndex);
        } else {
            editor.draft.messages.front() = HotkeyMessage{ "", 0, editor.bulkMethod };
        }
    }

    SyncEditorMessagesToMulti();
}

void BinderModule::Impl::DrawEditorMultiInputPopup() {
    UiSettings& ui = UiSettings::Instance();
    const std::string title = std::string(ui.Text(UiText::EditorMultiInputTitle)) + "##binder_editor_multi_input_popup";
    if (editor.multiInputPopupPending) {
        ImGui::OpenPopup(title.c_str());
        editor.multiInputPopupPending = false;
    }
    ImGui::SetNextWindowSize(ScaleUi(640.0f, 460.0f), ImGuiCond_Appearing);
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
