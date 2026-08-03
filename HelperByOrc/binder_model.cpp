#include "binder_module_impl.h"

std::string BinderModule::Impl::AllocateCategoryId() {
    return "category-" + std::to_string(nextCategoryId++);
}

BinderCategory BinderModule::Impl::MakeDefaultCategory() {
    BinderCategory category;
    category.id = AllocateCategoryId();
    category.name = UiSettings::Instance().Text(UiText::BinderDefaultRootFolder);
    category.conditions.assign(static_cast<std::size_t>(ConditionId::Count), false);
    return category;
}

void BinderModule::Impl::EnsureCategories() {
    if (categories.empty()) {
        categories.push_back(MakeDefaultCategory());
    }

    for (BinderCategory& category : categories) {
        if (category.id.empty()) {
            category.id = AllocateCategoryId();
        }
        category.name = SanitizeFolderName(category.name);
        if (category.name.empty()) {
            category.name = UiSettings::Instance().Text(UiText::BinderDefaultRootFolder);
        }
        if (category.conditions.size() < static_cast<std::size_t>(ConditionId::Count)) {
            category.conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
        }
    }

    if (!FindCategoryById(activeCategoryId)) {
        activeCategoryId = categories.front().id;
    }
}

BinderCategory* BinderModule::Impl::FindCategoryById(std::string_view id) {
    for (BinderCategory& category : categories) {
        if (category.id == id) {
            return &category;
        }
    }
    return nullptr;
}

const BinderCategory* BinderModule::Impl::FindCategoryById(std::string_view id) const {
    for (const BinderCategory& category : categories) {
        if (category.id == id) {
            return &category;
        }
    }
    return nullptr;
}

BinderCategory& BinderModule::Impl::ActiveCategory() {
    EnsureCategories();
    if (BinderCategory* category = FindCategoryById(activeCategoryId)) {
        return *category;
    }
    activeCategoryId = categories.front().id;
    return categories.front();
}

const BinderCategory& BinderModule::Impl::ActiveCategory() const {
    if (const BinderCategory* category = FindCategoryById(activeCategoryId)) {
        return *category;
    }
    return categories.front();
}

std::vector<std::unique_ptr<FolderNode>>& BinderModule::Impl::ActiveFolders() {
    return ActiveCategory().folders;
}

const std::vector<std::unique_ptr<FolderNode>>& BinderModule::Impl::ActiveFolders() const {
    return ActiveCategory().folders;
}

std::vector<ExplorerItem>& BinderModule::Impl::ActiveRootItems() {
    return ActiveCategory().rootItems;
}

const std::vector<ExplorerItem>& BinderModule::Impl::ActiveRootItems() const {
    return ActiveCategory().rootItems;
}

std::vector<std::vector<std::string>>& BinderModule::Impl::ActiveNavigationBackStack() {
    return ActiveCategory().navigationBackStack;
}

const std::vector<std::vector<std::string>>& BinderModule::Impl::ActiveNavigationBackStack() const {
    return ActiveCategory().navigationBackStack;
}

void BinderModule::Impl::SelectCategory(std::string_view categoryId) {
    if (categoryId == activeCategoryId || !FindCategoryById(categoryId)) {
        return;
    }

    CancelInlineFolderEdit();
    folderDeleteTarget = nullptr;
    folderDeletePopupPending = false;
    folderConditionsTarget = nullptr;
    folderConditionsPopupPending = false;
    folderIconTarget = nullptr;
    folderIconPopupPending = false;
    moveFolderTarget = -1;
    moveFolderPopupPending = false;
    activeCategoryId = std::string(categoryId);
    currentFolder = nullptr;
    if (BinderCategory* category = FindCategoryById(activeCategoryId)) {
        if (!category->lastOpenFolderPath.empty()) {
            currentFolder = FindFolderByPath(category->folders, category->lastOpenFolderPath);
            if (!currentFolder) {
                category->lastOpenFolderPath.clear();
            }
        }
    }
    ClearExplorerSelection();
    categoryTabSelectionTargetId = activeCategoryId;
    SaveConfig();
}

std::string BinderModule::Impl::NextCategoryName() const {
    const std::string baseName = UiSettings::Instance().Text(UiText::BinderNewCategory);
    std::string name = baseName;
    for (int suffix = 2; !CategoryNameUnique(name); ++suffix) {
        name = baseName + " " + std::to_string(suffix);
    }
    return name;
}

bool BinderModule::Impl::CategoryNameUnique(std::string_view name, const BinderCategory* ignoredCategory) const {
    for (const BinderCategory& category : categories) {
        if (&category == ignoredCategory) {
            continue;
        }
        if (binder_tags::EqualNoCaseUtf8(category.name, name)) {
            return false;
        }
    }
    return true;
}

void BinderModule::Impl::BeginRenameCategory(std::string_view categoryId) {
    BinderCategory* category = FindCategoryById(categoryId);
    if (!category) {
        return;
    }
    categoryRenameTargetId = category->id;
    categoryRenameBuffer = category->name;
    categoryRenamePopupPending = true;
}

bool BinderModule::Impl::MoveCategoryByOffset(std::string_view categoryId, int offset) {
    if (offset == 0) {
        return false;
    }
    const auto it = std::find_if(categories.begin(), categories.end(), [&](const BinderCategory& category) {
        return category.id == categoryId;
    });
    if (it == categories.end()) {
        return false;
    }
    const int index = static_cast<int>(std::distance(categories.begin(), it));
    const int target = std::clamp(index + offset, 0, static_cast<int>(categories.size()) - 1);
    if (target == index) {
        return false;
    }
    std::iter_swap(categories.begin() + index, categories.begin() + target);
    categoryTabSelectionTargetId = std::string(categoryId);
    ++categoryTabOrderRevision;
    debuglog::WriteInfo(
        "[binder] category moved id=%.*s from=%d to=%d",
        static_cast<int>(categoryId.size()),
        categoryId.data(),
        index,
        target);
    SaveConfig();
    return true;
}

void BinderModule::Impl::MoveCategoryContents(BinderCategory& from, BinderCategory& to) {
    NormalizeExplorerOrders();
    std::vector<ExplorerItem> movedOrder;
    movedOrder.reserve(from.rootItems.size());

    const auto findRootFolderByName = [](std::vector<std::unique_ptr<FolderNode>>& roots, std::string_view name) {
        return std::find_if(roots.begin(), roots.end(), [&](const std::unique_ptr<FolderNode>& folder) {
            return folder && folder->name == name;
        });
    };

    for (const ExplorerItem& item : from.rootItems) {
        if (item.kind == ExplorerItemKind::Bind) {
            const int index = FindHotkeyIndexByOrderId(item.key);
            if (index >= 0 && hotkeys[static_cast<std::size_t>(index)].categoryId == from.id) {
                hotkeys[static_cast<std::size_t>(index)].categoryId = to.id;
                movedOrder.push_back(item);
            }
            continue;
        }

        auto folderIt = findRootFolderByName(from.folders, item.key);
        if (folderIt == from.folders.end()) {
            continue;
        }

        FolderNode* folder = folderIt->get();
        const std::vector<std::string> oldPath = BuildFolderPath(folder);
        const std::string baseName = folder->name;
        std::string newName = baseName;
        for (int suffix = 2; !FolderNameUnique(to.folders, newName, nullptr); ++suffix) {
            newName = baseName + " " + std::to_string(suffix);
        }
        folder->name = newName;
        folder->parent = nullptr;
        const std::vector<std::string> newPath{ newName };
        for (HotkeyEntry& hotkey : hotkeys) {
            if (hotkey.categoryId == from.id && PathStartsWith(hotkey.folderPath, oldPath)) {
                hotkey.categoryId = to.id;
                hotkey.folderPath = ReplacePathPrefix(hotkey.folderPath, oldPath, newPath);
            }
        }

        to.folders.push_back(std::move(*folderIt));
        from.folders.erase(folderIt);
        movedOrder.push_back(ExplorerItem{ ExplorerItemKind::Folder, newName });
    }

    for (HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.categoryId == from.id) {
            hotkey.categoryId = to.id;
            if (hotkey.folderPath.empty() && !hotkey.orderId.empty()) {
                movedOrder.push_back(ExplorerItem{ ExplorerItemKind::Bind, hotkey.orderId });
            }
        }
    }

    to.rootItems.insert(to.rootItems.end(), movedOrder.begin(), movedOrder.end());
    from.rootItems.clear();
    from.folders.clear();
}

