#include "unwanted_messages_module.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "app_config.h"
#include "debug_log.h"
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
#include <limits>
#include <sstream>
#include <utility>

namespace {

constexpr std::string_view kUnwantedSectionName = "unwanted";
constexpr int kUnwantedSchemaVersion = 2;
constexpr int kMaxConfigPatternLength = 65535;
constexpr std::uint64_t kPerfTelemetryWindowMs = 5000;

struct RegexReferenceItem {
    UiText category;
    const char* expression;
    UiText description;
};

constexpr RegexReferenceItem kRegexReferenceItems[] = {
    {UiText::UnwantedRegexRefCategoryReady, R"(\A\z)", UiText::UnwantedRegexRefEmptyMessage},
    {UiText::UnwantedRegexRefCategoryReady, R"(\A[ ]+\z)", UiText::UnwantedRegexRefSpacesOnly},
    {UiText::UnwantedRegexRefCategoryBasic, R"(\A)", UiText::UnwantedRegexRefAbsoluteStart},
    {UiText::UnwantedRegexRefCategoryBasic, R"(\z)", UiText::UnwantedRegexRefAbsoluteEnd},
    {UiText::UnwantedRegexRefCategoryBasic, R"(\Q...\E)", UiText::UnwantedRegexRefQuotedLiteral},
    {UiText::UnwantedRegexRefCategoryBasic, R"(\.)", UiText::UnwantedRegexRefEscapedMeta},
    {UiText::UnwantedRegexRefCategoryBasic, ".", UiText::UnwantedRegexRefAnyChar},
    {UiText::UnwantedRegexRefCategoryClasses, "[abc]", UiText::UnwantedRegexRefClass},
    {UiText::UnwantedRegexRefCategoryClasses, "[^abc]", UiText::UnwantedRegexRefNegatedClass},
    {UiText::UnwantedRegexRefCategoryClasses, "[a-z]", UiText::UnwantedRegexRefRange},
    {UiText::UnwantedRegexRefCategoryClasses, R"(\d)", UiText::UnwantedRegexRefUnicodeDigit},
    {UiText::UnwantedRegexRefCategoryClasses, R"(\p{L})", UiText::UnwantedRegexRefUnicodeLetter},
    {UiText::UnwantedRegexRefCategoryClasses, R"(\s)", UiText::UnwantedRegexRefWhitespace},
    {UiText::UnwantedRegexRefCategoryClasses, R"(\h)", UiText::UnwantedRegexRefHorizontalWhitespace},
    {UiText::UnwantedRegexRefCategoryClasses, R"(\w)", UiText::UnwantedRegexRefWordChar},
    {UiText::UnwantedRegexRefCategoryClasses, R"(\b)", UiText::UnwantedRegexRefWordBoundary},
    {UiText::UnwantedRegexRefCategoryQuantifiers, "?", UiText::UnwantedRegexRefOptional},
    {UiText::UnwantedRegexRefCategoryQuantifiers, "*", UiText::UnwantedRegexRefZeroOrMore},
    {UiText::UnwantedRegexRefCategoryQuantifiers, "+", UiText::UnwantedRegexRefOneOrMore},
    {UiText::UnwantedRegexRefCategoryQuantifiers, "{3}", UiText::UnwantedRegexRefExactCount},
    {UiText::UnwantedRegexRefCategoryQuantifiers, "{1,4}", UiText::UnwantedRegexRefRangeCount},
    {UiText::UnwantedRegexRefCategoryQuantifiers, "*?", UiText::UnwantedRegexRefLazy},
    {UiText::UnwantedRegexRefCategoryGroups, "(?:...)", UiText::UnwantedRegexRefNonCapturingGroup},
    {UiText::UnwantedRegexRefCategoryGroups, "(a|b)", UiText::UnwantedRegexRefAlternation},
    {UiText::UnwantedRegexRefCategoryGroups, "(?=...)", UiText::UnwantedRegexRefPositiveLookahead},
    {UiText::UnwantedRegexRefCategoryGroups, "(?!...)", UiText::UnwantedRegexRefNegativeLookahead},
    {UiText::UnwantedRegexRefCategoryGroups, "(?<=a)", UiText::UnwantedRegexRefPositiveLookbehind},
    {UiText::UnwantedRegexRefCategoryGroups, "(?<!a)", UiText::UnwantedRegexRefNegativeLookbehind},
    {UiText::UnwantedRegexRefCategoryGroups, "(?>...)", UiText::UnwantedRegexRefAtomicGroup},
    {UiText::UnwantedRegexRefCategoryReady, R"(\[[0-9]{1,4}\])", UiText::UnwantedTokenPlayerIdHelp},
    {UiText::UnwantedRegexRefCategoryReady, R"(\{[0-9A-Fa-f]{6}(?:[0-9A-Fa-f]{2})?\})", UiText::UnwantedTokenColorHelp},
    {UiText::UnwantedRegexRefCategoryReady, R"((?=[A-Za-z0-9_]{3,24}(?:[^A-Za-z0-9_]|\z))[A-Za-z0-9]+_[A-Za-z0-9]+)", UiText::UnwantedTokenNicknameHelp},
    {UiText::UnwantedRegexRefCategoryReady, R"([+-]?[0-9]+)", UiText::UnwantedTokenIntegerHelp},
    {UiText::UnwantedRegexRefCategoryReady, R"([+-]?[0-9]+(?:[.,][0-9]+)?)", UiText::UnwantedTokenDecimalHelp},
    {UiText::UnwantedRegexRefCategoryReady, R"([+-]?[0-9]+(?:[.,][0-9]+)?%)", UiText::UnwantedTokenPercentageHelp},
    {UiText::UnwantedRegexRefCategoryReady, R"([+-]?[0-9]+(?:[.,][0-9]+)?[kK][0-9]*)", UiText::UnwantedTokenCompactAmountHelp},
    {UiText::UnwantedRegexRefCategoryReady, R"(\$[+-]?[0-9]+(?:[.,][0-9]+)?)", UiText::UnwantedTokenMoneyHelp},
    {UiText::UnwantedRegexRefCategoryReady, R"((?:[01][0-9]|2[0-3]):[0-5][0-9])", UiText::UnwantedTokenClockHelp},
    {UiText::UnwantedRegexRefCategoryReady, R"([0-9]{1,3}:[0-5][0-9])", UiText::UnwantedTokenDurationHelp},
    {UiText::UnwantedRegexRefCategoryReady, R"(\[[^\]\r\n]{1,64}\])", UiText::UnwantedTokenBracketPrefixHelp},
    {UiText::UnwantedRegexRefCategoryReady,
        R"((?:https?://)?(?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?\.)+[A-Za-z]{2,63}(?::(?:[0-9]{1,4}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?(?:/(?:[^\s\]\)}>]*[^\s\]\)}>,;!?])?)?)",
        UiText::UnwantedTokenDomainHelp},
};

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

bool BadgeButton(const char* label, const char* id, const ImVec4& color, bool active = false) {
    const ImVec2 size = BadgeSize(label);
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    DrawBadgeVisual(label, ImRect(min, max), color, hovered || active);
    return clicked;
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

bool RegexContainsBroadWildcard(std::string_view pattern) {
    bool escaped = false;
    bool inClass = false;
    bool inQuotedLiteral = false;
    for (std::size_t i = 0; i + 1 < pattern.size(); ++i) {
        const char ch = pattern[i];
        if (!inClass && ch == '\\' && pattern[i + 1] == (inQuotedLiteral ? 'E' : 'Q')) {
            inQuotedLiteral = !inQuotedLiteral;
            ++i;
            escaped = false;
            continue;
        }
        if (inQuotedLiteral) {
            continue;
        }
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '[') {
            inClass = true;
            continue;
        }
        if (ch == ']' && inClass) {
            inClass = false;
            continue;
        }
        if (!inClass && ch == '.' && (pattern[i + 1] == '*' || pattern[i + 1] == '+')) {
            return true;
        }
    }
    return false;
}

std::size_t Utf8CharacterOffset(std::string_view value, std::size_t byteOffset) {
    byteOffset = std::min(byteOffset, value.size());
    std::size_t characters = 0;
    for (std::size_t i = 0; i < byteOffset;) {
        const unsigned char lead = static_cast<unsigned char>(value[i]);
        std::size_t size = 1;
        if ((lead & 0xE0) == 0xC0) size = 2;
        else if ((lead & 0xF0) == 0xE0) size = 3;
        else if ((lead & 0xF8) == 0xF0) size = 4;
        i += std::min(size, byteOffset - i);
        ++characters;
    }
    return characters;
}

std::string FormatPcrePosition(std::string_view pattern, std::size_t byteOffset) {
    UiSettings& ui = UiSettings::Instance();
    byteOffset = std::min(byteOffset, pattern.size());
    const std::size_t lineStart = byteOffset == 0
        ? std::string_view::npos
        : pattern.rfind('\n', byteOffset - 1);
    const std::size_t actualStart = lineStart == std::string_view::npos ? 0 : lineStart + 1;
    const std::size_t lineEnd = pattern.find('\n', byteOffset);
    const std::size_t actualEnd = lineEnd == std::string_view::npos ? pattern.size() : lineEnd;
    std::string line(pattern.substr(actualStart, actualEnd - actualStart));
    constexpr std::size_t kMaxContextBytes = 96;
    std::size_t caretByte = byteOffset - actualStart;
    if (line.size() > kMaxContextBytes) {
        const std::size_t contextStart = caretByte > kMaxContextBytes / 2 ? caretByte - kMaxContextBytes / 2 : 0;
        line = line.substr(contextStart, kMaxContextBytes);
        caretByte -= contextStart;
        if (contextStart > 0) {
            line.insert(0, "...");
            caretByte += 3;
        }
    }
    const std::size_t caretChars = Utf8CharacterOffset(line, std::min(caretByte, line.size()));
    return ui.Format(
        UiText::UnwantedPcrePositionDetail,
        std::to_string(Utf8CharacterOffset(pattern, byteOffset) + 1).c_str(),
        line.c_str(),
        std::string(caretChars, ' ').c_str());
}

const char* RuntimeWarningDisplay(std::string_view status) {
    UiSettings& ui = UiSettings::Instance();
    if (status == "match_limit") return ui.Text(UiText::UnwantedRuntimeMatchLimit);
    if (status == "depth_limit") return ui.Text(UiText::UnwantedRuntimeDepthLimit);
    if (status == "heap_limit") return ui.Text(UiText::UnwantedRuntimeHeapLimit);
    if (status == "invalid_utf8") return ui.Text(UiText::UnwantedRuntimeInvalidText);
    return ui.Text(UiText::UnwantedRuntimeGenericError);
}

void AppendWarning(std::string& warning, std::string_view message) {
    if (message.empty()) {
        return;
    }
    if (!warning.empty()) {
        warning.push_back(' ');
    }
    warning.append(message);
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

void UnwantedMessagesModule::Shutdown() {
    rules_.clear();
    selectedRuleIds_.clear();
    scrollToRuleId_.clear();
    regexReferenceOpen_ = false;
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
        page_ = Page::Home;
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
    case Page::Create:
        DrawCreatePage(reload);
        break;
    case Page::Rules:
        DrawRulesPage(reload);
        break;
    case Page::Home:
    default:
        DrawHomePage(reload);
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

bool UnwantedMessagesModule::DrawPageHeader(const char* subtitle, bool& reload) {
    UiSettings& ui = UiSettings::Instance();
    const bool narrow = ImGui::GetContentRegionAvail().x < ScaleUi(620.0f);
    const float headerHeight = narrow ? ScaleUi(84.0f) : ScaleUi(58.0f);
    if (!BeginUnwantedPanel("##unwanted_page_header", ImVec2(0.0f, headerHeight))) {
        EndUnwantedPanel();
        return false;
    }

    if (UnwantedTextButton(ui_icons::ChevronLeft, ui.Text(UiText::EditorBack), "##unwanted_page_back", ScaleUi(112.0f, 0.0f))) {
        const Page target = page_ == Page::Home ? Page::Closed : Page::Home;
        if (page_ == Page::Create && ruleDraft_.active && ruleDraft_.dirty) {
            pendingPageAfterDiscard_ = target;
            unsavedConfirmOpen_ = true;
        } else {
            page_ = target;
            if (target != Page::Create) {
                ruleDraft_ = {};
            }
        }
        EndUnwantedPanel();
        return true;
    }
    const float buttonWidth = narrow ? ScaleUi(118.0f) : ScaleUi(138.0f);
    if (!narrow) {
        ImGui::SameLine();
        ImGui::Text("%s", ui.Text(UiText::UnwantedTitle));
        if (subtitle && subtitle[0] != '\0') {
            ImGui::SameLine();
            ImGui::TextDisabled("/ %s", subtitle);
        }
    }
    ImGui::SameLine(std::max(
        ImGui::GetCursorPosX() + ScaleUi(8.0f),
        ImGui::GetWindowWidth() - buttonWidth - ScaleUi(10.0f)));
    if (UnwantedTextButton(ui_icons::Gear, ui.Text(UiText::UnwantedTools), "##unwanted_settings", ImVec2(buttonWidth, 0.0f))) {
        ImGui::OpenPopup("##unwanted_settings_popup");
    }
    if (narrow) {
        ImGui::Spacing();
        ImGui::Text("%s", ui.Text(UiText::UnwantedTitle));
        if (subtitle && subtitle[0] != '\0') {
            ImGui::SameLine();
            ImGui::TextDisabled("/ %s", subtitle);
        }
    }
    DrawSettingsPopup();
    EndUnwantedPanel();
    ImGui::Spacing();
    (void)reload;
    return false;
}

void UnwantedMessagesModule::DrawHomePage(bool& reload) {
    UiSettings& ui = UiSettings::Instance();
    const UnwantedVisualStyle visual = UnwantedStyleTokens();
    if (DrawPageHeader(ui.Text(UiText::UnwantedHome), reload)) {
        return;
    }

    const std::size_t invalid = std::count_if(rules_.begin(), rules_.end(), [](const Rule& rule) { return !rule.error.empty(); });
    const std::size_t enabled = std::count_if(rules_.begin(), rules_.end(), [](const Rule& rule) { return rule.enabled; });
    const std::size_t duplicates = DuplicateRuleCount();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool horizontal = avail.x >= ScaleUi(780.0f);
    const float gap = ScaleUi(10.0f);
    const float width = horizontal ? (avail.x - gap * 2.0f) / 3.0f : avail.x;
    const float height = ScaleUi(172.0f);

    const auto beginCard = [&](const char* id, const ImVec4& accent) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, WithAlpha(visual.panelBg, 0.96f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ScaleUi(9.0f));
        const bool visible = ImGui::BeginChild(id, ImVec2(width, height), ImGuiChildFlags_FrameStyle);
        if (visible) {
            const ImVec2 min = ImGui::GetWindowPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                min,
                ImVec2(min.x + ScaleUi(5.0f), min.y + ImGui::GetWindowHeight()),
                ImGui::GetColorU32(accent),
                ScaleUi(9.0f),
                ImDrawFlags_RoundCornersLeft);
        }
        return visible;
    };
    const auto endCard = [&] {
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    };

    if (beginCard("##unwanted_toggle_card", settings_.enabled ? visual.ok : visual.danger)) {
        ImGui::TextColored(settings_.enabled ? visual.ok : visual.danger, "%s", ui.Text(UiText::UnwantedModuleToggleTitle));
        ImGui::TextWrapped("%s", ui.Text(UiText::UnwantedModuleToggleDesc));
        ImGui::Spacing();
        bool enabledSetting = settings_.enabled;
        if (ImGui::Checkbox(ui.Text(settings_.enabled ? UiText::UnwantedModuleOn : UiText::UnwantedModuleOff), &enabledSetting)) {
            settings_.enabled = enabledSetting;
            PublishRuntimeSnapshot();
            SaveSettings();
        }
    }
    endCard();
    if (horizontal) ImGui::SameLine(0.0f, gap); else ImGui::Spacing();

    if (beginCard("##unwanted_create_card", visual.accent)) {
        ImGui::TextColored(visual.accent, "%s", ui.Text(UiText::UnwantedNewRule));
        ImGui::TextWrapped("%s", ui.Text(UiText::UnwantedCreateCardDesc));
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ScaleUi(42.0f));
        if (UnwantedTextButton(ui_icons::Plus, ui.Text(UiText::UnwantedCreateOpen), "##open_create", ImVec2(-1.0f, 0.0f))) {
            StartCreateRule({}, RuleType::Regex);
            page_ = Page::Create;
        }
    }
    endCard();
    if (horizontal) ImGui::SameLine(0.0f, gap); else ImGui::Spacing();

