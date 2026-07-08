#include "tags_module_impl.h"
#include "tags_module_detail.h"

void TagsModule::Impl::LoadConfig() {
    debuglog::WriteInfo("TagsModule::LoadConfig begin");
    customVariables_.clear();

    const jsonutil::JsonObject section = AppConfig::Instance().ReadSectionObject(kTagsSectionName);
    const jsonutil::JsonObject* customVars = jsonutil::JsonObjectOrNull(&section, kCustomVarsKey.data());
    if (!customVars) {
        return;
    }

    for (const auto& [key, value] : *customVars) {
        if (const std::string* text = value.TryString()) {
            customVariables_.emplace_back(key, *text);
        }
    }

    std::sort(customVariables_.begin(), customVariables_.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    debuglog::WriteInfo("TagsModule::LoadConfig done customVars=%llu", static_cast<unsigned long long>(customVariables_.size()));
}

void TagsModule::Impl::SaveConfig() const {
    debuglog::WriteInfo("TagsModule::SaveConfig queued customVars=%llu", static_cast<unsigned long long>(customVariables_.size()));
    jsonutil::JsonObject section;
    jsonutil::JsonObject customVars;
    for (const auto& [name, value] : customVariables_) {
        customVars[name] = value;
    }
    section[std::string(kCustomVarsKey)] = jsonutil::JsonValue(std::move(customVars));
    AppConfig::Instance().QueueSectionReplace(std::string(kTagsSectionName), jsonutil::JsonValue(std::move(section)));
}

void TagsModule::Impl::OpenKeyEmulatePicker() {
    keyPickerSearchQuery_.clear();
    ImGui::OpenPopup("##tags_keyemulate_picker");
}

void TagsModule::Impl::OpenDialogItemPicker() {
    dialogItemPickerSearchQuery_.clear();
    dialogItemPickerOpenPending_ = true;
}

void TagsModule::Impl::OpenDialogTextPicker(DialogTextPickerSource source) {
    dialogTextPickerSearchQuery_.clear();
    dialogTextPickerSource_ = source;
    dialogTextPickerOpenPending_ = true;
}

void TagsModule::Impl::OpenSampDialogTextPicker() {
    OpenDialogTextPicker(DialogTextPickerSource::Samp);
}

void TagsModule::Impl::OpenArizonaDialogTextPicker() {
    OpenDialogTextPicker(DialogTextPickerSource::Arizona);
}

void TagsModule::Impl::DrawVariableHelperPopups(std::function<void(std::string_view)> tokenAction) {
    DrawKeyEmulatePickerPopup();
    DrawDialogItemPickerPopup(tokenAction);
    DrawDialogTextPickerPopup(tokenAction);
}

void TagsModule::Impl::DrawKeyEmulatePickerPopup() {
    UiSettings& ui = UiSettings::Instance();

    ImGui::SetNextWindowSize(ScaleUi(560.0f, 520.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("##tags_keyemulate_picker")) {
        return;
    }

    ImGui::TextUnformatted(ui.Text(UiText::MiscVariablesKeyPickerTitle));
    ImGui::TextWrapped("%s", ui.Text(UiText::MiscVariablesKeyPickerIntro));
    ImGui::Separator();

    InputTextWithHintString(
        "##tags_keyemulate_search",
        ui.Text(UiText::MiscVariablesKeyPickerSearchHint),
        keyPickerSearchQuery_,
        ImGuiInputTextFlags_AutoSelectAll,
        96);
    ImGui::Spacing();

    const std::string filter = ToLower(keyPickerSearchQuery_);
    DrawSearchableTokenList(
        "##tags_keyemulate_picker_list",
        GetVirtualKeyPickerEntries(),
        filter,
        UiText::MiscVariablesKeyPickerEmpty,
        keyPickerSearchQuery_,
        [](const TagsModule::Impl::VirtualKeyPickerEntry& entry) -> const std::string& { return entry.search; },
        [](const TagsModule::Impl::VirtualKeyPickerEntry& entry) -> const std::string& { return entry.label; },
        [](const TagsModule::Impl::VirtualKeyPickerEntry& entry) { return MakeKeyEmulateTokenImpl(entry.code); });

    ImGui::Spacing();
    ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesKeyPickerCopyHint));

    ImGui::EndPopup();
}