void BinderModule::Impl::DeleteCategory(
    std::string_view categoryId,
    std::string_view moveTargetId,
    const bool deleteContents) {
    if (categories.size() <= 1) {
        Notify(
            NotificationGroup::Validation,
            NotificationSeverity::Error,
            UiSettings::Instance().Text(UiText::CategoryCannotDeleteLast),
            2200.0);
        return;
    }

    auto sourceIt = std::find_if(categories.begin(), categories.end(), [&](const BinderCategory& category) {
        return category.id == categoryId;
    });
    if (sourceIt == categories.end()) {
        return;
    }

    if (!deleteContents) {
        BinderCategory* target = FindCategoryById(moveTargetId);
        if (!target || target->id == sourceIt->id) {
            return;
        }
        MoveCategoryContents(*sourceIt, *target);
    } else {
        std::vector<std::uint64_t> runtimeIds;
        for (const HotkeyEntry& hotkey : hotkeys) {
            if (hotkey.categoryId == sourceIt->id) {
                runtimeIds.push_back(hotkey.runtimeId);
            }
        }
        for (const std::uint64_t runtimeId : runtimeIds) {
            StopHotkeyByRuntimeId(runtimeId);
        }
        hotkeys.erase(
            std::remove_if(hotkeys.begin(), hotkeys.end(), [&](const HotkeyEntry& hotkey) {
                return hotkey.categoryId == sourceIt->id;
            }),
            hotkeys.end());
        RefreshNumbers();
    }

    const bool deletingActive = sourceIt->id == activeCategoryId;
    const std::string fallbackId = !deleteContents && !moveTargetId.empty()
        ? std::string(moveTargetId)
        : (sourceIt == categories.begin() ? (sourceIt + 1)->id : (sourceIt - 1)->id);
    categories.erase(sourceIt);
    ++categoryTabOrderRevision;
    EnsureCategories();
    if (deletingActive || !FindCategoryById(activeCategoryId)) {
        activeCategoryId = FindCategoryById(fallbackId) ? fallbackId : categories.front().id;
        currentFolder = nullptr;
    }
    categoryTabSelectionTargetId = activeCategoryId;
    ClearExplorerSelection();
    NormalizeExplorerOrders();
    SaveConfig();
}

std::uint64_t BinderModule::Impl::AllocateHotkeyRuntimeId() {
    return nextHotkeyRuntimeId++;
}

std::string BinderModule::Impl::AllocateHotkeyOrderId() {
    return "bind-" + std::to_string(nextHotkeyOrderId++);
}

FolderNode* BinderModule::Impl::EnsureRootFolder() {
    EnsureCategories();
    return ActiveFolders().empty() ? nullptr : ActiveFolders().front().get();
}

bool BinderModule::Impl::NormalizeLegacyRootFolderName() {
    FolderNode* root = ActiveFolders().empty() ? nullptr : ActiveFolders().front().get();
    if (!root) {
        return false;
    }

    const std::string desiredName = UiSettings::Instance().Text(UiText::BinderDefaultRootFolder);
    if (root->name == desiredName) {
        return false;
    }
    if (!root->name.empty() && !IsLegacyRootFolderName(root->name)) {
        return false;
    }

    const auto oldPath = BuildFolderPath(root);
    root->name = desiredName;
    RemapHotkeysFolderPrefix(oldPath, BuildFolderPath(root));
    return true;
}

HotkeyEntry BinderModule::Impl::MakeDefaultHotkey() {
    HotkeyEntry hotkey;
    hotkey.label = UiSettings::Instance().Text(UiText::BinderDefaultHotkey);
    hotkey.hotkeyMode = HotkeyMode::ModifierTrigger;
    hotkey.messages.push_back(HotkeyMessage{ "", 0, 2 });
    hotkey.conditions.assign(static_cast<std::size_t>(ConditionId::Count), false);
    hotkey.repeatIntervalMs = kDefaultRepeatIntervalMs;
    hotkey.textConfirmation = TextConfirmation{};
    hotkey.runtimeId = AllocateHotkeyRuntimeId();
    hotkey.orderId = AllocateHotkeyOrderId();
    hotkey.categoryId = ActiveCategory().id;
    return hotkey;
}

void BinderModule::Impl::RefreshNumbers() {
    int number = 1;
    for (HotkeyEntry& hotkey : hotkeys) {
        hotkey.number = number++;
    }
}

std::vector<ExplorerItem>& BinderModule::Impl::ItemsForFolder(FolderNode* folder) {
    return folder ? folder->items : ActiveRootItems();
}

const std::vector<ExplorerItem>& BinderModule::Impl::ItemsForFolder(const FolderNode* folder) const {
    return folder ? folder->items : ActiveRootItems();
}

std::vector<std::string> BinderModule::Impl::FolderPathForDirectory(const FolderNode* folder) const {
    return folder ? BuildFolderPath(folder) : std::vector<std::string>{};
}

std::vector<std::string> BinderModule::Impl::CurrentFolderPath() const {
    return FolderPathForDirectory(currentFolder);
}

void BinderModule::Impl::ClearExplorerSelection() {
    explorerSelection = {};
    explorerSelectionScrollPending = false;
    selectedFolder = nullptr;
    selectedBindIndex = -1;
}

void BinderModule::Impl::SelectExplorerFolder(FolderNode* folder, const bool scrollIntoView) {
    if (!folder) {
        ClearExplorerSelection();
        return;
    }

    explorerSelection.kind = ExplorerSelectionKind::Folder;
    explorerSelection.folderId = folder->id;
    explorerSelection.bindOrderId.clear();
    explorerSelectionScrollPending = explorerSelectionScrollPending || scrollIntoView;
    selectedFolder = folder;
    selectedBindIndex = -1;
}

void BinderModule::Impl::SelectExplorerBind(const int index, const bool scrollIntoView) {
    if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
        ClearExplorerSelection();
        return;
    }

    explorerSelection.kind = ExplorerSelectionKind::Bind;
    explorerSelection.folderId = 0;
    explorerSelection.bindOrderId = hotkeys[static_cast<std::size_t>(index)].orderId;
    explorerSelectionScrollPending = explorerSelectionScrollPending || scrollIntoView;
    selectedFolder = nullptr;
    selectedBindIndex = index;
}

void BinderModule::Impl::SelectExplorerItem(
    const ExplorerItem& item,
    FolderNode* directory,
    const bool scrollIntoView) {
    if (item.kind == ExplorerItemKind::Folder) {
        SelectExplorerFolder(FindFolderByNameInDirectory(directory, item.key), scrollIntoView);
        return;
    }

    SelectExplorerBind(FindHotkeyIndexByOrderId(item.key), scrollIntoView);
}

bool BinderModule::Impl::IsExplorerFolderSelected(const FolderNode* folder) const {
    return folder != nullptr
        && explorerSelection.kind == ExplorerSelectionKind::Folder
        && explorerSelection.folderId == folder->id;
}

bool BinderModule::Impl::IsExplorerBindSelected(const int index) const {
    return index >= 0
        && index < static_cast<int>(hotkeys.size())
        && explorerSelection.kind == ExplorerSelectionKind::Bind
        && explorerSelection.bindOrderId == hotkeys[static_cast<std::size_t>(index)].orderId;
}

bool BinderModule::Impl::IsFolderEffectivelyEnabled(const FolderNode* folder) const {
    for (const FolderNode* current = folder; current != nullptr; current = current->parent) {
        if (!current->enabled) {
            return false;
        }
    }
    return true;
}

bool BinderModule::Impl::IsFolderPathEnabled(
    const BinderCategory& category,
    const std::vector<std::string>& path) const {
    if (path.empty()) {
        return true;
    }

    const FolderNode* folder = FindFolderByPath(category.folders, path);
    return !folder || IsFolderEffectivelyEnabled(folder);
}

bool BinderModule::Impl::IsHotkeyFolderEnabled(const HotkeyEntry& hotkey) const {
    if (hotkey.folderPath.empty()) {
        return true;
    }

    const BinderCategory* category = FindCategoryById(hotkey.categoryId);
    return !category || IsFolderPathEnabled(*category, hotkey.folderPath);
}

bool BinderModule::Impl::IsHotkeyEffectivelyEnabled(const HotkeyEntry& hotkey) const {
    return hotkey.enabled && IsHotkeyFolderEnabled(hotkey);
}

