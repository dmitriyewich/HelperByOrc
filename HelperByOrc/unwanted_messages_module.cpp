#include "unwanted_messages_module.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "app_config.h"
#include "debug_log.h"
#include "text_pattern_input.h"
#include "text_pattern_ui_support.h"
#include "ui_icons.h"
#include "ui_settings.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <utility>

namespace {

constexpr std::string_view kUnwantedSectionName = "unwanted";
constexpr int kUnwantedSchemaVersion = 3;
constexpr int kMaxConfigPatternLength = 65535;
constexpr std::uint64_t kPerfTelemetryWindowMs = 5000;

enum class RegexWorkspaceMode {
    Automatic = 0,
    Manual,
};

using text_pattern_ui::AppendWarning;
using text_pattern_ui::ContainsBroadWildcard;
using text_pattern_ui::FormatCompilePosition;
using text_pattern_ui::Utf8CharacterOffset;

double UnwantedPerfNowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::uint64_t UnwantedPerfTickMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct ImGuiStringUserData {
    std::string* value = nullptr;
    ImGuiInputTextCallback chain = nullptr;
    void* chainUserData = nullptr;
};

int ImGuiStringResizeCallback(ImGuiInputTextCallbackData* data) {
    auto* userData = static_cast<ImGuiStringUserData*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        IM_ASSERT(userData && userData->value);
        userData->value->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = userData->value->data();
        return 0;
    }
    if (userData && userData->chain) {
        data->UserData = userData->chainUserData;
        return userData->chain(data);
    }
    return 0;
}

bool InputTextString(
    const char* label,
    std::string& value,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 256) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value, nullptr, nullptr };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputText(label, value.data(), value.capacity() + 1, flags, ImGuiStringResizeCallback, &userData);
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

    ImGuiStringUserData userData{ &value, nullptr, nullptr };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputTextWithHint(label, hint, value.data(), value.capacity() + 1, flags, ImGuiStringResizeCallback, &userData);
}

bool InputTextMultilineString(
    const char* label,
    std::string& value,
    const ImVec2& size,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 1024) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value, nullptr, nullptr };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputTextMultiline(label, value.data(), value.capacity() + 1, size, flags, ImGuiStringResizeCallback, &userData);
}

float ScaleUi(float value) {
    return UiSettings::Instance().Scale(value);
}

ImVec2 ScaleUi(float x, float y) {
    return UiSettings::Instance().Scale(ImVec2(x, y));
}

RegexWorkspaceMode CurrentRegexWorkspaceMode(int value) {
    return static_cast<RegexWorkspaceMode>(std::clamp(
        value,
        static_cast<int>(RegexWorkspaceMode::Automatic),
        static_cast<int>(RegexWorkspaceMode::Manual)));
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

struct UnwantedVisualStyle {
    ImVec4 panelBg{};
    ImVec4 panelBorder{};
    ImVec4 rowHover{};
    ImVec4 rowSelected{};
    ImVec4 rowAlt{};
    ImVec4 accent{};
    ImVec4 mutedText{};
    ImVec4 faintText{};
    ImVec4 ok{};
    ImVec4 warn{};
    ImVec4 danger{};
};

UnwantedVisualStyle UnwantedStyleTokens() {
    const ImVec4* colors = ImGui::GetStyle().Colors;
    const ImVec4& windowBg = colors[ImGuiCol_WindowBg];
    const ImVec4& childBg = colors[ImGuiCol_ChildBg];
    const ImVec4& text = colors[ImGuiCol_Text];
    const ImVec4& textDisabled = colors[ImGuiCol_TextDisabled];
    const ImVec4& buttonActive = colors[ImGuiCol_ButtonActive];
    const ImVec4& buttonHovered = colors[ImGuiCol_ButtonHovered];
    const ImVec4& headerHovered = colors[ImGuiCol_HeaderHovered];
    const ImVec4& headerActive = colors[ImGuiCol_HeaderActive];
    const ImVec4& border = colors[ImGuiCol_Border];

    UnwantedVisualStyle style;
    style.panelBg = WithAlpha(BlendColor(childBg, windowBg, 0.18f), childBg.w);
    style.panelBorder = WithAlpha(border, 0.42f);
    style.rowHover = WithAlpha(headerHovered, 0.20f);
    style.rowSelected = WithAlpha(headerActive, 0.30f);
    style.rowAlt = WithAlpha(text, 0.025f);
    style.accent = WithAlpha(buttonActive, 0.96f);
    style.mutedText = WithAlpha(BlendColor(textDisabled, text, 0.28f), 0.92f);
    style.faintText = WithAlpha(textDisabled, 0.80f);
    style.ok = WithAlpha(BlendColor(text, buttonActive, 0.38f), 0.98f);
    style.warn = ImVec4(0.96f, 0.70f, 0.34f, 1.0f);
    style.danger = WithAlpha(BlendColor(text, buttonHovered, 0.18f), 0.95f);
    return style;
}

void DrawUnwantedPanelBackground(const ImVec2& size) {
    const UnwantedVisualStyle visual = UnwantedStyleTokens();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float width = size.x <= 0.0f ? std::max(1.0f, avail.x + size.x) : size.x;
    const float height = size.y <= 0.0f ? std::max(1.0f, avail.y + size.y) : size.y;
    const ImRect rect(pos, ImVec2(pos.x + width, pos.y + height));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float rounding = ScaleUi(7.0f);
    drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(visual.panelBg), rounding);
    drawList->AddRect(rect.Min, rect.Max, ImGui::GetColorU32(visual.panelBorder), rounding, 0, ScaleUi(1.0f));
}

