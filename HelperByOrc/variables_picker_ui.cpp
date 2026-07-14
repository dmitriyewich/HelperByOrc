#include "variables_picker_ui.h"

#include "ui_icons.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>

namespace variables_picker {
namespace {

struct ImGuiStringUserData {
    std::string* value = nullptr;
};

struct VisualStyle {
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
    ImVec4 ok{};
    ImVec4 warn{};
    ImVec4 danger{};
};

constexpr std::size_t kVariablesCategoryCount = 13;

constexpr std::size_t FnvPrimeValue() {
    if constexpr (sizeof(std::size_t) == 8) {
        return 1099511628211ull;
    } else {
        return 16777619u;
    }
}

constexpr std::size_t FnvOffsetValue() {
    if constexpr (sizeof(std::size_t) == 8) {
        return 1469598103934665603ull;
    } else {
        return 2166136261u;
    }
}

std::size_t HashCombine(std::size_t hash, std::string_view value) {
    constexpr std::size_t kFnvPrime = FnvPrimeValue();
    for (const unsigned char ch : value) {
        hash ^= static_cast<std::size_t>(ch);
        hash *= kFnvPrime;
    }
    return hash;
}

std::size_t HashCombine(std::size_t hash, std::size_t value) {
    constexpr std::size_t kFnvPrime = FnvPrimeValue();
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        hash ^= static_cast<std::size_t>((value >> (i * 8)) & 0xFFu);
        hash *= kFnvPrime;
    }
    return hash;
}

std::size_t EntriesHash(const std::vector<Entry>& entries) {
    std::size_t hash = FnvOffsetValue();
    hash = HashCombine(hash, entries.size());
    for (const Entry& entry : entries) {
        hash = HashCombine(hash, static_cast<std::size_t>(entry.kind));
        hash = HashCombine(hash, static_cast<std::size_t>(entry.category));
        hash = HashCombine(hash, entry.id);
        hash = HashCombine(hash, entry.name);
        hash = HashCombine(hash, entry.token);
        hash = HashCombine(hash, entry.example);
        hash = HashCombine(hash, static_cast<std::size_t>(entry.descriptionText));
        hash = HashCombine(hash, entry.description);
        hash = HashCombine(hash, entry.value);
        hash = HashCombine(hash, entry.action ? 1u : 0u);
    }
    return hash;
}

std::size_t CategoryIndex(Category category) {
    return static_cast<std::size_t>(category);
}

float ScaleUi(float value) {
    return UiSettings::Instance().Scale(value);
}

ImVec2 ScaleUi(float x, float y) {
    return UiSettings::Instance().Scale(ImVec2(x, y));
}

ImVec4 WithAlpha(ImVec4 color, float alpha) {
    color.w = alpha;
    return color;
}

ImVec4 BlendColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t);
}

VisualStyle StyleTokens() {
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

    VisualStyle style;
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
    style.ok = WithAlpha(BlendColor(text, buttonActive, 0.34f), 0.96f);
    style.warn = ImVec4(0.97f, 0.78f, 0.38f, 0.96f);
    style.danger = WithAlpha(BlendColor(text, buttonHovered, 0.18f), 0.92f);
    return style;
}

int InputTextResizeCallback(ImGuiInputTextCallbackData* data) {
    auto* userData = static_cast<ImGuiStringUserData*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        IM_ASSERT(userData && userData->value);
        userData->value->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = userData->value->data();
    }
    return 0;
}

bool InputTextWithHintString(
    const char* label,
    const char* hint,
    std::string& value,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 256) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputTextWithHint(label, hint, value.data(), value.capacity() + 1, flags, InputTextResizeCallback, &userData);
}

bool InputTextString(
    const char* label,
    std::string& value,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 256) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputText(label, value.data(), value.capacity() + 1, flags, InputTextResizeCallback, &userData);
}

bool InputTextMultilineString(
    const char* label,
    std::string& value,
    const ImVec2& size,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 512) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputTextMultiline(
        label,
        value.data(),
        value.capacity() + 1,
        size,
        flags,
        InputTextResizeCallback,
        &userData);
}