    if (beginCard("##unwanted_rules_card", visual.mutedText)) {
        ImGui::TextColored(visual.mutedText, "%s", ui.Text(UiText::UnwantedRules));
        ImGui::TextWrapped("%s", ui.Format(
            UiText::UnwantedRulesCardStats,
            std::to_string(rules_.size()).c_str(),
            std::to_string(enabled).c_str(),
            std::to_string(invalid).c_str(),
            std::to_string(duplicates).c_str()).c_str());
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ScaleUi(42.0f));
        if (UnwantedTextButton(ui_icons::Bars, ui.Text(UiText::UnwantedRulesOpen), "##open_rules", ImVec2(-1.0f, 0.0f))) {
            page_ = Page::Rules;
        }
    }
    endCard();

    ImGui::Spacing();
    MatchResult lastBlockedView;
    std::uint64_t blockedCountView = 0;
    {
        std::lock_guard lock(statusMutex_);
        lastBlockedView = lastBlocked_;
        blockedCountView = blockedCount_;
    }
    if (BeginUnwantedPanel("##unwanted_session_summary", ImVec2(0.0f, ScaleUi(76.0f)))) {
        ImGui::Text("%s", ui.Text(UiText::UnwantedSessionTitle));
        if (lastBlockedView.matched) {
            ImGui::TextWrapped("%s", ui.Format(
                UiText::UnwantedLastBlocked,
                lastBlockedView.candidate.c_str()).c_str());
        } else {
            ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedLastBlockedEmpty));
        }
        ImGui::TextDisabled("%s", ui.Format(UiText::UnwantedBlockedCount, std::to_string(blockedCountView).c_str()).c_str());
    }
    EndUnwantedPanel();
}

