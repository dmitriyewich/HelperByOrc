#include "binder_module_impl.h"

namespace {

int TwoPaneVisualFolderDepth(const int depth) {
    return std::max(depth - 1, 0);
}

} // namespace

int BinderModule::Impl::FolderOrderIndex(FolderNode* folder) const {
    if (!folder) {
        return -1;
    }

    const std::vector<ExplorerItem>& items = ItemsForFolder(folder->parent);
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const ExplorerItem& item = items[static_cast<std::size_t>(i)];
        if (item.kind == ExplorerItemKind::Folder && item.key == folder->name) {
            return i;
        }
    }
    return -1;
}

void BinderModule::Impl::SetTwoPaneActivePane(const TwoPaneActivePane pane) {
    twoPaneActivePane = pane;
}

void BinderModule::Impl::CollectTwoPaneVisibleFolderNodes(
    FolderNode& folder,
    const std::string& filter,
    std::vector<FolderNode*>& out) {
    if (!TwoPaneFolderMatchesFilter(folder, filter)) {
        return;
    }

    out.push_back(&folder);

    const bool searchActive = !filter.empty();
    if (!searchActive && !folder.open) {
        return;
    }

    for (auto& child : folder.children) {
        if (child) {
            CollectTwoPaneVisibleFolderNodes(*child, filter, out);
        }
    }
}

std::vector<FolderNode*> BinderModule::Impl::CollectTwoPaneVisibleFolders(const std::string& filter) {
    std::vector<FolderNode*> folders;
    folders.push_back(nullptr);
    for (auto& folder : ActiveFolders()) {
        if (folder) {
            CollectTwoPaneVisibleFolderNodes(*folder, filter, folders);
        }
    }
    return folders;
}

std::vector<int> BinderModule::Impl::CollectTwoPaneVisibleBindIndices(const std::string& filter) {
    NormalizeExplorerOrders();

    std::vector<int> indices;
    const std::vector<ExplorerItem>& items = ItemsForFolder(currentFolder);
    const std::vector<std::string> folderPath = CurrentFolderPath();
    const std::string& categoryId = ActiveCategory().id;

    for (const ExplorerItem& item : items) {
        if (item.kind != ExplorerItemKind::Bind) {
            continue;
        }

        const int hotkeyIndex = FindHotkeyIndexByOrderId(item.key);
        if (hotkeyIndex < 0 || hotkeyIndex >= static_cast<int>(hotkeys.size())) {
            continue;
        }

        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(hotkeyIndex)];
        if (hotkey.categoryId != categoryId || hotkey.folderPath != folderPath || !TwoPaneBindMatchesFilter(hotkey, filter)) {
            continue;
        }

        indices.push_back(hotkeyIndex);
    }

    return indices;
}

void BinderModule::Impl::MoveTwoPaneFolderSelection(const int delta, const std::string& filter) {
    if (delta == 0) {
        return;
    }

    std::vector<FolderNode*> folders = CollectTwoPaneVisibleFolders(filter);
    if (folders.empty()) {
        return;
    }

    auto it = std::find(folders.begin(), folders.end(), currentFolder);
    int index = it == folders.end()
        ? (delta > 0 ? 0 : static_cast<int>(folders.size()) - 1)
        : static_cast<int>(std::distance(folders.begin(), it));
    index = std::clamp(index + (it == folders.end() ? 0 : delta), 0, static_cast<int>(folders.size()) - 1);

    OpenFolder(folders[static_cast<std::size_t>(index)], false);
}

void BinderModule::Impl::MoveTwoPaneBindSelection(const int delta, const std::string& filter) {
    if (delta == 0) {
        return;
    }

    const std::vector<int> indices = CollectTwoPaneVisibleBindIndices(filter);
    if (indices.empty()) {
        if (explorerSelection.kind == ExplorerSelectionKind::Bind) {
            ClearExplorerSelection();
        }
        return;
    }

    const int selectedIndex = explorerSelection.kind == ExplorerSelectionKind::Bind
        ? FindHotkeyIndexByOrderId(explorerSelection.bindOrderId)
        : selectedBindIndex;
    auto it = std::find(indices.begin(), indices.end(), selectedIndex);
    int index = it == indices.end()
        ? (delta > 0 ? 0 : static_cast<int>(indices.size()) - 1)
        : static_cast<int>(std::distance(indices.begin(), it));
    index = std::clamp(index + (it == indices.end() ? 0 : delta), 0, static_cast<int>(indices.size()) - 1);

    SelectExplorerBind(indices[static_cast<std::size_t>(index)], true);
}

