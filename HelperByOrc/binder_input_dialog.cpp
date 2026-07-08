#include "binder_module_impl.h"

std::string BinderModule::Impl::ApplyInputValues(std::string text, const std::map<std::string, std::string>& values) const {
    static const std::regex kPlaceholder("\\{\\{([A-Za-z0-9_]+)\\}\\}");
    std::string result;
    std::sregex_iterator it(text.begin(), text.end(), kPlaceholder);
    std::sregex_iterator end;
    std::size_t lastPos = 0;
    for (; it != end; ++it) {
        const auto& match = *it;
        result.append(text, lastPos, static_cast<std::size_t>(match.position()) - lastPos);
        const std::string key = match[1].str();
        auto valueIt = values.find(key);
        if (valueIt == values.end()) {
            valueIt = values.find(ToLower(key));
        }
        if (valueIt == values.end()) {
            valueIt = values.find(NormalizeInputKey(key));
        }
        if (valueIt != values.end()) {
            result += valueIt->second;
        }
        lastPos = static_cast<std::size_t>(match.position() + match.length());
    }
    result.append(text, lastPos, std::string::npos);
    return result;
}

std::string BinderModule::Impl::BuildInputValue(const InputDialogField& field) const {
    const auto buttonText = [&](int index) -> std::string {
        if (index < 0 || index >= static_cast<int>(field.input.buttons.size())) {
            return {};
        }
        const InputButton& button = field.input.buttons[static_cast<std::size_t>(index)];
        return button.text;
    };

    if (field.input.mode == InputMode::Text) {
        return field.textValue;
    }

    if (field.input.multiSelect) {
        std::ostringstream stream;
        bool first = true;
        for (const int selected : field.selectedButtons) {
            const std::string textValue = buttonText(selected);
            if (textValue.empty()) {
                continue;
            }
            if (!first) {
                stream << (field.input.multiSeparator.empty() ? ", " : field.input.multiSeparator);
            }
            stream << textValue;
            first = false;
        }
        if (!field.textValue.empty() && field.input.mode == InputMode::ButtonsListText) {
            return field.textValue;
        }
        return stream.str();
    }

    if (field.selectedButtonIndex.has_value()) {
        const std::string textValue = buttonText(*field.selectedButtonIndex);
        if (!textValue.empty()) {
            return field.input.mode == InputMode::ButtonsListText && !field.textValue.empty() ? field.textValue : textValue;
        }
    }

    return field.textValue;
}

std::vector<int> BinderModule::Impl::FilterButtons(const InputDialogState& dialog, std::size_t fieldIndex) const {
    std::vector<int> result;
    if (fieldIndex >= dialog.fields.size()) {
        return result;
    }

    const HotkeyInput& input = dialog.fields[fieldIndex].input;
    const std::string parentKey = NormalizeInputKey(input.cascadeParentKey);
    int parentIndex = -1;
    if (!parentKey.empty()) {
        for (std::size_t i = 0; i < dialog.fields.size(); ++i) {
            if (i == fieldIndex) {
                continue;
            }
            if (NormalizeInputKey(dialog.fields[i].input.key) == parentKey) {
                parentIndex = static_cast<int>(i);
                break;
            }
        }
    }

    std::set<std::string> selectedTokens;
    if (parentIndex >= 0) {
        const InputDialogField& parent = dialog.fields[static_cast<std::size_t>(parentIndex)];
        const auto addToken = [&](std::string token) {
            token = ToLower(Trim(token));
            if (!token.empty()) {
                selectedTokens.insert(std::move(token));
            }
        };

        if (parent.selectedButtonIndex.has_value()) {
            const int idx = *parent.selectedButtonIndex;
            if (idx >= 0 && idx < static_cast<int>(parent.input.buttons.size())) {
                addToken(parent.input.buttons[static_cast<std::size_t>(idx)].label);
                addToken(parent.input.buttons[static_cast<std::size_t>(idx)].text);
            }
        }
        for (const int idx : parent.selectedButtons) {
            if (idx >= 0 && idx < static_cast<int>(parent.input.buttons.size())) {
                addToken(parent.input.buttons[static_cast<std::size_t>(idx)].label);
                addToken(parent.input.buttons[static_cast<std::size_t>(idx)].text);
            }
        }
    }

    for (std::size_t i = 0; i < input.buttons.size(); ++i) {
        const InputButton& button = input.buttons[i];
        if (parentIndex >= 0 && !Trim(button.when).empty()) {
            bool matches = false;
            for (const std::string& token : Split(button.when, '|')) {
                if (selectedTokens.contains(ToLower(Trim(token)))) {
                    matches = true;
                    break;
                }
            }
            if (!matches) {
                continue;
            }
        }
        result.push_back(static_cast<int>(i));
    }
    return result;
}

