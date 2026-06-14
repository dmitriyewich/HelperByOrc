#include "binder_module_impl.h"

void BinderModule::Impl::DrawExplorerBreadcrumb() {
    UiSettings& ui = UiSettings::Instance();
    struct BreadcrumbCrumb {
        FolderNode* folder = nullptr;
        std::string label;
        bool current = false;
        bool ellipsis = false;
    };

    std::vector<FolderNode*> chain;
    for (FolderNode* folder = currentFolder; folder != nullptr; folder = folder->parent) {
        chain.push_back(folder);
    }
    std::reverse(chain.begin(), chain.end());

    std::string fullPath = ui.Text(UiText::BinderRootName);
    for (const FolderNode* folder : chain) {
        if (folder) {
            fullPath += " / " + folder->name;
        }
    }

    const BinderListVisualStyle& visual = BinderListStyleTokens();
    const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float barHeight = ImGui::GetFrameHeight();
    const float barPadX = ScaleUi(6.0f);
    const float chipPadX = ScaleUi(8.0f);
    const float chipGap = ScaleUi(4.0f);
    const float minChipWidth = ScaleUi(34.0f);
    const float maxChipWidth = ScaleUi(190.0f);
    const float separatorWidth = ImGui::CalcTextSize(ui_icons::ChevronRight).x + chipGap * 2.0f;

    std::vector<BreadcrumbCrumb> crumbs;
    crumbs.push_back(BreadcrumbCrumb{
        nullptr,
        std::string(ui_icons::House) + " " + ui.Text(UiText::BinderRootName),
        currentFolder == nullptr,
        false,
    });
    for (FolderNode* folder : chain) {
        if (!folder) {
            continue;
        }
        crumbs.push_back(BreadcrumbCrumb{
            folder,
            FolderIconGlyph(*folder) + " " + folder->name,
            folder == currentFolder,
            false,
        });
    }

    const auto naturalCrumbWidth = [&](const BreadcrumbCrumb& crumb) {
        return std::min(maxChipWidth, ImGui::CalcTextSize(crumb.label.c_str()).x + chipPadX * 2.0f);
    };

    float requiredWidth = barPadX * 2.0f;
    for (std::size_t i = 0; i < crumbs.size(); ++i) {
        requiredWidth += naturalCrumbWidth(crumbs[i]);
        if (i != 0) {
            requiredWidth += separatorWidth;
        }
    }

    std::vector<BreadcrumbCrumb> visibleCrumbs = crumbs;
    if (crumbs.size() > 3 && requiredWidth > availableWidth) {
        visibleCrumbs.clear();
        visibleCrumbs.push_back(crumbs.front());
        visibleCrumbs.push_back(BreadcrumbCrumb{ nullptr, "...", false, true });
        visibleCrumbs.insert(visibleCrumbs.end(), crumbs.end() - 2, crumbs.end());
    }

    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImRect barRect(start, ImVec2(start.x + availableWidth, start.y + barHeight));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(barRect.Min, barRect.Max, ImGui::GetColorU32(visual.searchBg), ScaleUi(6.0f));
    drawList->AddRect(barRect.Min, barRect.Max, ImGui::GetColorU32(WithAlpha(visual.panelBorder, 0.28f)), ScaleUi(6.0f));

    ImGui::PushClipRect(barRect.Min, barRect.Max, true);
    float x = barRect.Min.x + barPadX;
    const float y = barRect.Min.y + std::floor((barHeight - ImGui::GetFrameHeight()) * 0.5f);
    for (std::size_t i = 0; i < visibleCrumbs.size(); ++i) {
        BreadcrumbCrumb& crumb = visibleCrumbs[i];
        if (i != 0) {
            const ImVec2 separatorSize = ImGui::CalcTextSize(ui_icons::ChevronRight);
            drawList->AddText(
                ImVec2(
                    std::floor(x + chipGap),
                    std::floor(barRect.Min.y + (barHeight - separatorSize.y) * 0.5f)),
                ImGui::GetColorU32(visual.faintText),
                ui_icons::ChevronRight);
            x += separatorWidth;
        }

        const std::size_t remainingCrumbs = visibleCrumbs.size() - i - 1;
        const float reservedForRest =
            remainingCrumbs == 0 ? 0.0f : remainingCrumbs * minChipWidth + remainingCrumbs * separatorWidth;
        const float remainingWidth = std::max(0.0f, barRect.Max.x - barPadX - x - reservedForRest);
        if (remainingWidth < minChipWidth) {
            break;
        }

        const float naturalWidth = naturalCrumbWidth(crumb);
        const float chipWidth = std::max(minChipWidth, std::min(naturalWidth, remainingWidth));
        const ImRect chipRect(ImVec2(x, y), ImVec2(x + chipWidth, y + ImGui::GetFrameHeight()));
        const std::string clippedLabel = EllipsizeText(
            crumb.label,
            std::max(0.0f, chipRect.GetWidth() - chipPadX * 2.0f));

        ImGui::SetCursorScreenPos(chipRect.Min);
        const std::string id = crumb.ellipsis
            ? "##binder_breadcrumb_ellipsis"
            : "##binder_breadcrumb_" + std::to_string(crumb.folder ? crumb.folder->id : 0);
        ImGui::InvisibleButton(id.c_str(), chipRect.GetSize());
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const bool held = ImGui::IsItemActive();
        if (!crumb.ellipsis && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            OpenFolder(crumb.folder, true);
        }

        ImVec4 chipBg = hovered ? visual.rowHover : WithAlpha(visual.rowHover, 0.0f);
        if (held) {
            chipBg = visual.rowSelectedHover;
        } else if (crumb.current) {
            chipBg = visual.rowSelected;
        }
        if (chipBg.w > 0.0f) {
            drawList->AddRectFilled(chipRect.Min, chipRect.Max, ImGui::GetColorU32(chipBg), ScaleUi(4.0f));
        }
        if (crumb.current) {
            drawList->AddRect(chipRect.Min, chipRect.Max, ImGui::GetColorU32(WithAlpha(visual.panelBorder, 0.34f)), ScaleUi(4.0f));
        }

        if (!crumb.ellipsis) {
            DrawExplorerDirectoryDropTarget(
                chipRect,
                crumb.folder,
                "explorer_breadcrumb_drop",
                true,
                true,
                true);
        }

        const ImVec4 textColor = crumb.current ? visual.headerText : visual.mutedText;
        const ImVec2 labelSize = ImGui::CalcTextSize(clippedLabel.c_str());
        drawList->AddText(
            ImVec2(
                std::floor(chipRect.Min.x + chipPadX),
                std::floor(chipRect.Min.y + (chipRect.GetHeight() - labelSize.y) * 0.5f)),
            ImGui::GetColorU32(textColor),
            clippedLabel.c_str());

        if (!ActiveExplorerDragPayload() && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s", fullPath.c_str());
        }
        x = chipRect.Max.x;
    }
    ImGui::PopClipRect();

    ImGui::SetCursorScreenPos(ImVec2(barRect.Min.x, barRect.Max.y));
}

