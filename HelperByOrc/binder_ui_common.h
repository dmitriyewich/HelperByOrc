#pragma once

// Included from binder_module_impl.h after foundational binder_internal helpers.
// Keep these helpers header-only: they depend on current ImGui frame state.

namespace binder_internal {

struct BinderListVisualStyle {
    ImVec4 panelBg{};
    ImVec4 panelBorder{};
    ImVec4 headerText{};
    ImVec4 mutedText{};
    ImVec4 faintText{};
    ImVec4 rowHover{};
    ImVec4 rowSelected{};
    ImVec4 rowSelectedHover{};
    ImVec4 rowAlt{};
    ImVec4 separator{};
    ImVec4 accent{};
    ImVec4 searchBg{};
    ImVec4 searchHover{};
    ImVec4 searchActive{};
    ImVec4 buttonBg{};
    ImVec4 buttonHover{};
    ImVec4 buttonActive{};
    ImVec4 enabled{};
    ImVec4 running{};
    ImVec4 paused{};
    ImVec4 danger{};
};

inline BinderListVisualStyle BinderListStyleTokens() {
    const ImVec4* colors = ImGui::GetStyle().Colors;
    const ImVec4& windowBg = colors[ImGuiCol_WindowBg];
    const ImVec4& childBg = colors[ImGuiCol_ChildBg];
    const ImVec4& frameBg = colors[ImGuiCol_FrameBg];
    const ImVec4& frameBgHovered = colors[ImGuiCol_FrameBgHovered];
    const ImVec4& frameBgActive = colors[ImGuiCol_FrameBgActive];
    const ImVec4& button = colors[ImGuiCol_Button];
    const ImVec4& buttonHovered = colors[ImGuiCol_ButtonHovered];
    const ImVec4& buttonActive = colors[ImGuiCol_ButtonActive];
    const ImVec4& headerHovered = colors[ImGuiCol_HeaderHovered];
    const ImVec4& headerActive = colors[ImGuiCol_HeaderActive];
    const ImVec4& text = colors[ImGuiCol_Text];
    const ImVec4& textDisabled = colors[ImGuiCol_TextDisabled];
    const ImVec4& border = colors[ImGuiCol_Border];

    BinderListVisualStyle style;
    style.panelBg = WithAlpha(BlendColor(childBg, windowBg, 0.18f), childBg.w);
    style.panelBorder = WithAlpha(border, 0.42f);
    style.headerText = text;
    style.mutedText = WithAlpha(BlendColor(textDisabled, text, 0.28f), 0.92f);
    style.faintText = WithAlpha(textDisabled, 0.82f);
    style.rowHover = WithAlpha(headerHovered, 0.20f);
    style.rowSelected = WithAlpha(headerActive, 0.30f);
    style.rowSelectedHover = WithAlpha(headerActive, 0.38f);
    style.rowAlt = WithAlpha(text, 0.025f);
    style.separator = WithAlpha(border, 0.18f);
    style.accent = WithAlpha(buttonActive, 0.96f);
    style.searchBg = WithAlpha(frameBg, 0.96f);
    style.searchHover = frameBgHovered;
    style.searchActive = frameBgActive;
    style.buttonBg = WithAlpha(button, 0.96f);
    style.buttonHover = buttonHovered;
    style.buttonActive = buttonActive;
    style.enabled = WithAlpha(BlendColor(text, buttonActive, 0.28f), 0.94f);
    style.running = WithAlpha(BlendColor(text, buttonActive, 0.42f), 0.96f);
    style.paused = WithAlpha(BlendColor(text, buttonActive, 0.22f), 0.92f);
    style.danger = WithAlpha(BlendColor(text, buttonHovered, 0.18f), 0.92f);
    return style;
}

inline void DrawQuickMenuTitleBand(const char* title, const BinderListVisualStyle& visual) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    if (width <= 1.0f) {
        return;
    }

    const float bandHeight = std::max(ImGui::GetTextLineHeight(), ScaleUi(14.0f));
    if (title == nullptr || title[0] == '\0') {
        ImGui::Dummy(ImVec2(width, bandHeight));
        return;
    }

    const float textX = pos.x + ScaleUi(1.0f);
    const float maxTextWidth = std::max(1.0f, width - ScaleUi(12.0f));
    const std::string visibleTitle = EllipsizeText(title, maxTextWidth);
    if (visibleTitle.empty()) {
        ImGui::Dummy(ImVec2(width, bandHeight));
        return;
    }