bool BeginUnwantedPanel(const char* id, const ImVec2& size) {
    DrawUnwantedPanelBackground(size);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(9.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    return ImGui::BeginChild(id, size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
}

void EndUnwantedPanel() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

bool UnwantedTextButton(const char* icon, const char* label, const char* id, const ImVec2& size = ImVec2(0.0f, 0.0f)) {
    std::string text;
    if (icon && icon[0] != '\0') {
        text = std::string(icon) + " ";
    }
    text += label ? label : "";
    text += id ? id : "";
    return ImGui::Button(text.c_str(), size);
}

bool UnwantedPrimaryButton(const char* icon, const char* label, const char* id, const ImVec2& size) {
    const UnwantedVisualStyle visual = UnwantedStyleTokens();
    ImGui::PushStyleColor(ImGuiCol_Button, WithAlpha(visual.accent, 0.58f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(visual.accent, 0.78f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, WithAlpha(visual.accent, 0.92f));
    const bool clicked = UnwantedTextButton(icon, label, id, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

float UnwantedButtonWidth(const char* icon, const char* label, float minimum = 0.0f) {
    std::string visibleText;
    if (icon && icon[0] != '\0') {
        visibleText = icon;
        if (label && label[0] != '\0') {
            visibleText.push_back(' ');
        }
    }
    if (label) {
        visibleText += label;
    }

    const float contentWidth = ImGui::CalcTextSize(visibleText.c_str()).x
        + ImGui::GetStyle().FramePadding.x * 2.0f;
    return std::max(minimum, contentWidth);
}

void CenterTableCellVertically(float rowHeight, float contentHeight) {
    const float offset = std::max(
        0.0f,
        (rowHeight - contentHeight) * 0.5f - ImGui::GetStyle().CellPadding.y);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offset);
}

bool UnwantedDangerButton(const char* label, const char* id, const ImVec2& size) {
    const UnwantedVisualStyle visual = UnwantedStyleTokens();
    ImGui::PushStyleColor(ImGuiCol_Button, WithAlpha(visual.danger, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(visual.danger, 0.48f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, WithAlpha(visual.danger, 0.68f));
    const bool clicked = UnwantedTextButton(nullptr, label, id, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

bool FilterChip(const char* label, bool active) {
    const UnwantedVisualStyle visual = UnwantedStyleTokens();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ScaleUi(8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaleUi(9.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, active ? visual.rowSelected : WithAlpha(visual.panelBg, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? visual.rowSelected : visual.rowHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, visual.rowSelected);
    const bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    return clicked;
}

ImVec2 BadgeSize(const char* label) {
    const ImVec2 textSize = ImGui::CalcTextSize(label ? label : "");
    return ImVec2(textSize.x + ScaleUi(12.0f), textSize.y + ScaleUi(4.0f));
}

void DrawBadgeVisual(const char* label, const ImRect& rect, const ImVec4& color, bool hovered = false) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 bg = ImGui::GetColorU32(WithAlpha(color, hovered ? 0.18f : 0.12f));
    drawList->AddRectFilled(rect.Min, rect.Max, bg, ScaleUi(7.0f));

    const ImVec2 textSize = ImGui::CalcTextSize(label ? label : "");
    const ImVec2 textPos(
        rect.Min.x + std::floor((rect.GetWidth() - textSize.x) * 0.5f),
        rect.Min.y + std::floor((rect.GetHeight() - textSize.y) * 0.5f));
    drawList->AddText(textPos, ImGui::GetColorU32(color), label ? label : "");
}

void TextBadge(const char* label, const ImVec4& color) {
    const ImVec2 size = BadgeSize(label);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    DrawBadgeVisual(label, ImRect(pos, ImVec2(pos.x + size.x, pos.y + size.y)), color);
}

void DrawUnwantedTooltip(const char* text) {
    if (text && text[0] != '\0' && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", text);
    }
}

std::string TrimAscii(std::string_view value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(start, end - start));
}

bool TryExactRegexDisplayText(std::string_view rawPattern, std::string& displayText) {
    const std::string pattern = TrimAscii(rawPattern);
    if (pattern.size() < 4 || !pattern.starts_with("\\A") || !pattern.ends_with("\\z")) {
        return false;
    }

    const std::string_view body(pattern.data() + 2, pattern.size() - 4);
    std::string decoded;
    decoded.reserve(body.size());
    for (std::size_t i = 0; i < body.size();) {
        const unsigned char ch = static_cast<unsigned char>(body[i]);
        if (ch == '\r' || ch == '\n') {
            return false;
        }
        if (ch != '\\') {
            if (std::string_view(".^$|?*+()[]{}").find(static_cast<char>(ch)) != std::string_view::npos) {
                return false;
            }
            decoded.push_back(static_cast<char>(ch));
            ++i;
            continue;
        }

        if (i + 1 >= body.size()) {
            return false;
        }
        const unsigned char escaped = static_cast<unsigned char>(body[i + 1]);
        if (escaped == 'Q') {
            const std::size_t quoteEnd = body.find("\\E", i + 2);
            if (quoteEnd == std::string_view::npos) {
                return false;
            }
            const std::string_view quoted = body.substr(i + 2, quoteEnd - i - 2);
            if (quoted.find_first_of("\r\n") != std::string_view::npos) {
                return false;
            }
            decoded.append(quoted);
            i = quoteEnd + 2;
            continue;
        }
        if (std::isalnum(escaped) != 0 || escaped == '_') {
            return false;
        }
        decoded.push_back(static_cast<char>(escaped));
        i += 2;
    }

    if (decoded.empty()) {
        return false;
    }
    displayText = std::move(decoded);
    return true;
}

const char* RuntimeWarningDisplay(std::string_view status) {
    UiSettings& ui = UiSettings::Instance();
    if (status == "match_limit") return ui.Text(UiText::UnwantedRuntimeMatchLimit);
    if (status == "depth_limit") return ui.Text(UiText::UnwantedRuntimeDepthLimit);
    if (status == "heap_limit") return ui.Text(UiText::UnwantedRuntimeHeapLimit);
    if (status == "invalid_utf8") return ui.Text(UiText::UnwantedRuntimeInvalidText);
    return ui.Text(UiText::UnwantedRuntimeGenericError);
}

bool IsHex(char ch) {
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

bool TryColorTag(std::string_view text, std::size_t offset, std::size_t& consumed) {
    consumed = 0;
    if (offset >= text.size() || text[offset] != '{') {
        return false;
    }

    const std::size_t close = text.find('}', offset + 1);
    if (close == std::string_view::npos) {
        return false;
    }

    const std::size_t length = close - offset - 1;
    if (length != 6 && length != 8) {
        return false;
    }

    for (std::size_t i = offset + 1; i < close; ++i) {
        if (!IsHex(text[i])) {
            return false;
        }
    }

    consumed = close - offset + 1;
    return true;
}

std::string StripColorTags(std::string_view value) {
    std::string out;
    out.reserve(value.size());

    for (std::size_t i = 0; i < value.size();) {
        std::size_t consumed = 0;
        if (TryColorTag(value, i, consumed)) {
            i += consumed;
            continue;
        }
        out.push_back(value[i++]);
    }

    return out;
}

std::string CollapseWhitespace(std::string_view value) {
    std::string out;
    out.reserve(value.size());

    bool inWhitespace = false;
    for (const unsigned char ch : value) {
        if (std::isspace(ch) != 0) {
            if (!inWhitespace) {
                out.push_back(' ');
                inWhitespace = true;
            }
            continue;
        }
        out.push_back(static_cast<char>(ch));
        inWhitespace = false;
    }

    return out;
}

std::string Utf8ToLower(std::string_view value) {
    if (value.empty()) {
        return {};
    }

    const int wideLength = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (wideLength <= 0) {
        std::string fallback(value);
        std::transform(fallback.begin(), fallback.end(), fallback.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return fallback;
    }

    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        wide.data(),
        wideLength);
    CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));

    const int utf8Length = WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.data(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (utf8Length <= 0) {
        return std::string(value);
    }

    std::string out(static_cast<std::size_t>(utf8Length), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.data(),
        static_cast<int>(wide.size()),
        out.data(),
        utf8Length,
        nullptr,
        nullptr);
    return out;
}

bool DecodeUtf8At(std::string_view value, std::size_t offset, std::uint32_t& codepoint, std::size_t& size) {
    codepoint = 0;
    size = 0;
    if (offset >= value.size()) {
        return false;
    }

    const auto lead = static_cast<unsigned char>(value[offset]);
    if (lead < 0x80) {
        codepoint = lead;
        size = 1;
        return true;
    }

    int expected = 0;
    std::uint32_t cp = 0;
    if ((lead & 0xE0) == 0xC0) {
        expected = 2;
        cp = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        expected = 3;
        cp = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        expected = 4;
        cp = lead & 0x07;
    } else {
        codepoint = lead;
        size = 1;
        return true;
    }

    if (offset + static_cast<std::size_t>(expected) > value.size()) {
        codepoint = lead;
        size = 1;
        return true;
    }

    for (int i = 1; i < expected; ++i) {
        const auto tail = static_cast<unsigned char>(value[offset + static_cast<std::size_t>(i)]);
        if ((tail & 0xC0) != 0x80) {
            codepoint = lead;
            size = 1;
            return true;
        }
        cp = (cp << 6) | (tail & 0x3F);
    }

    codepoint = cp;
    size = static_cast<std::size_t>(expected);
    return true;
}

bool DecodePrevUtf8(std::string_view value, std::size_t byteOffset, std::uint32_t& codepoint) {
    if (byteOffset == 0 || byteOffset > value.size()) {
        return false;
    }

    std::size_t start = byteOffset - 1;
    while (start > 0 && (static_cast<unsigned char>(value[start]) & 0xC0) == 0x80) {
        --start;
    }

    std::size_t size = 0;
    return DecodeUtf8At(value, start, codepoint, size) && start + size == byteOffset;
}

bool DecodeNextUtf8(std::string_view value, std::size_t byteOffset, std::uint32_t& codepoint) {
    if (byteOffset >= value.size()) {
        return false;
    }

    std::size_t size = 0;
    return DecodeUtf8At(value, byteOffset, codepoint, size);
}

std::string Utf8Preview(std::string_view value, std::size_t maxCharacters) {
    const std::string trimmed = TrimAscii(value);
    std::size_t offset = 0;
    std::size_t characters = 0;
    while (offset < trimmed.size() && characters < maxCharacters) {
        std::uint32_t codepoint = 0;
        std::size_t size = 0;
        DecodeUtf8At(trimmed, offset, codepoint, size);
        offset += std::max<std::size_t>(size, 1);
        ++characters;
    }
    if (offset >= trimmed.size()) {
        return trimmed;
    }
    return trimmed.substr(0, offset) + "...";
}

std::string SuggestRuleName(std::string_view rawSample) {
    std::string sample = TrimAscii(text_pattern_input::ExtractChatlogPayload(rawSample).payload);
    std::size_t colorLength = 0;
    while (TryColorTag(sample, 0, colorLength)) {
        sample = TrimAscii(std::string_view(sample).substr(colorLength));
    }
    if (sample.empty()) {
        return {};
    }

    if (sample.front() == '[') {
        const std::size_t close = sample.find(']');
        if (close != std::string::npos && close > 1 && close <= 64) {
            const std::string prefix = Utf8Preview(std::string_view(sample).substr(1, close - 1), 24);
            const std::string rest = Utf8Preview(std::string_view(sample).substr(close + 1), 32);
            return rest.empty() ? prefix : prefix + ": " + rest;
        }
    }

    for (std::size_t offset = 0; offset + 1 < sample.size(); ++offset) {
        if (sample[offset] != '/') {
            continue;
        }
        const bool commandStart = offset == 0
            || std::isspace(static_cast<unsigned char>(sample[offset - 1])) != 0
            || sample[offset - 1] == '(' || sample[offset - 1] == '[';
        if (!commandStart || std::isalnum(static_cast<unsigned char>(sample[offset + 1])) == 0) {
            continue;
        }
        std::size_t end = offset + 2;
        while (end < sample.size()) {
            const unsigned char current = static_cast<unsigned char>(sample[end]);
            if (std::isalnum(current) == 0 && sample[end] != '_' && sample[end] != '-') {
                break;
            }
            ++end;
        }
        return sample.substr(offset, end - offset);
    }

    return Utf8Preview(sample, 40);
}

bool IsWordCodepoint(std::uint32_t cp) {
    if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || cp == '_') {
        return true;
    }
    if (cp >= 0x0400 && cp <= 0x052F) {
        return true;
    }
    return false;
}

bool IsWordBoundary(std::string_view value, std::size_t matchStart, std::size_t matchEnd) {
    std::uint32_t before = 0;
    std::uint32_t after = 0;
    const bool hasBefore = DecodePrevUtf8(value, matchStart, before);
    const bool hasAfter = DecodeNextUtf8(value, matchEnd, after);
    return (!hasBefore || !IsWordCodepoint(before)) && (!hasAfter || !IsWordCodepoint(after));
}

void AddUnique(std::vector<std::string>& values, std::string value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
}

const char* RuleTypeName(UnwantedMessagesModule::RuleType type) {
    return type == UnwantedMessagesModule::RuleType::Regex ? "regex" : "literal";
}

} // namespace

void UnwantedMessagesModule::OnProcessAttach() {
    ReloadConfig();
    debuglog::WriteInfo("UnwantedMessagesModule::OnProcessAttach rules=%zu", rules_.size());
}

void UnwantedMessagesModule::SetChatAsiCompatibilityHandler(std::function<void(bool)> handler) {
    chatAsiCompatibilityHandler_ = std::move(handler);
    ApplyChatAsiCompatibilitySetting();
}

void UnwantedMessagesModule::SetSampRakHooks(SampRakHooks* hooks) {
    sampRakHooks_ = hooks;
}

void UnwantedMessagesModule::Shutdown() {
    sampRakHooks_ = nullptr;
    messageHistoryPicker_ = {};
    rules_.clear();
    selectedRuleIds_.clear();
    selectionMode_ = false;
    focusRuleSearch_ = false;
    scrollToRuleId_.clear();
    helperWorkspaceMode_ = static_cast<int>(RegexWorkspaceMode::Automatic);
    page_ = Page::Closed;
    {
        std::lock_guard lock(snapshotMutex_);
        runtimeSnapshot_.reset();
    }
    {
        std::lock_guard lock(statusMutex_);
        runtimeWarnings_.clear();
        runtimeWarningsView_.clear();
        perfStats_ = {};
        lastBlocked_ = {};
        blockedCount_ = 0;
    }
}

void UnwantedMessagesModule::ReloadConfig() {
    const jsonutil::JsonObject section = AppConfig::Instance().ReadSectionObject(kUnwantedSectionName);

    LoadFromConfig(section);
    CompileRules();
    PublishRuntimeSnapshot();
    ApplyChatAsiCompatibilitySetting();

    debuglog::WriteInfo("UnwantedMessagesModule::ReloadConfig done");
}

bool UnwantedMessagesModule::ShouldBlock(const UnwantedMessageContext& context) {
    const double totalBeginMs = UnwantedPerfNowMs();
    std::optional<PerfLogSnapshot> perfLog;
    std::vector<RegexLogEvent> regexLogs;
    bool blocked = false;
    double waitMs = 0.0;
    double matchMs = 0.0;

    const double snapshotWaitBeginMs = UnwantedPerfNowMs();
    std::shared_ptr<const RuntimeSnapshot> snapshot;
    {
        std::lock_guard lock(snapshotMutex_);
        snapshot = runtimeSnapshot_;
    }
    waitMs += UnwantedPerfNowMs() - snapshotWaitBeginMs;
    if (!snapshot || !snapshot->settings.enabled || snapshot->rules.empty()) {
        return false;
    }

    const std::vector<std::string> candidates = BuildCandidates(context, snapshot->settings);
    std::vector<std::string> foldedCandidates;
    if (snapshot->hasNoCaseLiteral) {
        foldedCandidates.reserve(candidates.size());
        for (const std::string& candidate : candidates) {
            foldedCandidates.push_back(Utf8ToLower(candidate));
        }
    }

    MatchResult result;
    std::size_t actualChecks = 0;
    std::vector<unsigned char> evaluationState(snapshot->rules.size(), 0);
    std::vector<unwanted_regex::MatchResult> runtimeErrors(snapshot->rules.size());
    const double matchWaitBeginMs = UnwantedPerfNowMs();
    {
        std::lock_guard lock(matchMutex_);
        waitMs += UnwantedPerfNowMs() - matchWaitBeginMs;
        const double matchBeginMs = UnwantedPerfNowMs();
        blocked = MatchCandidates(
            *snapshot,
            candidates,
            foldedCandidates,
            context.source,
            &result,
            actualChecks,
            evaluationState,
            runtimeErrors);
        matchMs = UnwantedPerfNowMs() - matchBeginMs;
    }

    ApplyRuntimeEvaluation(*snapshot, evaluationState, runtimeErrors, regexLogs);
    const double totalMs = UnwantedPerfNowMs() - totalBeginMs;
    {
        std::lock_guard lock(statusMutex_);
        if (blocked) {
            ++blockedCount_;
            lastBlocked_ = std::move(result);
        }
        perfLog = AccumulatePerfStats(
            totalMs,
            waitMs,
            matchMs,
            candidates.size(),
            snapshot->rules.size(),
            snapshot->regexRules,
            actualChecks,
            blocked);
    }

    for (const RegexLogEvent& event : regexLogs) {
        debuglog::WriteError(
            "[unwanted][regex] rule=%s status=%s code=%d",
            event.ruleId.c_str(),
            event.status.c_str(),
            event.errorCode);
    }

    if (perfLog) {
        debuglog::WriteInfo(
            "[unwanted][perf] window=%llums messages=%llu blocked=%llu candidates=%llu maxCandidates=%zu rules=%zu regexRules=%zu checks=%llu avgTotal=%.3fms avgWait=%.3fms avgMatch=%.3fms maxTotal=%.3fms maxWait=%.3fms maxMatch=%.3fms",
            static_cast<unsigned long long>(perfLog->windowMs),
            static_cast<unsigned long long>(perfLog->messages),
            static_cast<unsigned long long>(perfLog->blocked),
            static_cast<unsigned long long>(perfLog->candidates),
            perfLog->maxCandidates,
            perfLog->maxRules,
            perfLog->maxRegexRules,
            static_cast<unsigned long long>(perfLog->ruleChecks),
            perfLog->avgTotalMs,
            perfLog->avgWaitMs,
            perfLog->avgMatchMs,
            perfLog->maxTotalMs,
            perfLog->maxWaitMs,
            perfLog->maxMatchMs);
    }

    return blocked;
}

std::optional<UnwantedMessagesModule::PerfLogSnapshot> UnwantedMessagesModule::AccumulatePerfStats(
    double totalMs,
    double waitMs,
    double matchMs,
    std::size_t candidateCount,
    std::size_t enabledRules,
    std::size_t regexRules,
    std::size_t actualChecks,
    bool blocked) {
    const std::uint64_t nowMs = UnwantedPerfTickMs();
    if (perfStats_.windowStartMs == 0) {
        perfStats_.windowStartMs = nowMs;
    }

    ++perfStats_.messages;
    if (blocked) {
        ++perfStats_.blocked;
    }
    perfStats_.candidates += static_cast<std::uint64_t>(candidateCount);
    perfStats_.ruleChecks += static_cast<std::uint64_t>(actualChecks);
    perfStats_.totalMs += totalMs;
    perfStats_.waitMs += waitMs;
    perfStats_.matchMs += matchMs;
    perfStats_.maxTotalMs = std::max(perfStats_.maxTotalMs, totalMs);
    perfStats_.maxWaitMs = std::max(perfStats_.maxWaitMs, waitMs);
    perfStats_.maxMatchMs = std::max(perfStats_.maxMatchMs, matchMs);
    perfStats_.maxCandidates = std::max(perfStats_.maxCandidates, candidateCount);
    perfStats_.maxRules = std::max(perfStats_.maxRules, enabledRules);
    perfStats_.maxRegexRules = std::max(perfStats_.maxRegexRules, regexRules);

    const std::uint64_t windowMs = nowMs - perfStats_.windowStartMs;
    if (windowMs < kPerfTelemetryWindowMs) {
        return std::nullopt;
    }

    PerfLogSnapshot snapshot{};
    snapshot.windowMs = windowMs;
    snapshot.messages = perfStats_.messages;
    snapshot.blocked = perfStats_.blocked;
    snapshot.candidates = perfStats_.candidates;
    snapshot.ruleChecks = perfStats_.ruleChecks;
    snapshot.avgTotalMs = perfStats_.messages > 0
        ? perfStats_.totalMs / static_cast<double>(perfStats_.messages)
        : 0.0;
    snapshot.avgWaitMs = perfStats_.messages > 0
        ? perfStats_.waitMs / static_cast<double>(perfStats_.messages)
        : 0.0;
    snapshot.avgMatchMs = perfStats_.messages > 0
        ? perfStats_.matchMs / static_cast<double>(perfStats_.messages)
        : 0.0;
    snapshot.maxTotalMs = perfStats_.maxTotalMs;
    snapshot.maxWaitMs = perfStats_.maxWaitMs;
    snapshot.maxMatchMs = perfStats_.maxMatchMs;
    snapshot.maxCandidates = perfStats_.maxCandidates;
    snapshot.maxRules = perfStats_.maxRules;
    snapshot.maxRegexRules = perfStats_.maxRegexRules;

    perfStats_ = {};
    return snapshot;
}

bool UnwantedMessagesModule::IsMiscPageOpen() const {
    return page_ != Page::Closed;
}

bool UnwantedMessagesModule::DrawMiscCard() {
    UiSettings& ui = UiSettings::Instance();
    const float cardHeight = ScaleUi(136.0f);
    const ImVec2 screenMin = ImGui::GetCursorScreenPos();
    const ImVec2 screenMax(screenMin.x + ImGui::GetContentRegionAvail().x, screenMin.y + cardHeight);
    const bool hovered = ImGui::IsMouseHoveringRect(screenMin, screenMax);
    const bool held = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    const ImVec4 accent(0.55f, 0.86f, 0.98f, 1.0f);
    const ImVec4 baseBg = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    const ImVec4 hoverBg = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
    const ImVec4 activeBg = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    const ImVec4 bg = held ? activeBg : hovered ? hoverBg : baseBg;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ScaleUi(8.0f));
    if (ImGui::BeginChild("##misc_unwanted_card", ImVec2(0.0f, cardHeight), ImGuiChildFlags_FrameStyle)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 childMin = ImGui::GetWindowPos();
        const ImVec2 childMax(childMin.x + ImGui::GetWindowSize().x, childMin.y + ImGui::GetWindowSize().y);
        drawList->AddRectFilled(
            childMin,
            ImVec2(childMin.x + ScaleUi(6.0f), childMax.y),
            ImGui::GetColorU32(accent),
            ScaleUi(8.0f),
            ImDrawFlags_RoundCornersLeft);

        ImGui::SetCursorPos(ScaleUi(20.0f, 18.0f));
        ImGui::TextColored(accent, "%s", ui.Text(UiText::UnwantedTitle));
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x - ScaleUi(20.0f));
        ImGui::TextWrapped("%s", ui.Text(UiText::UnwantedEntryDesc));
        ImGui::PopTextWrapPos();

        const ImVec2 actionSize = ImGui::CalcTextSize(ui.Text(UiText::MiscOpenSectionAction));
        ImGui::SetCursorPosX(std::max(ScaleUi(20.0f), ImGui::GetWindowWidth() - actionSize.x - ScaleUi(20.0f)));
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - ScaleUi(34.0f)));
        ImGui::TextDisabled("%s", ui.Text(UiText::MiscOpenSectionAction));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        page_ = Page::Rules;
        return true;
    }
    return false;
}

void UnwantedMessagesModule::DrawMainPage() {
    bool reload = false;
    {
        std::lock_guard lock(statusMutex_);
        runtimeWarningsView_.clear();
        runtimeWarningsView_.reserve(runtimeWarnings_.size());
        for (const auto& [id, state] : runtimeWarnings_) {
            if (!state.status.empty()) {
                runtimeWarningsView_[id] = state.status;
            }
        }
    }
    RefreshLocalizedDiagnostics();
    switch (page_) {
    case Page::Editor:
        DrawEditorPage();
        break;
    case Page::Rules:
    default:
        DrawRulesPage();
        break;
    }
    DrawDeleteConfirmPopup();
    DrawUnsavedConfirmPopup();
    if (reloadRequested_) {
        reloadRequested_ = false;
        reload = true;
    }

    if (reload) {
        ReloadConfig();
    }
}

bool UnwantedMessagesModule::RuleHasProblem(const Rule& rule) const {
    return !rule.error.empty()
        || rule.duplicate
        || !rule.warning.empty()
        || runtimeWarningsView_.find(rule.id) != runtimeWarningsView_.end();
}

void UnwantedMessagesModule::DrawRulesHeader() {
    UiSettings& ui = UiSettings::Instance();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - ScaleUi(18.0f));
    const float frameHeight = ImGui::GetFrameHeight();
    const float backWidth = UnwantedButtonWidth(
        ui_icons::ChevronLeft,
        ui.Text(UiText::EditorBack),
        ScaleUi(112.0f));
    const float settingsWidth = UnwantedButtonWidth(ui_icons::Gear, "", ScaleUi(42.0f));
    const float addWidth = UnwantedButtonWidth(
        ui_icons::Plus,
        ui.Text(UiText::UnwantedAddRule),
        ScaleUi(174.0f));
    const char* stateText = ui.Text(settings_.enabled ? UiText::UnwantedModuleOn : UiText::UnwantedModuleOff);
    const float toggleWidth = frameHeight
        + style.ItemInnerSpacing.x
        + ImGui::CalcTextSize(ui.Text(UiText::UnwantedModuleToggleTitle)).x
        + style.ItemSpacing.x
        + ImGui::CalcTextSize(stateText).x;
    const float actionsWidth = toggleWidth + addWidth + settingsWidth + style.ItemSpacing.x * 2.0f;
    const float leadingWidth = backWidth
        + style.ItemSpacing.x
        + ImGui::CalcTextSize(ui.Text(UiText::UnwantedTitle)).x;
    const bool inlineActions = availableWidth >= leadingWidth + actionsWidth + ScaleUi(16.0f);
    const bool splitActions = !inlineActions && actionsWidth > availableWidth;
    const int rowCount = inlineActions ? 1 : (splitActions ? 3 : 2);
    const float headerHeight = frameHeight * static_cast<float>(rowCount)
        + style.ItemSpacing.y * static_cast<float>(rowCount - 1)
        + ScaleUi(16.0f);
    if (!BeginUnwantedPanel("##unwanted_page_header", ImVec2(0.0f, headerHeight))) {
        EndUnwantedPanel();
        return;
    }

    const float firstRowY = ImGui::GetCursorPosY();
    const float contentMinX = ImGui::GetCursorPosX();
    const float contentMaxX = ImGui::GetWindowWidth() - style.WindowPadding.x;
    if (UnwantedTextButton(
            ui_icons::ChevronLeft,
            ui.Text(UiText::EditorBack),
            "##unwanted_page_back",
            ImVec2(backWidth, 0.0f))) {
        page_ = Page::Closed;
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", ui.Text(UiText::UnwantedTitle));

    if (inlineActions) {
        ImGui::SameLine(std::max(
            ImGui::GetCursorPosX() + ScaleUi(8.0f),
            contentMaxX - actionsWidth));
    } else {
        ImGui::SetCursorPos(ImVec2(contentMinX, firstRowY + frameHeight + style.ItemSpacing.y));
    }

    bool enabled = settings_.enabled;
    if (ImGui::Checkbox("##unwanted_module_enabled", &enabled)) {
        settings_.enabled = enabled;
        PublishRuntimeSnapshot();
        SaveSettings();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(ui.Text(UiText::UnwantedModuleToggleTitle));
    DrawUnwantedTooltip(ui.Text(UiText::UnwantedModuleToggleDesc));
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%s",
        ui.Text(settings_.enabled ? UiText::UnwantedModuleOn : UiText::UnwantedModuleOff));
    if (splitActions) {
        ImGui::SetCursorPos(ImVec2(contentMinX, firstRowY + (frameHeight + style.ItemSpacing.y) * 2.0f));
    } else {
        ImGui::SameLine();
    }
    if (UnwantedPrimaryButton(
            ui_icons::Plus,
            ui.Text(UiText::UnwantedAddRule),
            "##unwanted_add_rule",
            ImVec2(addWidth, 0.0f))) {
        StartCreateRule({}, RuleType::Regex);
        page_ = Page::Editor;
    }
    ImGui::SameLine();
    if (UnwantedTextButton(ui_icons::Gear, "", "##unwanted_settings", ImVec2(settingsWidth, 0.0f))) {
        ImGui::OpenPopup("##unwanted_settings_popup");
    }
    DrawSettingsPopup();
    EndUnwantedPanel();
    ImGui::Spacing();
}

void UnwantedMessagesModule::DrawRulesSummary() {
    UiSettings& ui = UiSettings::Instance();
    const std::size_t enabled = std::count_if(rules_.begin(), rules_.end(), [](const Rule& rule) { return rule.enabled; });
    const std::size_t problems = static_cast<std::size_t>(std::count_if(
        rules_.begin(), rules_.end(), [this](const Rule& rule) { return RuleHasProblem(rule); }));
    MatchResult lastBlockedView;
    std::uint64_t blockedCountView = 0;
    {
        std::lock_guard lock(statusMutex_);
        lastBlockedView = lastBlocked_;
        blockedCountView = blockedCount_;
    }
    if (BeginUnwantedPanel("##unwanted_session_summary", ImVec2(0.0f, ScaleUi(66.0f)))) {
        ImGui::Text("%s", ui.Format(
            UiText::UnwantedRulesSummary,
            std::to_string(rules_.size()).c_str(),
            std::to_string(enabled).c_str(),
            std::to_string(problems).c_str()).c_str());
        const std::string blocked = ui.Format(
            UiText::UnwantedBlockedThisLaunch,
            std::to_string(blockedCountView).c_str());
        const float blockedWidth = ImGui::CalcTextSize(blocked.c_str()).x;
        if (ImGui::GetContentRegionAvail().x > blockedWidth + ScaleUi(40.0f)) {
            ImGui::SameLine(ImGui::GetWindowWidth() - blockedWidth - ImGui::GetStyle().WindowPadding.x);
        }
        ImGui::Text("%s", blocked.c_str());
        if (lastBlockedView.matched) {
            const std::string last = ui.Format(UiText::UnwantedLastBlockedCompact, lastBlockedView.candidate.c_str());
            ImGui::TextDisabled("%s", last.c_str());
            DrawUnwantedTooltip(lastBlockedView.candidate.c_str());
        } else {
            ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedLastBlockedEmpty));
        }
    }
    EndUnwantedPanel();
    ImGui::Spacing();
}

void UnwantedMessagesModule::DrawRulesPage() {
    const ImGuiIO& io = ImGui::GetIO();
    const bool ctrl = (io.KeyMods & ImGuiMod_Ctrl) != 0;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        StartCreateRule({}, RuleType::Regex);
        page_ = Page::Editor;
        return;
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        focusRuleSearch_ = true;
    }
    DrawRulesHeader();
    if (page_ == Page::Closed) {
        return;
    }
    DrawRulesSummary();
    DrawRulesPane(ImGui::GetContentRegionAvail());
}