std::string ToLowerAscii(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

std::string TrimAscii(std::string_view value) {
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

bool DecodeFirstUtf8Codepoint(std::string_view text, ImWchar& outCodepoint) {
    outCodepoint = 0;
    if (text.empty()) {
        return false;
    }

    const unsigned char* s = reinterpret_cast<const unsigned char*>(text.data());
    const unsigned char first = s[0];
    if (first < 0x80) {
        outCodepoint = first;
        return true;
    }

    int extraBytes = 0;
    ImWchar codepoint = 0;
    if ((first & 0xE0) == 0xC0) {
        extraBytes = 1;
        codepoint = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
        extraBytes = 2;
        codepoint = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        extraBytes = 3;
        codepoint = first & 0x07;
    } else {
        return false;
    }

    if (text.size() < static_cast<std::size_t>(extraBytes + 1)) {
        return false;
    }

    for (int i = 1; i <= extraBytes; ++i) {
        const unsigned char ch = s[i];
        if ((ch & 0xC0) != 0x80) {
            return false;
        }
        codepoint = static_cast<ImWchar>((codepoint << 6) | (ch & 0x3F));
    }

    outCodepoint = codepoint;
    return true;
}

bool TryCalcIconMetrics(const char* icon, ImFont* font, float fontSize, ImVec2& glyphSize, ImVec2& glyphCenter) {
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

void DrawCenteredIconGlyph(
    ImDrawList* drawList,
    const char* icon,
    const ImRect& rect,
    ImU32 color,
    float preferredFontSize = 0.0f) {
    if (!drawList || !icon || icon[0] == '\0') {
        return;
    }

    ImFont* font = ImGui::GetFont();
    float fontSize = preferredFontSize > 0.0f ? preferredFontSize : ImGui::GetFontSize();
    fontSize = std::max(1.0f, fontSize);

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

void DrawPanelBackground(const ImVec2& size, const VisualStyle& visual) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImRect rect(pos, ImVec2(pos.x + std::max(1.0f, size.x), pos.y + std::max(1.0f, size.y)));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float rounding = ScaleUi(7.0f);
    drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(visual.panelBg), rounding);
    drawList->AddRect(rect.Min, rect.Max, ImGui::GetColorU32(visual.panelBorder), rounding, 0, ScaleUi(1.0f));
}

bool BeginPanel(const char* id, const ImVec2& size, const VisualStyle& visual, ImGuiWindowFlags flags = 0) {
    DrawPanelBackground(size, visual);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(8.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    return ImGui::BeginChild(id, size, ImGuiChildFlags_None, flags | ImGuiWindowFlags_NoBackground);
}

void EndPanel() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void DrawRowBackground(ImDrawList* drawList, const ImRect& rowRect, int rowIndex, bool selected, bool hovered, const VisualStyle& visual) {
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

bool FlatIconButton(
    const char* icon,
    const char* id,
    const char* tooltip,
    const ImVec2& size,
    const VisualStyle& visual,
    ImVec4 iconColor = ImVec4(0.0f, 0.0f, 0.0f, -1.0f),
    bool enabled = true) {
    const bool clickedRaw = ImGui::InvisibleButton(id, size);
    const bool clicked = enabled && clickedRaw;
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

    if (tooltip && tooltip[0] != '\0' && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
}

bool TextButton(
    const char* icon,
    const char* label,
    const char* id,
    const ImVec2& size,
    const VisualStyle& visual,
    bool enabled = true,
    const char* tooltip = nullptr) {
    if (!enabled) {
        ImGui::BeginDisabled();
    }
    const bool clicked = ImGui::Button((std::string(icon && icon[0] ? icon : "") + (icon && icon[0] ? " " : "") + label + id).c_str(), size);
    if (!enabled) {
        ImGui::EndDisabled();
    }
    if (tooltip && tooltip[0] != '\0' && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    (void)visual;
    return enabled && clicked;
}

ImVec4 BadgeColor(const Entry& entry, const VisualStyle& visual) {
    if (entry.action) {
        return visual.warn;
    }
    switch (entry.kind) {
    case EntryKind::Function:
        return visual.ok;
    case EntryKind::Custom:
        return ImVec4(0.78f, 0.62f, 0.92f, 0.96f);
    case EntryKind::Parameter:
        return ImVec4(0.55f, 0.86f, 0.98f, 0.96f);
    case EntryKind::Simple:
    default:
        return visual.mutedText;
    }
}

const char* KindBadgeLabel(const Entry& entry, UiSettings& ui) {
    if (entry.action) {
        return ui.Text(UiText::VariablesBadgeAction);
    }
    switch (entry.kind) {
    case EntryKind::Function:
        return ui.Text(UiText::VariablesBadgeFunction);
    case EntryKind::Custom:
        return ui.Text(UiText::VariablesBadgeCustom);
    case EntryKind::Parameter:
        return ui.Text(UiText::VariablesBadgeParameter);
    case EntryKind::Simple:
    default:
        return ui.Text(UiText::VariablesBadgeSimple);
    }
}

void DrawBadge(const char* label, const ImVec4& color, const VisualStyle& visual) {
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(textSize.x + ScaleUi(10.0f), std::max(ImGui::GetFrameHeight() * 0.72f, textSize.y + ScaleUi(4.0f)));
    const ImRect rect(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(WithAlpha(color, 0.16f)), ScaleUi(4.0f));
    drawList->AddRect(rect.Min, rect.Max, ImGui::GetColorU32(WithAlpha(color, 0.36f)), ScaleUi(4.0f), 0, ScaleUi(1.0f));
    drawList->AddText(
        ImVec2(rect.Min.x + ScaleUi(5.0f), rect.Min.y + std::floor((rect.GetHeight() - textSize.y) * 0.5f)),
        ImGui::GetColorU32(BlendColor(color, visual.headerText, 0.18f)),
        label);
    ImGui::Dummy(size);
}

std::string EntryDescription(const Entry& entry, UiSettings& ui) {
    if (!entry.description.empty()) {
        return entry.description;
    }
    if (entry.descriptionText != UiText::Count) {
        return ui.Text(entry.descriptionText);
    }
    return {};
}

std::string CollapseToSingleLine(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    bool pendingSpace = false;
    for (const unsigned char ch : text) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            pendingSpace = !result.empty();
            continue;
        }
        if (pendingSpace) {
            result.push_back(' ');
            pendingSpace = false;
        }
        result.push_back(static_cast<char>(ch));
    }
    return result;
}

std::string EllipsizeSingleLine(std::string text, ImFont* font, float fontSize, float maxWidth) {
    if (text.empty() || maxWidth <= 0.0f || !font) {
        return maxWidth > 0.0f ? text : std::string();
    }

    const auto textWidth = [font, fontSize](std::string_view value) {
        return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, value.data(), value.data() + value.size()).x;
    };
    if (textWidth(text) <= maxWidth) {
        return text;
    }

    constexpr std::string_view kEllipsis = "...";
    if (textWidth(kEllipsis) >= maxWidth) {
        return std::string(kEllipsis);
    }

    std::vector<std::size_t> boundaries;
    boundaries.reserve(text.size() + 1);
    boundaries.push_back(0);
    for (std::size_t pos = 0; pos < text.size();) {
        ++pos;
        while (pos < text.size() && (static_cast<unsigned char>(text[pos]) & 0xC0u) == 0x80u) {
            ++pos;
        }
        boundaries.push_back(pos);
    }

    const float prefixWidthLimit = maxWidth - textWidth(kEllipsis);
    std::size_t low = 0;
    std::size_t high = boundaries.size() - 1;
    while (low < high) {
        const std::size_t middle = low + (high - low + 1) / 2;
        const std::string_view prefix(text.data(), boundaries[middle]);
        if (textWidth(prefix) <= prefixWidthLimit) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    text.resize(boundaries[low]);
    text += kEllipsis;
    return text;
}

std::string PrimaryText(const Entry& entry) {
    if (entry.kind == EntryKind::Function && !entry.example.empty()) {
        return entry.example;
    }
    return entry.token;
}

std::string TemplateText(const Entry& entry) {
    if (entry.kind == EntryKind::Function && !entry.token.empty()) {
        return entry.token;
    }
    return {};
}

bool EntryMatchesCategory(const Entry& entry, Category category) {
    if (category == Category::All) {
        return true;
    }
    if (category == Category::Actions) {
        return entry.action;
    }
    return entry.category == category;
}

std::string BuildSearchBlob(const Entry& entry, UiSettings& ui) {
    std::string haystack;
    haystack.reserve(entry.name.size() + entry.token.size() + entry.example.size() + 96);
    haystack += entry.name;
    haystack.push_back(' ');
    haystack += entry.token;
    haystack.push_back(' ');
    haystack += entry.example;
    haystack.push_back(' ');
    haystack += CategoryLabel(entry.category, ui);
    haystack.push_back(' ');
    haystack += EntryDescription(entry, ui);
    return ToLowerAscii(haystack);
}

void EnsureSearchBlobCache(const std::vector<Entry>& entries, State& state, UiSettings& ui, std::size_t entriesHash) {
    if (state.searchBlobEntriesHash == entriesHash
        && state.searchBlobLanguageCache == ui.Language()
        && state.searchBlobCache.size() == entries.size()) {
        return;
    }

    state.searchBlobCache.clear();
    state.searchBlobCache.reserve(entries.size());
    for (const Entry& entry : entries) {
        state.searchBlobCache.push_back(BuildSearchBlob(entry, ui));
    }
    state.searchBlobEntriesHash = entriesHash;
    state.searchBlobLanguageCache = ui.Language();
}

bool EntryMatchesSearch(
    const Entry& entry,
    std::string_view loweredQuery,
    UiSettings& ui,
    const std::string* cachedSearchBlob) {
    if (loweredQuery.empty()) {
        return true;
    }

    if (cachedSearchBlob) {
        return cachedSearchBlob->find(loweredQuery) != std::string::npos;
    }
    return BuildSearchBlob(entry, ui).find(loweredQuery) != std::string::npos;
}

const std::vector<int>& VisibleIndices(const std::vector<Entry>& entries, State& state, UiSettings& ui, std::size_t entriesHash) {
    const std::string query = ToLowerAscii(TrimAscii(state.search));
    EnsureSearchBlobCache(entries, state, ui, entriesHash);
    if (state.visibleEntriesHash == entriesHash
        && state.visibleSearchCache == query
        && state.visibleCategoryCache == state.activeCategory
        && state.visibleLanguageCache == ui.Language()) {
        return state.visibleCache;
    }

    state.visibleEntriesHash = entriesHash;
    state.visibleSearchCache = query;
    state.visibleCategoryCache = state.activeCategory;
    state.visibleLanguageCache = ui.Language();
    state.visibleCache.clear();
    state.visibleCache.reserve(entries.size());
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const Entry& entry = entries[static_cast<std::size_t>(i)];
        const std::string* cachedSearchBlob = static_cast<std::size_t>(i) < state.searchBlobCache.size()
            ? &state.searchBlobCache[static_cast<std::size_t>(i)]
            : nullptr;
        if (EntryMatchesCategory(entry, state.activeCategory) && EntryMatchesSearch(entry, query, ui, cachedSearchBlob)) {
            state.visibleCache.push_back(i);
        }
    }
    return state.visibleCache;
}

int CountCategory(const std::vector<Entry>& entries, State& state, Category category, std::size_t entriesHash) {
    if (state.categoryCountsEntriesHash != entriesHash) {
        state.categoryCountsCache.fill(0);
        state.categoryCountsCache[CategoryIndex(Category::All)] = static_cast<int>(entries.size());
        for (const Entry& entry : entries) {
            const std::size_t index = CategoryIndex(entry.category);
            if (index < kVariablesCategoryCount && entry.category != Category::Actions) {
                ++state.categoryCountsCache[index];
            }
            if (entry.action) {
                ++state.categoryCountsCache[CategoryIndex(Category::Actions)];
            }
        }
        state.categoryCountsEntriesHash = entriesHash;
    }

    if (category == Category::All) {
        return state.categoryCountsCache[CategoryIndex(Category::All)];
    }

    const std::size_t index = CategoryIndex(category);
    return index < kVariablesCategoryCount ? state.categoryCountsCache[index] : 0;
}

std::string CategoryChipLabel(Category category, int count, UiSettings& ui) {
    return std::string(CategoryLabel(category, ui)) + " " + std::to_string(count);
}

struct CategoryChip {
    Category category = Category::All;
    int count = 0;
    std::string label{};
    float width = 0.0f;
};

struct CommandBarLayout {
    std::vector<CategoryChip> chips{};
    int chipRows = 1;
    float panelHeight = 0.0f;
};

CommandBarLayout BuildCommandBarLayout(
    const std::vector<Entry>& entries,
    State& state,
    const Options& options,
    std::size_t entriesHash,
    float panelWidth) {
    UiSettings& ui = UiSettings::Instance();
    static constexpr Category categories[] = {
        Category::All,
        Category::Player,
        Category::Target,
        Category::Vehicle,
        Category::World,
        Category::Time,
        Category::SampDialog,
        Category::Arizona,
        Category::Binder,
        Category::Text,
        Category::Actions,
        Category::Custom,
        Category::Parameters,
    };

    CommandBarLayout layout;
    layout.chips.reserve(std::size(categories));
    const ImGuiStyle& style = ImGui::GetStyle();
    const float chipSpacing = ScaleUi(5.0f);
    for (const Category category : categories) {
        const int count = CountCategory(entries, state, category, entriesHash);
        if ((category == Category::Parameters && count == 0)
            || (category == Category::Custom && count == 0 && !options.allowCustomEdit)) {
            continue;
        }
        CategoryChip chip;
        chip.category = category;
        chip.count = count;
        chip.label = CategoryChipLabel(category, count, ui);
        chip.width = ImGui::CalcTextSize(chip.label.c_str()).x + style.FramePadding.x * 2.0f;
        layout.chips.push_back(std::move(chip));
    }

    const float contentWidth = std::max(ScaleUi(120.0f), panelWidth - ScaleUi(16.0f));
    float rowWidth = 0.0f;
    layout.chipRows = layout.chips.empty() ? 0 : 1;
    for (const CategoryChip& chip : layout.chips) {
        const float needed = rowWidth > 0.0f ? chipSpacing + chip.width : chip.width;
        if (rowWidth > 0.0f && rowWidth + needed > contentWidth) {
            ++layout.chipRows;
            rowWidth = chip.width;
        } else {
            rowWidth += needed;
        }
    }

    const float frameHeight = ImGui::GetFrameHeight();
    const float rowsHeight = layout.chipRows > 0
        ? frameHeight * static_cast<float>(layout.chipRows)
            + style.ItemSpacing.y * static_cast<float>(layout.chipRows - 1)
        : 0.0f;
    layout.panelHeight = ScaleUi(18.0f)
        + frameHeight
        + style.ItemSpacing.y
        + ImGui::GetTextLineHeight()
        + style.ItemSpacing.y
        + rowsHeight;
    return layout;
}

Request MakeCopyRequest(std::string text) {
    Request request;
    request.type = RequestType::Copy;
    request.text = std::move(text);
    return request;
}

Request MakeInsertRequest(std::string text, bool closePopupAfterAction) {
    Request request;
    request.type = RequestType::Insert;
    request.closePopupAfterAction = closePopupAfterAction;
    request.text = std::move(text);
    return request;
}

Request MakePrimaryRequest(const Entry& entry, const Options& options) {
    if (options.mode == Mode::Insert) {
        return MakeInsertRequest(PrimaryText(entry), options.closeOnInsert);
    }
    return MakeCopyRequest(PrimaryText(entry));
}

Request MakeTemplateRequest(const Entry& entry, const Options& options) {
    if (options.mode == Mode::Insert) {
        return MakeInsertRequest(TemplateText(entry), options.closeOnInsert);
    }
    return MakeCopyRequest(TemplateText(entry));
}

Request MakePrimaryCopyRequest(const Entry& entry) {
    return MakeCopyRequest(PrimaryText(entry));
}

Request MakeTemplateCopyRequest(const Entry& entry) {
    return MakeCopyRequest(TemplateText(entry));
}

bool DrawCategoryChip(Category category, int count, State& state, const VisualStyle& visual, UiSettings& ui) {
    const std::string label = CategoryChipLabel(category, count, ui);
    const bool active = state.activeCategory == category;
    const ImVec4 bg = active ? visual.rowSelected : visual.buttonBg;
    const ImVec4 hover = active ? visual.rowSelectedHover : visual.buttonHover;
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, visual.buttonActive);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ScaleUi(5.0f));
    const bool clicked = ImGui::Button(label.c_str());
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    if (clicked) {
        state.activeCategory = category;
    }
    return clicked;
}

void DrawCommandBar(
    State& state,
    const std::vector<Entry>& entries,
    const Options& options,
    const std::vector<int>& visible,
    const CommandBarLayout& layout,
    Request& request) {
    UiSettings& ui = UiSettings::Instance();
    const VisualStyle visual = StyleTokens();
    if (!BeginPanel(
            "##variables_command_bar",
            ImVec2(0.0f, layout.panelHeight),
            visual,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        EndPanel();
        return;
    }

    const float clearSide = ImGui::GetFrameHeight();
    const float newCustomWidth = options.allowCustomEdit ? ScaleUi(154.0f) : 0.0f;
    const float searchWidth = std::max(ScaleUi(260.0f), ImGui::GetContentRegionAvail().x - clearSide - newCustomWidth - ScaleUi(options.allowCustomEdit ? 18.0f : 6.0f));
    ImGui::SetNextItemWidth(searchWidth);
    const std::string searchHint = std::string(ui_icons::Search) + " " + ui.Text(UiText::VariablesSearchHint);
    InputTextWithHintString("##variables_search", searchHint.c_str(), state.search, ImGuiInputTextFlags_AutoSelectAll, 128);
    ImGui::SameLine(0.0f, ScaleUi(4.0f));
    if (FlatIconButton(ui_icons::Xmark, "##variables_clear_search", ui.Text(UiText::Clear), ImVec2(clearSide, clearSide), visual, ImVec4(0.0f, 0.0f, 0.0f, -1.0f), !state.search.empty())) {
        state.search.clear();
    }

    if (options.allowCustomEdit) {
        ImGui::SameLine(0.0f, ScaleUi(10.0f));
        if (TextButton(ui_icons::Plus, ui.Text(UiText::VariablesNewCustom), "##variables_new_custom", ImVec2(newCustomWidth, 0.0f), visual)) {
            state.customDraftOpen = true;
            state.customCreateMode = true;
            state.customOriginalName.clear();
            state.customName.clear();
            state.customValue.clear();
            state.customError.clear();
        }
    }

    ImGui::Spacing();
    const std::string visibleText = ui.Format(
        UiText::VariablesVisibleCountFormat,
        std::to_string(visible.size()).c_str(),
        std::to_string(entries.size()).c_str());
    ImGui::TextDisabled("%s", visibleText.c_str());
    ImGui::Spacing();

    const float startX = ImGui::GetCursorPosX();
    const float wrapWidth = ImGui::GetContentRegionAvail().x;
    float rowWidth = 0.0f;
    for (const CategoryChip& chip : layout.chips) {
        const float spacing = rowWidth > 0.0f ? ScaleUi(5.0f) : 0.0f;
        if (rowWidth > 0.0f && rowWidth + spacing + chip.width > wrapWidth) {
            ImGui::SetCursorPosX(startX);
            rowWidth = 0.0f;
        } else if (rowWidth > 0.0f) {
            ImGui::SameLine(0.0f, spacing);
        }
        DrawCategoryChip(chip.category, chip.count, state, visual, ui);
        rowWidth += (rowWidth > 0.0f ? spacing : 0.0f) + chip.width;
    }

    (void)request;
    EndPanel();
}

void EnsureSelection(State& state, const std::vector<Entry>& entries, const std::vector<int>& visible) {
    if (!state.selectedId.empty()) {
        const auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& entry) {
            return entry.id == state.selectedId;
        });
        if (it != entries.end()) {
            return;
        }
    }

    if (!visible.empty()) {
        state.selectedId = entries[static_cast<std::size_t>(visible.front())].id;
    } else {
        state.selectedId.clear();
    }
}