void BinderModule::Impl::DrawExplorerToolbar() {
    UiSettings& ui = UiSettings::Instance();
    const ImGuiStyle& style = ImGui::GetStyle();
    const BinderListVisualStyle& visual = BinderListStyleTokens();
    const float buttonSide = ImGui::GetFrameHeight();
    const float itemGap = ScaleUi(4.0f);
    const float groupGap = ScaleUi(8.0f);
    const ImVec2 buttonSize(buttonSide, buttonSide);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float toolbarWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    ImGui::GetWindowDrawList()->AddRectFilled(
        start,
        ImVec2(start.x + toolbarWidth, start.y + buttonSide),
        ImGui::GetColorU32(WithAlpha(visual.searchBg, 0.72f)),
        ScaleUi(6.0f));
    ImGui::GetWindowDrawList()->AddRect(
        start,
        ImVec2(start.x + toolbarWidth, start.y + buttonSide),
        ImGui::GetColorU32(WithAlpha(visual.panelBorder, 0.22f)),
        ScaleUi(6.0f),
        0,
        ScaleUi(1.0f));

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(itemGap, style.ItemSpacing.y));

    bool firstItem = true;
    const auto nextToolbarItem = [&](const float spacing) {
        if (firstItem) {
            firstItem = false;
            return;
        }
        ImGui::SameLine(0.0f, spacing);
    };

    const auto drawToolbarIconButton = [&](const char* icon, const char* id, const char* tooltip, const bool enabled) {
        nextToolbarItem(itemGap);
        return BinderListFlatIconButton(icon, id, tooltip, buttonSize, ImVec4(0.0f, 0.0f, 0.0f, -1.0f), enabled);
    };
    const auto drawToolbarTextButton = [&](const char* icon, const char* label, const char* id, const char* tooltip) {
        nextToolbarItem(itemGap);
        const float width = std::max(buttonSide, BinderListTextActionButtonWidth(icon, label));
        return BinderListTextActionButton(icon, label, id, tooltip, ImVec2(width, buttonSide));
    };

    const bool backDisabled = ActiveNavigationBackStack().empty();
    if (drawToolbarIconButton(ui_icons::ChevronLeft, "##binder_back", ui.Text(UiText::EditorBack), !backDisabled)) {
        NavigateBack();
    }

    const bool upDisabled = currentFolder == nullptr;
    if (drawToolbarIconButton(ui_icons::AngleUp, "##binder_up", ui.Text(UiText::BinderGoUp), !upDisabled)) {
        NavigateUp();
    }

    if (drawToolbarTextButton(ui_icons::Keyboard, ui.Text(UiText::AddBind), "##explorer_add_bind", ui.Text(UiText::BinderAddBindTooltip))) {
        StartEditing(-1, true);
    }

    if (drawToolbarTextButton(ui_icons::FolderPlus, ui.Text(UiText::FolderAdd), "##explorer_add_folder", ui.Text(UiText::BinderAddFolderTooltip))) {
        BeginInlineCreateFolder(currentFolder);
    }

    if (folderMoveUndo_) {
        if (drawToolbarIconButton(ui_icons::RotateLeft, "##explorer_folder_undo", ui.Text(UiText::UndoFolderMove), true)) {
            ApplyFolderMoveUndo();
        }
    }

    nextToolbarItem(groupGap);
    DrawBinderListSearchBox("##binder_explorer_search", ui.Text(UiText::BinderSearchGlobal), bindSearch);

    ImGui::PopStyleVar();

    DrawExplorerBreadcrumb();
}

void BinderModule::Impl::DrawExplorerInlineFolderEditContent(
    const ExplorerListLayout& layout,
    const ImRect& rowRect,
    const bool selected) {
    UiSettings& ui = UiSettings::Instance();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const BinderListVisualStyle& visual = BinderListStyleTokens();
    ImVec4 iconColor = selected ? visual.headerText : visual.mutedText;
    DrawCenteredIconGlyph(
        drawList,
        ui_icons::Folder,
        ImRect(ImVec2(layout.iconX, rowRect.Min.y), ImVec2(layout.iconX + layout.iconW, rowRect.Max.y)),
        ImGui::GetColorU32(iconColor));

    const float buttonSide = std::ceil(ImGui::GetFrameHeight() - ScaleUi(1.0f));
    const ImVec2 buttonSize(buttonSide, buttonSide);
    const float buttonGap = ScaleUi(4.0f);
    const float actionGroupWidth = buttonSide * 2.0f + buttonGap;
    const float inputX = layout.nameX + ScaleUi(6.0f);
    const float inputY = rowRect.Min.y + std::floor(std::max(0.0f, (layout.rowHeight - ImGui::GetFrameHeight()) * 0.5f));
    const float inputMaxX = std::max(inputX + ScaleUi(48.0f), layout.actionsX - ScaleUi(8.0f));
    const float inputWidth = std::max(ScaleUi(48.0f), inputMaxX - inputX);

    if (folderInlineEdit.focusPending) {
        ImGui::SetKeyboardFocusHere();
        folderInlineEdit.focusPending = false;
    }
    ImGui::SetCursorScreenPos(ImVec2(inputX, inputY));
    ImGui::SetNextItemWidth(inputWidth);
    const bool submitted = InputTextString(
        "##folder_inline_name",
        folderInlineEdit.name,
        ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue,
        128);
    const bool escapePressed = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    const bool deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SetCursorScreenPos(ImVec2(
        std::floor(layout.actionsX + layout.actionsW - actionGroupWidth),
        std::floor(rowRect.Min.y + std::max(0.0f, (layout.rowHeight - buttonSide) * 0.5f))));
    const bool saveClicked = BinderListFlatIconButton(ui_icons::Check, "##folder_inline_save", ui.Text(UiText::Save), buttonSize, visual.enabled);
    ImGui::SameLine(0.0f, buttonGap);
    const bool cancelClicked = BinderListFlatIconButton(ui_icons::Delete, "##folder_inline_cancel", ui.Text(UiText::Cancel), buttonSize, visual.danger);

    if (cancelClicked || escapePressed) {
        CancelInlineFolderEdit();
        return;
    }
    if (saveClicked || submitted || deactivatedAfterEdit) {
        (void)CommitInlineFolderEdit();
    }
}

void BinderModule::Impl::DrawExplorerInlineFolderEditRow(
    const int rowIndex,
    const ExplorerListLayout& layout,
    const ImRect& rowRect) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    DrawBinderListRowBackground(drawList, rowRect, rowIndex, true, false);

    ImGui::PushID("folder_inline_create");
    DrawExplorerInlineFolderEditContent(layout, rowRect, true);
    ImGui::SetCursorScreenPos(ImVec2(rowRect.Min.x, rowRect.Max.y));
    ImGui::PopID();
}

