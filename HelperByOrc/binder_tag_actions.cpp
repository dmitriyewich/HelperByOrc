#include "binder_module_impl.h"

std::string BinderModule::Impl::EscapeBindTagLogValue(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\r': escaped += "\\r"; break;
        case '\n': escaped += "\\n"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

int BinderModule::Impl::FindHotkeyIndexByRuntimeId(std::uint64_t runtimeId) const {
    if (runtimeId == 0) {
        return -1;
    }

    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        if (hotkeys[i].runtimeId == runtimeId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

BindTagContextDesc BinderModule::Impl::DescribeBindTagContext(std::uint64_t runtimeId) const {
    BindTagContextDesc desc;
    desc.hotkeyIndex = FindHotkeyIndexByRuntimeId(runtimeId);
    if (desc.hotkeyIndex < 0 || desc.hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        return desc;
    }

    const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(desc.hotkeyIndex)];
    desc.name = BuildBindDisplayLabel(hotkey);
    desc.folder = JoinPath(hotkey.folderPath);
    if (const RunningBind* running = FindRunningBind(runtimeId); running && !running->categoryName.empty()) {
        desc.category = running->categoryName;
    } else if (const BinderCategory* category = FindCategoryById(hotkey.categoryId)) {
        desc.category = category->name;
    }
    return desc;
}

std::string BinderModule::Impl::DescribeBindTagLogSource(std::uint64_t runtimeId) const {
    const BindTagContextDesc source = DescribeBindTagContext(runtimeId);
    std::string result = "runtime=" + std::to_string(runtimeId);
    result += " index=" + std::to_string(source.hotkeyIndex);
    result += " category=\"" + EscapeBindTagLogValue(source.category) + "\"";
    result += " folder=\"" + EscapeBindTagLogValue(source.folder) + "\"";
    result += " bind=\"" + EscapeBindTagLogValue(source.name) + "\"";
    return result;
}

std::string BinderModule::Impl::DescribeBindTagLogTargets(const std::vector<int>& targetIndices) const {
    constexpr std::size_t kMaxLoggedTargets = 8;
    std::string result = "count=" + std::to_string(targetIndices.size());
    const std::size_t logged = std::min(targetIndices.size(), kMaxLoggedTargets);
    for (std::size_t i = 0; i < logged; ++i) {
        const int index = targetIndices[i];
        result += " target={index=" + std::to_string(index);
        if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
            result += " invalid=1}";
            continue;
        }

        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
        const BinderCategory* category = FindCategoryById(hotkey.categoryId);
        result += " ui=" + std::to_string(hotkey.number);
        result += " id=\"" + EscapeBindTagLogValue(binder_tags::StableSelector(hotkey.orderId)) + "\"";
        result += " category=\"" + EscapeBindTagLogValue(category ? category->name : hotkey.categoryId) + "\"";
        result += " folder=\"" + EscapeBindTagLogValue(JoinPath(hotkey.folderPath)) + "\"";
        result += " bind=\"" + EscapeBindTagLogValue(BuildBindDisplayLabel(hotkey)) + "\"";
        result += " enabled=" + std::to_string(hotkey.enabled ? 1 : 0);
        result += " effective=" + std::to_string(IsHotkeyEffectivelyEnabled(index) ? 1 : 0);
        result += " running=" + std::to_string(IsHotkeyRunning(index) ? 1 : 0);
        result += " paused=" + std::to_string(IsHotkeyPaused(index) ? 1 : 0);
        result += " input=" + std::to_string(hotkey.awaitingInput ? 1 : 0);
        result += " confirmation=" + std::to_string(hotkey.waitingTextConfirmation ? 1 : 0);
        result += '}';
    }
    if (targetIndices.size() > logged) {
        result += " omitted=" + std::to_string(targetIndices.size() - logged);
    }
    return result;
}

binder_tags::Catalog BinderModule::Impl::BuildBindSelectorCatalog() const {
    binder_tags::Catalog catalog;
    catalog.categories.reserve(categories.size());
    for (const BinderCategory& category : categories) {
        binder_tags::CategoryEntry entry;
        entry.id = category.id;
        entry.name = category.name;
        CollectFolderPaths(category.folders, entry.folderPaths);
        catalog.categories.push_back(std::move(entry));
    }

    catalog.binds.reserve(hotkeys.size());
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        const HotkeyEntry& hotkey = hotkeys[i];
        catalog.binds.push_back(binder_tags::BindEntry{
            static_cast<int>(i),
            hotkey.number,
            hotkey.orderId,
            BuildBindDisplayLabel(hotkey),
            hotkey.categoryId,
            hotkey.folderPath,
            hotkey.enabled,
            IsHotkeyEffectivelyEnabled(static_cast<int>(i)),
        });
    }
    return catalog;
}