const Entry* FindSelectedEntry(const State& state, const std::vector<Entry>& entries) {
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& entry) {
        return entry.id == state.selectedId;
    });
    return it == entries.end() ? nullptr : &(*it);
}

void DrawEntryRow(
    State& state,
    const std::vector<Entry>& entries,
    int entryIndex,
    int rowIndex,
    const Options& options,
    Request& request) {
    UiSettings& ui = UiSettings::Instance();
    const VisualStyle visual = StyleTokens();
    const Entry& entry = entries[static_cast<std::size_t>(entryIndex)];
    const float rowHeight = ScaleUi(50.0f);
    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    const ImVec2 rowSize(std::max(1.0f, ImGui::GetContentRegionAvail().x), rowHeight);
    const ImRect rowRect(rowMin, ImVec2(rowMin.x + rowSize.x, rowMin.y + rowSize.y));

    ImGui::PushID(entry.id.c_str());
    ImGui::InvisibleButton("##row", rowSize);
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool selected = state.selectedId == entry.id;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        state.selectedId = entry.id;
    }
    if (selected && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (options.mode != Mode::Insert || options.allowInsert) {
            request = MakePrimaryRequest(entry, options);
        }
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    DrawRowBackground(drawList, rowRect, rowIndex, selected, hovered, visual);

    const float padX = ScaleUi(12.0f);
    const char* badgeLabel = KindBadgeLabel(entry, ui);
    const ImVec2 badgeTextSize = ImGui::CalcTextSize(badgeLabel);
    const ImVec2 badgeSize(badgeTextSize.x + ScaleUi(10.0f), std::max(ScaleUi(20.0f), badgeTextSize.y + ScaleUi(4.0f)));
    const ImRect badgeRect(
        ImVec2(rowRect.Max.x - badgeSize.x - ScaleUi(10.0f), rowRect.Min.y + std::floor((rowHeight - badgeSize.y) * 0.5f)),
        ImVec2(rowRect.Max.x - ScaleUi(10.0f), rowRect.Min.y + std::floor((rowHeight - badgeSize.y) * 0.5f) + badgeSize.y));
    const float textMaxX = badgeRect.Min.x - ScaleUi(8.0f);
    const ImVec2 tokenPos(rowRect.Min.x + padX, rowRect.Min.y + ScaleUi(7.0f));
    ImFont* const font = ImGui::GetFont();
    const float descFontSize = ImGui::GetFontSize() * 0.92f;
    const float textWidth = std::max(0.0f, textMaxX - tokenPos.x);
    const std::string desc = EllipsizeSingleLine(
        CollapseToSingleLine(EntryDescription(entry, ui)),
        font,
        descFontSize,
        textWidth);
    const ImVec4 clip(rowRect.Min.x, rowRect.Min.y, textMaxX, rowRect.Max.y);
    drawList->AddText(
        font,
        ImGui::GetFontSize(),
        tokenPos,
        ImGui::GetColorU32(visual.headerText),
        entry.token.c_str(),
        nullptr,
        0.0f,
        &clip);
    if (!desc.empty()) {
        const ImVec2 descPos(rowRect.Min.x + padX, rowRect.Min.y + ScaleUi(28.0f));
        drawList->AddText(
            font,
            descFontSize,
            descPos,
            ImGui::GetColorU32(visual.faintText),
            desc.c_str(),
            nullptr,
            0.0f,
            &clip);
    }

    const ImVec4 badgeColor = BadgeColor(entry, visual);
    drawList->AddRectFilled(badgeRect.Min, badgeRect.Max, ImGui::GetColorU32(WithAlpha(badgeColor, 0.16f)), ScaleUi(4.0f));
    drawList->AddRect(badgeRect.Min, badgeRect.Max, ImGui::GetColorU32(WithAlpha(badgeColor, 0.36f)), ScaleUi(4.0f), 0, ScaleUi(1.0f));
    drawList->AddText(
        ImVec2(badgeRect.Min.x + ScaleUi(5.0f), badgeRect.Min.y + std::floor((badgeRect.GetHeight() - badgeTextSize.y) * 0.5f)),
        ImGui::GetColorU32(BlendColor(badgeColor, visual.headerText, 0.18f)),
        badgeLabel);
    ImGui::PopID();
}

