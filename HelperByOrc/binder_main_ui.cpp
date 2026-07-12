#include "binder_module_impl.h"

void BinderModule::Impl::DrawCategoryTabs() {
    EnsureCategories();
    UiSettings& ui = UiSettings::Instance();
    struct PendingCategoryMove {
        std::string categoryId;
        int offset = 0;
    };
    std::optional<PendingCategoryMove> pendingMove;

    const std::string tabBarId = "##binder_category_tabs_" + std::to_string(categoryTabOrderRevision);
    if (!ImGui::BeginTabBar(tabBarId.c_str(), ImGuiTabBarFlags_FittingPolicyScroll)) {
        return;
    }

    for (std::size_t i = 0; i < categories.size(); ++i) {
        BinderCategory& category = categories[i];
        const std::string categoryId = category.id;
        const bool active = category.id == activeCategoryId;
        const ImGuiTabItemFlags flags = categoryTabSelectionTargetId == category.id
            ? ImGuiTabItemFlags_SetSelected
            : 0;
        const std::string label = category.name + "##binder_category_" + category.id;
        const bool tabOpen = ImGui::BeginTabItem(label.c_str(), nullptr, flags);
        if (tabOpen) {
            if (!active) {
                SelectCategory(category.id);
            }
            if (categoryTabSelectionTargetId == category.id) {
                categoryTabSelectionTargetId.clear();
            }
        }

        if (ImGui::BeginDragDropTarget()) {
            bool accepted = false;
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(kBindDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                    accepted = true;
                    if (payload->IsDelivery()) {
                        const int hotkeyIndex = *static_cast<const int*>(payload->Data);
                        MoveBindToCategoryRoot(hotkeyIndex, categoryId, "category_tab_drop");
                    }
                }
            }
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(kFolderDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                    accepted = true;
                    if (payload->IsDelivery()) {
                        const int folderId = *static_cast<const int*>(payload->Data);
                        MoveFolderToCategoryRoot(folderId, categoryId, "category_tab_drop");
                    }
                }
            }
            if (accepted) {
                ImGui::SetTooltip("%s", category.name.c_str());
            }
            ImGui::EndDragDropTarget();
        }

        const std::string contextId = "##binder_category_context_" + categoryId;
        if (ImGui::BeginPopupContextItem(contextId.c_str())) {
            if (ImGui::MenuItem(ui.Text(UiText::FolderRename))) {
                BeginRenameCategory(categoryId);
            }
            if (ImGui::MenuItem(ui.Text(UiText::ShowInQuickMenu), nullptr, category.quickMenu)) {
                category.quickMenu = !category.quickMenu;
                SaveConfig();
            }
            if (ImGui::MenuItem(ui.Text(UiText::CategoryQuickMenuConditions))) {
                categoryConditionsTargetId = categoryId;
                categoryConditionsPopupPending = true;
            }
            ImGui::Separator();
            const bool canMoveLeft = i > 0;
            if (!canMoveLeft) {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem(ui.Text(UiText::CategoryMoveLeft)) && canMoveLeft) {
                pendingMove = PendingCategoryMove{ categoryId, -1 };
            }
            if (!canMoveLeft) {
                ImGui::EndDisabled();
            }
            const bool canMoveRight = i + 1 < categories.size();
            if (!canMoveRight) {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem(ui.Text(UiText::CategoryMoveRight)) && canMoveRight) {
                pendingMove = PendingCategoryMove{ categoryId, 1 };
            }
            if (!canMoveRight) {
                ImGui::EndDisabled();
            }
            ImGui::Separator();
            const bool canDelete = categories.size() > 1;
            if (!canDelete) {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem(ui.Text(UiText::Delete)) && canDelete) {
                categoryDeleteTargetId = categoryId;
                categoryDeleteMoveTargetId.clear();
                for (const BinderCategory& target : categories) {
                    if (target.id != categoryId) {
                        categoryDeleteMoveTargetId = target.id;
                        break;
                    }
                }
                categoryDeletePopupPending = true;
            }
            if (!canDelete) {
                ImGui::EndDisabled();
            }
            ImGui::EndPopup();
        }

        if (tabOpen) {
            ImGui::EndTabItem();
        }
    }

    if (ImGui::TabItemButton("+##binder_add_category", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
        BinderCategory category = MakeDefaultCategory();
        category.name = NextCategoryName();
        const std::string id = category.id;
        categories.push_back(std::move(category));
        ++categoryTabOrderRevision;
        SelectCategory(id);
        BeginRenameCategory(id);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", ui.Text(UiText::CategoryAdd));
    }

    ImGui::EndTabBar();
    if (pendingMove) {
        MoveCategoryByOffset(pendingMove->categoryId, pendingMove->offset);
    }
}

