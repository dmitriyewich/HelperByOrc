#include "binder_module_impl.h"

void BinderModule::Impl::CaptureQuickMenuConditionSnapshot() {
    bool sampCursorActive = false;
    if (sampApi) {
        sampCursorActive = sampApi->is_chat_opened() || sampApi->isDialogActive() || sampApi->IsSampCursorActive();
    }

    ConditionRuntimeContext rawContext{};
    rawContext.helperUiCursorActive = false;
    rawContext.gameWindowForeground = gameInputForeground_;

    const bool windowsCursorActive = IsWindowsCursorActiveForConditions(&rawContext);
    quickMenuSampCursorActiveAtOpen = sampCursorActive;
    quickMenuWindowsCursorActiveAtOpen = windowsCursorActive;

    debuglog::WriteInfo(
        "[ui] quickmenu condition snapshot sampCursor=%d windowsCursor=%d",
        sampCursorActive ? 1 : 0,
        windowsCursorActive ? 1 : 0);
}

void BinderModule::Impl::ClearQuickMenuConditionSnapshot() {
    quickMenuSampCursorActiveAtOpen.reset();
    quickMenuWindowsCursorActiveAtOpen.reset();
}

bool BinderModule::Impl::VisibleQuickMenuEntriesExist() const {
    const auto hotkeyVisible = [&](const int index) {
        if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
            return false;
        }
        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
        if (!hotkey.enabled || !hotkey.quickMenu) {
            return false;
        }
        const ConditionRuntimeContext context = MakeConditionContext(quickMenuOpen);
        return !ConditionsBlocked(hotkey.conditions, hotkey.conditionsCombine, sampApi, &context);
    };

    const auto categoryVisible = [&](const BinderCategory& category) {
        const ConditionRuntimeContext context = MakeConditionContext(quickMenuOpen);
        return category.quickMenu && !ConditionsBlocked(category.conditions, category.conditionsCombine, sampApi, &context);
    };

    const auto directoryHasVisibleEntries = [&](auto&& self, const BinderCategory& category, const FolderNode* folder) -> bool {
        const std::vector<ExplorerItem>& items = folder ? folder->items : category.rootItems;
        for (const ExplorerItem& item : items) {
            if (item.kind == ExplorerItemKind::Bind) {
                const int index = FindHotkeyIndexByOrderId(item.key);
                if (index >= 0 && hotkeys[static_cast<std::size_t>(index)].categoryId == category.id && hotkeyVisible(index)) {
                    return true;
                }
                continue;
            }

            FolderNode* child = FindFolderByNameInDirectory(category, const_cast<FolderNode*>(folder), item.key);
            if (child && FolderVisibleInQuickMenu(*child) && self(self, category, child)) {
                return true;
            }
        }
        return false;
    };

    for (const BinderCategory& category : categories) {
        if (categoryVisible(category) && directoryHasVisibleEntries(directoryHasVisibleEntries, category, nullptr)) {
            return true;
        }
    }
    return false;
}

bool BinderModule::Impl::FolderVisibleInQuickMenu(const FolderNode& folder) const {
    const ConditionRuntimeContext context = MakeConditionContext(quickMenuOpen);
    if (!folder.quickMenu || ConditionsBlocked(folder.conditions, folder.conditionsCombine, sampApi, &context)) {
        return false;
    }
    return !folder.parent || FolderVisibleInQuickMenu(*folder.parent);
}

