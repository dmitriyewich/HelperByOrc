#include "binder_editor.h"

#include "icon_registry.h"
#include "ui_fonts.h"
#include "ui_icons.h"
#include "ui_settings.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <string>
#include <string_view>

namespace binder_editor {
namespace {

constexpr char kConfirmationSettingsPopupId[] = "binder_editor_confirmation_settings";

std::string Trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

float ScaleUi(float value) {
    return UiSettings::Instance().Scale(value);
}

ImVec2 ScaleUi(float x, float y) {
    return UiSettings::Instance().Scale(ImVec2(x, y));
}

std::string ResolveBindIconGlyph(std::string_view iconId) {
    if (!iconId.empty()) {
        if (std::string glyph = icon_registry::ResolveGlyph(iconId); !glyph.empty()) {
            return glyph;
        }
    }
    return ui_icons::Keyboard;
}

struct ImGuiStringUserData {
    std::string* value = nullptr;
};

int ImGuiStringResizeCallback(ImGuiInputTextCallbackData* data) {
    auto* userData = static_cast<ImGuiStringUserData*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize && userData && userData->value) {
        std::string& value = *userData->value;
        value.resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = value.data();
    }
    return 0;
}

bool InputTextString(const char* label, std::string& value, ImGuiInputTextFlags flags, std::size_t minBuffer) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputText(label, value.data(), value.capacity() + 1, flags, ImGuiStringResizeCallback, &userData);
}

bool InputTextWithHintString(
    const char* label,
    const char* hint,
    std::string& value,
    ImGuiInputTextFlags flags,
    std::size_t minBuffer) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputTextWithHint(
        label,
        hint,
        value.data(),
        value.capacity() + 1,
        flags,
        ImGuiStringResizeCallback,
        &userData);
}

bool IsUtf8ContinuationByte(unsigned char value) {
    return (value & 0xC0) == 0x80;
}