void DrawListPane(State& state, const std::vector<Entry>& entries, const std::vector<int>& visible, const Options& options, Request& request) {
    UiSettings& ui = UiSettings::Instance();
    const VisualStyle visual = StyleTokens();
    if (!BeginPanel("##variables_list_pane", ImVec2(0.0f, 0.0f), visual, ImGuiWindowFlags_NoScrollbar)) {
        EndPanel();
        return;
    }

    ImGui::Text("%s", ui.Text(UiText::MiscVariablesCatalogTitle));
    ImGui::Separator();
    if (ImGui::BeginChild("##variables_list_scroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None)) {
        if (visible.empty()) {
            ImGui::TextDisabled("%s", ui.Text(UiText::VariablesNoMatches));
        } else {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible.size()), ScaleUi(50.0f));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const int index = visible[static_cast<std::size_t>(row)];
                    DrawEntryRow(state, entries, index, row, options, request);
                }
            }
        }
    }
    ImGui::EndChild();
    EndPanel();
}

struct HelperAction {
    RequestType requestType = RequestType::None;
    const char* icon = nullptr;
    UiText label = UiText::Count;
    UiText tooltip = UiText::Count;
};

bool TryGetHelperAction(const Entry& entry, HelperAction& action) {
    if (entry.kind != EntryKind::Function) {
        return false;
    }

    RequestType requestType = RequestType::None;
    const char* icon = ui_icons::Search;
    UiText label = UiText::Count;
    UiText tooltip = UiText::Count;
    if (entry.name == "keyemulate") {
        requestType = RequestType::OpenKeyEmulatePicker;
        icon = ui_icons::Keyboard;
        label = UiText::VariablesPickKey;
        tooltip = UiText::MiscVariablesKeyPickerOpenHint;
    } else if (entry.name == "dialogitem") {
        requestType = RequestType::OpenDialogItemPicker;
        label = UiText::VariablesPickDialogItem;
        tooltip = UiText::MiscVariablesDialogItemPickerOpenHint;
    } else if (entry.name == "arzdialogitem") {
        requestType = RequestType::OpenArizonaDialogItemPicker;
        label = UiText::VariablesPickDialogItem;
        tooltip = UiText::MiscVariablesArzDialogItemPickerOpenHint;
    } else if (entry.name == "dialogtext") {
        requestType = RequestType::OpenDialogTextPicker;
        label = UiText::VariablesPickDialogIndex;
        tooltip = UiText::MiscVariablesDialogTextPickerOpenHint;
    } else if (entry.name == "arzdialoggetdialogtext") {
        requestType = RequestType::OpenArizonaDialogTextPicker;
        label = UiText::VariablesPickDialogIndex;
        tooltip = UiText::MiscVariablesArzDialogTextPickerOpenHint;
    } else if (entry.name == "binddisable"
        || entry.name == "bindenable"
        || entry.name == "bindstart"
        || entry.name == "bindstop"
        || entry.name == "bindpause"
        || entry.name == "bindunpause"
        || entry.name == "bindfastmenu"
        || entry.name == "bindunfastmenu"
        || entry.name == "bindrandom"
        || entry.name == "bindended"
        || entry.name == "bindpopup") {
        requestType = RequestType::OpenBindSelectorBuilder;
        label = UiText::VariablesBuildBindTag;
        tooltip = UiText::MiscVariablesBindBuilderOpenHint;
    }

    if (requestType == RequestType::None) {
        return false;
    }

    action.requestType = requestType;
    action.icon = icon;
    action.label = label;
    action.tooltip = tooltip;
    return true;
}