void UnwantedMessagesModule::DrawCreatePage(bool& reload) {
    UiSettings& ui = UiSettings::Instance();
    if (DrawPageHeader(ui.Text(UiText::UnwantedCreateRuleTitle), reload)) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        if (ruleDraft_.active && ruleDraft_.dirty) {
            pendingPageAfterDiscard_ = Page::Home;
            unsavedConfirmOpen_ = true;
        } else {
            ruleDraft_ = {};
            page_ = Page::Home;
        }
        return;
    }
    if (!ruleDraft_.active || !ruleDraft_.createMode) {
        StartCreateRule({}, RuleType::Regex);
    }

    if (BeginUnwantedPanel("##unwanted_create_workspace", ImVec2(0.0f, 0.0f))) {
        DrawRegexHelperWizard();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        DrawRuleEditor(false);
    }
    EndUnwantedPanel();
}

void UnwantedMessagesModule::DrawRulesPage(bool& reload) {
    UiSettings& ui = UiSettings::Instance();
    if (DrawPageHeader(ui.Text(UiText::UnwantedRules), reload)) {
        return;
    }
    DrawRulesPane(ImGui::GetContentRegionAvail());
    DrawRuleEditorPopup();
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
    activeRuleId_.clear();
    scrollToRuleId_.clear();
    regexReferenceOpen_ = false;
    ruleDraft_ = {};
    lastTesterMatch_ = {};
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
    rule.validation.broadWildcard = RegexContainsBroadWildcard(rule.text);
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
            FormatPcrePosition(rule.text, rule.validation.pcreErrorOffset).c_str());
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
        const std::string& sortName = rule.name.empty() ? rule.text : rule.name;
        rule.sortNameLower = Utf8ToLower(sortName);
        rule.searchBlobLower = Utf8ToLower(
            rule.id + "\n" + rule.name + "\n" + rule.text + "\n" + RuleTypeName(rule.type)
            + "\n" + rule.error + "\n" + rule.warning);
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
    const UnwantedVisualStyle visual = UnwantedStyleTokens();
    if (!BeginUnwantedPanel("##unwanted_rules_pane", size)) {
        EndUnwantedPanel();
        return;
    }

    const std::string count = std::to_string(rules_.size());
    ImGui::Text("%s", ui.Text(UiText::UnwantedRules));
    ImGui::SameLine();
    TextBadge(count.c_str(), visual.mutedText);
    ImGui::SameLine();
    if (selectedRuleIds_.empty()) {
        if (ImGui::Button(ui.Text(UiText::UnwantedSelectAll), ScaleUi(122.0f, 0.0f))) {
            SelectAllRules();
        }
    } else if (ImGui::Button(
                   ui.Format(UiText::UnwantedBulkActionsFormat, std::to_string(selectedRuleIds_.size()).c_str()).c_str(),
                   ScaleUi(152.0f, 0.0f))) {
        ImGui::OpenPopup("##unwanted_bulk_actions");
    }
    if (ImGui::BeginPopup("##unwanted_bulk_actions")) {
        if (ImGui::MenuItem(ui.Text(UiText::UnwantedEnableSelected))) {
            SetSelectedRulesEnabled(true);
        }
        if (ImGui::MenuItem(ui.Text(UiText::UnwantedDisableSelected))) {
            SetSelectedRulesEnabled(false);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ui.Text(UiText::UnwantedClearSelection))) {
            ClearSelection();
        }
        if (ImGui::MenuItem(ui.Text(UiText::UnwantedDeleteSelected))) {
            deleteSelectedConfirmOpen_ = true;
        }
        ImGui::EndPopup();
    }

    const std::string searchHint = std::string(ui_icons::Search) + " " + ui.Text(UiText::UnwantedSearchHint);
    ImGui::SetNextItemWidth(-1.0f);
    InputTextWithHintString("##unwanted_rule_search", searchHint.c_str(), ruleSearch_, ImGuiInputTextFlags_AutoSelectAll, 128);
    const char* sortLabel = ui.Text(UiText::UnwantedSortStored);
    if (ruleSort_ == RuleSort::Name) sortLabel = ui.Text(UiText::UnwantedSortName);
    if (ruleSort_ == RuleSort::Type) sortLabel = ui.Text(UiText::UnwantedSortByType);
    if (ruleSort_ == RuleSort::Status) sortLabel = ui.Text(UiText::UnwantedSortStatus);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x < ScaleUi(360.0f) ? -1.0f : ScaleUi(190.0f));
    if (ImGui::BeginCombo("##unwanted_sort", sortLabel)) {
        if (ImGui::Selectable(ui.Text(UiText::UnwantedSortStored), ruleSort_ == RuleSort::Stored)) ruleSort_ = RuleSort::Stored;
        if (ImGui::Selectable(ui.Text(UiText::UnwantedSortName), ruleSort_ == RuleSort::Name)) ruleSort_ = RuleSort::Name;
        if (ImGui::Selectable(ui.Text(UiText::UnwantedSortByType), ruleSort_ == RuleSort::Type)) ruleSort_ = RuleSort::Type;
        if (ImGui::Selectable(ui.Text(UiText::UnwantedSortStatus), ruleSort_ == RuleSort::Status)) ruleSort_ = RuleSort::Status;
        ImGui::EndCombo();
    }

    const float wrapRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    bool firstChip = true;
    const auto drawChip = [&](RuleFilter filter, UiText label) {
        const float width = ImGui::CalcTextSize(ui.Text(label)).x + ScaleUi(22.0f);
        if (!firstChip && ImGui::GetCursorScreenPos().x + width > wrapRight) {
            ImGui::NewLine();
            firstChip = true;
        }
        if (!firstChip) {
            ImGui::SameLine();
        }
        if (FilterChip(ui.Text(label), ruleFilter_ == filter)) {
            ruleFilter_ = filter;
        }
        firstChip = false;
    };

    drawChip(RuleFilter::All, UiText::UnwantedFilterAll);
    drawChip(RuleFilter::Enabled, UiText::UnwantedFilterEnabled);
    drawChip(RuleFilter::Disabled, UiText::UnwantedFilterDisabled);
    drawChip(RuleFilter::Regex, UiText::UnwantedFilterRegex);
    drawChip(RuleFilter::Literal, UiText::UnwantedFilterLiteral);
    drawChip(RuleFilter::Errors, UiText::UnwantedFilterErrors);

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

    ImGui::TextDisabled(
        "%s",
        ui.Format(
            UiText::UnwantedVisibleRulesFormat,
            std::to_string(visible.size()).c_str(),
            std::to_string(rules_.size()).c_str()).c_str());
    DrawRulesTable(visible);
    EndUnwantedPanel();
}