bool BinderModule::Impl::IsHotkeyEffectivelyEnabled(const int index) const {
    return index >= 0
        && index < static_cast<int>(hotkeys.size())
        && IsHotkeyEffectivelyEnabled(hotkeys[static_cast<std::size_t>(index)]);
}

bool BinderModule::Impl::IsExplorerSelectionVisibleInFolder(const ExplorerSelection& selection, FolderNode* directory) {
    if (selection.kind == ExplorerSelectionKind::None) {
        return true;
    }

    const std::vector<ExplorerItem>& items = ItemsForFolder(directory);
    if (selection.kind == ExplorerSelectionKind::Bind) {
        return std::any_of(items.begin(), items.end(), [&](const ExplorerItem& item) {
            return item.kind == ExplorerItemKind::Bind && item.key == selection.bindOrderId;
        });
    }

    FolderNode* folder = FindFolderByIdR(ActiveFolders(), selection.folderId);
    if (!folder || folder->parent != directory) {
        return false;
    }
    return std::any_of(items.begin(), items.end(), [&](const ExplorerItem& item) {
        return item.kind == ExplorerItemKind::Folder && item.key == folder->name;
    });
}

int BinderModule::Impl::FindExplorerSelectionIndex(
    const std::vector<ExplorerItem>& items,
    FolderNode* directory) const {
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const ExplorerItem& item = items[static_cast<std::size_t>(i)];
        if (item.kind == ExplorerItemKind::Folder) {
            const FolderNode* folder = FindFolderByNameInDirectory(directory, item.key);
            if (IsExplorerFolderSelected(folder)) {
                return i;
            }
            continue;
        }

        if (explorerSelection.kind == ExplorerSelectionKind::Bind && explorerSelection.bindOrderId == item.key) {
            return i;
        }
    }
    return -1;
}

void BinderModule::Impl::MoveExplorerSelection(const int delta) {
    if (delta == 0) {
        return;
    }

    const std::vector<ExplorerItem>& items = ItemsForFolder(currentFolder);
    if (items.empty()) {
        ClearExplorerSelection();
        return;
    }

    int index = FindExplorerSelectionIndex(items, currentFolder);
    if (index < 0) {
        index = delta > 0 ? 0 : static_cast<int>(items.size()) - 1;
    } else {
        index = std::clamp(index + delta, 0, static_cast<int>(items.size()) - 1);
    }

    SelectExplorerItem(items[static_cast<std::size_t>(index)], currentFolder, true);
}

void BinderModule::Impl::SelectExplorerNeighborAfterRemoval(
    FolderNode* directory,
    const ExplorerItem& removedItem,
    const std::vector<ExplorerItem>& beforeItems) {
    const std::vector<ExplorerItem>& afterItems = ItemsForFolder(directory);
    if (afterItems.empty()) {
        ClearExplorerSelection();
        return;
    }

    int removedIndex = -1;
    for (int i = 0; i < static_cast<int>(beforeItems.size()); ++i) {
        const ExplorerItem& item = beforeItems[static_cast<std::size_t>(i)];
        if (item.kind == removedItem.kind && item.key == removedItem.key) {
            removedIndex = i;
            break;
        }
    }

    if (removedIndex < 0) {
        removedIndex = 0;
    }
    const int nextIndex = std::clamp(removedIndex, 0, static_cast<int>(afterItems.size()) - 1);
    SelectExplorerItem(afterItems[static_cast<std::size_t>(nextIndex)], directory, true);
}

FolderNode* BinderModule::Impl::FindFolderByNameInDirectory(FolderNode* folder, std::string_view name) const {
    return FindFolderByNameInDirectory(ActiveCategory(), folder, name);
}

FolderNode* BinderModule::Impl::FindFolderByNameInDirectory(
    const BinderCategory& category,
    FolderNode* folder,
    std::string_view name) const {
    const auto& children = folder ? folder->children : category.folders;
    for (const auto& child : children) {
        if (child && child->name == name) {
            return child.get();
        }
    }
    return nullptr;
}

void BinderModule::Impl::OpenFolder(FolderNode* folder, const bool pushHistory) {
    const std::vector<std::string> oldPath = CurrentFolderPath();
    const std::vector<std::string> newPath = FolderPathForDirectory(folder);
    if (oldPath == newPath) {
        currentFolder = folder;
        ClearExplorerSelection();
        return;
    }
    CancelInlineFolderEdit();
    if (pushHistory) {
        auto& navigationBackStack = ActiveNavigationBackStack();
        navigationBackStack.push_back(oldPath);
        if (navigationBackStack.size() > 64) {
            navigationBackStack.erase(navigationBackStack.begin());
        }
    }
    currentFolder = folder;
    ActiveCategory().lastOpenFolderPath = newPath;
    ClearExplorerSelection();
    if (saveLastOpenFolder) {
        SaveConfig();
    }
}

void BinderModule::Impl::QueueFolderOpenStateSave() {
    if (saveFolderOpenState) {
        SaveConfig();
    }
}

void BinderModule::Impl::SetFolderOpenState(FolderNode* folder, const bool open) {
    if (!folder || folder->open == open) {
        return;
    }

    folder->open = open;
    QueueFolderOpenStateSave();
}

void BinderModule::Impl::OpenFolderPath(const std::vector<std::string>& path, const bool pushHistory) {
    if (path.empty()) {
        OpenFolder(nullptr, pushHistory);
        return;
    }
    if (FolderNode* folder = FindFolderByPath(ActiveFolders(), path)) {
        OpenFolder(folder, pushHistory);
    }
}

void BinderModule::Impl::NavigateUp() {
    if (!currentFolder) {
        return;
    }
    OpenFolder(currentFolder->parent, true);
}

void BinderModule::Impl::NavigateBack() {
    auto& navigationBackStack = ActiveNavigationBackStack();
    if (navigationBackStack.empty()) {
        return;
    }
    const std::vector<std::string> path = navigationBackStack.back();
    navigationBackStack.pop_back();
    OpenFolderPath(path, false);
}

void BinderModule::Impl::NormalizeExplorerOrderForDirectory(FolderNode* folder) {
    NormalizeExplorerOrderForDirectory(ActiveCategory(), folder);
}

void BinderModule::Impl::NormalizeExplorerOrderForDirectory(BinderCategory& category, FolderNode* folder) {
    const std::vector<std::string> path = FolderPathForDirectory(folder);
    auto& items = folder ? folder->items : category.rootItems;
    std::vector<ExplorerItem> normalized;
    normalized.reserve(items.size());
    std::set<std::string> seenFolders;
    std::set<std::string> seenBinds;

    const auto folderExists = [&](std::string_view name) {
        return FindFolderByNameInDirectory(category, folder, name) != nullptr;
    };
    const auto bindBelongsHere = [&](std::string_view orderId) {
        const int index = FindHotkeyIndexByOrderId(orderId);
        return index >= 0
            && hotkeys[static_cast<std::size_t>(index)].categoryId == category.id
            && hotkeys[static_cast<std::size_t>(index)].folderPath == path;
    };

    for (const ExplorerItem& item : items) {
        if (item.kind == ExplorerItemKind::Folder) {
            if (folderExists(item.key) && seenFolders.insert(item.key).second) {
                normalized.push_back(item);
            }
        } else if (bindBelongsHere(item.key) && seenBinds.insert(item.key).second) {
            normalized.push_back(item);
        }
    }

    const auto& children = folder ? folder->children : category.folders;
    for (const auto& child : children) {
        if (child && seenFolders.insert(child->name).second) {
            normalized.push_back(ExplorerItem{ ExplorerItemKind::Folder, child->name });
        }
    }
    for (const HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.categoryId == category.id
            && hotkey.folderPath == path
            && !hotkey.orderId.empty()
            && seenBinds.insert(hotkey.orderId).second) {
            normalized.push_back(ExplorerItem{ ExplorerItemKind::Bind, hotkey.orderId });
        }
    }

    items = std::move(normalized);
    for (auto& child : children) {
        if (child) {
            NormalizeExplorerOrderForDirectory(category, child.get());
        }
    }
}

void BinderModule::Impl::NormalizeExplorerOrders() {
    EnsureCategories();
    EnsureHotkeyOrderIds();
    for (BinderCategory& category : categories) {
        NormalizeExplorerOrderForDirectory(category, nullptr);
    }
}