std::string BinderModule::Impl::BuildThisbindTagValue(std::uint64_t runtimeId) const {
    const BindTagContextDesc desc = DescribeBindTagContext(runtimeId);
    if (desc.hotkeyIndex < 0 || desc.name.empty()) {
        return {};
    }

    std::string value = binder_tags::QuoteToken(desc.name);
    value += ' ';
    value += binder_tags::QuoteToken(desc.folder);
    return value;
}

std::string BinderModule::Impl::BuildThisbindNameTagValue(std::uint64_t runtimeId) const {
    const BindTagContextDesc desc = DescribeBindTagContext(runtimeId);
    return desc.hotkeyIndex < 0 ? std::string{} : desc.name;
}

std::string BinderModule::Impl::BuildThisbindFolderTagValue(std::uint64_t runtimeId) const {
    const BindTagContextDesc desc = DescribeBindTagContext(runtimeId);
    return desc.hotkeyIndex < 0 ? std::string{} : desc.folder;
}

std::string BinderModule::Impl::BuildThiscategoryTagValue(std::uint64_t runtimeId) const {
    const BindTagContextDesc desc = DescribeBindTagContext(runtimeId);
    return desc.hotkeyIndex < 0 ? std::string{} : desc.category;
}

bool BinderModule::Impl::IsRuntimeActive(std::uint64_t runtimeId) const {
    if (runtimeId == 0) {
        return false;
    }

    if (const int hotkeyIndex = FindHotkeyIndexByRuntimeId(runtimeId); hotkeyIndex >= 0) {
        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(hotkeyIndex)];
        if (hotkey.awaitingInput || hotkey.waitingTextConfirmation) {
            return true;
        }
    }

    return FindRunningBind(runtimeId) != nullptr;
}

bool BinderModule::Impl::IsRuntimePaused(std::uint64_t runtimeId) const {
    if (const RunningBind* running = FindRunningBind(runtimeId)) {
        return running->paused;
    }
    return false;
}

bool BinderModule::Impl::PauseRuntime(std::uint64_t runtimeId) {
    const int hotkeyIndex = FindHotkeyIndexByRuntimeId(runtimeId);
    return hotkeyIndex >= 0 && PauseHotkey(hotkeyIndex);
}

bool BinderModule::Impl::ResumeRuntime(std::uint64_t runtimeId) {
    const int hotkeyIndex = FindHotkeyIndexByRuntimeId(runtimeId);
    return hotkeyIndex >= 0 && ResumeHotkey(hotkeyIndex);
}

bool BinderModule::Impl::StopRuntime(std::uint64_t runtimeId) {
    if (!IsRuntimeActive(runtimeId)) {
        return false;
    }

    StopHotkeyByRuntimeId(runtimeId);
    return true;
}

void BinderModule::Impl::QueueSelfRestart(std::uint64_t runtimeId) {
    if (runtimeId == 0) {
        return;
    }
    if (std::find(pendingSelfRestarts.begin(), pendingSelfRestarts.end(), runtimeId) != pendingSelfRestarts.end()) {
        debuglog::WriteInfo(
            "[binder][bind-tag] self-restart already queued source={%s}",
            DescribeBindTagLogSource(runtimeId).c_str());
        return;
    }
    pendingSelfRestarts.push_back(runtimeId);
    debuglog::WriteInfo(
        "[binder][bind-tag] self-restart queued source={%s}",
        DescribeBindTagLogSource(runtimeId).c_str());
}

