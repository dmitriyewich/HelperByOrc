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

void SetNextResponsiveWaitIfPopupSize() {
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    const ImVec2 margin = ScaleUi(24.0f, 24.0f);
    const ImVec2 maximumSize(
        std::max(1.0f, viewport->WorkSize.x - margin.x),
        std::max(1.0f, viewport->WorkSize.y - margin.y));
    const ImVec2 preferredSize(
        std::min(ScaleUi(720.0f), maximumSize.x),
        std::min(ScaleUi(620.0f), maximumSize.y));
    const ImVec2 minimumSize(
        std::min(ScaleUi(360.0f), maximumSize.x),
        std::min(ScaleUi(300.0f), maximumSize.y));

    ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(minimumSize, maximumSize);
    ImGui::SetNextWindowSize(preferredSize, ImGuiCond_Appearing);
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
    jsonutil::JsonObject customVars;
    for (const auto& [name, value] : customVariables_) {
        customVars[name] = value;
    }
    const bool expandExternalTags = ExpandExternalTagsEnabled();
    AppConfig::Instance().QueueSectionMutation(
        std::string(kTagsSectionName),
        [customVars = std::move(customVars), expandExternalTags](jsonutil::JsonObject& section) mutable {
            section[std::string(kExpandExternalTagsKey)] = expandExternalTags;
            section[std::string(kCustomVarsKey)] = jsonutil::JsonValue(std::move(customVars));
        },
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

void TagsModule::Impl::RefreshCodeVariableReservedNames() {
    std::unordered_set<std::string> simpleNames;
    std::unordered_set<std::string> functionNames;
    simpleNames.reserve(tagRegistry_.Entries().size() + customVariables_.size());
    functionNames.reserve(tagRegistry_.Entries().size() + customVariables_.size());
    for (const TagEntry& entry : tagRegistry_.Entries()) {
        if (entry.kind == TagKind::Simple) {
            simpleNames.insert(ToLower(entry.name));
        } else {
            functionNames.insert(ToLower(entry.name));
        }
    }
    for (const auto& [name, value] : customVariables_) {
        UNREFERENCED_PARAMETER(value);
        const std::string normalized = ToLower(name);
        simpleNames.insert(normalized);
        functionNames.insert(normalized);
    }
    codevars::Runtime::Instance().SetReservedNames(std::move(simpleNames), std::move(functionNames));
    codeCatalogRevision_ = 0;
    InvalidateVariablePickerEntriesCache();
}

void TagsModule::Impl::InvalidateVariablePickerEntriesCache() const {
    variablePickerEntriesCatalogRevision_ = 0;
    variablePickerEntriesCustomRevision_ = 0;
    variablePickerEntriesCodeRevision_ = 0;
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

void TagsModule::Impl::OpenWaitIfBuilder() {
    waitIfBuilder_ = {};
    const auto& keys = VirtualKeyPickerEntries();
    const auto enter = std::find_if(keys.begin(), keys.end(), [](const VirtualKeyPickerEntry& key) {
        return key.code == VK_RETURN;
    });
    if (enter != keys.end()) {
        waitIfBuilder_.keyIndex = static_cast<int>(std::distance(keys.begin(), enter));
    }
    waitIfBuilder_.openPending = true;
}

void TagsModule::Impl::DrawVariableHelperPopups(std::function<void(std::string_view)> tokenAction) {
    DrawKeyEmulatePickerPopup();
    DrawDialogItemPickerPopup(tokenAction);
    DrawDialogTextPickerPopup(tokenAction);
    DrawBindSelectorBuilderPopup(tokenAction);
    DrawWaitIfBuilderPopup(tokenAction);
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
            NotifyClipboardSuccess(ui.Text(UiText::ToastClipboardCopied), 1400.0);
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

void TagsModule::Impl::DrawWaitIfBuilderPopup(const std::function<void(std::string_view)>& tokenAction) {
    UiSettings& ui = UiSettings::Instance();
    WaitIfBuilderState& state = waitIfBuilder_;
    constexpr char kPopupId[] = "##tags_waitif_builder";
    if (state.openPending) {
        ImGui::OpenPopup(kPopupId);
        state.openPending = false;
    }

    SetNextResponsiveWaitIfPopupSize();
    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    ImGui::TextUnformatted(ui.Text(UiText::WaitIfBuilderTitle));
    ImGui::Separator();
    std::string expression;
    const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const float contentHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y - footerHeight);
    if (ImGui::BeginChild(
            "##waitif_builder_content",
            ImVec2(0.0f, contentHeight),
            ImGuiChildFlags_None,
            ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        ImGui::TextWrapped("%s", ui.Text(UiText::WaitIfBuilderIntro));
        ImGui::Spacing();
        ImGui::Checkbox(ui.Text(UiText::WaitIfBuilderRawMode), &state.rawMode);
        ImGui::SetItemDefaultFocus();
        ImGui::Spacing();
    }

    if (state.rawMode) {
        ImGui::TextDisabled("%s", ui.Text(UiText::WaitIfBuilderExpression));
        ImGui::SetNextItemWidth(-FLT_MIN);
        InputTextWithHintString(
            "##waitif_builder_raw",
            ui.Text(UiText::WaitIfBuilderRawHint),
            state.rawExpression,
            ImGuiInputTextFlags_None,
            4096);
        expression = Trim(state.rawExpression);
    } else {
        const bool english = ui.Language() == UiLanguage::English;
        const char* categoryLabelsRu[]{ "Все", "Состояния", "Игрок и транспорт", "Числовые значения", "Клавиши" };
        const char* categoryLabelsEn[]{ "All", "States", "Player and vehicle", "Numeric values", "Keys" };
        const char** categoryLabels = english ? categoryLabelsEn : categoryLabelsRu;
        state.categoryIndex = std::clamp(state.categoryIndex, 0, 4);

        const auto matchesCategory = [&](const WaitIfActionDefinition& definition) {
            if (state.categoryIndex == 0) {
                return true;
            }
            if (state.categoryIndex == 1) {
                return definition.kind == WaitIfActionKind::State;
            }
            if (state.categoryIndex == 2) {
                return definition.kind == WaitIfActionKind::PlayerBoolean
                    || definition.kind == WaitIfActionKind::PlayerBooleanWithValue;
            }
            if (state.categoryIndex == 3) {
                return definition.kind == WaitIfActionKind::PlayerNumber;
            }
            return definition.kind == WaitIfActionKind::KeyBoolean;
        };

        const auto& definitions = WaitIfActionDefinitions();
        const int previousCategory = state.categoryIndex;
        const int previousActionIndex = state.actionIndex;
        ImGui::TextDisabled("%s", ui.Text(UiText::WaitIfBuilderCategory));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##waitif_builder_category", categoryLabels[state.categoryIndex])) {
            for (int i = 0; i < 5; ++i) {
                const bool selected = state.categoryIndex == i;
                if (ImGui::Selectable(categoryLabels[i], selected)) {
                    state.categoryIndex = i;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (previousCategory != state.categoryIndex
            || state.actionIndex < 0
            || state.actionIndex >= static_cast<int>(definitions.size())
            || !matchesCategory(definitions[static_cast<std::size_t>(state.actionIndex)])) {
            state.actionIndex = 0;
            while (state.actionIndex < static_cast<int>(definitions.size())
                && !matchesCategory(definitions[static_cast<std::size_t>(state.actionIndex)])) {
                ++state.actionIndex;
            }
        }
        state.actionIndex = std::clamp(state.actionIndex, 0, static_cast<int>(definitions.size()) - 1);
        const WaitIfActionDefinition* definition = &definitions[static_cast<std::size_t>(state.actionIndex)];
        if (previousActionIndex != state.actionIndex) {
            state.comparisonValue = definition->defaultValue;
            constexpr std::array<std::string_view, 6> comparisons{ "==", "!=", ">", ">=", "<", "<=" };
            const auto found = std::find(comparisons.begin(), comparisons.end(), definition->defaultComparison);
            state.comparisonIndex = found == comparisons.end()
                ? 0
                : static_cast<int>(std::distance(comparisons.begin(), found));
        }
        const auto actionLabel = [&](const WaitIfActionDefinition& item) {
            return std::string(item.name) + " — " + (english ? item.labelEn : item.labelRu);
        };
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-FLT_MIN);
        InputTextWithHintString(
            "##waitif_builder_action_search",
            ui.Text(UiText::WaitIfBuilderActionSearch),
            state.actionSearch,
            ImGuiInputTextFlags_AutoSelectAll,
            128);
        const std::string actionSearch = ToLowerUtf8(Trim(state.actionSearch));
        const auto matchesSearch = [&](const WaitIfActionDefinition& item) {
            return actionSearch.empty()
                || ToLowerUtf8(actionLabel(item)).find(actionSearch) != std::string::npos;
        };

        const std::string selectedActionLabel = actionLabel(*definition);
        ImGui::TextDisabled("%s", ui.Text(UiText::WaitIfBuilderAction));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(0.0f, ScaleUi(120.0f)),
            ImVec2(FLT_MAX, ScaleUi(360.0f)));
        if (ImGui::BeginCombo("##waitif_builder_action", selectedActionLabel.c_str(), ImGuiComboFlags_HeightLarge)) {
            bool hasMatches = false;
            for (int i = 0; i < static_cast<int>(definitions.size()); ++i) {
                const WaitIfActionDefinition& item = definitions[static_cast<std::size_t>(i)];
                if (!matchesCategory(item) || !matchesSearch(item)) {
                    continue;
                }
                hasMatches = true;
                const std::string label = actionLabel(item);
                const bool selected = state.actionIndex == i;
                if (ImGui::Selectable(label.c_str(), selected)) {
                    state.actionIndex = i;
                    definition = &item;
                    state.comparisonValue = item.defaultValue;
                    constexpr std::array<std::string_view, 6> comparisons{ "==", "!=", ">", ">=", "<", "<=" };
                    const auto found = std::find(comparisons.begin(), comparisons.end(), item.defaultComparison);
                    state.comparisonIndex = found == comparisons.end()
                        ? 0
                        : static_cast<int>(std::distance(comparisons.begin(), found));
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            if (!hasMatches) {
                ImGui::TextDisabled("%s", ui.Text(UiText::WaitIfBuilderActionSearchEmpty));
            }
            ImGui::EndCombo();
        }
        definition = &definitions[static_cast<std::size_t>(state.actionIndex)];

        std::string playerSelector;
        if (definition->kind == WaitIfActionKind::PlayerBoolean
            || definition->kind == WaitIfActionKind::PlayerBooleanWithValue
            || definition->kind == WaitIfActionKind::PlayerNumber) {
            const char* playerLabels[]{
                "{myid}",
                "{targetid}",
                "{closestid}",
                "{closestdriverid}",
                english ? "Custom ID / nickname / variable" : "Свой ID / ник / переменная",
            };
            constexpr std::array<std::string_view, 4> playerTokens{
                "{myid}",
                "{targetid}",
                "{closestid}",
                "{closestdriverid}",
            };
            state.playerSelectorIndex = std::clamp(state.playerSelectorIndex, 0, 4);
            ImGui::TextDisabled("%s", ui.Text(UiText::WaitIfBuilderPlayer));
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##waitif_builder_player", playerLabels[state.playerSelectorIndex])) {
                for (int i = 0; i < 5; ++i) {
                    const bool selected = state.playerSelectorIndex == i;
                    if (ImGui::Selectable(playerLabels[i], selected)) {
                        state.playerSelectorIndex = i;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            if (state.playerSelectorIndex < static_cast<int>(playerTokens.size())) {
                playerSelector = playerTokens[static_cast<std::size_t>(state.playerSelectorIndex)];
            } else {
                ImGui::SetNextItemWidth(-FLT_MIN);
                InputTextWithHintString(
                    "##waitif_builder_custom_player",
                    ui.Text(UiText::WaitIfBuilderCustomPlayer),
                    state.customPlayerSelector,
                    ImGuiInputTextFlags_None,
                    128);
                std::string custom = Trim(state.customPlayerSelector);
                const bool alreadyQuoted = custom.size() >= 2
                    && ((custom.front() == '"' && custom.back() == '"')
                        || (custom.front() == '\'' && custom.back() == '\''));
                if (!custom.empty() && !alreadyQuoted) {
                    int numeric = 0;
                    const auto parsed = std::from_chars(custom.data(), custom.data() + custom.size(), numeric);
                    if (parsed.ec != std::errc{} || parsed.ptr != custom.data() + custom.size()) {
                        std::string quoted{ "\"" };
                        quoted.reserve(custom.size() + 2);
                        for (const char ch : custom) {
                            if (ch == '\\' || ch == '"') {
                                quoted.push_back('\\');
                            }
                            quoted.push_back(ch);
                        }
                        quoted.push_back('"');
                        custom = std::move(quoted);
                    }
                }
                playerSelector = std::move(custom);
            }
        }

        if (definition->kind == WaitIfActionKind::PlayerNumber) {
            constexpr std::array<const char*, 6> comparisons{ "==", "!=", ">", ">=", "<", "<=" };
            state.comparisonIndex = std::clamp(state.comparisonIndex, 0, static_cast<int>(comparisons.size()) - 1);
            ImGui::TextDisabled("%s", ui.Text(UiText::WaitIfBuilderComparison));
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##waitif_builder_comparison", comparisons[static_cast<std::size_t>(state.comparisonIndex)])) {
                for (int i = 0; i < static_cast<int>(comparisons.size()); ++i) {
                    const bool selected = state.comparisonIndex == i;
                    if (ImGui::Selectable(comparisons[static_cast<std::size_t>(i)], selected)) {
                        state.comparisonIndex = i;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("%s", ui.Text(UiText::WaitIfBuilderValue));
            ImGui::SetNextItemWidth(-FLT_MIN);
            InputTextWithHintString(
                "##waitif_builder_value",
                ui.Text(UiText::WaitIfBuilderValue),
                state.comparisonValue,
                ImGuiInputTextFlags_None,
                64);
            if (!playerSelector.empty() && !Trim(state.comparisonValue).empty()) {
                expression = std::string(definition->name) + "(" + playerSelector + ") "
                    + comparisons[static_cast<std::size_t>(state.comparisonIndex)] + " "
                    + Trim(state.comparisonValue);
            }
        } else if (definition->kind == WaitIfActionKind::PlayerBooleanWithValue) {
            ImGui::TextDisabled("%s", ui.Text(UiText::WaitIfBuilderValue));
            ImGui::SetNextItemWidth(-FLT_MIN);
            InputTextWithHintString(
                "##waitif_builder_function_value",
                ui.Text(UiText::WaitIfBuilderValue),
                state.comparisonValue,
                ImGuiInputTextFlags_None,
                64);
            if (!playerSelector.empty() && !Trim(state.comparisonValue).empty()) {
                expression = std::string(definition->name) + "(" + playerSelector + ", "
                    + Trim(state.comparisonValue) + ")";
            }
        } else if (definition->kind == WaitIfActionKind::PlayerBoolean) {
            if (!playerSelector.empty()) {
                expression = std::string(definition->name) + "(" + playerSelector + ")";
            }
        } else if (definition->kind == WaitIfActionKind::KeyBoolean) {
            const auto& keys = VirtualKeyPickerEntries();
            if (!keys.empty()) {
                state.keyIndex = std::clamp(state.keyIndex, 0, static_cast<int>(keys.size()) - 1);
                ImGui::TextDisabled("%s", ui.Text(UiText::WaitIfBuilderKey));
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SetNextWindowSizeConstraints(
                    ImVec2(0.0f, ScaleUi(120.0f)),
                    ImVec2(FLT_MAX, ScaleUi(360.0f)));
                if (ImGui::BeginCombo("##waitif_builder_key", keys[static_cast<std::size_t>(state.keyIndex)].label.c_str())) {
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(keys.size()));
                    clipper.IncludeItemByIndex(state.keyIndex);
                    while (clipper.Step()) {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                            const bool selected = state.keyIndex == i;
                            if (ImGui::Selectable(keys[static_cast<std::size_t>(i)].label.c_str(), selected)) {
                                state.keyIndex = i;
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                char keyCode[8]{};
                std::snprintf(
                    keyCode,
                    sizeof(keyCode),
                    "0x%02X",
                    keys[static_cast<std::size_t>(state.keyIndex)].code);
                expression = std::string(definition->name) + "(" + keyCode + ")";
            }
        } else {
            expression = definition->name;
        }

        ImGui::Checkbox(ui.Text(UiText::WaitIfBuilderNegate), &state.negate);
        if (state.negate && !expression.empty()) {
            expression = "not (" + expression + ")";
        }
    }

    const std::string generated = expression.empty() ? std::string() : "[waitif(" + expression + ")]";
    const bool expressionTooLong = expression.size() > 4096;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("%s", ui.Text(UiText::WaitIfBuilderPreview));
    if (generated.empty()) {
        ImGui::TextDisabled("-");
    } else {
        ImGui::PushTextWrapPos();
        ImGui::TextUnformatted(generated.c_str());
        ImGui::PopTextWrapPos();
    }
    if (expressionTooLong) {
        ImGui::TextWrapped("%s", ui.Text(UiText::WaitIfBuilderExpressionTooLong));
    }
    ImGui::EndChild();

    ImGui::Separator();
    const bool canSubmit = !generated.empty() && !expressionTooLong;
    if (!canSubmit) {
        ImGui::BeginDisabled();
    }
    const float buttonSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttonWidth = std::max(
        1.0f,
        (ImGui::GetContentRegionAvail().x - buttonSpacing) * 0.5f);
    if (ImGui::Button(
            ui.Text(tokenAction ? UiText::VariablesInsert : UiText::VariablesCopy),
            ImVec2(buttonWidth, 0.0f))) {
        if (tokenAction) {
            tokenAction(generated);
        } else {
            ImGui::SetClipboardText(generated.c_str());
            NotifyClipboardSuccess(ui.Text(UiText::ToastClipboardCopied), 1400.0);
        }
        ImGui::CloseCurrentPopup();
    }
    if (!canSubmit) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::Close), ImVec2(buttonWidth, 0.0f))) {
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
    EnsureCodeCatalogEntries();
    const std::uint64_t codeRevision = codevars::Runtime::Instance().CatalogRevision();
    if (variablePickerEntriesCatalogRevision_ == catalogEntriesRevision_
        && variablePickerEntriesCustomRevision_ == customVariablesRevision_
        && variablePickerEntriesCodeRevision_ == codeRevision) {
        return variablePickerEntriesCache_;
    }

    variablePickerEntriesCache_.clear();
    variablePickerEntriesCache_.reserve(catalogEntries_.size() + customVariables_.size());
    for (const CatalogEntry& tag : catalogEntries_) {
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
        entry.description = tag.description;
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
    variablePickerEntriesCodeRevision_ = codeRevision;
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
    const std::vector<codevars::CatalogVariable> codeVariables = codevars::Runtime::Instance().Catalog();
    if (std::any_of(codeVariables.begin(), codeVariables.end(), [&](const codevars::CatalogVariable& variable) {
            return ToLower(variable.name) == loweredName;
        })) {
        return ui.Text(UiText::VariablesCustomErrorCodeConflict);
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
    RefreshCodeVariableReservedNames();
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
    RefreshCodeVariableReservedNames();
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
    case variables_picker::RequestType::OpenWaitIfBuilder:
        OpenWaitIfBuilder();
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

    if (ImGui::BeginTabBar("##variables_sections")) {
        if (ImGui::BeginTabItem(ui.Text(UiText::VariablesTabCatalog))) {
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
            DrawWaitIfBuilderPopup();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ui.Text(UiText::VariablesTabLua))) {
            DrawLuaVariablesTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
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