void BinderModule::Impl::DrawTwoPaneFolderKeyboardShortcuts() {
    if (folderInlineEdit.mode != FolderInlineEditMode::None) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !twoPaneFolderSearch.empty()) {
        twoPaneFolderSearch.clear();
        ImGui::ClearActiveID();
        return;
    }

    if (ImGui::GetActiveID() != 0) {
        return;
    }

    const std::string filter = ToLower(Trim(twoPaneFolderSearch));
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
        MoveTwoPaneFolderSelection(-1, filter);
    } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
        MoveTwoPaneFolderSelection(1, filter);
    } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        if (currentFolder && !currentFolder->open) {
            currentFolder->open = true;
            SaveConfig();
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
        NavigateUp();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (CanDeleteFolder(currentFolder)) {
            folderDeleteTarget = currentFolder;
            folderDeletePopupPending = true;
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        if (currentFolder) {
            BeginInlineRenameFolder(currentFolder);
        }
    }
}

void BinderModule::Impl::DrawTwoPaneBindKeyboardShortcuts() {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !twoPaneBindSearch.empty()) {
        twoPaneBindSearch.clear();
        ImGui::ClearActiveID();
        return;
    }

    if (ImGui::GetActiveID() != 0) {
        return;
    }

    const std::string filter = ToLower(Trim(twoPaneBindSearch));
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
        MoveTwoPaneBindSelection(-1, filter);
    } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
        MoveTwoPaneBindSelection(1, filter);
    } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        const std::vector<int> indices = CollectTwoPaneVisibleBindIndices(filter);
        const int bindIndex = explorerSelection.kind == ExplorerSelectionKind::Bind
            ? FindHotkeyIndexByOrderId(explorerSelection.bindOrderId)
            : selectedBindIndex;
        if (std::find(indices.begin(), indices.end(), bindIndex) != indices.end()) {
            StartEditing(bindIndex, false);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
        NavigateUp();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        const std::vector<int> indices = CollectTwoPaneVisibleBindIndices(filter);
        const int bindIndex = explorerSelection.kind == ExplorerSelectionKind::Bind
            ? FindHotkeyIndexByOrderId(explorerSelection.bindOrderId)
            : selectedBindIndex;
        if (std::find(indices.begin(), indices.end(), bindIndex) != indices.end()) {
            bindDeleteTarget = bindIndex;
            bindDeletePopupPending = true;
        }
    }
}

void BinderModule::Impl::DrawTwoPaneKeyboardShortcuts(const bool focused) {
    if (!focused) {
        return;
    }

    if (twoPaneActivePane == TwoPaneActivePane::Folders) {
        DrawTwoPaneFolderKeyboardShortcuts();
        return;
    }

    DrawTwoPaneBindKeyboardShortcuts();
}

bool BinderModule::Impl::TwoPaneFolderMatchesFilter(const FolderNode& folder, std::string_view filter) const {
    if (filter.empty()) {
        return true;
    }

    const std::string hay = ToLower(folder.name + " " + JoinPath(BuildFolderPath(&folder)));
    if (hay.find(filter) != std::string::npos) {
        return true;
    }
    for (const auto& child : folder.children) {
        if (child && TwoPaneFolderMatchesFilter(*child, filter)) {
            return true;
        }
    }
    return false;
}

bool BinderModule::Impl::TwoPaneBindMatchesFilter(const HotkeyEntry& hotkey, std::string_view filter) const {
    if (filter.empty()) {
        return true;
    }

    const std::string hay = ToLower(
        BuildBindDisplayLabel(hotkey) + " "
        + hotkey.command + " "
        + std::to_string(hotkey.number));
    return hay.find(filter) != std::string::npos;
}