void BinderModule::Impl::CenterQuickMenuCursorOnGameWindow() {
    HWND hwnd = ::GetForegroundWindow();
    if (hwnd == nullptr) {
        return;
    }

    RECT clientRect{};
    if (::GetClientRect(hwnd, &clientRect) == FALSE) {
        return;
    }

    POINT clientPoint{
        (clientRect.right - clientRect.left) / 2,
        (clientRect.bottom - clientRect.top) / 2,
    };
    POINT screenPoint = clientPoint;
    if (::ClientToScreen(hwnd, &screenPoint) == FALSE) {
        return;
    }

    if (::SetCursorPos(screenPoint.x, screenPoint.y) == FALSE) {
        debuglog::WriteError("[ui] quickmenu cursor center failed: hwnd=%p err=%lu", hwnd, ::GetLastError());
        return;
    }

    quickMenuMouseClientPos = ImVec2(static_cast<float>(clientPoint.x), static_cast<float>(clientPoint.y));
    quickMenuMouseClientPosValid = true;
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui::GetIO().AddMousePosEvent(quickMenuMouseClientPos.x, quickMenuMouseClientPos.y);
    }
    debuglog::WriteInfo(
        "[ui] quickmenu cursor centered hwnd=%p client=(%ld,%ld) screen=(%ld,%ld)",
        hwnd,
        clientPoint.x,
        clientPoint.y,
        screenPoint.x,
        screenPoint.y);
}

void BinderModule::Impl::ResetQuickMenuVisualState() {
    quickMenuFocusPending = false;
    quickMenuFocusReassertFrames = 0;
    quickMenuMouseEventPending = false;
    quickMenuMouseEventInside = false;
    quickMenuCloseAfterMouseFrame = false;
    quickMenuMouseClientPosValid = false;
    quickMenuHitItems.clear();
    quickMenuPos = ImVec2(0.0f, 0.0f);
}

void BinderModule::Impl::UpdateQuickMenuState() {
    if (capture.Active()) {
        quickMenuOpen = false;
        ResetQuickMenuVisualState();
        return;
    }

    const bool hasEntries = VisibleQuickMenuEntriesExist();
    if (!hasEntries) {
        quickMenuOpen = false;
        ResetQuickMenuVisualState();
        return;
    }

    if (IsMainWindowHotkeyPressed()) {
        quickMenuOpen = false;
        quickMenuToggleLatch = false;
        ResetQuickMenuVisualState();
        return;
    }

    const bool comboHeld = IsQuickMenuComboPressed();
    const bool mouseButtonHeld = IsQuickMenuMouseButtonHeld();
    quickMenuCloseAfterMouseFrame = false;
    if (quickMenuReopenBlocked) {
        quickMenuOpen = false;
        if (!comboHeld) {
            quickMenuReopenBlocked = false;
            quickMenuToggleLatch = false;
            ResetQuickMenuVisualState();
        }
        return;
    }

    if (quickMenuActivationMode == QuickMenuActivationMode::Toggle) {
        if (comboHeld && !quickMenuToggleLatch) {
            quickMenuToggleLatch = true;
            quickMenuOpen = !quickMenuOpen;
        } else if (!comboHeld) {
            quickMenuToggleLatch = false;
        }
    } else {
        if (comboHeld) {
            quickMenuOpen = true;
        } else if (quickMenuOpen && (quickMenuMouseEventPending || mouseButtonHeld)) {
            quickMenuOpen = true;
            quickMenuCloseAfterMouseFrame = !mouseButtonHeld;
        } else {
            quickMenuOpen = false;
        }
    }

    if (!quickMenuOpen) {
        ResetQuickMenuVisualState();
    }
}