void DrawHelperActionButton(const Entry& entry, const VisualStyle& visual, Request& request) {
    HelperAction action;
    if (!TryGetHelperAction(entry, action)) {
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    ImGui::Spacing();
    const float width = std::max(1.0f, std::min(ScaleUi(230.0f), ImGui::GetContentRegionAvail().x));
    if (TextButton(
            action.icon,
            ui.Text(action.label),
            "##variables_open_helper",
            ImVec2(width, 0.0f),
            visual,
            true,
            ui.Text(action.tooltip))) {
        request.type = action.requestType;
        request.name = entry.name;
    }
}

void BeginCustomDraft(State& state, const Entry& entry) {
    state.customDraftOpen = true;
    state.customCreateMode = false;
    state.customOriginalName = entry.name;
    state.customName = entry.name;
    state.customValue = entry.value;
    state.customError.clear();
}

void DrawCustomDraft(State& state, Request& request) {
    UiSettings& ui = UiSettings::Instance();
    const VisualStyle visual = StyleTokens();
    ImGui::Separator();
    ImGui::Text("%s", ui.Text(state.customCreateMode ? UiText::VariablesCustomCreateTitle : UiText::VariablesCustomEditTitle));
    ImGui::TextDisabled("%s", ui.Text(UiText::VariablesCustomNameHint));
    ImGui::SetNextItemWidth(-FLT_MIN);
    InputTextString("##variables_custom_name", state.customName, ImGuiInputTextFlags_AutoSelectAll, 96);
    ImGui::Spacing();
    ImGui::TextDisabled("%s", ui.Text(UiText::VariablesCustomValue));
    InputTextMultilineString(
        "##variables_custom_value",
        state.customValue,
        ImVec2(-FLT_MIN, ScaleUi(96.0f)),
        0,
        512);
    if (!state.customError.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(visual.danger, "%s", state.customError.c_str());
    }

    ImGui::Spacing();
    if (TextButton(ui_icons::Check, ui.Text(UiText::Save), "##variables_save_custom", ScaleUi(126.0f, 0.0f), visual)) {
        request.type = RequestType::SaveCustom;
        request.name = state.customName;
        request.value = state.customValue;
        request.text = state.customOriginalName;
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::Cancel), ScaleUi(110.0f, 0.0f))) {
        state.customDraftOpen = false;
        state.customError.clear();
    }
}

