#include "binder_module_impl.h"

namespace {

constexpr char kQuickMenuCategoryPopupId[] = "##quick_menu_category_selector_popup";
constexpr float kTextTabsEdgeInset = 1.0f;

struct CategoryControl {
    ImRect rect{};
    bool pressed = false;
    bool hovered = false;
    bool delayedHovered = false;
    bool active = false;
    bool focused = false;
};

float CategoryBandHeight() {
    return std::max(ImGui::GetTextLineHeight(), ScaleUi(14.0f));
}

CategoryControl RegisterCategoryControl(const char* id, const ImRect& rect) {
    ImGui::SetCursorScreenPos(rect.Min);
    CategoryControl control;
    ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
    control.pressed = ImGui::InvisibleButton(id, rect.GetSize(), ImGuiButtonFlags_EnableNav);
    ImGui::PopItemFlag();
    control.hovered = ImGui::IsItemHovered();
    control.delayedHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
    control.active = ImGui::IsItemActive();
    control.focused = ImGui::IsItemFocused();
    control.rect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    return control;
}

void FinishCategoryBand(const ImVec2& pos, float width, float height) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(width, height));
}

void DrawCategoryControlBackground(const CategoryControl& control, const BinderListVisualStyle& visual) {
    const bool navFocused = control.focused && ImGui::GetIO().NavVisible;
    if (!control.hovered && !control.active && !navFocused) {
        return;
    }
    const ImVec4 color = control.active
        ? WithAlpha(visual.buttonActive, 0.38f)
        : WithAlpha(visual.rowHover, navFocused ? 0.92f : 0.72f);
    ImGui::GetWindowDrawList()->AddRectFilled(
        control.rect.Min,
        control.rect.Max,
        ImGui::GetColorU32(color),
        ScaleUi(3.0f));
}

void DrawCenteredCategoryText(
    const ImRect& rect,
    std::string_view text,
    const ImVec4& color) {
    const ImVec2 textSize = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    const ImVec2 position(
        std::floor(rect.Min.x + std::max(0.0f, (rect.GetWidth() - textSize.x) * 0.5f)),
        std::floor(rect.Min.y + std::max(0.0f, (rect.GetHeight() - textSize.y) * 0.5f)));
    ImGui::GetWindowDrawList()->AddText(
        position,
        ImGui::GetColorU32(color),
        text.data(),
        text.data() + text.size());
}

void DrawCategoryBandTitle(
    const ImVec2& pos,
    float rightEdge,
    float height,
    const char* title,
    const BinderListVisualStyle& visual) {
    if (title == nullptr || title[0] == '\0' || rightEdge <= pos.x) {
        return;
    }

    const float textX = pos.x + ScaleUi(1.0f);
    const float maxTextWidth = std::max(1.0f, rightEdge - textX - ScaleUi(8.0f));
    const std::string visibleTitle = EllipsizeText(title, maxTextWidth);
    if (visibleTitle.empty()) {
        return;
    }

    const ImVec2 textSize = ImGui::CalcTextSize(visibleTitle.c_str());
    const float textY = pos.y + std::floor((height - textSize.y) * 0.5f) - ScaleUi(1.0f);
    const float lineY = std::floor(textY + textSize.y * 0.5f) + 0.5f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddText(
        ImVec2(textX, textY),
        ImGui::GetColorU32(WithAlpha(BlendColor(visual.mutedText, visual.headerText, 0.22f), 0.95f)),
        visibleTitle.c_str());

    const float lineStart = textX + textSize.x + ScaleUi(7.0f);
    if (rightEdge > lineStart) {
        drawList->AddLine(
            ImVec2(lineStart, lineY),
            ImVec2(rightEdge, lineY),
            ImGui::GetColorU32(WithAlpha(visual.separator, 0.74f)),
            ScaleUi(1.0f));
    }
}

std::string CategoryLabelWithId(const BinderCategory& category, const char* idPrefix) {
    return category.name + "###" + idPrefix + category.id;
}

const BinderCategory* DrawCategorySelectorPopup(
    const std::vector<const BinderCategory*>& visibleCategories,
    const BinderCategory* activeCategory,
    const ImVec2& anchor,
    bool& categorySelectionConsumed) {
    const BinderCategory* selectedCategory = activeCategory;
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Appearing);
    if (!ImGui::BeginPopup(kQuickMenuCategoryPopupId, ImGuiWindowFlags_NoSavedSettings)) {
        return selectedCategory;
    }

    for (const BinderCategory* category : visibleCategories) {
        if (category == nullptr) {
            continue;
        }
        const std::string label = CategoryLabelWithId(*category, "qm_category_popup_");
        if (ImGui::MenuItem(label.c_str(), nullptr, category == activeCategory)) {
            selectedCategory = category;
            categorySelectionConsumed = true;
        }
    }
    ImGui::EndPopup();
    return selectedCategory;
}