void BinderModule::Impl::DrawInputDialog() {
    if (inputDialog) {
        const bool compactDialog = inputDialog->fields.size() == 1;
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const ImVec2 minWindowSize = compactDialog ? ScaleUi(420.0f, 280.0f) : ScaleUi(560.0f, 360.0f);
        const ImVec2 preferredWindowSize = compactDialog ? ScaleUi(560.0f, 460.0f) : ScaleUi(720.0f, 560.0f);
        const ImVec2 maxWindowSize(
            std::max(minWindowSize.x, displaySize.x - ScaleUi(24.0f)),
            std::max(minWindowSize.y, displaySize.y - ScaleUi(24.0f)));
        ImGui::SetNextWindowSizeConstraints(minWindowSize, maxWindowSize);
        ImGui::SetNextWindowSize(
            ImVec2(std::min(preferredWindowSize.x, maxWindowSize.x), std::min(preferredWindowSize.y, maxWindowSize.y)),
            ImGuiCond_Appearing);
        ImGui::OpenPopup("##binder_input_dialog");
    }

    if (!ImGui::BeginPopupModal("##binder_input_dialog", nullptr)) {
        return;
    }

    if (!inputDialog || inputDialog->hotkeyIndex < 0 || inputDialog->hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        inputDialog.reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    HotkeyEntry& hotkey = hotkeys[inputDialog->hotkeyIndex];
    const bool compactDialog = inputDialog->fields.size() == 1;
    const bool dialogAppearing = ImGui::IsWindowAppearing();
    bool focusAssigned = false;
    bool submitRequested = false;
    bool cancelRequested = false;
    const bool focusSearchShortcut =
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F, ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteOverActive);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Enter, ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteOverActive)
        || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadEnter, ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteOverActive)) {
        submitRequested = true;
    }
    if (ImGui::Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteOverActive)) {
        cancelRequested = true;
    }

    ImGui::TextWrapped("%s", ui.Format(UiText::FillBindParametersFormat, BuildBindDisplayLabel(hotkey).c_str()).c_str());
    ImGui::Separator();

    auto rebuildSelectedText = [](InputDialogField& field) {
        std::ostringstream stream;
        bool first = true;
        for (const int idx : field.selectedButtons) {
            if (idx < 0 || idx >= static_cast<int>(field.input.buttons.size())) {
                continue;
            }
            const InputButton& button = field.input.buttons[static_cast<std::size_t>(idx)];
            if (button.text.empty()) {
                continue;
            }
            if (!first) {
                stream << (field.input.multiSeparator.empty() ? ", " : field.input.multiSeparator);
            }
            stream << button.text;
            first = false;
        }
        field.textValue = stream.str();
    };
    auto dialogButtonLabel = [](const InputButton& button, int buttonIndex) {
        const std::string label = Trim(button.label);
        if (!label.empty()) {
            return label;
        }
        const std::string text = Trim(button.text);
        if (!text.empty()) {
            return text;
        }
        return std::to_string(buttonIndex + 1);
    };
    const auto dialogButtonMatchesQuery = [&](const InputButton& button, int buttonIndex, std::string_view query) {
        const std::string normalizedQuery = ToLower(Trim(query));
        if (normalizedQuery.empty()) {
            return true;
        }

        const std::string label = ToLower(dialogButtonLabel(button, buttonIndex));
        if (label.find(normalizedQuery) != std::string::npos) {
            return true;
        }
        if (ToLower(button.text).find(normalizedQuery) != std::string::npos) {
            return true;
        }
        return ToLower(button.hint).find(normalizedQuery) != std::string::npos;
    };
    const auto drawPreviewBlock = [&](bool compactPreview) {
        std::map<std::string, std::string> values;
        for (std::size_t fieldIndex = 0; fieldIndex < inputDialog->fields.size(); ++fieldIndex) {
            const InputDialogField& previewField = inputDialog->fields[fieldIndex];
            const std::string key = NormalizeInputKey(previewField.input.key);
            const std::string value = BuildInputValue(previewField);
            if (!key.empty()) {
                values[key] = value;
                values[ToLower(key)] = value;
            }
            values[std::to_string(fieldIndex + 1)] = value;
        }

        ImGui::SeparatorText(ui.Text(UiText::InputDialogPreviewTitle));
        const ImVec2 previewSize = compactPreview ? ImVec2(0.0f, ScaleUi(104.0f)) : ImVec2(0.0f, ScaleUi(124.0f));
        if (!ImGui::BeginChild("##binder_input_preview", previewSize, ImGuiChildFlags_FrameStyle)) {
            ImGui::EndChild();
            return;
        }

        bool hasPreviewRows = false;
        for (std::size_t messageIndex = 0; messageIndex < hotkey.messages.size(); ++messageIndex) {
            const HotkeyMessage& message = hotkey.messages[messageIndex];
            std::string previewText = ApplyInputValues(message.text, values);
            if (tagsModule) {
                previewText = tagsModule->ExpandText(previewText, TagsModule::EvaluationContext{
                                                                     sampApi,
                                                                     inputDialog->activationSource,
                                                                     inputDialog->activationText,
                                                                     inputDialog->bindCommand,
                                                                     false,
                                                                 });
            }
            std::replace(previewText.begin(), previewText.end(), '\r', ' ');
            std::replace(previewText.begin(), previewText.end(), '\n', ' ');
            if (Trim(previewText).empty()) {
                continue;
            }

            hasPreviewRows = true;
            ImGui::TextWrapped(
                "%d. [%s] %s",
                static_cast<int>(messageIndex + 1),
                SendMethodLabel(message.method),
                previewText.c_str());
        }

        if (!hasPreviewRows) {
            ImGui::TextDisabled("%s", ui.Text(UiText::InputDialogPreviewEmpty));
        }
        ImGui::EndChild();
    };
    const auto applyButtonSelection = [&](InputDialogField& field, int buttonIndex) {
        const InputButton& button = field.input.buttons[static_cast<std::size_t>(buttonIndex)];
        if (field.input.multiSelect) {
            field.selectedButtonIndex.reset();
            if (field.selectedButtons.contains(buttonIndex)) {
                field.selectedButtons.erase(buttonIndex);
            } else {
                field.selectedButtons.insert(buttonIndex);
            }
            rebuildSelectedText(field);
        } else {
            field.selectedButtons.clear();
            field.selectedButtonIndex = buttonIndex;
            if (field.input.mode == InputMode::ButtonsListText) {
                field.textValue = button.text;
            }
        }
    };
    const auto drawSearchInput = [&](InputDialogField& field, bool wantFocus) {
        if (wantFocus) {
            ImGui::SetKeyboardFocusHere();
            focusAssigned = true;
        }
        ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_F, ImGuiInputFlags_Tooltip);
        ImGui::SetNextItemWidth(-1.0f);
        InputTextWithHintString(
            "##input_search",
            ui.Text(UiText::InputDialogSearchHint),
            field.searchValue,
            ImGuiInputTextFlags_AutoSelectAll,
            128);
    };
    const auto drawButtonChoice = [&](const std::string& label, bool selected) {
        if (selected) {
            const ImVec4 activeColor = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
            ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, activeColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
        }
        const bool clicked = ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f));
        if (selected) {
            ImGui::PopStyleColor(3);
        }
        return clicked;
    };
    const auto drawRegularButtonList = [&](InputDialogField& field, const std::vector<int>& shownButtons) -> bool {
        bool picked = false;
        if (ImGui::BeginChild("##input_buttons", ImVec2(0.0f, ScaleUi(160.0f)), ImGuiChildFlags_FrameStyle)) {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(shownButtons.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const int buttonIndex = shownButtons[static_cast<std::size_t>(row)];
                    const InputButton& button = field.input.buttons[static_cast<std::size_t>(buttonIndex)];
                    std::string label = dialogButtonLabel(button, buttonIndex);
                    const bool selected =
                        field.input.multiSelect ? field.selectedButtons.contains(buttonIndex) : field.selectedButtonIndex.value_or(-1) == buttonIndex;
                    if (selected && field.input.multiSelect) {
                        label = std::string(ui_icons::Check) + " " + label;
                    }
                    if (drawButtonChoice(label, selected)) {
                        applyButtonSelection(field, buttonIndex);
                        picked = true;
                    }
                    if (!button.hint.empty() && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", button.hint.c_str());
                    }
                }
            }
            if (shownButtons.empty()) {
                ImGui::TextDisabled("%s", ui.Text(UiText::InputDialogNoOptions));
            }
        }
        ImGui::EndChild();
        return picked;
    };
    const auto drawCompactButtonList = [&](InputDialogField& field, const std::vector<int>& shownButtons) -> bool {
        bool picked = false;
        if (ImGui::BeginChild("##input_buttons_compact", ImVec2(0.0f, ScaleUi(176.0f)), ImGuiChildFlags_FrameStyle)) {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(shownButtons.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const int buttonIndex = shownButtons[static_cast<std::size_t>(row)];
                    const InputButton& button = field.input.buttons[static_cast<std::size_t>(buttonIndex)];
                    std::string label = dialogButtonLabel(button, buttonIndex);
                    const bool selected =
                        field.input.multiSelect ? field.selectedButtons.contains(buttonIndex) : field.selectedButtonIndex.value_or(-1) == buttonIndex;
                    if (selected && field.input.multiSelect) {
                        label = std::string(ui_icons::Check) + " " + label;
                    }
                    if (drawButtonChoice(label, selected)) {
                        applyButtonSelection(field, buttonIndex);
                        picked = true;
                    }
                    if (!button.hint.empty() && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", button.hint.c_str());
                    }
                }
            }
            if (shownButtons.empty()) {
                ImGui::TextDisabled("%s", ui.Text(UiText::InputDialogNoOptions));
            }
        }
        ImGui::EndChild();
        return picked;
    };
    const auto drawFieldTextEditor = [&](InputDialogField& field, bool wantFocus, bool compactEditor) {
        if (wantFocus) {
            ImGui::SetKeyboardFocusHere();
            focusAssigned = true;
        }
        const float height = compactEditor ? ScaleUi(148.0f) : ScaleUi(112.0f);
        const bool changed = InputTextMultilineWithCounterString(
            "##input_text_value",
            field.textValue,
            ImVec2(-FLT_MIN, height),
            ImGuiInputTextFlags_AllowTabInput,
            512);
        if (changed && field.input.mode == InputMode::ButtonsListText) {
            field.selectedButtonIndex.reset();
            field.selectedButtons.clear();
        }
    };
    const auto submitDialog = [&]() {
        if (!IsHotkeyEffectivelyEnabled(hotkey)) {
            Notify(
                NotificationGroup::BinderErrors,
                NotificationSeverity::Warning,
                ui.Text(UiText::BindFolderDisabledTooltip),
                2200.0);
            hotkey.awaitingInput = false;
            inputDialog.reset();
            ImGui::CloseCurrentPopup();
            return;
        }

        std::map<std::string, std::string> values;
        for (std::size_t i = 0; i < inputDialog->fields.size(); ++i) {
            const InputDialogField& field = inputDialog->fields[i];
            const std::string key = NormalizeInputKey(field.input.key);
            const std::string value = BuildInputValue(field);
            if (!key.empty()) {
                values[key] = value;
                values[ToLower(key)] = value;
            }
            values[std::to_string(i + 1)] = value;
        }

        StartRunningBind(
            hotkey,
            std::move(values),
            inputDialog->startDelayMs,
            inputDialog->activationSource,
            inputDialog->activationText,
            inputDialog->bindCommand);
        hotkey.awaitingInput = false;
        inputDialog.reset();
        ImGui::CloseCurrentPopup();
    };
    const auto cancelDialog = [&]() {
        hotkey.awaitingInput = false;
        inputDialog.reset();
        ImGui::CloseCurrentPopup();
    };

    for (std::size_t i = 0; i < inputDialog->fields.size(); ++i) {
        InputDialogField& field = inputDialog->fields[i];
        ImGui::PushID(static_cast<int>(i));
        if (!compactDialog || i == 0) {
            const std::string title = field.input.label.empty() ? field.input.key : field.input.label;
            ImGui::TextWrapped("%s", title.c_str());
            ImGui::Separator();
        }
        if (!field.input.hint.empty()) {
            ImGui::TextWrapped("%s", field.input.hint.c_str());
        }

        if (field.input.mode == InputMode::Text) {
            drawFieldTextEditor(field, !focusAssigned && dialogAppearing, compactDialog);
        } else {
            const auto filteredButtons = FilterButtons(*inputDialog, i);
            std::set<int> visibleButtons(filteredButtons.begin(), filteredButtons.end());
            if (field.input.multiSelect) {
                bool selectionChanged = false;
                for (auto it = field.selectedButtons.begin(); it != field.selectedButtons.end();) {
                    if (!visibleButtons.contains(*it)) {
                        it = field.selectedButtons.erase(it);
                        selectionChanged = true;
                    } else {
                        ++it;
                    }
                }
                if (selectionChanged) {
                    rebuildSelectedText(field);
                }
            } else if (field.selectedButtonIndex.has_value() && !visibleButtons.contains(*field.selectedButtonIndex)) {
                field.selectedButtonIndex.reset();
            }

            drawSearchInput(
                field,
                !focusAssigned && InputModeUsesButtons(field.input.mode) && (dialogAppearing || focusSearchShortcut));

            std::vector<int> shownButtons;
            shownButtons.reserve(filteredButtons.size());
            for (const int buttonIndex : filteredButtons) {
                const InputButton& button = field.input.buttons[static_cast<std::size_t>(buttonIndex)];
                if (dialogButtonMatchesQuery(button, buttonIndex, field.searchValue)) {
                    shownButtons.push_back(buttonIndex);
                }
            }

            const bool picked = compactDialog ? drawCompactButtonList(field, shownButtons) : drawRegularButtonList(field, shownButtons);
            if (compactDialog && picked && field.input.mode == InputMode::ButtonsList && !field.input.multiSelect) {
                submitRequested = true;
            }

            if (field.input.mode == InputMode::ButtonsListText) {
                drawFieldTextEditor(field, false, compactDialog);
            }
        }

        if (!compactDialog && i + 1 != inputDialog->fields.size()) {
            ImGui::Spacing();
        }
        ImGui::PopID();
    }

    drawPreviewBlock(compactDialog);
    ImGui::Separator();
    ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_Enter, ImGuiInputFlags_Tooltip);
    if (ImGui::Button(ui.Text(UiText::Launch))) {
        submitRequested = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemShortcut(ImGuiKey_Escape, ImGuiInputFlags_Tooltip);
    if (ImGui::Button(ui.Text(UiText::Cancel))) {
        cancelRequested = true;
    }

    if (submitRequested) {
        submitDialog();
    } else if (cancelRequested) {
        cancelDialog();
    }

    ImGui::EndPopup();
}