void BinderModule::Impl::DrawExplorerFolderRow(
    FolderNode& folder,
    const int rowIndex,
    const ExplorerListLayout& layout,
    const ImRect& rowRect) {
    UiSettings& ui = UiSettings::Instance();
    const bool selected = IsExplorerFolderSelected(&folder);
    const BinderListVisualStyle& visual = BinderListStyleTokens();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const bool hovered = ImGui::IsMouseHoveringRect(rowRect.Min, rowRect.Max, true);

    DrawBinderListRowBackground(drawList, rowRect, rowIndex, selected, hovered);

    ImGui::PushID(folder.id);
    if (IsInlineRenamingFolder(&folder)) {
        DrawExplorerInlineFolderEditContent(layout, rowRect, true);
        ImGui::SetCursorScreenPos(ImVec2(rowRect.Min.x, rowRect.Max.y));
        ImGui::PopID();
        return;
    }

    ImGui::SetCursorScreenPos(rowRect.Min);
    const float rowHitWidth = std::max(1.0f, layout.actionsX - rowRect.Min.x - ScaleUi(4.0f));
    const bool clicked = ImGui::InvisibleButton(
        "##folder_row",
        ImVec2(rowHitWidth, layout.rowHeight),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool rowItemHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool doubleClicked = rowItemHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    std::optional<FolderDropZone> dropPreviewZone{};
    const auto drawRowDropPreview = [&](FolderDropZone zone) {
        ImGui::PushClipRect(rowRect.Min, rowRect.Max, true);
        DrawFolderDropPreview(rowRect, zone);
        ImGui::PopClipRect();
    };
    if (clicked || doubleClicked) {
        SelectExplorerFolder(&folder);
    }
    if (doubleClicked) {
        OpenFolder(&folder, true);
    }
    if (selected && explorerSelectionScrollPending) {
        ImGui::SetScrollHereY(0.50f);
        explorerSelectionScrollPending = false;
    }

    if (hovered) {
        if (const ImGuiPayload* payload = ActiveExplorerDragPayload()) {
            const FolderDropZone zone = ResolveFolderDropZone(rowRect);
            if (IsExplorerBindDragPayload(payload)) {
                dropPreviewZone = zone;
            } else {
                int folderId = 0;
                if (TryGetExplorerFolderDragId(payload, folderId)) {
                    const bool into = zone == FolderDropZone::Into;
                    FolderNode* targetFolder = into ? &folder : currentFolder;
                    const int insertIndex = into
                        ? static_cast<int>(folder.items.size())
                        : rowIndex + (zone == FolderDropZone::After ? 1 : 0);
                    if (CanMoveFolderToExplorerDirectory(folderId, targetFolder, insertIndex)) {
                        dropPreviewZone = zone;
                    }
                }
            }
        }
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
        const int folderId = folder.id;
        ImGui::SetDragDropPayload(kFolderDragPayload, &folderId, sizeof(folderId));
        ImGui::TextUnformatted(folder.name.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        const FolderDropZone zone = ResolveFolderDropZone(rowRect);
        const bool into = zone == FolderDropZone::Into;
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kBindDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                dropPreviewZone = zone;
                if (payload->IsDelivery()) {
                    const int hotkeyIndex = *static_cast<const int*>(payload->Data);
                    if (into) {
                        MoveBindToExplorerDirectory(hotkeyIndex, &folder, std::nullopt, "explorer_folder_drop");
                    } else {
                        MoveBindToExplorerDirectory(
                            hotkeyIndex,
                            currentFolder,
                            rowIndex + (zone == FolderDropZone::After ? 1 : 0),
                            "explorer_bind_reorder");
                    }
                }
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kFolderDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                const int folderId = *static_cast<const int*>(payload->Data);
                FolderNode* targetFolder = into ? &folder : currentFolder;
                const int insertIndex = into
                    ? static_cast<int>(folder.items.size())
                    : rowIndex + (zone == FolderDropZone::After ? 1 : 0);
                const bool validTarget = CanMoveFolderToExplorerDirectory(
                    folderId,
                    targetFolder,
                    insertIndex);
                if (validTarget) {
                    dropPreviewZone = zone;
                }
                if (payload->IsDelivery()) {
                    if (!validTarget) {
                        debuglog::WriteError(
                            "[binder] explorer folder drop rejected id=%d target=folder_row zone=%d",
                            folderId,
                            static_cast<int>(zone));
                    } else {
                        if (into) {
                            MoveFolderToExplorerDirectory(
                                folderId,
                                &folder,
                                static_cast<int>(folder.items.size()),
                                "explorer_folder_into",
                                true);
                        } else {
                            MoveFolderToExplorerDirectory(
                                folderId,
                                currentFolder,
                                rowIndex + (zone == FolderDropZone::After ? 1 : 0),
                                "explorer_folder_reorder",
                                true);
                        }
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    const float textY = rowRect.Min.y + std::floor((layout.rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
    ImVec4 iconColor = selected || hovered ? visual.headerText : visual.mutedText;
    const std::string folderIcon = FolderIconGlyph(folder);
    DrawCenteredIconGlyph(
        drawList,
        folderIcon.c_str(),
        ImRect(ImVec2(layout.iconX, rowRect.Min.y), ImVec2(layout.iconX + layout.iconW, rowRect.Max.y)),
        ImGui::GetColorU32(iconColor));

    const bool hasConditions = HasSelectedCondition(folder.conditions);
    const std::string marker = std::string(ui_icons::Sliders);
    const ImVec2 markerSize = hasConditions ? ImGui::CalcTextSize(marker.c_str()) : ImVec2(0.0f, 0.0f);
    const float markerReserve = hasConditions ? markerSize.x + ScaleUi(18.0f) : 0.0f;
    const ImRect nameRect(
        ImVec2(layout.nameX, rowRect.Min.y),
        ImVec2(layout.actionsX - ScaleUi(8.0f), rowRect.Max.y));
    const std::string folderLabel = EllipsizeText(
        folder.name,
        std::max(0.0f, nameRect.GetWidth() - markerReserve));
    ImVec4 textColor = selected || hovered ? visual.headerText : visual.mutedText;
    ImGui::PushClipRect(nameRect.Min, nameRect.Max, true);
    drawList->AddText(
        ImVec2(nameRect.Min.x, textY),
        ImGui::GetColorU32(textColor),
        folderLabel.c_str());
    if (hasConditions) {
        const float markerX = nameRect.Max.x - markerSize.x - ScaleUi(4.0f);
        DrawCenteredIconGlyph(
            drawList,
            marker.c_str(),
            ImRect(ImVec2(markerX, rowRect.Min.y), ImVec2(markerX + markerSize.x, rowRect.Max.y)),
            ImGui::GetColorU32(visual.faintText));
    }
    ImGui::PopClipRect();

    const float buttonSide = std::ceil(ImGui::GetFrameHeight() - ScaleUi(1.0f));
    const ImVec2 buttonSize(buttonSide, buttonSide);
    ImGui::SetCursorScreenPos(ImVec2(
        std::floor(layout.actionsX + layout.actionsW - buttonSide),
        std::floor(rowRect.Min.y + std::max(0.0f, (layout.rowHeight - buttonSide) * 0.5f))));
    if (BinderListFlatIconButton(ui_icons::Bars, "##folder_actions", ui.Text(UiText::ColumnActions), buttonSize)) {
        ImGui::OpenPopup("##binder_folder_actions");
    }
    if (ImGui::BeginPopup("##binder_folder_actions")) {
        if (ImGui::MenuItem(ui.Text(UiText::BinderOpenFolder))) {
            OpenFolder(&folder, true);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem(ui.Text(UiText::AddBind))) {
            OpenFolder(&folder, true);
            StartEditing(-1, true);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem(ui.Text(UiText::FolderAdd))) {
            OpenFolder(&folder, true);
            BeginInlineCreateFolder(&folder);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem(ui.Text(UiText::ActionMoveTo))) {
            SelectExplorerFolder(&folder);
            moveFolderTarget = folder.id;
            moveFolderPopupPending = true;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem(ui.Text(UiText::FolderRename))) {
            SelectExplorerFolder(&folder);
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
        const bool canDelete = CanDeleteFolder(&folder);
        if (!canDelete) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem(ui.Text(UiText::Delete)) && canDelete) {
            SelectExplorerFolder(&folder);
            folderDeleteTarget = &folder;
            folderDeletePopupPending = true;
            ImGui::CloseCurrentPopup();
        }
        if (!canDelete) {
            ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }
    if (dropPreviewZone.has_value()) {
        drawRowDropPreview(*dropPreviewZone);
    }
    ImGui::SetCursorScreenPos(ImVec2(rowRect.Min.x, rowRect.Max.y));
    ImGui::PopID();
}

void BinderModule::Impl::DrawExplorerBindRow(
    const int index,
    const int rowIndex,
    const ExplorerListLayout& layout,
    const ImRect& rowRect,
    const bool acceptFolderPayload) {
    if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
        return;
    }

    HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
    UiSettings& ui = UiSettings::Instance();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float iconButtonSide = std::ceil(ImGui::GetFrameHeight() - ScaleUi(1.0f));
    const ImVec2 iconButtonSize(iconButtonSide, iconButtonSide);
    const float actionButtonGap = ScaleUi(4.0f);
    const bool rowHovered = ImGui::IsMouseHoveringRect(rowRect.Min, rowRect.Max, true);
    const bool selected = IsExplorerBindSelected(index);
    const bool modernVisual = layout.modernVisual;
    const bool isRunning = IsHotkeyRunning(index);
    const bool isPaused = IsHotkeyPaused(index);

    if (modernVisual) {
        DrawBinderListRowBackground(drawList, rowRect, rowIndex, selected, rowHovered);
    } else {
        ImVec4 rowBg = style.Colors[ImGuiCol_HeaderHovered];
        rowBg.w = rowHovered ? 0.14f : 0.0f;
        if (rowIndex % 2 != 0 && !rowHovered && !selected) {
            rowBg = style.Colors[ImGuiCol_FrameBg];
            rowBg.w = 0.10f;
        }
        if (selected) {
            rowBg = style.Colors[ImGuiCol_HeaderActive];
            rowBg.w = 0.26f;
        }
        if (rowBg.w > 0.0f) {
            drawList->AddRectFilled(rowRect.Min, rowRect.Max, ImGui::GetColorU32(rowBg), ScaleUi(2.0f));
        }
        drawList->AddLine(
            ImVec2(rowRect.Min.x, rowRect.Max.y),
            rowRect.Max,
            ImGui::GetColorU32(ImGuiCol_Border, 0.24f));
    }

    ImGui::PushID(index);

    const ImRect rowHitRect(
        rowRect.Min,
        ImVec2(std::max(rowRect.Min.x + ScaleUi(1.0f), layout.actionsX - ScaleUi(4.0f)), rowRect.Max.y));
    ImGui::SetCursorScreenPos(rowHitRect.Min);
    const bool bindClicked = ImGui::InvisibleButton(
        "##bind_row",
        rowHitRect.GetSize(),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool bindHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool showBindTooltip = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
    std::optional<FolderDropZone> dropPreviewZone{};
    const auto drawRowDropPreview = [&](FolderDropZone zone) {
        ImGui::PushClipRect(rowRect.Min, rowRect.Max, true);
        DrawFolderDropPreview(rowRect, zone);
        ImGui::PopClipRect();
    };
    if (bindClicked) {
        SelectExplorerBind(index);
    }

    ImVec4 iconColor = style.Colors[hotkey.enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled];
    if (modernVisual) {
        const BinderListVisualStyle& visual = BinderListStyleTokens();
        iconColor = !hotkey.enabled
            ? visual.faintText
            : isPaused
                ? visual.paused
                : isRunning
                    ? visual.running
                    : (selected || bindHovered ? visual.headerText : visual.mutedText);
    } else {
        iconColor.w = hotkey.enabled ? 0.92f : 0.62f;
        if (selected || bindHovered) {
            iconColor.w = hotkey.enabled ? 1.0f : 0.78f;
        }
    }
    const std::string bindIcon = BindIconGlyph(hotkey);
    DrawCenteredIconGlyph(
        drawList,
        bindIcon.c_str(),
        ImRect(ImVec2(layout.iconX, rowRect.Min.y), ImVec2(layout.iconX + layout.iconW, rowRect.Max.y)),
        ImGui::GetColorU32(iconColor));

    const ImRect bindRect(
        ImVec2(layout.nameX, rowRect.Min.y),
        ImVec2(layout.nameX + layout.nameW, rowRect.Max.y));
    const float bindPadX = 0.0f;
    const float textAvailableWidth = std::max(0.0f, bindRect.GetWidth() - bindPadX * 2.0f);
    const std::string displayLabel = BuildBindDisplayLabel(hotkey);
    const std::string bindTitle = "\xE2\x84\x96" + std::to_string(hotkey.number) + " " + displayLabel;
    const LaunchCellContent launchContent = BuildLaunchCellContent(hotkey);
    const std::string launchRaw = launchContent.primary.empty() ? std::string{} : " (" + launchContent.primary + ")";
    const float launchGap = ScaleUi(6.0f);
    const float titleWidth = ImGui::CalcTextSize(bindTitle.c_str()).x;
    const float minLaunchWidth = ScaleUi(42.0f);
    const float remainingForLaunch = textAvailableWidth - titleWidth - launchGap;
    const bool drawLaunch = !launchRaw.empty() && remainingForLaunch >= minLaunchWidth;
    const std::string launchLabel = drawLaunch
        ? EllipsizeText(launchRaw, std::max(0.0f, remainingForLaunch))
        : std::string{};
    const float launchWidth = drawLaunch ? ImGui::CalcTextSize(launchLabel.c_str()).x : 0.0f;
    const float titleMaxWidth = std::max(0.0f, textAvailableWidth - (drawLaunch ? launchWidth + launchGap : 0.0f));
    const std::string bindName = EllipsizeText(bindTitle, titleMaxWidth);
    ImVec4 bindNameColor = style.Colors[hotkey.enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled];
    bindNameColor.w = hotkey.enabled ? 0.96f : 0.82f;
    ImVec4 launchTextColor = style.Colors[ImGuiCol_TextDisabled];
    launchTextColor.w = hotkey.enabled ? 0.78f : 0.54f;
    if (modernVisual) {
        const BinderListVisualStyle& visual = BinderListStyleTokens();
        bindNameColor = !hotkey.enabled ? WithAlpha(visual.mutedText, 0.64f) : visual.headerText;
        launchTextColor = !hotkey.enabled ? WithAlpha(visual.faintText, 0.58f) : visual.faintText;
        if (!selected && !bindHovered && hotkey.enabled) {
            bindNameColor.w = 0.92f;
        }
        if (selected || bindHovered) {
            bindNameColor.w = hotkey.enabled ? 1.0f : 0.72f;
            launchTextColor.w = hotkey.enabled ? 0.86f : 0.62f;
        }
    } else if (selected) {
        bindNameColor = style.Colors[ImGuiCol_Text];
        launchTextColor.w = 0.86f;
    } else if (bindHovered && hotkey.enabled) {
        bindNameColor.w = 1.00f;
        launchTextColor.w = 0.88f;
    }

    ImGui::PushClipRect(bindRect.Min, bindRect.Max, true);
    const float textStartX = bindRect.Min.x + bindPadX;
    const float bindTextY = rowRect.Min.y + (rowRect.GetHeight() - ImGui::GetTextLineHeight()) * 0.5f;
    drawList->AddText(
        ImVec2(textStartX, bindTextY),
        ImGui::GetColorU32(bindNameColor),
        bindName.c_str());
    if (drawLaunch) {
        drawList->AddText(
            ImVec2(textStartX + ImGui::CalcTextSize(bindName.c_str()).x + launchGap, bindTextY),
            ImGui::GetColorU32(launchTextColor),
            launchLabel.c_str());
    }
    ImGui::PopClipRect();
    if (bindHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        SelectExplorerBind(index);
        StartEditing(index, false);
    }
    if (selected && explorerSelectionScrollPending) {
        ImGui::SetScrollHereY(0.50f);
        explorerSelectionScrollPending = false;
    }
    if (rowHovered) {
        if (const ImGuiPayload* payload = ActiveExplorerDragPayload()) {
            FolderDropZone zone = ResolveFolderDropZone(rowRect);
            if (zone == FolderDropZone::Into) {
                zone = FolderDropZone::After;
            }
            if (IsExplorerBindDragPayload(payload)) {
                dropPreviewZone = zone;
            } else if (acceptFolderPayload) {
                int folderId = 0;
                if (TryGetExplorerFolderDragId(payload, folderId)) {
                    const int insertIndex = rowIndex + (zone == FolderDropZone::After ? 1 : 0);
                    if (CanMoveFolderToExplorerDirectory(folderId, currentFolder, insertIndex)) {
                        dropPreviewZone = zone;
                    }
                }
            }
        }
    }
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
        const int hotkeyIndex = index;
        ImGui::SetDragDropPayload(kBindDragPayload, &hotkeyIndex, sizeof(hotkeyIndex));
        ImGui::TextUnformatted(bindName.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        FolderDropZone zone = ResolveFolderDropZone(rowRect);
        if (zone == FolderDropZone::Into) {
            zone = FolderDropZone::After;
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kBindDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                dropPreviewZone = zone;
                if (payload->IsDelivery()) {
                    const int hotkeyIndex = *static_cast<const int*>(payload->Data);
                    MoveBindToExplorerDirectory(
                        hotkeyIndex,
                        currentFolder,
                        rowIndex + (zone == FolderDropZone::After ? 1 : 0),
                        "explorer_bind_reorder");
                }
            }
        }
        if (acceptFolderPayload) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kFolderDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                    const int folderId = *static_cast<const int*>(payload->Data);
                    const int insertIndex = rowIndex + (zone == FolderDropZone::After ? 1 : 0);
                    const bool validTarget = CanMoveFolderToExplorerDirectory(folderId, currentFolder, insertIndex);
                    if (validTarget) {
                        dropPreviewZone = zone;
                    }
                    if (payload->IsDelivery()) {
                        if (!validTarget) {
                            debuglog::WriteError(
                                "[binder] explorer folder drop rejected id=%d target=bind_row zone=%d",
                                folderId,
                                static_cast<int>(zone));
                        } else {
                            MoveFolderToExplorerDirectory(
                                folderId,
                                currentFolder,
                                rowIndex + (zone == FolderDropZone::After ? 1 : 0),
                                "explorer_folder_reorder",
                                true);
                        }
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (showBindTooltip) {
        std::vector<std::string> tooltipLines{
            ui.Format(UiText::BindListEntryFormat, hotkey.number, displayLabel.c_str()),
        };
        const std::vector<std::string> launchLabels = BuildLaunchLabels(hotkey);
        if (!launchLabels.empty()) {
            tooltipLines.push_back(JoinLaunchLabels(launchLabels, "\n"));
        }
        ImGui::SetTooltip("%s", JoinLaunchLabels(tooltipLines, "\n").c_str());
    }

    const int actionButtonCount = isRunning ? 5 : 4;
    const float actionGroupWidth =
        iconButtonSize.x * static_cast<float>(actionButtonCount)
        + actionButtonGap * static_cast<float>(std::max(actionButtonCount - 1, 0));
    const auto drawActionButton = [&](const char* icon, const char* id, const char* tooltip, ImVec4 color = ImVec4(0.0f, 0.0f, 0.0f, -1.0f)) {
        return modernVisual
            ? BinderListFlatIconButton(icon, id, tooltip, iconButtonSize, color)
            : SmallIconActionButton(icon, id, tooltip, iconButtonSize);
    };
    ImGui::SetCursorScreenPos(ImVec2(
        std::floor(layout.actionsX + layout.actionsW - actionGroupWidth),
        std::floor(rowRect.Min.y + std::max(0.0f, (layout.rowHeight - iconButtonSize.y) * 0.5f))));
    if (drawActionButton(
            hotkey.enabled ? ui_icons::ToggleOn : ui_icons::ToggleOff,
            "##enabled",
            ui.Text(UiText::Enabled),
            hotkey.enabled ? BinderListStyleTokens().enabled : BinderListStyleTokens().faintText)) {
        hotkey.enabled = !hotkey.enabled;
        SaveConfig();
    }
    ImGui::SameLine(0.0f, actionButtonGap);
    if (!isRunning) {
        ImGui::BeginDisabled(!hotkey.enabled);
        if (drawActionButton(ui_icons::Play, "##run", ui.Text(UiText::Run), BinderListStyleTokens().running)) {
            TryEnqueueHotkey(index, 0, "manual", "");
        }
        ImGui::EndDisabled();
    } else if (isPaused) {
        if (drawActionButton(ui_icons::Play, "##resume", ui.Text(UiText::Resume), BinderListStyleTokens().running)) {
            ResumeHotkey(index);
        }
        ImGui::SameLine(0.0f, actionButtonGap);
        if (drawActionButton(ui_icons::Stop, "##stop", ui.Text(UiText::Stop), BinderListStyleTokens().danger)) {
            StopHotkey(index);
        }
    } else {
        if (drawActionButton(ui_icons::Pause, "##pause", ui.Text(UiText::Pause), BinderListStyleTokens().paused)) {
            PauseHotkey(index);
        }
        ImGui::SameLine(0.0f, actionButtonGap);
        if (drawActionButton(ui_icons::Stop, "##stop", ui.Text(UiText::Stop), BinderListStyleTokens().danger)) {
            StopHotkey(index);
        }
    }
    ImGui::SameLine(0.0f, actionButtonGap);
    if (drawActionButton(ui_icons::Edit, "##edit", ui.Text(UiText::Edit))) {
        SelectExplorerBind(index);
        StartEditing(index, false);
    }
    ImGui::SameLine(0.0f, actionButtonGap);
    if (drawActionButton(ui_icons::Bars, "##more", ui.Text(UiText::ColumnActions))) {
        ImGui::OpenPopup("##binder_bind_actions");
    }
    if (ImGui::BeginPopup("##binder_bind_actions")) {
        if (!hotkey.enabled) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem(ui.Text(UiText::ShowInQuickMenu), nullptr, hotkey.quickMenu)) {
            hotkey.quickMenu = !hotkey.quickMenu;
            SaveConfig();
        }
        if (!hotkey.enabled) {
            ImGui::EndDisabled();
        }
        if (ImGui::MenuItem(ui.Text(UiText::ActionMoveTo))) {
            SelectExplorerBind(index);
            moveBindTarget = index;
            moveBindPopupPending = true;
        }
        if (ImGui::MenuItem(ui.Text(UiText::ActionDuplicate))) {
            SelectExplorerBind(index);
            DuplicateHotkeyAt(index);
        }
        if (ImGui::MenuItem(ui.Text(UiText::ActionBindLines))) {
            SelectExplorerBind(index);
            bindLinesTarget = index;
            bindLinesPopupPending = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ui.Text(UiText::Delete))) {
            SelectExplorerBind(index);
            bindDeleteTarget = index;
            bindDeletePopupPending = true;
        }
        ImGui::EndPopup();
    }

    if (dropPreviewZone.has_value()) {
        drawRowDropPreview(*dropPreviewZone);
    }
    ImGui::SetCursorScreenPos(ImVec2(rowRect.Min.x, rowRect.Max.y));
    ImGui::PopID();
}

void BinderModule::Impl::DrawExplorerKeyboardShortcuts(const bool focused) {
    if (!focused || ImGui::GetActiveID() != 0) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !bindSearch.empty()) {
        bindSearch.clear();
        return;
    }

    if (!Trim(bindSearch).empty()) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
        MoveExplorerSelection(-1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
        MoveExplorerSelection(1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        const int bindIndex = explorerSelection.kind == ExplorerSelectionKind::Bind
            ? FindHotkeyIndexByOrderId(explorerSelection.bindOrderId)
            : selectedBindIndex;
        if (bindIndex >= 0 && bindIndex < static_cast<int>(hotkeys.size())) {
            StartEditing(bindIndex, false);
        } else if (selectedFolder && selectedFolder != currentFolder) {
            OpenFolder(selectedFolder, true);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
        NavigateUp();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        const int bindIndex = explorerSelection.kind == ExplorerSelectionKind::Bind
            ? FindHotkeyIndexByOrderId(explorerSelection.bindOrderId)
            : selectedBindIndex;
        if (bindIndex >= 0 && bindIndex < static_cast<int>(hotkeys.size())) {
            bindDeleteTarget = bindIndex;
            bindDeletePopupPending = true;
        } else if (CanDeleteFolder(selectedFolder)) {
            folderDeleteTarget = selectedFolder;
            folderDeletePopupPending = true;
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        if (selectedFolder && selectedFolder != currentFolder) {
            BeginInlineRenameFolder(selectedFolder);
        }
    }
}

void BinderModule::Impl::DrawExplorerSearchResults() {
    const std::string query = ToLower(Trim(bindSearch));
    if (query.empty()) {
        return;
    }
    UiSettings& ui = UiSettings::Instance();
    const BinderListVisualStyle& visual = BinderListStyleTokens();

    struct SearchResult {
        std::string categoryId;
        ExplorerItem item;
        std::vector<std::string> folderPath;
        bool categoryOnly = false;
    };

    std::vector<SearchResult> results;
    for (const BinderCategory& category : categories) {
        const std::string hay = ToLower(category.name);
        if (hay.find(query) != std::string::npos) {
            results.push_back(SearchResult{
                category.id,
                ExplorerItem{ ExplorerItemKind::Folder, category.name },
                {},
                true,
            });
        }
    }

    const auto collect = [&](auto&& self, const BinderCategory& category, FolderNode* folder) -> void {
        const std::vector<std::string> path = FolderPathForDirectory(folder);
        const std::vector<ExplorerItem>& items = folder ? folder->items : category.rootItems;
        for (const ExplorerItem& item : items) {
            if (item.kind == ExplorerItemKind::Folder) {
                FolderNode* child = FindFolderByNameInDirectory(category, folder, item.key);
                if (!child) {
                    continue;
                }
                const std::string hay = ToLower(category.name + " " + child->name + " " + JoinPath(BuildFolderPath(child)));
                if (hay.find(query) != std::string::npos) {
                    results.push_back(SearchResult{ category.id, item, path, false });
                }
                self(self, category, child);
            } else {
                const int index = FindHotkeyIndexByOrderId(item.key);
                if (index < 0) {
                    continue;
                }
                const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
                if (hotkey.categoryId != category.id) {
                    continue;
                }
                const std::string hay = ToLower(category.name + " " + hotkey.label + " " + hotkey.command + " " + JoinPath(hotkey.folderPath));
                if (hay.find(query) != std::string::npos) {
                    results.push_back(SearchResult{ category.id, item, path, false });
                }
            }
        }
    };
    for (const BinderCategory& category : categories) {
        collect(collect, category, nullptr);
    }

    if (results.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, visual.faintText);
        ImGui::TextUnformatted(ui.Text(UiText::MiscVariablesCatalogEmpty));
        ImGui::PopStyleColor();
        return;
    }

    const auto resultBelongsToActiveCategory = [&](const SearchResult& result) {
        return result.categoryId == ActiveCategory().id;
    };
    const auto findResultFolder = [&](const SearchResult& result) -> FolderNode* {
        if (!resultBelongsToActiveCategory(result) || result.item.kind != ExplorerItemKind::Folder || result.categoryOnly) {
            return nullptr;
        }
        FolderNode* parent = result.folderPath.empty() ? nullptr : FindFolderByPath(ActiveFolders(), result.folderPath);
        return FindFolderByNameInDirectory(parent, result.item.key);
    };
    const auto findResultPathFolder = [&](const SearchResult& result) -> FolderNode* {
        if (!resultBelongsToActiveCategory(result) || result.folderPath.empty()) {
            return nullptr;
        }
        return FindFolderByPath(ActiveFolders(), result.folderPath);
    };
    const auto beginSearchDragSource = [&](const SearchResult& result, const std::string& label) {
        if (!resultBelongsToActiveCategory(result) || result.categoryOnly) {
            return;
        }
        if (result.item.kind == ExplorerItemKind::Folder) {
            FolderNode* folder = findResultFolder(result);
            if (!folder) {
                return;
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
                const int folderId = folder->id;
                ImGui::SetDragDropPayload(kFolderDragPayload, &folderId, sizeof(folderId));
                ImGui::TextUnformatted(label.c_str());
                ImGui::EndDragDropSource();
            }
            return;
        }

        const int index = FindHotkeyIndexByOrderId(result.item.key);
        if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
            return;
        }
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
            ImGui::SetDragDropPayload(kBindDragPayload, &index, sizeof(index));
            ImGui::TextUnformatted(label.c_str());
            ImGui::EndDragDropSource();
        }
    };
    const auto drawPathCell = [&](const SearchResult& result, const std::string& pathText) {
        const ImVec2 cellPos = ImGui::GetCursorScreenPos();
        const float cellWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float cellHeight = std::max(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetFrameHeight());
        const ImRect cellRect(cellPos, ImVec2(cellPos.x + cellWidth, cellPos.y + cellHeight));
        ImGui::InvisibleButton("##search_path_drop", cellRect.GetSize());

        const float padX = ScaleUi(4.0f);
        const std::string clipped = EllipsizeText(pathText, std::max(0.0f, cellWidth - padX * 2.0f));
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(cellRect.Min.x + padX, cellRect.Min.y + std::floor((cellRect.GetHeight() - ImGui::GetTextLineHeight()) * 0.5f)),
            ImGui::GetColorU32(visual.faintText),
            clipped.c_str());
        if (!ActiveExplorerDragPayload()
            && clipped != pathText
            && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s", pathText.c_str());
        }

        const bool activeTarget = resultBelongsToActiveCategory(result)
            && (result.folderPath.empty() || findResultPathFolder(result) != nullptr);
        DrawExplorerDirectoryDropTarget(
            cellRect,
            result.folderPath.empty() ? nullptr : findResultPathFolder(result),
            "explorer_search_path_drop",
            activeTarget,
            false,
            true);
    };

    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, WithAlpha(visual.searchBg, 0.48f));
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, visual.separator);
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, WithAlpha(visual.panelBorder, 0.22f));
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, WithAlpha(visual.rowAlt, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, visual.rowAlt);
    ImGui::PushStyleColor(ImGuiCol_Header, visual.rowSelected);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, visual.rowHover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, visual.rowSelectedHover);
    if (ImGui::BeginTable(
            "##binder_explorer_search",
            2,
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
            ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn(ui.Text(UiText::ColumnName), ImGuiTableColumnFlags_WidthStretch, 0.70f);
        ImGui::TableSetupColumn(ui.Text(UiText::Folder), ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableHeadersRow();
        int row = 0;
        for (const SearchResult& result : results) {
            ImGui::PushID(row++);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (result.categoryOnly) {
                const std::string label = std::string(ui_icons::Folder) + " " + result.item.key;
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAvailWidth | ImGuiSelectableFlags_AllowDoubleClick)
                    && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    SelectCategory(result.categoryId);
                    OpenFolder(nullptr, true);
                    bindSearch.clear();
                }
            } else if (result.item.kind == ExplorerItemKind::Folder) {
                FolderNode* resultFolder = findResultFolder(result);
                const std::string label = (resultFolder ? FolderIconGlyph(*resultFolder) : std::string(ui_icons::Folder)) + " " + result.item.key;
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAvailWidth | ImGuiSelectableFlags_AllowDoubleClick)
                    && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    SelectCategory(result.categoryId);
                    const BinderCategory& category = ActiveCategory();
                    FolderNode* folder = FindFolderByNameInDirectory(
                        category,
                        result.folderPath.empty() ? nullptr : FindFolderByPath(category.folders, result.folderPath),
                        result.item.key);
                    if (folder) {
                        OpenFolder(folder, true);
                        bindSearch.clear();
                    }
                }
                const ImRect nameRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                beginSearchDragSource(result, label);
                DrawExplorerDirectoryDropTarget(
                    nameRect,
                    findResultFolder(result),
                    "explorer_search_folder_drop",
                    resultBelongsToActiveCategory(result) && findResultFolder(result) != nullptr,
                    false,
                    true);
            } else {
                const int index = FindHotkeyIndexByOrderId(result.item.key);
                const std::string label = index >= 0
                    ? BindIconGlyph(hotkeys[static_cast<std::size_t>(index)]) + " " + hotkeys[static_cast<std::size_t>(index)].label
                    : std::string(ui_icons::Keyboard);
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAvailWidth | ImGuiSelectableFlags_AllowDoubleClick)
                    && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && index >= 0) {
                    SelectCategory(result.categoryId);
                    SelectExplorerBind(index);
                    bindSearch.clear();
                    StartEditing(index, false);
                }
                beginSearchDragSource(result, label);
            }
            ImGui::TableSetColumnIndex(1);
            const BinderCategory* category = FindCategoryById(result.categoryId);
            std::string path = category ? category->name : std::string{};
            if (!result.folderPath.empty()) {
                path += " / " + JoinPath(result.folderPath);
            }
            drawPathCell(result, path);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleColor(8);
}