bool DecodeFirstUtf8Codepoint(std::string_view text, ImWchar& outCodepoint) {
    if (text.empty()) {
        return false;
    }

    const unsigned char first = static_cast<unsigned char>(text[0]);
    if (first < 0x80) {
        outCodepoint = first;
        return true;
    }

    if ((first & 0xE0) == 0xC0 && text.size() >= 2) {
        const unsigned char second = static_cast<unsigned char>(text[1]);
        if (!IsUtf8ContinuationByte(second)) {
            return false;
        }
        outCodepoint = static_cast<ImWchar>(((first & 0x1F) << 6) | (second & 0x3F));
        return true;
    }

    if ((first & 0xF0) == 0xE0 && text.size() >= 3) {
        const unsigned char second = static_cast<unsigned char>(text[1]);
        const unsigned char third = static_cast<unsigned char>(text[2]);
        if (!IsUtf8ContinuationByte(second) || !IsUtf8ContinuationByte(third)) {
            return false;
        }
        outCodepoint = static_cast<ImWchar>(((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F));
        return true;
    }

    if ((first & 0xF8) == 0xF0 && text.size() >= 4) {
        const unsigned char second = static_cast<unsigned char>(text[1]);
        const unsigned char third = static_cast<unsigned char>(text[2]);
        const unsigned char fourth = static_cast<unsigned char>(text[3]);
        if (!IsUtf8ContinuationByte(second) || !IsUtf8ContinuationByte(third) || !IsUtf8ContinuationByte(fourth)) {
            return false;
        }
        outCodepoint = static_cast<ImWchar>(
            ((first & 0x07) << 18) | ((second & 0x3F) << 12) | ((third & 0x3F) << 6) | (fourth & 0x3F));
        return true;
    }

    return false;
}

std::string Utf8TrimLastChar(std::string_view value) {
    if (value.empty()) {
        return {};
    }

    std::size_t size = value.size();
    while (size > 0) {
        --size;
        const unsigned char ch = static_cast<unsigned char>(value[size]);
        if ((ch & 0xC0) != 0x80) {
            break;
        }
    }
    return std::string(value.substr(0, size));
}

std::string EllipsizeText(std::string_view text, float maxWidth) {
    if (maxWidth <= 0.0f) {
        return {};
    }

    std::string result(text);
    if (result.empty() || ImGui::CalcTextSize(result.c_str()).x <= maxWidth) {
        return result;
    }

    constexpr char kEllipsis[] = "...";
    const float ellipsisWidth = ImGui::CalcTextSize(kEllipsis).x;
    if (ellipsisWidth >= maxWidth) {
        return kEllipsis;
    }

    while (!result.empty()) {
        result = Utf8TrimLastChar(result);
        const std::string candidate = result + kEllipsis;
        if (candidate.empty() || ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth) {
            return candidate;
        }
    }

    return kEllipsis;
}

bool TryCalcIconMetrics(
    const char* icon,
    ImFont* font,
    const float fontSize,
    ImVec2& glyphSize,
    ImVec2& glyphCenter) {
    if (!icon || !font || fontSize <= 0.0f) {
        return false;
    }

    ImWchar iconCodepoint = 0;
    if (!DecodeFirstUtf8Codepoint(icon, iconCodepoint)) {
        return false;
    }

    ImFontBaked* bakedFont = font->GetFontBaked(fontSize);
    if (!bakedFont) {
        return false;
    }

    const ImFontGlyph* glyph = bakedFont->FindGlyphNoFallback(iconCodepoint);
    if (!glyph) {
        return false;
    }

    const float glyphScale = fontSize / std::max(1.0f, bakedFont->Size);
    glyphSize = ImVec2((glyph->X1 - glyph->X0) * glyphScale, (glyph->Y1 - glyph->Y0) * glyphScale);
    glyphCenter = ImVec2((glyph->X0 + glyph->X1) * glyphScale * 0.5f, (glyph->Y0 + glyph->Y1) * glyphScale * 0.5f);
    return true;
}

void DrawCenteredIconGlyph(ImDrawList* drawList, const char* icon, const ImRect& rect, ImU32 color, float preferredFontSize = 0.0f) {
    if (!drawList || !icon || icon[0] == '\0') {
        return;
    }

    ImFont* font = ImGui::GetFont();
    float fontSize = std::max(1.0f, preferredFontSize > 0.0f ? preferredFontSize : ImGui::GetFontSize());
    ImVec2 glyphSize{};
    ImVec2 glyphCenter{};
    bool hasGlyphMetrics = TryCalcIconMetrics(icon, font, fontSize, glyphSize, glyphCenter);
    if (hasGlyphMetrics) {
        const float padding = std::max(1.0f, std::floor(rect.GetHeight() * 0.12f));
        const ImVec2 maxGlyphSize(
            std::max(1.0f, rect.GetWidth() - padding * 2.0f),
            std::max(1.0f, rect.GetHeight() - padding * 2.0f));
        const float fitScale = std::min(
            1.0f,
            std::min(maxGlyphSize.x / std::max(1.0f, glyphSize.x), maxGlyphSize.y / std::max(1.0f, glyphSize.y)));
        if (fitScale < 1.0f) {
            fontSize = std::max(1.0f, std::floor(fontSize * fitScale));
            hasGlyphMetrics = TryCalcIconMetrics(icon, font, fontSize, glyphSize, glyphCenter);
        }
    }

    ImVec2 iconPos{};
    if (hasGlyphMetrics) {
        const ImVec2 rectCenter(rect.Min.x + rect.GetWidth() * 0.5f, rect.Min.y + rect.GetHeight() * 0.5f);
        iconPos.x = std::floor(rectCenter.x - glyphCenter.x + 0.5f);
        iconPos.y = std::floor(rectCenter.y - glyphCenter.y + 0.5f);
    } else {
        const ImVec2 iconSize = font ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, icon) : ImGui::CalcTextSize(icon);
        iconPos.x = std::floor(rect.Min.x + (rect.GetWidth() - iconSize.x) * 0.5f + 0.5f);
        iconPos.y = std::floor(rect.Min.y + (rect.GetHeight() - iconSize.y) * 0.5f + 0.5f);
    }

    const ImVec4 clipRect(rect.Min.x, rect.Min.y, rect.Max.x, rect.Max.y);
    drawList->AddText(font, fontSize, iconPos, color, icon, nullptr, 0.0f, &clipRect);
}

bool IconButton(const char* icon, const char* id, const char* tooltip, const ImVec2& size) {
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        min,
        max,
        ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button),
        style.FrameRounding);
    if (style.FrameBorderSize > 0.0f) {
        drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding, 0, style.FrameBorderSize);
    }
    DrawCenteredIconGlyph(drawList, icon, ImRect(min, max), ImGui::GetColorU32(ImGuiCol_Text));
    if (tooltip && tooltip[0] != '\0' && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
}