jsonutil::JsonValue UnwantedMessagesModule::SerializeConfig() const {
    jsonutil::JsonObject root;
    root["schema_version"] = kUnwantedSchemaVersion;

    jsonutil::JsonObject settings;
    settings["enabled"] = settings_.enabled;
    settings["chat_asi_compatibility"] = settings_.chatAsiCompatibility;
    settings["max_pattern_len"] = settings_.maxPatternLength;

    jsonutil::JsonObject normalizer;
    normalizer["strip_colors"] = settings_.normalizer.stripColors;
    normalizer["collapse_ws"] = settings_.normalizer.collapseWhitespace;
    normalizer["trim"] = settings_.normalizer.trim;
    settings["normalizer"] = jsonutil::JsonValue(std::move(normalizer));
    root["settings"] = jsonutil::JsonValue(std::move(settings));

    jsonutil::JsonArray rules;
    rules.reserve(rules_.size());
    for (const Rule& rule : rules_) {
        jsonutil::JsonObject item;
        item["id"] = rule.id;
        if (!rule.name.empty()) {
            item["name"] = rule.name;
        }
        if (!rule.sample.empty()) {
            item["sample"] = rule.sample;
        }
        item["enabled"] = rule.enabled;
        item["type"] = rule.invalidType ? rule.rawType : RuleTypeName(rule.type);
        item["text"] = rule.text;
        item["nocase"] = rule.nocase;
        item["whole_word"] = rule.wholeWord;
        rules.emplace_back(std::move(item));
    }
    root["rules"] = jsonutil::JsonValue(std::move(rules));

    return jsonutil::JsonValue(std::move(root));
}

void UnwantedMessagesModule::LoadFromConfig(const jsonutil::JsonObject& section) {
    settings_ = Settings{};
    rules_.clear();
    selectedRuleIds_.clear();
    selectionMode_ = false;
    focusRuleSearch_ = false;
    activeRuleId_.clear();
    scrollToRuleId_.clear();
    helperWorkspaceMode_ = static_cast<int>(RegexWorkspaceMode::Automatic);
    ruleDraft_ = {};
    draftTesterCache_ = {};
    nextRuleSerial_ = 1;
    diagnosticsLanguage_ = -1;
    draftValidationCache_ = {};

    const jsonutil::JsonObject* settings = jsonutil::JsonObjectOrNull(&section, "settings");
    settings_.enabled = jsonutil::JsonBoolOr(settings, "enabled", true);
    settings_.chatAsiCompatibility = jsonutil::JsonBoolOr(settings, "chat_asi_compatibility", true);
    settings_.maxPatternLength = std::clamp(
        jsonutil::JsonNumberOr(settings, "max_pattern_len", 2048),
        1,
        kMaxConfigPatternLength);

    const jsonutil::JsonObject* normalizer = jsonutil::JsonObjectOrNull(settings, "normalizer");
    settings_.normalizer.stripColors = jsonutil::JsonBoolOr(normalizer, "strip_colors", false);
    settings_.normalizer.collapseWhitespace = jsonutil::JsonBoolOr(normalizer, "collapse_ws", false);
    settings_.normalizer.trim = jsonutil::JsonBoolOr(normalizer, "trim", false);

    const jsonutil::JsonArray* rules = jsonutil::JsonArrayOrNull(&section, "rules");
    {
        std::lock_guard lock(statusMutex_);
        runtimeWarnings_.clear();
        runtimeWarningsView_.clear();
    }
    if (!rules) {
        return;
    }

    for (const jsonutil::JsonValue& value : *rules) {
        const jsonutil::JsonObject* object = value.TryObject();
        if (!object) {
            continue;
        }
        const std::string id = jsonutil::JsonStringOr(object, "id", {});
        if (id.rfind("unwanted-", 0) != 0) {
            continue;
        }
        const std::string suffix = id.substr(9);
        char* end = nullptr;
        const unsigned long long serial = std::strtoull(suffix.c_str(), &end, 10);
        if (end && *end == '\0' && serial >= nextRuleSerial_) {
            nextRuleSerial_ = serial + 1;
        }
    }

    std::set<std::string, std::less<>> occupiedIds;
    for (const jsonutil::JsonValue& value : *rules) {
        const jsonutil::JsonObject* object = value.TryObject();
        if (!object) {
            continue;
        }

        Rule rule;
        rule.id = jsonutil::JsonStringOr(object, "id", {});
        if (rule.id.empty() || occupiedIds.find(rule.id) != occupiedIds.end()) {
            do {
                rule.id = AllocateRuleId();
            } while (occupiedIds.find(rule.id) != occupiedIds.end());
        }
        occupiedIds.insert(rule.id);
        rule.enabled = jsonutil::JsonBoolOr(object, "enabled", true);
        rule.name = jsonutil::JsonStringOr(object, "name", {});
        rule.sample = jsonutil::JsonStringOr(object, "sample", {});
        rule.rawType = jsonutil::JsonStringOr(object, "type", "literal");
        rule.invalidType = rule.rawType != "literal" && rule.rawType != "regex";
        rule.type = rule.rawType == "regex" ? RuleType::Regex : RuleType::Literal;
        rule.text = jsonutil::JsonStringOr(object, "text", {});
        rule.nocase = jsonutil::JsonBoolOr(object, "nocase", false);
        rule.wholeWord = jsonutil::JsonBoolOr(object, "whole_word", false);
        rules_.push_back(std::move(rule));
    }

}

void UnwantedMessagesModule::SaveConfig() const {
    AppConfig::Instance().QueueSectionReplace(std::string(kUnwantedSectionName), SerializeConfig());
}

void UnwantedMessagesModule::SaveSettings() const {
    const Settings settingsCopy = settings_;
    AppConfig::Instance().QueueSectionMutation(
        std::string(kUnwantedSectionName),
        [settingsCopy](jsonutil::JsonObject& section) {
            jsonutil::JsonObject settings;
            settings["enabled"] = settingsCopy.enabled;
            settings["chat_asi_compatibility"] = settingsCopy.chatAsiCompatibility;
            settings["max_pattern_len"] = settingsCopy.maxPatternLength;
            jsonutil::JsonObject normalizer;
            normalizer["strip_colors"] = settingsCopy.normalizer.stripColors;
            normalizer["collapse_ws"] = settingsCopy.normalizer.collapseWhitespace;
            normalizer["trim"] = settingsCopy.normalizer.trim;
            settings["normalizer"] = jsonutil::JsonValue(std::move(normalizer));
            section["settings"] = jsonutil::JsonValue(std::move(settings));
        },
        "unwanted:settings");
}

void UnwantedMessagesModule::ApplyChatAsiCompatibilitySetting() const {
    if (chatAsiCompatibilityHandler_) {
        chatAsiCompatibilityHandler_(settings_.chatAsiCompatibility);
    }
}

void UnwantedMessagesModule::CompileRules() {
    for (Rule& rule : rules_) {
        CompileRule(rule);
    }
    RebuildRuleViewCache();
}

void UnwantedMessagesModule::CompileRule(Rule& rule) {
    rule.validation = {};
    rule.prepared.reset();
    if (rule.invalidType) {
        rule.validation.error = ValidationError::UnknownType;
        FormatRuleDiagnostics(rule);
        return;
    }
    if (static_cast<int>(rule.text.size()) > settings_.maxPatternLength) {
        rule.validation.error = ValidationError::TooLong;
        FormatRuleDiagnostics(rule);
        return;
    }
    if (rule.text.empty()) {
        rule.validation.error = ValidationError::Empty;
        FormatRuleDiagnostics(rule);
        return;
    }

    auto prepared = std::make_shared<PreparedRule>();
    prepared->type = rule.type;
    prepared->text = rule.text;
    prepared->nocase = rule.nocase;
    prepared->wholeWord = rule.type == RuleType::Literal && rule.wholeWord;
    if (rule.type == RuleType::Literal) {
        prepared->literalNeedle = rule.nocase ? Utf8ToLower(rule.text) : rule.text;
        rule.prepared = std::move(prepared);
        FormatRuleDiagnostics(rule);
        return;
    }

    const bool legacyAnchored = rule.text.size() >= 2 && rule.text.front() == '^' && rule.text.back() == '$';
    const bool absoluteAnchored = rule.text.size() >= 4 && rule.text.rfind("\\A", 0) == 0
        && rule.text.substr(rule.text.size() - 2) == "\\z";
    rule.validation.unanchored = !legacyAnchored && !absoluteAnchored;
    rule.validation.broadWildcard = ContainsBroadWildcard(rule.text);
    unwanted_regex::CompileResult compiled = unwanted_regex::Compile(rule.text, rule.nocase);
    if (!compiled.program) {
        rule.validation.error = ValidationError::RegexCompile;
        rule.validation.pcreErrorOffset = compiled.errorOffset;
        FormatRuleDiagnostics(rule);
        return;
    }
    rule.validation.matchesEmpty = compiled.program->MatchesEmpty();
    prepared->compiledRegex = std::move(compiled.program);
    rule.prepared = std::move(prepared);
    FormatRuleDiagnostics(rule);
}

void UnwantedMessagesModule::FormatRuleDiagnostics(Rule& rule) const {
    UiSettings& ui = UiSettings::Instance();
    rule.error.clear();
    rule.warning.clear();
    switch (rule.validation.error) {
    case ValidationError::Empty:
        rule.error = ui.Text(UiText::UnwantedErrorEmpty);
        break;
    case ValidationError::TooLong:
        rule.error = ui.Format(UiText::UnwantedErrorTooLong, std::to_string(settings_.maxPatternLength).c_str());
        break;
    case ValidationError::UnknownType:
        rule.error = ui.Format(UiText::UnwantedErrorUnknownType, rule.rawType.c_str());
        break;
    case ValidationError::RegexCompile:
        rule.error = ui.Format(
            UiText::UnwantedPcreErrorFormat,
            FormatCompilePosition(rule.text, rule.validation.pcreErrorOffset).c_str());
        break;
    case ValidationError::None:
    default:
        break;
    }
    if (rule.validation.unanchored) {
        AppendWarning(rule.warning, ui.Text(UiText::UnwantedRegexSafetyUnanchored));
    }
    if (rule.validation.broadWildcard) {
        AppendWarning(rule.warning, ui.Text(UiText::UnwantedRegexBroadWildcard));
    }
    if (rule.validation.matchesEmpty) {
        AppendWarning(rule.warning, ui.Text(UiText::UnwantedRegexMatchesEmpty));
    }
}

void UnwantedMessagesModule::RefreshLocalizedDiagnostics() {
    const int language = static_cast<int>(UiSettings::Instance().Language());
    if (diagnosticsLanguage_ == language) {
        return;
    }
    diagnosticsLanguage_ = language;
    draftValidationCache_.ready = false;
    for (Rule& rule : rules_) {
        FormatRuleDiagnostics(rule);
    }
    RebuildRuleViewCache();
    if (!helperSample_.empty()) {
        RegenerateHelperOutput();
    }
}