void BinderModule::Impl::LogExplorerOrderValidation(std::string_view source) {
    int errors = 0;
    const auto logError = [&](const char* message, const BinderCategory& category, const std::vector<std::string>& path, const std::string& key) {
        if (errors++ >= 8) {
            return;
        }
        const std::string dirText = path.empty() ? std::string("<root>") : JoinPath(path);
        debuglog::WriteError(
            "[binder] explorer order invalid source=%.*s category=%s dir=%s key=%s reason=%s",
            static_cast<int>(source.size()),
            source.data(),
            category.id.c_str(),
            dirText.c_str(),
            key.c_str(),
            message);
    };

    const auto validateDirectory = [&](auto&& self, BinderCategory& category, FolderNode* folder) -> void {
        const std::vector<std::string> path = FolderPathForDirectory(folder);
        std::set<std::string> expectedFolders;
        const auto& children = folder ? folder->children : category.folders;
        for (const auto& child : children) {
            if (child) {
                expectedFolders.insert(child->name);
            }
        }

        std::set<std::string> expectedBinds;
        for (const HotkeyEntry& hotkey : hotkeys) {
            if (hotkey.categoryId == category.id && hotkey.folderPath == path && !hotkey.orderId.empty()) {
                expectedBinds.insert(hotkey.orderId);
            }
        }

        std::set<std::string> seenFolders;
        std::set<std::string> seenBinds;
        const std::vector<ExplorerItem>& items = folder ? folder->items : category.rootItems;
        for (const ExplorerItem& item : items) {
            if (item.kind == ExplorerItemKind::Folder) {
                if (expectedFolders.find(item.key) == expectedFolders.end()) {
                    logError("missing_folder", category, path, item.key);
                } else if (!seenFolders.insert(item.key).second) {
                    logError("duplicate_folder", category, path, item.key);
                }
                continue;
            }

            if (expectedBinds.find(item.key) == expectedBinds.end()) {
                logError("missing_bind", category, path, item.key);
            } else if (!seenBinds.insert(item.key).second) {
                logError("duplicate_bind", category, path, item.key);
            }
        }

        for (const std::string& name : expectedFolders) {
            if (seenFolders.find(name) == seenFolders.end()) {
                logError("folder_not_in_order", category, path, name);
            }
        }
        for (const std::string& orderId : expectedBinds) {
            if (seenBinds.find(orderId) == seenBinds.end()) {
                logError("bind_not_in_order", category, path, orderId);
            }
        }

        for (const auto& child : children) {
            if (child) {
                self(self, category, child.get());
            }
        }
    };

    for (BinderCategory& category : categories) {
        validateDirectory(validateDirectory, category, nullptr);
    }
}

void BinderModule::Impl::RemoveBindFromExplorerOrders(const std::string& orderId) {
    if (orderId.empty()) {
        return;
    }

    const auto removeFrom = [&](std::vector<ExplorerItem>& items) {
        items.erase(
            std::remove_if(items.begin(), items.end(), [&](const ExplorerItem& item) {
                return item.kind == ExplorerItemKind::Bind && item.key == orderId;
            }),
            items.end());
    };

    const auto recurse = [&](auto&& self, std::vector<std::unique_ptr<FolderNode>>& nodes) -> void {
        for (auto& node : nodes) {
            if (!node) {
                continue;
            }
            removeFrom(node->items);
            self(self, node->children);
        }
    };
    for (BinderCategory& category : categories) {
        removeFrom(category.rootItems);
        recurse(recurse, category.folders);
    }
}

void BinderModule::Impl::RemoveFolderFromParentOrder(FolderNode* folder) {
    if (!folder) {
        return;
    }
    auto& items = ItemsForFolder(folder->parent);
    items.erase(
        std::remove_if(items.begin(), items.end(), [&](const ExplorerItem& item) {
            return item.kind == ExplorerItemKind::Folder && item.key == folder->name;
        }),
        items.end());
}

bool BinderModule::Impl::InsertExplorerItem(FolderNode* folder, ExplorerItem item, int index) {
    if (item.key.empty()) {
        return false;
    }
    auto& items = ItemsForFolder(folder);
    items.erase(
        std::remove_if(items.begin(), items.end(), [&](const ExplorerItem& existing) {
            return existing.kind == item.kind && existing.key == item.key;
        }),
        items.end());
    index = std::clamp(index, 0, static_cast<int>(items.size()));
    items.insert(items.begin() + index, std::move(item));
    return true;
}

void BinderModule::Impl::AppendExplorerItemIfMissing(FolderNode* folder, ExplorerItem item) {
    const auto& items = ItemsForFolder(folder);
    const bool exists = std::any_of(items.begin(), items.end(), [&](const ExplorerItem& existing) {
        return existing.kind == item.kind && existing.key == item.key;
    });
    if (!exists) {
        InsertExplorerItem(folder, std::move(item), static_cast<int>(items.size()));
    }
}

bool BinderModule::Impl::MoveBindToExplorerDirectory(
    const int hotkeyIndex,
    FolderNode* targetFolder,
    std::optional<int> insertIndex,
    std::string_view source) {
    if (hotkeyIndex < 0 || hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        return false;
    }

    HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(hotkeyIndex)];
    const std::string orderId = hotkey.orderId.empty() ? AllocateHotkeyOrderId() : hotkey.orderId;
    hotkey.orderId = orderId;
    auto& targetItems = ItemsForFolder(targetFolder);
    if (insertIndex) {
        const auto existingIt = std::find_if(targetItems.begin(), targetItems.end(), [&](const ExplorerItem& item) {
            return item.kind == ExplorerItemKind::Bind && item.key == orderId;
        });
        if (existingIt != targetItems.end()) {
            const int oldIndex = static_cast<int>(std::distance(targetItems.begin(), existingIt));
            if (oldIndex < *insertIndex) {
                *insertIndex -= 1;
            }
        }
    }

    RemoveBindFromExplorerOrders(orderId);
    hotkey.categoryId = ActiveCategory().id;
    hotkey.folderPath = FolderPathForDirectory(targetFolder);
    InsertExplorerItem(
        targetFolder,
        ExplorerItem{ ExplorerItemKind::Bind, orderId },
        insertIndex.value_or(static_cast<int>(targetItems.size())));
    if (!StartsWith(source, "explorer_")) {
        currentFolder = targetFolder;
    }
    if (currentFolder == targetFolder) {
        SelectExplorerBind(hotkeyIndex, true);
    } else {
        SelectExplorerFolder(targetFolder, true);
    }

    debuglog::WriteInfo(
        "[binder] bind moved index=%d source=%.*s folder=%s",
        hotkeyIndex,
        static_cast<int>(source.size()),
        source.data(),
        JoinPath(hotkey.folderPath).c_str());
    SaveConfig();
    return true;
}

bool BinderModule::Impl::MoveBindToCategoryRoot(
    const int hotkeyIndex,
    std::string_view targetCategoryId,
    std::string_view source) {
    if (hotkeyIndex < 0 || hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        return false;
    }

    BinderCategory* targetCategory = FindCategoryById(targetCategoryId);
    if (!targetCategory) {
        return false;
    }
    if (targetCategory->id == ActiveCategory().id) {
        return MoveBindToExplorerDirectory(hotkeyIndex, nullptr, std::nullopt, source);
    }

    HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(hotkeyIndex)];
    const std::string orderId = hotkey.orderId.empty() ? AllocateHotkeyOrderId() : hotkey.orderId;
    hotkey.orderId = orderId;

    RemoveBindFromExplorerOrders(orderId);
    hotkey.categoryId = targetCategory->id;
    hotkey.folderPath.clear();
    targetCategory->rootItems.push_back(ExplorerItem{ ExplorerItemKind::Bind, orderId });
    targetCategory->lastOpenFolderPath.clear();
    ClearExplorerSelection();

    debuglog::WriteInfo(
        "[binder] bind moved index=%d source=%.*s category=%s folder=<root>",
        hotkeyIndex,
        static_cast<int>(source.size()),
        source.data(),
        targetCategory->id.c_str());
    SaveConfig();
    return true;
}