bool IconToggleButton(const char* icon, const char* id, const char* tooltip, bool& value) {
    const ImVec2 size(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    if (clicked) {
        value = !value;
    }

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 bgColor = value
        ? ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_ButtonActive)
        : ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    const ImU32 borderColor = value ? ImGui::GetColorU32(ImVec4(0.48f, 0.63f, 0.96f, 0.95f)) : ImGui::GetColorU32(ImGuiCol_Border);
    drawList->AddRectFilled(min, max, bgColor, style.FrameRounding);
    drawList->AddRect(min, max, borderColor, style.FrameRounding, 0, std::max(1.0f, style.FrameBorderSize));
    DrawCenteredIconGlyph(
        drawList,
        icon,
        ImRect(min, max),
        ImGui::GetColorU32(value ? ImGuiCol_Text : ImGuiCol_TextDisabled),
        std::floor(size.y * 0.72f));

    if (tooltip && tooltip[0] != '\0' && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
}

const char* HotkeyModeLabel(HotkeyMode mode) {
    UiSettings& ui = UiSettings::Instance();
    return ui.Text(mode == HotkeyMode::OrderedCombo ? UiText::HotkeyModeOrderedCombo : UiText::HotkeyModeModifierTrigger);
}

void ApplyDerivedActivationFlags(HotkeyEntry& hotkey, bool trimText) {
    if (trimText) {
        hotkey.command = Trim(hotkey.command);
        hotkey.textTrigger.text = Trim(hotkey.textTrigger.text);
    }

    const bool hasCommand = !Trim(hotkey.command).empty();
    const bool hasTrigger = !Trim(hotkey.textTrigger.text).empty();
    hotkey.commandEnabled = hasCommand;
    hotkey.textTrigger.enabled = hasTrigger;

    if (!hasCommand) {
        hotkey.commandConfirmation.enabled = false;
    }
    if (!hasTrigger) {
        hotkey.textTrigger.pattern = false;
        hotkey.textConfirmation.enabled = false;
    }
}

void NormalizeConfirmationKeys(HotkeyEntry& hotkey) {
    hotkey.textConfirmation.key = ::hotkeys::IsHotkeyKey(hotkey.textConfirmation.key)
        ? ::hotkeys::NormalizeKey(hotkey.textConfirmation.key)
        : kBinderDefaultConfirmKey;
    hotkey.textConfirmation.cancelKey = ::hotkeys::IsHotkeyKey(hotkey.textConfirmation.cancelKey)
        ? ::hotkeys::NormalizeKey(hotkey.textConfirmation.cancelKey)
        : kBinderDefaultCancelKey;
}

void DrawTextTooltip(const char* text) {
    if (text && text[0] != '\0' && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text);
    }
}

void DrawHotkeyModeCombo(State& editor) {
    const HotkeyMode hotkeyModes[] = { HotkeyMode::ModifierTrigger, HotkeyMode::OrderedCombo };
    int hotkeyModeIndex = editor.draft.hotkeyMode == HotkeyMode::OrderedCombo ? 1 : 0;
    ImGui::SetNextItemWidth(ImGui::GetFrameHeight());
    if (ImGui::BeginCombo("##binder_editor_hotkey_mode", "", ImGuiComboFlags_NoPreview)) {
        for (int i = 0; i < IM_ARRAYSIZE(hotkeyModes); ++i) {
            const bool selected = i == hotkeyModeIndex;
            if (ImGui::Selectable(HotkeyModeLabel(hotkeyModes[i]), selected)) {
                hotkeyModeIndex = i;
                editor.draft.hotkeyMode = hotkeyModes[hotkeyModeIndex];
                editor.draft.keys = ::hotkeys::NormalizeCombo(editor.draft.keys, editor.draft.hotkeyMode);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    DrawTextTooltip(HotkeyModeLabel(editor.draft.hotkeyMode));
}

void DrawCheckboxWithTooltip(const char* label, bool& value, const char* tooltip) {
    ImGui::Checkbox(label, &value);
    DrawTextTooltip(tooltip);
}

void DrawLabelSpacer() {
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
}

void DrawTopAlignedSeparatorText(const char* label) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float lift = std::max(0.0f, style.WindowPadding.y - ScaleUi(1.0f));
    ImGui::SetCursorPosY(std::max(0.0f, ImGui::GetCursorPosY() - lift));
    ImGui::SeparatorText(label);
    ImGui::SetCursorPosY(std::max(0.0f, ImGui::GetCursorPosY() - (style.ItemSpacing.y + ScaleUi(4.0f))));
}

void CompactVerticalGap(float targetGap) {
    const float itemSpacingY = ImGui::GetStyle().ItemSpacing.y;
    if (itemSpacingY > targetGap) {
        ImGui::SetCursorPosY(std::max(0.0f, ImGui::GetCursorPosY() - (itemSpacingY - targetGap)));
    }
}

void CenterNextItem(float itemWidth) {
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > itemWidth) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - itemWidth) * 0.5f);
    }
}

} // namespace