void UnwantedMessagesModule::DrawRulesTable(const std::vector<std::size_t>& visibleIndices) {
    UiSettings& ui = UiSettings::Instance();
    const UnwantedVisualStyle visual = UnwantedStyleTokens();
    if (rules_.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedNoRules));
        return;
    }
    if (visibleIndices.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedNoVisibleRules));
        return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp
        | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_NoSavedSettings
        | ImGuiTableFlags_BordersInnerH;
    const bool compact = ImGui::GetContentRegionAvail().x < ScaleUi(760.0f);
    const int columnCount = compact ? 4 : 7;
    if (!ImGui::BeginTable("##unwanted_rules_table", columnCount, flags, ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
        return;
    }

    ImGui::TableSetupColumn("sel", ImGuiTableColumnFlags_WidthFixed, ScaleUi(28.0f));
    ImGui::TableSetupColumn("enabled", ImGuiTableColumnFlags_WidthFixed, ScaleUi(34.0f));
    if (compact) {
        ImGui::TableSetupColumn("summary", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("actions", ImGuiTableColumnFlags_WidthFixed, ScaleUi(44.0f));
    } else {
        ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed, ScaleUi(76.0f));
        ImGui::TableSetupColumn("text", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("flags", ImGuiTableColumnFlags_WidthFixed, ScaleUi(118.0f));
        ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthFixed, ScaleUi(86.0f));
        ImGui::TableSetupColumn("actions", ImGuiTableColumnFlags_WidthFixed, ScaleUi(44.0f));
    }

    if (!scrollToRuleId_.empty()) {
        const auto target = std::find_if(visibleIndices.begin(), visibleIndices.end(), [&](std::size_t index) {
            return index < rules_.size() && rules_[index].id == scrollToRuleId_;
        });
        if (target != visibleIndices.end()) {
            const float row = static_cast<float>(std::distance(visibleIndices.begin(), target));
            ImGui::SetScrollY(std::max(0.0f, row * ImGui::GetTextLineHeightWithSpacing() - ImGui::GetWindowHeight() * 0.4f));
        }
        scrollToRuleId_.clear();
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visibleIndices.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::size_t index = visibleIndices[static_cast<std::size_t>(row)];
            Rule& rule = rules_[index];
            const bool active = !ruleDraft_.createMode && activeRuleId_ == rule.id;
            ImGui::PushID(rule.id.c_str());
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            bool selected = IsRuleSelected(rule.id);
            if (ImGui::Checkbox("##selected", &selected)) {
                SetRuleSelected(rule.id, selected);
            }

            ImGui::TableSetColumnIndex(1);
            if (ImGui::Checkbox("##enabled", &rule.enabled)) {
                PublishRuntimeSnapshot();
                SaveConfig();
            }

            if (compact) {
                ImGui::TableSetColumnIndex(2);
                std::string visibleLabel = rule.name.empty()
                    ? (rule.text.empty() ? rule.id : rule.text)
                    : rule.name + "  —  " + rule.text;
                if (ImGui::Selectable((visibleLabel + "##rule_select_compact").c_str(), active)) {
                    StartEditRule(rule.id);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::SetTooltip("%s", rule.text.empty() ? rule.id.c_str() : rule.text.c_str());
                }
                std::string meta = rule.type == RuleType::Regex
                    ? ui.Text(UiText::UnwantedTypeRegex)
                    : ui.Text(UiText::UnwantedTypeLiteral);
                if (rule.nocase) meta += std::string(" · ") + ui.Text(UiText::UnwantedNoCase);
                if (rule.wholeWord) meta += std::string(" · ") + ui.Text(UiText::UnwantedWholeWord);
                if (!rule.error.empty()) meta += std::string(" · ") + ui.Text(UiText::UnwantedInvalidRule);
                else if (rule.duplicate) meta += std::string(" · ") + ui.Text(UiText::UnwantedDuplicate);
                else if (runtimeWarningsView_.find(rule.id) != runtimeWarningsView_.end()) {
                    meta += std::string(" · ") + ui.Text(UiText::UnwantedWarning);
                } else if (!rule.warning.empty()) meta += std::string(" · ") + ui.Text(UiText::UnwantedWarning);
                else meta += std::string(" · ") + ui.Text(UiText::UnwantedRuleOk);
                ImGui::TextDisabled("%s", meta.c_str());
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    if (!rule.error.empty()) ImGui::SetTooltip("%s", rule.error.c_str());
                    else if (const auto runtimeWarning = runtimeWarningsView_.find(rule.id);
                             runtimeWarning != runtimeWarningsView_.end()) {
                        ImGui::SetTooltip("%s", ui.Format(
                            UiText::UnwantedRuntimeWarningFormat,
                            RuntimeWarningDisplay(runtimeWarning->second)).c_str());
                    } else if (!rule.warning.empty()) ImGui::SetTooltip("%s", rule.warning.c_str());
                }

                ImGui::TableSetColumnIndex(3);
                if (ImGui::SmallButton("...")) {
                    ImGui::OpenPopup("##rule_actions_compact");
                }
                if (ImGui::BeginPopup("##rule_actions_compact")) {
                    if (ImGui::MenuItem(ui.Text(UiText::Edit))) StartEditRule(rule.id);
                    if (ImGui::MenuItem(ui.Text(UiText::MoveUp), nullptr, false, index > 0)) {
                        std::swap(rules_[index], rules_[index - 1]);
                        PublishRuntimeSnapshot();
                        SaveConfig();
                    }
                    if (ImGui::MenuItem(ui.Text(UiText::MoveDown), nullptr, false, index + 1 < rules_.size())) {
                        std::swap(rules_[index], rules_[index + 1]);
                        PublishRuntimeSnapshot();
                        SaveConfig();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(ui.Text(UiText::Delete))) {
                        selectedRuleIds_.clear();
                        selectedRuleIds_.insert(rule.id);
                        deleteSelectedConfirmOpen_ = true;
                        ImGui::EndPopup();
                        ImGui::PopID();
                        ImGui::EndTable();
                        return;
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
                continue;
            }

            ImGui::TableSetColumnIndex(2);
            if (rule.type == RuleType::Regex) {
                if (BadgeButton(
                        ui.Text(UiText::UnwantedTypeRegex),
                        "##type_filter",
                        visual.accent,
                        ruleFilter_ == RuleFilter::Regex)) {
                    ruleFilter_ = RuleFilter::Regex;
                }
            } else if (BadgeButton(
                           ui.Text(UiText::UnwantedTypeLiteral),
                           "##type_filter",
                           visual.mutedText,
                           ruleFilter_ == RuleFilter::Literal)) {
                ruleFilter_ = RuleFilter::Literal;
            }

            ImGui::TableSetColumnIndex(3);
            std::string visibleLabel = rule.name.empty() ? (rule.text.empty() ? rule.id : rule.text) : rule.name + "  —  " + rule.text;
            const std::string label = visibleLabel + "##rule_select";
            if (ImGui::Selectable(label.c_str(), active, ImGuiSelectableFlags_SpanAvailWidth)) {
                StartEditRule(rule.id);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", rule.text.empty() ? rule.id.c_str() : rule.text.c_str());
            }

            ImGui::TableSetColumnIndex(4);
            bool hasBadge = false;
            if (rule.nocase) {
                TextBadge(ui.Text(UiText::UnwantedNoCase), visual.mutedText);
                hasBadge = true;
            }
            if (rule.wholeWord) {
                if (hasBadge) {
                    ImGui::SameLine();
                }
                TextBadge(ui.Text(UiText::UnwantedWholeWord), visual.mutedText);
                hasBadge = true;
            }
            if (!hasBadge) {
                ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedNoFlags));
            }

            ImGui::TableSetColumnIndex(5);
            if (!rule.error.empty()) {
                ImGui::TextColored(visual.danger, "%s", ui.Text(UiText::UnwantedInvalidRule));
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::SetTooltip("%s", rule.error.c_str());
                }
            } else if (IsDuplicateRule(index)) {
                ImGui::TextColored(visual.warn, "%s", ui.Text(UiText::UnwantedDuplicate));
            } else if (const auto runtimeWarning = runtimeWarningsView_.find(rule.id);
                       runtimeWarning != runtimeWarningsView_.end()) {
                ImGui::TextColored(
                    visual.warn,
                    "%s",
                    ui.Format(
                        UiText::UnwantedRuntimeWarningFormat,
                        RuntimeWarningDisplay(runtimeWarning->second)).c_str());
            } else if (!rule.warning.empty()) {
                ImGui::TextColored(visual.warn, "%s", ui.Text(UiText::UnwantedWarning));
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::SetTooltip("%s", rule.warning.c_str());
                }
            } else {
                ImGui::TextColored(visual.ok, "%s", ui.Text(UiText::UnwantedRuleOk));
            }

            ImGui::TableSetColumnIndex(6);
            if (ImGui::SmallButton("...")) {
                ImGui::OpenPopup("##rule_actions");
            }
            if (ImGui::BeginPopup("##rule_actions")) {
                if (ImGui::MenuItem(ui.Text(UiText::Edit))) {
                    StartEditRule(rule.id);
                }
                if (ImGui::MenuItem(ui.Text(UiText::MoveUp), nullptr, false, index > 0)) {
                    std::swap(rules_[index], rules_[index - 1]);
                    PublishRuntimeSnapshot();
                    SaveConfig();
                }
                if (ImGui::MenuItem(ui.Text(UiText::MoveDown), nullptr, false, index + 1 < rules_.size())) {
                    std::swap(rules_[index], rules_[index + 1]);
                    PublishRuntimeSnapshot();
                    SaveConfig();
                }
                ImGui::Separator();
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

bool UnwantedMessagesModule::DrawRuleEditor(bool popupMode) {
    UiSettings& ui = UiSettings::Instance();
    const UnwantedVisualStyle visual = UnwantedStyleTokens();

    if (!ruleDraft_.active) {
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedNoSelection));
        if (ImGui::Button(ui.Text(UiText::UnwantedNewRule), ScaleUi(150.0f, 0.0f))) {
            StartCreateRule();
        }
        return false;
    }

    ImGui::Text("%s", ui.Text(ruleDraft_.createMode ? UiText::UnwantedCreateRuleTitle : UiText::UnwantedEditRuleTitle));
    if (!ruleDraft_.createMode) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ruleDraft_.id.c_str());
    }

    bool changed = false;
    ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedRuleName));
    ImGui::SetNextItemWidth(-1.0f);
    changed |= InputTextWithHintString(
        "##unwanted_rule_name",
        ui.Text(UiText::UnwantedRuleNameHint),
        ruleDraft_.name,
        ImGuiInputTextFlags_AutoSelectAll,
        128);

    const float controlsRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedRuleEnabled), &ruleDraft_.enabled);
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + ScaleUi(130.0f) <= controlsRight) {
        ImGui::SameLine();
    }
    const char* typePreview = ruleDraft_.type == RuleType::Regex ? ui.Text(UiText::UnwantedTypeRegex) : ui.Text(UiText::UnwantedTypeLiteral);
    ImGui::SetNextItemWidth(ScaleUi(130.0f));
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
        lastTesterMatch_ = {};
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    DrawTesterPanel();
    ImGui::Spacing();

    std::string draftError;
    std::string draftWarning;
    ValidateDraft(draftError, draftWarning);

    if (!draftError.empty()) {
        ImGui::TextColored(visual.danger, "%s", draftError.c_str());
    } else if (!draftWarning.empty()) {
        ImGui::TextColored(visual.warn, "%s", draftWarning.c_str());
    } else {
        ImGui::TextColored(visual.ok, "%s", ui.Text(UiText::UnwantedRuleOk));
    }

    ImGui::BeginDisabled(!draftError.empty() || (!ruleDraft_.dirty && !ruleDraft_.createMode));
    if (UnwantedTextButton(ui_icons::Check, ui.Text(UiText::Save), "##unwanted_save_rule", ScaleUi(132.0f, 0.0f))) {
        SaveDraftRule();
    }
    ImGui::EndDisabled();
    if (!ruleDraft_.createMode) {
        ImGui::SameLine();
        if (UnwantedTextButton(ui_icons::Delete, ui.Text(UiText::Delete), "##unwanted_delete_rule", ScaleUi(132.0f, 0.0f))) {
            selectedRuleIds_.clear();
            selectedRuleIds_.insert(ruleDraft_.id);
            deleteSelectedConfirmOpen_ = true;
        }
    }
    if (popupMode) {
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::Cancel), ScaleUi(132.0f, 0.0f))) {
            if (ruleDraft_.dirty) {
                pendingPageAfterDiscard_ = Page::Rules;
                unsavedConfirmOpen_ = true;
            } else {
                editPopupOpen_ = false;
                ruleDraft_ = {};
                ImGui::CloseCurrentPopup();
            }
        }
    }
    return draftError.empty();
}