bool BinderModule::Impl::CanMoveFolderToExplorerDirectory(
    const int folderId,
    FolderNode* targetFolder,
    const int insertIndex,
    bool* noop) {
    if (noop) {
        *noop = false;
    }

    FolderListPos from;
    if (!FindFolderListPos(ActiveFolders(), nullptr, folderId, from)) {
        return false;
    }
    FolderNode* moving = (*from.list)[static_cast<std::size_t>(from.index)].get();
    if (!moving) {
        return false;
    }

    const std::vector<ExplorerItem>& targetOrder = ItemsForFolder(targetFolder);
    const auto folderVectorIndexForOrder = [&](int orderIndex) {
        int folderIndex = 0;
        for (int i = 0; i < std::min(orderIndex, static_cast<int>(targetOrder.size())); ++i) {
            const ExplorerItem& item = targetOrder[static_cast<std::size_t>(i)];
            if (item.kind == ExplorerItemKind::Folder) {
                ++folderIndex;
            }
        }
        return folderIndex;
    };

    FolderListPos dest{};
    dest.list = targetFolder ? &targetFolder->children : &ActiveFolders();
    dest.listParent = targetFolder;
    dest.index = folderVectorIndexForOrder(insertIndex);
    return IsValidFolderDropTarget(folderId, dest, noop);
}

bool BinderModule::Impl::MoveFolderToExplorerDirectory(
    const int folderId,
    FolderNode* targetFolder,
    int insertIndex,
    std::string_view source,
    const bool recordUndo) {
    FolderListPos from;
    if (!FindFolderListPos(ActiveFolders(), nullptr, folderId, from)) {
        return false;
    }
    FolderNode* moving = (*from.list)[static_cast<std::size_t>(from.index)].get();
    if (!moving) {
        return false;
    }

    const std::vector<ExplorerItem>& targetOrder = ItemsForFolder(targetFolder);
    const auto folderVectorIndexForOrder = [&](int orderIndex) {
        int folderIndex = 0;
        for (int i = 0; i < std::min(orderIndex, static_cast<int>(targetOrder.size())); ++i) {
            const ExplorerItem& item = targetOrder[static_cast<std::size_t>(i)];
            if (item.kind == ExplorerItemKind::Folder) {
                ++folderIndex;
            }
        }
        return folderIndex;
    };

    FolderListPos dest{};
    dest.list = targetFolder ? &targetFolder->children : &ActiveFolders();
    dest.listParent = targetFolder;
    dest.index = folderVectorIndexForOrder(insertIndex);

    bool noop = false;
    if (!IsValidFolderDropTarget(folderId, dest, &noop)) {
        debuglog::WriteError(
            "[binder] explorer folder move rejected id=%d source=%.*s",
            folderId,
            static_cast<int>(source.size()),
            source.data());
        Notify(
            NotificationGroup::Validation,
            NotificationSeverity::Error,
            UiSettings::Instance().Text(UiText::ToastFolderMoveInvalid),
            2200.0);
        return false;
    }

    FolderNode* oldParent = moving->parent;
    const std::string folderName = moving->name;
    auto& targetItems = ItemsForFolder(targetFolder);
    if (oldParent == targetFolder) {
        const auto oldOrderIt = std::find_if(targetItems.begin(), targetItems.end(), [&](const ExplorerItem& item) {
            return item.kind == ExplorerItemKind::Folder && item.key == folderName;
        });
        if (oldOrderIt != targetItems.end()) {
            const int oldOrderIndex = static_cast<int>(std::distance(targetItems.begin(), oldOrderIt));
            if (oldOrderIndex < insertIndex) {
                --insertIndex;
            }
        }
    }

    if (noop) {
        RemoveFolderFromParentOrder(moving);
        InsertExplorerItem(targetFolder, ExplorerItem{ ExplorerItemKind::Folder, folderName }, insertIndex);
        SelectExplorerFolder(moving, true);
        SaveConfig();
        return true;
    }

    RemoveFolderFromParentOrder(moving);
    if (!RelocateFolderNode(folderId, dest, recordUndo)) {
        AppendExplorerItemIfMissing(oldParent, ExplorerItem{ ExplorerItemKind::Folder, folderName });
        return false;
    }

    InsertExplorerItem(targetFolder, ExplorerItem{ ExplorerItemKind::Folder, folderName }, insertIndex);
    if (!StartsWith(source, "explorer_")) {
        currentFolder = targetFolder;
    }
    if (currentFolder == targetFolder) {
        SelectExplorerFolder(moving, true);
    } else {
        SelectExplorerFolder(targetFolder, true);
    }
    debuglog::WriteInfo(
        "[binder] explorer folder moved id=%d source=%.*s",
        folderId,
        static_cast<int>(source.size()),
        source.data());
    SaveConfig();
    return true;
}

bool BinderModule::Impl::MoveFolderToCategoryRoot(
    const int folderId,
    std::string_view targetCategoryId,
    std::string_view source) {
    BinderCategory& sourceCategory = ActiveCategory();
    BinderCategory* targetCategory = FindCategoryById(targetCategoryId);
    if (!targetCategory) {
        return false;
    }
    if (targetCategory->id == sourceCategory.id) {
        return MoveFolderToExplorerDirectory(
            folderId,
            nullptr,
            static_cast<int>(sourceCategory.rootItems.size()),
            source,
            true);
    }

    FolderListPos from;
    if (!FindFolderListPos(sourceCategory.folders, nullptr, folderId, from)) {
        return false;
    }

    FolderNode* moving = (*from.list)[static_cast<std::size_t>(from.index)].get();
    if (!moving) {
        return false;
    }

    const std::vector<std::string> oldPath = BuildFolderPath(moving);
    const std::string baseName = moving->name;
    std::string newName = baseName;
    for (int suffix = 2; !FolderNameUnique(targetCategory->folders, newName, nullptr); ++suffix) {
        newName = baseName + " " + std::to_string(suffix);
    }

    RemoveFolderFromParentOrder(moving);
    std::unique_ptr<FolderNode> extracted = std::move((*from.list)[static_cast<std::size_t>(from.index)]);
    from.list->erase(from.list->begin() + from.index);
    extracted->name = newName;
    extracted->parent = nullptr;

    targetCategory->folders.push_back(std::move(extracted));
    targetCategory->rootItems.push_back(ExplorerItem{ ExplorerItemKind::Folder, newName });
    targetCategory->lastOpenFolderPath.clear();

    const std::vector<std::string> newPath{ newName };
    for (HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.categoryId == sourceCategory.id && PathStartsWith(hotkey.folderPath, oldPath)) {
            hotkey.categoryId = targetCategory->id;
            hotkey.folderPath = ReplacePathPrefix(hotkey.folderPath, oldPath, newPath);
        }
    }

    folderMoveUndo_.reset();
    ClearExplorerSelection();

    debuglog::WriteInfo(
        "[binder] folder moved id=%d source=%.*s category=%s oldPath=%s newPath=%s",
        folderId,
        static_cast<int>(source.size()),
        source.data(),
        targetCategory->id.c_str(),
        JoinPath(oldPath).c_str(),
        JoinPath(newPath).c_str());
    SaveConfig();
    return true;
}