void NormalizeDraftForOpen(State& editor) {
    if (!editor.draft.commandEnabled) {
        editor.draft.command.clear();
    }
    if (!editor.draft.textTrigger.enabled) {
        editor.draft.textTrigger.text.clear();
    }
    NormalizeConfirmationKeys(editor.draft);
    ApplyDerivedActivationFlags(editor.draft, false);
}

HotkeyEntry BuildComparableDraft(const State& editor) {
    HotkeyEntry comparable = editor.draft;
    comparable.conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    comparable.conditionsCombine = ConditionCombineMode::RequireAny;
    comparable.repeatIntervalMs = std::max(comparable.repeatIntervalMs, 0);
    NormalizeConfirmationKeys(comparable);
    ApplyDerivedActivationFlags(comparable, true);
    return comparable;
}

void NormalizeDraftForSave(HotkeyEntry& hotkey) {
    std::erase_if(hotkey.messages, [](const HotkeyMessage& message) {
        return Trim(message.text).empty();
    });
    hotkey.repeatIntervalMs = std::max(hotkey.repeatIntervalMs, 0);
    NormalizeConfirmationKeys(hotkey);
    ApplyDerivedActivationFlags(hotkey, true);
}

void DrawLaunchPanel(State& editor, const LaunchPanelActions& actions) {
    UiSettings& ui = UiSettings::Instance();
    ApplyDerivedActivationFlags(editor.draft, false);

    DrawTopAlignedSeparatorText(ui.Text(UiText::EditorPrimaryLaunch));

    const int activeConditions = static_cast<int>(std::count(editor.draft.conditions.begin(), editor.draft.conditions.end(), true));
    const std::string conditionsLabel = activeConditions > 0
        ? ui.Format(UiText::EditorConditionsButtonCount, activeConditions)
        : std::string(ui.Text(UiText::EditorOpenConditions));
    const std::string conditionsTooltip = activeConditions > 0
        ? ui.Format(UiText::EditorConditionsCount, activeConditions)
        : std::string(ui.Text(UiText::EditorConditionsNone));

    const ImGuiStyle& style = ImGui::GetStyle();
    const float frameHeight = ImGui::GetFrameHeight();
    const float hotkeyButtonWidth = ScaleUi(156.0f);
    const float hotkeyColumnWidth = hotkeyButtonWidth + style.ItemSpacing.x + frameHeight;
    const float commandColumnWidth = ScaleUi(210.0f);
    const ImVec2 launchTableSize(ImGui::GetContentRegionAvail().x, 0.0f);

    if (ImGui::BeginTable("##binder_editor_launch_line1", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings, launchTableSize)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("conditions", ImGuiTableColumnFlags_WidthFixed, ScaleUi(124.0f));
        ImGui::TableSetupColumn("quick", ImGuiTableColumnFlags_WidthFixed, frameHeight + style.CellPadding.x * 2.0f);
        ImGui::TableSetupColumn("multi", ImGuiTableColumnFlags_WidthFixed, ScaleUi(136.0f));
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", ui.Text(UiText::NameOptional));
        ImGui::TableSetColumnIndex(1);
        DrawLabelSpacer();
        ImGui::TableSetColumnIndex(2);
        DrawLabelSpacer();
        ImGui::TableSetColumnIndex(3);
        DrawLabelSpacer();

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (editor.focusNamePending) {
            ImGui::SetKeyboardFocusHere();
            editor.focusNamePending = false;
        }
        const std::string iconPickerPopup = std::string(ui.Text(UiText::IconPickerTitle)) + "##binder_editor_icon_picker";
        const std::string bindIcon = ResolveBindIconGlyph(editor.draft.iconId);
        if (IconButton(bindIcon.c_str(), "##binder_editor_icon", ui.Text(UiText::IconPickerTitle), ImVec2(frameHeight, frameHeight))) {
            icon_picker::OpenPopup(iconPickerPopup.c_str());
        }
        std::string selectedIconId;
        if (icon_picker::DrawPopup(editor.iconPicker, icon_picker::Options{ iconPickerPopup.c_str(), ImVec2(560.0f, 460.0f) }, selectedIconId)) {
            editor.draft.iconId = icon_registry::NormalizeIconId(selectedIconId);
        }
        ImGui::SameLine(0.0f, ScaleUi(6.0f));
        ImGui::SetNextItemWidth(-FLT_MIN);
        InputTextString("##binder_editor_name", editor.draft.label, ImGuiInputTextFlags_AutoSelectAll, 160);

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button(conditionsLabel.c_str(), ImVec2(-FLT_MIN, 0.0f))) {
            editor.conditionsPopupPending = true;
        }
        DrawTextTooltip(conditionsTooltip.c_str());

        ImGui::TableSetColumnIndex(2);
        CenterNextItem(frameHeight);
        IconToggleButton(ui_icons::Bolt, "##binder_editor_quick_menu", ui.Text(UiText::EditorQuickMenuTooltip), editor.draft.quickMenu);

        ImGui::TableSetColumnIndex(3);
        if (ImGui::Button(ui.Text(UiText::EditorOpenMultiInput), ImVec2(-FLT_MIN, 0.0f))) {
            editor.multiInputPopupPending = true;
        }
        DrawTextTooltip(ui.Text(UiText::EditorMultiInputHint));

        ImGui::EndTable();
    }

    CompactVerticalGap(0.0f);
    if (ImGui::BeginTable("##binder_editor_launch_line2", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings, launchTableSize)) {
        ImGui::TableSetupColumn("hotkey", ImGuiTableColumnFlags_WidthFixed, hotkeyColumnWidth);
        ImGui::TableSetupColumn("trigger", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("command", ImGuiTableColumnFlags_WidthFixed, commandColumnWidth);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", ui.Text(UiText::ColumnHotkey));
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("%s", ui.Text(UiText::TextTrigger));
        DrawTextTooltip(ui.Text(UiText::EditorTriggerHint));
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled("%s", ui.Text(UiText::Command));

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        const std::string hotkeyText = editor.draft.keys.empty()
            ? ui.Text(UiText::HotkeyNotSet)
            : ::hotkeys::ToString(editor.draft.keys, editor.draft.hotkeyMode);
        if (ImGui::Button(hotkeyText.c_str(), ImVec2(hotkeyButtonWidth, 0.0f)) && actions.beginHotkeyCapture) {
            actions.beginHotkeyCapture();
        }
        ImGui::SameLine();
        DrawHotkeyModeCombo(editor);

        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        DrawTextTriggerInput(
            editor,
            "##binder_editor_trigger",
            ui.Text(UiText::EditorTriggerExample));

        ImGui::TableSetColumnIndex(2);
        ImGui::SetNextItemWidth(-FLT_MIN);
        InputTextString("##binder_editor_command", editor.draft.command, ImGuiInputTextFlags_AutoSelectAll, 128);

        ImGui::EndTable();
    }

    ApplyDerivedActivationFlags(editor.draft, false);

    CompactVerticalGap(ScaleUi(1.0f));
    if (ImGui::BeginTable("##binder_editor_launch_line3", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings, launchTableSize)) {
        ImGui::TableSetupColumn("hotkey", ImGuiTableColumnFlags_WidthFixed, hotkeyColumnWidth);
        ImGui::TableSetupColumn("trigger", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("command", ImGuiTableColumnFlags_WidthFixed, commandColumnWidth);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        const float repeatLineStartX = ImGui::GetCursorPosX();
        DrawCheckboxWithTooltip(ui.Text(UiText::Repeat), editor.draft.repeatMode, ui.Text(UiText::EditorRepeatTooltip));
        ImGui::SameLine();
        ImGui::BeginDisabled(!editor.draft.repeatMode);
        const float repeatInputWidth = std::max(
            ScaleUi(64.0f),
            hotkeyButtonWidth - (ImGui::GetCursorPosX() - repeatLineStartX));
        ImGui::SetNextItemWidth(repeatInputWidth);
        ImGui::InputInt("##binder_editor_repeat", &editor.draft.repeatIntervalMs, 0, 0);
        if (editor.draft.repeatIntervalMs < 0) {
            editor.draft.repeatIntervalMs = 0;
        }
        ImGui::EndDisabled();
        DrawTextTooltip(ui.Text(UiText::RepeatInterval));

        ImGui::TableSetColumnIndex(1);
        const std::string patternButtonLabel = std::string(
            editor.draft.textTrigger.pattern ? ui_icons::ToggleOn : ui_icons::ToggleOff)
            + " " + ui.Text(UiText::EditorTogglePattern);
        if (editor.draft.textTrigger.pattern) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(patternButtonLabel.c_str())) {
            editor.textPatternHelper.popupPending = true;
        }
        if (editor.draft.textTrigger.pattern) {
            ImGui::PopStyleColor();
        }
        DrawTextTooltip(ui.Text(UiText::EditorTriggerPatternTooltip));
        ImGui::SameLine();
        ImGui::BeginDisabled(!editor.draft.textTrigger.enabled);
        DrawCheckboxWithTooltip(
            ui.Text(UiText::EditorToggleTextConfirm),
            editor.draft.textConfirmation.enabled,
            ui.Text(UiText::TextConfirmation));
        ImGui::EndDisabled();

        ImGui::TableSetColumnIndex(2);
        const float commandLineStartX = ImGui::GetCursorPosX();
        ImGui::BeginDisabled(!editor.draft.commandEnabled);
        DrawCheckboxWithTooltip(
            ui.Text(UiText::EditorToggleCommandConfirm),
            editor.draft.commandConfirmation.enabled,
            ui.Text(UiText::CommandConfirmation));
        ImGui::EndDisabled();
        const bool hasConfirmation = editor.draft.textConfirmation.enabled || editor.draft.commandConfirmation.enabled;
        ImGui::SameLine();
        const float settingsButtonWidth = std::max(
            ScaleUi(72.0f),
            commandColumnWidth - (ImGui::GetCursorPosX() - commandLineStartX));
        ImGui::BeginDisabled(!hasConfirmation);
        if (ImGui::Button(ui.Text(UiText::EditorConfirmationSettings), ImVec2(settingsButtonWidth, 0.0f)) && hasConfirmation) {
            editor.confirmationSettingsPopupPending = true;
        }
        ImGui::EndDisabled();
        DrawTextTooltip(hasConfirmation ? ui.Text(UiText::EditorConfirmationHint) : ui.Text(UiText::EditorConfirmationSettingsDisabledTooltip));

        ImGui::EndTable();
    }

    ApplyDerivedActivationFlags(editor.draft, false);
}