void UnwantedMessagesModule::RebuildRuleViewCache() {
    std::unordered_map<std::string, std::size_t> counts;
    counts.reserve(rules_.size());
    const auto duplicateKey = [](const Rule& rule) {
        const std::string typeKey = rule.invalidType ? "invalid:" + rule.rawType : RuleTypeName(rule.type);
        return typeKey + ":" + std::to_string(rule.text.size()) + ":"
            + rule.text + ":" + (rule.nocase ? "1" : "0") + (rule.wholeWord ? "1" : "0");
    };
    for (const Rule& rule : rules_) {
        ++counts[duplicateKey(rule)];
    }
    for (Rule& rule : rules_) {
        rule.duplicate = counts[duplicateKey(rule)] > 1;
        rule.exactDisplayText.clear();
        if (rule.type == RuleType::Regex) {
            TryExactRegexDisplayText(rule.text, rule.exactDisplayText);
        }
        const std::string& fallbackName = rule.exactDisplayText.empty() ? rule.text : rule.exactDisplayText;
        const std::string& sortName = rule.name.empty() ? fallbackName : rule.name;
        rule.sortNameLower = Utf8ToLower(sortName);
        rule.searchBlobLower = Utf8ToLower(
            rule.id + "\n" + rule.name + "\n" + rule.sample + "\n" + rule.text + "\n" + RuleTypeName(rule.type)
            + "\n" + rule.exactDisplayText + "\n" + rule.error + "\n" + rule.warning);
    }
}

void UnwantedMessagesModule::PublishRuntimeSnapshot() {
    auto snapshot = std::make_shared<RuntimeSnapshot>();
    snapshot->settings = settings_;
    snapshot->rules.reserve(rules_.size());
    for (const Rule& rule : rules_) {
        if (!rule.enabled || !rule.error.empty() || !rule.prepared) {
            continue;
        }
        snapshot->rules.push_back(RuntimeRule{rule.id, rule.prepared});
        if (rule.type == RuleType::Regex) {
            ++snapshot->regexRules;
        } else if (rule.nocase) {
            snapshot->hasNoCaseLiteral = true;
        }
    }
    std::lock_guard lock(snapshotMutex_);
    runtimeSnapshot_ = std::move(snapshot);
}

std::string UnwantedMessagesModule::AllocateRuleId() {
    return "unwanted-" + std::to_string(nextRuleSerial_++);
}

std::vector<std::string> UnwantedMessagesModule::BuildCandidates(
    const UnwantedMessageContext& context,
    const Settings& settings) const {
    std::vector<std::string> rawCandidates;
    rawCandidates.reserve(8);
    AddUnique(rawCandidates, context.text);

    // SA:MP may pass the prefix and text separately. Depending on the chat type,
    // the text already starts with its own separator (often after a color tag),
    // so retain both API representations without changing the legacy one.
    if (!context.prefix.empty()) {
        AddUnique(rawCandidates, context.prefix + " " + context.text);
        AddUnique(rawCandidates, context.prefix + context.text);
    }

    if (context.playerId >= 0) {
        const std::string id = std::to_string(context.playerId);
        const bool hasName = !context.playerName.empty() && context.playerName != "UNKNOWN";
        if (hasName) {
            AddUnique(rawCandidates, context.playerName + "[" + id + "]: " + context.text);
            AddUnique(rawCandidates, context.playerName + ": " + context.text);
            if (context.prefix != context.playerName) {
                AddUnique(rawCandidates, context.playerName + " " + context.text);
            }
        }
        AddUnique(rawCandidates, "[" + id + "] " + context.text);
        AddUnique(rawCandidates, id + " " + context.text);
    }

    std::vector<std::string> candidates;
    candidates.reserve(rawCandidates.size() * 2);
    for (const std::string& rawCandidate : rawCandidates) {
        AddUnique(candidates, NormalizeCandidate(rawCandidate, settings));
    }

    // Formatting codes are not visible to the player. Keep raw candidates for
    // color-aware legacy rules, but also expose the rendered text so a rule made
    // from copied/visible chat behaves exactly like the editor tester.
    if (!settings.normalizer.stripColors) {
        for (const std::string& rawCandidate : rawCandidates) {
            const std::string visibleCandidate = StripColorTags(rawCandidate);
            if (visibleCandidate != rawCandidate) {
                AddUnique(candidates, NormalizeCandidate(visibleCandidate, settings));
            }
        }
    }

    return candidates;
}

std::string UnwantedMessagesModule::NormalizeCandidate(std::string_view text, const Settings& settings) const {
    std::string result(text);
    if (settings.normalizer.stripColors) {
        result = StripColorTags(result);
    }
    if (settings.normalizer.collapseWhitespace) {
        result = CollapseWhitespace(result);
    }
    if (settings.normalizer.trim) {
        result = TrimAscii(result);
    }
    return result;
}

std::string UnwantedMessagesModule::NormalizeCandidate(std::string_view text) const {
    return NormalizeCandidate(text, settings_);
}

bool UnwantedMessagesModule::MatchCandidates(
    const RuntimeSnapshot& snapshot,
    const std::vector<std::string>& candidates,
    const std::vector<std::string>& foldedCandidates,
    UnwantedMessageSource source,
    MatchResult* result,
    std::size_t& actualChecks,
    std::vector<unsigned char>& evaluationState,
    std::vector<unwanted_regex::MatchResult>& runtimeErrors) const {
    for (std::size_t candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex) {
        const std::string& candidate = candidates[candidateIndex];
        const std::string_view foldedCandidate = foldedCandidates.empty()
            ? std::string_view{}
            : std::string_view(foldedCandidates[candidateIndex]);
        for (std::size_t ruleIndex = 0; ruleIndex < snapshot.rules.size(); ++ruleIndex) {
            const RuntimeRule& rule = snapshot.rules[ruleIndex];
            ++actualChecks;
            const unwanted_regex::MatchResult match = MatchRule(rule, candidate, foldedCandidate);
            if (match.status != unwanted_regex::MatchStatus::Match
                && match.status != unwanted_regex::MatchStatus::NoMatch) {
                evaluationState[ruleIndex] = 2;
                runtimeErrors[ruleIndex] = match;
                continue;
            }
            if (evaluationState[ruleIndex] == 0) {
                evaluationState[ruleIndex] = 1;
            }
            if (match.status != unwanted_regex::MatchStatus::Match) {
                continue;
            }

            if (result) {
                result->matched = true;
                result->ruleId = rule.id;
                result->ruleText = rule.prepared ? rule.prepared->text : std::string{};
                result->candidate = candidate;
                result->source = source;
            }
            return true;
        }
    }
    return false;
}

unwanted_regex::MatchResult UnwantedMessagesModule::MatchRule(
    const RuntimeRule& rule,
    std::string_view candidate,
    std::string_view foldedCandidate) const {
    if (!rule.prepared) {
        return {unwanted_regex::MatchStatus::NoMatch, 0};
    }
    if (rule.prepared->type == RuleType::Literal) {
        return {
            MatchLiteral(*rule.prepared, candidate, foldedCandidate)
                ? unwanted_regex::MatchStatus::Match
                : unwanted_regex::MatchStatus::NoMatch,
            0};
    }
    if (!rule.prepared->compiledRegex) {
        return {unwanted_regex::MatchStatus::NoMatch, 0};
    }
    return rule.prepared->compiledRegex->Match(candidate);
}

bool UnwantedMessagesModule::MatchLiteral(
    const PreparedRule& rule,
    std::string_view candidate,
    std::string_view foldedCandidate) const {
    const std::string_view haystack = rule.nocase ? foldedCandidate : candidate;
    const std::string_view needle = rule.literalNeedle;
    if (needle.empty()) {
        return false;
    }

    std::size_t pos = haystack.find(needle);
    while (pos != std::string::npos) {
        const std::size_t end = pos + needle.size();
        if (!rule.wholeWord || IsWordBoundary(haystack, pos, end)) {
            return true;
        }
        pos = haystack.find(needle, pos + 1);
    }

    return false;
}

void UnwantedMessagesModule::ApplyRuntimeEvaluation(
    const RuntimeSnapshot& snapshot,
    const std::vector<unsigned char>& evaluationState,
    const std::vector<unwanted_regex::MatchResult>& runtimeErrors,
    std::vector<RegexLogEvent>& logs) {
    {
        std::lock_guard lock(snapshotMutex_);
        if (runtimeSnapshot_.get() != &snapshot) {
            return;
        }
    }
    const std::uint64_t nowMs = UnwantedPerfTickMs();
    std::lock_guard lock(statusMutex_);
    for (std::size_t i = 0; i < snapshot.rules.size(); ++i) {
        if (evaluationState[i] == 0) {
            continue;
        }
        RuntimeWarningState& state = runtimeWarnings_[snapshot.rules[i].id];
        if (evaluationState[i] == 1) {
            state.status.clear();
            continue;
        }
        const unwanted_regex::MatchResult& error = runtimeErrors[i];
        state.status = unwanted_regex::MatchStatusName(error.status);
        if (nowMs - state.lastLogMs >= kPerfTelemetryWindowMs) {
            state.lastLogMs = nowMs;
            logs.push_back(RegexLogEvent{snapshot.rules[i].id, state.status, error.errorCode});
        }
    }
}

void UnwantedMessagesModule::DrawRulesPane(const ImVec2& size) {
    UiSettings& ui = UiSettings::Instance();
    if (!BeginUnwantedPanel("##unwanted_rules_pane", size)) {
        EndUnwantedPanel();
        return;
    }

    const std::string searchHint = std::string(ui_icons::Search) + " " + ui.Text(UiText::UnwantedSearchHint);
    const float sortWidth = ScaleUi(190.0f);
    const float toolbarWidth = ImGui::GetContentRegionAvail().x;
    const bool inlineSort = toolbarWidth >= ScaleUi(520.0f);
    ImGui::SetNextItemWidth(inlineSort
        ? std::max(ScaleUi(180.0f), toolbarWidth - sortWidth - ImGui::GetStyle().ItemSpacing.x)
        : -1.0f);
    if (focusRuleSearch_) {
        ImGui::SetKeyboardFocusHere();
        focusRuleSearch_ = false;
    }
    InputTextWithHintString("##unwanted_rule_search", searchHint.c_str(), ruleSearch_, ImGuiInputTextFlags_AutoSelectAll, 128);
    const char* sortLabel = ui.Text(UiText::UnwantedSortStored);
    if (ruleSort_ == RuleSort::Name) sortLabel = ui.Text(UiText::UnwantedSortName);
    if (ruleSort_ == RuleSort::Type) sortLabel = ui.Text(UiText::UnwantedSortByType);
    if (ruleSort_ == RuleSort::Status) sortLabel = ui.Text(UiText::UnwantedSortStatus);
    const std::string sortPreview = ui.Format(UiText::UnwantedSortFormat, sortLabel);
    if (inlineSort) {
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(inlineSort ? sortWidth : -1.0f);
    if (ImGui::BeginCombo("##unwanted_sort", sortPreview.c_str())) {
        if (ImGui::Selectable(ui.Text(UiText::UnwantedSortStored), ruleSort_ == RuleSort::Stored)) ruleSort_ = RuleSort::Stored;
        if (ImGui::Selectable(ui.Text(UiText::UnwantedSortName), ruleSort_ == RuleSort::Name)) ruleSort_ = RuleSort::Name;
        if (ImGui::Selectable(ui.Text(UiText::UnwantedSortByType), ruleSort_ == RuleSort::Type)) ruleSort_ = RuleSort::Type;
        if (ImGui::Selectable(ui.Text(UiText::UnwantedSortStatus), ruleSort_ == RuleSort::Status)) ruleSort_ = RuleSort::Status;
        ImGui::EndCombo();
    }

    std::size_t enabledCount = 0;
    std::size_t problemCount = 0;
    for (const Rule& rule : rules_) {
        enabledCount += rule.enabled ? 1u : 0u;
        problemCount += RuleHasProblem(rule) ? 1u : 0u;
    }
    const std::size_t disabledCount = rules_.size() - enabledCount;
    const float wrapRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    bool firstChip = true;
    const auto drawChip = [&](RuleFilter filter, UiText label, std::size_t count) {
        const std::string text = std::string(ui.Text(label)) + " " + std::to_string(count);
        const float width = ImGui::CalcTextSize(text.c_str()).x + ScaleUi(22.0f);
        if (!firstChip) {
            if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= wrapRight) {
                ImGui::SameLine();
            }
        }
        if (FilterChip(text.c_str(), ruleFilter_ == filter)) {
            ruleFilter_ = filter;
        }
        firstChip = false;
    };

    drawChip(RuleFilter::All, UiText::UnwantedFilterAll, rules_.size());
    drawChip(RuleFilter::Enabled, UiText::UnwantedFilterEnabled, enabledCount);
    drawChip(RuleFilter::Disabled, UiText::UnwantedFilterDisabled, disabledCount);
    drawChip(RuleFilter::Problems, UiText::UnwantedFilterProblems, problemCount);
    const float preferredTypeFilterWidth = ScaleUi(190.0f);
    if (!firstChip
        && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + preferredTypeFilterWidth <= wrapRight) {
        ImGui::SameLine();
    }
    DrawRuleTypeFilter(std::min(preferredTypeFilterWidth, ImGui::GetContentRegionAvail().x));

    ImGui::Spacing();
    std::vector<std::size_t> visible;
    visible.reserve(rules_.size());
    const std::string loweredSearch = Utf8ToLower(TrimAscii(ruleSearch_));
    for (std::size_t i = 0; i < rules_.size(); ++i) {
        if (RuleMatchesFilter(rules_[i], loweredSearch)) {
            visible.push_back(i);
        }
    }
    if (ruleSort_ != RuleSort::Stored) {
        std::stable_sort(visible.begin(), visible.end(), [&](std::size_t leftIndex, std::size_t rightIndex) {
            const Rule& left = rules_[leftIndex];
            const Rule& right = rules_[rightIndex];
            if (ruleSort_ == RuleSort::Name) {
                return left.sortNameLower < right.sortNameLower;
            }
            if (ruleSort_ == RuleSort::Type) {
                return left.type < right.type;
            }
            const int leftStatus = !left.error.empty() ? 0
                : left.duplicate ? 1
                : runtimeWarningsView_.find(left.id) != runtimeWarningsView_.end() || !left.warning.empty() ? 2 : 3;
            const int rightStatus = !right.error.empty() ? 0
                : right.duplicate ? 1
                : runtimeWarningsView_.find(right.id) != runtimeWarningsView_.end() || !right.warning.empty() ? 2 : 3;
            return leftStatus < rightStatus;
        });
    }

    const float actionRowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    const auto placeActionInlineIfFits = [&](float nextWidth) {
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + nextWidth <= actionRowRight) {
            ImGui::SameLine();
        }
    };
    if (selectionMode_) {
        RetainVisibleSelection(visible);
        const bool allShownSelected = !visible.empty() && std::all_of(
            visible.begin(), visible.end(), [this](std::size_t index) {
                return index < rules_.size() && IsRuleSelected(rules_[index].id);
            });
        bool selectShown = allShownSelected;
        if (ImGui::Checkbox(
                ui.Format(UiText::UnwantedSelectShown, std::to_string(visible.size()).c_str()).c_str(),
                &selectShown)) {
            SelectVisibleRules(visible, selectShown);
        }
        const std::string selectedText = ui.Format(
            UiText::UnwantedBulkActionsFormat,
            std::to_string(selectedRuleIds_.size()).c_str());
        placeActionInlineIfFits(ImGui::CalcTextSize(selectedText.c_str()).x);
        ImGui::TextDisabled("%s", selectedText.c_str());
        if (!selectedRuleIds_.empty()) {
            placeActionInlineIfFits(UnwantedButtonWidth(nullptr, ui.Text(UiText::UnwantedEnableSelected)));
            if (ImGui::Button(ui.Text(UiText::UnwantedEnableSelected))) {
                SetSelectedRulesEnabled(true);
            }
            placeActionInlineIfFits(UnwantedButtonWidth(nullptr, ui.Text(UiText::UnwantedDisableSelected)));
            if (ImGui::Button(ui.Text(UiText::UnwantedDisableSelected))) {
                SetSelectedRulesEnabled(false);
            }
            placeActionInlineIfFits(UnwantedButtonWidth(nullptr, ui.Text(UiText::UnwantedDeleteSelected)));
            if (ImGui::Button(ui.Text(UiText::UnwantedDeleteSelected))) {
                deleteSelectedConfirmOpen_ = true;
            }
        }
        placeActionInlineIfFits(UnwantedButtonWidth(nullptr, ui.Text(UiText::UnwantedExitSelection)));
        if (ImGui::Button(ui.Text(UiText::UnwantedExitSelection))) {
            selectionMode_ = false;
            ClearSelection();
        }
    } else if (ImGui::Button(ui.Text(UiText::UnwantedSelectionMode))) {
        selectionMode_ = true;
        ClearSelection();
    }
    const std::string visibleText = ui.Format(
        UiText::UnwantedVisibleRulesFormat,
        std::to_string(visible.size()).c_str(),
        std::to_string(rules_.size()).c_str());
    placeActionInlineIfFits(ImGui::CalcTextSize(visibleText.c_str()).x);
    ImGui::TextDisabled("%s", visibleText.c_str());
    DrawRulesTable(visible);
    EndUnwantedPanel();
}

