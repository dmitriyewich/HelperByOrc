#include "binder_module_impl.h"

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

std::string BinderModule::Impl::BuildThisbindTagValue(std::uint64_t runtimeId) const {
    const BindTagContextDesc desc = DescribeBindTagContext(runtimeId);
    if (desc.hotkeyIndex < 0 || desc.name.empty()) {
        return {};
    }

    std::string value = QuoteBindTagToken(desc.name);
    value += ' ';
    value += QuoteBindTagToken(desc.folder);
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
        return;
    }
    pendingSelfRestarts.push_back(runtimeId);
}

bool BinderModule::Impl::StartPendingSelfRestart(std::uint64_t runtimeId) {
    const auto it = std::find(pendingSelfRestarts.begin(), pendingSelfRestarts.end(), runtimeId);
    if (it == pendingSelfRestarts.end()) {
        return false;
    }

    pendingSelfRestarts.erase(it);
    const int index = FindHotkeyIndexByRuntimeId(runtimeId);
    if (index < 0) {
        return false;
    }

    return TryEnqueueHotkey(index, kNestedBindStartDelayMs, "bind_tag", "[bindstart({thisbind})]");
}

std::vector<int> BinderModule::Impl::ResolveBindTagTargets(
    BindTagAction action,
    std::string_view rawParam,
    std::uint64_t sourceRuntimeId,
    std::string& error) const {
    error.clear();

    const BindTagContextDesc context = DescribeBindTagContext(sourceRuntimeId);
    const std::vector<BindTagToken> tokens = TokenizeBindTagArgs(rawParam);

    BindTagSelector selector;
    selector.hasTokens = !tokens.empty();
    selector.contextHotkeyIndex = context.hotkeyIndex;

    const auto parseDisplayAlias = [](std::string_view value, int& number, std::string& displayName) {
        constexpr std::string_view kNumberSign = "\xE2\x84\x96";
        std::string text = Trim(value);
        if (text.size() < kNumberSign.size() || std::string_view(text).substr(0, kNumberSign.size()) != kNumberSign) {
            return false;
        }

        text.erase(0, kNumberSign.size());
        text = Trim(text);
        std::size_t digitCount = 0;
        while (digitCount < text.size() && std::isdigit(static_cast<unsigned char>(text[digitCount])) != 0) {
            ++digitCount;
        }
        if (digitCount == 0) {
            return false;
        }

        int parsedNumber = 0;
        if (!ParseInt(std::string_view(text).substr(0, digitCount), parsedNumber) || parsedNumber < 1) {
            return false;
        }

        number = parsedNumber;
        displayName = Trim(std::string_view(text).substr(digitCount));
        return true;
    };

    const auto assignFolderToken = [&](const BindTagToken& token) {
        selector.folderProvided = true;
        selector.folder = token.value;
        selector.folderExact = token.quoted;
        selector.rootExplicit = token.quoted && token.value.empty();
    };

    const auto assignCategoryToken = [&](const BindTagToken& token) {
        selector.categoryProvided = true;
        selector.category = token.value;
        selector.categoryExact = token.quoted;
    };

    if (action == BindTagAction::Random) {
        selector.kind = BindTagTargetKind::All;
        if (tokens.empty()) {
            if (selector.contextHotkeyIndex < 0) {
                error = "param_required";
                return {};
            }
            const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(selector.contextHotkeyIndex)];
            selector.folderProvided = true;
            selector.folder = JoinPath(hotkey.folderPath);
            selector.folderExact = true;
            selector.rootExplicit = hotkey.folderPath.empty();
        } else if (tokens.front().value == "*") {
            selector.folderProvided = false;
            if (tokens.size() >= 2) {
                assignCategoryToken(tokens[1]);
            }
        } else {
            assignFolderToken(tokens.front());
            if (tokens.size() >= 2) {
                assignCategoryToken(tokens[1]);
            }
        }
    } else if (tokens.empty()) {
        if (selector.contextHotkeyIndex < 0) {
            error = "param_required";
            return {};
        }
        selector.kind = BindTagTargetKind::Context;
    } else {
        const BindTagToken& first = tokens.front();
        int numericIndex = 0;
        std::string displayAliasName;
        if (!first.quoted && ParseInt(first.value, numericIndex) && numericIndex > 0) {
            selector.kind = BindTagTargetKind::Number;
            selector.number = numericIndex;
        } else if (parseDisplayAlias(first.value, numericIndex, displayAliasName)) {
            selector.kind = displayAliasName.empty() ? BindTagTargetKind::Number : BindTagTargetKind::DisplayAlias;
            selector.number = numericIndex;
            selector.displayAliasName = std::move(displayAliasName);
        } else {
            selector.kind = BindTagTargetKind::Name;
            selector.target = first.value;
            selector.targetQuoted = first.quoted;
        }

        if (tokens.size() >= 2) {
            assignFolderToken(tokens[1]);
        }
        if (tokens.size() >= 3) {
            assignCategoryToken(tokens[2]);
        }
    }

    if (selector.kind == BindTagTargetKind::Context && selector.contextHotkeyIndex >= 0) {
        return { selector.contextHotkeyIndex };
    }

    const auto pathEqualNoCase = [](const std::vector<std::string>& left, const std::vector<std::string>& right) {
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t i = 0; i < left.size(); ++i) {
            if (!EqualNoCase(left[i], right[i])) {
                return false;
            }
        }
        return true;
    };

    const auto splitFolderPath = [](std::string_view value) {
        std::vector<std::string> path;
        for (std::string part : Split(value, '/')) {
            part = Trim(part);
            if (!part.empty()) {
                path.push_back(std::move(part));
            }
        }
        return path;
    };

    bool categoryAmbiguous = false;
    const auto findCategory = [&](std::string_view query, bool exact) -> const BinderCategory* {
        categoryAmbiguous = false;
        const std::string trimmed = Trim(query);
        if (trimmed.empty()) {
            return nullptr;
        }

        const BinderCategory* partialMatch = nullptr;
        bool partialAmbiguous = false;
        for (const BinderCategory& category : categories) {
            if (EqualNoCase(category.id, trimmed) || EqualNoCase(category.name, trimmed)) {
                return &category;
            }
            if (!exact && (ContainsNoCase(category.id, trimmed) || ContainsNoCase(category.name, trimmed))) {
                if (partialMatch) {
                    partialAmbiguous = true;
                    categoryAmbiguous = true;
                } else {
                    partialMatch = &category;
                }
            }
        }
        return partialAmbiguous ? nullptr : partialMatch;
    };

    const BinderCategory* scopeCategory = nullptr;
    if (selector.categoryProvided && !Trim(selector.category).empty()) {
        scopeCategory = findCategory(selector.category, selector.categoryExact);
        if (!scopeCategory) {
            error = categoryAmbiguous ? "bind_ambiguous" : "category_not_found";
            return {};
        }
    } else {
        const std::string scopeCategoryId = selector.contextHotkeyIndex >= 0
            ? hotkeys[static_cast<std::size_t>(selector.contextHotkeyIndex)].categoryId
            : activeCategoryId;
        scopeCategory = FindCategoryById(scopeCategoryId);
        if (!scopeCategory) {
            scopeCategory = &ActiveCategory();
        }
    }

    std::optional<std::vector<std::string>> scopePath;
    if (selector.folderProvided) {
        if (selector.rootExplicit || Trim(selector.folder).empty()) {
            scopePath = std::vector<std::string>{};
        } else {
            const std::vector<std::string> requestedPath = splitFolderPath(selector.folder);
            std::vector<std::vector<std::string>> folderPaths;
            CollectFolderPaths(scopeCategory->folders, folderPaths);

            std::vector<std::vector<std::string>> matches;
            for (const auto& path : folderPaths) {
                const std::string fullPath = JoinPath(path);
                const std::string folderName = path.empty() ? std::string{} : path.back();
                const bool exactPathMatch = pathEqualNoCase(path, requestedPath)
                    || EqualNoCase(fullPath, selector.folder)
                    || EqualNoCase(folderName, selector.folder);
                const bool partialMatch = !selector.folderExact
                    && (ContainsNoCase(fullPath, selector.folder) || ContainsNoCase(folderName, selector.folder));
                if (selector.folderExact ? exactPathMatch : (exactPathMatch || partialMatch)) {
                    matches.push_back(path);
                }
            }

            if (matches.empty()) {
                error = "folder_not_found";
                return {};
            }
            if (matches.size() > 1) {
                error = "bind_ambiguous";
                return {};
            }
            scopePath = matches.front();
        }
    }

    std::vector<int> candidates;
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        const HotkeyEntry& hotkey = hotkeys[i];
        if (hotkey.categoryId != scopeCategory->id) {
            continue;
        }
        if (scopePath.has_value() && hotkey.folderPath != *scopePath) {
            continue;
        }
        candidates.push_back(static_cast<int>(i));
    }

    if (selector.kind == BindTagTargetKind::All) {
        return candidates;
    }

    const auto hotkeyDisplayName = [this](int index) {
        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
        return BuildBindDisplayLabel(hotkey);
    };

    if (selector.kind == BindTagTargetKind::Number || selector.kind == BindTagTargetKind::DisplayAlias) {
        std::vector<int> matches;
        for (const int index : candidates) {
            const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
            if (hotkey.number != selector.number) {
                continue;
            }
            if (selector.kind == BindTagTargetKind::DisplayAlias
                && !EqualNoCase(hotkeyDisplayName(index), selector.displayAliasName)) {
                continue;
            }
            matches.push_back(index);
        }
        if (matches.size() > 1) {
            error = "bind_ambiguous";
            return {};
        }
        return matches;
    }

    if (selector.kind == BindTagTargetKind::Name && !selector.target.empty()) {
        std::vector<int> exactMatches;
        std::vector<int> partialMatches;
        for (const int index : candidates) {
            const std::string displayName = hotkeyDisplayName(index);
            if (EqualNoCase(displayName, selector.target)) {
                exactMatches.push_back(index);
                continue;
            }
            if (!selector.targetQuoted && ContainsNoCase(displayName, selector.target)) {
                partialMatches.push_back(index);
            }
        }

        if (exactMatches.size() > 1) {
            error = "bind_ambiguous";
            return {};
        }
        if (exactMatches.size() == 1) {
            return exactMatches;
        }
        if (selector.targetQuoted) {
            return {};
        }
        if (partialMatches.size() > 1) {
            error = "bind_ambiguous";
            return {};
        }
        if (partialMatches.size() == 1) {
            return partialMatches;
        }
        return {};
    }

    return {};
}