bool BinderModule::Impl::StartPendingSelfRestart(std::uint64_t runtimeId) {
    const auto it = std::find(pendingSelfRestarts.begin(), pendingSelfRestarts.end(), runtimeId);
    if (it == pendingSelfRestarts.end()) {
        return false;
    }

    pendingSelfRestarts.erase(it);
    const int index = FindHotkeyIndexByRuntimeId(runtimeId);
    if (index < 0) {
        debuglog::WriteError(
            "[binder][bind-tag] self-restart failed reason=source_not_found source={%s}",
            DescribeBindTagLogSource(runtimeId).c_str());
        return false;
    }

    const bool started = TryEnqueueHotkey(index, kNestedBindStartDelayMs, "bind_tag", "[bindstart({thisbind})]");
    debuglog::WriteInfo(
        "[binder][bind-tag] self-restart execute success=%d source={%s} targets={%s}",
        started ? 1 : 0,
        DescribeBindTagLogSource(runtimeId).c_str(),
        DescribeBindTagLogTargets({ index }).c_str());
    return started;
}

std::vector<int> BinderModule::Impl::ResolveBindTagTargets(
    BindTagAction action,
    std::string_view rawParam,
    std::uint64_t sourceRuntimeId,
    std::string& error) const {
    binder_tags::Action selectorAction = binder_tags::Action::Start;
    switch (action) {
    case BindTagAction::Disable: selectorAction = binder_tags::Action::Disable; break;
    case BindTagAction::Enable: selectorAction = binder_tags::Action::Enable; break;
    case BindTagAction::Start: selectorAction = binder_tags::Action::Start; break;
    case BindTagAction::Stop: selectorAction = binder_tags::Action::Stop; break;
    case BindTagAction::Pause: selectorAction = binder_tags::Action::Pause; break;
    case BindTagAction::Unpause: selectorAction = binder_tags::Action::Unpause; break;
    case BindTagAction::FastMenu: selectorAction = binder_tags::Action::FastMenu; break;
    case BindTagAction::UnfastMenu: selectorAction = binder_tags::Action::UnfastMenu; break;
    case BindTagAction::Random: selectorAction = binder_tags::Action::Random; break;
    case BindTagAction::Ended: selectorAction = binder_tags::Action::Ended; break;
    case BindTagAction::Popup: selectorAction = binder_tags::Action::Popup; break;
    case BindTagAction::StopAll:
    case BindTagAction::Unknown:
        error = "invalid_syntax";
        return {};
    }

    const BindTagContextDesc source = DescribeBindTagContext(sourceRuntimeId);
    binder_tags::Context context;
    context.bindIndex = source.hotkeyIndex;
    if (source.hotkeyIndex >= 0 && source.hotkeyIndex < static_cast<int>(hotkeys.size())) {
        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(source.hotkeyIndex)];
        context.categoryId = hotkey.categoryId;
        context.folderPath = hotkey.folderPath;
    } else {
        context.categoryId = activeCategoryId;
    }

    binder_tags::ResolveResult resolved =
        binder_tags::Resolve(selectorAction, rawParam, context, BuildBindSelectorCatalog());
    error = std::string(binder_tags::ErrorCode(resolved.error));
    return std::move(resolved.indices);
}