bool BinderModule::Impl::MoveFolderToCategoryDirectory(
    const int folderId,
    std::string_view targetCategoryId,
    const std::vector<std::string>& targetFolderPath,
    std::string_view source) {
    BinderCategory& sourceCategory = ActiveCategory();
    BinderCategory* targetCategory = FindCategoryById(targetCategoryId);
    if (!targetCategory) {
        return false;
    }

    if (targetCategory->id == sourceCategory.id) {
        FolderNode* targetFolder = nullptr;
        if (!targetFolderPath.empty()) {
            targetFolder = FindFolderByPath(ActiveFolders(), targetFolderPath);
            if (!targetFolder) {
                return false;
            }
        }
        const int insertIndex = static_cast<int>(ItemsForFolder(targetFolder).size());
        return MoveFolderToExplorerDirectory(folderId, targetFolder, insertIndex, source, true);
    }

    FolderListPos from;
    if (!FindFolderListPos(sourceCategory.folders, nullptr, folderId, from)) {
        return false;
    }

    FolderNode* moving = (*from.list)[static_cast<std::size_t>(from.index)].get();
    if (!moving) {
        return false;
    }

    FolderNode* targetFolder = nullptr;
    if (!targetFolderPath.empty()) {
        targetFolder = FindFolderByPath(targetCategory->folders, targetFolderPath);
        if (!targetFolder) {
            return false;
        }
    }

    CancelInlineFolderEdit();
    const std::vector<std::string> oldPath = BuildFolderPath(moving);
    const std::string baseName = moving->name;
    auto& targetChildren = targetFolder ? targetFolder->children : targetCategory->folders;
    std::string newName = baseName;
    for (int suffix = 2; !FolderNameUnique(targetChildren, newName, nullptr); ++suffix) {
        newName = baseName + " " + std::to_string(suffix);
    }

    RemoveFolderFromParentOrder(moving);
    std::unique_ptr<FolderNode> extracted = std::move((*from.list)[static_cast<std::size_t>(from.index)]);
    from.list->erase(from.list->begin() + from.index);
    extracted->name = newName;
    extracted->parent = targetFolder;
    FolderNode* movedFolder = extracted.get();
    targetChildren.push_back(std::move(extracted));

    auto& targetItems = targetFolder ? targetFolder->items : targetCategory->rootItems;
    targetItems.push_back(ExplorerItem{ ExplorerItemKind::Folder, newName });

    std::vector<std::string> newPath = targetFolderPath;
    newPath.push_back(newName);
    for (HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.categoryId == sourceCategory.id && PathStartsWith(hotkey.folderPath, oldPath)) {
            hotkey.categoryId = targetCategory->id;
            hotkey.folderPath = ReplacePathPrefix(hotkey.folderPath, oldPath, newPath);
        }
    }

    folderMoveUndo_.reset();
    activeCategoryId = targetCategory->id;
    categoryTabSelectionTargetId = activeCategoryId;
    currentFolder = targetFolder;
    targetCategory->lastOpenFolderPath = targetFolderPath;
    SelectExplorerFolder(movedFolder, true);
    ExpandFolderBranch(movedFolder);

    debuglog::WriteInfo(
        "[binder] folder moved id=%d source=%.*s category=%s oldPath=%s newPath=%s",
        folderId,
        static_cast<int>(source.size()),
        source.data(),
        targetCategory->id.c_str(),
        JoinPath(oldPath).c_str(),
        JoinPath(newPath).c_str());
    SaveConfig();
    return true;
}

ExplorerDirectoryDropStatus BinderModule::Impl::CanDropExplorerPayloadToDirectory(
    const ImGuiPayload* payload,
    FolderNode* targetFolder,
    const bool rejectNoop) {
    if (!payload) {
        return ExplorerDirectoryDropStatus::None;
    }

    if (IsExplorerBindDragPayload(payload)) {
        const int hotkeyIndex = *static_cast<const int*>(payload->Data);
        if (hotkeyIndex < 0 || hotkeyIndex >= static_cast<int>(hotkeys.size())) {
            return ExplorerDirectoryDropStatus::Invalid;
        }

        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(hotkeyIndex)];
        if (hotkey.categoryId != ActiveCategory().id) {
            return ExplorerDirectoryDropStatus::Invalid;
        }
        if (rejectNoop && hotkey.folderPath == FolderPathForDirectory(targetFolder)) {
            return ExplorerDirectoryDropStatus::Noop;
        }
        return ExplorerDirectoryDropStatus::Accept;
    }

    int folderId = 0;
    if (!TryGetExplorerFolderDragId(payload, folderId)) {
        return ExplorerDirectoryDropStatus::None;
    }

    FolderListPos from{};
    if (!FindFolderListPos(ActiveFolders(), nullptr, folderId, from)) {
        return ExplorerDirectoryDropStatus::Invalid;
    }
    FolderNode* moving = (*from.list)[static_cast<std::size_t>(from.index)].get();
    if (!moving || IsUnderOrEqual(moving, targetFolder)) {
        return ExplorerDirectoryDropStatus::Invalid;
    }
    if (rejectNoop && moving->parent == targetFolder) {
        return ExplorerDirectoryDropStatus::Noop;
    }

    bool noop = false;
    const bool valid = CanMoveFolderToExplorerDirectory(
        folderId,
        targetFolder,
        static_cast<int>(ItemsForFolder(targetFolder).size()),
        &noop);
    if (!valid) {
        return ExplorerDirectoryDropStatus::Invalid;
    }
    if (rejectNoop && noop) {
        return ExplorerDirectoryDropStatus::Noop;
    }
    return ExplorerDirectoryDropStatus::Accept;
}

bool BinderModule::Impl::DropExplorerPayloadToDirectory(
    const ImGuiPayload* payload,
    FolderNode* targetFolder,
    std::string_view source) {
    if (!payload) {
        return false;
    }

    bool moved = false;
    if (IsExplorerBindDragPayload(payload)) {
        moved = MoveBindToExplorerDirectory(
            *static_cast<const int*>(payload->Data),
            targetFolder,
            std::nullopt,
            source);
    } else {
        int folderId = 0;
        if (!TryGetExplorerFolderDragId(payload, folderId)) {
            return false;
        }
        moved = MoveFolderToExplorerDirectory(
            folderId,
            targetFolder,
            static_cast<int>(ItemsForFolder(targetFolder).size()),
            source,
            true);
    }

    if (moved && (StartsWith(source, "explorer_breadcrumb") || StartsWith(source, "explorer_search"))) {
        ClearExplorerSelection();
    }
    return moved;
}

bool BinderModule::Impl::DropExplorerPayloadToTwoPaneDirectory(
    const ImGuiPayload* payload,
    FolderNode* targetFolder,
    std::optional<int> insertIndex,
    std::string_view source) {
    if (!payload) {
        return false;
    }

    FolderNode* preservedFolder = currentFolder;
    const ExplorerSelection preservedSelection = explorerSelection;
    FolderNode* preservedSelectedFolder = selectedFolder;
    const int preservedSelectedBindIndex = selectedBindIndex;

    bool moved = false;
    if (IsExplorerBindDragPayload(payload)) {
        moved = MoveBindToExplorerDirectory(
            *static_cast<const int*>(payload->Data),
            targetFolder,
            insertIndex,
            source);
    } else {
        int folderId = 0;
        if (!TryGetExplorerFolderDragId(payload, folderId)) {
            return false;
        }
        moved = MoveFolderToExplorerDirectory(
            folderId,
            targetFolder,
            insertIndex.value_or(static_cast<int>(ItemsForFolder(targetFolder).size())),
            source,
            true);
    }

    if (!moved) {
        return false;
    }

    currentFolder = preservedFolder;
    if (IsExplorerSelectionVisibleInFolder(preservedSelection, currentFolder)) {
        explorerSelection = preservedSelection;
        selectedFolder = preservedSelectedFolder;
        selectedBindIndex = preservedSelectedBindIndex;
    } else {
        ClearExplorerSelection();
    }
    explorerSelectionScrollPending = false;
    ActiveCategory().lastOpenFolderPath = CurrentFolderPath();
    SaveConfig();
    return true;
}

ExplorerDirectoryDropStatus BinderModule::Impl::DrawExplorerDirectoryDropTarget(
    const ImRect& rect,
    FolderNode* targetFolder,
    std::string_view source,
    const bool activeCategoryTarget,
    const bool rejectCurrentFolder,
    const bool rejectNoop) {
    const ImGuiPayload* activePayload = ActiveExplorerDragPayload();
    if (!activePayload) {
        return ExplorerDirectoryDropStatus::None;
    }

    ExplorerDirectoryDropStatus status = ExplorerDirectoryDropStatus::None;
    if (!activeCategoryTarget) {
        status = ExplorerDirectoryDropStatus::Invalid;
    } else if (rejectCurrentFolder && targetFolder == currentFolder) {
        status = ExplorerDirectoryDropStatus::Noop;
    } else {
        status = CanDropExplorerPayloadToDirectory(activePayload, targetFolder, rejectNoop);
    }

    const bool hovered = ImGui::IsMouseHoveringRect(rect.Min, rect.Max, true);
    if (hovered && status != ExplorerDirectoryDropStatus::None) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
        ImVec4 fill = ImGui::ColorConvertU32ToFloat4(borderColor);
        fill.w = std::min(0.32f, fill.w * 0.22f);
        if (status != ExplorerDirectoryDropStatus::Accept) {
            borderColor = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.82f);
            fill = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
            fill.w = 0.42f;
        }
        drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(fill), ScaleUi(4.0f));
        drawList->AddRect(rect.Min, rect.Max, borderColor, ScaleUi(4.0f), 0, ScaleUi(1.5f));

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            UiSettings& ui = UiSettings::Instance();
            const char* tooltip = ui.Text(UiText::BinderDropMoveHere);
            if (!activeCategoryTarget) {
                tooltip = ui.Text(UiText::BinderDropCrossCategoryDisabled);
            } else if (status == ExplorerDirectoryDropStatus::Noop) {
                tooltip = ui.Text(UiText::BinderDropCurrentFolder);
            } else if (status == ExplorerDirectoryDropStatus::Invalid) {
                tooltip = ui.Text(UiText::ToastFolderMoveInvalid);
            }
            ImGui::SetTooltip("%s", tooltip);
        }
    }

    if (!activeCategoryTarget || (rejectCurrentFolder && targetFolder == currentFolder)) {
        return status;
    }

    if (ImGui::BeginDragDropTarget()) {
        const ExplorerDirectoryDropStatus targetStatus =
            CanDropExplorerPayloadToDirectory(activePayload, targetFolder, rejectNoop);
        if (targetStatus == ExplorerDirectoryDropStatus::Accept) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(kBindDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->IsDelivery()) {
                    DropExplorerPayloadToDirectory(payload, targetFolder, source);
                }
            }
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(kFolderDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->IsDelivery()) {
                    DropExplorerPayloadToDirectory(payload, targetFolder, source);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    return status;
}