    const ImVec2 textSize = ImGui::CalcTextSize(visibleTitle.c_str());
    const float textY = pos.y + std::floor((bandHeight - textSize.y) * 0.5f) - ScaleUi(1.0f);
    const float lineY = std::floor(textY + textSize.y * 0.5f) + 0.5f;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 lineColor = ImGui::GetColorU32(WithAlpha(visual.separator, 0.74f));
    const float rightLineStart = textX + textSize.x + ScaleUi(7.0f);
    const float rightLineEnd = pos.x + width;
    if (rightLineEnd > rightLineStart) {
        drawList->AddLine(ImVec2(rightLineStart, lineY), ImVec2(rightLineEnd, lineY), lineColor, ScaleUi(1.0f));
    }

    drawList->AddText(
        ImVec2(textX, textY),
        ImGui::GetColorU32(WithAlpha(BlendColor(visual.mutedText, visual.headerText, 0.22f), 0.95f)),
        visibleTitle.c_str());
    ImGui::Dummy(ImVec2(width, bandHeight));
}

inline void DrawTwoPanePanelBackground(const ImVec2& size) {
    const BinderListVisualStyle& visual = BinderListStyleTokens();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImRect rect(pos, ImVec2(pos.x + std::max(1.0f, size.x), pos.y + std::max(1.0f, size.y)));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float rounding = ScaleUi(7.0f);
    drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(visual.panelBg), rounding);
    drawList->AddRect(rect.Min, rect.Max, ImGui::GetColorU32(visual.panelBorder), rounding, 0, ScaleUi(1.0f));
}