void BinderModule::Impl::DrawCategoryPopups() {
    UiSettings& ui = UiSettings::Instance();

    if (categoryConditionsPopupPending) {
        BinderCategory* category = FindCategoryById(categoryConditionsTargetId);
        if (category) {
            if (DrawConditionFlagsPopup(
                    "##binder_category_conditions",
                    categoryConditionsPopupPending,
                    UiText::CategoryConditions,
                    category->conditions,
                    &category->conditionsCombine)) {
                SaveConfig();
            }
            if (!categoryConditionsPopupPending && !ImGui::IsPopupOpen("##binder_category_conditions")) {
                categoryConditionsTargetId.clear();
            }
        } else {
            categoryConditionsPopupPending = false;
            categoryConditionsTargetId.clear();
        }
    }

    if (categoryRenamePopupPending) {
        ImGui::OpenPopup("##binder_category_rename");
        categoryRenamePopupPending = false;
    }
    if (ImGui::BeginPopupModal("##binder_category_rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        BinderCategory* category = FindCategoryById(categoryRenameTargetId);
        if (!category) {
            categoryRenameTargetId.clear();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        ImGui::TextUnformatted(ui.Text(UiText::CategoryRenameTitle));
        ImGui::Separator();
        InputTextString("##binder_category_name", categoryRenameBuffer, ImGuiInputTextFlags_AutoSelectAll, 128);
        bool save = ImGui::Button(ui.Text(UiText::Save));
        ImGui::SameLine();
        const bool cancel = ImGui::Button(ui.Text(UiText::Cancel));
        if (ImGui::Shortcut(ImGuiKey_Enter, ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteOverActive)) {
            save = true;
        }
        if (save) {
            const std::string name = SanitizeFolderName(categoryRenameBuffer);
            if (name.empty()) {
                Notify(NotificationGroup::Validation, NotificationSeverity::Error, ui.Text(UiText::ValidationCategoryNameRequired), 2200.0);
                categoryRenamePopupPending = true;
            } else if (!CategoryNameUnique(name, category)) {
                Notify(NotificationGroup::Validation, NotificationSeverity::Error, ui.Text(UiText::ValidationCategoryNameUnique), 2200.0);
                categoryRenamePopupPending = true;
            } else {
                category->name = name;
                categoryRenameTargetId.clear();
                SaveConfig();
                ImGui::CloseCurrentPopup();
            }
        } else if (cancel) {
            categoryRenameTargetId.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (categoryDeletePopupPending) {
        ImGui::OpenPopup("##binder_category_delete");
        categoryDeletePopupPending = false;
    }
    if (ImGui::BeginPopupModal("##binder_category_delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        BinderCategory* category = FindCategoryById(categoryDeleteTargetId);
        if (!category || categories.size() <= 1) {
            categoryDeleteTargetId.clear();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        ImGui::TextWrapped("%s", ui.Text(UiText::DeleteCategoryQuestion));
        ImGui::TextDisabled("%s", category->name.c_str());
        ImGui::Spacing();
        if (ImGui::BeginCombo(
                ui.Text(UiText::CategoryMoveContentsTarget),
                FindCategoryById(categoryDeleteMoveTargetId) ? FindCategoryById(categoryDeleteMoveTargetId)->name.c_str() : "")) {
            for (const BinderCategory& target : categories) {
                if (target.id == category->id) {
                    continue;
                }
                const bool selected = target.id == categoryDeleteMoveTargetId;
                if (ImGui::Selectable(target.name.c_str(), selected)) {
                    categoryDeleteMoveTargetId = target.id;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button(ui.Text(UiText::Cancel))) {
            categoryDeleteTargetId.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::DeleteCategoryMoveContents))) {
            DeleteCategory(categoryDeleteTargetId, categoryDeleteMoveTargetId, false);
            categoryDeleteTargetId.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::DeleteCategoryAll))) {
            DeleteCategory(categoryDeleteTargetId, {}, true);
            categoryDeleteTargetId.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void BinderModule::Impl::DrawFolderPopups() {
    UiSettings& ui = UiSettings::Instance();

    const std::string iconPickerPopup = std::string(ui.Text(UiText::IconPickerTitle)) + "##binder_folder_icon_picker";
    if (folderIconPopupPending) {
        icon_picker::OpenPopup(iconPickerPopup.c_str());
        folderIconPopupPending = false;
    }
    std::string selectedIconId;
    if (icon_picker::DrawPopup(iconPickerState, icon_picker::Options{ iconPickerPopup.c_str(), ImVec2(560.0f, 460.0f) }, selectedIconId)) {
        if (folderIconTarget) {
            folderIconTarget->iconId = icon_registry::NormalizeIconId(selectedIconId);
            SaveConfig();
        }
    }
    if (!ImGui::IsPopupOpen(iconPickerPopup.c_str())) {
        folderIconTarget = nullptr;
    }

    if (folderConditionsTarget) {
        if (DrawConditionFlagsPopup(
                "##binder_folder_conditions",
                folderConditionsPopupPending,
                UiText::FolderConditions,
                folderConditionsTarget->conditions,
                &folderConditionsTarget->conditionsCombine)) {
            SaveConfig();
        }
        if (!folderConditionsPopupPending && !ImGui::IsPopupOpen("##binder_folder_conditions")) {
            folderConditionsTarget = nullptr;
        }
    } else {
        folderConditionsPopupPending = false;
    }

    if (folderDeletePopupPending) {
        ImGui::OpenPopup("##binder_folder_delete");
        folderDeletePopupPending = false;
    }
    if (ImGui::BeginPopupModal("##binder_folder_delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", UiSettings::Instance().Text(UiText::DeleteFolderMoveBindsQuestion));
        if (folderDeleteTarget) {
            ImGui::TextDisabled("%s", folderDeleteTarget->name.c_str());
        }
        auto removeFolderNode = [&]() {
            auto& siblings = folderDeleteTarget->parent ? folderDeleteTarget->parent->children : ActiveFolders();
            siblings.erase(
                std::remove_if(siblings.begin(), siblings.end(), [&](const std::unique_ptr<FolderNode>& item) {
                    return item.get() == folderDeleteTarget;
                }),
                siblings.end());
        };
        auto finishFolderDelete = [&](FolderNode* fallbackFolder, const std::vector<std::string>& removedPath) {
            const auto selectedPath = selectedFolder ? BuildFolderPath(selectedFolder) : std::vector<std::string>{};
            const auto currentPath = CurrentFolderPath();
            FolderNode* removedParent = folderDeleteTarget ? folderDeleteTarget->parent : nullptr;
            const ExplorerItem removedItem{
                ExplorerItemKind::Folder,
                folderDeleteTarget ? folderDeleteTarget->name : std::string{},
            };
            const std::vector<ExplorerItem> beforeItems = ItemsForFolder(removedParent);
            const bool currentWasInRemovedTree =
                (!currentPath.empty() && PathStartsWith(currentPath, removedPath)) || currentFolder == folderDeleteTarget;
            const bool selectionWasInRemovedTree =
                (!selectedPath.empty() && PathStartsWith(selectedPath, removedPath)) || selectedFolder == folderDeleteTarget;
            if (folderConditionsTarget && IsUnderOrEqual(folderDeleteTarget, folderConditionsTarget)) {
                folderConditionsTarget = nullptr;
                folderConditionsPopupPending = false;
            }
            if (folderIconTarget && IsUnderOrEqual(folderDeleteTarget, folderIconTarget)) {
                folderIconTarget = nullptr;
                folderIconPopupPending = false;
            }
            if ((folderInlineEdit.target && IsUnderOrEqual(folderDeleteTarget, folderInlineEdit.target))
                || (folderInlineEdit.parent && IsUnderOrEqual(folderDeleteTarget, folderInlineEdit.parent))) {
                CancelInlineFolderEdit();
            }
            RemoveFolderFromParentOrder(folderDeleteTarget);
            removeFolderNode();
            if (currentWasInRemovedTree) {
                currentFolder = fallbackFolder;
            }
            folderDeleteTarget = nullptr;
            SaveConfig();
            if ((currentWasInRemovedTree || selectionWasInRemovedTree) && currentFolder == removedParent) {
                SelectExplorerNeighborAfterRemoval(removedParent, removedItem, beforeItems);
            } else if (selectionWasInRemovedTree) {
                ClearExplorerSelection();
            }
            ImGui::CloseCurrentPopup();
        };

        if (ImGui::Button(UiSettings::Instance().Text(UiText::Cancel))) {
            folderDeleteTarget = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button(UiSettings::Instance().Text(UiText::DeleteFolderMoveContentsHere))) {
            if (CanDeleteFolder(folderDeleteTarget)) {
                NormalizeExplorerOrders();
                FolderNode* fallbackFolder = folderDeleteTarget->parent;
                const std::vector<std::string> removedPath = BuildFolderPath(folderDeleteTarget);
                const std::vector<std::string> targetPath = FolderPathForDirectory(fallbackFolder);
                auto& targetChildren = fallbackFolder ? fallbackFolder->children : ActiveFolders();

                const std::vector<ExplorerItem> oldItems = folderDeleteTarget->items;
                for (const ExplorerItem& item : oldItems) {
                    if (item.kind == ExplorerItemKind::Bind) {
                        const int index = FindHotkeyIndexByOrderId(item.key);
                        if (index >= 0
                            && hotkeys[static_cast<std::size_t>(index)].categoryId == ActiveCategory().id
                            && hotkeys[static_cast<std::size_t>(index)].folderPath == removedPath) {
                            RemoveBindFromExplorerOrders(item.key);
                            hotkeys[static_cast<std::size_t>(index)].folderPath = targetPath;
                            AppendExplorerItemIfMissing(fallbackFolder, ExplorerItem{ ExplorerItemKind::Bind, item.key });
                        }
                        continue;
                    }

                    auto childIt = std::find_if(
                        folderDeleteTarget->children.begin(),
                        folderDeleteTarget->children.end(),
                        [&](const std::unique_ptr<FolderNode>& child) {
                            return child && child->name == item.key;
                        });
                    if (childIt == folderDeleteTarget->children.end()) {
                        continue;
                    }

                    FolderNode* child = childIt->get();
                    const std::vector<std::string> oldChildPath = BuildFolderPath(child);
                    const std::string baseName = child->name;
                    std::string newName = baseName;
                    for (int suffix = 2; !FolderNameUnique(targetChildren, newName, folderDeleteTarget); ++suffix) {
                        newName = baseName + " " + std::to_string(suffix);
                    }
                    child->name = newName;
                    child->parent = fallbackFolder;
                    const std::vector<std::string> newChildPath = targetPath;
                    std::vector<std::string> fullNewChildPath = newChildPath;
                    fullNewChildPath.push_back(newName);
                    RemapHotkeysFolderPrefix(oldChildPath, fullNewChildPath);

                    std::unique_ptr<FolderNode> extracted = std::move(*childIt);
                    folderDeleteTarget->children.erase(childIt);
                    targetChildren.push_back(std::move(extracted));
                    AppendExplorerItemIfMissing(fallbackFolder, ExplorerItem{ ExplorerItemKind::Folder, newName });
                }

                for (HotkeyEntry& hotkey : hotkeys) {
                    if (hotkey.categoryId == ActiveCategory().id && hotkey.folderPath == removedPath) {
                        RemoveBindFromExplorerOrders(hotkey.orderId);
                        hotkey.folderPath = targetPath;
                        AppendExplorerItemIfMissing(fallbackFolder, ExplorerItem{ ExplorerItemKind::Bind, hotkey.orderId });
                    }
                }

                finishFolderDelete(fallbackFolder, removedPath);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(UiSettings::Instance().Text(UiText::DeleteFolderAll))) {
            if (CanDeleteFolder(folderDeleteTarget)) {
                FolderNode* fallbackFolder = folderDeleteTarget->parent;
                const auto removedPath = BuildFolderPath(folderDeleteTarget);
                DeleteHotkeysFromFolderPath(removedPath);
                finishFolderDelete(fallbackFolder, removedPath);
            }
        }
        ImGui::EndPopup();
    }
}

void BinderModule::Impl::DrawBindDeletePopup() {
    if (bindDeletePopupPending) {
        ImGui::OpenPopup("##binder_bind_delete");
        bindDeletePopupPending = false;
    }

    if (ImGui::BeginPopupModal("##binder_bind_delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", UiSettings::Instance().Text(UiText::DeleteSelectedBindQuestion));
        if (bindDeleteTarget >= 0 && bindDeleteTarget < static_cast<int>(hotkeys.size())) {
            ImGui::TextDisabled("%s", hotkeys[static_cast<std::size_t>(bindDeleteTarget)].label.c_str());
        }
        if (ImGui::Button(UiSettings::Instance().Text(UiText::Cancel))) {
            bindDeleteTarget = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button(UiSettings::Instance().Text(UiText::Delete))) {
            if (bindDeleteTarget >= 0 && bindDeleteTarget < static_cast<int>(hotkeys.size())) {
                const HotkeyEntry& target = hotkeys[static_cast<std::size_t>(bindDeleteTarget)];
                const ExplorerItem removedItem{ ExplorerItemKind::Bind, target.orderId };
                const std::vector<ExplorerItem> beforeItems = ItemsForFolder(currentFolder);
                const bool removedFromCurrentDirectory =
                    target.categoryId == ActiveCategory().id && target.folderPath == CurrentFolderPath();
                RemoveBindFromExplorerOrders(target.orderId);
                StopHotkey(bindDeleteTarget);
                hotkeys.erase(hotkeys.begin() + bindDeleteTarget);
                RefreshNumbers();
                SaveConfig();
                if (removedFromCurrentDirectory) {
                    SelectExplorerNeighborAfterRemoval(currentFolder, removedItem, beforeItems);
                } else if (explorerSelection.kind == ExplorerSelectionKind::Bind
                    && explorerSelection.bindOrderId == removedItem.key) {
                    ClearExplorerSelection();
                }
            }
            bindDeleteTarget = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void BinderModule::Impl::DrawMoveBindPopup() {
    if (moveBindPopupPending) {
        ImGui::OpenPopup("##binder_move_bind");
        moveBindPopupPending = false;
    }

    if (!ImGui::BeginPopupModal("##binder_move_bind", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    HotkeyEntry* hotkey = nullptr;
    if (moveBindTarget >= 0 && moveBindTarget < static_cast<int>(hotkeys.size())) {
        hotkey = &hotkeys[static_cast<std::size_t>(moveBindTarget)];
    }

    ImGui::TextUnformatted(ui.Text(UiText::ActionMoveTo));
    ImGui::Separator();

    if (!hotkey) {
        moveBindTarget = -1;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextDisabled("%s", ui.Format(UiText::BindListEntryFormat, hotkey->number, BuildBindDisplayLabel(*hotkey).c_str()).c_str());
    ImGui::Spacing();

    const auto drawFolderNode = [&](auto&& self, const BinderCategory& category, FolderNode& folder) -> void {
        ImGui::PushID(folder.id);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (folder.children.empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        const std::string folderLabel = FormatFolderLabel(folder.name);
        const bool opened = ImGui::TreeNodeEx("##move_folder_node", flags, "%s", folderLabel.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            const std::vector<std::string> targetPath = BuildFolderPath(&folder);
            SelectCategory(category.id);
            MoveBindToFolderPath(moveBindTarget, targetPath, "move_popup");
            moveBindTarget = -1;
            ImGui::CloseCurrentPopup();
        }

        if (opened && !folder.children.empty()) {
            for (auto& child : folder.children) {
                if (child) {
                    self(self, category, *child);
                }
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    };

    if (ImGui::BeginChild("##binder_move_bind_folders", ScaleUi(360.0f, 240.0f), ImGuiChildFlags_Borders)) {
        for (const BinderCategory& category : categories) {
            ImGui::PushID(category.id.c_str());
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;
            const std::string categoryLabel = std::string(ui_icons::Folder) + " " + category.name;
            const bool opened = ImGui::TreeNodeEx("##move_bind_category", flags, "%s", categoryLabel.c_str());
            if (ImGui::BeginPopupContextItem("##move_bind_category_context")) {
                ImGui::TextDisabled("%s", category.name.c_str());
                ImGui::EndPopup();
            }
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                SelectCategory(category.id);
                MoveBindToFolderPath(moveBindTarget, {}, "move_popup");
                moveBindTarget = -1;
                ImGui::CloseCurrentPopup();
            }
            if (opened) {
                const std::string rootLabel = std::string(ui_icons::Folder) + " " + ui.Text(UiText::BinderRootName) + "##move_bind_root";
                const bool rootSelected = hotkey->categoryId == category.id && hotkey->folderPath.empty();
                if (ImGui::Selectable(rootLabel.c_str(), rootSelected, ImGuiSelectableFlags_SpanAvailWidth)) {
                    SelectCategory(category.id);
                    MoveBindToFolderPath(moveBindTarget, {}, "move_popup");
                    moveBindTarget = -1;
                    ImGui::CloseCurrentPopup();
                }
                for (auto& folder : category.folders) {
                    if (folder) {
                        drawFolderNode(drawFolderNode, category, *folder);
                    }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button(ui.Text(UiText::Cancel))) {
        moveBindTarget = -1;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void BinderModule::Impl::DrawMoveFolderPopup() {
    if (moveFolderPopupPending) {
        ImGui::OpenPopup("##binder_move_folder");
        moveFolderPopupPending = false;
    }

    if (!ImGui::BeginPopupModal("##binder_move_folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    FolderNode* moving = FindFolderByIdR(ActiveFolders(), moveFolderTarget);

    ImGui::TextUnformatted(ui.Text(UiText::ActionMoveTo));
    ImGui::Separator();

    if (!moving) {
        moveFolderTarget = -1;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const std::string activeCategoryIdAtOpen = ActiveCategory().id;
    const std::string movingLabel = FormatFolderPathLabel(BuildFolderPath(moving));
    ImGui::TextDisabled("%s", movingLabel.c_str());
    ImGui::Spacing();

    bool folderMoveCompleted = false;
    const auto moveTo = [&](const BinderCategory& category, const std::vector<std::string>& targetPath) {
        if (MoveFolderToCategoryDirectory(moveFolderTarget, category.id, targetPath, "move_folder_popup")) {
            moveFolderTarget = -1;
            folderMoveCompleted = true;
            ImGui::CloseCurrentPopup();
            return true;
        }

        if (category.id != activeCategoryIdAtOpen) {
            Notify(
                NotificationGroup::Validation,
                NotificationSeverity::Error,
                UiSettings::Instance().Text(UiText::ToastFolderMoveInvalid),
                2200.0);
        }
        return false;
    };

    const auto isBlockedTarget = [&](const BinderCategory& category, const FolderNode& folder) {
        return category.id == activeCategoryIdAtOpen && IsUnderOrEqual(moving, &folder);
    };

    const auto drawFolderNode = [&](auto&& self, const BinderCategory& category, FolderNode& folder) -> void {
        if (folderMoveCompleted) {
            return;
        }

        ImGui::PushID(folder.id);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (folder.children.empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        const bool blocked = isBlockedTarget(category, folder);
        if (blocked) {
            ImGui::BeginDisabled();
        }
        const std::string folderLabel = FormatFolderLabel(folder.name);
        const bool opened = ImGui::TreeNodeEx("##move_folder_node", flags, "%s", folderLabel.c_str());
        const bool clicked = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
        if (blocked) {
            ImGui::EndDisabled();
        }
        if (clicked && !blocked && moveTo(category, BuildFolderPath(&folder))) {
            ImGui::PopID();
            return;
        }

        if (opened && !folder.children.empty() && !folderMoveCompleted) {
            for (auto& child : folder.children) {
                if (child) {
                    self(self, category, *child);
                }
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    };

    if (ImGui::BeginChild("##binder_move_folder_targets", ScaleUi(360.0f, 240.0f), ImGuiChildFlags_Borders)) {
        for (BinderCategory& category : categories) {
            if (folderMoveCompleted) {
                break;
            }
            ImGui::PushID(category.id.c_str());
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;
            const std::string categoryLabel = std::string(ui_icons::Folder) + " " + category.name;
            const bool opened = ImGui::TreeNodeEx("##move_folder_category", flags, "%s", categoryLabel.c_str());
            if (opened) {
                const std::string rootLabel = std::string(ui_icons::Folder) + " " + ui.Text(UiText::BinderRootName) + "##move_folder_root";
                if (ImGui::Selectable(rootLabel.c_str(), false, ImGuiSelectableFlags_SpanAvailWidth)
                    && moveTo(category, std::vector<std::string>{})) {
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }
                for (auto& folder : category.folders) {
                    if (folderMoveCompleted) {
                        break;
                    }
                    if (folder) {
                        drawFolderNode(drawFolderNode, category, *folder);
                    }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (folderMoveCompleted) {
        ImGui::EndPopup();
        return;
    }

    ImGui::Separator();
    if (ImGui::Button(ui.Text(UiText::Cancel))) {
        moveFolderTarget = -1;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void BinderModule::Impl::DrawBindLinesPopup() {
    if (bindLinesPopupPending) {
        ImGui::OpenPopup("##binder_bind_lines");
        bindLinesPopupPending = false;
    }

    if (!ImGui::BeginPopupModal("##binder_bind_lines", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    ImGui::TextUnformatted(ui.Text(UiText::BindLinesTitle));
    ImGui::Separator();

    HotkeyEntry* hotkey = nullptr;
    if (bindLinesTarget >= 0 && bindLinesTarget < static_cast<int>(hotkeys.size())) {
        hotkey = &hotkeys[static_cast<std::size_t>(bindLinesTarget)];
    }

    if (!hotkey) {
        bindLinesTarget = -1;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextDisabled("%s", ui.Format(UiText::BindListEntryFormat, hotkey->number, BuildBindDisplayLabel(*hotkey).c_str()).c_str());
    ImGui::Spacing();

    if (hotkey->messages.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::BindLinesEmpty));
    } else if (ImGui::BeginTable(
                   "##binder_bind_lines_table",
                   4,
                   ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                   ScaleUi(760.0f, 260.0f))) {
        ImGui::TableSetupColumn(ui.Text(UiText::ColumnText), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(ui.Text(UiText::ColumnDelay), ImGuiTableColumnFlags_WidthFixed, ScaleUi(90.0f));
        ImGui::TableSetupColumn(ui.Text(UiText::ColumnMethod), ImGuiTableColumnFlags_WidthFixed, ScaleUi(150.0f));
        ImGui::TableSetupColumn(ui.Text(UiText::ColumnActions), ImGuiTableColumnFlags_WidthFixed, ScaleUi(95.0f));
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < hotkey->messages.size(); ++i) {
            const HotkeyMessage& message = hotkey->messages[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            ImGui::TableSetColumnIndex(0);
            ImGui::TextWrapped("%s", message.text.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", message.intervalMs);

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(SendMethodLabel(message.method));

            ImGui::TableSetColumnIndex(3);
            if (ImGui::Button(ui.Text(UiText::Send))) {
                DoSend(message.text, message.method);
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button(ui.Text(UiText::Cancel))) {
        bindLinesTarget = -1;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void BinderModule::Impl::DrawCapturePopup(bool insideEditorPopup) {
    if (insideEditorPopup != CaptureUsesEditorPopup()) {
        return;
    }

    const HotkeyMode captureDisplayMode =
        captureTarget == CaptureTarget::BindHotkey && editor.active ? editor.draft.hotkeyMode : HotkeyMode::ModifierTrigger;

    hotkeys::DrawCapturePopupModal(
        "##binder_capture_popup",
        capturePopupState,
        capture,
        [this](const std::vector<UINT>& keys) {
            if (ApplyCapturedKeys(keys)) {
                capturePopupInEditor = false;
                return true;
            }
            return false;
        },
        true,
        captureDisplayMode,
        {},
        [this]() {
            captureTarget = CaptureTarget::None;
            captureHotkeyIndex = -1;
            capturePopupInEditor = false;
        });

    if (!capture.Active()) {
        capturePopupInEditor = false;
    }
}

void BinderModule::Impl::DrawSettingsSection(bool includeHeader) {
    EnsureInitialized();

    UiSettings& ui = UiSettings::Instance();
    if (includeHeader) {
        ImGui::SeparatorText(ui.Text(UiText::QuickMenuWindowTitle));
    }
    ImGui::Text("%s", ui.Format(UiText::QuickMenuFormat, QuickMenuHotkeyText().c_str()).c_str());
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::ChangeQuickMenuHotkey))) {
        BeginCapture(CaptureTarget::QuickMenuHotkey);
    }

    const QuickMenuActivationMode quickModes[] = { QuickMenuActivationMode::Hold, QuickMenuActivationMode::Toggle };
    const char* quickModeLabels[] = {
        QuickMenuModeLabel(QuickMenuActivationMode::Hold),
        QuickMenuModeLabel(QuickMenuActivationMode::Toggle),
    };
    int quickMode = quickMenuActivationMode == QuickMenuActivationMode::Toggle ? 1 : 0;
    ImGui::SetNextItemWidth(ScaleUi(180.0f));
    if (ImGui::Combo(ui.Text(UiText::QuickMenuMode), &quickMode, quickModeLabels, IM_ARRAYSIZE(quickModeLabels))) {
        quickMenuActivationMode = quickModes[quickMode];
        SaveConfig();
    }

    const QuickMenuStyle quickStyles[] = { QuickMenuStyle::Tree, QuickMenuStyle::Cascade };
    const char* quickStyleLabels[] = {
        ui.Text(UiText::QuickMenuStyleTree),
        ui.Text(UiText::QuickMenuStyleCascade),
    };
    int quickStyle = quickMenuStyle == QuickMenuStyle::Cascade ? 1 : 0;
    ImGui::SetNextItemWidth(ScaleUi(180.0f));
    if (ImGui::Combo(ui.Text(UiText::QuickMenuStyle), &quickStyle, quickStyleLabels, IM_ARRAYSIZE(quickStyleLabels))) {
        quickMenuStyle = quickStyles[quickStyle];
        ResetQuickMenuVisualState();
        SaveConfig();
    }

    if (ImGui::Checkbox(ui.Text(UiText::QuickMenuShowScrollbar), &quickMenuShowScrollbar)) {
        SaveConfig();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", ui.Text(UiText::QuickMenuShowScrollbarHint));
    }
}

void BinderModule::Impl::DrawBinderSettingsSection(bool includeHeader) {
    EnsureInitialized();

    UiSettings& ui = UiSettings::Instance();
    if (includeHeader) {
        ImGui::SeparatorText(ui.Text(UiText::SettingsSectionBinder));
    }
    ImGui::TextWrapped("%s", ui.Text(UiText::SettingsBinderIntro));
    ImGui::Spacing();

    const BindListStyle bindStyles[] = { BindListStyle::Explorer, BindListStyle::TwoPane };
    const char* bindStyleLabels[] = {
        ui.Text(UiText::SettingsBinderListStyleExplorer),
        ui.Text(UiText::SettingsBinderListStyleTwoPane),
    };
    int bindStyle = bindListStyle == BindListStyle::TwoPane ? 1 : 0;
    ImGui::SetNextItemWidth(ScaleUi(180.0f));
    if (ImGui::Combo(ui.Text(UiText::SettingsBinderListStyle), &bindStyle, bindStyleLabels, IM_ARRAYSIZE(bindStyleLabels))) {
        bindListStyle = bindStyles[bindStyle];
        SaveConfig();
    }

    int timeoutSeconds = textConfirmationWaitTimeoutMs / 1000;
    ImGui::SetNextItemWidth(ScaleUi(150.0f));
    if (ImGui::DragInt(
            ui.Text(UiText::SettingsBinderTextConfirmationTimeoutSec),
            &timeoutSeconds,
            0.2f,
            kMinTextConfirmationWaitTimeoutMs / 1000,
            kMaxTextConfirmationWaitTimeoutMs / 1000)) {
        timeoutSeconds = std::clamp(
            timeoutSeconds,
            kMinTextConfirmationWaitTimeoutMs / 1000,
            kMaxTextConfirmationWaitTimeoutMs / 1000);
        const int timeoutMs = timeoutSeconds * 1000;
        if (textConfirmationWaitTimeoutMs != timeoutMs) {
            textConfirmationWaitTimeoutMs = timeoutMs;
            SaveConfig();
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", ui.Text(UiText::SettingsBinderTextConfirmationTimeoutHint));
    }
}

void BinderModule::Impl::DrawMainTab() {
    EnsureInitialized();

    if (editor.active) {
        DrawEditor();
        DrawCategoryPopups();
        DrawFolderPopups();
        DrawMoveBindPopup();
        DrawMoveFolderPopup();
        return;
    }

    ImGui::SeparatorText(UiSettings::Instance().Text(UiText::BinderSectionTitle));
    DrawCategoryTabs();
    if (bindListStyle == BindListStyle::TwoPane) {
        DrawTwoPaneBinder();
    } else {
        DrawExplorerPane();
    }

    DrawCategoryPopups();
    DrawFolderPopups();
    DrawEditor();
    DrawMoveBindPopup();
    DrawMoveFolderPopup();
}

void BinderModule::Impl::DrawOverlay() {
    DrawQuickMenu();
    DrawInputDialog();
    DrawCapturePopup(false);
    DrawBindLinesPopup();
}