const BinderCategory* DrawTitleSelector(
    const std::vector<const BinderCategory*>& visibleCategories,
    const BinderCategory* activeCategory,
    const BinderListVisualStyle& visual,
    bool& categorySelectionConsumed) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = CategoryBandHeight();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float arrowWidth = ImGui::CalcTextSize(ui_icons::AngleDown).x;
    const float horizontalPadding = ScaleUi(5.0f);
    const float contentGap = ScaleUi(4.0f);
    const float naturalWidth = ImGui::CalcTextSize(activeCategory->name.c_str()).x
        + arrowWidth
        + contentGap
        + horizontalPadding * 2.0f;
    const float maximumSelectorWidth = std::max(1.0f, width - ScaleUi(40.0f));
    const float minimumSelectorWidth = std::min(ScaleUi(72.0f), maximumSelectorWidth);
    const float selectorWidth =
        std::clamp(naturalWidth, minimumSelectorWidth, maximumSelectorWidth);
    const ImRect selectorRect(
        ImVec2(pos.x + width - selectorWidth, pos.y),
        ImVec2(pos.x + width, pos.y + height));
    const CategoryControl selector =
        RegisterCategoryControl("##quick_menu_category_title_selector", selectorRect);
    if (selector.pressed) {
        ImGui::OpenPopup(kQuickMenuCategoryPopupId);
    }

    DrawCategoryBandTitle(
        pos,
        selector.rect.Min.x - ScaleUi(5.0f),
        height,
        ui_icons::Bolt,
        visual);
    DrawCategoryControlBackground(selector, visual);

    const float labelMaxWidth =
        std::max(1.0f, selector.rect.GetWidth() - horizontalPadding * 2.0f - arrowWidth - contentGap);
    const std::string visibleLabel = EllipsizeText(activeCategory->name, labelMaxWidth);
    const ImVec2 labelSize = ImGui::CalcTextSize(visibleLabel.c_str());
    const ImVec2 arrowSize = ImGui::CalcTextSize(ui_icons::AngleDown);
    const float contentWidth = labelSize.x + contentGap + arrowSize.x;
    const float textX = selector.rect.Min.x
        + std::max(horizontalPadding, (selector.rect.GetWidth() - contentWidth) * 0.5f);
    const float textY = selector.rect.Min.y
        + std::floor((selector.rect.GetHeight() - labelSize.y) * 0.5f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(selector.rect.Min, selector.rect.Max, true);
    drawList->AddText(ImVec2(textX, textY), ImGui::GetColorU32(visual.headerText), visibleLabel.c_str());
    drawList->AddText(
        ImVec2(textX + labelSize.x + contentGap, textY),
        ImGui::GetColorU32(visual.mutedText),
        ui_icons::AngleDown);
    drawList->PopClipRect();

    if (visibleLabel != activeCategory->name
        && selector.delayedHovered) {
        ImGui::SetTooltip("%s", activeCategory->name.c_str());
    }

    FinishCategoryBand(pos, width, height);
    return DrawCategorySelectorPopup(
        visibleCategories,
        activeCategory,
        ImVec2(selector.rect.Min.x, selector.rect.Max.y + style.ItemSpacing.y),
        categorySelectionConsumed);
}