std::string BinderModule::Impl::DescribeBindTagError(std::string_view actionName, std::string_view error) const {
    UiSettings& ui = UiSettings::Instance();
    const std::string token = "[" + std::string(actionName) + "]";

    if (error == "param_required") {
        return ui.Format(UiText::ToastBindTagTargetRequired, token.c_str());
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
    if (error == "bind_not_running") {
        return ui.Format(UiText::ToastBindTagNotRunning, token.c_str());
    }
    if (error == "bind_not_paused") {
        return ui.Format(UiText::ToastBindTagNotPaused, token.c_str());
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
        const ConditionRuntimeContext conditionContext = MakeConditionContext(false);
        for (const int index : targetIndices) {
            if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
                continue;
            }
            const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
            if (!hotkey.enabled
                || hotkey.awaitingInput
                || hotkey.waitingTextConfirmation
                || IsHotkeyRunning(index)
                || ShouldBlockHelperCursorActivation(hotkey, "bind_tag")
                || ConditionsBlocked(hotkey.conditions, hotkey.conditionsCombine, sampApi, &conditionContext)) {
                continue;
            }
            pool.push_back(index);
        }

        if (pool.empty()) {
            result.error = "bind_not_started";
            return result;
        }

        const int chosen = pool[RandomIndex(pool.size())];
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
        return result;
    }

    if (action == BindTagAction::StopAll) {
        if (sourceRuntimeId != 0) {
            pendingBindTagActions.push_back(PendingBindTagAction{
                action,
                sourceRuntimeId,
                std::string(actionName),
                {},
            });
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
        return result;
    }

    if (action != BindTagAction::Ended && sourceRuntimeId != 0) {
        pendingBindTagActions.push_back(PendingBindTagAction{
            action,
            sourceRuntimeId,
            std::string(actionName),
            std::move(targetIndices),
        });
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
    return result;
}