void TagsModule::Impl::DrawDialogItemPickerPopup(const std::function<void(std::string_view)>& tokenAction) {
    if (dialogItemPickerOpenPending_) {
        ImGui::OpenPopup("##tags_dialogitem_picker");
        dialogItemPickerOpenPending_ = false;
    }

    UiSettings& ui = UiSettings::Instance();
    ImGui::SetNextWindowSize(ScaleUi(620.0f, 540.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("##tags_dialogitem_picker")) {
        return;
    }

    ImGui::TextUnformatted(ui.Text(UiText::MiscVariablesDialogItemPickerTitle));
    ImGui::TextWrapped("%s", ui.Text(UiText::MiscVariablesDialogItemPickerIntro));
    ImGui::Separator();

    InputTextWithHintString(
        "##tags_dialogitem_search",
        ui.Text(UiText::MiscVariablesDialogItemPickerSearchHint),
        dialogItemPickerSearchQuery_,
        ImGuiInputTextFlags_AutoSelectAll,
        128);
    ImGui::Spacing();

    std::string error;
    const std::optional<DialogListItems> items = ReadActiveDialogListItems(sampApi_, error);
    if (!items.has_value()) {
        const UiText errorText = error == "not_list" ? UiText::MiscVariablesDialogItemPickerNotList : UiText::MiscVariablesDialogItemPickerNoDialog;
        ImGui::TextDisabled("%s", ui.Text(errorText));
        ImGui::EndPopup();
        return;
    }

    const std::string caption = sampApi_ ? NormalizeDialogCaptionVisibleText(sampApi_->get_dialog_caption()) : std::string();
    if (!caption.empty()) {
        ImGui::TextDisabled("%s", ui.Format(UiText::MiscVariablesDialogItemPickerCaptionLabel, caption.c_str()).c_str());
    }
    if (!items->headerText.empty()) {
        ImGui::TextWrapped("%s", ui.Format(UiText::MiscVariablesDialogItemPickerHeaderLabel, items->headerText.c_str()).c_str());
    }
    ImGui::Spacing();

    const std::string filter = ToLowerUtf8(dialogItemPickerSearchQuery_);
    DrawSearchableTokenList(
        "##tags_dialogitem_picker_list",
        items->items,
        filter,
        UiText::MiscVariablesDialogItemPickerEmpty,
        dialogItemPickerSearchQuery_,
        [](const DialogListItemInfo& item) {
            return ToLowerUtf8(std::to_string(item.index1) + " " + item.text + " " + item.rawText);
        },
        [](const DialogListItemInfo& item) {
            const std::string visibleText = item.text.empty() ? item.rawText : item.text;
            return std::to_string(item.index1) + " - " + visibleText;
        },
        [](const DialogListItemInfo& item) {
            return "[dialogitem(" + std::to_string(item.index1) + ")]";
        },
        tokenAction);

    ImGui::Spacing();
    ImGui::TextDisabled(
        "%s",
        ui.Text(tokenAction ? UiText::EditorVariablesDialogPickerInsertHint : UiText::MiscVariablesDialogItemPickerCopyHint));
    ImGui::EndPopup();
}

void TagsModule::Impl::DrawDialogTextPickerPopup(const std::function<void(std::string_view)>& tokenAction) {
    if (dialogTextPickerOpenPending_) {
        ImGui::OpenPopup("##tags_dialogtext_picker");
        dialogTextPickerOpenPending_ = false;
    }

    UiSettings& ui = UiSettings::Instance();
    ImGui::SetNextWindowSize(ScaleUi(620.0f, 540.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("##tags_dialogtext_picker")) {
        return;
    }

    const bool arizonaSource = dialogTextPickerSource_ == DialogTextPickerSource::Arizona;
    ImGui::TextUnformatted(ui.Text(arizonaSource ? UiText::MiscVariablesArzDialogTextPickerTitle : UiText::MiscVariablesDialogTextPickerTitle));
    ImGui::TextWrapped(
        "%s",
        ui.Text(arizonaSource ? UiText::MiscVariablesArzDialogTextPickerIntro : UiText::MiscVariablesDialogTextPickerIntro));
    ImGui::Separator();

    InputTextWithHintString(
        "##tags_dialogtext_search",
        ui.Text(UiText::MiscVariablesDialogTextPickerSearchHint),
        dialogTextPickerSearchQuery_,
        ImGuiInputTextFlags_AutoSelectAll,
        128);
    ImGui::Spacing();

    std::optional<DialogTextItems> items;
    std::string caption;
    if (arizonaSource) {
        if (arizonaCefDialogs_ && arizonaCefDialogs_->LastDialogId() >= 0) {
            items = CollectDialogTextItems(arizonaCefDialogs_->LastDialogText());
            caption = arizonaCefDialogs_->LastDialogTitle();
        }
    } else {
        std::string error;
        items = ReadActiveDialogTextItems(sampApi_, error);
        caption = sampApi_ ? NormalizeDialogCaptionVisibleText(sampApi_->get_dialog_caption()) : std::string();
    }
    if (!items.has_value()) {
        ImGui::TextDisabled(
            "%s",
            ui.Text(arizonaSource ? UiText::MiscVariablesArzDialogTextPickerNoDialog : UiText::MiscVariablesDialogTextPickerNoDialog));
        ImGui::EndPopup();
        return;
    }

    if (!caption.empty()) {
        ImGui::TextDisabled(
            "%s",
            ui.Format(
                  arizonaSource ? UiText::MiscVariablesArzDialogTextPickerCaptionLabel : UiText::MiscVariablesDialogTextPickerCaptionLabel,
                  caption.c_str())
                .c_str());
    }
    ImGui::TextDisabled(
        "%s",
        ui.Format(UiText::MiscVariablesDialogTextPickerCountLabel, std::to_string(items->flat.size()).c_str()).c_str());
    ImGui::Spacing();

    const std::string filter = ToLowerUtf8(dialogTextPickerSearchQuery_);
    DrawSearchableTokenList(
        "##tags_dialogtext_picker_list",
        items->flat,
        filter,
        UiText::MiscVariablesDialogTextPickerEmpty,
        dialogTextPickerSearchQuery_,
        [](const DialogTextToken& token) {
            return ToLowerUtf8(std::to_string(token.index) + " " + token.text);
        },
        [](const DialogTextToken& token) {
            return std::to_string(token.index) + " - " + token.text;
        },
        [arizonaSource](const DialogTextToken& token) {
            return std::string(arizonaSource ? "[ARZdialoggetdialogtext(" : "[dialogtext(")
                + std::to_string(token.index)
                + ")]";
        },
        tokenAction);

    ImGui::Spacing();
    ImGui::TextDisabled(
        "%s",
        ui.Text(
            tokenAction
                ? UiText::EditorVariablesDialogPickerInsertHint
                : arizonaSource ? UiText::MiscVariablesArzDialogTextPickerCopyHint : UiText::MiscVariablesDialogTextPickerCopyHint));
    ImGui::EndPopup();
}

void TagsModule::Impl::DrawMiscHomePage() {
    UiSettings& ui = UiSettings::Instance();

    ImGui::SeparatorText(ui.Text(UiText::TabMisc));
    ImGui::TextWrapped("%s", ui.Text(UiText::MiscHomeIntro));
    ImGui::Spacing();

    if (DrawNavigationCardButton(
            "##misc_variables_card",
            ui.Text(UiText::MiscVariablesTitle),
            ui.Text(UiText::MiscVariablesEntryDesc),
            ui.Text(UiText::MiscOpenSectionAction),
            ImVec4(0.97f, 0.83f, 0.46f, 1.0f),
            136.0f)) {
        currentPage_ = MiscPage::Variables;
    }
}

std::vector<variables_picker::Entry> TagsModule::Impl::BuildVariablePickerEntries() const {
    std::vector<variables_picker::Entry> entries;
    entries.reserve(tagRegistry_.Entries().size() + customVariables_.size());

    for (const TagEntry& tag : tagRegistry_.Entries()) {
        const variables_picker::EntryKind kind = tag.kind == TagKind::Function
            ? variables_picker::EntryKind::Function
            : variables_picker::EntryKind::Simple;
        variables_picker::Entry entry;
        entry.kind = kind;
        entry.category = variables_picker::ClassifyBuiltin(tag.name);
        entry.id = variables_picker::MakeEntryId(kind, tag.token);
        entry.name = tag.name;
        entry.token = tag.token;
        entry.example = tag.example;
        entry.descriptionText = tag.descriptionText;
        entry.action = variables_picker::IsActionBuiltin(kind, tag.name);
        entries.push_back(std::move(entry));
    }

    for (const auto& [name, value] : customVariables_) {
        variables_picker::Entry entry;
        entry.kind = variables_picker::EntryKind::Custom;
        entry.category = variables_picker::Category::Custom;
        entry.id = variables_picker::MakeEntryId(variables_picker::EntryKind::Custom, name);
        entry.name = name;
        entry.token = "{" + name + "}";
        entry.example = entry.token;
        entry.value = value;
        entry.description = value;
        entries.push_back(std::move(entry));
    }

    return entries;
}

std::string TagsModule::Impl::ValidateCustomVariableName(std::string_view originalName, std::string_view name) const {
    UiSettings& ui = UiSettings::Instance();
    const std::string cleanName = Trim(name);
    if (cleanName.empty()) {
        return ui.Text(UiText::VariablesCustomErrorEmptyName);
    }
    if (cleanName.size() > 64) {
        return ui.Text(UiText::VariablesCustomErrorBadName);
    }

    for (const unsigned char ch : cleanName) {
        if (std::isalnum(ch) == 0 && ch != '_') {
            return ui.Text(UiText::VariablesCustomErrorBadName);
        }
    }

    const std::string loweredName = ToLower(cleanName);
    const std::string loweredOriginal = ToLower(Trim(originalName));
    for (const TagEntry& tag : tagRegistry_.Entries()) {
        if (ToLower(tag.name) == loweredName) {
            return ui.Text(UiText::VariablesCustomErrorBuiltinConflict);
        }
    }

    for (const auto& [customName, _] : customVariables_) {
        const std::string loweredCustom = ToLower(customName);
        if (loweredCustom == loweredName && loweredCustom != loweredOriginal) {
            return ui.Text(UiText::VariablesCustomErrorDuplicate);
        }
    }

    return {};
}

bool TagsModule::Impl::UpsertCustomVariable(std::string originalName, std::string name, std::string value) {
    name = Trim(name);
    originalName = Trim(originalName);
    const std::string error = ValidateCustomVariableName(originalName, name);
    if (!error.empty()) {
        variablesPickerState_.customError = error;
        return false;
    }

    const std::string loweredOriginal = ToLower(originalName);
    if (!loweredOriginal.empty()) {
        customVariables_.erase(
            std::remove_if(
                customVariables_.begin(),
                customVariables_.end(),
                [&](const auto& item) { return ToLower(item.first) == loweredOriginal; }),
            customVariables_.end());
    }

    const std::string loweredName = ToLower(name);
    auto existing = std::find_if(customVariables_.begin(), customVariables_.end(), [&](const auto& item) {
        return ToLower(item.first) == loweredName;
    });
    if (existing != customVariables_.end()) {
        existing->first = std::move(name);
        existing->second = std::move(value);
    } else {
        customVariables_.emplace_back(std::move(name), std::move(value));
    }

    std::sort(customVariables_.begin(), customVariables_.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    SaveConfig();
    return true;
}

bool TagsModule::Impl::DeleteCustomVariable(std::string_view name) {
    const std::string loweredName = ToLower(Trim(name));
    const auto oldSize = customVariables_.size();
    customVariables_.erase(
        std::remove_if(
            customVariables_.begin(),
            customVariables_.end(),
            [&](const auto& item) { return ToLower(item.first) == loweredName; }),
        customVariables_.end());
    if (customVariables_.size() == oldSize) {
        return false;
    }

    SaveConfig();
    return true;
}

void TagsModule::Impl::HandleVariablePickerRequest(const variables_picker::Request& request) {
    switch (request.type) {
    case variables_picker::RequestType::Copy:
        ImGui::SetClipboardText(request.text.c_str());
        break;
    case variables_picker::RequestType::OpenKeyEmulatePicker:
        OpenKeyEmulatePicker();
        break;
    case variables_picker::RequestType::OpenDialogItemPicker:
        OpenDialogItemPicker();
        break;
    case variables_picker::RequestType::OpenDialogTextPicker:
        OpenDialogTextPicker(DialogTextPickerSource::Samp);
        break;
    case variables_picker::RequestType::OpenArizonaDialogTextPicker:
        OpenDialogTextPicker(DialogTextPickerSource::Arizona);
        break;
    case variables_picker::RequestType::SaveCustom:
        if (UpsertCustomVariable(request.text, request.name, request.value)) {
            const std::string cleanName = Trim(request.name);
            variablesPickerState_.customDraftOpen = false;
            variablesPickerState_.customError.clear();
            variablesPickerState_.selectedId =
                variables_picker::MakeEntryId(variables_picker::EntryKind::Custom, cleanName);
        }
        break;
    case variables_picker::RequestType::DeleteCustom:
        if (DeleteCustomVariable(request.name)) {
            variablesPickerState_.selectedId.clear();
        }
        break;
    case variables_picker::RequestType::None:
    case variables_picker::RequestType::Insert:
    default:
        break;
    }
}

std::vector<variables_picker::Entry> TagsModule::Impl::BuildVariablePickerEntriesForInsert() const {
    return BuildVariablePickerEntries();
}

void TagsModule::Impl::HandleVariablePickerUtilityRequest(const variables_picker::Request& request) {
    HandleVariablePickerRequest(request);
}

void TagsModule::Impl::DrawVariablesPage() {
    UiSettings& ui = UiSettings::Instance();

    if (ImGui::Button(ui.Text(UiText::EditorBack), ScaleUi(120.0f, 0.0f))) {
        currentPage_ = MiscPage::Home;
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ui.Text(UiText::TabMisc));
    ImGui::Spacing();

    ImGui::Text("%s", ui.Text(UiText::MiscVariablesTitle));
    ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesEntryDesc));
    ImGui::Spacing();

    const std::vector<variables_picker::Entry> entries = BuildVariablePickerEntries();
    const variables_picker::Request request = variables_picker::Draw(
        variablesPickerState_,
        entries,
        variables_picker::Options{
            variables_picker::Mode::Manage,
            "misc_variables_picker",
            false,
            true,
            false,
            ImGui::GetContentRegionAvail(),
        });
    HandleVariablePickerRequest(request);

    DrawKeyEmulatePickerPopup();
    DrawDialogItemPickerPopup();
    DrawDialogTextPickerPopup();
}

bool TagsModule::Impl::IsMiscHomePage() const {
    return currentPage_ == MiscPage::Home;
}

void TagsModule::Impl::DrawMiscTab() {
    if (currentPage_ == MiscPage::Variables) {
        DrawVariablesPage();
        return;
    }

    DrawMiscHomePage();
}