int BinderModule::Impl::RemapHotkeysFolderPrefix(
    const std::vector<std::string>& oldPath,
    const std::vector<std::string>& newPath) {
    int changed = 0;
    const std::string categoryId = ActiveCategory().id;
    for (HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.categoryId != categoryId || !PathStartsWith(hotkey.folderPath, oldPath)) {
            continue;
        }
        hotkey.folderPath = ReplacePathPrefix(hotkey.folderPath, oldPath, newPath);
        ++changed;
    }
    return changed;
}

int BinderModule::Impl::MoveHotkeysFromFolderPath(
    const std::vector<std::string>& fromPath,
    const std::vector<std::string>& toPath) {
    int changed = 0;
    const std::string categoryId = ActiveCategory().id;
    for (HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.categoryId != categoryId || !PathStartsWith(hotkey.folderPath, fromPath)) {
            continue;
        }
        hotkey.folderPath = toPath;
        ++changed;
    }
    return changed;
}

int BinderModule::Impl::DeleteHotkeysFromFolderPath(const std::vector<std::string>& fromPath) {
    int removed = 0;
    const std::string categoryId = ActiveCategory().id;
    std::vector<std::uint64_t> runtimeIdsToStop;
    runtimeIdsToStop.reserve(hotkeys.size());
    for (const HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.categoryId == categoryId && PathStartsWith(hotkey.folderPath, fromPath)) {
            runtimeIdsToStop.push_back(hotkey.runtimeId);
        }
    }
    for (const std::uint64_t runtimeId : runtimeIdsToStop) {
        StopHotkeyByRuntimeId(runtimeId);
    }
    for (const HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.categoryId == categoryId && PathStartsWith(hotkey.folderPath, fromPath)) {
            RemoveBindFromExplorerOrders(hotkey.orderId);
        }
    }

    hotkeys.erase(
        std::remove_if(hotkeys.begin(), hotkeys.end(), [&](const HotkeyEntry& hotkey) {
            if (hotkey.categoryId != categoryId || !PathStartsWith(hotkey.folderPath, fromPath)) {
                return false;
            }
            ++removed;
            return true;
        }),
        hotkeys.end());

    if (removed > 0) {
        RefreshNumbers();
        if (explorerSelection.kind == ExplorerSelectionKind::Bind) {
            ClearExplorerSelection();
        }
        bindDeleteTarget = -1;
        moveBindTarget = -1;
        bindLinesTarget = -1;
    }
    return removed;
}

void BinderModule::Impl::MoveBindToFolderPath(
    const int hotkeyIndex,
    const std::vector<std::string>& folderPath,
    std::string_view source) {
    if (hotkeyIndex < 0 || hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        debuglog::WriteError(
            "[binder] bind move rejected index=%d source=%.*s reason=out_of_range",
            hotkeyIndex,
            static_cast<int>(source.size()),
            source.data());
        return;
    }

    FolderNode* targetFolder = nullptr;
    const std::string folderPathText = folderPath.empty() ? std::string("<unfiled>") : JoinPath(folderPath);
    if (!folderPath.empty()) {
        targetFolder = FindFolderByPath(ActiveFolders(), folderPath);
        if (!targetFolder) {
            debuglog::WriteError(
                "[binder] bind move rejected index=%d source=%.*s reason=folder_not_found path=%s",
                hotkeyIndex,
                static_cast<int>(source.size()),
                source.data(),
                folderPathText.c_str());
            return;
        }
    }

    (void)MoveBindToExplorerDirectory(hotkeyIndex, targetFolder, std::nullopt, source);
}

std::string BinderModule::Impl::NextFolderNameForParent(FolderNode* parent) const {
    const auto& siblings = parent ? parent->children : ActiveFolders();
    const std::string baseName = UiSettings::Instance().Text(UiText::BinderNewFolder);
    std::string name = baseName;
    for (int suffix = 2; !FolderNameUnique(siblings, name); ++suffix) {
        name = baseName + " " + std::to_string(suffix);
    }
    return name;
}

void BinderModule::Impl::BeginInlineCreateFolder(FolderNode* parent) {
    bindSearch.clear();
    twoPaneFolderSearch.clear();
    if (ExpandFolderBranch(parent)) {
        QueueFolderOpenStateSave();
    }
    folderInlineEdit = {};
    folderInlineEdit.mode = FolderInlineEditMode::Create;
    folderInlineEdit.parent = parent;
    folderInlineEdit.name = NextFolderNameForParent(parent);
    folderInlineEdit.focusPending = true;
    ClearExplorerSelection();
}

void BinderModule::Impl::BeginInlineRenameFolder(FolderNode* folder) {
    if (!folder) {
        return;
    }

    bindSearch.clear();
    twoPaneFolderSearch.clear();
    if (ExpandFolderBranch(folder->parent)) {
        QueueFolderOpenStateSave();
    }
    folderInlineEdit = {};
    folderInlineEdit.mode = FolderInlineEditMode::Rename;
    folderInlineEdit.target = folder;
    folderInlineEdit.parent = folder->parent;
    folderInlineEdit.name = folder->name;
    folderInlineEdit.focusPending = true;
    SelectExplorerFolder(folder);
}

void BinderModule::Impl::CancelInlineFolderEdit() {
    folderInlineEdit = {};
}

bool BinderModule::Impl::IsInlineRenamingFolder(const FolderNode* folder) const {
    return folderInlineEdit.mode == FolderInlineEditMode::Rename && folderInlineEdit.target == folder;
}

bool BinderModule::Impl::CommitInlineFolderEdit() {
    const std::string name = SanitizeFolderName(folderInlineEdit.name);
    if (name.empty()) {
        Notify(
            NotificationGroup::Validation,
            NotificationSeverity::Error,
            UiSettings::Instance().Text(UiText::ValidationFolderNameRequired),
            2200.0);
        folderInlineEdit.focusPending = true;
        return false;
    }

    if (binder_tags::IsReservedFolderName(name)) {
        Notify(
            NotificationGroup::Validation,
            NotificationSeverity::Error,
            UiSettings::Instance().Text(UiText::ValidationFolderNameReserved),
            2600.0);
        folderInlineEdit.focusPending = true;
        return false;
    }

    if (folderInlineEdit.mode == FolderInlineEditMode::Rename) {
        FolderNode* target = folderInlineEdit.target;
        if (!target) {
            CancelInlineFolderEdit();
            return false;
        }

        auto& siblings = target->parent ? target->parent->children : ActiveFolders();
        if (!FolderNameUnique(siblings, name, target)) {
            Notify(
                NotificationGroup::Validation,
                NotificationSeverity::Error,
                UiSettings::Instance().Text(UiText::ValidationFolderNameUnique),
                2200.0);
            folderInlineEdit.focusPending = true;
            return false;
        }

        if (target->name != name) {
            const std::vector<std::string> oldPath = BuildFolderPath(target);
            const std::string oldName = target->name;
            target->name = name;
            const std::vector<std::string> newPath = BuildFolderPath(target);
            RemapHotkeysFolderPrefix(oldPath, newPath);
            auto& order = ItemsForFolder(target->parent);
            for (ExplorerItem& item : order) {
                if (item.kind == ExplorerItemKind::Folder && item.key == oldName) {
                    item.key = name;
                    break;
                }
            }
        }

        SelectExplorerFolder(target);
        CancelInlineFolderEdit();
        SaveConfig();
        return true;
    }

    if (folderInlineEdit.mode == FolderInlineEditMode::Create) {
        FolderNode* parent = folderInlineEdit.parent;
        auto& siblings = parent ? parent->children : ActiveFolders();
        if (!FolderNameUnique(siblings, name)) {
            Notify(
                NotificationGroup::Validation,
                NotificationSeverity::Error,
                UiSettings::Instance().Text(UiText::ValidationFolderNameUnique),
                2200.0);
            folderInlineEdit.focusPending = true;
            return false;
        }

        auto folder = std::make_unique<FolderNode>();
        folder->id = nextFolderId++;
        folder->name = name;
        folder->conditions.assign(static_cast<std::size_t>(ConditionId::Count), false);
        folder->parent = parent;
        FolderNode* created = folder.get();
        if (parent) {
            parent->children.push_back(std::move(folder));
        } else {
            ActiveFolders().push_back(std::move(folder));
        }
        AppendExplorerItemIfMissing(parent, ExplorerItem{ ExplorerItemKind::Folder, created->name });

        currentFolder = parent;
        SelectExplorerFolder(created, true);
        CancelInlineFolderEdit();
        SaveConfig();
        return true;
    }

    CancelInlineFolderEdit();
    return false;
}