std::string BinderModule::Impl::DescribeBindTagError(std::string_view actionName, std::string_view error) const {
    UiSettings& ui = UiSettings::Instance();
    const std::string token = "[" + std::string(actionName) + "]";

    if (error == "param_required") {
        return ui.Format(UiText::ToastBindTagTargetRequired, token.c_str());
    }
    if (error == "invalid_syntax") {
        return ui.Format(UiText::ToastBindTagInvalidSyntax, token.c_str());
    }
    if (error == "too_many_args") {
        return ui.Format(UiText::ToastBindTagTooManyArguments, token.c_str());
    }
    if (error == "unterminated_quote") {
        return ui.Format(UiText::ToastBindTagUnterminatedQuote, token.c_str());
    }
    if (error == "invalid_stable_id") {
        return ui.Format(UiText::ToastBindTagInvalidStableId, token.c_str());
    }
    if (error == "no_folders") {
        return ui.Text(UiText::ToastBindTagNoFolders);
    }
    if (error == "folder_not_found") {
        return ui.Format(UiText::ToastBindTagFolderNotFound, token.c_str());
    }
    if (error == "category_not_found") {
        return ui.Format(UiText::ToastBindTagCategoryNotFound, token.c_str());
    }
    if (error == "bind_ambiguous") {
        return ui.Format(UiText::ToastBindTagAmbiguous, token.c_str());
    }
    if (error == "bind_not_found") {
        return ui.Format(UiText::ToastBindTagBindNotFound, token.c_str());
    }
    if (error == "bind_not_started") {
        return ui.Format(UiText::ToastBindTagNotStarted, token.c_str());
    }
    if (error == "bind_already_running") {
        return ui.Format(UiText::ToastBindTagAlreadyRunning, token.c_str());
    }
    if (error == "bind_disabled") {
        return ui.Format(UiText::ToastBindTagDisabled, token.c_str());
    }
    if (error == "folder_disabled") {
        return ui.Format(UiText::ToastBindTagFolderDisabled, token.c_str());
    }
    if (error == "bind_busy") {
        return ui.Format(UiText::ToastBindTagBusy, token.c_str());
    }
    if (error == "bind_conditions_blocked") {
        return ui.Format(UiText::ToastBindTagConditionsBlocked, token.c_str());
    }
    if (error == "bind_input_busy") {
        return ui.Format(UiText::ToastBindTagInputBusy, token.c_str());
    }
    if (error == "bind_empty") {
        return ui.Format(UiText::ToastBindTagEmpty, token.c_str());
    }
    if (error == "bind_not_running") {
        return ui.Format(UiText::ToastBindTagNotRunning, token.c_str());
    }
    if (error == "bind_not_paused") {
        return ui.Format(UiText::ToastBindTagNotPaused, token.c_str());
    }
    if (error == "bind_already_paused") {
        return ui.Format(UiText::ToastBindTagAlreadyPaused, token.c_str());
    }
    if (error == "bind_no_changes") {
        return ui.Format(UiText::ToastBindTagNoChanges, token.c_str());
    }
    if (error == "bind_popup_unavailable") {
        return ui.Format(UiText::ToastBindTagPopupUnavailable, token.c_str());
    }
    if (error == "stopall_empty") {
        return ui.Text(UiText::ToastBindTagStopAllEmpty);
    }
    if (error == "unknown_action") {
        return ui.Format(UiText::ToastBindTagUnknownAction, std::string(actionName).c_str());
    }
    return ui.Format(UiText::ToastBindTagBindNotFound, token.c_str());
}

bool BinderModule::Impl::RequestBindLinesPopup(int index) {
    if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
        return false;
    }

    bindLinesTarget = index;
    bindLinesPopupPending = true;
    return true;
}