const BinderCategory* DrawTextTabs(
    const std::vector<const BinderCategory*>& visibleCategories,
    const BinderCategory* activeCategory,
    const BinderListVisualStyle& visual,
    std::uint64_t categoryTabOrderRevision,
    bool forceSelectedCategory) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float contentLeft = window->WorkRect.Min.x;
    const float contentRight = window->WorkRect.Max.x;
    const ImVec2 tabBarPos(
        window->Pos.x + kTextTabsEdgeInset,
        window->Pos.y + kTextTabsEdgeInset);
    const float tabBarRight =
        window->Pos.x + window->Size.x - kTextTabsEdgeInset;
    ImGui::SetCursorScreenPos(tabBarPos);

    const ImVec4 transparent = WithAlpha(visual.panelBg, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaleUi(4.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Tab, transparent);
    ImGui::PushStyleColor(ImGuiCol_TabHovered, WithAlpha(visual.rowHover, 0.82f));
    ImGui::PushStyleColor(ImGuiCol_TabSelected, transparent);
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, transparent);
    ImGui::PushStyleColor(ImGuiCol_TabDimmed, transparent);
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, transparent);

    const BinderCategory* selectedCategory = activeCategory;
    const std::string tabBarId =
        "##quick_menu_category_text_tabs_" + std::to_string(categoryTabOrderRevision);
    const bool restoreSelectedCategory = forceSelectedCategory || ImGui::IsWindowAppearing();
    window->WorkRect.Max.x = tabBarRight;
    const bool tabBarOpen =
        ImGui::BeginTabBar(tabBarId.c_str(), ImGuiTabBarFlags_FittingPolicyScroll);
    window->WorkRect.Max.x = contentRight;
    if (tabBarOpen) {
        for (const BinderCategory* category : visibleCategories) {
            if (category == nullptr) {
                continue;
            }
            const bool selected = category == activeCategory;
            const std::string label = CategoryLabelWithId(*category, "qm_category_text_tab_");
            const ImGuiTabItemFlags flags = restoreSelectedCategory && selected
                ? ImGuiTabItemFlags_SetSelected
                : 0;
            ImGui::PushStyleColor(ImGuiCol_Text, selected ? visual.headerText : visual.mutedText);
            const bool tabOpen = ImGui::BeginTabItem(label.c_str(), nullptr, flags);
            ImGui::PopStyleColor();

            const ImRect tabRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            const float textAvailable =
                std::max(0.0f, tabRect.GetWidth() - ImGui::GetStyle().FramePadding.x * 2.0f);
            if (ImGui::CalcTextSize(category->name.c_str()).x > textAvailable
                && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", category->name.c_str());
            }
            if (tabOpen) {
                selectedCategory = category;
                const float inset = ScaleUi(4.0f);
                const float thickness = std::max(1.0f, ScaleUi(1.5f));
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(tabRect.Min.x + inset, tabRect.Max.y - thickness * 0.5f),
                    ImVec2(tabRect.Max.x - inset, tabRect.Max.y - thickness * 0.5f),
                    ImGui::GetColorU32(visual.accent),
                    thickness);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(2);
    const float contentY = ImGui::GetCursorScreenPos().y;
    ImGui::SetCursorScreenPos(ImVec2(contentLeft, contentY));
    return selectedCategory;
}

std::size_t FindCategoryIndex(
    const std::vector<const BinderCategory*>& visibleCategories,
    const BinderCategory* activeCategory) {
    for (std::size_t index = 0; index < visibleCategories.size(); ++index) {
        if (visibleCategories[index] == activeCategory) {
            return index;
        }
    }
    return 0;
}

const BinderCategory* DrawCarousel(
    const std::vector<const BinderCategory*>& visibleCategories,
    const BinderCategory* activeCategory,
    const BinderListVisualStyle& visual,
    bool& categorySelectionConsumed) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = CategoryBandHeight();
    if (width <= height * 2.0f + ScaleUi(8.0f)) {
        return DrawTitleSelector(
            visibleCategories,
            activeCategory,
            visual,
            categorySelectionConsumed);
    }

    const float titleReserve = ImGui::CalcTextSize(ui_icons::Bolt).x + ScaleUi(12.0f);
    float widestLabel = 0.0f;
    for (const BinderCategory* category : visibleCategories) {
        if (category != nullptr) {
            widestLabel = std::max(widestLabel, ImGui::CalcTextSize(category->name.c_str()).x);
        }
    }
    const float naturalControlWidth =
        height * 2.0f + widestLabel + ScaleUi(12.0f);
    const float minimumControlWidth = height * 2.0f + ScaleUi(42.0f);
    const float maximumControlWidth =
        std::max(minimumControlWidth, width - titleReserve);
    const float controlWidth = std::min(
        width,
        std::clamp(naturalControlWidth, minimumControlWidth, maximumControlWidth));
    const ImRect controlRect(
        ImVec2(pos.x + width - controlWidth, pos.y),
        ImVec2(pos.x + width, pos.y + height));
    const ImRect previousRect(controlRect.Min, ImVec2(controlRect.Min.x + height, controlRect.Max.y));
    const ImRect nextRect(ImVec2(controlRect.Max.x - height, controlRect.Min.y), controlRect.Max);
    const ImRect labelRect(
        ImVec2(previousRect.Max.x, controlRect.Min.y),
        ImVec2(nextRect.Min.x, controlRect.Max.y));

    const CategoryControl previous =
        RegisterCategoryControl("##quick_menu_category_previous", previousRect);
    const CategoryControl label =
        RegisterCategoryControl("##quick_menu_category_carousel_label", labelRect);
    const CategoryControl next =
        RegisterCategoryControl("##quick_menu_category_next", nextRect);

    const std::size_t activeIndex = FindCategoryIndex(visibleCategories, activeCategory);
    const BinderCategory* selectedCategory = activeCategory;
    if (previous.pressed) {
        selectedCategory = visibleCategories[
            (activeIndex + visibleCategories.size() - 1) % visibleCategories.size()];
        categorySelectionConsumed = true;
    } else if (next.pressed) {
        selectedCategory = visibleCategories[(activeIndex + 1) % visibleCategories.size()];
        categorySelectionConsumed = true;
    }
    if (label.pressed) {
        ImGui::OpenPopup(kQuickMenuCategoryPopupId);
    }

    DrawCategoryBandTitle(
        pos,
        controlRect.Min.x - ScaleUi(5.0f),
        height,
        ui_icons::Bolt,
        visual);
    DrawCategoryControlBackground(previous, visual);
    DrawCategoryControlBackground(label, visual);
    DrawCategoryControlBackground(next, visual);
    DrawCenteredCategoryText(previous.rect, ui_icons::ChevronLeft, visual.mutedText);
    DrawCenteredCategoryText(next.rect, ui_icons::ChevronRight, visual.mutedText);

    const float labelMaxWidth = std::max(1.0f, label.rect.GetWidth() - ScaleUi(8.0f));
    const std::string visibleLabel = EllipsizeText(selectedCategory->name, labelMaxWidth);
    DrawCenteredCategoryText(label.rect, visibleLabel, visual.headerText);
    if (visibleLabel != selectedCategory->name
        && label.delayedHovered) {
        ImGui::SetTooltip("%s", selectedCategory->name.c_str());
    }

    FinishCategoryBand(pos, width, height);
    return DrawCategorySelectorPopup(
        visibleCategories,
        selectedCategory,
        ImVec2(label.rect.Min.x, label.rect.Max.y + ImGui::GetStyle().ItemSpacing.y),
        categorySelectionConsumed);
}

} // namespace

