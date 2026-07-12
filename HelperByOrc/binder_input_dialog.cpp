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
    constexpr int kSearchThreshold = 7;
    constexpr char kPopupId[] = "###binder_input_dialog";

    if (!inputDialog) {
        return;
    }
    if (inputDialog->hotkeyIndex < 0 || inputDialog->hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        inputDialog.reset();
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    HotkeyEntry& hotkey = hotkeys[inputDialog->hotkeyIndex];
    const bool compactDialog = inputDialog->fields.size() == 1;
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImVec2 minimumSize = compactDialog ? ScaleUi(400.0f, 240.0f) : ScaleUi(460.0f, 280.0f);
    const ImVec2 maximumSize(
        std::max(minimumSize.x, displaySize.x - ScaleUi(24.0f)),
        std::max(minimumSize.y, displaySize.y - ScaleUi(24.0f)));
    ImGui::SetNextWindowSizeConstraints(minimumSize, maximumSize);
    ImGui::SetNextWindowSize(ImVec2(std::min(ScaleUi(580.0f), maximumSize.x), 0.0f), ImGuiCond_Appearing);
    if (!inputDialog->popupOpened) {
        ImGui::OpenPopup(kPopupId);
        inputDialog->popupOpened = true;
    }

    const std::string popupTitle = BuildBindDisplayLabel(hotkey) + kPopupId;
    bool popupOpen = true;
    const bool popupVisible = ImGui::BeginPopupModal(popupTitle.c_str(), &popupOpen, ImGuiWindowFlags_AlwaysAutoResize);
    if (!popupOpen) {
        hotkey.awaitingInput = false;
        inputDialog.reset();
        if (popupVisible) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        return;
    }
    if (!popupVisible) {
        if (!ImGui::IsPopupOpen(kPopupId)) {
            hotkey.awaitingInput = false;
            inputDialog.reset();
        }
        return;
    }

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

    auto rebuildSelectedText = [](InputDialogField& field) {
        std::ostringstream stream;
        bool first = true;
        for (const int index : field.selectedButtons) {
            if (index < 0 || index >= static_cast<int>(field.input.buttons.size())) {
                continue;
            }
            const std::string& value = field.input.buttons[static_cast<std::size_t>(index)].text;
            if (value.empty()) {
                continue;
            }
            if (!first) {
                stream << (field.input.multiSeparator.empty() ? ", " : field.input.multiSeparator);
            }
            stream << value;
            first = false;
        }
        field.textValue = stream.str();
    };
    const auto dialogButtonLabel = [](const InputButton& button, int buttonIndex) {
        const std::string label = Trim(button.label);
        if (!label.empty()) {
            return label;
        }
        const std::string text = Trim(button.text);
        return text.empty() ? std::to_string(buttonIndex + 1) : text;
    };
    const auto dialogButtonMatchesQuery = [&](const InputButton& button, int buttonIndex, std::string_view normalizedQuery) {
        if (normalizedQuery.empty()) {
            return true;
        }
        return ToLower(dialogButtonLabel(button, buttonIndex)).find(normalizedQuery) != std::string::npos
            || ToLower(button.text).find(normalizedQuery) != std::string::npos
            || ToLower(button.hint).find(normalizedQuery) != std::string::npos;
    };
    const auto applyButtonSelection = [&](InputDialogField& field, int buttonIndex) {
        const InputButton& button = field.input.buttons[static_cast<std::size_t>(buttonIndex)];
        field.activeButtonIndex = buttonIndex;
        if (field.input.multiSelect) {
            field.selectedButtonIndex.reset();
            if (field.selectedButtons.contains(buttonIndex)) {
                field.selectedButtons.erase(buttonIndex);
            } else {
                field.selectedButtons.insert(buttonIndex);
            }
            rebuildSelectedText(field);
            return;
        }
        field.selectedButtons.clear();
        field.selectedButtonIndex = buttonIndex;
        if (field.input.mode == InputMode::ButtonsListText) {
            field.textValue = button.text;
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

    inputDialog->activeFieldIndex = std::clamp(
        inputDialog->activeFieldIndex,
        0,
        std::max(0, static_cast<int>(inputDialog->fields.size()) - 1));
    std::vector<std::vector<int>> availableButtons(inputDialog->fields.size());
    for (std::size_t i = 0; i < inputDialog->fields.size(); ++i) {
        InputDialogField& field = inputDialog->fields[i];
        if (!InputModeUsesButtons(field.input.mode)) {
            continue;
        }
        availableButtons[i] = FilterButtons(*inputDialog, i);
        const auto isVisible = [&](int buttonIndex) {
            return std::binary_search(availableButtons[i].begin(), availableButtons[i].end(), buttonIndex);
        };
        if (field.input.multiSelect) {
            bool changed = false;
            for (auto it = field.selectedButtons.begin(); it != field.selectedButtons.end();) {
                if (!isVisible(*it)) {
                    it = field.selectedButtons.erase(it);
                    changed = true;
                } else {
                    ++it;
                }
            }
            if (changed) {
                rebuildSelectedText(field);
            }
        } else if (field.selectedButtonIndex.has_value() && !isVisible(*field.selectedButtonIndex)) {
            field.selectedButtonIndex.reset();
            if (field.input.mode == InputMode::ButtonsListText) {
                field.textValue.clear();
            }
        }
        if (!isVisible(field.activeButtonIndex)) {
            field.activeButtonIndex = availableButtons[i].empty() ? -1 : availableButtons[i].front();
        }
    }

    std::optional<std::pair<std::size_t, int>> previewOverride;
    const auto drawOptionRow = [&](InputDialogField& field, int buttonIndex, bool active) {
        const InputButton& button = field.input.buttons[static_cast<std::size_t>(buttonIndex)];
        std::string label = dialogButtonLabel(button, buttonIndex);
        if (field.input.multiSelect && field.selectedButtons.contains(buttonIndex)) {
            label = std::string(ui_icons::Check) + "  " + label;
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        const float width = std::max(ScaleUi(120.0f), ImGui::GetContentRegionAvail().x);
        const float textWidth = std::max(ScaleUi(80.0f), width - style.FramePadding.x * 2.0f);
        const float fullTextHeight = ImGui::CalcTextSize(label.c_str(), nullptr, false, textWidth).y;
        const float textHeight = std::min(fullTextHeight, ImGui::GetTextLineHeight() * 2.0f);
        const float rowHeight = std::max(ImGui::GetFrameHeight(), textHeight + style.FramePadding.y * 2.0f);
        const bool clicked = ImGui::InvisibleButton("##option_row", ImVec2(width, rowHeight));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 rowMin = ImGui::GetItemRectMin();
        const ImVec2 rowMax = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (active || hovered) {
            drawList->AddRectFilled(
                rowMin,
                rowMax,
                ImGui::GetColorU32(active ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered),
                style.FrameRounding);
        }
        const ImVec2 textPos(rowMin.x + style.FramePadding.x, rowMin.y + style.FramePadding.y);
        const ImVec4 clipRect(rowMin.x, rowMin.y, rowMax.x, rowMax.y);
        drawList->AddText(
            ImGui::GetFont(),
            ImGui::GetFontSize(),
            textPos,
            ImGui::GetColorU32(ImGuiCol_Text),
            label.c_str(),
            nullptr,
            textWidth,
            &clipRect);
        if (hovered) {
            const bool customTextWins = field.input.mode == InputMode::ButtonsListText
                && !field.textValue.empty()
                && !field.selectedButtonIndex.has_value();
            if (!customTextWins) {
                previewOverride = std::make_pair(
                    static_cast<std::size_t>(&field - inputDialog->fields.data()),
                    buttonIndex);
            }
            if (fullTextHeight > textHeight || !button.hint.empty()) {
                ImGui::BeginTooltip();
                ImGui::TextWrapped("%s", label.c_str());
                if (!button.hint.empty()) {
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", button.hint.c_str());
                }
                ImGui::EndTooltip();
            }
        }
        return clicked;
    };
    const auto drawTextEditor = [&](InputDialogField& field, bool wantFocus) {
        if (wantFocus) {
            ImGui::SetKeyboardFocusHere();
            focusAssigned = true;
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool changed = InputTextString(
            "##input_text_value",
            field.textValue,
            ImGuiInputTextFlags_AutoSelectAll,
            512);
        if (changed && field.input.mode == InputMode::ButtonsListText) {
            field.selectedButtonIndex.reset();
            field.selectedButtons.clear();
        }
    };

    for (std::size_t i = 0; i < inputDialog->fields.size(); ++i) {
        InputDialogField& field = inputDialog->fields[i];
        const std::string fieldTitle = Trim(field.input.label).empty() ? field.input.key : field.input.label;
        ImGui::PushID(static_cast<int>(i));

        if (!compactDialog) {
            std::string value = Trim(BuildInputValue(field));
            if (value.empty()) {
                value = ui.Text(UiText::InputDialogEmptyValue);
            }
            value = EllipsizeText(value, std::max(ScaleUi(120.0f), ImGui::GetContentRegionAvail().x * 0.45f));
            const std::string summary = ui.Format(UiText::InputDialogFieldSummaryFormat, fieldTitle.c_str(), value.c_str());
            if (ImGui::Selectable(
                    (summary + "###field_header").c_str(),
                    inputDialog->activeFieldIndex == static_cast<int>(i),
                    0,
                    ImVec2(0.0f, 0.0f))) {
                inputDialog->activeFieldIndex = static_cast<int>(i);
            }
            if (inputDialog->activeFieldIndex != static_cast<int>(i)) {
                ImGui::PopID();
                continue;
            }
        } else {
            ImGui::TextWrapped("%s", fieldTitle.c_str());
            ImGui::Separator();
        }

        if (!field.input.hint.empty()) {
            ImGui::TextDisabled("%s", field.input.hint.c_str());
        }

        if (field.input.mode == InputMode::Text) {
            drawTextEditor(field, !focusAssigned && dialogAppearing);
            ImGui::PopID();
            continue;
        }

        const bool showSearch = availableButtons[i].size() >= kSearchThreshold;
        if (showSearch) {
            if (!focusAssigned && (dialogAppearing || focusSearchShortcut)) {
                ImGui::SetKeyboardFocusHere();
                focusAssigned = true;
            }
            ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_F, ImGuiInputFlags_Tooltip);
            ImGui::SetNextItemWidth(-FLT_MIN);
            InputTextWithHintString(
                "##input_search",
                ui.Text(UiText::InputDialogSearchHint),
                field.searchValue,
                ImGuiInputTextFlags_AutoSelectAll,
                128);
        } else {
            field.searchValue.clear();
        }

        std::vector<int> shownButtons;
        shownButtons.reserve(availableButtons[i].size());
        const std::string normalizedQuery = ToLower(Trim(field.searchValue));
        for (const int buttonIndex : availableButtons[i]) {
            if (dialogButtonMatchesQuery(field.input.buttons[static_cast<std::size_t>(buttonIndex)], buttonIndex, normalizedQuery)) {
                shownButtons.push_back(buttonIndex);
            }
        }
        if (std::find(shownButtons.begin(), shownButtons.end(), field.activeButtonIndex) == shownButtons.end()) {
            field.activeButtonIndex = shownButtons.empty() ? -1 : shownButtons.front();
        }

        const bool moveDown = ImGui::Shortcut(
            ImGuiKey_DownArrow,
            ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteOverActive | ImGuiInputFlags_Repeat);
        const bool moveUp = ImGui::Shortcut(
            ImGuiKey_UpArrow,
            ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteOverActive | ImGuiInputFlags_Repeat);
        const bool keyboardMoved = moveDown || moveUp;
        if ((moveDown || moveUp) && !shownButtons.empty()) {
            auto activeIt = std::find(shownButtons.begin(), shownButtons.end(), field.activeButtonIndex);
            int position = activeIt == shownButtons.end() ? 0 : static_cast<int>(activeIt - shownButtons.begin());
            position = std::clamp(position + (moveDown ? 1 : -1), 0, static_cast<int>(shownButtons.size()) - 1);
            field.activeButtonIndex = shownButtons[static_cast<std::size_t>(position)];
        }

        const float averageRowHeight = ImGui::GetFrameHeightWithSpacing() * 1.55f;
        const float listHeight = std::clamp(
            averageRowHeight * static_cast<float>(std::max<std::size_t>(shownButtons.size(), 2)),
            ScaleUi(88.0f),
            std::min(ScaleUi(300.0f), displaySize.y * 0.42f));
        if (ImGui::BeginChild("##input_options", ImVec2(0.0f, listHeight), ImGuiChildFlags_FrameStyle)) {
            if (shownButtons.empty()) {
                ImGui::TextDisabled("%s", ui.Text(UiText::InputDialogNoOptions));
            }
            for (const int buttonIndex : shownButtons) {
                ImGui::PushID(buttonIndex);
                const bool selected = field.input.multiSelect
                    ? field.selectedButtons.contains(buttonIndex)
                    : field.selectedButtonIndex.value_or(-1) == buttonIndex;
                const bool clicked = drawOptionRow(field, buttonIndex, selected || field.activeButtonIndex == buttonIndex);
                if (keyboardMoved && field.activeButtonIndex == buttonIndex) {
                    ImGui::SetScrollHereY(0.5f);
                }
                ImGui::PopID();
                if (!clicked) {
                    continue;
                }
                applyButtonSelection(field, buttonIndex);
                if (compactDialog && field.input.mode == InputMode::ButtonsList && !field.input.multiSelect) {
                    submitRequested = true;
                } else if (!compactDialog && !field.input.multiSelect && i + 1 < inputDialog->fields.size()) {
                    inputDialog->activeFieldIndex = static_cast<int>(i + 1);
                }
            }
        }
        ImGui::EndChild();

        const bool activateCurrent = ImGui::Shortcut(
            ImGuiKey_Enter,
            ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteOverActive)
            || ImGui::Shortcut(
                ImGuiKey_KeypadEnter,
                ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteOverActive);
        if (activateCurrent && field.activeButtonIndex >= 0) {
            applyButtonSelection(field, field.activeButtonIndex);
            if (compactDialog && field.input.mode == InputMode::ButtonsList && !field.input.multiSelect) {
                submitRequested = true;
            } else if (!compactDialog && !field.input.multiSelect && i + 1 < inputDialog->fields.size()) {
                inputDialog->activeFieldIndex = static_cast<int>(i + 1);
            }
        }

        if (field.input.mode == InputMode::ButtonsListText) {
            drawTextEditor(field, false);
        }
        ImGui::PopID();
    }

    if (!previewOverride.has_value() && !inputDialog->fields.empty()) {
        const std::size_t fieldIndex = static_cast<std::size_t>(std::clamp(
            inputDialog->activeFieldIndex,
            0,
            static_cast<int>(inputDialog->fields.size()) - 1));
        const int buttonIndex = inputDialog->fields[fieldIndex].activeButtonIndex;
        const InputDialogField& activeField = inputDialog->fields[fieldIndex];
        const bool customTextWins = activeField.input.mode == InputMode::ButtonsListText
            && !activeField.textValue.empty()
            && !activeField.selectedButtonIndex.has_value();
        if (buttonIndex >= 0 && !customTextWins) {
            previewOverride = std::make_pair(fieldIndex, buttonIndex);
        }
    }

    ImGui::SetNextItemOpen(false, ImGuiCond_Appearing);
    const std::string previewLabel = ui.Format(
        UiText::InputDialogPreviewSummaryFormat,
        static_cast<int>(hotkey.messages.size()));
    if (ImGui::CollapsingHeader(previewLabel.c_str())) {
        std::map<std::string, std::string> values;
        for (std::size_t fieldIndex = 0; fieldIndex < inputDialog->fields.size(); ++fieldIndex) {
            const InputDialogField& previewField = inputDialog->fields[fieldIndex];
            std::string value = BuildInputValue(previewField);
            if (previewOverride.has_value() && previewOverride->first == fieldIndex) {
                const int buttonIndex = previewOverride->second;
                if (buttonIndex >= 0 && buttonIndex < static_cast<int>(previewField.input.buttons.size())) {
                    value = previewField.input.buttons[static_cast<std::size_t>(buttonIndex)].text;
                }
            }
            const std::string key = NormalizeInputKey(previewField.input.key);
            if (!key.empty()) {
                values[key] = value;
                values[ToLower(key)] = value;
            }
            values[std::to_string(fieldIndex + 1)] = value;
        }

        if (ImGui::BeginChild("##binder_input_preview", ImVec2(0.0f, ScaleUi(104.0f)), ImGuiChildFlags_FrameStyle)) {
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
        }
        ImGui::EndChild();
    }

    const bool instantSingleChoice = compactDialog
        && inputDialog->fields.front().input.mode == InputMode::ButtonsList
        && !inputDialog->fields.front().input.multiSelect;
    ImGui::Separator();
    if (instantSingleChoice) {
        ImGui::TextDisabled("%s", ui.Text(UiText::InputDialogCancelHint));
    } else {
        ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_Enter, ImGuiInputFlags_Tooltip);
        if (ImGui::Button(ui.Text(UiText::Launch))) {
            submitRequested = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemShortcut(ImGuiKey_Escape, ImGuiInputFlags_Tooltip);
        if (ImGui::Button(ui.Text(UiText::Cancel))) {
            cancelRequested = true;
        }
    }

    if (submitRequested) {
        submitDialog();
    } else if (cancelRequested) {
        cancelDialog();
    }
    ImGui::EndPopup();
}