void UnwantedMessagesModule::DrawRuleTypeFilter(float width) {
    UiSettings& ui = UiSettings::Instance();
    const char* label = ui.Text(UiText::UnwantedTypeFilterAll);
    if (ruleTypeFilter_ == RuleTypeFilter::Literal) label = ui.Text(UiText::UnwantedTypeLiteral);
    if (ruleTypeFilter_ == RuleTypeFilter::Regex) label = ui.Text(UiText::UnwantedTypeRegex);
    const std::string preview = std::string(ui.Text(UiText::UnwantedTypeFilter)) + ": " + label;
    ImGui::SetNextItemWidth(std::max(1.0f, width));
    if (ImGui::BeginCombo("##unwanted_type_filter", preview.c_str())) {
        if (ImGui::Selectable(ui.Text(UiText::UnwantedTypeFilterAll), ruleTypeFilter_ == RuleTypeFilter::All)) {
            ruleTypeFilter_ = RuleTypeFilter::All;
        }
        if (ImGui::Selectable(ui.Text(UiText::UnwantedTypeLiteral), ruleTypeFilter_ == RuleTypeFilter::Literal)) {
            ruleTypeFilter_ = RuleTypeFilter::Literal;
        }
        if (ImGui::Selectable(ui.Text(UiText::UnwantedTypeRegex), ruleTypeFilter_ == RuleTypeFilter::Regex)) {
            ruleTypeFilter_ = RuleTypeFilter::Regex;
        }
        ImGui::EndCombo();
    }
}

void UnwantedMessagesModule::DrawRulesTable(const std::vector<std::size_t>& visibleIndices) {
    UiSettings& ui = UiSettings::Instance();
    const UnwantedVisualStyle visual = UnwantedStyleTokens();
    if (rules_.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedNoRules));
        if (UnwantedPrimaryButton(
                ui_icons::Plus,
                ui.Text(UiText::UnwantedCreateOpen),
                "##unwanted_empty_create",
                ScaleUi(180.0f, 0.0f))) {
            StartCreateRule({}, RuleType::Regex);
            page_ = Page::Editor;
        }
        return;
    }
    if (visibleIndices.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedNoVisibleRules));
        if (ImGui::Button(ui.Text(UiText::UnwantedResetFilters), ScaleUi(210.0f, 0.0f))) {
            ruleSearch_.clear();
            ruleFilter_ = RuleFilter::All;
            ruleTypeFilter_ = RuleTypeFilter::All;
        }
        return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp
        | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_NoSavedSettings
        | ImGuiTableFlags_BordersInnerH
        | ImGuiTableFlags_BordersOuter;
    const float tableWidth = ImGui::GetContentRegionAvail().x;
    const bool splitTypeAndStatus = tableWidth >= ScaleUi(900.0f);
    const int columnCount = (selectionMode_ ? 1 : 0) + (splitTypeAndStatus ? 5 : 4);
    if (!ImGui::BeginTable("##unwanted_rules_table", columnCount, flags, ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const auto fixedTextColumnWidth = [&](std::initializer_list<const char*> labels, float minimum) {
        float width = minimum;
        for (const char* label : labels) {
            width = std::max(width, ImGui::CalcTextSize(label).x + style.CellPadding.x * 2.0f + ScaleUi(8.0f));
        }
        return width;
    };
    const float typeColumnWidth = fixedTextColumnWidth(
        {ui.Text(UiText::UnwantedColumnType), ui.Text(UiText::UnwantedTypeLiteral), ui.Text(UiText::UnwantedTypeRegex)},
        ScaleUi(88.0f));
    const float statusColumnWidth = fixedTextColumnWidth(
        {ui.Text(UiText::UnwantedColumnStatus),
         ui.Text(UiText::UnwantedRuleOk),
         ui.Text(UiText::UnwantedRuleDisabled),
         ui.Text(UiText::UnwantedInvalidRule),
         ui.Text(UiText::UnwantedWarning),
         ui.Text(UiText::UnwantedDuplicate)},
        ScaleUi(112.0f));
    const float typeStatusColumnWidth = fixedTextColumnWidth(
        {ui.Text(UiText::UnwantedColumnTypeStatus)},
        std::max(typeColumnWidth, statusColumnWidth));
    const float actionsColumnWidth = fixedTextColumnWidth(
        {ui.Text(UiText::UnwantedColumnActions)},
        ScaleUi(72.0f));

    ImGui::TableSetupScrollFreeze(0, 1);
    if (selectionMode_) {
        ImGui::TableSetupColumn(ui.Text(UiText::UnwantedColumnSelect), ImGuiTableColumnFlags_WidthFixed, ScaleUi(42.0f));
    }
    ImGui::TableSetupColumn(ui.Text(UiText::UnwantedColumnEnabled), ImGuiTableColumnFlags_WidthFixed, ScaleUi(42.0f));
    ImGui::TableSetupColumn(ui.Text(UiText::UnwantedColumnRule), ImGuiTableColumnFlags_WidthStretch, 1.0f);
    if (splitTypeAndStatus) {
        ImGui::TableSetupColumn(ui.Text(UiText::UnwantedColumnType), ImGuiTableColumnFlags_WidthFixed, typeColumnWidth);
        ImGui::TableSetupColumn(ui.Text(UiText::UnwantedColumnStatus), ImGuiTableColumnFlags_WidthFixed, statusColumnWidth);
    } else {
        ImGui::TableSetupColumn(
            ui.Text(UiText::UnwantedColumnTypeStatus),
            ImGuiTableColumnFlags_WidthFixed,
            typeStatusColumnWidth);
    }
    ImGui::TableSetupColumn(
        ui.Text(UiText::UnwantedColumnActions),
        ImGuiTableColumnFlags_WidthFixed,
        actionsColumnWidth);
    ImGui::TableHeadersRow();

    int scrollTargetRow = -1;
    if (!scrollToRuleId_.empty()) {
        const auto target = std::find_if(visibleIndices.begin(), visibleIndices.end(), [&](std::size_t index) {
            return index < rules_.size() && rules_[index].id == scrollToRuleId_;
        });
        if (target != visibleIndices.end()) {
            scrollTargetRow = static_cast<int>(std::distance(visibleIndices.begin(), target));
        } else if (FindRuleIndexById(scrollToRuleId_) < 0) {
            scrollToRuleId_.clear();
        }
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visibleIndices.size()));
    if (scrollTargetRow >= 0) {
        clipper.IncludeItemByIndex(scrollTargetRow);
    }
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::size_t index = visibleIndices[static_cast<std::size_t>(row)];
            Rule& rule = rules_[index];
            const bool active = !ruleDraft_.createMode && activeRuleId_ == rule.id;
            const float rowHeight = ScaleUi(54.0f);
            const float ruleCellHeight = ScaleUi(46.0f);
            ImGui::PushID(rule.id.c_str());
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);

            int column = 0;
            if (selectionMode_) {
                ImGui::TableSetColumnIndex(column++);
                CenterTableCellVertically(rowHeight, ImGui::GetFrameHeight());
                bool selected = IsRuleSelected(rule.id);
                if (ImGui::Checkbox("##selected", &selected)) {
                    SetRuleSelected(rule.id, selected);
                }
                DrawUnwantedTooltip(ui.Text(UiText::UnwantedSelectRuleHelp));
            }

            ImGui::TableSetColumnIndex(column++);
            CenterTableCellVertically(rowHeight, ImGui::GetFrameHeight());
            if (ImGui::Checkbox("##enabled", &rule.enabled)) {
                PublishRuntimeSnapshot();
                SaveConfig();
            }
            DrawUnwantedTooltip(ui.Text(UiText::UnwantedRuleEnabled));

            ImGui::TableSetColumnIndex(column++);
            CenterTableCellVertically(rowHeight, ruleCellHeight);
            const std::string visibleTitle = RuleDisplayTitle(rule);
            const std::string visibleSummary = RuleDisplaySummary(rule);
            if (ImGui::Selectable(
                    "##rule_select",
                    active,
                    ImGuiSelectableFlags_SpanAvailWidth | ImGuiSelectableFlags_AllowDoubleClick,
                    ImVec2(0.0f, ruleCellHeight))) {
                StartEditRule(rule.id);
                page_ = Page::Editor;
            }
            const ImVec2 labelMin = ImGui::GetItemRectMin();
            const ImVec2 labelMax = ImGui::GetItemRectMax();
            ImDrawList* rowDrawList = ImGui::GetWindowDrawList();
            const float textMinX = labelMin.x + style.FramePadding.x;
            const float textMaxX = labelMax.x - style.FramePadding.x;
            const ImVec2 titleMin(textMinX, labelMin.y + ScaleUi(3.0f));
            const ImVec2 titleMax(textMaxX, titleMin.y + ImGui::GetTextLineHeight());
            const ImVec2 summaryMin(textMinX, labelMin.y + ScaleUi(25.0f));
            const ImVec2 summaryMax(textMaxX, summaryMin.y + ImGui::GetTextLineHeight());
            ImGui::RenderTextEllipsis(
                rowDrawList,
                titleMin,
                titleMax,
                titleMax.x,
                visibleTitle.c_str(),
                nullptr,
                nullptr);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::RenderTextEllipsis(
                rowDrawList,
                summaryMin,
                summaryMax,
                summaryMax.x,
                visibleSummary.c_str(),
                nullptr,
                nullptr);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s\n\n%s", visibleSummary.c_str(), rule.text.c_str());
            }

            const char* typeLabel = rule.type == RuleType::Regex
                ? ui.Text(UiText::UnwantedTypeRegex)
                : ui.Text(UiText::UnwantedTypeLiteral);
            const char* statusLabel = ui.Text(UiText::UnwantedRuleOk);
            ImVec4 statusColor = visual.ok;
            const char* statusTooltip = nullptr;
            if (!rule.enabled) {
                statusLabel = ui.Text(UiText::UnwantedRuleDisabled);
                statusColor = visual.faintText;
            } else if (!rule.error.empty()) {
                statusLabel = ui.Text(UiText::UnwantedInvalidRule);
                statusColor = visual.danger;
                statusTooltip = rule.error.c_str();
            } else if (IsDuplicateRule(index)) {
                statusLabel = ui.Text(UiText::UnwantedDuplicate);
                statusColor = visual.warn;
            } else if (const auto runtimeWarning = runtimeWarningsView_.find(rule.id);
                       runtimeWarning != runtimeWarningsView_.end()) {
                statusLabel = ui.Text(UiText::UnwantedWarning);
                statusColor = visual.warn;
                statusTooltip = RuntimeWarningDisplay(runtimeWarning->second);
            } else if (!rule.warning.empty()) {
                statusLabel = ui.Text(UiText::UnwantedWarning);
                statusColor = visual.warn;
                statusTooltip = rule.warning.c_str();
            }

            ImGui::TableSetColumnIndex(column++);
            const float badgeHeight = BadgeSize(typeLabel).y;
            if (splitTypeAndStatus) {
                CenterTableCellVertically(rowHeight, badgeHeight);
                TextBadge(typeLabel, visual.mutedText);
                ImGui::TableSetColumnIndex(column++);
                CenterTableCellVertically(rowHeight, ImGui::GetTextLineHeight());
            } else {
                CenterTableCellVertically(
                    rowHeight,
                    badgeHeight + style.ItemSpacing.y + ImGui::GetTextLineHeight());
                TextBadge(typeLabel, visual.mutedText);
            }
            ImGui::TextColored(statusColor, "%s", statusLabel);
            if (statusTooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", statusTooltip);
            }
            if (row == scrollTargetRow && rule.id == scrollToRuleId_) {
                ImGui::SetScrollHereY(0.5f);
                scrollToRuleId_.clear();
            }

            ImGui::TableSetColumnIndex(column);
            CenterTableCellVertically(rowHeight, ImGui::GetFrameHeight());
            if (UnwantedTextButton(ui_icons::Bars, "", "##rule_actions_button", ScaleUi(40.0f, 0.0f))) {
                ImGui::OpenPopup("##rule_actions");
            }
            DrawUnwantedTooltip(ui.Text(UiText::UnwantedColumnActions));
            if (ImGui::BeginPopup("##rule_actions")) {
                if (ImGui::MenuItem(ui.Text(UiText::Edit))) {
                    StartEditRule(rule.id);
                    page_ = Page::Editor;
                }
                if (ImGui::MenuItem(ui.Text(UiText::UnwantedDuplicateAction))) {
                    const std::string id = rule.id;
                    ImGui::EndPopup();
                    ImGui::PopID();
                    ImGui::EndTable();
                    DuplicateRule(id);
                    return;
                }
                const bool manualSort = ruleSort_ == RuleSort::Stored;
                if (ImGui::MenuItem(ui.Text(UiText::MoveUp), nullptr, false, manualSort && index > 0)) {
                    std::swap(rules_[index], rules_[index - 1]);
                    PublishRuntimeSnapshot();
                    SaveConfig();
                }
                if (ImGui::MenuItem(ui.Text(UiText::MoveDown), nullptr, false, manualSort && index + 1 < rules_.size())) {
                    std::swap(rules_[index], rules_[index + 1]);
                    PublishRuntimeSnapshot();
                    SaveConfig();
                }
                ImGui::Separator();
                if (ImGui::MenuItem(ui.Text(UiText::UnwantedCopyPattern))) {
                    ImGui::SetClipboardText(rule.text.c_str());
                }
                if (ImGui::MenuItem(ui.Text(UiText::Delete))) {
                    selectedRuleIds_.clear();
                    selectedRuleIds_.insert(rules_[index].id);
                    deleteSelectedConfirmOpen_ = true;
                    ImGui::EndPopup();
                    ImGui::PopID();
                    ImGui::EndTable();
                    return;
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
    }

    ImGui::EndTable();
}