void DrawDeleteConfirmPopup(State& state, Request& request) {
    UiSettings& ui = UiSettings::Instance();
    const std::string popupId = std::string(ui.Text(UiText::Delete)) + "##variables_delete_custom_confirm";
    if (state.customDeleteConfirmOpen) {
        ImGui::OpenPopup(popupId.c_str());
        state.customDeleteConfirmOpen = false;
    }

    if (!ImGui::BeginPopupModal(popupId.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextWrapped("%s", ui.Format(UiText::VariablesDeleteCustomQuestion, state.customOriginalName.c_str()).c_str());
    ImGui::Spacing();
    if (ImGui::Button(ui.Text(UiText::Delete), ScaleUi(120.0f, 0.0f))) {
        request.type = RequestType::DeleteCustom;
        request.name = state.customOriginalName;
        state.customDraftOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::Cancel), ScaleUi(110.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void DrawInspectorPane(State& state, const Entry* selected, const Options& options, Request& request) {
    UiSettings& ui = UiSettings::Instance();
    const VisualStyle visual = StyleTokens();
    if (!BeginPanel("##variables_inspector_pane", ImVec2(0.0f, 0.0f), visual)) {
        EndPanel();
        return;
    }

    ImGui::Text("%s", ui.Text(UiText::MiscVariablesInspectorTitle));
    ImGui::Separator();
    if (!selected) {
        ImGui::TextDisabled("%s", ui.Text(UiText::VariablesInspectorEmpty));
        EndPanel();
        return;
    }

    ImGui::TextColored(visual.headerText, "%s", selected->token.c_str());
    ImGui::Spacing();
    DrawBadge(CategoryLabel(selected->category, ui), visual.mutedText, visual);
    ImGui::SameLine();
    DrawBadge(KindBadgeLabel(*selected, ui), BadgeColor(*selected, visual), visual);

    const std::string desc = EntryDescription(*selected, ui);
    if (!desc.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesDescriptionLabel));
        ImGui::TextWrapped("%s", desc.c_str());
    }

    if (selected->kind == EntryKind::Custom) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", ui.Text(UiText::VariablesCustomValue));
        ImGui::TextWrapped("%s", selected->value.c_str());
    } else if (!selected->example.empty() && selected->example != selected->token) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesExampleLabel));
        ImGui::TextWrapped("%s", selected->example.c_str());
    }

    ImGui::Spacing();
    const bool insertMode = options.mode == Mode::Insert;
    const bool primaryEnabled = !insertMode || options.allowInsert;
    const bool copyInInsertMode = insertMode && options.allowCopyInInsert;
    const auto drawInsertCopyPair = [&](const char* insertLabel, const char* copyLabel, const char* idSuffix, bool useTemplateText) {
        const std::string insertId = std::string("##variables_insert_") + idSuffix;
        const std::string copyId = std::string("##variables_copy_") + idSuffix;
        if (TextButton(
                ui_icons::Plus,
                insertLabel,
                insertId.c_str(),
                ScaleUi(170.0f, 0.0f),
                visual,
                primaryEnabled,
                primaryEnabled ? nullptr : ui.Text(UiText::VariablesNoInsertTarget))) {
            request = useTemplateText ? MakeTemplateRequest(*selected, options) : MakePrimaryRequest(*selected, options);
        }
        ImGui::SameLine();
        if (TextButton(
                ui_icons::Copy,
                copyLabel,
                copyId.c_str(),
                ScaleUi(170.0f, 0.0f),
                visual)) {
            request = useTemplateText ? MakeTemplateCopyRequest(*selected) : MakePrimaryCopyRequest(*selected);
        }
    };

    if (insertMode) {
        const char* insertLabel = selected->kind == EntryKind::Function ? ui.Text(UiText::VariablesInsertExample) : ui.Text(UiText::VariablesInsert);
        if (copyInInsertMode) {
            drawInsertCopyPair(
                insertLabel,
                selected->kind == EntryKind::Function ? ui.Text(UiText::VariablesCopyExample) : ui.Text(UiText::VariablesCopy),
                "primary_action",
                false);
        } else if (TextButton(
                       ui_icons::Plus,
                       insertLabel,
                       "##variables_primary_action",
                       ScaleUi(170.0f, 0.0f),
                       visual,
                       primaryEnabled,
                       primaryEnabled ? nullptr : ui.Text(UiText::VariablesNoInsertTarget))) {
            request = MakePrimaryRequest(*selected, options);
        }
    } else {
        const char* primaryLabel = selected->kind == EntryKind::Function ? ui.Text(UiText::VariablesCopyExample) : ui.Text(UiText::UnwantedCopy);
        if (TextButton(
                ui_icons::Copy,
                primaryLabel,
                "##variables_primary_action",
                ScaleUi(170.0f, 0.0f),
                visual)) {
            request = MakePrimaryRequest(*selected, options);
        }
    }

    const std::string templ = TemplateText(*selected);
    if (!templ.empty() && templ != PrimaryText(*selected)) {
        if (insertMode) {
            if (copyInInsertMode) {
                ImGui::Spacing();
                drawInsertCopyPair(
                    ui.Text(UiText::VariablesInsertTemplate),
                    ui.Text(UiText::VariablesCopyTemplate),
                    "template_action",
                    true);
            } else {
                ImGui::SameLine();
                if (TextButton(
                        ui_icons::Plus,
                        ui.Text(UiText::VariablesInsertTemplate),
                        "##variables_template_action",
                        ScaleUi(170.0f, 0.0f),
                        visual,
                        primaryEnabled,
                        primaryEnabled ? nullptr : ui.Text(UiText::VariablesNoInsertTarget))) {
                    request = MakeTemplateRequest(*selected, options);
                }
            }
        } else {
            ImGui::SameLine();
            if (TextButton(
                    ui_icons::Copy,
                    ui.Text(UiText::VariablesCopyTemplate),
                    "##variables_template_action",
                    ScaleUi(170.0f, 0.0f),
                    visual)) {
                request = MakeTemplateRequest(*selected, options);
            }
        }
    } else if (selected->kind == EntryKind::Function && selected->example == selected->token) {
        if (insertMode) {
            if (copyInInsertMode) {
                ImGui::Spacing();
                drawInsertCopyPair(
                    ui.Text(UiText::VariablesInsertTemplate),
                    ui.Text(UiText::VariablesCopyTemplate),
                    "same_template_action",
                    false);
            } else {
                ImGui::SameLine();
                if (TextButton(
                        ui_icons::Plus,
                        ui.Text(UiText::VariablesInsertTemplate),
                        "##variables_same_template_action",
                        ScaleUi(170.0f, 0.0f),
                        visual,
                        primaryEnabled,
                        primaryEnabled ? nullptr : ui.Text(UiText::VariablesNoInsertTarget))) {
                    request = MakePrimaryRequest(*selected, options);
                }
            }
        } else {
            ImGui::SameLine();
            if (TextButton(
                    ui_icons::Copy,
                    ui.Text(UiText::VariablesCopyTemplate),
                    "##variables_same_template_action",
                    ScaleUi(170.0f, 0.0f),
                    visual)) {
                request = MakePrimaryRequest(*selected, options);
            }
        }
    }

    DrawHelperActionButton(*selected, visual, request);

    if (selected->kind == EntryKind::Function
        && (selected->name.rfind("bind", 0) == 0 || selected->name.rfind("thisbind", 0) == 0)) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", ui.Text(UiText::MiscVariablesBindSelectorNote));
    }

    if (selected->kind == EntryKind::Function && selected->name == "paramcmd") {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesParamcmdNote));
    }
    if (selected->kind == EntryKind::Function && selected->name == "keyemulate") {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesKeyEmulateNote));
    }

    if (options.drawInspectorExtra) {
        options.drawInspectorExtra(options.inspectorExtraContext, *selected);
    }

    if (selected->kind == EntryKind::Custom && options.allowCustomEdit) {
        ImGui::Spacing();
        if (TextButton(ui_icons::Edit, ui.Text(UiText::Edit), "##variables_edit_custom", ScaleUi(126.0f, 0.0f), visual)) {
            BeginCustomDraft(state, *selected);
        }
        ImGui::SameLine();
        if (TextButton(ui_icons::Delete, ui.Text(UiText::Delete), "##variables_delete_custom", ScaleUi(126.0f, 0.0f), visual)) {
            state.customOriginalName = selected->name;
            state.customDeleteConfirmOpen = true;
        }
    } else if (selected->kind == EntryKind::Custom) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", ui.Text(UiText::VariablesCustomReadOnlyHint));
    }

    if (state.customDraftOpen && options.allowCustomEdit) {
        DrawCustomDraft(state, request);
    }
    DrawDeleteConfirmPopup(state, request);
    EndPanel();
}

} // namespace