void BinderModule::Impl::DrawTwoPaneFolderInlineEditRow(FolderNode* parent, const int depth, int& rowIndex) {
    if (folderInlineEdit.mode == FolderInlineEditMode::None) {
        return;
    }
    if (folderInlineEdit.mode == FolderInlineEditMode::Create && folderInlineEdit.parent != parent) {
        return;
    }
    if (folderInlineEdit.mode == FolderInlineEditMode::Rename
        && (!folderInlineEdit.target || folderInlineEdit.target->parent != parent)) {
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    const float rowHeight = std::max(ImGui::GetFrameHeight() + ScaleUi(2.0f), ScaleUi(28.0f));
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 rowPos = ImGui::GetCursorScreenPos();
    const ImRect rowRect(rowPos, ImVec2(rowPos.x + width, rowPos.y + rowHeight));
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    DrawBinderListRowBackground(drawList, rowRect, rowIndex, true, false);

    const int visualDepth = TwoPaneVisualFolderDepth(depth);
    const float indent = ScaleUi(16.0f) * static_cast<float>(visualDepth);
    const float arrowW = ScaleUi(18.0f);
    const float iconW = ScaleUi(22.0f);
    const float buttonSide = std::ceil(ImGui::GetFrameHeight() - ScaleUi(1.0f));
    const ImVec2 buttonSize(buttonSide, buttonSide);
    const float buttonGap = ScaleUi(4.0f);
    const float actionGroupWidth = buttonSide * 2.0f + buttonGap;
    const float inputX = rowRect.Min.x + indent + arrowW + iconW + ScaleUi(4.0f);
    const float inputY = rowRect.Min.y + std::floor(std::max(0.0f, (rowHeight - ImGui::GetFrameHeight()) * 0.5f));
    const float inputMaxX = std::max(inputX + ScaleUi(48.0f), rowRect.Max.x - actionGroupWidth - ScaleUi(10.0f));
    const float inputWidth = std::max(ScaleUi(48.0f), inputMaxX - inputX);

    DrawCenteredIconGlyph(
        drawList,
        ui_icons::Folder,
        ImRect(
            ImVec2(rowRect.Min.x + indent + arrowW, rowRect.Min.y),
            ImVec2(rowRect.Min.x + indent + arrowW + iconW, rowRect.Max.y)),
        ImGui::GetColorU32(BinderListStyleTokens().headerText));

    ImGui::PushID(parent ? parent->id : 0);
    ImGui::PushID(folderInlineEdit.mode == FolderInlineEditMode::Create ? "create" : "rename");
    if (folderInlineEdit.focusPending) {
        ImGui::SetKeyboardFocusHere();
        folderInlineEdit.focusPending = false;
    }
    ImGui::SetCursorScreenPos(ImVec2(inputX, inputY));
    ImGui::SetNextItemWidth(inputWidth);
    const bool submitted = InputTextString(
        "##two_pane_folder_name",
        folderInlineEdit.name,
        ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue,
        128);
    const bool escapePressed = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    const bool deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SetCursorScreenPos(ImVec2(
        std::floor(rowRect.Max.x - actionGroupWidth - ScaleUi(4.0f)),
        std::floor(rowRect.Min.y + std::max(0.0f, (rowHeight - buttonSide) * 0.5f))));
    const bool saveClicked = BinderListFlatIconButton(ui_icons::Check, "##save", ui.Text(UiText::Save), buttonSize, BinderListStyleTokens().enabled);
    ImGui::SameLine(0.0f, buttonGap);
    const bool cancelClicked = BinderListFlatIconButton(ui_icons::Delete, "##cancel", ui.Text(UiText::Cancel), buttonSize, BinderListStyleTokens().danger);
    ImGui::PopID();
    ImGui::PopID();

    if (cancelClicked || escapePressed) {
        CancelInlineFolderEdit();
    } else if (saveClicked || submitted || deactivatedAfterEdit) {
        (void)CommitInlineFolderEdit();
    }

    ImGui::SetCursorScreenPos(ImVec2(rowRect.Min.x, rowRect.Max.y));
    ++rowIndex;
}

void BinderModule::Impl::DrawTwoPaneRootRow(int& rowIndex, const std::string& filter) {
    UiSettings& ui = UiSettings::Instance();
    const float rowHeight = std::max(ImGui::GetFrameHeight() + ScaleUi(2.0f), ScaleUi(28.0f));
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 rowPos = ImGui::GetCursorScreenPos();
    const ImRect rowRect(rowPos, ImVec2(rowPos.x + width, rowPos.y + rowHeight));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const bool selected = currentFolder == nullptr;
    const bool hovered = ImGui::IsMouseHoveringRect(rowRect.Min, rowRect.Max, true);

    DrawBinderListRowBackground(drawList, rowRect, rowIndex, selected, hovered);

    ImGui::PushID("two_pane_root");
    ImGui::SetCursorScreenPos(rowRect.Min);
    const bool clicked = ImGui::InvisibleButton("##row", rowRect.GetSize());
    if (clicked) {
        OpenFolder(nullptr, false);
    }

    std::optional<FolderDropZone> dropPreview{};
    ExplorerDirectoryDropStatus dropStatus = ExplorerDirectoryDropStatus::None;
    if (const ImGuiPayload* payload = ActiveExplorerDragPayload()) {
        if (IsExplorerBindDragPayload(payload)) {
            dropStatus = CanDropExplorerPayloadToDirectory(payload, nullptr, true);
        } else {
            int folderId = 0;
            if (TryGetExplorerFolderDragId(payload, folderId)) {
                bool noop = false;
                const bool valid = CanMoveFolderToExplorerDirectory(
                    folderId,
                    nullptr,
                    static_cast<int>(ItemsForFolder(nullptr).size()),
                    &noop);
                dropStatus = !valid
                    ? ExplorerDirectoryDropStatus::Invalid
                    : (noop ? ExplorerDirectoryDropStatus::Noop : ExplorerDirectoryDropStatus::Accept);
            }
        }
        if (hovered && dropStatus == ExplorerDirectoryDropStatus::Accept) {
            dropPreview = FolderDropZone::Into;
        }
    }
    if (ImGui::BeginDragDropTarget()) {
        if (dropStatus == ExplorerDirectoryDropStatus::Accept) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kBindDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->IsDelivery()) {
                    DropExplorerPayloadToTwoPaneDirectory(
                        payload,
                        nullptr,
                        std::nullopt,
                        "explorer_two_pane_root_drop");
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kFolderDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->IsDelivery()) {
                    DropExplorerPayloadToTwoPaneDirectory(
                        payload,
                        nullptr,
                        static_cast<int>(ItemsForFolder(nullptr).size()),
                        "explorer_two_pane_root_drop");
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    const float iconW = ScaleUi(22.0f);
    const float textY = rowRect.Min.y + std::floor((rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
    DrawCenteredIconGlyph(
        drawList,
        ui_icons::House,
        ImRect(rowRect.Min, ImVec2(rowRect.Min.x + iconW, rowRect.Max.y)),
        ImGui::GetColorU32(selected ? BinderListStyleTokens().headerText : BinderListStyleTokens().mutedText));
    const std::string label = EllipsizeText(ui.Text(UiText::BinderNoFolder), std::max(0.0f, rowRect.GetWidth() - iconW - ScaleUi(10.0f)));
    drawList->AddText(
        ImVec2(rowRect.Min.x + iconW + ScaleUi(4.0f), textY),
        ImGui::GetColorU32(selected ? BinderListStyleTokens().headerText : BinderListStyleTokens().mutedText),
        label.c_str());
    if (dropPreview) {
        DrawFolderDropPreview(rowRect, *dropPreview);
    } else if (hovered && dropStatus == ExplorerDirectoryDropStatus::Noop && ActiveExplorerDragPayload()) {
        ImGui::SetTooltip("%s", ui.Text(UiText::BinderDropCurrentFolder));
    }

    ImGui::SetCursorScreenPos(ImVec2(rowRect.Min.x, rowRect.Max.y));
    ImGui::PopID();
    ++rowIndex;

    if (folderInlineEdit.mode == FolderInlineEditMode::Create && folderInlineEdit.parent == nullptr && filter.empty()) {
        DrawTwoPaneFolderInlineEditRow(nullptr, 1, rowIndex);
    }
}

void BinderModule::Impl::DrawTwoPaneFolderNode(FolderNode& folder, const int depth, int& rowIndex, const std::string& filter) {
    if (!TwoPaneFolderMatchesFilter(folder, filter)) {
        return;
    }

    const bool searchActive = !filter.empty();
    const bool opened = searchActive || folder.open;
    if (IsInlineRenamingFolder(&folder)) {
        DrawTwoPaneFolderInlineEditRow(folder.parent, depth, rowIndex);
    } else {
        UiSettings& ui = UiSettings::Instance();
        const float rowHeight = std::max(ImGui::GetFrameHeight() + ScaleUi(2.0f), ScaleUi(28.0f));
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        const ImRect rowRect(rowPos, ImVec2(rowPos.x + width, rowPos.y + rowHeight));
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const bool selected = currentFolder == &folder;
        const bool hovered = ImGui::IsMouseHoveringRect(rowRect.Min, rowRect.Max, true);
        const bool hasChildren = !folder.children.empty()
            || (folderInlineEdit.mode == FolderInlineEditMode::Create && folderInlineEdit.parent == &folder);

        DrawBinderListRowBackground(drawList, rowRect, rowIndex, selected, hovered);

        const int visualDepth = TwoPaneVisualFolderDepth(depth);
        const float indent = ScaleUi(16.0f) * static_cast<float>(visualDepth);
        const float arrowW = ScaleUi(18.0f);
        const float iconW = ScaleUi(22.0f);
        const float buttonSide = std::ceil(ImGui::GetFrameHeight() - ScaleUi(1.0f));
        const ImVec2 buttonSize(buttonSide, buttonSide);
        const float actionX = rowRect.Max.x - buttonSide - ScaleUi(4.0f);
        const ImRect arrowRect(
            ImVec2(rowRect.Min.x + indent, rowRect.Min.y),
            ImVec2(rowRect.Min.x + indent + arrowW, rowRect.Max.y));

        ImGui::PushID(folder.id);
        ImGui::SetCursorScreenPos(rowRect.Min);
        const float rowHitWidth = std::max(1.0f, actionX - rowRect.Min.x - ScaleUi(4.0f));
        const bool clicked = ImGui::InvisibleButton("##row", ImVec2(rowHitWidth, rowHeight));
        const bool rowItemHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const bool doubleClicked = rowItemHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        if (clicked) {
            if (hasChildren && ImGui::IsMouseHoveringRect(arrowRect.Min, arrowRect.Max, true)) {
                folder.open = !folder.open;
                SaveConfig();
            } else {
                OpenFolder(&folder, false);
            }
        }
        if (doubleClicked && hasChildren) {
            folder.open = !folder.open;
            SaveConfig();
        }

        std::optional<FolderDropZone> dropPreview{};
        ExplorerDirectoryDropStatus dropStatus = ExplorerDirectoryDropStatus::None;
        FolderNode* dropTarget = nullptr;
        std::optional<int> dropInsertIndex{};
        if (const ImGuiPayload* payload = ActiveExplorerDragPayload()) {
            FolderDropZone zone = ResolveFolderDropZone(rowRect);
            if (IsExplorerBindDragPayload(payload)) {
                zone = FolderDropZone::Into;
                dropTarget = &folder;
                dropStatus = CanDropExplorerPayloadToDirectory(payload, dropTarget, true);
            } else {
                int folderId = 0;
                if (TryGetExplorerFolderDragId(payload, folderId)) {
                    dropTarget = zone == FolderDropZone::Into ? &folder : folder.parent;
                    const int orderIndex = FolderOrderIndex(&folder);
                    dropInsertIndex = zone == FolderDropZone::Into
                        ? static_cast<int>(ItemsForFolder(&folder).size())
                        : orderIndex + (zone == FolderDropZone::After ? 1 : 0);
                    bool noop = false;
                    const bool valid = orderIndex >= 0
                        && CanMoveFolderToExplorerDirectory(folderId, dropTarget, *dropInsertIndex, &noop);
                    dropStatus = !valid
                        ? ExplorerDirectoryDropStatus::Invalid
                        : (noop ? ExplorerDirectoryDropStatus::Noop : ExplorerDirectoryDropStatus::Accept);
                }
            }
            if (hovered && dropStatus == ExplorerDirectoryDropStatus::Accept) {
                dropPreview = zone;
            }
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
            const int folderId = folder.id;
            ImGui::SetDragDropPayload(kFolderDragPayload, &folderId, sizeof(folderId));
            ImGui::TextUnformatted(folder.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (dropStatus == ExplorerDirectoryDropStatus::Accept) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kBindDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                    if (payload->IsDelivery()) {
                        DropExplorerPayloadToTwoPaneDirectory(
                            payload,
                            dropTarget,
                            dropInsertIndex,
                            "explorer_two_pane_folder_drop");
                    }
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kFolderDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                    if (payload->IsDelivery()) {
                        DropExplorerPayloadToTwoPaneDirectory(
                            payload,
                            dropTarget,
                            dropInsertIndex,
                            "explorer_two_pane_folder_drop");
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        const float textY = rowRect.Min.y + std::floor((rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
        if (hasChildren) {
            DrawCenteredIconGlyph(
                drawList,
                opened ? ui_icons::AngleDown : ui_icons::ChevronRight,
                arrowRect,
                ImGui::GetColorU32(BinderListStyleTokens().faintText),
                std::floor(ImGui::GetFontSize() * 0.76f));
        }
        const ImRect iconRect(
            ImVec2(rowRect.Min.x + indent + arrowW, rowRect.Min.y),
            ImVec2(rowRect.Min.x + indent + arrowW + iconW, rowRect.Max.y));
        const std::string folderIcon = FolderIconGlyph(folder);
        DrawCenteredIconGlyph(
            drawList,
            folderIcon.c_str(),
            iconRect,
            ImGui::GetColorU32(selected ? BinderListStyleTokens().headerText : BinderListStyleTokens().mutedText));

        const bool hasConditions = HasSelectedCondition(folder.conditions);
        const std::string marker = std::string(ui_icons::Sliders);
        const ImVec2 markerSize = hasConditions ? ImGui::CalcTextSize(marker.c_str()) : ImVec2(0.0f, 0.0f);
        const float markerReserve = hasConditions ? markerSize.x + ScaleUi(12.0f) : 0.0f;
        const float labelX = iconRect.Max.x + ScaleUi(4.0f);
        const float labelMaxX = std::max(labelX, actionX - ScaleUi(6.0f));
        const std::string label = EllipsizeText(folder.name, std::max(0.0f, labelMaxX - labelX - markerReserve));
        ImGui::PushClipRect(ImVec2(labelX, rowRect.Min.y), ImVec2(labelMaxX, rowRect.Max.y), true);
        drawList->AddText(
            ImVec2(labelX, textY),
            ImGui::GetColorU32(selected ? BinderListStyleTokens().headerText : BinderListStyleTokens().mutedText),
            label.c_str());
        if (hasConditions) {
            DrawCenteredIconGlyph(
                drawList,
                marker.c_str(),
                ImRect(ImVec2(labelMaxX - markerSize.x, rowRect.Min.y), ImVec2(labelMaxX, rowRect.Max.y)),
                ImGui::GetColorU32(BinderListStyleTokens().faintText));
        }
        ImGui::PopClipRect();

        ImGui::SetCursorScreenPos(ImVec2(
            std::floor(actionX),
            std::floor(rowRect.Min.y + std::max(0.0f, (rowHeight - buttonSide) * 0.5f))));
        if (BinderListFlatIconButton(ui_icons::Bars, "##folder_actions", ui.Text(UiText::ColumnActions), buttonSize)) {
            ImGui::OpenPopup("##two_pane_folder_actions");
        }
        if (ImGui::BeginPopup("##two_pane_folder_actions")) {
            if (ImGui::MenuItem(ui.Text(UiText::BinderOpenFolder))) {
                OpenFolder(&folder, false);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem(ui.Text(UiText::AddBind))) {
                OpenFolder(&folder, false);
                StartEditing(-1, true);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem(ui.Text(UiText::FolderAdd))) {
                folder.open = true;
                OpenFolder(&folder, false);
                BeginInlineCreateFolder(&folder);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem(ui.Text(UiText::ActionMoveTo))) {
                moveFolderTarget = folder.id;
                moveFolderPopupPending = true;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem(ui.Text(UiText::FolderRename))) {
                BeginInlineRenameFolder(&folder);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem(ui.Text(UiText::IconPickerTitle))) {
                folderIconTarget = &folder;
                folderIconPopupPending = true;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem(ui.Text(UiText::EditorOpenConditions))) {
                folderConditionsTarget = &folder;
                folderConditionsPopupPending = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ui.Text(UiText::Delete))) {
                folderDeleteTarget = &folder;
                folderDeletePopupPending = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (dropPreview) {
            DrawFolderDropPreview(rowRect, *dropPreview);
        }
        if (rowItemHovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !ActiveExplorerDragPayload()) {
            const std::string path = JoinPath(BuildFolderPath(&folder));
            if (!path.empty() && path != folder.name) {
                ImGui::SetTooltip("%s", path.c_str());
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(rowRect.Min.x, rowRect.Max.y));
        ImGui::PopID();
        ++rowIndex;
    }

    if (opened) {
        for (auto& child : folder.children) {
            if (child) {
                DrawTwoPaneFolderNode(*child, depth + 1, rowIndex, filter);
            }
        }
        if (folderInlineEdit.mode == FolderInlineEditMode::Create && folderInlineEdit.parent == &folder && filter.empty()) {
            DrawTwoPaneFolderInlineEditRow(&folder, depth + 1, rowIndex);
        }
    }
}

void BinderModule::Impl::DrawTwoPaneFolderPane() {
    UiSettings& ui = UiSettings::Instance();
    const std::size_t folderCount = CountFoldersRecursive(ActiveFolders());
    if (DrawTwoPanePaneHeader(
            ui.Text(UiText::ColumnFolders),
            folderCount,
            nullptr,
            ui_icons::FolderPlus,
            ui.Text(UiText::FolderAdd),
            "##two_pane_add_folder",
            ui.Text(UiText::BinderAddFolderTooltip))) {
        if (currentFolder) {
            currentFolder->open = true;
        }
        BeginInlineCreateFolder(currentFolder);
    }
    DrawBinderListSearchBox(
        "##binder_two_pane_folder_search",
        ui.Text(UiText::BinderSearchFolders),
        twoPaneFolderSearch);

    const std::string filter = ToLower(Trim(twoPaneFolderSearch));
    int rowIndex = 0;
    DrawTwoPaneRootRow(rowIndex, filter);
    for (auto& folder : ActiveFolders()) {
        if (folder) {
            DrawTwoPaneFolderNode(*folder, 1, rowIndex, filter);
        }
    }

    // The custom folder tree advances layout with SetCursorScreenPos(); commit
    // the final position so Dear ImGui does not treat it as boundary growth.
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void BinderModule::Impl::DrawTwoPaneBindDirectory() {
    NormalizeExplorerOrders();
    const std::vector<ExplorerItem> items = ItemsForFolder(currentFolder);
    const std::string filter = ToLower(Trim(twoPaneBindSearch));

    UiSettings& ui = UiSettings::Instance();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float iconButtonSide = std::ceil(ImGui::GetFrameHeight() - ScaleUi(1.0f));
    const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float columnGap = ScaleUi(8.0f);
    const float minNameWidth = ScaleUi(90.0f);

    ExplorerListLayout layout;
    layout.width = availableWidth;
    layout.rowHeight = std::max(ImGui::GetFrameHeight() + ScaleUi(2.0f), ScaleUi(28.0f));
    layout.headerHeight = std::max(ImGui::GetTextLineHeight() + ScaleUi(7.0f), ScaleUi(23.0f));
    layout.iconW = std::ceil(iconButtonSide + ScaleUi(18.0f));
    layout.actionsW = std::ceil(iconButtonSide * 5.0f + ScaleUi(4.0f) * 4.0f + ScaleUi(8.0f));
    layout.actionsW = std::max(1.0f, layout.actionsW);
    layout.modernVisual = true;
    layout.iconX = start.x;
    layout.actionsX = std::max(start.x + layout.iconW + minNameWidth + columnGap, start.x + layout.width - layout.actionsW);
    layout.nameX = layout.iconX + layout.iconW;
    layout.nameW = std::max(1.0f, layout.actionsX - layout.nameX - columnGap);

    const ImRect headerRect(start, ImVec2(start.x + layout.width, start.y + layout.headerHeight));
    drawList->AddLine(
        ImVec2(headerRect.Min.x, headerRect.Max.y),
        headerRect.Max,
        ImGui::GetColorU32(BinderListStyleTokens().separator));

    const auto drawHeaderLabel = [&](const char* label, float x, float width, bool centered) {
        const ImRect cell(ImVec2(x, headerRect.Min.y), ImVec2(x + width, headerRect.Max.y));
        const float padX = ScaleUi(6.0f);
        const std::string clipped = EllipsizeText(label ? label : "", std::max(0.0f, cell.GetWidth() - padX * 2.0f));
        const ImVec2 labelSize = ImGui::CalcTextSize(clipped.c_str());
        const float textX = centered
            ? std::floor(cell.Min.x + std::max(0.0f, (cell.GetWidth() - labelSize.x) * 0.5f))
            : std::floor(cell.Min.x + padX);
        const float textY = std::floor(cell.Min.y + std::max(0.0f, (cell.GetHeight() - labelSize.y) * 0.5f));
        ImGui::PushClipRect(cell.Min, cell.Max, true);
        drawList->AddText(ImVec2(textX, textY), ImGui::GetColorU32(BinderListStyleTokens().faintText), clipped.c_str());
        ImGui::PopClipRect();
    };

    drawHeaderLabel("", layout.iconX, layout.iconW, true);
    drawHeaderLabel(ui.Text(UiText::ColumnName), layout.nameX, layout.nameW, false);
    drawHeaderLabel(ui.Text(UiText::ColumnActions), layout.actionsX, layout.actionsW, true);
    ImGui::Dummy(ImVec2(layout.width, layout.headerHeight));

    int visibleRows = 0;
    for (int orderIndex = 0; orderIndex < static_cast<int>(items.size()); ++orderIndex) {
        const ExplorerItem& item = items[static_cast<std::size_t>(orderIndex)];
        if (item.kind != ExplorerItemKind::Bind) {
            continue;
        }
        const int hotkeyIndex = FindHotkeyIndexByOrderId(item.key);
        if (hotkeyIndex < 0 || hotkeyIndex >= static_cast<int>(hotkeys.size())) {
            continue;
        }
        HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(hotkeyIndex)];
        if (hotkey.categoryId != ActiveCategory().id || hotkey.folderPath != CurrentFolderPath()) {
            continue;
        }
        if (!TwoPaneBindMatchesFilter(hotkey, filter)) {
            continue;
        }

        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        const ImRect rowRect(rowPos, ImVec2(rowPos.x + layout.width, rowPos.y + layout.rowHeight));
        DrawExplorerBindRow(hotkeyIndex, orderIndex, layout, rowRect, false);
        ++visibleRows;
    }

    if (visibleRows == 0) {
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        const ImRect emptyRect(rowPos, ImVec2(rowPos.x + layout.width, rowPos.y + layout.rowHeight));
        const float textY = emptyRect.Min.y + std::floor((layout.rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
        drawList->AddText(
            ImVec2(emptyRect.Min.x + ScaleUi(12.0f), textY),
            ImGui::GetColorU32(BinderListStyleTokens().faintText),
            ui.Text(UiText::BinderEmptyBinds));
        ImGui::SetCursorScreenPos(emptyRect.Min);
        ImGui::InvisibleButton("##two_pane_empty_binds_drop", emptyRect.GetSize());
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kBindDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->Data != nullptr && payload->DataSize == sizeof(int) && payload->IsDelivery()) {
                    MoveBindToExplorerDirectory(
                        *static_cast<const int*>(payload->Data),
                        currentFolder,
                        static_cast<int>(items.size()),
                        "explorer_two_pane_bind_drop");
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && IsExplorerBindDragPayload(ActiveExplorerDragPayload())) {
            DrawFolderDropPreview(emptyRect, FolderDropZone::Into);
        }
        ImGui::SetCursorScreenPos(ImVec2(emptyRect.Min.x, emptyRect.Max.y));
    }

    const float remainingHeight = ImGui::GetContentRegionAvail().y;
    const float bottomSlack = ImGui::GetStyle().ItemSpacing.y + ScaleUi(2.0f);
    if (remainingHeight > ScaleUi(8.0f) + bottomSlack) {
        const ImVec2 dropPos = ImGui::GetCursorScreenPos();
        const float dropHeight = std::max(ScaleUi(8.0f), remainingHeight - bottomSlack);
        const ImRect dropRect(dropPos, ImVec2(dropPos.x + layout.width, dropPos.y + dropHeight));
        ImGui::InvisibleButton("##two_pane_bind_empty_area_drop", dropRect.GetSize());
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kBindDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->Data != nullptr && payload->DataSize == sizeof(int) && payload->IsDelivery()) {
                    MoveBindToExplorerDirectory(
                        *static_cast<const int*>(payload->Data),
                        currentFolder,
                        static_cast<int>(items.size()),
                        "explorer_two_pane_bind_drop");
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && IsExplorerBindDragPayload(ActiveExplorerDragPayload())) {
            DrawFolderDropPreview(dropRect, FolderDropZone::Before);
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void BinderModule::Impl::DrawTwoPaneBindPane() {
    UiSettings& ui = UiSettings::Instance();
    const std::string folderLabel = currentFolder ? JoinPath(BuildFolderPath(currentFolder)) : ui.Text(UiText::BinderNoFolder);
    const std::vector<std::string> folderPath = CurrentFolderPath();
    const std::string& categoryId = ActiveCategory().id;
    const std::size_t bindCount = static_cast<std::size_t>(std::count_if(hotkeys.begin(), hotkeys.end(), [&](const HotkeyEntry& hotkey) {
        return hotkey.categoryId == categoryId && hotkey.folderPath == folderPath;
    }));

    if (DrawTwoPanePaneHeader(
            ui.Text(UiText::ColumnBinds),
            bindCount,
            folderLabel.c_str(),
            ui_icons::Keyboard,
            ui.Text(UiText::AddBind),
            "##two_pane_add_bind",
            ui.Text(UiText::BinderAddBindTooltip))) {
        StartEditing(-1, true);
    }
    DrawBinderListSearchBox(
        "##binder_two_pane_bind_search",
        ui.Text(UiText::BinderSearchBinds),
        twoPaneBindSearch);
    DrawTwoPaneBindDirectory();
}

void BinderModule::Impl::DrawTwoPaneBinder() {
    EnsureRootFolder();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float gap = style.ItemSpacing.x;
    const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float availableHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
    const float minLeft = ScaleUi(180.0f);
    const float minRight = ScaleUi(280.0f);
    const float maxLeft = ScaleUi(340.0f);
    float leftWidth = std::clamp(availableWidth * 0.25f, minLeft, maxLeft);
    if (availableWidth - leftWidth - gap < minRight) {
        leftWidth = std::max(ScaleUi(140.0f), availableWidth - minRight - gap);
    }
    const float maxUsableLeft = std::max(ScaleUi(140.0f), availableWidth - gap - ScaleUi(140.0f));
    leftWidth = std::clamp(leftWidth, ScaleUi(140.0f), maxUsableLeft);
    const float rightWidth = std::max(1.0f, availableWidth - leftWidth - gap);

    bool foldersFocused = false;
    bool bindsFocused = false;
    if (BeginTwoPanePanel("##binder_two_pane_folders", ImVec2(leftWidth, availableHeight))) {
        DrawTwoPaneFolderPane();
        foldersFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
            || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        if (foldersFocused) {
            SetTwoPaneActivePane(TwoPaneActivePane::Folders);
        }
    }
    EndTwoPanePanel();
    ImGui::SameLine(0.0f, gap);
    if (BeginTwoPanePanel("##binder_two_pane_binds", ImVec2(rightWidth, availableHeight))) {
        DrawTwoPaneBindPane();
        bindsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
            || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        if (bindsFocused) {
            SetTwoPaneActivePane(TwoPaneActivePane::Binds);
        }
    }
    EndTwoPanePanel();

    DrawTwoPaneKeyboardShortcuts(foldersFocused || bindsFocused);
    DrawBindDeletePopup();
}