void UnwantedMessagesModule::DrawEditorPage() {
    UiSettings& ui = UiSettings::Instance();
    if (!ruleDraft_.active) {
        StartCreateRule({}, RuleType::Regex);
    }

    const auto requestBack = [&] {
        if (ruleDraft_.dirty) {
            pendingPageAfterDiscard_ = Page::Rules;
            unsavedConfirmOpen_ = true;
        } else {
            ruleDraft_ = {};
            activeRuleId_.clear();
            page_ = Page::Rules;
        }
    };
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)
        && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        requestBack();
        return;
    }

    if (BeginUnwantedPanel("##unwanted_editor_header", ImVec2(0.0f, ScaleUi(58.0f)))) {
        if (UnwantedTextButton(
                ui_icons::ChevronLeft,
                ui.Text(UiText::UnwantedRules),
                "##unwanted_editor_back",
                ScaleUi(128.0f, 0.0f))) {
            requestBack();
        }
        ImGui::SameLine();
        const float titleOffset = ImGui::GetCursorPosX();
        ImGui::Text("%s", ui.Text(
            ruleDraft_.createMode ? UiText::UnwantedCreateRuleTitle : UiText::UnwantedEditRuleTitle));
        ImGui::SetCursorPosX(titleOffset);
        if (ruleDraft_.createMode) {
            ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedCreateRuleSubtitle));
        } else if (const Rule* editingRule = FindRuleById(ruleDraft_.id)) {
            ImGui::TextDisabled("%s", RuleDisplayTitle(*editingRule).c_str());
        }
    }
    EndUnwantedPanel();
    ImGui::Spacing();

    const float footerHeight = ScaleUi(68.0f);
    const float contentHeight = std::max(ScaleUi(120.0f), ImGui::GetContentRegionAvail().y - footerHeight);
    if (ImGui::BeginChild("##unwanted_editor_scroll", ImVec2(0.0f, contentHeight), ImGuiChildFlags_None)) {
        DrawRuleMessageStep();
        ImGui::Spacing();
        DrawRuleResultStep();
        ImGui::Spacing();
        ImGui::SeparatorText(ui.Text(UiText::UnwantedTester));
        DrawTesterPanel();
        ImGui::Spacing();
        DrawRuleAdvancedSettings();
    }
    ImGui::EndChild();
    DrawEditorFooter();
}

void UnwantedMessagesModule::DrawRuleMessageStep() {
    UiSettings& ui = UiSettings::Instance();
    ImGui::SeparatorText(ui.Text(UiText::UnwantedEditorMessageStep));
    ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedHelperFlowHint));
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool sampleChanged = InputTextWithHintString(
        "##unwanted_editor_sample",
        ui.Text(UiText::UnwantedHelperInputHint),
        helperSample_,
        ImGuiInputTextFlags_AutoSelectAll,
        1024);
    if (ImGui::Button(ui.Text(UiText::MessageHistoryOpen))) {
        rak_message_history_ui::RequestOpen(messageHistoryPicker_);
    }
    if (std::optional<std::string> selectedMessage = rak_message_history_ui::DrawPicker(
            sampRakHooks_,
            messageHistoryPicker_,
            "unwanted_editor_message_history")) {
        helperSample_ = std::move(*selectedMessage);
        sampleChanged = true;
    }
    if (sampleChanged) {
        ruleDraft_.sample = std::string(text_pattern_input::ExtractChatlogPayload(helperSample_).payload);
        if (ruleDraft_.name.empty() || ruleDraft_.nameAutoGenerated) {
            ruleDraft_.name = SuggestRuleName(ruleDraft_.sample);
            ruleDraft_.nameAutoGenerated = !ruleDraft_.name.empty();
        }
        RegenerateHelperOutput();
        if (CurrentRegexWorkspaceMode(helperWorkspaceMode_) == RegexWorkspaceMode::Automatic) {
            ApplyAutomaticHelperToDraft();
        }
        ruleDraft_.dirty = true;
        draftTesterCache_.ready = false;
    }
    const text_pattern_input::ChatlogSample prepared = text_pattern_input::ExtractChatlogPayload(helperSample_);
    if (prepared.timestampRemoved) {
        ImGui::TextDisabled("%s", ui.Text(UiText::TextPatternChatlogTimestampRemoved));
    }

    ImGui::Spacing();
    ImGui::SeparatorText(ui.Text(UiText::UnwantedEditorModeStep));
    const bool automatic = CurrentRegexWorkspaceMode(helperWorkspaceMode_) == RegexWorkspaceMode::Automatic;
    if (FilterChip(ui.Text(UiText::UnwantedEditorAutomatic), automatic)) {
        helperWorkspaceMode_ = static_cast<int>(RegexWorkspaceMode::Automatic);
        RegenerateHelperOutput();
        ApplyAutomaticHelperToDraft();
    }
    ImGui::SameLine();
    const bool manual = CurrentRegexWorkspaceMode(helperWorkspaceMode_) == RegexWorkspaceMode::Manual;
    if (FilterChip(ui.Text(UiText::UnwantedEditorManual), manual)) {
        helperWorkspaceMode_ = static_cast<int>(RegexWorkspaceMode::Manual);
    }
    if (!helperWarning_.empty()) {
        ImGui::TextColored(UnwantedStyleTokens().warn, "%s", helperWarning_.c_str());
    }

    if (manual) {
        if (helperConstructor_.preparedSample.empty()) {
            ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedHelperInputHint));
        } else {
            const text_pattern_constructor_ui::DrawResult result =
                text_pattern_constructor_ui::DrawInline(helperConstructor_, "unwanted_manual");
            if (result.applied) {
                ruleDraft_.type = RuleType::Regex;
                ruleDraft_.wholeWord = false;
                ruleDraft_.text = result.pattern;
                ruleDraft_.sample = std::string(text_pattern_input::ExtractChatlogPayload(helperSample_).payload);
                ruleDraft_.dirty = true;
                draftValidationCache_.ready = false;
                draftTesterCache_.ready = false;
            }
        }
    } else if (!helperTokens_.empty()) {
        ImGui::TextDisabled("%s", ui.Format(
            UiText::UnwantedDetectedTokensFormat,
            std::to_string(helperTokens_.size()).c_str()).c_str());
        for (const unwanted_regex_builder::Token& token : helperTokens_) {
            ImGui::PushID(static_cast<int>(token.offset));
            const UiText tokenLabel = text_pattern_ui::TokenLabel(token.kind);
            TextBadge(tokenLabel == UiText::Count ? "?" : ui.Text(tokenLabel), UnwantedStyleTokens().accent);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", token.source.c_str());
            ImGui::PopID();
        }
    } else if (!helperSample_.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternNoParts));
    }
}

void UnwantedMessagesModule::DrawRuleResultStep() {
    UiSettings& ui = UiSettings::Instance();
    const UnwantedVisualStyle visual = UnwantedStyleTokens();
    ImGui::SeparatorText(ui.Text(UiText::UnwantedEditorResultStep));

    std::string error;
    std::string warning;
    ValidateDraft(error, warning);
    const ImVec4 statusColor = !error.empty() ? visual.danger : !warning.empty() ? visual.warn : visual.ok;
    const char* status = !error.empty()
        ? error.c_str()
        : !warning.empty() ? warning.c_str() : ui.Text(UiText::UnwantedEditorResultReady);
    ImGui::TextColored(statusColor, "%s", status);
    if (!ruleDraft_.sample.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedEditorResultHint));
        ImGui::TextWrapped("%s", ruleDraft_.sample.c_str());
    }
    if (ImGui::CollapsingHeader(ui.Text(UiText::UnwantedEditorTechnicalPattern))) {
        ImGui::TextWrapped("%s", ruleDraft_.text.c_str());
        if (ImGui::Button(ui.Text(UiText::UnwantedCopy))) {
            ImGui::SetClipboardText(ruleDraft_.text.c_str());
        }
    }
}

void UnwantedMessagesModule::DrawRuleAdvancedSettings() {
    UiSettings& ui = UiSettings::Instance();
    if (!ImGui::CollapsingHeader(ui.Text(UiText::UnwantedEditorAdvancedSettings))) {
        return;
    }

    bool changed = false;
    ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedRuleName));
    ImGui::SetNextItemWidth(-1.0f);
    const bool nameChanged = InputTextWithHintString(
        "##unwanted_rule_name",
        ui.Text(UiText::UnwantedRuleNameHint),
        ruleDraft_.name,
        ImGuiInputTextFlags_AutoSelectAll,
        128);
    changed |= nameChanged;
    if (nameChanged) {
        ruleDraft_.nameAutoGenerated = false;
    }

    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedRuleEnabled), &ruleDraft_.enabled);
    ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedRuleType));
    const char* typePreview = ruleDraft_.type == RuleType::Regex ? ui.Text(UiText::UnwantedTypeRegex) : ui.Text(UiText::UnwantedTypeLiteral);
    ImGui::SetNextItemWidth(std::min(ScaleUi(220.0f), ImGui::GetContentRegionAvail().x));
    const bool typeComboOpen = ImGui::BeginCombo("##unwanted_draft_type", typePreview);
    DrawUnwantedTooltip(ui.Text(
        ruleDraft_.type == RuleType::Regex ? UiText::UnwantedTypeRegexHelp : UiText::UnwantedTypeLiteralHelp));
    if (typeComboOpen) {
        if (ImGui::Selectable(ui.Text(UiText::UnwantedTypeLiteral), ruleDraft_.type == RuleType::Literal)) {
            ruleDraft_.type = RuleType::Literal;
            changed = true;
        }
        if (ImGui::Selectable(ui.Text(UiText::UnwantedTypeRegex), ruleDraft_.type == RuleType::Regex)) {
            ruleDraft_.type = RuleType::Regex;
            ruleDraft_.wholeWord = false;
            changed = true;
        }
        ImGui::EndCombo();
    }

    const float controlsRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedNoCase), &ruleDraft_.nocase);
    DrawUnwantedTooltip(ui.Text(UiText::UnwantedNoCaseHelp));
    const float wholeWordWidth = ImGui::GetFrameHeight()
        + ImGui::GetStyle().ItemInnerSpacing.x
        + ImGui::CalcTextSize(ui.Text(UiText::UnwantedWholeWord)).x;
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + wholeWordWidth <= controlsRight) {
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(ruleDraft_.type != RuleType::Literal);
    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedWholeWord), &ruleDraft_.wholeWord);
    ImGui::EndDisabled();

    ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedRuleText));
    changed |= InputTextMultilineString(
        "##unwanted_draft_text",
        ruleDraft_.text,
        ImVec2(-1.0f, ScaleUi(92.0f)),
        ImGuiInputTextFlags_None,
        1024);
    DrawUnwantedTooltip(ui.Text(
        ruleDraft_.type == RuleType::Regex ? UiText::UnwantedTypeRegexHelp : UiText::UnwantedTypeLiteralHelp));
    if (changed) {
        ruleDraft_.dirty = true;
        draftValidationCache_.ready = false;
        draftTesterCache_.ready = false;
    }

    const std::string referenceTitle = std::string(ui.Text(UiText::UnwantedRegexReference)) + "###unwanted_editor_reference";
    if (ruleDraft_.type == RuleType::Regex && ImGui::CollapsingHeader(referenceTitle.c_str())) {
        DrawRegexReferencePanel();
    }
}

void UnwantedMessagesModule::DrawEditorFooter() {
    UiSettings& ui = UiSettings::Instance();
    const UnwantedVisualStyle visual = UnwantedStyleTokens();
    std::string error;
    std::string warning;
    ValidateDraft(error, warning);
    if (!BeginUnwantedPanel("##unwanted_editor_footer", ImVec2(0.0f, ScaleUi(60.0f)))) {
        EndUnwantedPanel();
        return;
    }

    if (!ruleDraft_.createMode && UnwantedDangerButton(
            ui.Text(UiText::Delete),
            "##unwanted_delete_rule",
            ImVec2(ScaleUi(132.0f), 0.0f))) {
        selectedRuleIds_.clear();
        selectedRuleIds_.insert(ruleDraft_.id);
        deleteSelectedConfirmOpen_ = true;
    }
    if (!ruleDraft_.createMode) {
        ImGui::SameLine();
    }
    ImGui::TextColored(
        !error.empty() ? visual.danger : !warning.empty() ? visual.warn : visual.ok,
        "%s",
        !error.empty() ? error.c_str() : !warning.empty() ? warning.c_str() : ui.Text(UiText::UnwantedRuleOk));

    const float buttonWidth = ScaleUi(132.0f);
    const float totalWidth = buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - totalWidth - ImGui::GetStyle().WindowPadding.x));
    if (ImGui::Button(ui.Text(UiText::Cancel), ImVec2(buttonWidth, 0.0f))) {
        if (ruleDraft_.dirty) {
            pendingPageAfterDiscard_ = Page::Rules;
            unsavedConfirmOpen_ = true;
        } else {
            ruleDraft_ = {};
            activeRuleId_.clear();
            page_ = Page::Rules;
        }
    }
    ImGui::SameLine();
    const bool saveShortcut = (ImGui::GetIO().KeyMods & ImGuiMod_Ctrl) != 0
        && ImGui::IsKeyPressed(ImGuiKey_S, false);
    ImGui::BeginDisabled(!error.empty());
    if (UnwantedPrimaryButton(
            ui_icons::Check,
            ui.Text(UiText::Save),
            "##unwanted_save_rule",
            ImVec2(buttonWidth, 0.0f)) || saveShortcut) {
        SaveDraftRule();
    }
    ImGui::EndDisabled();
    EndUnwantedPanel();
}

void UnwantedMessagesModule::DrawTesterPanel() {
    UiSettings& ui = UiSettings::Instance();
    const UnwantedVisualStyle visual = UnwantedStyleTokens();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (InputTextWithHintString(
        "##unwanted_test_text",
        ui.Text(UiText::UnwantedTesterHint),
        testText_,
        ImGuiInputTextFlags_AutoSelectAll,
        1024)) {
        draftTesterCache_.ready = false;
    }
    RefreshDraftTester();
    if (text_pattern_input::ExtractChatlogPayload(testText_).timestampRemoved) {
        ImGui::TextDisabled("%s", ui.Text(UiText::TextPatternChatlogTimestampRemoved));
    }
    if (!draftTesterCache_.normalized.empty()) {
        ImGui::TextDisabled("%s", ui.Format(
            UiText::UnwantedTesterNormalizedFormat,
            std::to_string(Utf8CharacterOffset(
                draftTesterCache_.normalized,
                draftTesterCache_.normalized.size())).c_str(),
            draftTesterCache_.normalized.c_str()).c_str());
    }
    switch (draftTesterCache_.result) {
    case TesterResult::Match:
        ImGui::TextColored(visual.ok, "%s", ui.Text(UiText::UnwantedTesterMatched));
        break;
    case TesterResult::NoMatch:
        ImGui::TextColored(visual.warn, "%s", ui.Text(UiText::UnwantedTesterNoMatch));
        break;
    case TesterResult::RuleError:
        ImGui::TextColored(visual.danger, "%s", ui.Text(UiText::UnwantedTesterRuleError));
        break;
    case TesterResult::Neutral:
    default:
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedTesterNeutral));
        break;
    }
}

void UnwantedMessagesModule::RefreshDraftTester() {
    const bool sameNormalizer = draftTesterCache_.normalizer.stripColors == settings_.normalizer.stripColors
        && draftTesterCache_.normalizer.collapseWhitespace == settings_.normalizer.collapseWhitespace
        && draftTesterCache_.normalizer.trim == settings_.normalizer.trim;
    if (draftTesterCache_.ready
        && draftTesterCache_.testText == testText_
        && draftTesterCache_.pattern == ruleDraft_.text
        && draftTesterCache_.type == ruleDraft_.type
        && draftTesterCache_.nocase == ruleDraft_.nocase
        && draftTesterCache_.wholeWord == ruleDraft_.wholeWord
        && sameNormalizer) {
        return;
    }

    draftTesterCache_ = {};
    draftTesterCache_.ready = true;
    draftTesterCache_.testText = testText_;
    draftTesterCache_.pattern = ruleDraft_.text;
    draftTesterCache_.type = ruleDraft_.type;
    draftTesterCache_.nocase = ruleDraft_.nocase;
    draftTesterCache_.wholeWord = ruleDraft_.wholeWord;
    draftTesterCache_.normalizer = settings_.normalizer;
    if (testText_.empty()) {
        draftTesterCache_.result = TesterResult::Neutral;
        return;
    }

    std::string error;
    std::string warning;
    if (!ValidateDraft(error, warning)) {
        draftTesterCache_.result = TesterResult::RuleError;
        return;
    }

    const text_pattern_input::ChatlogSample chatlogSample = text_pattern_input::ExtractChatlogPayload(testText_);
    UnwantedMessageContext context;
    context.text.assign(chatlogSample.payload);
    const std::vector<std::string> candidates = BuildCandidates(context, settings_);
    if (!candidates.empty()) {
        draftTesterCache_.normalized = candidates.front();
    }

    bool matched = false;
    if (ruleDraft_.type == RuleType::Literal) {
        PreparedRule temporary;
        temporary.type = RuleType::Literal;
        temporary.text = TrimAscii(ruleDraft_.text);
        temporary.nocase = ruleDraft_.nocase;
        temporary.wholeWord = ruleDraft_.wholeWord;
        temporary.literalNeedle = temporary.nocase ? Utf8ToLower(temporary.text) : temporary.text;
        for (const std::string& candidate : candidates) {
            const std::string folded = temporary.nocase ? Utf8ToLower(candidate) : std::string{};
            if (MatchLiteral(temporary, candidate, folded)) {
                matched = true;
                break;
            }
        }
    } else {
        unwanted_regex::CompileResult compiled = unwanted_regex::Compile(
            TrimAscii(ruleDraft_.text), ruleDraft_.nocase);
        if (!compiled.program) {
            draftTesterCache_.result = TesterResult::RuleError;
            return;
        }
        for (const std::string& candidate : candidates) {
            if (compiled.program->Match(candidate).status == unwanted_regex::MatchStatus::Match) {
                matched = true;
                break;
            }
        }
    }
    draftTesterCache_.result = matched ? TesterResult::Match : TesterResult::NoMatch;
}