inline bool BeginTwoPanePanel(const char* id, const ImVec2& size) {
    DrawTwoPanePanelBackground(size);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(8.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    return ImGui::BeginChild(id, size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
}

inline void EndTwoPanePanel() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

inline void DrawBinderListRowBackground(
    ImDrawList* drawList,
    const ImRect& rowRect,
    const int rowIndex,
    const bool selected,
    const bool hovered) {
    const BinderListVisualStyle& visual = BinderListStyleTokens();
    ImVec4 bg = selected
        ? (hovered ? visual.rowSelectedHover : visual.rowSelected)
        : (hovered ? visual.rowHover : WithAlpha(visual.rowAlt, rowIndex % 2 != 0 ? visual.rowAlt.w : 0.0f));
    if (bg.w > 0.0f) {
        drawList->AddRectFilled(rowRect.Min, rowRect.Max, ImGui::GetColorU32(bg), ScaleUi(5.0f));
    }

    if (selected) {
        const ImRect accentRect(
            ImVec2(rowRect.Min.x + ScaleUi(2.0f), rowRect.Min.y + ScaleUi(5.0f)),
            ImVec2(rowRect.Min.x + ScaleUi(4.0f), rowRect.Max.y - ScaleUi(5.0f)));
        drawList->AddRectFilled(accentRect.Min, accentRect.Max, ImGui::GetColorU32(visual.accent), ScaleUi(2.0f));
    } else {
        drawList->AddLine(
            ImVec2(rowRect.Min.x + ScaleUi(6.0f), rowRect.Max.y),
            ImVec2(rowRect.Max.x - ScaleUi(6.0f), rowRect.Max.y),
            ImGui::GetColorU32(visual.separator));
    }
}

inline bool BinderListFlatIconButton(
    const char* icon,
    const char* id,
    const char* tooltip,
    const ImVec2& size,
    ImVec4 iconColor = ImVec4(0.0f, 0.0f, 0.0f, -1.0f),
    bool enabled = true) {
    const BinderListVisualStyle& visual = BinderListStyleTokens();
    const bool buttonClicked = ImGui::InvisibleButton(id, size);
    const bool clicked = enabled && buttonClicked;
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const bool held = enabled && ImGui::IsItemActive();

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if ((hovered && enabled) || held) {
        const ImVec4 bg = held ? visual.buttonActive : visual.buttonHover;
        drawList->AddRectFilled(min, max, ImGui::GetColorU32(bg), ScaleUi(5.0f));
        drawList->AddRect(min, max, ImGui::GetColorU32(WithAlpha(visual.panelBorder, 0.28f)), ScaleUi(5.0f), 0, ScaleUi(1.0f));
    }

    if (iconColor.w < 0.0f) {
        iconColor = !enabled ? visual.faintText : hovered || held ? visual.headerText : visual.mutedText;
    }
    DrawCenteredIconGlyph(drawList, icon, ImRect(min, max), ImGui::GetColorU32(iconColor));

    if (tooltip != nullptr && tooltip[0] != '\0' && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
}

inline float BinderListTextActionButtonWidth(const char* icon, const char* label) {
    const float iconReserve = icon != nullptr && icon[0] != '\0' ? ScaleUi(22.0f) : 0.0f;
    const float iconGap = iconReserve > 0.0f ? ScaleUi(5.0f) : 0.0f;
    const float textWidth = ImGui::CalcTextSize(label ? label : "").x;
    return std::ceil(ScaleUi(10.0f) + iconReserve + iconGap + textWidth + ScaleUi(10.0f));
}

inline bool BinderListTextActionButton(
    const char* icon,
    const char* label,
    const char* id,
    const char* tooltip,
    const ImVec2& size) {
    const BinderListVisualStyle visual = BinderListStyleTokens();
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec4 bg = held ? visual.buttonActive : hovered ? visual.buttonHover : visual.buttonBg;
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(bg), ScaleUi(5.0f));
    drawList->AddRect(min, max, ImGui::GetColorU32(visual.panelBorder), ScaleUi(5.0f), 0, ScaleUi(1.0f));

    const float padX = ScaleUi(10.0f);
    const float iconW = icon != nullptr && icon[0] != '\0' ? ScaleUi(22.0f) : 0.0f;
    const float iconGap = iconW > 0.0f ? ScaleUi(5.0f) : 0.0f;
    float x = min.x + padX;
    const ImU32 textColor = ImGui::GetColorU32(visual.headerText);
    if (iconW > 0.0f) {
        DrawCenteredIconGlyph(
            drawList,
            icon,
            ImRect(ImVec2(x, min.y), ImVec2(x + iconW, max.y)),
            textColor,
            std::floor(ImGui::GetFontSize() * 0.90f));
        x += iconW + iconGap;
    }

    const char* text = label ? label : "";
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const float textY = min.y + std::floor(std::max(0.0f, (size.y - textSize.y) * 0.5f));
    const ImVec4 clip(min.x, min.y, max.x, max.y);
    drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, textY), textColor, text, nullptr, 0.0f, &clip);

    if (tooltip != nullptr && tooltip[0] != '\0' && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
}

inline std::size_t CountFoldersRecursive(const std::vector<std::unique_ptr<FolderNode>>& folders) {
    std::size_t count = 0;
    for (const auto& folder : folders) {
        if (!folder) {
            continue;
        }
        ++count;
        count += CountFoldersRecursive(folder->children);
    }
    return count;
}

inline bool DrawTwoPanePaneHeader(
    const char* title,
    const std::size_t count,
    const char* subtitle,
    const char* actionIcon,
    const char* actionLabel,
    const char* actionId,
    const char* actionTooltip) {
    const BinderListVisualStyle& visual = BinderListStyleTokens();
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float height = ImGui::GetFrameHeight();
    const bool hasAction = actionIcon != nullptr && actionIcon[0] != '\0';
    const float buttonWidth = hasAction
        ? std::max(height, BinderListTextActionButtonWidth(actionIcon, actionLabel))
        : 0.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImRect rect(pos, ImVec2(pos.x + width, pos.y + height));
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const float textY = rect.Min.y + std::floor((rect.GetHeight() - ImGui::GetTextLineHeight()) * 0.5f);
    const float padX = ScaleUi(4.0f);
    const std::string countText = std::to_string(count);
    const ImVec2 titleSize = ImGui::CalcTextSize(title ? title : "");
    const ImVec2 countSize = ImGui::CalcTextSize(countText.c_str());
    const float pillPadX = ScaleUi(6.0f);
    const float pillGap = ScaleUi(7.0f);
    const float subtitleGap = ScaleUi(7.0f);
    const float actionReserve = buttonWidth > 0.0f ? buttonWidth + ScaleUi(6.0f) : 0.0f;
    float x = rect.Min.x + padX;

    ImGui::PushClipRect(rect.Min, ImVec2(rect.Max.x - actionReserve, rect.Max.y), true);
    drawList->AddText(ImVec2(x, textY), ImGui::GetColorU32(visual.headerText), title ? title : "");
    x += titleSize.x + pillGap;

    const ImRect countPill(
        ImVec2(x, rect.Min.y + ScaleUi(4.0f)),
        ImVec2(x + countSize.x + pillPadX * 2.0f, rect.Max.y - ScaleUi(4.0f)));
    drawList->AddRectFilled(countPill.Min, countPill.Max, ImGui::GetColorU32(WithAlpha(visual.buttonHover, 0.34f)), ScaleUi(8.0f));
    drawList->AddText(
        ImVec2(countPill.Min.x + pillPadX, textY),
        ImGui::GetColorU32(visual.mutedText),
        countText.c_str());
    x = countPill.Max.x + subtitleGap;

    if (subtitle != nullptr && subtitle[0] != '\0' && x < rect.Max.x - actionReserve) {
        const std::string clipped = EllipsizeText(subtitle, std::max(0.0f, rect.Max.x - actionReserve - x));
        drawList->AddText(ImVec2(x, textY), ImGui::GetColorU32(visual.faintText), clipped.c_str());
        if (ImGui::IsMouseHoveringRect(ImVec2(x, rect.Min.y), ImVec2(rect.Max.x - actionReserve, rect.Max.y), true)
            && clipped != subtitle) {
            ImGui::SetTooltip("%s", subtitle);
        }
    }
    ImGui::PopClipRect();

    bool clicked = false;
    if (hasAction) {
        ImGui::SetCursorScreenPos(ImVec2(rect.Max.x - buttonWidth, rect.Min.y));
        clicked = BinderListTextActionButton(actionIcon, actionLabel, actionId, actionTooltip, ImVec2(buttonWidth, height));
    }
    ImGui::SetCursorScreenPos(ImVec2(rect.Min.x, rect.Max.y + ScaleUi(4.0f)));
    return clicked;
}

inline bool DrawBinderListSearchBox(const char* id, const char* hint, std::string& value) {
    const BinderListVisualStyle& visual = BinderListStyleTokens();
    const float gap = ScaleUi(4.0f);
    const float clearSide = ImGui::GetFrameHeight();
    const bool hasClear = !value.empty();
    const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float inputWidth = hasClear ? std::max(ScaleUi(48.0f), availableWidth - clearSide - gap) : availableWidth;
    const std::string searchHint = std::string(ui_icons::Search) + " " + (hint ? hint : "");

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ScaleUi(6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, ScaleUi(1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaleUi(8.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, visual.searchBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, visual.searchHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, visual.searchActive);
    ImGui::PushStyleColor(ImGuiCol_Border, WithAlpha(visual.panelBorder, 0.24f));
    ImGui::SetNextItemWidth(inputWidth);
    bool changed = InputTextWithHintString(id, searchHint.c_str(), value, ImGuiInputTextFlags_AutoSelectAll, 128);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);

    if (hasClear) {
        ImGui::SameLine(0.0f, gap);
        const std::string clearId = std::string(id ? id : "##search") + "_clear";
        if (BinderListFlatIconButton(ui_icons::Xmark, clearId.c_str(), UiSettings::Instance().Text(UiText::BinderClearSearch), ImVec2(clearSide, clearSide))) {
            value.clear();
            changed = true;
        }
    }
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorStartPos().x, ImGui::GetCursorPosY() + ScaleUi(2.0f)));
    return changed;
}

inline const char* InputModeLabel(InputMode mode) {
    UiSettings& ui = UiSettings::Instance();
    switch (mode) {
    case InputMode::Text:
        return ui.Text(UiText::InputModeText);
    case InputMode::ButtonsList:
        return ui.Text(UiText::InputModeButtonsList);
    case InputMode::ButtonsListText:
        return ui.Text(UiText::InputModeButtonsListText);
    }
    return ui.Text(UiText::InputModeText);
}

inline const char* QuickMenuModeLabel(QuickMenuActivationMode mode) {
    return UiSettings::Instance().Text(
        mode == QuickMenuActivationMode::Toggle ? UiText::QuickMenuModeToggle : UiText::QuickMenuModeHold);
}

} // namespace binder_internal