void BinderModule::Impl::DrawExplorerEmptyAreaContextMenu(const char* popupId) {
    if (ImGui::BeginPopupContextItem(popupId)) {
        UiSettings& ui = UiSettings::Instance();
        if (ImGui::MenuItem(ui.Text(UiText::AddBind))) {
            StartEditing(-1, true);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem(ui.Text(UiText::FolderAdd))) {
            BeginInlineCreateFolder(currentFolder);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void BinderModule::Impl::DrawExplorerDirectory() {
    NormalizeExplorerOrders();
    const std::vector<ExplorerItem> items = ItemsForFolder(currentFolder);

    UiSettings& ui = UiSettings::Instance();
    const BinderListVisualStyle& visual = BinderListStyleTokens();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float iconButtonSide = std::ceil(ImGui::GetFrameHeight() - ScaleUi(1.0f));
    const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float columnGap = ScaleUi(8.0f);
    const float minNameWidth = ScaleUi(90.0f);

    ExplorerListLayout layout;
    layout.width = availableWidth;
    layout.rowHeight = std::max(ImGui::GetFrameHeight() + ScaleUi(2.0f), ScaleUi(28.0f));
    layout.headerHeight = std::max(ImGui::GetTextLineHeight() + ScaleUi(10.0f), ScaleUi(26.0f));
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
        ImGui::GetColorU32(visual.separator));

    const auto drawHeaderLabel = [&](const char* label, float x, float width, const char* tooltip, bool centered) {
        if (label == nullptr) {
            label = "";
        }
        const ImRect cell(ImVec2(x, headerRect.Min.y), ImVec2(x + width, headerRect.Max.y));
        const float padX = ScaleUi(6.0f);
        const std::string clipped = EllipsizeText(label, std::max(0.0f, cell.GetWidth() - padX * 2.0f));
        const ImVec2 labelSize = ImGui::CalcTextSize(clipped.c_str());
        const float textX = centered
            ? std::floor(cell.Min.x + std::max(0.0f, (cell.GetWidth() - labelSize.x) * 0.5f))
            : std::floor(cell.Min.x + padX);
        const float textY = std::floor(cell.Min.y + std::max(0.0f, (cell.GetHeight() - labelSize.y) * 0.5f));
        ImGui::PushClipRect(cell.Min, cell.Max, true);
        drawList->AddText(ImVec2(textX, textY), ImGui::GetColorU32(visual.faintText), clipped.c_str());
        ImGui::PopClipRect();
        if (tooltip != nullptr && tooltip[0] != '\0' && ImGui::IsMouseHoveringRect(cell.Min, cell.Max, true)) {
            ImGui::SetTooltip("%s", tooltip);
        }
    };

    drawHeaderLabel("", layout.iconX, layout.iconW, nullptr, true);
    drawHeaderLabel(ui.Text(UiText::ColumnName), layout.nameX, layout.nameW, nullptr, false);
    drawHeaderLabel(ui.Text(UiText::ColumnActions), layout.actionsX, layout.actionsW, nullptr, true);

    ImGui::Dummy(ImVec2(layout.width, layout.headerHeight));

    int rowIndex = 0;
    for (const ExplorerItem& item : items) {
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        const ImRect rowRect(rowPos, ImVec2(rowPos.x + layout.width, rowPos.y + layout.rowHeight));
        bool rowDrawn = false;
        if (item.kind == ExplorerItemKind::Folder) {
            if (FolderNode* folder = FindFolderByNameInDirectory(currentFolder, item.key)) {
                DrawExplorerFolderRow(*folder, rowIndex, layout, rowRect);
                rowDrawn = true;
            }
        } else {
            const int hotkeyIndex = FindHotkeyIndexByOrderId(item.key);
            if (hotkeyIndex >= 0) {
                DrawExplorerBindRow(hotkeyIndex, rowIndex, layout, rowRect);
                rowDrawn = true;
            }
        }
        if (rowDrawn) {
            ++rowIndex;
        }
    }

    const bool creatingInCurrentFolder =
        folderInlineEdit.mode == FolderInlineEditMode::Create && folderInlineEdit.parent == currentFolder;
    if (creatingInCurrentFolder) {
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        const ImRect rowRect(rowPos, ImVec2(rowPos.x + layout.width, rowPos.y + layout.rowHeight));
        DrawExplorerInlineFolderEditRow(rowIndex, layout, rowRect);
        ++rowIndex;
    }

    if (items.empty() && !creatingInCurrentFolder) {
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        const ImRect emptyRect(rowPos, ImVec2(rowPos.x + layout.width, rowPos.y + layout.rowHeight));
        const float textY = emptyRect.Min.y + std::floor((layout.rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
        drawList->AddText(
            ImVec2(emptyRect.Min.x + ScaleUi(12.0f), textY),
            ImGui::GetColorU32(visual.faintText),
            ui.Text(UiText::BinderEmptyFolder));
        ImGui::SetCursorScreenPos(emptyRect.Min);
        ImGui::InvisibleButton("##explorer_empty_message_drop", emptyRect.GetSize());
        const bool emptyMessageHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            ClearExplorerSelection();
        }
        DrawExplorerEmptyAreaContextMenu("##explorer_empty_message_context");
        std::optional<FolderDropZone> emptyMessagePreview{};
        if (emptyMessageHovered) {
            if (const ImGuiPayload* payload = ActiveExplorerDragPayload()) {
                if (IsExplorerBindDragPayload(payload)) {
                    emptyMessagePreview = FolderDropZone::Into;
                } else {
                    int folderId = 0;
                    if (TryGetExplorerFolderDragId(payload, folderId)
                        && CanMoveFolderToExplorerDirectory(folderId, currentFolder, 0)) {
                        emptyMessagePreview = FolderDropZone::Into;
                    }
                }
            }
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kBindDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                    emptyMessagePreview = FolderDropZone::Into;
                    if (payload->IsDelivery()) {
                        MoveBindToExplorerDirectory(
                            *static_cast<const int*>(payload->Data),
                            currentFolder,
                            0,
                            "explorer_empty_drop");
                    }
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kFolderDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                    const int folderId = *static_cast<const int*>(payload->Data);
                    const bool validTarget = CanMoveFolderToExplorerDirectory(folderId, currentFolder, 0);
                    if (validTarget) {
                        emptyMessagePreview = FolderDropZone::Into;
                    }
                    if (payload->IsDelivery()) {
                        if (!validTarget) {
                            debuglog::WriteError(
                                "[binder] explorer folder drop rejected id=%d target=empty_message",
                                folderId);
                        } else {
                            MoveFolderToExplorerDirectory(
                                folderId,
                                currentFolder,
                                0,
                                "explorer_empty_drop",
                                true);
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (emptyMessagePreview.has_value()) {
            DrawFolderDropPreview(emptyRect, *emptyMessagePreview);
        }
        ImGui::SetCursorScreenPos(ImVec2(emptyRect.Min.x, emptyRect.Max.y));
    }

    const float remainingHeight = ImGui::GetContentRegionAvail().y;
    const float bottomSlack = ImGui::GetStyle().ItemSpacing.y + ScaleUi(2.0f);
    if (remainingHeight > ScaleUi(8.0f) + bottomSlack) {
        const ImVec2 dropPos = ImGui::GetCursorScreenPos();
        const float dropHeight = std::max(ScaleUi(8.0f), remainingHeight - bottomSlack);
        const ImRect dropRect(dropPos, ImVec2(dropPos.x + layout.width, dropPos.y + dropHeight));
        ImGui::InvisibleButton("##explorer_empty_area_drop", dropRect.GetSize());
        const bool emptyAreaHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            ClearExplorerSelection();
        }
        DrawExplorerEmptyAreaContextMenu("##explorer_empty_area_context");
        const FolderDropZone emptyAreaZone = items.empty() ? FolderDropZone::Into : FolderDropZone::Before;
        std::optional<FolderDropZone> emptyAreaPreview{};
        if (emptyAreaHovered) {
            if (const ImGuiPayload* payload = ActiveExplorerDragPayload()) {
                if (IsExplorerBindDragPayload(payload)) {
                    emptyAreaPreview = emptyAreaZone;
                } else {
                    int folderId = 0;
                    if (TryGetExplorerFolderDragId(payload, folderId)
                        && CanMoveFolderToExplorerDirectory(folderId, currentFolder, static_cast<int>(items.size()))) {
                        emptyAreaPreview = emptyAreaZone;
                    }
                }
            }
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kBindDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                    emptyAreaPreview = emptyAreaZone;
                    if (payload->IsDelivery()) {
                        MoveBindToExplorerDirectory(
                            *static_cast<const int*>(payload->Data),
                            currentFolder,
                            static_cast<int>(items.size()),
                            "explorer_empty_drop");
                    }
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kFolderDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                    const int folderId = *static_cast<const int*>(payload->Data);
                    const bool validTarget =
                        CanMoveFolderToExplorerDirectory(folderId, currentFolder, static_cast<int>(items.size()));
                    if (validTarget) {
                        emptyAreaPreview = emptyAreaZone;
                    }
                    if (payload->IsDelivery()) {
                        if (!validTarget) {
                            debuglog::WriteError(
                                "[binder] explorer folder drop rejected id=%d target=empty_area",
                                folderId);
                        } else {
                            MoveFolderToExplorerDirectory(
                                folderId,
                                currentFolder,
                                static_cast<int>(items.size()),
                                "explorer_empty_drop",
                                true);
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (emptyAreaPreview.has_value()) {
            DrawFolderDropPreview(dropRect, *emptyAreaPreview);
        }
    }

    // The custom list advances layout with SetCursorScreenPos(); commit the final
    // position even when the list overflows and no empty-area item is emitted.
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void BinderModule::Impl::DrawExplorerPane() {
    EnsureRootFolder();
    DrawExplorerToolbar();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ScaleUi(2.0f));

    const bool searchActive = !Trim(bindSearch).empty();
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
        || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    if (searchActive) {
        DrawExplorerSearchResults();
    } else {
        DrawExplorerDirectory();
    }
    DrawExplorerKeyboardShortcuts(focused);

    DrawBindDeletePopup();
}