void UnwantedMessagesModule::ApplyAutomaticHelperToDraft() {
    if (!helperGeneralizedValid_) {
        return;
    }
    const bool changed = ruleDraft_.type != RuleType::Regex
        || ruleDraft_.wholeWord
        || ruleDraft_.text != helperGeneralized_
        || ruleDraft_.sample != text_pattern_input::ExtractChatlogPayload(helperSample_).payload;
    ruleDraft_.type = RuleType::Regex;
    ruleDraft_.wholeWord = false;
    ruleDraft_.text = helperGeneralized_;
    ruleDraft_.sample = std::string(text_pattern_input::ExtractChatlogPayload(helperSample_).payload);
    if (changed) {
        ruleDraft_.dirty = true;
        draftValidationCache_.ready = false;
        draftTesterCache_.ready = false;
    }
}
void UnwantedMessagesModule::RefreshRegexReferenceFilter() {
    UiSettings& ui = UiSettings::Instance();
    const int language = static_cast<int>(ui.Language());
    const std::string search = Utf8ToLower(TrimAscii(regexReferenceSearch_));
    if (regexReferenceFilterLanguage_ == language
        && regexReferenceFilterSearch_ == search) {
        return;
    }

    regexReferenceFilterLanguage_ = language;
    regexReferenceFilterSearch_ = search;
    regexReferenceVisibleItems_.clear();
    const std::span<const text_pattern_ui::ReferenceItem> items = text_pattern_ui::ReferenceItems();
    regexReferenceVisibleItems_.reserve(items.size());
    for (std::size_t itemIndex = 0; itemIndex < items.size(); ++itemIndex) {
        if (regexReferenceFilterSearch_.empty()) {
            regexReferenceVisibleItems_.push_back(itemIndex);
            continue;
        }
        const text_pattern_ui::ReferenceItem& item = items[itemIndex];
        const std::string searchable = std::string(item.expression)
            + "\n" + ui.Text(item.category)
            + "\n" + ui.Text(item.description);
        if (Utf8ToLower(searchable).find(regexReferenceFilterSearch_) != std::string::npos) {
            regexReferenceVisibleItems_.push_back(itemIndex);
        }
    }
}

void UnwantedMessagesModule::DrawRegexReferencePanel() {
    UiSettings& ui = UiSettings::Instance();
    const UnwantedVisualStyle visual = UnwantedStyleTokens();

    ImGui::TextWrapped("%s", ui.Text(UiText::UnwantedRegexReferenceHint));
    const std::string searchHint = std::string(ui_icons::Search) + " " + ui.Text(UiText::UnwantedRegexReferenceSearch);
    ImGui::SetNextItemWidth(-1.0f);
    InputTextWithHintString(
        "##unwanted_regex_reference_search",
        searchHint.c_str(),
        regexReferenceSearch_,
        ImGuiInputTextFlags_AutoSelectAll,
        256);
    RefreshRegexReferenceFilter();
    const std::span<const text_pattern_ui::ReferenceItem> items = text_pattern_ui::ReferenceItems();
    const bool anyVisible = !regexReferenceVisibleItems_.empty();
    const auto appendItem = [&](const text_pattern_ui::ReferenceItem& item) {
        ruleDraft_.type = RuleType::Regex;
        ruleDraft_.wholeWord = false;
        ruleDraft_.text += item.expression;
        ruleDraft_.dirty = true;
        draftTesterCache_.ready = false;
    };

    const bool compactReference = ImGui::GetContentRegionAvail().x < ScaleUi(760.0f);
    if (compactReference) {
        if (ImGui::BeginChild("##unwanted_regex_reference_cards", ImVec2(0.0f, ScaleUi(320.0f)), ImGuiChildFlags_Borders)) {
            for (const std::size_t itemIndex : regexReferenceVisibleItems_) {
                const text_pattern_ui::ReferenceItem& item = items[itemIndex];
                ImGui::PushID(item.expression);
                TextBadge(ui.Text(item.category), visual.mutedText);
                ImGui::SameLine();
                ImGui::TextWrapped("%s", item.expression);
                ImGui::PushStyleColor(ImGuiCol_Text, visual.mutedText);
                ImGui::TextWrapped("%s", ui.Text(item.description));
                ImGui::PopStyleColor();
                if (ImGui::Button(ui.Text(UiText::UnwantedCopy), ScaleUi(112.0f, 0.0f))) {
                    ImGui::SetClipboardText(item.expression);
                }
                ImGui::SameLine();
                if (UnwantedPrimaryButton(
                        ui_icons::Plus,
                        ui.Text(UiText::UnwantedRegexReferenceAppend),
                        "##append_reference_item",
                        ScaleUi(128.0f, 0.0f))) {
                    appendItem(item);
                }
                ImGui::Separator();
                ImGui::PopID();
            }
            if (!anyVisible) {
                ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedRegexReferenceNoResults));
            }
        }
        ImGui::EndChild();
    } else {
        const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp
            | ImGuiTableFlags_RowBg
            | ImGuiTableFlags_BordersInnerH
            | ImGuiTableFlags_BordersInnerV
            | ImGuiTableFlags_Resizable
            | ImGuiTableFlags_ScrollY
            | ImGuiTableFlags_NoSavedSettings;
        if (ImGui::BeginTable("##unwanted_regex_reference_table", 5, flags, ImVec2(0.0f, ScaleUi(320.0f)))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(ui.Text(UiText::UnwantedRegexReferenceCategory), ImGuiTableColumnFlags_WidthFixed, ScaleUi(105.0f));
            ImGui::TableSetupColumn(ui.Text(UiText::UnwantedRegexReferenceExpression), ImGuiTableColumnFlags_WidthStretch, 0.85f);
            ImGui::TableSetupColumn(ui.Text(UiText::UnwantedRegexReferenceDescription), ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableSetupColumn(ui.Text(UiText::UnwantedCopy), ImGuiTableColumnFlags_WidthFixed, ScaleUi(92.0f));
            ImGui::TableSetupColumn(ui.Text(UiText::UnwantedRegexReferenceAppend), ImGuiTableColumnFlags_WidthFixed, ScaleUi(105.0f));
            ImGui::TableHeadersRow();

            for (const std::size_t itemIndex : regexReferenceVisibleItems_) {
                const text_pattern_ui::ReferenceItem& item = items[itemIndex];
                ImGui::PushID(item.expression);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", ui.Text(item.category));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", item.expression);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", ui.Text(item.description));
                ImGui::TableSetColumnIndex(3);
                if (ImGui::Button(ui.Text(UiText::UnwantedCopy), ImVec2(-1.0f, 0.0f))) {
                    ImGui::SetClipboardText(item.expression);
                }
                ImGui::TableSetColumnIndex(4);
                if (ImGui::Button(ui.Text(UiText::UnwantedRegexReferenceAppend), ImVec2(-1.0f, 0.0f))) {
                    appendItem(item);
                }
                ImGui::PopID();
            }
            if (!anyVisible) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedRegexReferenceNoResults));
            }
            ImGui::EndTable();
        }
    }

}

void UnwantedMessagesModule::DrawUnsavedConfirmPopup() {
    UiSettings& ui = UiSettings::Instance();
    const std::string title = std::string(ui.Text(UiText::UnwantedUnsavedTitle)) + "###unwanted_unsaved_confirm";
    if (unsavedConfirmOpen_) {
        unsavedConfirmOpen_ = false;
        ImGui::OpenPopup(title.c_str());
    }
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextWrapped("%s", ui.Text(UiText::UnwantedUnsavedDesc));
    std::string error;
    std::string warning;
    ValidateDraft(error, warning);
    ImGui::BeginDisabled(!error.empty());
    if (ImGui::Button(ui.Text(UiText::UnwantedSaveChanges), ScaleUi(150.0f, 0.0f))) {
        const bool reloadAfterSave = reloadAfterDiscard_;
        reloadAfterDiscard_ = false;
        if (SaveDraftRule() && reloadAfterSave) {
            reloadRequested_ = true;
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::UnwantedDiscard), ScaleUi(150.0f, 0.0f))) {
        ruleDraft_ = {};
        activeRuleId_.clear();
        page_ = pendingPageAfterDiscard_;
        if (reloadAfterDiscard_) {
            reloadAfterDiscard_ = false;
            reloadRequested_ = true;
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::UnwantedContinueEditing), ScaleUi(190.0f, 0.0f))) {
        reloadAfterDiscard_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void UnwantedMessagesModule::DrawSettingsPopup() {
    UiSettings& ui = UiSettings::Instance();
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(ScaleUi(360.0f), 0.0f),
        ScaleUi(500.0f, 560.0f));
    if (!ImGui::BeginPopup("##unwanted_settings_popup")) {
        return;
    }

    bool changed = false;
    ImGui::SeparatorText(ui.Text(UiText::UnwantedSettingsBasic));
    ImGui::TextWrapped("%s", ui.Text(UiText::UnwantedNormalizerDesc));
    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedStripColors), &settings_.normalizer.stripColors);
    DrawUnwantedTooltip(ui.Text(UiText::UnwantedStripColorsHelp));
    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedCollapseWhitespace), &settings_.normalizer.collapseWhitespace);
    DrawUnwantedTooltip(ui.Text(UiText::UnwantedCollapseWhitespaceHelp));
    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedTrim), &settings_.normalizer.trim);
    DrawUnwantedTooltip(ui.Text(UiText::UnwantedTrimHelp));
    ImGui::TextDisabled("%s", ui.Text(
        ImGui::GetTime() < settingsSavedUntil_
            ? UiText::UnwantedSettingsSavedNow
            : UiText::UnwantedSettingsSaved));

    const bool previousChatAsiCompatibility = settings_.chatAsiCompatibility;
    const int previousMaxPatternLength = settings_.maxPatternLength;
    if (ImGui::CollapsingHeader(ui.Text(UiText::UnwantedSettingsDiagnostics))) {
        const bool chatAsiLoaded = GetModuleHandleA("_chat.asi") != nullptr;
        const char* chatAsiStatus = !chatAsiLoaded
            ? ui.Text(UiText::UnwantedChatAsiStandard)
            : settings_.chatAsiCompatibility
                ? ui.Text(UiText::UnwantedChatAsiDetected)
                : ui.Text(UiText::UnwantedChatAsiDisabled);
        ImGui::TextDisabled("%s", ui.Format(UiText::UnwantedChatAsiStatus, chatAsiStatus).c_str());
        changed |= ImGui::Checkbox(
            ui.Text(UiText::UnwantedChatAsiCompatibility),
            &settings_.chatAsiCompatibility);
        DrawUnwantedTooltip(ui.Text(UiText::UnwantedChatAsiCompatibilityHelp));
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedMaxPatternLength));
        ImGui::SetNextItemWidth(-1.0f);
        changed |= ImGui::InputInt("##unwanted_max_pattern_length", &settings_.maxPatternLength, 0, 0);
        settings_.maxPatternLength = std::clamp(settings_.maxPatternLength, 1, kMaxConfigPatternLength);

        if (ImGui::Button(ui.Text(UiText::UnwantedReload), ImVec2(-1.0f, 0.0f))) {
            if (ruleDraft_.active && ruleDraft_.dirty) {
                pendingPageAfterDiscard_ = page_;
                reloadAfterDiscard_ = true;
                unsavedConfirmOpen_ = true;
            } else {
                reloadRequested_ = true;
            }
        }
        if (ImGui::Button(ui.Text(UiText::UnwantedSettingsReset), ImVec2(-1.0f, 0.0f))) {
            const bool enabled = settings_.enabled;
            settings_ = Settings{};
            settings_.enabled = enabled;
            changed = true;
        }
    }
    const bool chatAsiCompatibilityChanged =
        previousChatAsiCompatibility != settings_.chatAsiCompatibility;
    const bool patternLimitChanged = previousMaxPatternLength != settings_.maxPatternLength;

    if (changed) {
        if (chatAsiCompatibilityChanged) {
            ApplyChatAsiCompatibilitySetting();
        }
        if (patternLimitChanged) {
            CompileRules();
        }
        PublishRuntimeSnapshot();
        RegenerateHelperOutput();
        draftTesterCache_.ready = false;
        SaveSettings();
        settingsSavedUntil_ = ImGui::GetTime() + 1.0;
    }
    ImGui::EndPopup();
}