void BinderModule::Impl::DrawQuickMenu() {
    if (!quickMenuOpen) {
        return;
    }

    NormalizeExplorerOrders();

    const auto hotkeyVisible = [&](const int index) {
        if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
            return false;
        }
        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
        if (!hotkey.enabled || !hotkey.quickMenu) {
            return false;
        }
        const ConditionRuntimeContext context = MakeConditionContext(quickMenuOpen);
        return !ConditionsBlocked(hotkey.conditions, hotkey.conditionsCombine, sampApi, &context);
    };

    const auto categoryVisible = [&](const BinderCategory& category) {
        const ConditionRuntimeContext context = MakeConditionContext(quickMenuOpen);
        return category.quickMenu && !ConditionsBlocked(category.conditions, category.conditionsCombine, sampApi, &context);
    };

    const auto directoryHasVisibleEntries = [&](auto&& self, const BinderCategory& category, const FolderNode* folder) -> bool {
        const std::vector<ExplorerItem>& items = folder ? folder->items : category.rootItems;
        for (const ExplorerItem& item : items) {
            if (item.kind == ExplorerItemKind::Bind) {
                const int index = FindHotkeyIndexByOrderId(item.key);
                if (index >= 0 && hotkeys[static_cast<std::size_t>(index)].categoryId == category.id && hotkeyVisible(index)) {
                    return true;
                }
                continue;
            }
            FolderNode* child = FindFolderByNameInDirectory(category, const_cast<FolderNode*>(folder), item.key);
            if (child && FolderVisibleInQuickMenu(*child) && self(self, category, child)) {
                return true;
            }
        }
        return false;
    };

    std::vector<const BinderCategory*> visibleCategories;
    for (const BinderCategory& category : categories) {
        if (categoryVisible(category) && directoryHasVisibleEntries(directoryHasVisibleEntries, category, nullptr)) {
            visibleCategories.push_back(&category);
        }
    }

    if (visibleCategories.empty()) {
        quickMenuOpen = false;
        ResetQuickMenuVisualState();
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const auto syncOsMouseToImGui = []() {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        if (vp == nullptr || vp->PlatformHandle == nullptr) {
            return;
        }
        const HWND hwnd = reinterpret_cast<HWND>(vp->PlatformHandle);
        POINT pt{};
        if (::GetCursorPos(&pt) == FALSE || ::ScreenToClient(hwnd, &pt) == FALSE) {
            return;
        }
        ImGui::GetIO().AddMousePosEvent(static_cast<float>(pt.x), static_cast<float>(pt.y));
    };
    if (quickMenuPos.x == 0.0f && quickMenuPos.y == 0.0f) {
        quickMenuSize = ScaleUi(static_cast<float>(kQuickMenuWidth), static_cast<float>(kQuickMenuHeight));
        quickMenuPos = ImVec2((io.DisplaySize.x - quickMenuSize.x) * 0.5f, (io.DisplaySize.y - quickMenuSize.y) * 0.5f);
    }

    const bool persistentOpen = quickMenuActivationMode == QuickMenuActivationMode::Toggle;
    bool windowOpen = true;
    int selectedHotkeyIndex = -1;
    quickMenuHitItems.clear();

    const auto recordQuickMenuHit = [&](const int index) {
        ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (ImGuiWindow* window = ImGui::GetCurrentWindowRead()) {
            rect.Min.x = std::min(rect.Min.x, window->WorkRect.Min.x);
            rect.Max.x = std::max(rect.Max.x, window->WorkRect.Max.x);
        }
        rect.Expand(ImVec2(ScaleUi(2.0f), ScaleUi(1.0f)));
        quickMenuHitItems.push_back({ rect, index });
    };
    const auto findManualQuickMenuHit = [&]() {
        const ImVec2 mouse = quickMenuMouseClientPosValid ? quickMenuMouseClientPos : ImGui::GetIO().MousePos;
        for (auto it = quickMenuHitItems.rbegin(); it != quickMenuHitItems.rend(); ++it) {
            if (it->hotkeyIndex >= 0 && it->rect.Contains(mouse)) {
                return it->hotkeyIndex;
            }
        }
        return -1;
    };

    const auto closeQuickMenuForSelection = [&]() {
        quickMenuOpen = false;
        quickMenuReopenBlocked = true;
        ResetQuickMenuVisualState();
    };

    const auto hotkeyVisibleLabel = [&](const int index) {
        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
        return BindIconGlyph(hotkey) + " "
            + BuildBindDisplayLabel(hotkey);
    };

    const auto hotkeyShortcut = [&](const int index) {
        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
        return hotkey.keys.empty() ? std::string() : ::hotkeys::ToString(hotkey.keys, hotkey.hotkeyMode);
    };

    const auto labelWithId = [](std::string visible, const char* idPrefix, const std::string& id) {
        visible += "##";
        visible += idPrefix;
        visible += id;
        return visible;
    };

    const bool focusWarmup = quickMenuFocusPending || quickMenuFocusReassertFrames > 0;
    ImGui::SetNextWindowPos(quickMenuPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(quickMenuSize, ImGuiCond_Always);
    if (focusWarmup) {
        ImGui::SetNextWindowFocus();
    }
    const BinderListVisualStyle visual = BinderListStyleTokens();
    constexpr int kQuickMenuStyleVarCount = 8;
    constexpr int kQuickMenuStyleColorCount = 16;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaleUi(4.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ScaleUi(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ScaleUi(7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, ScaleUi(1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, ScaleUi(4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, ScaleUi(4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, visual.panelBg);
    ImGui::PushStyleColor(ImGuiCol_Border, visual.panelBorder);
    ImGui::PushStyleColor(ImGuiCol_Separator, visual.separator);
    ImGui::PushStyleColor(ImGuiCol_Header, visual.rowSelected);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, visual.rowHover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, visual.rowSelectedHover);
    ImGui::PushStyleColor(ImGuiCol_Tab, WithAlpha(visual.searchBg, 0.72f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, visual.buttonHover);
    ImGui::PushStyleColor(ImGuiCol_TabSelected, WithAlpha(visual.buttonActive, 0.86f));
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, WithAlpha(visual.accent, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_TabDimmed, WithAlpha(visual.searchBg, 0.46f));
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, WithAlpha(visual.buttonActive, 0.58f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, WithAlpha(visual.panelBg, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, WithAlpha(visual.panelBorder, 0.64f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, WithAlpha(visual.accent, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, WithAlpha(visual.accent, 0.84f));
    ImGuiWindowFlags quickMenuFlags =
        ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoScrollbar;
    if (!ImGui::Begin(kQuickMenuHostWindowId, nullptr, quickMenuFlags)) {
        ImGui::End();
        ImGui::PopStyleColor(kQuickMenuStyleColorCount);
        ImGui::PopStyleVar(kQuickMenuStyleVarCount);
        if (quickMenuFocusReassertFrames > 0) {
            --quickMenuFocusReassertFrames;
        }
        if (quickMenuFocusReassertFrames <= 0) {
            quickMenuFocusPending = false;
        }
        return;
    }
    if (focusWarmup) {
        ImGui::BringWindowToFocusFront(ImGui::GetCurrentWindow());
        syncOsMouseToImGui();
    }
    quickMenuPos = ImGui::GetWindowPos();
    quickMenuSize = ImGui::GetWindowSize();
    const bool quickMenuWindowHovered =
        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool quickMenuWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (quickMenuFocusPending && (quickMenuWindowHovered || quickMenuWindowFocused)) {
        quickMenuFocusPending = false;
    }
    if (quickMenuFocusReassertFrames > 0) {
        --quickMenuFocusReassertFrames;
    }
    if (quickMenuFocusReassertFrames <= 0) {
        quickMenuFocusPending = false;
    }

    DrawQuickMenuTitleBand(UiSettings::Instance().Text(UiText::QuickMenuWindowTitle), visual);

    if (persistentOpen && ImGui::Button(UiSettings::Instance().Text(UiText::Cancel))) {
        windowOpen = false;
    }

    const auto findVisibleCategoryById = [&](std::string_view id) -> const BinderCategory* {
        for (const BinderCategory* category : visibleCategories) {
            if (category != nullptr && category->id == id) {
                return category;
            }
        }
        return nullptr;
    };

    const BinderCategory* activeCategory = findVisibleCategoryById(quickMenuActiveCategoryId);
    if (activeCategory == nullptr) {
        activeCategory = visibleCategories.front();
        quickMenuActiveCategoryId = activeCategory->id;
    }

    if (visibleCategories.size() > 1) {
        if (ImGui::BeginTabBar("##quick_menu_category_tabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
            for (const BinderCategory* category : visibleCategories) {
                if (category == nullptr) {
                    continue;
                }
                const std::string visibleTab = EllipsizeText(category->name, ScaleUi(82.0f));
                const std::string label = labelWithId(visibleTab, "qm_category_tab_", category->id);
                if (ImGui::BeginTabItem(label.c_str())) {
                    quickMenuActiveCategoryId = category->id;
                    activeCategory = category;
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        activeCategory = findVisibleCategoryById(quickMenuActiveCategoryId);
        if (activeCategory == nullptr) {
            activeCategory = visibleCategories.front();
            quickMenuActiveCategoryId = activeCategory->id;
        }
    } else {
        activeCategory = visibleCategories.front();
        quickMenuActiveCategoryId = activeCategory->id;
    }

    const auto drawShortcutInLastItem = [&](const std::string& shortcut, const float rightX) {
        if (shortcut.empty()) {
            return;
        }

        const ImRect itemRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        const ImVec2 shortcutSize = ImGui::CalcTextSize(shortcut.c_str());
        const float shortcutX = std::max(itemRect.Min.x, rightX - shortcutSize.x - ScaleUi(3.0f));
        const float shortcutY = itemRect.Min.y + std::floor((itemRect.GetHeight() - shortcutSize.y) * 0.5f);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(shortcutX, shortcutY),
            ImGui::GetColorU32(visual.faintText),
            shortcut.c_str());
    };

    const auto drawQuickMenuRowSeparator = [&]() {
        ImGuiWindow* window = ImGui::GetCurrentWindowRead();
        if (window == nullptr || window->SkipItems) {
            return;
        }

        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float left = std::max(cursor.x + ScaleUi(22.0f), window->WorkRect.Min.x + ScaleUi(18.0f));
        const float right = window->WorkRect.Max.x - ScaleUi(7.0f);
        if (right <= left + ScaleUi(8.0f)) {
            return;
        }

        const float gap = std::max(ScaleUi(2.0f), ImGui::GetStyle().ItemSpacing.y);
        const float y = std::floor(cursor.y - gap * 0.5f) + 0.5f;
        window->DrawList->AddLine(
            ImVec2(left, y),
            ImVec2(right, y),
            ImGui::GetColorU32(WithAlpha(visual.separator, 0.56f)),
            std::max(1.0f, ScaleUi(1.0f)));
    };

    constexpr float kCascadePopupLabelMaxWidth = 320.0f;
    constexpr float kCascadePopupShortcutMaxWidth = 132.0f;

    const auto cascadePopupLabelMaxWidth = [&]() {
        return std::max(ScaleUi(96.0f), ScaleUi(kCascadePopupLabelMaxWidth));
    };

    const auto cascadePopupShortcut = [&](const std::string& shortcut) {
        return shortcut.empty()
            ? std::string()
            : EllipsizeText(shortcut, std::max(ScaleUi(48.0f), ScaleUi(kCascadePopupShortcutMaxWidth)));
    };

    const auto drawCascadeHotkey = [&](const int index, const char* idPrefix, const int depth) {
        const std::string shortcut = hotkeyShortcut(index);
        const float availableWidth = std::max(ScaleUi(32.0f), ImGui::GetContentRegionAvail().x);
        const float shortcutReserve = shortcut.empty() ? 0.0f : ImGui::CalcTextSize(shortcut.c_str()).x + ScaleUi(20.0f);
        const float labelMaxWidth = depth == 0
            ? std::max(ScaleUi(24.0f), availableWidth - shortcutReserve)
            : cascadePopupLabelMaxWidth();
        const float rightX = ImGui::GetCursorScreenPos().x + availableWidth;
        const std::string label = labelWithId(
            EllipsizeText(hotkeyVisibleLabel(index), labelMaxWidth),
            idPrefix,
            std::to_string(index));
        const std::string popupShortcut = depth == 0 ? std::string() : cascadePopupShortcut(shortcut);
        const bool activated = depth == 0
            ? ImGui::MenuItem(label.c_str(), nullptr, false, true)
            : ImGui::MenuItem(label.c_str(), popupShortcut.empty() ? nullptr : popupShortcut.c_str(), false, true);
        recordQuickMenuHit(index);
        if (activated) {
            selectedHotkeyIndex = index;
            return;
        }
        if (depth == 0) {
            drawShortcutInLastItem(shortcut, rightX);
        }
    };

    const auto drawTreeHotkey = [&](const int index, const char* idPrefix) {
        const std::string shortcut = hotkeyShortcut(index);
        const float availableWidth = std::max(ScaleUi(32.0f), ImGui::GetContentRegionAvail().x);
        const float shortcutReserve = shortcut.empty() ? 0.0f : ImGui::CalcTextSize(shortcut.c_str()).x + ScaleUi(18.0f);
        const float labelMaxWidth = std::max(ScaleUi(24.0f), availableWidth - shortcutReserve);
        const float rightX = ImGui::GetCursorScreenPos().x + availableWidth;
        const std::string label = labelWithId(
            EllipsizeText(hotkeyVisibleLabel(index), labelMaxWidth),
            idPrefix,
            std::to_string(index));
        const bool activated = ImGui::Selectable(label.c_str(), false);
        recordQuickMenuHit(index);
        if (activated) {
            selectedHotkeyIndex = index;
        }
        drawShortcutInLastItem(shortcut, rightX);
    };

    const auto drawCascadeDirectory = [&](auto&& self, const BinderCategory& category, FolderNode* folder, const char* idPrefix, const int depth) -> void {
        const std::vector<ExplorerItem>& items = folder ? folder->items : category.rootItems;
        bool rowDrawn = false;
        const auto drawSeparatorBeforeRow = [&]() {
            if (rowDrawn) {
                drawQuickMenuRowSeparator();
            }
            rowDrawn = true;
        };

        for (const ExplorerItem& item : items) {
            if (selectedHotkeyIndex >= 0) {
                return;
            }
            if (item.kind == ExplorerItemKind::Bind) {
                const int index = FindHotkeyIndexByOrderId(item.key);
                if (index >= 0 && hotkeys[static_cast<std::size_t>(index)].categoryId == category.id && hotkeyVisible(index)) {
                    drawSeparatorBeforeRow();
                    drawCascadeHotkey(index, idPrefix, depth);
                }
                continue;
            }

            FolderNode* child = FindFolderByNameInDirectory(category, folder, item.key);
            if (!child || !FolderVisibleInQuickMenu(*child) || !directoryHasVisibleEntries(directoryHasVisibleEntries, category, child)) {
                continue;
            }
            drawSeparatorBeforeRow();
            const std::string path = category.id + "/" + JoinPath(BuildFolderPath(child));
            const float labelMaxWidth = depth == 0
                ? std::max(ScaleUi(24.0f), ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ScaleUi(8.0f))
                : cascadePopupLabelMaxWidth();
            const std::string label = labelWithId(
                EllipsizeText(FolderIconGlyph(*child) + " " + child->name, labelMaxWidth),
                "qm_folder_",
                path);
            if (ImGui::BeginMenu(label.c_str())) {
                self(self, category, child, "qm_bind_", depth + 1);
                ImGui::EndMenu();
            }
        }
    };

    bool treeRowDrawn = false;
    const auto drawTreeDirectory = [&](auto&& self, const BinderCategory& category, FolderNode* folder, int depth) -> void {
        const std::vector<ExplorerItem>& items = folder ? folder->items : category.rootItems;
        const auto drawSeparatorBeforeRow = [&]() {
            if (treeRowDrawn) {
                drawQuickMenuRowSeparator();
            }
            treeRowDrawn = true;
        };

        for (const ExplorerItem& item : items) {
            if (selectedHotkeyIndex >= 0) {
                return;
            }
            if (item.kind == ExplorerItemKind::Bind) {
                const int index = FindHotkeyIndexByOrderId(item.key);
                if (index >= 0 && hotkeys[static_cast<std::size_t>(index)].categoryId == category.id && hotkeyVisible(index)) {
                    drawSeparatorBeforeRow();
                    drawTreeHotkey(index, "qm_tree_bind_");
                }
                continue;
            }

            FolderNode* child = FindFolderByNameInDirectory(category, folder, item.key);
            if (!child || !FolderVisibleInQuickMenu(*child) || !directoryHasVisibleEntries(directoryHasVisibleEntries, category, child)) {
                continue;
            }
            drawSeparatorBeforeRow();
            const std::string path = category.id + "/" + JoinPath(BuildFolderPath(child));
            const float labelMaxWidth = std::max(ScaleUi(24.0f), ImGui::GetContentRegionAvail().x - ScaleUi(4.0f));
            const std::string label = labelWithId(
                EllipsizeText(FolderIconGlyph(*child) + " " + child->name, labelMaxWidth),
                "qm_tree_folder_",
                path);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
            if (depth == 0) {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }
            if (ImGui::TreeNodeEx(label.c_str(), flags)) {
                self(self, category, child, depth + 1);
                ImGui::TreePop();
            }
        }
    };

    ImGuiWindowFlags contentFlags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings;
    if (!quickMenuShowScrollbar) {
        contentFlags |= ImGuiWindowFlags_NoScrollbar;
    }
    const ImVec2 contentSize(0.0f, std::max(1.0f, ImGui::GetContentRegionAvail().y));
    if (ImGui::BeginChild("##quick_menu_content", contentSize, ImGuiChildFlags_None, contentFlags)) {
        if (quickMenuStyle == QuickMenuStyle::Cascade) {
            drawCascadeDirectory(drawCascadeDirectory, *activeCategory, nullptr, "qm_root_bind_", 0);
        } else {
            drawTreeDirectory(drawTreeDirectory, *activeCategory, nullptr, 0);
        }
    }
    ImGui::EndChild();

    const bool closeAfterMouseFrame = quickMenuCloseAfterMouseFrame;
    const bool mouseEventInside = quickMenuMouseEventInside;
    const bool comboHeldNow = IsQuickMenuComboPressed();
    const int manualHitIndex = findManualQuickMenuHit();
    const bool manualHit = manualHitIndex >= 0;
    const bool imguiHovered = quickMenuWindowHovered || ImGui::IsAnyItemHovered();
    if (selectedHotkeyIndex < 0 && manualHit && io.MouseReleased[0]) {
        selectedHotkeyIndex = manualHitIndex;
    }
    if (mouseEventInside && selectedHotkeyIndex < 0 && (io.MouseClicked[0] || io.MouseReleased[0] || closeAfterMouseFrame)) {
        TraceQuickMenuInput(
            closeAfterMouseFrame ? "mouse_frame_no_selection" : "mouse_frame_pending",
            quickMenuPos,
            quickMenuSize,
            selectedHotkeyIndex,
            comboHeldNow,
            closeAfterMouseFrame,
            closeAfterMouseFrame,
            manualHitIndex,
            manualHit,
            imguiHovered);
    }

    if (persistentOpen && !windowOpen) {
        quickMenuOpen = false;
    }
    ImGui::End();
    ImGui::PopStyleColor(kQuickMenuStyleColorCount);
    ImGui::PopStyleVar(kQuickMenuStyleVarCount);

    if (persistentOpen && !windowOpen) {
        TraceQuickMenuInput(
            "cancel",
            quickMenuPos,
            quickMenuSize,
            selectedHotkeyIndex,
            comboHeldNow,
            false,
            true,
            manualHitIndex,
            manualHit,
            imguiHovered);
        closeQuickMenuForSelection();
        return;
    }
    if (selectedHotkeyIndex >= 0) {
        TraceQuickMenuInput(
            "selected",
            quickMenuPos,
            quickMenuSize,
            selectedHotkeyIndex,
            comboHeldNow,
            closeAfterMouseFrame,
            true,
            manualHitIndex,
            manualHit,
            imguiHovered);
        closeQuickMenuForSelection();
        TryEnqueueHotkey(selectedHotkeyIndex, 0, "quick_menu", "");
        return;
    }
    if (closeAfterMouseFrame) {
        quickMenuOpen = false;
        ResetQuickMenuVisualState();
        return;
    }

    quickMenuMouseEventPending = false;
    quickMenuMouseEventInside = false;
    quickMenuCloseAfterMouseFrame = false;
}