std::string MakeEntryId(EntryKind kind, std::string_view token) {
    char prefix = 's';
    switch (kind) {
    case EntryKind::Function:
        prefix = 'f';
        break;
    case EntryKind::Custom:
        prefix = 'c';
        break;
    case EntryKind::Parameter:
        prefix = 'p';
        break;
    case EntryKind::Simple:
    default:
        prefix = 's';
        break;
    }
    return std::string(1, prefix) + ":" + std::string(token);
}

const char* CategoryLabel(Category category, UiSettings& ui) {
    switch (category) {
    case Category::All:
        return ui.Text(UiText::VariablesCategoryAll);
    case Category::Player:
        return ui.Text(UiText::VariablesCategoryPlayer);
    case Category::Target:
        return ui.Text(UiText::VariablesCategoryTarget);
    case Category::Vehicle:
        return ui.Text(UiText::VariablesCategoryVehicle);
    case Category::World:
        return ui.Text(UiText::VariablesCategoryWorld);
    case Category::Time:
        return ui.Text(UiText::VariablesCategoryTime);
    case Category::SampDialog:
        return ui.Text(UiText::VariablesCategorySampDialog);
    case Category::Arizona:
        return ui.Text(UiText::VariablesCategoryArizona);
    case Category::Binder:
        return ui.Text(UiText::VariablesCategoryBinder);
    case Category::Text:
        return ui.Text(UiText::VariablesCategoryText);
    case Category::Actions:
        return ui.Text(UiText::VariablesCategoryActions);
    case Category::Custom:
        return ui.Text(UiText::VariablesCategoryCustom);
    case Category::Parameters:
        return ui.Text(UiText::VariablesCategoryParameters);
    default:
        return "";
    }
}