void UnwantedMessagesModule::DrawDeleteConfirmPopup() {
    UiSettings& ui = UiSettings::Instance();
    const std::string popupTitle = std::string(ui.Text(UiText::UnwantedDeleteSelected)) + "###unwanted_delete_selected_confirm";
    if (deleteSelectedConfirmOpen_) {
        deleteSelectedConfirmOpen_ = false;
        ImGui::OpenPopup(popupTitle.c_str());
    }

    if (!ImGui::BeginPopupModal(popupTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const Rule* singleRule = selectedRuleIds_.size() == 1
        ? FindRuleById(*selectedRuleIds_.begin())
        : nullptr;
    if (singleRule) {
        ImGui::TextWrapped(
            "%s",
            ui.Format(UiText::UnwantedDeleteRuleQuestion, RuleDisplayTitle(*singleRule).c_str()).c_str());
        ImGui::TextDisabled(
            "%s",
            ui.Format(
                UiText::UnwantedDeleteRuleSummary,
                (singleRule->sample.empty() ? singleRule->text : singleRule->sample).c_str()).c_str());
    } else {
        ImGui::TextWrapped(
            "%s",
            ui.Format(UiText::UnwantedDeleteSelectedQuestion, std::to_string(selectedRuleIds_.size()).c_str()).c_str());
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedDeleteSelectionScope));
    }
    const std::string deleteLabel = singleRule
        ? ui.Text(UiText::Delete)
        : ui.Format(UiText::UnwantedDeleteRulesAction, std::to_string(selectedRuleIds_.size()).c_str());
    if (UnwantedDangerButton(deleteLabel.c_str(), "##unwanted_confirm_delete", ScaleUi(150.0f, 0.0f))) {
        DeleteSelectedRules();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::Cancel), ScaleUi(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

std::string UnwantedMessagesModule::AddRule(
    RuleType type,
    std::string text,
    bool enabled,
    bool nocase,
    bool wholeWord,
    std::string name,
    std::string sample) {
    if (text.empty()) {
        return {};
    }

    Rule rule;
    rule.id = AllocateRuleId();
    rule.name = TrimAscii(name);
    rule.sample = std::move(sample);
    const std::string id = rule.id;
    rule.enabled = enabled;
    rule.type = type;
    rule.rawType = RuleTypeName(type);
    rule.text = std::move(text);
    rule.nocase = nocase;
    rule.wholeWord = type == RuleType::Literal && wholeWord;
    CompileRule(rule);
    rules_.push_back(std::move(rule));
    RebuildRuleViewCache();
    PublishRuntimeSnapshot();
    SaveConfig();
    return id;
}

void UnwantedMessagesModule::DeleteSelectedRules() {
    if (selectedRuleIds_.empty()) {
        return;
    }

    const bool deletesDraft = !ruleDraft_.id.empty()
        && selectedRuleIds_.find(ruleDraft_.id) != selectedRuleIds_.end();

    rules_.erase(
        std::remove_if(rules_.begin(), rules_.end(), [this](const Rule& rule) {
            return selectedRuleIds_.find(rule.id) != selectedRuleIds_.end();
        }),
        rules_.end());
    if (selectedRuleIds_.find(activeRuleId_) != selectedRuleIds_.end() || deletesDraft) {
        activeRuleId_.clear();
        ruleDraft_ = {};
        if (page_ == Page::Editor) {
            page_ = Page::Rules;
        }
    }
    selectedRuleIds_.clear();
    RebuildRuleViewCache();
    PublishRuntimeSnapshot();
    SaveConfig();
}

void UnwantedMessagesModule::SetSelectedRulesEnabled(bool enabled) {
    if (selectedRuleIds_.empty()) {
        return;
    }

    for (Rule& rule : rules_) {
        if (selectedRuleIds_.find(rule.id) != selectedRuleIds_.end()) {
            rule.enabled = enabled;
        }
    }
    PublishRuntimeSnapshot();
    SaveConfig();
}

bool UnwantedMessagesModule::IsRuleSelected(std::string_view id) const {
    return selectedRuleIds_.find(std::string(id)) != selectedRuleIds_.end();
}

void UnwantedMessagesModule::SetRuleSelected(std::string_view id, bool selected) {
    if (selected) {
        selectedRuleIds_.insert(std::string(id));
    } else {
        selectedRuleIds_.erase(std::string(id));
    }
}

void UnwantedMessagesModule::ClearSelection() {
    selectedRuleIds_.clear();
}

void UnwantedMessagesModule::SelectVisibleRules(
    const std::vector<std::size_t>& visibleIndices,
    bool selected) {
    if (!selected) {
        selectedRuleIds_.clear();
        return;
    }
    selectedRuleIds_.clear();
    for (const std::size_t index : visibleIndices) {
        if (index < rules_.size()) {
            selectedRuleIds_.insert(rules_[index].id);
        }
    }
}

void UnwantedMessagesModule::RetainVisibleSelection(const std::vector<std::size_t>& visibleIndices) {
    std::set<std::string, std::less<>> visibleIds;
    for (const std::size_t index : visibleIndices) {
        if (index < rules_.size()) {
            visibleIds.insert(rules_[index].id);
        }
    }
    std::erase_if(selectedRuleIds_, [&](const std::string& id) {
        return visibleIds.find(id) == visibleIds.end();
    });
}

std::string UnwantedMessagesModule::RuleDisplayTitle(const Rule& rule) const {
    if (!rule.name.empty()) {
        return rule.name;
    }
    if (!rule.sample.empty()) {
        return Utf8Preview(rule.sample, 48);
    }
    if (!rule.exactDisplayText.empty()) {
        return Utf8Preview(rule.exactDisplayText, 48);
    }
    if (!rule.text.empty()) {
        return Utf8Preview(rule.text, 48);
    }
    return rule.id;
}

std::string UnwantedMessagesModule::RuleDisplaySummary(const Rule& rule) const {
    UiSettings& ui = UiSettings::Instance();
    if (!rule.error.empty()) {
        return rule.error;
    }
    if (!rule.sample.empty()) {
        return ui.Format(UiText::UnwantedRuleSampleSummary, rule.sample.c_str());
    }
    if (rule.type == RuleType::Literal) {
        return ui.Format(UiText::UnwantedRuleLiteralSummary, rule.text.c_str());
    }
    if (!rule.exactDisplayText.empty()) {
        return ui.Format(UiText::UnwantedRuleExactSummary, rule.exactDisplayText.c_str());
    }
    return ui.Format(UiText::UnwantedRulePatternFallback, rule.text.c_str());
}

void UnwantedMessagesModule::DuplicateRule(std::string_view id) {
    const Rule* source = FindRuleById(id);
    if (!source) {
        return;
    }
    Rule copy;
    copy.id = AllocateRuleId();
    copy.name = source->name;
    copy.sample = source->sample;
    copy.enabled = source->enabled;
    copy.type = source->type;
    copy.text = source->text;
    copy.nocase = source->nocase;
    copy.wholeWord = source->wholeWord;
    copy.rawType = source->rawType;
    copy.invalidType = source->invalidType;
    CompileRule(copy);
    const int sourceIndex = FindRuleIndexById(id);
    const std::size_t insertAt = sourceIndex < 0
        ? rules_.size()
        : static_cast<std::size_t>(sourceIndex) + 1;
    const std::string copiedId = copy.id;
    rules_.insert(rules_.begin() + static_cast<std::ptrdiff_t>(insertAt), std::move(copy));
    RebuildRuleViewCache();
    PublishRuntimeSnapshot();
    SaveConfig();
    scrollToRuleId_ = copiedId;
}

int UnwantedMessagesModule::FindRuleIndexById(std::string_view id) const {
    for (std::size_t i = 0; i < rules_.size(); ++i) {
        if (rules_[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

UnwantedMessagesModule::Rule* UnwantedMessagesModule::FindRuleById(std::string_view id) {
    const int index = FindRuleIndexById(id);
    return index >= 0 ? &rules_[static_cast<std::size_t>(index)] : nullptr;
}

const UnwantedMessagesModule::Rule* UnwantedMessagesModule::FindRuleById(std::string_view id) const {
    const int index = FindRuleIndexById(id);
    return index >= 0 ? &rules_[static_cast<std::size_t>(index)] : nullptr;
}

void UnwantedMessagesModule::StartCreateRule(std::string sample, RuleType type) {
    activeRuleId_.clear();
    helperSample_ = sample;
    helperWorkspaceMode_ = static_cast<int>(RegexWorkspaceMode::Automatic);
    helperGeneralized_.clear();
    helperTokens_.clear();
    helperConstructor_ = {};
    helperWarning_.clear();
    helperGeneralizedValid_ = false;
    ruleDraft_ = {};
    ruleDraft_.active = true;
    ruleDraft_.createMode = true;
    ruleDraft_.sample = std::string(text_pattern_input::ExtractChatlogPayload(sample).payload);
    ruleDraft_.name = SuggestRuleName(ruleDraft_.sample);
    ruleDraft_.nameAutoGenerated = !ruleDraft_.name.empty();
    ruleDraft_.enabled = true;
    ruleDraft_.type = type;
    ruleDraft_.wholeWord = false;
    ruleDraft_.dirty = !ruleDraft_.sample.empty();
    if (!ruleDraft_.sample.empty()) {
        RegenerateHelperOutput();
        if (CurrentRegexWorkspaceMode(helperWorkspaceMode_) == RegexWorkspaceMode::Automatic) {
            ApplyAutomaticHelperToDraft();
        }
    }
    draftValidationCache_.ready = false;
}

void UnwantedMessagesModule::StartEditRule(std::string_view id) {
    const Rule* rule = FindRuleById(id);
    if (!rule) {
        return;
    }

    activeRuleId_ = rule->id;
    ruleDraft_ = {};
    ruleDraft_.active = true;
    ruleDraft_.createMode = false;
    ruleDraft_.id = rule->id;
    ruleDraft_.name = rule->name;
    ruleDraft_.sample = rule->sample;
    ruleDraft_.enabled = rule->enabled;
    ruleDraft_.type = rule->type;
    ruleDraft_.text = rule->text;
    ruleDraft_.nocase = rule->nocase;
    ruleDraft_.wholeWord = rule->wholeWord;
    ruleDraft_.dirty = false;
    helperSample_ = rule->sample;
    helperWorkspaceMode_ = static_cast<int>(RegexWorkspaceMode::Automatic);
    helperConstructor_ = {};
    RegenerateHelperOutput();
    draftValidationCache_.ready = false;
    draftTesterCache_ = {};
}

bool UnwantedMessagesModule::SaveDraftRule() {
    if (!ruleDraft_.active) {
        return false;
    }

    std::string text = TrimAscii(ruleDraft_.text);
    if (text.empty()) {
        return false;
    }

    std::string validationError;
    std::string validationWarning;
    if (!ValidateDraft(validationError, validationWarning)) {
        return false;
    }

    if (ruleDraft_.createMode) {
        const std::string id = AddRule(
            ruleDraft_.type,
            std::move(text),
            ruleDraft_.enabled,
            ruleDraft_.nocase,
            ruleDraft_.wholeWord,
            ruleDraft_.name,
            ruleDraft_.sample);
        if (id.empty()) {
            return false;
        }
        activeRuleId_ = id;
        scrollToRuleId_ = id;
        const Rule* createdRule = FindRuleById(id);
        const std::string loweredSearch = Utf8ToLower(TrimAscii(ruleSearch_));
        if (!createdRule || !RuleMatchesFilter(*createdRule, loweredSearch)) {
            ruleSearch_.clear();
            ruleFilter_ = RuleFilter::All;
            ruleTypeFilter_ = RuleTypeFilter::All;
        }
        ruleDraft_ = {};
        page_ = Page::Rules;
        return true;
    }

    Rule* rule = FindRuleById(ruleDraft_.id);
    if (!rule) {
        return false;
    }

    rule->enabled = ruleDraft_.enabled;
    rule->name = TrimAscii(ruleDraft_.name);
    rule->sample = ruleDraft_.sample;
    rule->type = ruleDraft_.type;
    rule->rawType = RuleTypeName(ruleDraft_.type);
    rule->invalidType = false;
    rule->text = std::move(text);
    rule->nocase = ruleDraft_.nocase;
    rule->wholeWord = ruleDraft_.type == RuleType::Literal && ruleDraft_.wholeWord;
    CompileRule(*rule);
    RebuildRuleViewCache();
    PublishRuntimeSnapshot();
    SaveConfig();
    scrollToRuleId_ = rule->id;
    const std::string loweredSearch = Utf8ToLower(TrimAscii(ruleSearch_));
    if (!RuleMatchesFilter(*rule, loweredSearch)) {
        ruleSearch_.clear();
        ruleFilter_ = RuleFilter::All;
        ruleTypeFilter_ = RuleTypeFilter::All;
    }
    ruleDraft_ = {};
    page_ = Page::Rules;
    return true;
}

bool UnwantedMessagesModule::ValidateDraft(std::string& error, std::string& warning) const {
    const std::string text = TrimAscii(ruleDraft_.text);
    UiSettings& ui = UiSettings::Instance();
    const int language = static_cast<int>(ui.Language());
    if (draftValidationCache_.ready
        && draftValidationCache_.text == text
        && draftValidationCache_.type == ruleDraft_.type
        && draftValidationCache_.nocase == ruleDraft_.nocase
        && draftValidationCache_.maxPatternLength == settings_.maxPatternLength
        && draftValidationCache_.language == language) {
        error = draftValidationCache_.error;
        warning = draftValidationCache_.warning;
        return draftValidationCache_.valid;
    }

    draftValidationCache_ = {};
    draftValidationCache_.ready = true;
    draftValidationCache_.text = text;
    draftValidationCache_.type = ruleDraft_.type;
    draftValidationCache_.nocase = ruleDraft_.nocase;
    draftValidationCache_.maxPatternLength = settings_.maxPatternLength;
    draftValidationCache_.language = language;
    error.clear();
    warning.clear();
    if (text.empty()) {
        error = ui.Text(UiText::UnwantedErrorEmpty);
    } else if (static_cast<int>(text.size()) > settings_.maxPatternLength) {
        error = ui.Format(UiText::UnwantedErrorTooLong, std::to_string(settings_.maxPatternLength).c_str());
    } else if (ruleDraft_.type == RuleType::Regex) {
        const bool legacyAnchored = text.size() >= 2 && text.front() == '^' && text.back() == '$';
        const bool absoluteAnchored = text.size() >= 4 && text.rfind("\\A", 0) == 0 && text.substr(text.size() - 2) == "\\z";
        if (!legacyAnchored && !absoluteAnchored) {
            warning = ui.Text(UiText::UnwantedRegexSafetyUnanchored);
        }
        if (ContainsBroadWildcard(text)) {
            AppendWarning(warning, ui.Text(UiText::UnwantedRegexBroadWildcard));
        }
        unwanted_regex::CompileResult compiled = unwanted_regex::Compile(text, ruleDraft_.nocase);
        if (!compiled.program) {
            error = ui.Format(
                UiText::UnwantedPcreErrorFormat,
                FormatCompilePosition(text, compiled.errorOffset).c_str());
        } else if (compiled.program->MatchesEmpty()) {
            AppendWarning(warning, ui.Text(UiText::UnwantedRegexMatchesEmpty));
        }
    }
    draftValidationCache_.error = error;
    draftValidationCache_.warning = warning;
    draftValidationCache_.valid = error.empty();
    return draftValidationCache_.valid;
}

bool UnwantedMessagesModule::IsDuplicateRule(std::size_t index) const {
    if (index >= rules_.size()) {
        return false;
    }
    return rules_[index].duplicate;
}

bool UnwantedMessagesModule::RuleMatchesFilter(const Rule& rule, std::string_view loweredSearch) const {
    switch (ruleFilter_) {
    case RuleFilter::Enabled:
        if (!rule.enabled) {
            return false;
        }
        break;
    case RuleFilter::Disabled:
        if (rule.enabled) {
            return false;
        }
        break;
    case RuleFilter::Problems:
        if (!RuleHasProblem(rule)) {
            return false;
        }
        break;
    case RuleFilter::All:
    default:
        break;
    }

    if (ruleTypeFilter_ == RuleTypeFilter::Literal && rule.type != RuleType::Literal) {
        return false;
    }
    if (ruleTypeFilter_ == RuleTypeFilter::Regex && rule.type != RuleType::Regex) {
        return false;
    }

    if (loweredSearch.empty()) {
        return true;
    }

    if (rule.searchBlobLower.find(loweredSearch) != std::string::npos) {
        return true;
    }
    const auto runtimeWarning = runtimeWarningsView_.find(rule.id);
    return runtimeWarning != runtimeWarningsView_.end()
        && Utf8ToLower(runtimeWarning->second).find(loweredSearch) != std::string::npos;
}

void UnwantedMessagesModule::RegenerateHelperOutput() {
    unwanted_regex_builder::Options options;
    options.colors = !settings_.normalizer.stripColors;
    const text_pattern_input::ChatlogSample chatlogSample = text_pattern_input::ExtractChatlogPayload(helperSample_);
    const std::string normalized = NormalizeCandidate(chatlogSample.payload);
    text_pattern_constructor_ui::SetPreparedSample(helperConstructor_, normalized);
    const unwanted_regex_builder::Result built = unwanted_regex_builder::Build(normalized, options);
    helperGeneralized_ = built.recommended;
    helperTokens_ = built.tokens;
    helperWarning_.clear();
    helperGeneralizedValid_ = false;
    UiSettings& ui = UiSettings::Instance();
    if (!built.error.empty()) {
        helperWarning_ = ui.Text(UiText::UnwantedHelperInvalidUtf8);
        helperGeneralized_.clear();
        helperTokens_.clear();
        return;
    }

    const auto matchesSource = [&](const std::string& pattern) {
        if (pattern.empty()) {
            return false;
        }
        unwanted_regex::CompileResult compiled = unwanted_regex::Compile(pattern, false);
        return compiled.program
            && compiled.program->Match(normalized).status == unwanted_regex::MatchStatus::Match;
    };
    const bool exactValid = matchesSource(built.exact);
    helperGeneralizedValid_ = matchesSource(helperGeneralized_);
    if (!helperGeneralizedValid_ && exactValid) {
        helperGeneralized_ = built.exact;
        helperGeneralizedValid_ = true;
        helperWarning_ = ui.Text(UiText::UnwantedHelperExactFallback);
    }
}