void UnwantedMessagesModule::DrawTesterPanel() {
    UiSettings& ui = UiSettings::Instance();
    const UnwantedVisualStyle visual = UnwantedStyleTokens();

    ImGui::Text("%s", ui.Text(UiText::UnwantedTester));
    const float actionWidth = ScaleUi(132.0f);
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const bool inlineAction = availableWidth >= ScaleUi(360.0f);
    if (inlineAction) {
        ImGui::SetNextItemWidth(std::max(
            ScaleUi(160.0f),
            availableWidth - actionWidth - ImGui::GetStyle().ItemSpacing.x));
    } else {
        ImGui::SetNextItemWidth(-1.0f);
    }
    const bool testerTextChanged = InputTextWithHintString(
        "##unwanted_test_text",
        ui.Text(UiText::UnwantedTesterHint),
        testText_,
        ImGuiInputTextFlags_AutoSelectAll,
        1024);
    if (testerTextChanged) {
        lastTesterMatch_ = {};
    }
    if (inlineAction) {
        ImGui::SameLine();
    }
    const bool testRequested = UnwantedTextButton(
        ui_icons::Check,
        ui.Text(UiText::UnwantedTestAction),
        "##unwanted_test",
        ImVec2(actionWidth, 0.0f));
    const std::string normalized = NormalizeCandidate(testText_);
    ImGui::TextDisabled(
        "%s",
        ui.Format(
            UiText::UnwantedTesterNormalizedFormat,
            std::to_string(Utf8CharacterOffset(normalized, normalized.size())).c_str(),
            normalized.c_str()).c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", ui.Text(UiText::UnwantedTesterEmptyHint));
    ImGui::PopStyleColor();

    if (testRequested) {
        lastTesterMatch_ = {};
        const std::string candidate = NormalizeCandidate(testText_);
        bool matched = false;
        if (ruleDraft_.active) {
            if (ruleDraft_.type == RuleType::Literal) {
                PreparedRule temporary;
                temporary.type = RuleType::Literal;
                temporary.text = TrimAscii(ruleDraft_.text);
                temporary.nocase = ruleDraft_.nocase;
                temporary.wholeWord = ruleDraft_.wholeWord;
                temporary.literalNeedle = temporary.nocase ? Utf8ToLower(temporary.text) : temporary.text;
                const std::string folded = temporary.nocase ? Utf8ToLower(candidate) : std::string{};
                matched = MatchLiteral(temporary, candidate, folded);
            } else {
                unwanted_regex::CompileResult compiled = unwanted_regex::Compile(TrimAscii(ruleDraft_.text), ruleDraft_.nocase);
                matched = compiled.program
                    && compiled.program->Match(candidate).status == unwanted_regex::MatchStatus::Match;
            }
        }
        if (matched) {
            lastTesterMatch_.matched = true;
            lastTesterMatch_.ruleId = ruleDraft_.createMode ? "__draft__" : ruleDraft_.id;
            lastTesterMatch_.candidate = candidate;
        }
    }

    if (lastTesterMatch_.matched) {
        ImGui::TextColored(
            visual.ok,
            "%s",
            ui.Text(UiText::UnwantedTesterMatched));
    } else {
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedTesterNoMatch));
    }
}