void DrawConfirmationSettingsPopup(State& editor, const LaunchPanelActions& actions) {
    UiSettings& ui = UiSettings::Instance();
    const std::string title = std::string(ui.Text(UiText::EditorConfirmationSettings)) + "###" + kConfirmationSettingsPopupId;
    if (editor.confirmationSettingsPopupPending) {
        ImGui::OpenPopup(title.c_str());
        editor.confirmationSettingsPopupPending = false;
    }

    bool open = true;
    const float confirmationMaxWidth = std::max(ScaleUi(320.0f), ImGui::GetIO().DisplaySize.x - ScaleUi(24.0f));
    ImGui::SetNextWindowSize(ImVec2(std::min(ScaleUi(520.0f), confirmationMaxWidth), 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title.c_str(), &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    if (editor.draft.textConfirmation.enabled) {
        ImGui::Checkbox(ui.Text(UiText::TriggerWaitWithoutTimeout), &editor.draft.textConfirmation.waitForResolution);
        DrawTextTooltip(ui.Text(UiText::TriggerWaitTimeoutHint));
    }
    if (editor.draft.commandConfirmation.enabled) {
        ImGui::Checkbox(ui.Text(UiText::CommandWaitWithoutTimeout), &editor.draft.commandConfirmation.waitForResolution);
    }

    ImGui::Spacing();
    if (ImGui::BeginTable("##binder_editor_confirmation_keys", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("confirm", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("cancel", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled(
            "%s",
            ui.Format(UiText::ConfirmKeyFormat, ::hotkeys::KeyName(editor.draft.textConfirmation.key).c_str()).c_str());
        if (ImGui::Button((std::string(ui_icons::Keyboard) + " " + ui.Text(UiText::Change) + "##confirm").c_str(), ImVec2(-FLT_MIN, 0.0f))
            && actions.beginConfirmKeyCapture) {
            actions.beginConfirmKeyCapture();
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled(
            "%s",
            ui.Format(UiText::CancelKeyFormat, ::hotkeys::KeyName(editor.draft.textConfirmation.cancelKey).c_str()).c_str());
        if (ImGui::Button((std::string(ui_icons::Keyboard) + " " + ui.Text(UiText::Change) + "##cancel").c_str(), ImVec2(-FLT_MIN, 0.0f))
            && actions.beginCancelKeyCapture) {
            actions.beginCancelKeyCapture();
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("%s", ui.Text(UiText::EditorConfirmationHint));
    ImGui::Spacing();
    if (ImGui::Button(ui.Text(UiText::Close), ScaleUi(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void DrawInline(State& editor, const ShellActions& actions) {
    UiSettings& ui = UiSettings::Instance();
    const auto [prevIndex, nextIndex] = actions.editorNeighborIndices ? actions.editorNeighborIndices() : std::pair<int, int>{ -1, -1 };
    const bool hasUnsavedChanges = actions.hasUnsavedChanges ? actions.hasUnsavedChanges() : false;

    const std::string title = ui.Text(editor.isNew ? UiText::NewBindTitle : UiText::EditBindTitle);
    std::string breadcrumb = ui.Text(UiText::BinderSectionTitle);
    if (editor.draft.folderPath.empty()) {
        breadcrumb += " / ";
        breadcrumb += ui.Text(UiText::BinderRootName);
    } else {
        for (const std::string& part : editor.draft.folderPath) {
            breadcrumb += " / " + part;
        }
    }
    breadcrumb += " / " + (Trim(editor.draft.label).empty()
        ? std::string(ui.Text(editor.isNew ? UiText::NewBindTitle : UiText::EditBindTitle))
        : editor.draft.label);

    const ImVec4 headerBg(0.14f, 0.16f, 0.20f, 0.98f);
    const ImVec4 panelBg(0.12f, 0.14f, 0.18f, 0.98f);
    const ImVec4 footerBg(0.11f, 0.13f, 0.17f, 0.98f);
    const std::string backLabel = std::string(ui_icons::ChevronLeft) + " " + ui.Text(UiText::EditorBack);
    const std::string previousLabel = std::string(ui_icons::ChevronLeft) + " " + ui.Text(UiText::EditorPreviousBind);
    const std::string nextLabel = std::string(ui.Text(UiText::EditorNextBind)) + " " + std::string(ui_icons::ChevronRight);
    const std::string variablesLabel = std::string(ui_icons::Tags) + " " + ui.Text(UiText::EditorVariables);
    const std::string saveLabel = std::string(ui_icons::SaveDisk) + " " + ui.Text(UiText::Save);
    const float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;

    const auto requestAction = [&](State::PendingAction action, int targetIndex = -1) {
        if (actions.requestAction) {
            actions.requestAction(action, targetIndex);
        }
    };
    const auto alignRight = [](float width) {
        const float startX = ImGui::GetCursorPosX();
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > width) {
            ImGui::SetCursorPosX(startX + avail - width);
        }
    };
    const auto pushPrimaryButtonStyle = []() {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.39f, 0.68f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.33f, 0.48f, 0.81f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.31f, 0.56f, 1.0f));
    };
    const auto popPrimaryButtonStyle = []() {
        ImGui::PopStyleColor(3);
    };

    ImGui::PushStyleColor(ImGuiCol_ChildBg, headerBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(12.0f, 6.0f));
    if (ImGui::BeginChild("##binder_editor_header", ImVec2(0.0f, ScaleUi(52.0f)), ImGuiChildFlags_Borders)) {
        if (ImGui::BeginTable("##binder_editor_header_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 0.56f);
            ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 0.44f);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            const float backButtonWidth = ScaleUi(92.0f);
            if (ImGui::Button(backLabel.c_str(), ImVec2(backButtonWidth, 0.0f))) {
                requestAction(State::PendingAction::Close);
            }
            ImGui::SameLine(0.0f, ScaleUi(10.0f));
            ImGui::Checkbox(ui.Text(UiText::Enabled), &editor.draft.enabled);
            ImGui::SameLine(0.0f, ScaleUi(8.0f));
            const float headerTitleStartX = ImGui::GetCursorPosX();
            const float headerTitleTopY = ImGui::GetCursorPosY();
            ImGui::TextUnformatted(title.c_str());
            if (hasUnsavedChanges) {
                ImGui::SameLine(0.0f, ScaleUi(10.0f));
                ImGui::TextColored(ImVec4(0.96f, 0.68f, 0.25f, 1.0f), "%s", ui_icons::Star);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::SetTooltip("%s", ui.Text(UiText::EditorUnsaved));
                }
            }
            const float headerBreadcrumbY = std::max(headerTitleTopY + ScaleUi(15.0f), ImGui::GetCursorPosY() - ScaleUi(6.0f));
            ImGui::SetCursorPos(ImVec2(headerTitleStartX, headerBreadcrumbY));
            std::string headerBreadcrumb;
            {
                const ui_fonts::ScopedFontSize fontScope(ui_fonts::FontSizeForScale(0.75f));
                headerBreadcrumb = EllipsizeText(breadcrumb, std::max(0.0f, ImGui::GetContentRegionAvail().x * 2.0f));
                ImGui::TextDisabled("%s", headerBreadcrumb.c_str());
            }
            if (headerBreadcrumb != breadcrumb && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", breadcrumb.c_str());
            }

            ImGui::TableSetColumnIndex(1);
            const float headerAvailableWidth = ImGui::GetContentRegionAvail().x;
            const float desiredPreviousButtonWidth = ScaleUi(148.0f);
            const float desiredNextButtonWidth = ScaleUi(136.0f);
            const float headerButtonScale = std::min(
                1.0f,
                std::max(0.0f, headerAvailableWidth - itemSpacingX)
                    / (desiredPreviousButtonWidth + desiredNextButtonWidth));
            const float previousButtonWidth = desiredPreviousButtonWidth * headerButtonScale;
            const float nextButtonWidth = desiredNextButtonWidth * headerButtonScale;
            const float headerActionWidth = previousButtonWidth + nextButtonWidth + itemSpacingX;
            alignRight(headerActionWidth);
            ImGui::BeginDisabled(prevIndex < 0);
            if (ImGui::Button(previousLabel.c_str(), ImVec2(previousButtonWidth, 0.0f))) {
                requestAction(State::PendingAction::Navigate, prevIndex);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(nextIndex < 0);
            if (ImGui::Button(nextLabel.c_str(), ImVec2(nextButtonWidth, 0.0f))) {
                requestAction(State::PendingAction::Navigate, nextIndex);
            }
            ImGui::EndDisabled();

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, panelBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(16.0f, 7.0f));
    if (ImGui::BeginChild("##binder_editor_launch_panel", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        DrawLaunchPanel(editor, actions.launchActions);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, panelBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(8.0f, 10.0f));
    if (ImGui::BeginChild("##binder_editor_work", ImVec2(0.0f, -ScaleUi(50.0f)), ImGuiChildFlags_Borders)) {
        if (ImGui::BeginTabBar("##binder_editor_work_tabs")) {
            if (ImGui::BeginTabItem(
                    ui.Text(UiText::EditorScenarioTab),
                    nullptr,
                    editor.tabSelectionPending && editor.activeTab == State::Tab::Scenario ? ImGuiTabItemFlags_SetSelected : 0)) {
                editor.activeTab = State::Tab::Scenario;
                if (actions.drawScenarioTab) {
                    actions.drawScenarioTab();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(
                    ui.Text(UiText::EditorInputFieldsTab),
                    nullptr,
                    editor.tabSelectionPending && editor.activeTab == State::Tab::InputFields ? ImGuiTabItemFlags_SetSelected : 0)) {
                editor.activeTab = State::Tab::InputFields;
                ImGui::TextDisabled("%s", ui.Text(UiText::EditorVariablesHint));
                ImGui::Spacing();
                if (actions.drawInputEditor) {
                    actions.drawInputEditor();
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
            editor.tabSelectionPending = false;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0.0f, ScaleUi(2.0f)));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, footerBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(12.0f, 6.0f));
    if (ImGui::BeginChild("##binder_editor_footer", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        if (ImGui::BeginTable("##binder_editor_footer_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 0.50f);
            ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 0.50f);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            if (ImGui::Button(variablesLabel.c_str(), ScaleUi(170.0f, 0.0f))) {
                editor.variablesPopupPending = true;
            }

            ImGui::TableSetColumnIndex(1);
            const float footerAvailableWidth = ImGui::GetContentRegionAvail().x;
            const float desiredSaveButtonWidth = ScaleUi(190.0f);
            const float desiredCancelButtonWidth = ScaleUi(130.0f);
            const float footerButtonScale = std::min(
                1.0f,
                std::max(0.0f, footerAvailableWidth - itemSpacingX)
                    / (desiredSaveButtonWidth + desiredCancelButtonWidth));
            const float saveButtonWidth = desiredSaveButtonWidth * footerButtonScale;
            const float cancelButtonWidth = desiredCancelButtonWidth * footerButtonScale;
            const float footerActionWidth = saveButtonWidth + cancelButtonWidth + itemSpacingX;
            alignRight(footerActionWidth);
            pushPrimaryButtonStyle();
            if (ImGui::Button(saveLabel.c_str(), ImVec2(saveButtonWidth, 0.0f)) && actions.saveRequested) {
                actions.saveRequested();
            }
            popPrimaryButtonStyle();
            ImGui::SameLine();
            if (ImGui::Button(ui.Text(UiText::Cancel), ImVec2(cancelButtonWidth, 0.0f))) {
                requestAction(State::PendingAction::Close);
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (actions.drawCapturePopup) {
        actions.drawCapturePopup();
    }
    if (actions.drawConditionsPopup) {
        actions.drawConditionsPopup();
    }
    DrawConfirmationSettingsPopup(editor, actions.launchActions);
    DrawTextPatternHelperPopup(editor);
    if (actions.drawVariablesPopup) {
        actions.drawVariablesPopup();
    }
    if (actions.drawMultiInputPopup) {
        actions.drawMultiInputPopup();
    }
    if (actions.drawDiscardPopup) {
        actions.drawDiscardPopup();
    }
}

} // namespace binder_editor