BinderModule::TagActionResult BinderModule::Impl::ExecuteBindTagActionNow(
    BindTagAction action,
    const std::vector<int>& targetIndices,
    std::string_view actionName,
    std::uint64_t sourceRuntimeId) {
    (void)actionName;

    BinderModule::TagActionResult result;
    result.affected = 0;

    switch (action) {
    case BindTagAction::StopAll: {
        const int stopped = StopAllHotkeys();
        result.success = true;
        result.affected = stopped;
        return result;
    }
    case BindTagAction::Ended: {
        bool ended = true;
        for (const int index : targetIndices) {
            if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
                ended = false;
                break;
            }

            const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
            if (hotkey.awaitingInput || hotkey.waitingTextConfirmation || FindRunningBindForHotkey(index) != nullptr) {
                ended = false;
                break;
            }
        }

        result.success = true;
        result.affected = static_cast<int>(targetIndices.size());
        result.value = ended ? "1" : "0";
        return result;
    }
    case BindTagAction::Random: {
        std::vector<int> pool;
        pool.reserve(targetIndices.size());
        int rejectedDisabled = 0;
        int rejectedInput = 0;
        int rejectedRunning = 0;
        int rejectedCursor = 0;
        int rejectedConditions = 0;
        const ConditionRuntimeContext conditionContext = MakeConditionContext(false);
        for (const int index : targetIndices) {
            if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
                continue;
            }
            const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
            if (!IsHotkeyEffectivelyEnabled(index)) {
                ++rejectedDisabled;
                continue;
            }
            if (hotkey.awaitingInput || hotkey.waitingTextConfirmation) {
                ++rejectedInput;
                continue;
            }
            if (IsHotkeyRunning(index)) {
                ++rejectedRunning;
                continue;
            }
            if (ShouldBlockHelperCursorActivation(hotkey, "bind_tag")) {
                ++rejectedCursor;
                continue;
            }
            if (ConditionsBlocked(hotkey.conditions, hotkey.conditionsCombine, sampApi, &conditionContext)) {
                ++rejectedConditions;
                continue;
            }
            pool.push_back(index);
        }

        if (pool.empty()) {
            debuglog::WriteError(
                "[binder][bind-tag] random rejected candidates=%d eligible=0 disabled=%d input=%d running=%d cursor=%d conditions=%d source={%s}",
                static_cast<int>(targetIndices.size()),
                rejectedDisabled,
                rejectedInput,
                rejectedRunning,
                rejectedCursor,
                rejectedConditions,
                DescribeBindTagLogSource(sourceRuntimeId).c_str());
            result.error = "bind_not_started";
            return result;
        }

        const int chosen = pool[RandomIndex(pool.size())];
        debuglog::WriteInfo(
            "[binder][bind-tag] random selected candidates=%d eligible=%d disabled=%d input=%d running=%d cursor=%d conditions=%d source={%s} chosen={%s}",
            static_cast<int>(targetIndices.size()),
            static_cast<int>(pool.size()),
            rejectedDisabled,
            rejectedInput,
            rejectedRunning,
            rejectedCursor,
            rejectedConditions,
            DescribeBindTagLogSource(sourceRuntimeId).c_str(),
            DescribeBindTagLogTargets({ chosen }).c_str());
        if (TryEnqueueHotkey(chosen, 0, "bind_tag", std::string(actionName))) {
            result.success = true;
            result.affected = 1;
        } else {
            result.error = "bind_not_started";
        }
        return result;
    }
    case BindTagAction::Popup: {
        if (!targetIndices.empty() && RequestBindLinesPopup(targetIndices.front())) {
            result.success = true;
            result.affected = 1;
        } else {
            result.error = "bind_popup_unavailable";
        }
        return result;
    }
    case BindTagAction::Start: {
        int changed = 0;
        for (const int index : targetIndices) {
            if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
                continue;
            }

            if (sourceRuntimeId != 0
                && hotkeys[static_cast<std::size_t>(index)].runtimeId == sourceRuntimeId
                && FindRunningBind(sourceRuntimeId) != nullptr) {
                QueueSelfRestart(sourceRuntimeId);
                ++changed;
                continue;
            }

            const HotkeyEntry& target = hotkeys[static_cast<std::size_t>(index)];
            if (IsHotkeyRunning(index)) {
                result.error = "bind_already_running";
                return result;
            }
            if (!target.enabled) {
                result.error = "bind_disabled";
                return result;
            }
            const BinderCategory* category = FindCategoryById(target.categoryId);
            if (!category || !IsFolderPathEnabled(*category, target.folderPath)) {
                result.error = "folder_disabled";
                return result;
            }
            if (target.awaitingInput || target.waitingTextConfirmation) {
                result.error = "bind_busy";
                return result;
            }
            std::string conditionMessage;
            if (ConditionsBlockHotkeyStart(target, "bind_tag", &conditionMessage)) {
                result.error = "bind_conditions_blocked";
                return result;
            }
            if (!target.inputs.empty() && inputDialog.has_value() && inputDialog->hotkeyIndex != index) {
                result.error = "bind_input_busy";
                return result;
            }
            if (target.inputs.empty() && target.messages.empty()) {
                result.error = "bind_empty";
                return result;
            }

            if (TryEnqueueHotkey(index, 0, "bind_tag", std::string(actionName))) {
                ++changed;
            }
        }
        result.success = changed > 0;
        result.affected = changed;
        if (!result.success) {
            result.error = "bind_not_started";
        }
        return result;
    }
    case BindTagAction::Stop: {
        int changed = 0;
        for (const int index : targetIndices) {
            if (StopHotkey(index)) {
                ++changed;
            }
        }
        result.success = changed > 0;
        result.affected = changed;
        if (!result.success) {
            result.error = "bind_not_running";
        }
        return result;
    }
    case BindTagAction::Pause: {
        int changed = 0;
        for (const int index : targetIndices) {
            if (IsHotkeyPaused(index)) {
                result.error = "bind_already_paused";
                return result;
            }
            if (PauseHotkey(index)) {
                ++changed;
            }
        }
        result.success = changed > 0;
        result.affected = changed;
        if (!result.success) {
            result.error = "bind_not_running";
        }
        return result;
    }
    case BindTagAction::Unpause: {
        int changed = 0;
        for (const int index : targetIndices) {
            if (ResumeHotkey(index)) {
                ++changed;
            }
        }
        result.success = changed > 0;
        result.affected = changed;
        if (!result.success) {
            result.error = "bind_not_paused";
        }
        return result;
    }
    case BindTagAction::Disable: {
        int changed = 0;
        for (const int index : targetIndices) {
            if (index >= 0 && index < static_cast<int>(hotkeys.size()) && hotkeys[static_cast<std::size_t>(index)].enabled) {
                hotkeys[static_cast<std::size_t>(index)].enabled = false;
                ++changed;
            }
        }
        if (changed > 0) {
            SaveConfig();
        }
        result.success = true;
        result.affected = changed;
        return result;
    }
    case BindTagAction::Enable: {
        int changed = 0;
        for (const int index : targetIndices) {
            if (index >= 0 && index < static_cast<int>(hotkeys.size()) && !hotkeys[static_cast<std::size_t>(index)].enabled) {
                hotkeys[static_cast<std::size_t>(index)].enabled = true;
                ++changed;
            }
        }
        if (changed > 0) {
            SaveConfig();
        }
        result.success = true;
        result.affected = changed;
        return result;
    }
    case BindTagAction::FastMenu:
    case BindTagAction::UnfastMenu: {
        const bool desired = action == BindTagAction::FastMenu;
        int changed = 0;
        for (const int index : targetIndices) {
            if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
                continue;
            }
            HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
            if (hotkey.quickMenu != desired) {
                hotkey.quickMenu = desired;
                ++changed;
            }
        }
        if (changed > 0) {
            SaveConfig();
        }
        result.success = true;
        result.affected = changed;
        return result;
    }
    case BindTagAction::Unknown:
    default:
        result.error = "unknown_action";
        return result;
    }
}