void UnwantedMessagesModule::DrawRegexHelperWizard() {
    UiSettings& ui = UiSettings::Instance();
    const UnwantedVisualStyle visual = UnwantedStyleTokens();

    ImGui::Text("%s", ui.Text(UiText::UnwantedRegexHelper));
    const float referenceWidth = ScaleUi(170.0f);
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + referenceWidth
        <= ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x) {
        ImGui::SameLine();
    }
    if (UnwantedTextButton(
            ui_icons::Book,
            ui.Text(UiText::UnwantedRegexReference),
            "##unwanted_regex_reference",
            ImVec2(referenceWidth, 0.0f))) {
        const std::string title = std::string(ui.Text(UiText::UnwantedRegexReference)) + "###unwanted_regex_reference_modal";
        regexReferenceOpen_ = true;
        ImGui::OpenPopup(title.c_str());
    }
    ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedHelperFlowHint));
    ImGui::SetNextItemWidth(-1.0f);
    bool changed = InputTextWithHintString(
        "##unwanted_helper_sample",
        ui.Text(UiText::UnwantedHelperInputHint),
        helperSample_,
        ImGuiInputTextFlags_AutoSelectAll,
        1024);

    ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedGeneralizations));
    const float optionsRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    bool firstOption = true;
    float previousOptionRight = 0.0f;
    const auto drawOption = [&](bool& value, UiText label, UiText help) {
        const float width = ImGui::GetFrameHeight()
            + ImGui::GetStyle().ItemInnerSpacing.x
            + ImGui::CalcTextSize(ui.Text(label)).x;
        if (!firstOption && previousOptionRight + ImGui::GetStyle().ItemSpacing.x + width <= optionsRight) {
            ImGui::SameLine();
        }
        changed |= ImGui::Checkbox(ui.Text(label), &value);
        DrawUnwantedTooltip(ui.Text(help));
        previousOptionRight = ImGui::GetItemRectMax().x;
        firstOption = false;
    };
    drawOption(helperColors_, UiText::UnwantedHelperColors, UiText::UnwantedTokenColorHelp);
    drawOption(helperNumbers_, UiText::UnwantedHelperNumbers, UiText::UnwantedTokenIntegerHelp);
    drawOption(helperMoney_, UiText::UnwantedHelperMoney, UiText::UnwantedTokenMoneyHelp);
    drawOption(helperTime_, UiText::UnwantedHelperTime, UiText::UnwantedTokenClockHelp);
    drawOption(helperNick_, UiText::UnwantedHelperNick, UiText::UnwantedTokenNicknameHelp);
    drawOption(helperPlayerId_, UiText::UnwantedHelperPlayerId, UiText::UnwantedTokenPlayerIdHelp);
    drawOption(helperDomain_, UiText::UnwantedHelperDomain, UiText::UnwantedTokenDomainHelp);
    drawOption(helperBracketTag_, UiText::UnwantedHelperBracketTag, UiText::UnwantedTokenBracketPrefixHelp);

    if (changed) {
        RegenerateHelperOutput();
    }
    if (!helperSample_.empty() && helperExact_.empty() && helperGeneralized_.empty() && helperContains_.empty()) {
        RegenerateHelperOutput();
    }

    if (!helperSample_.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedNormalizedPreview));
        ImGui::TextWrapped("%s", NormalizeCandidate(helperSample_).c_str());
    }

    if (!helperTokens_.empty()) {
        ImGui::Spacing();
        ImGui::Text("%s", ui.Text(UiText::UnwantedDetectedTokens));
        const auto tokenLabel = [&](unwanted_regex_builder::TokenKind kind) {
            switch (kind) {
            case unwanted_regex_builder::TokenKind::Color: return ui.Text(UiText::UnwantedTokenColor);
            case unwanted_regex_builder::TokenKind::PlayerId: return ui.Text(UiText::UnwantedTokenPlayerId);
            case unwanted_regex_builder::TokenKind::BracketPrefix: return ui.Text(UiText::UnwantedTokenBracketPrefix);
            case unwanted_regex_builder::TokenKind::Nickname: return ui.Text(UiText::UnwantedTokenNickname);
            case unwanted_regex_builder::TokenKind::Integer: return ui.Text(UiText::UnwantedTokenInteger);
            case unwanted_regex_builder::TokenKind::Decimal: return ui.Text(UiText::UnwantedTokenDecimal);
            case unwanted_regex_builder::TokenKind::Percentage: return ui.Text(UiText::UnwantedTokenPercentage);
            case unwanted_regex_builder::TokenKind::CompactAmount: return ui.Text(UiText::UnwantedTokenCompactAmount);
            case unwanted_regex_builder::TokenKind::Money: return ui.Text(UiText::UnwantedTokenMoney);
            case unwanted_regex_builder::TokenKind::Clock: return ui.Text(UiText::UnwantedTokenClock);
            case unwanted_regex_builder::TokenKind::Duration: return ui.Text(UiText::UnwantedTokenDuration);
            case unwanted_regex_builder::TokenKind::Domain: return ui.Text(UiText::UnwantedTokenDomain);
            default: return "?";
            }
        };
        const auto tokenHelp = [&](unwanted_regex_builder::TokenKind kind) {
            switch (kind) {
            case unwanted_regex_builder::TokenKind::Color: return ui.Text(UiText::UnwantedTokenColorHelp);
            case unwanted_regex_builder::TokenKind::PlayerId: return ui.Text(UiText::UnwantedTokenPlayerIdHelp);
            case unwanted_regex_builder::TokenKind::BracketPrefix: return ui.Text(UiText::UnwantedTokenBracketPrefixHelp);
            case unwanted_regex_builder::TokenKind::Nickname: return ui.Text(UiText::UnwantedTokenNicknameHelp);
            case unwanted_regex_builder::TokenKind::Integer: return ui.Text(UiText::UnwantedTokenIntegerHelp);
            case unwanted_regex_builder::TokenKind::Decimal: return ui.Text(UiText::UnwantedTokenDecimalHelp);
            case unwanted_regex_builder::TokenKind::Percentage: return ui.Text(UiText::UnwantedTokenPercentageHelp);
            case unwanted_regex_builder::TokenKind::CompactAmount: return ui.Text(UiText::UnwantedTokenCompactAmountHelp);
            case unwanted_regex_builder::TokenKind::Money: return ui.Text(UiText::UnwantedTokenMoneyHelp);
            case unwanted_regex_builder::TokenKind::Clock: return ui.Text(UiText::UnwantedTokenClockHelp);
            case unwanted_regex_builder::TokenKind::Duration: return ui.Text(UiText::UnwantedTokenDurationHelp);
            case unwanted_regex_builder::TokenKind::Domain: return ui.Text(UiText::UnwantedTokenDomainHelp);
            default: return "";
            }
        };
        for (const auto& token : helperTokens_) {
            ImGui::PushID(static_cast<int>(token.offset));
            TextBadge(tokenLabel(token.kind), visual.accent);
            DrawUnwantedTooltip(tokenHelp(token.kind));
            ImGui::SameLine();
            ImGui::TextWrapped("%s  ->  %s", token.source.c_str(), token.pattern.c_str());
            ImGui::PopID();
        }
    }

    if (!helperWarning_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(visual.warn, "%s", helperWarning_.c_str());
    }

    ImGui::Spacing();
    ImGui::Text("%s", ui.Text(UiText::UnwantedRegexVariants));
    ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedRegexVariantsHint));
    const ImGuiTableFlags variantFlags = ImGuiTableFlags_SizingStretchProp
        | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerH
        | ImGuiTableFlags_NoSavedSettings;
    const bool variantsVisible = ImGui::BeginTable("##unwanted_regex_variants", 3, variantFlags);
    if (variantsVisible) {
        ImGui::TableSetupColumn("variant", ImGuiTableColumnFlags_WidthFixed, ScaleUi(128.0f));
        ImGui::TableSetupColumn("pattern", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthFixed, ScaleUi(122.0f));
    }

    const auto drawVariant = [&](UiText title, const std::string& pattern, bool valid) {
        if (pattern.empty()) {
            return;
        }
        ImGui::PushID(static_cast<int>(title));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(valid ? visual.ok : visual.danger, "%s", ui.Text(title));
        if (!valid && !helperWarning_.empty()) {
            DrawUnwantedTooltip(helperWarning_.c_str());
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("%s", pattern.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("%s", ui.Text(UiText::UnwantedCopy));
        }
        if (ImGui::IsItemClicked()) {
            ImGui::SetClipboardText(pattern.c_str());
        }
        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button(ui.Text(UiText::UnwantedUseInDraft), ImVec2(-1.0f, 0.0f))) {
            ruleDraft_.type = RuleType::Regex;
            ruleDraft_.wholeWord = false;
            ruleDraft_.text = pattern;
            ruleDraft_.dirty = true;
        }
        ImGui::PopID();
    };

    if (variantsVisible) {
        drawVariant(UiText::UnwantedHelperGeneralized, helperGeneralized_, helperGeneralizedValid_);
        drawVariant(UiText::UnwantedHelperExact, helperExact_, helperExactValid_);
        drawVariant(UiText::UnwantedHelperContains, helperContains_, helperContainsValid_);
        ImGui::EndTable();
    }
    DrawRegexReferencePopup();
}

