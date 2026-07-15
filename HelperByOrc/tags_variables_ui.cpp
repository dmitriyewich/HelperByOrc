#include "tags_module_impl.h"
#include "tags_module_detail.h"
#include "binder_module.h"

namespace {

constexpr std::array<std::string_view, 11> kBindBuilderActions{
    "bindstart",
    "bindstop",
    "bindpause",
    "bindunpause",
    "binddisable",
    "bindenable",
    "bindfastmenu",
    "bindunfastmenu",
    "bindrandom",
    "bindended",
    "bindpopup",
};

bool IsBindBuilderAction(std::string_view value) {
    return std::find(kBindBuilderActions.begin(), kBindBuilderActions.end(), value) != kBindBuilderActions.end();
}

} // namespace

void TagsModule::Impl::LoadConfig() {
    debuglog::WriteInfo("TagsModule::LoadConfig begin");
    customVariables_.clear();

    const jsonutil::JsonObject section = AppConfig::Instance().ReadSectionObject(kTagsSectionName);
    expandExternalTags_.store(
        jsonutil::JsonBoolOr(&section, kExpandExternalTagsKey.data(), true),
        std::memory_order_relaxed);
    const jsonutil::JsonObject* customVars = jsonutil::JsonObjectOrNull(&section, kCustomVarsKey.data());
    if (customVars) {
        for (const auto& [key, value] : *customVars) {
            if (const std::string* text = value.TryString()) {
                customVariables_.emplace_back(key, *text);
            }
        }
    }

    std::sort(customVariables_.begin(), customVariables_.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    RebuildCustomVariableIndex();
    debuglog::WriteInfo(
        "TagsModule::LoadConfig done customVars=%llu expandExternalTags=%d",
        static_cast<unsigned long long>(customVariables_.size()),
        ExpandExternalTagsEnabled() ? 1 : 0);
}

void TagsModule::Impl::SaveConfig() const {
    debuglog::WriteInfo(
        "TagsModule::SaveConfig queued customVars=%llu expandExternalTags=%d",
        static_cast<unsigned long long>(customVariables_.size()),
        ExpandExternalTagsEnabled() ? 1 : 0);
    jsonutil::JsonObject section;
    jsonutil::JsonObject customVars;
    for (const auto& [name, value] : customVariables_) {
        customVars[name] = value;
    }
    section[std::string(kExpandExternalTagsKey)] = ExpandExternalTagsEnabled();
    section[std::string(kCustomVarsKey)] = jsonutil::JsonValue(std::move(customVars));
    AppConfig::Instance().QueueSectionReplace(
        std::string(kTagsSectionName),
        jsonutil::JsonValue(std::move(section)),
        "tags:config");
}

bool TagsModule::Impl::ExpandExternalTagsEnabled() const {
    return expandExternalTags_.load(std::memory_order_relaxed);
}

void TagsModule::Impl::SetExpandExternalTagsEnabled(bool enabled) {
    const bool previous = expandExternalTags_.exchange(enabled, std::memory_order_relaxed);
    if (previous == enabled) {
        return;
    }

    debuglog::WriteInfo("[tags][external] enabled=%d", enabled ? 1 : 0);
    SaveConfig();
}

void TagsModule::Impl::RebuildCustomVariableIndex() {
    customVariableIndex_.clear();
    customVariableIndex_.reserve(customVariables_.size());
    for (std::size_t i = 0; i < customVariables_.size(); ++i) {
        customVariableIndex_[ToLower(customVariables_[i].first)] = i;
    }
    ++customVariablesRevision_;
    InvalidateVariablePickerEntriesCache();
}

void TagsModule::Impl::InvalidateVariablePickerEntriesCache() const {
    variablePickerEntriesCatalogRevision_ = 0;
    variablePickerEntriesCustomRevision_ = 0;
}

void TagsModule::Impl::OpenKeyEmulatePicker() {
    keyPickerSearchQuery_.clear();
    ImGui::OpenPopup("##tags_keyemulate_picker");
}

void TagsModule::Impl::OpenDialogItemPicker(DialogItemPickerSource source) {
    dialogItemPickerSearchQuery_.clear();
    dialogItemPickerSource_ = source;
    dialogItemPickerArizonaQueryStarted_ = false;
    dialogItemPickerOpenPending_ = true;
}

void TagsModule::Impl::OpenDialogItemPicker() {
    OpenDialogItemPicker(DialogItemPickerSource::Samp);
}

void TagsModule::Impl::OpenArizonaDialogItemPicker() {
    OpenDialogItemPicker(DialogItemPickerSource::Arizona);
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

void TagsModule::Impl::OpenBindSelectorBuilder(std::string_view action) {
    bindSelectorBuilder_ = {};
    bindSelectorBuilder_.action = IsBindBuilderAction(action) ? std::string(action) : "bindstart";
    if (binderModule_) {
        bindSelectorBuilder_.catalog = binderModule_->GetBindSelectorCatalog();
    }
    bindSelectorBuilder_.openPending = true;
}

void TagsModule::Impl::DrawVariableHelperPopups(std::function<void(std::string_view)> tokenAction) {
    DrawKeyEmulatePickerPopup();
    DrawDialogItemPickerPopup(tokenAction);
    DrawDialogTextPickerPopup(tokenAction);
    DrawBindSelectorBuilderPopup(tokenAction);
}

void TagsModule::Impl::DrawBindSelectorBuilderPopup(const std::function<void(std::string_view)>& tokenAction) {
    UiSettings& ui = UiSettings::Instance();
    BindSelectorBuilderState& state = bindSelectorBuilder_;
    constexpr char kPopupId[] = "##tags_bind_selector_builder";
    if (state.openPending) {
        ImGui::OpenPopup(kPopupId);
        state.openPending = false;
    }

    ImGui::SetNextWindowSize(ScaleUi(680.0f, 610.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    ImGui::TextUnformatted(ui.Text(UiText::BindBuilderTitle));
    ImGui::Separator();
    ImGui::TextWrapped("%s", ui.Text(UiText::BindBuilderIntro));
    ImGui::Spacing();

    if (ImGui::BeginCombo("##bind_builder_action", state.action.c_str())) {
        for (const std::string_view action : kBindBuilderActions) {
            const bool selected = state.action == action;
            if (ImGui::Selectable(std::string(action).c_str(), selected)) {
                state.action = std::string(action);
                state.bindIndex = -1;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ui.Text(UiText::BindBuilderAction));

    if (state.catalog.categories.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::BindBuilderEmpty));
        if (ImGui::Button(ui.Text(UiText::Close))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    state.categoryIndex = std::clamp(state.categoryIndex, 0, static_cast<int>(state.catalog.categories.size()) - 1);
    binder_tags::CategoryEntry& category = state.catalog.categories[static_cast<std::size_t>(state.categoryIndex)];
    if (ImGui::BeginCombo("##bind_builder_category", category.name.c_str())) {
        for (int i = 0; i < static_cast<int>(state.catalog.categories.size()); ++i) {
            const bool selected = i == state.categoryIndex;
            if (ImGui::Selectable(state.catalog.categories[static_cast<std::size_t>(i)].name.c_str(), selected)) {
                state.categoryIndex = i;
                state.folderChoice = -1;
                state.bindIndex = -1;
                state.search.clear();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ui.Text(UiText::BindBuilderCategory));

    binder_tags::CategoryEntry& selectedCategory =
        state.catalog.categories[static_cast<std::size_t>(state.categoryIndex)];
    const auto folderLabel = [&]() -> std::string {
        if (state.folderChoice == -1) {
            return ui.Text(UiText::BindBuilderAnyFolder);
        }
        if (state.folderChoice == -2) {
            return ui.Text(UiText::BindBuilderNoFolder);
        }
        if (state.folderChoice >= 0
            && state.folderChoice < static_cast<int>(selectedCategory.folderPaths.size())) {
            return binder_tags::JoinFolderPath(
                selectedCategory.folderPaths[static_cast<std::size_t>(state.folderChoice)]);
        }
        return ui.Text(UiText::BindBuilderAnyFolder);
    };
    const std::string currentFolderLabel = folderLabel();
    if (ImGui::BeginCombo("##bind_builder_folder", currentFolderLabel.c_str())) {
        if (ImGui::Selectable(ui.Text(UiText::BindBuilderAnyFolder), state.folderChoice == -1)) {
            state.folderChoice = -1;
            state.bindIndex = -1;
        }
        if (ImGui::Selectable(ui.Text(UiText::BindBuilderNoFolder), state.folderChoice == -2)) {
            state.folderChoice = -2;
            state.bindIndex = -1;
        }
        for (int i = 0; i < static_cast<int>(selectedCategory.folderPaths.size()); ++i) {
            const std::string path = binder_tags::JoinFolderPath(
                selectedCategory.folderPaths[static_cast<std::size_t>(i)]);
            if (ImGui::Selectable(path.c_str(), state.folderChoice == i)) {
                state.folderChoice = i;
                state.bindIndex = -1;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ui.Text(UiText::BindBuilderFolder));

    const bool randomAction = state.action == "bindrandom";
    std::string generated;
    if (randomAction) {
        state.randomScope = std::clamp(state.randomScope, 0, 3);
        const UiText scopeLabels[]{
            UiText::BindBuilderScopeCategory,
            UiText::BindBuilderScopeRoot,
            UiText::BindBuilderScopeDirect,
            UiText::BindBuilderScopeRecursive,
        };
        if (ImGui::BeginCombo("##bind_builder_random_scope", ui.Text(scopeLabels[state.randomScope]))) {
            for (int i = 0; i < 4; ++i) {
                if (ImGui::Selectable(ui.Text(scopeLabels[i]), state.randomScope == i)) {
                    state.randomScope = i;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ui.Text(UiText::BindBuilderScope));

        std::string scope;
        bool validScope = true;
        if (state.randomScope == 0) {
            scope = "*";
        } else if (state.randomScope == 1) {
            scope = binder_tags::QuoteToken("");
        } else if (state.folderChoice >= 0
            && state.folderChoice < static_cast<int>(selectedCategory.folderPaths.size())) {
            std::string path = binder_tags::JoinFolderPath(
                selectedCategory.folderPaths[static_cast<std::size_t>(state.folderChoice)]);
            if (state.randomScope == 3) {
                path += "/**";
            }
            scope = binder_tags::QuoteToken(path);
        } else {
            validScope = false;
        }
        if (validScope) {
            generated = "[bindrandom(" + scope + " " + binder_tags::QuoteToken(selectedCategory.name) + ")]";
        }
    } else {
        InputTextWithHintString(
            "##bind_builder_search",
            ui.Text(UiText::BindBuilderSearch),
            state.search,
            ImGuiInputTextFlags_None,
            128);

        const auto bindVisible = [&](const binder_tags::BindEntry& bind) {
            if (bind.categoryId != selectedCategory.id) {
                return false;
            }
            if (state.folderChoice == -2 && !bind.folderPath.empty()) {
                return false;
            }
            if (state.folderChoice >= 0
                && (state.folderChoice >= static_cast<int>(selectedCategory.folderPaths.size())
                    || bind.folderPath != selectedCategory.folderPaths[static_cast<std::size_t>(state.folderChoice)])) {
                return false;
            }
            return state.search.empty()
                || binder_tags::ContainsNoCaseUtf8(bind.displayName, state.search)
                || binder_tags::ContainsNoCaseUtf8(bind.stableId, state.search)
                || binder_tags::ContainsNoCaseUtf8(std::to_string(bind.number), state.search);
        };

        if (state.bindIndex < 0
            || state.bindIndex >= static_cast<int>(state.catalog.binds.size())
            || !bindVisible(state.catalog.binds[static_cast<std::size_t>(state.bindIndex)])) {
            state.bindIndex = -1;
            for (int i = 0; i < static_cast<int>(state.catalog.binds.size()); ++i) {
                if (bindVisible(state.catalog.binds[static_cast<std::size_t>(i)])) {
                    state.bindIndex = i;
                    break;
                }
            }
        }

        const char* bindPreview = state.bindIndex >= 0
            ? state.catalog.binds[static_cast<std::size_t>(state.bindIndex)].displayName.c_str()
            : ui.Text(UiText::BindBuilderEmpty);
        if (ImGui::BeginCombo("##bind_builder_bind", bindPreview)) {
            for (int i = 0; i < static_cast<int>(state.catalog.binds.size()); ++i) {
                const binder_tags::BindEntry& bind = state.catalog.binds[static_cast<std::size_t>(i)];
                if (!bindVisible(bind)) {
                    continue;
                }
                std::string label = "№" + std::to_string(bind.number) + " " + bind.displayName;
                if (!bind.effectivelyEnabled) {
                    label += " (off)";
                }
                if (ImGui::Selectable(label.c_str(), state.bindIndex == i)) {
                    state.bindIndex = i;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ui.Text(UiText::BindBuilderBind));

        const UiText outputLabels[]{
            UiText::BindBuilderOutputStable,
            UiText::BindBuilderOutputHuman,
            UiText::BindBuilderOutputNumber,
        };
        state.outputMode = std::clamp(state.outputMode, 0, 2);
        if (ImGui::BeginCombo("##bind_builder_output", ui.Text(outputLabels[state.outputMode]))) {
            for (int i = 0; i < 3; ++i) {
                if (ImGui::Selectable(ui.Text(outputLabels[i]), state.outputMode == i)) {
                    state.outputMode = i;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ui.Text(UiText::BindBuilderOutput));

        if (state.bindIndex >= 0) {
            const binder_tags::BindEntry& bind = state.catalog.binds[static_cast<std::size_t>(state.bindIndex)];
            std::string selector;
            if (state.outputMode == 0) {
                selector = binder_tags::StableSelector(bind.stableId);
            } else if (state.outputMode == 1) {
                selector = binder_tags::QuoteToken(bind.displayName)
                    + " " + binder_tags::QuoteToken(binder_tags::JoinFolderPath(bind.folderPath))
                    + " " + binder_tags::QuoteToken(selectedCategory.name);
            } else {
                selector = std::to_string(bind.number);
            }
            generated = "[" + state.action + "(" + selector + ")]";
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("%s", ui.Text(UiText::BindBuilderPreview));
    if (generated.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::BindBuilderEmpty));
    } else {
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(generated.c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::Spacing();
    if (generated.empty()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(ui.Text(tokenAction ? UiText::VariablesInsert : UiText::VariablesCopy), ScaleUi(150.0f, 0.0f))) {
        if (tokenAction) {
            tokenAction(generated);
        } else {
            ImGui::SetClipboardText(generated.c_str());
            NotifySuccess(ui.Text(UiText::ToastClipboardCopied), 1400.0);
        }
        ImGui::CloseCurrentPopup();
    }
    if (generated.empty()) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::Close), ScaleUi(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
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

    const bool arizonaSource = dialogItemPickerSource_ == DialogItemPickerSource::Arizona;
    ImGui::TextUnformatted(ui.Text(arizonaSource ? UiText::MiscVariablesArzDialogItemPickerTitle : UiText::MiscVariablesDialogItemPickerTitle));
    ImGui::TextWrapped(
        "%s",
        ui.Text(arizonaSource ? UiText::MiscVariablesArzDialogItemPickerIntro : UiText::MiscVariablesDialogItemPickerIntro));
    ImGui::Separator();

    InputTextWithHintString(
        "##tags_dialogitem_search",
        ui.Text(UiText::MiscVariablesDialogItemPickerSearchHint),
        dialogItemPickerSearchQuery_,
        ImGuiInputTextFlags_AutoSelectAll,
        128);
    ImGui::Spacing();

    std::optional<DialogListItems> items;
    std::string caption;
    if (arizonaSource) {
        if (arizonaCefDialogs_) {
            const std::string domItemsJson = dialogItemPickerArizonaQueryStarted_
                ? arizonaCefDialogs_->CachedListItemsJson()
                : arizonaCefDialogs_->QueryListItems(kArzDialogQueryDefaultTimeoutMs);
            dialogItemPickerArizonaQueryStarted_ = true;
            items = ParseDialogListItemsJson(domItemsJson);
            const std::string rpcText = arizonaCefDialogs_->LastDialogText();
            if ((!items.has_value() || items->items.empty()) && arizonaCefDialogs_->LastDialogId() >= 0 && !rpcText.empty()) {
                items = CollectDialogListItems(
                    rpcText,
                    GetDialogItemHeaderLinesToSkipForStyle(arizonaCefDialogs_->LastDialogStyle()));
            }
            caption = arizonaCefDialogs_->LastDialogTitle();
        }
    } else {
        std::string error;
        items = ReadActiveDialogListItems(sampApi_, error);
        if (!items.has_value()) {
            const UiText errorText =
                error == "not_list" ? UiText::MiscVariablesDialogItemPickerNotList : UiText::MiscVariablesDialogItemPickerNoDialog;
            ImGui::TextDisabled("%s", ui.Text(errorText));
            ImGui::EndPopup();
            return;
        }
        caption = sampApi_ ? NormalizeDialogCaptionVisibleText(sampApi_->get_dialog_caption()) : std::string();
    }

    if (!items.has_value() || items->items.empty()) {
        const UiText errorText =
            arizonaSource ? UiText::MiscVariablesArzDialogItemPickerNoDialog : UiText::MiscVariablesDialogItemPickerNoDialog;
        ImGui::TextDisabled("%s", ui.Text(errorText));
        ImGui::EndPopup();
        return;
    }

    if (!caption.empty()) {
        ImGui::TextDisabled(
            "%s",
            ui.Format(
                  arizonaSource ? UiText::MiscVariablesArzDialogItemPickerCaptionLabel : UiText::MiscVariablesDialogItemPickerCaptionLabel,
                  caption.c_str())
                .c_str());
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
        [arizonaSource](const DialogListItemInfo& item) {
            return std::string(arizonaSource ? "[ARZdialogitem(" : "[dialogitem(")
                + std::to_string(item.index1)
                + ")]";
        },
        tokenAction);

    ImGui::Spacing();
    ImGui::TextDisabled(
        "%s",
        ui.Text(
            tokenAction
                ? UiText::EditorVariablesDialogPickerInsertHint
                : arizonaSource ? UiText::MiscVariablesArzDialogItemPickerCopyHint : UiText::MiscVariablesDialogItemPickerCopyHint));
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

const std::vector<variables_picker::Entry>& TagsModule::Impl::BuildVariablePickerEntries() const {
    if (variablePickerEntriesCatalogRevision_ == catalogEntriesRevision_
        && variablePickerEntriesCustomRevision_ == customVariablesRevision_) {
        return variablePickerEntriesCache_;
    }

    variablePickerEntriesCache_.clear();
    variablePickerEntriesCache_.reserve(tagRegistry_.Entries().size() + customVariables_.size());
    for (const TagEntry& tag : tagRegistry_.Entries()) {
        const variables_picker::EntryKind kind = tag.kind == TagKind::Function
            ? variables_picker::EntryKind::Function
            : variables_picker::EntryKind::Simple;
        variables_picker::Entry entry;
        entry.kind = kind;
        entry.category = TagsModule::ToPickerCategory(tag.category);
        entry.id = variables_picker::MakeEntryId(kind, tag.token);
        entry.name = tag.name;
        entry.token = tag.token;
        entry.example = tag.example;
        entry.descriptionText = tag.descriptionText;
        entry.action = tag.action;
        variablePickerEntriesCache_.push_back(std::move(entry));
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
        variablePickerEntriesCache_.push_back(std::move(entry));
    }

    variablePickerEntriesCatalogRevision_ = catalogEntriesRevision_;
    variablePickerEntriesCustomRevision_ = customVariablesRevision_;
    return variablePickerEntriesCache_;
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
    if (tagRegistry_.Find(TagKind::Simple, loweredName) || tagRegistry_.Find(TagKind::Function, loweredName)) {
        return ui.Text(UiText::VariablesCustomErrorBuiltinConflict);
    }

    const auto customIt = customVariableIndex_.find(loweredName);
    if (customIt != customVariableIndex_.end() && loweredName != loweredOriginal) {
        return ui.Text(UiText::VariablesCustomErrorDuplicate);
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
    RebuildCustomVariableIndex();
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

    RebuildCustomVariableIndex();
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
    case variables_picker::RequestType::OpenArizonaDialogItemPicker:
        OpenDialogItemPicker(DialogItemPickerSource::Arizona);
        break;
    case variables_picker::RequestType::OpenDialogTextPicker:
        OpenDialogTextPicker(DialogTextPickerSource::Samp);
        break;
    case variables_picker::RequestType::OpenArizonaDialogTextPicker:
        OpenDialogTextPicker(DialogTextPickerSource::Arizona);
        break;
    case variables_picker::RequestType::OpenBindSelectorBuilder:
        OpenBindSelectorBuilder(request.name);
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

void TagsModule::Impl::DrawTransliterationDictionaryCard() {
    UiSettings& ui = UiSettings::Instance();
    ImGui::Spacing();
    ImGui::SeparatorText(ui.Text(UiText::TransliterationDictionaryTitle));
    ImGui::TextWrapped("%s", ui.Text(UiText::TransliterationDictionaryDescription));
    if (ImGui::Button(ui.Text(UiText::TransliterationDictionaryOpen), ScaleUi(140.0f, 0.0f))) {
        OpenTransliterationDictionaryFile();
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::TransliterationDictionaryReload), ScaleUi(120.0f, 0.0f))) {
        LoadTransliterationDictionary();
    }

    if (!transliterationDictionaryPathUtf8_.empty()) {
        ImGui::TextWrapped(
            ui.Text(UiText::TransliterationDictionaryPathFormat),
            transliterationDictionaryPathUtf8_.c_str());
    }

    const TransliterationDictionaryStatus& dictionaryStatus = transliterationDictionaryStatus_;
    const std::size_t skippedDictionaryLines = dictionaryStatus.invalidLines
        + dictionaryStatus.duplicateLines
        + dictionaryStatus.conflictLines
        + dictionaryStatus.limitLines;
    switch (dictionaryStatus.state) {
    case TransliterationDictionaryState::Missing:
        ImGui::TextDisabled("%s", ui.Text(UiText::TransliterationDictionaryMissing));
        break;
    case TransliterationDictionaryState::Loaded:
        ImGui::TextDisabled(
            ui.Text(UiText::TransliterationDictionaryLoadedFormat),
            static_cast<unsigned long long>(dictionaryStatus.loadedPairs));
        break;
    case TransliterationDictionaryState::LoadedWithWarnings:
        ImGui::TextColored(
            ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
            ui.Text(UiText::TransliterationDictionaryWarningsFormat),
            static_cast<unsigned long long>(dictionaryStatus.loadedPairs),
            static_cast<unsigned long long>(skippedDictionaryLines));
        break;
    case TransliterationDictionaryState::Error:
        ImGui::TextColored(
            ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
            "%s",
            ui.Text(UiText::TransliterationDictionaryLoadError));
        break;
    }
    if (transliterationDictionaryOpenFailed_) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
            "%s",
            ui.Text(UiText::TransliterationDictionaryOpenError));
    }
}

void TagsModule::Impl::DrawVariablePickerInspectorExtra(
    void* context,
    const variables_picker::Entry& entry) {
    if (!context
        || entry.kind != variables_picker::EntryKind::Function
        || (entry.name != "cyrtolat" && entry.name != "lattocyr")) {
        return;
    }

    static_cast<Impl*>(context)->DrawTransliterationDictionaryCard();
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

    const std::vector<variables_picker::Entry>& entries = BuildVariablePickerEntries();
    variables_picker::Options pickerOptions{
        variables_picker::Mode::Manage,
        "misc_variables_picker",
        false,
        true,
        false,
        ImGui::GetContentRegionAvail(),
    };
    pickerOptions.drawInspectorExtra = &DrawVariablePickerInspectorExtra;
    pickerOptions.inspectorExtraContext = this;
    const variables_picker::Request request = variables_picker::Draw(
        variablesPickerState_,
        entries,
        pickerOptions);
    HandleVariablePickerRequest(request);

    DrawKeyEmulatePickerPopup();
    DrawDialogItemPickerPopup();
    DrawDialogTextPickerPopup();
    DrawBindSelectorBuilderPopup();
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