BinderModule::TagActionResult BinderModule::Impl::ExecuteTagAction(
    std::string_view actionName,
    std::string_view param,
    std::uint64_t sourceRuntimeId) {
    BinderModule::TagActionResult result;
    const BindTagAction action = ParseBindTagActionName(actionName);
    if (action == BindTagAction::Unknown) {
        result.error = "unknown_action";
        Notify(NotificationGroup::TagErrors, NotificationSeverity::Error, DescribeBindTagError(actionName, result.error), 2600.0);
        debuglog::WriteError(
            "[binder][bind-tag] action rejected action=\"%s\" error=%s selector=\"%s\" source={%s}",
            EscapeBindTagLogValue(actionName).c_str(),
            result.error.c_str(),
            EscapeBindTagLogValue(param).c_str(),
            DescribeBindTagLogSource(sourceRuntimeId).c_str());
        return result;
    }

    if (action == BindTagAction::StopAll) {
        if (sourceRuntimeId != 0) {
            pendingBindTagActions.push_back(PendingBindTagAction{
                action,
                sourceRuntimeId,
                std::string(actionName),
                std::string(param),
                {},
            });
            debuglog::WriteInfo(
                "[binder][bind-tag] queued action=%.*s selector=\"%s\" source={%s} targets={count=all_running}",
                static_cast<int>(actionName.size()),
                actionName.data(),
                EscapeBindTagLogValue(param).c_str(),
                DescribeBindTagLogSource(sourceRuntimeId).c_str());
            result.success = true;
            result.affected = 1;
            return result;
        }

        result = ExecuteBindTagActionNow(action, {}, actionName, sourceRuntimeId);
        if (!result.success && !result.error.empty()) {
            Notify(NotificationGroup::TagErrors, NotificationSeverity::Error, DescribeBindTagError(actionName, result.error), 2600.0);
        }
        return result;
    }

    std::string error;
    std::vector<int> targetIndices = ResolveBindTagTargets(action, param, sourceRuntimeId, error);
    if (error.empty() && targetIndices.empty()) {
        error = "bind_not_found";
    }

    if (!error.empty()) {
        result.error = error;
        if (action == BindTagAction::Ended) {
            result.value = "0";
        }
        Notify(NotificationGroup::TagErrors, NotificationSeverity::Error, DescribeBindTagError(actionName, error), 2600.0);
        debuglog::WriteError(
            "[binder][bind-tag] resolve failed action=%.*s error=%s selector=\"%s\" source={%s}",
            static_cast<int>(actionName.size()),
            actionName.data(),
            error.c_str(),
            EscapeBindTagLogValue(param).c_str(),
            DescribeBindTagLogSource(sourceRuntimeId).c_str());
        return result;
    }

    if (action != BindTagAction::Ended) {
        debuglog::WriteInfo(
            "[binder][bind-tag] resolved action=%.*s selector=\"%s\" source={%s} targets={%s}",
            static_cast<int>(actionName.size()),
            actionName.data(),
            EscapeBindTagLogValue(param).c_str(),
            DescribeBindTagLogSource(sourceRuntimeId).c_str(),
            DescribeBindTagLogTargets(targetIndices).c_str());
    }

    if (action != BindTagAction::Ended && sourceRuntimeId != 0) {
        pendingBindTagActions.push_back(PendingBindTagAction{
            action,
            sourceRuntimeId,
            std::string(actionName),
            std::string(param),
            std::move(targetIndices),
        });
        debuglog::WriteInfo(
            "[binder][bind-tag] queued action=%.*s selector=\"%s\" source={%s}",
            static_cast<int>(actionName.size()),
            actionName.data(),
            EscapeBindTagLogValue(param).c_str(),
            DescribeBindTagLogSource(sourceRuntimeId).c_str());
        result.success = true;
        return result;
    }

    result = ExecuteBindTagActionNow(action, targetIndices, actionName, sourceRuntimeId);
    if (!result.success && !result.error.empty()) {
        if (action == BindTagAction::Ended) {
            result.value = "0";
        }
        Notify(NotificationGroup::TagErrors, NotificationSeverity::Error, DescribeBindTagError(actionName, result.error), 2600.0);
    }
    if (action == BindTagAction::Ended && sourceRuntimeId != 0) {
        debuglog::WriteInfo(
            "[binder][bind-tag] queried action=%.*s value=%s error=%s selector=\"%s\" source={%s} targets={%s}",
            static_cast<int>(actionName.size()),
            actionName.data(),
            result.value.c_str(),
            result.error.c_str(),
            EscapeBindTagLogValue(param).c_str(),
            DescribeBindTagLogSource(sourceRuntimeId).c_str(),
            DescribeBindTagLogTargets(targetIndices).c_str());
    }
    if (action != BindTagAction::Ended) {
        debuglog::WriteInfo(
            "[binder][bind-tag] executed action=%.*s success=%d affected=%d error=%s selector=\"%s\" source={%s} targets={%s}",
            static_cast<int>(actionName.size()),
            actionName.data(),
            result.success ? 1 : 0,
            result.affected,
            result.error.c_str(),
            EscapeBindTagLogValue(param).c_str(),
            DescribeBindTagLogSource(sourceRuntimeId).c_str(),
            DescribeBindTagLogTargets(targetIndices).c_str());
    }
    return result;
}