void UnwantedMessagesModule::DrawRegexReferencePopup() {
    UiSettings& ui = UiSettings::Instance();
    const std::string title = std::string(ui.Text(UiText::UnwantedRegexReference)) + "###unwanted_regex_reference_modal";
    if (!regexReferenceOpen_) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSizeConstraints(
        ScaleUi(620.0f, 430.0f),
        ImVec2(viewport->WorkSize.x * 0.94f, viewport->WorkSize.y * 0.94f));
    ImGui::SetNextWindowSize(
        ImVec2(viewport->WorkSize.x * 0.72f, viewport->WorkSize.y * 0.78f),
        ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        regexReferenceOpen_ = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextWrapped("%s", ui.Text(UiText::UnwantedRegexReferenceHint));
    const std::string searchHint = std::string(ui_icons::Search) + " " + ui.Text(UiText::UnwantedRegexReferenceSearch);
    ImGui::SetNextItemWidth(-1.0f);
    InputTextWithHintString(
        "##unwanted_regex_reference_search",
        searchHint.c_str(),
        regexReferenceSearch_,
        ImGuiInputTextFlags_AutoSelectAll,
        256);

    const std::string loweredSearch = Utf8ToLower(TrimAscii(regexReferenceSearch_));
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp
        | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerH
        | ImGuiTableFlags_BordersInnerV
        | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_NoSavedSettings;
    bool anyVisible = false;
    if (ImGui::BeginTable("##unwanted_regex_reference_table", 4, flags, ImVec2(0.0f, -ScaleUi(42.0f)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(ui.Text(UiText::UnwantedRegexReferenceCategory), ImGuiTableColumnFlags_WidthFixed, ScaleUi(105.0f));
        ImGui::TableSetupColumn(ui.Text(UiText::UnwantedRegexReferenceExpression), ImGuiTableColumnFlags_WidthStretch, 0.85f);
        ImGui::TableSetupColumn(ui.Text(UiText::UnwantedRegexReferenceDescription), ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("##append", ImGuiTableColumnFlags_WidthFixed, ScaleUi(105.0f));
        ImGui::TableHeadersRow();

        for (const RegexReferenceItem& item : kRegexReferenceItems) {
            std::string searchable = std::string(item.expression)
                + "\n" + ui.Text(item.category)
                + "\n" + ui.Text(item.description);
            searchable = Utf8ToLower(searchable);
            if (!loweredSearch.empty() && searchable.find(loweredSearch) == std::string::npos) {
                continue;
            }
            anyVisible = true;
            ImGui::PushID(item.expression);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", ui.Text(item.category));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", item.expression);
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("%s", ui.Text(UiText::UnwantedCopy));
            }
            if (ImGui::IsItemClicked()) {
                ImGui::SetClipboardText(item.expression);
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::TextWrapped("%s", ui.Text(item.description));
            ImGui::TableSetColumnIndex(3);
            if (ImGui::Button(ui.Text(UiText::UnwantedRegexReferenceAppend), ImVec2(-1.0f, 0.0f))) {
                ruleDraft_.type = RuleType::Regex;
                ruleDraft_.wholeWord = false;
                ruleDraft_.text += item.expression;
                ruleDraft_.dirty = true;
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

    if (ImGui::Button(ui.Text(UiText::Close), ScaleUi(120.0f, 0.0f))) {
        regexReferenceOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void UnwantedMessagesModule::DrawRuleEditorPopup() {
    UiSettings& ui = UiSettings::Instance();
    const std::string title = std::string(ui.Text(UiText::UnwantedEditRuleTitle)) + "###unwanted_rule_editor_modal";
    if (editPopupRequestOpen_) {
        editPopupRequestOpen_ = false;
        editPopupOpen_ = true;
        ImGui::OpenPopup(title.c_str());
    }
    if (editPopupForceClose_) {
        if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        editPopupForceClose_ = false;
        return;
    }
    if (!editPopupOpen_) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSizeConstraints(
        ScaleUi(620.0f, 480.0f),
        ImVec2(viewport->WorkSize.x * 0.92f, viewport->WorkSize.y * 0.92f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.78f, viewport->WorkSize.y * 0.82f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    const auto requestClose = [&] {
        if (ruleDraft_.dirty) {
            pendingPageAfterDiscard_ = Page::Rules;
            unsavedConfirmOpen_ = true;
            return false;
        }
        editPopupOpen_ = false;
        ruleDraft_ = {};
        ImGui::CloseCurrentPopup();
        return true;
    };
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !regexReferenceOpen_) {
        if (requestClose()) {
            ImGui::EndPopup();
            return;
        }
    }
    ImGui::SetCursorPosX(std::max(
        ImGui::GetCursorPosX(),
        ImGui::GetWindowWidth() - ScaleUi(132.0f)));
    if (UnwantedTextButton(ui_icons::Xmark, ui.Text(UiText::Close), "##unwanted_close_editor", ScaleUi(112.0f, 0.0f))) {
        if (requestClose()) {
            ImGui::EndPopup();
            return;
        }
    }
    ImGui::Separator();

    if (ImGui::BeginChild("##unwanted_edit_workspace", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        DrawRegexHelperWizard();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        DrawRuleEditor(true);
    }
    ImGui::EndChild();
    ImGui::EndPopup();
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
    if (ImGui::Button(ui.Text(UiText::UnwantedDiscard), ScaleUi(150.0f, 0.0f))) {
        editPopupForceClose_ = editPopupOpen_;
        ruleDraft_ = {};
        editPopupOpen_ = false;
        regexReferenceOpen_ = false;
        page_ = pendingPageAfterDiscard_;
        if (reloadAfterDiscard_) {
            reloadAfterDiscard_ = false;
            reloadRequested_ = true;
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::Cancel), ScaleUi(150.0f, 0.0f))) {
        reloadAfterDiscard_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void UnwantedMessagesModule::DrawSettingsPopup() {
    UiSettings& ui = UiSettings::Instance();
    if (!ImGui::BeginPopup("##unwanted_settings_popup")) {
        return;
    }

    bool changed = false;
    ImGui::Text("%s", ui.Text(UiText::UnwantedCompatibility));
    const bool previousChatAsiCompatibility = settings_.chatAsiCompatibility;
    changed |= ImGui::Checkbox(
        ui.Text(UiText::UnwantedChatAsiCompatibility),
        &settings_.chatAsiCompatibility);
    DrawUnwantedTooltip(ui.Text(UiText::UnwantedChatAsiCompatibilityHelp));
    const bool chatAsiCompatibilityChanged =
        previousChatAsiCompatibility != settings_.chatAsiCompatibility;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("%s", ui.Text(UiText::UnwantedNormalizer));
    ImGui::TextWrapped("%s", ui.Text(UiText::UnwantedNormalizerDesc));
    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedStripColors), &settings_.normalizer.stripColors);
    DrawUnwantedTooltip(ui.Text(UiText::UnwantedStripColorsHelp));
    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedCollapseWhitespace), &settings_.normalizer.collapseWhitespace);
    DrawUnwantedTooltip(ui.Text(UiText::UnwantedCollapseWhitespaceHelp));
    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedTrim), &settings_.normalizer.trim);
    DrawUnwantedTooltip(ui.Text(UiText::UnwantedTrimHelp));
    ImGui::SetNextItemWidth(ScaleUi(130.0f));
    const int previousMaxPatternLength = settings_.maxPatternLength;
    changed |= ImGui::InputInt(ui.Text(UiText::UnwantedMaxPatternLength), &settings_.maxPatternLength, 0, 0);
    settings_.maxPatternLength = std::clamp(settings_.maxPatternLength, 1, kMaxConfigPatternLength);
    const bool patternLimitChanged = previousMaxPatternLength != settings_.maxPatternLength;

    ImGui::Separator();
    if (ImGui::Button(ui.Text(UiText::UnwantedReload), ScaleUi(150.0f, 0.0f))) {
        if (ruleDraft_.active && ruleDraft_.dirty) {
            pendingPageAfterDiscard_ = page_;
            reloadAfterDiscard_ = true;
            unsavedConfirmOpen_ = true;
        } else {
            reloadRequested_ = true;
        }
    }

    if (changed) {
        if (chatAsiCompatibilityChanged) {
            ApplyChatAsiCompatibilitySetting();
        }
        if (patternLimitChanged) {
            CompileRules();
        }
        PublishRuntimeSnapshot();
        RegenerateHelperOutput();
        SaveSettings();
    }
    ImGui::EndPopup();
}

void UnwantedMessagesModule::DrawDeleteConfirmPopup() {
    UiSettings& ui = UiSettings::Instance();
    const std::string popupTitle = std::string(ui.Text(UiText::UnwantedDeleteSelected)) + "###unwanted_delete_selected_confirm";
    if (deleteSelectedConfirmOpen_) {
        ImGui::OpenPopup(popupTitle.c_str());
    }

    if (!ImGui::BeginPopupModal(popupTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextWrapped(
        "%s",
        ui.Format(UiText::UnwantedDeleteSelectedQuestion, std::to_string(selectedRuleIds_.size()).c_str()).c_str());
    if (ImGui::Button(ui.Text(UiText::Delete), ScaleUi(120.0f, 0.0f))) {
        DeleteSelectedRules();
        deleteSelectedConfirmOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::Cancel), ScaleUi(120.0f, 0.0f))) {
        deleteSelectedConfirmOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

std::string UnwantedMessagesModule::AddRule(
    RuleType type,
    std::string text,
    bool nocase,
    bool wholeWord,
    std::string name) {
    if (text.empty()) {
        return {};
    }

    Rule rule;
    rule.id = AllocateRuleId();
    rule.name = TrimAscii(name);
    const std::string id = rule.id;
    rule.enabled = true;
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
        editPopupForceClose_ = editPopupOpen_;
        editPopupOpen_ = false;
        editPopupRequestOpen_ = false;
        regexReferenceOpen_ = false;
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

void UnwantedMessagesModule::SelectAllRules() {
    selectedRuleIds_.clear();
    for (const Rule& rule : rules_) {
        selectedRuleIds_.insert(rule.id);
    }
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

void UnwantedMessagesModule::StartCreateRule(std::string text, RuleType type) {
    activeRuleId_.clear();
    helperSample_.clear();
    helperExact_.clear();
    helperGeneralized_.clear();
    helperContains_.clear();
    helperTokens_.clear();
    helperWarning_.clear();
    helperGeneralizedValid_ = false;
    helperExactValid_ = false;
    helperContainsValid_ = false;
    ruleDraft_ = {};
    ruleDraft_.active = true;
    ruleDraft_.createMode = true;
    ruleDraft_.enabled = true;
    ruleDraft_.type = type;
    ruleDraft_.text = std::move(text);
    ruleDraft_.wholeWord = false;
    ruleDraft_.dirty = !ruleDraft_.text.empty();
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
    ruleDraft_.enabled = rule->enabled;
    ruleDraft_.type = rule->type;
    ruleDraft_.text = rule->text;
    ruleDraft_.nocase = rule->nocase;
    ruleDraft_.wholeWord = rule->wholeWord;
    ruleDraft_.dirty = false;
    helperSample_.clear();
    RegenerateHelperOutput();
    draftValidationCache_.ready = false;
    editPopupRequestOpen_ = true;
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
            ruleDraft_.nocase,
            ruleDraft_.wholeWord,
            ruleDraft_.name);
        if (id.empty()) {
            return false;
        }
        activeRuleId_ = id;
        scrollToRuleId_ = id;
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
    ruleDraft_ = {};
    editPopupOpen_ = false;
    regexReferenceOpen_ = false;
    ImGui::CloseCurrentPopup();
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
        if (RegexContainsBroadWildcard(text)) {
            AppendWarning(warning, ui.Text(UiText::UnwantedRegexBroadWildcard));
        }
        unwanted_regex::CompileResult compiled = unwanted_regex::Compile(text, ruleDraft_.nocase);
        if (!compiled.program) {
            error = ui.Format(
                UiText::UnwantedPcreErrorFormat,
                FormatPcrePosition(text, compiled.errorOffset).c_str());
        } else if (compiled.program->MatchesEmpty()) {
            AppendWarning(warning, ui.Text(UiText::UnwantedRegexMatchesEmpty));
        }
    }
    draftValidationCache_.error = error;
    draftValidationCache_.warning = warning;
    draftValidationCache_.valid = error.empty();
    return draftValidationCache_.valid;
}

std::size_t UnwantedMessagesModule::DuplicateRuleCount() const {
    return static_cast<std::size_t>(std::count_if(
        rules_.begin(),
        rules_.end(),
        [](const Rule& rule) { return rule.duplicate; }));
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
    case RuleFilter::Regex:
        if (rule.type != RuleType::Regex) {
            return false;
        }
        break;
    case RuleFilter::Literal:
        if (rule.type != RuleType::Literal) {
            return false;
        }
        break;
    case RuleFilter::Errors:
        if (rule.error.empty()) {
            return false;
        }
        break;
    case RuleFilter::All:
    default:
        break;
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
    options.colors = helperColors_ && !settings_.normalizer.stripColors;
    options.playerIds = helperPlayerId_;
    options.bracketPrefixes = helperBracketTag_;
    options.nicknames = helperNick_;
    options.numbers = helperNumbers_;
    options.money = helperMoney_;
    options.time = helperTime_;
    options.domains = helperDomain_;
    const std::string normalized = NormalizeCandidate(helperSample_);
    const unwanted_regex_builder::Result built = unwanted_regex_builder::Build(normalized, options);
    helperExact_ = built.exact;
    helperGeneralized_ = built.recommended;
    helperContains_ = built.contains;
    helperTokens_ = built.tokens;
    helperWarning_.clear();
    helperGeneralizedValid_ = false;
    helperExactValid_ = false;
    helperContainsValid_ = false;
    UiSettings& ui = UiSettings::Instance();
    if (!built.error.empty()) {
        helperWarning_ = ui.Text(UiText::UnwantedHelperInvalidUtf8);
        helperExact_.clear();
        helperGeneralized_.clear();
        helperContains_.clear();
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
    helperExactValid_ = matchesSource(helperExact_);
    helperGeneralizedValid_ = matchesSource(helperGeneralized_);
    helperContainsValid_ = matchesSource(helperContains_);
    if (!helperGeneralizedValid_ && helperExactValid_) {
        helperGeneralized_ = helperExact_;
        helperGeneralizedValid_ = true;
        helperContains_ = helperExact_.size() >= 4
            ? helperExact_.substr(2, helperExact_.size() - 4)
            : std::string{};
        helperContainsValid_ = matchesSource(helperContains_);
        helperWarning_ = ui.Text(UiText::UnwantedHelperExactFallback);
    }
}