const BinderCategory* BinderModule::Impl::DrawQuickMenuCategoryNavigation(
    const std::vector<const BinderCategory*>& visibleCategories,
    const BinderCategory* activeCategory,
    const BinderListVisualStyle& visual,
    bool& categorySelectionConsumed) {
    categorySelectionConsumed = false;
    if (activeCategory == nullptr) {
        return nullptr;
    }
    if (visibleCategories.size() <= 1) {
        quickMenuCategoryTabsRevisionSeen = categoryTabOrderRevision;
        quickMenuRenderedCategoryId = activeCategory->id;
        if (quickMenuCategoryLayout != QuickMenuCategoryLayout::TextTabs) {
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const float height = CategoryBandHeight();
            DrawCategoryBandTitle(pos, pos.x + width, height, ui_icons::Bolt, visual);
            FinishCategoryBand(pos, width, height);
        }
        return activeCategory;
    }

    const bool forceTextTabSelection = quickMenuCategoryLayout == QuickMenuCategoryLayout::TextTabs
        && (quickMenuCategoryTabsRevisionSeen != categoryTabOrderRevision
            || quickMenuRenderedCategoryId != activeCategory->id);

    const BinderCategory* selectedCategory = activeCategory;
    switch (quickMenuCategoryLayout) {
    case QuickMenuCategoryLayout::TitleSelector:
        selectedCategory = DrawTitleSelector(
            visibleCategories,
            activeCategory,
            visual,
            categorySelectionConsumed);
        break;
    case QuickMenuCategoryLayout::TextTabs:
        selectedCategory = DrawTextTabs(
            visibleCategories,
            activeCategory,
            visual,
            categoryTabOrderRevision,
            forceTextTabSelection);
        categorySelectionConsumed = selectedCategory != activeCategory;
        break;
    case QuickMenuCategoryLayout::Carousel:
        selectedCategory = DrawCarousel(
            visibleCategories,
            activeCategory,
            visual,
            categorySelectionConsumed);
        break;
    }

    if (selectedCategory != nullptr) {
        quickMenuActiveCategoryId = selectedCategory->id;
        quickMenuRenderedCategoryId = selectedCategory->id;
    }
    quickMenuCategoryTabsRevisionSeen = categoryTabOrderRevision;
    return selectedCategory;
}