bool BinderModule::Impl::CanDeleteFolder(const FolderNode* folder) const {
    if (!folder) {
        return false;
    }
    return true;
}

void BinderModule::Impl::DuplicateHotkeyAt(int index) {
    if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
        return;
    }

    HotkeyEntry duplicated = hotkeys[static_cast<std::size_t>(index)];
    duplicated.comboActive = false;
    duplicated.awaitingInput = false;
    duplicated.waitingTextConfirmation = false;
    duplicated.lastRepeatPressed.clear();
    duplicated.pendingConfirmationKey = kDefaultConfirmKey;
    duplicated.pendingConfirmationCancelKey = kDefaultCancelKey;
    duplicated.pendingTriggerText.clear();
    duplicated.pendingTriggerSource.clear();
    duplicated.pendingTriggerCaptures = {};
    duplicated.lastActivatedAtMs = 0.0;
    duplicated.debounceUntilMs = 0.0;
    duplicated.textConfirmationDeadlineMs = 0.0;
    duplicated.runtimeId = AllocateHotkeyRuntimeId();
    duplicated.orderId = AllocateHotkeyOrderId();

    hotkeys.insert(hotkeys.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(duplicated));
    RefreshNumbers();
    selectedBindIndex = index + 1;
    BinderCategory* category = FindCategoryById(hotkeys[static_cast<std::size_t>(selectedBindIndex)].categoryId);
    if (!category) {
        category = &ActiveCategory();
        hotkeys[static_cast<std::size_t>(selectedBindIndex)].categoryId = category->id;
    }
    FolderNode* folder = hotkeys[static_cast<std::size_t>(selectedBindIndex)].folderPath.empty()
        ? nullptr
        : FindFolderByPath(category->folders, hotkeys[static_cast<std::size_t>(selectedBindIndex)].folderPath);
    if (category->id != ActiveCategory().id) {
        SelectCategory(category->id);
    }
    SelectExplorerBind(selectedBindIndex, true);
    auto& items = ItemsForFolder(folder);
    int insertIndex = static_cast<int>(items.size());
    const std::string originalOrderId = hotkeys[static_cast<std::size_t>(index)].orderId;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].kind == ExplorerItemKind::Bind && items[i].key == originalOrderId) {
            insertIndex = static_cast<int>(i + 1);
            break;
        }
    }
    InsertExplorerItem(
        folder,
        ExplorerItem{ ExplorerItemKind::Bind, hotkeys[static_cast<std::size_t>(selectedBindIndex)].orderId },
        insertIndex);
    SaveConfig();
}

bool BinderModule::Impl::IsValidFolderDropTarget(int moveId, const FolderListPos& dest, bool* noop) {
    if (noop) {
        *noop = false;
    }

    FolderListPos from{};
    if (!FindFolderListPos(ActiveFolders(), nullptr, moveId, from) || !dest.list) {
        return false;
    }

    FolderNode* node = (*from.list)[static_cast<std::size_t>(from.index)].get();
    if (dest.list == &node->children) {
        return false;
    }
    if (from.list != dest.list && !FolderNameUnique(*dest.list, node->name, nullptr)) {
        return false;
    }

    FolderNode* const destListOwner = FindListOwner(ActiveFolders(), dest.list);
    if (destListOwner && node != destListOwner && IsUnderOrEqual(node, destListOwner)) {
        return false;
    }

    if (from.list == dest.list && (dest.index == from.index || dest.index == from.index + 1)) {
        if (noop) {
            *noop = true;
        }
        return true;
    }
    return true;
}

bool BinderModule::Impl::RelocateFolderNode(int moveId, FolderListPos dest, bool recordUndo) {
    FolderListPos from;
    auto& activeFolders = ActiveFolders();
    if (!FindFolderListPos(activeFolders, nullptr, moveId, from)) {
        return false;
    }
    if (!dest.list) {
        return false;
    }
    FolderNode* node = (*from.list)[static_cast<std::size_t>(from.index)].get();
    if (dest.list == &node->children) {
        return false;
    }
    if (from.list != dest.list) {
        if (!FolderNameUnique(*dest.list, node->name, nullptr)) {
            return false;
        }
    }
    {
        FolderNode* const destListOwner = FindListOwner(activeFolders, dest.list);
        if (destListOwner) {
            if (node != destListOwner && IsUnderOrEqual(node, destListOwner)) {
                return false;
            }
        }
    }
    if (from.list == dest.list && from.index == dest.index) {
        return false;
    }

    const std::vector<std::string> oldPath = BuildFolderPath(node);
    const int savedListParentId =
        from.list == &activeFolders ? -1 : (from.listParent != nullptr ? from.listParent->id : -1);
    const int savedListIndex = from.index;

    int dix = dest.index;
    if (from.list == dest.list && from.index < dix) {
        dix -= 1;
    }

    std::unique_ptr<FolderNode> extracted = std::move((*from.list)[static_cast<std::size_t>(from.index)]);
    from.list->erase(from.list->begin() + from.index);
    {
        const int destSz = static_cast<int>(dest.list->size());
        if (dix < 0) {
            dix = 0;
        }
        if (dix > destSz) {
            dix = destSz;
        }
    }

    FolderNode* newParent = FindListOwner(activeFolders, dest.list);
    if (dest.list == &activeFolders) {
        newParent = nullptr;
    }
    node->parent = newParent;
    dest.list->insert(dest.list->begin() + dix, std::move(extracted));

    const std::vector<std::string> newPath = BuildFolderPath(node);
    if (oldPath != newPath) {
        RemapHotkeysFolderPrefix(oldPath, newPath);
    }
    if (recordUndo) {
        folderMoveUndo_ = FolderMoveUndo{node->id, savedListParentId, savedListIndex};
    } else {
        folderMoveUndo_.reset();
    }
    debuglog::WriteInfo(
        "[binder] folder moved id=%d oldParent=%d oldIndex=%d newParent=%d newIndex=%d",
        node->id,
        savedListParentId,
        savedListIndex,
        newParent ? newParent->id : -1,
        dix);
    ExpandFolderBranch(node);
    SaveConfig();
    return true;
}

void BinderModule::Impl::ApplyFolderMoveUndo() {
    if (!folderMoveUndo_) {
        return;
    }
    const int moveId = folderMoveUndo_->nodeId;
    const int oldP = folderMoveUndo_->oldListParentId;
    const int oldIdx = folderMoveUndo_->oldIndex;
    folderMoveUndo_.reset();
    FolderListPos d{};
    if (oldP < 0) {
        d.list = &ActiveFolders();
        d.index = oldIdx;
        d.listParent = nullptr;
    } else {
        FolderNode* p = FindFolderByIdR(ActiveFolders(), oldP);
        if (!p) {
            return;
        }
        d.list = &p->children;
        d.index = oldIdx;
        d.listParent = p;
    }
    if (!RelocateFolderNode(moveId, d, false)) {
        Notify(
            NotificationGroup::Validation,
            NotificationSeverity::Error,
            UiSettings::Instance().Text(UiText::ToastFolderMoveInvalid),
            2000.0);
    }
}