Request Draw(State& state, const std::vector<Entry>& entries, const Options& options) {
    UiSettings& ui = UiSettings::Instance();
    Request request;
    const std::size_t entriesHash = EntriesHash(entries);
    const std::vector<int>& visible = VisibleIndices(entries, state, ui, entriesHash);
    EnsureSelection(state, entries, visible);

    ImGui::PushID(options.id);
    const ImVec2 size = options.size.x > 0.0f || options.size.y > 0.0f ? options.size : ImGui::GetContentRegionAvail();
    const CommandBarLayout commandLayout = BuildCommandBarLayout(entries, state, options, entriesHash, size.x);
    const float gap = ScaleUi(8.0f);
    DrawCommandBar(state, entries, options, visible, commandLayout, request);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + gap);

    const float bodyHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
    if (ImGui::BeginTable(
            "##variables_body",
            2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings,
            ImVec2(0.0f, bodyHeight))) {
        ImGui::TableSetupColumn("list", ImGuiTableColumnFlags_WidthStretch, 0.46f);
        ImGui::TableSetupColumn("inspector", ImGuiTableColumnFlags_WidthStretch, 0.54f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawListPane(state, entries, visible, options, request);
        ImGui::TableSetColumnIndex(1);
        DrawInspectorPane(state, FindSelectedEntry(state, entries), options, request);
        ImGui::EndTable();
    }

    ImGui::PopID();
    return request;
}

} // namespace variables_picker
