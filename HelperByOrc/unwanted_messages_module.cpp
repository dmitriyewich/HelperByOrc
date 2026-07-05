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
constexpr int kUnwantedSchemaVersion = 1;
constexpr int kMaxConfigPatternLength = 65535;
constexpr std::uint64_t kPerfTelemetryWindowMs = 5000;

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
    if (value.empty()) {
        return;
    }
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
}

const char* RuleTypeName(UnwantedMessagesModule::RuleType type) {
    return type == UnwantedMessagesModule::RuleType::Regex ? "regex" : "literal";
}

std::string SourceLabel(UnwantedMessageSource source) {
    switch (source) {
    case UnwantedMessageSource::CChatAddEntry:
        return "CChat::AddEntry";
    case UnwantedMessageSource::CChatAddMessage:
        return "CChat::AddMessage";
    case UnwantedMessageSource::CChatAddChatMessage:
        return "CChat::AddChatMessage";
    case UnwantedMessageSource::RakClientMessage:
        return "RPC ClientMessage";
    case UnwantedMessageSource::RakChat:
        return "RPC Chat";
    case UnwantedMessageSource::RakChatBubble:
        return "RPC ChatBubble";
    default:
        return "unknown";
    }
}

std::string EscapeRegex(std::string_view value) {
    std::string out;
    out.reserve(value.size() * 2);
    for (const char ch : value) {
        switch (ch) {
        case '\\':
        case '^':
        case '$':
        case '.':
        case '|':
        case '?':
        case '*':
        case '+':
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
            out.push_back('\\');
            break;
        default:
            break;
        }
        out.push_back(ch);
    }
    return out;
}

bool IsRegexEscaped(std::string_view value, std::size_t offset) {
    if (offset == 0 || offset > value.size()) {
        return false;
    }

    std::size_t slashCount = 0;
    for (std::size_t i = offset; i > 0 && value[i - 1] == '\\'; --i) {
        ++slashCount;
    }
    return slashCount % 2 != 0;
}

bool IsRegexQuantifier(char ch) {
    return ch == '*' || ch == '+' || ch == '?';
}

bool RegexHasRepeatedWildcard(std::string_view pattern) {
    int wildcardRuns = 0;
    for (std::size_t i = 0; i + 1 < pattern.size(); ++i) {
        if (IsRegexEscaped(pattern, i)) {
            continue;
        }
        if (pattern[i] == '.' && (pattern[i + 1] == '*' || pattern[i + 1] == '+')) {
            ++wildcardRuns;
            if (wildcardRuns >= 2) {
                return true;
            }
            ++i;
        }
    }
    return false;
}

bool RegexHasNestedQuantifier(std::string_view pattern) {
    struct GroupState {
        bool hasQuantifier = false;
        bool hasWildcardQuantifier = false;
    };

    std::vector<GroupState> stack;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char ch = pattern[i];
        if (IsRegexEscaped(pattern, i)) {
            continue;
        }

        if (ch == '(') {
            stack.push_back({});
            continue;
        }
        if (ch == '.' && i + 1 < pattern.size() && (pattern[i + 1] == '*' || pattern[i + 1] == '+') && !stack.empty()) {
            stack.back().hasQuantifier = true;
            stack.back().hasWildcardQuantifier = true;
            continue;
        }
        if ((IsRegexQuantifier(ch) || ch == '{') && !stack.empty()) {
            stack.back().hasQuantifier = true;
            continue;
        }
        if (ch != ')' || stack.empty()) {
            continue;
        }

        const bool innerHasWildcardQuantifier = stack.back().hasWildcardQuantifier;
        stack.pop_back();
        if (!innerHasWildcardQuantifier) {
            continue;
        }

        const std::size_t next = i + 1;
        if (next < pattern.size() && !IsRegexEscaped(pattern, next) && (IsRegexQuantifier(pattern[next]) || pattern[next] == '{')) {
            return true;
        }
    }
    return false;
}

bool RegexLooksTooBroad(std::string_view pattern) {
    if (pattern == ".*" || pattern == ".+") {
        return true;
    }
    if (!pattern.empty() && !IsRegexEscaped(pattern, 0) && pattern.rfind(".*", 0) == 0 && pattern.size() <= 8) {
        return true;
    }
    return false;
}

bool StartsBracketTag(std::string_view value, std::size_t offset, std::size_t& consumed) {
    consumed = 0;
    if (offset != 0 || value.empty() || value.front() != '[') {
        return false;
    }

    const std::size_t close = value.find(']', 1);
    if (close == std::string_view::npos || close <= 1) {
        return false;
    }

    consumed = close + 1;
    return true;
}

bool StartsTime(std::string_view value, std::size_t offset, std::size_t& consumed) {
    consumed = 0;
    if (offset + 5 > value.size()) {
        return false;
    }
    if (std::isdigit(static_cast<unsigned char>(value[offset])) == 0
        || std::isdigit(static_cast<unsigned char>(value[offset + 1])) == 0
        || value[offset + 2] != ':'
        || std::isdigit(static_cast<unsigned char>(value[offset + 3])) == 0
        || std::isdigit(static_cast<unsigned char>(value[offset + 4])) == 0) {
        return false;
    }
    consumed = 5;
    return true;
}

bool StartsMoney(std::string_view value, std::size_t offset, std::size_t& consumed) {
    consumed = 0;
    if (offset + 2 > value.size() || value[offset] != '$' || std::isdigit(static_cast<unsigned char>(value[offset + 1])) == 0) {
        return false;
    }

    std::size_t end = offset + 2;
    while (end < value.size()) {
        const unsigned char ch = static_cast<unsigned char>(value[end]);
        if (std::isdigit(ch) == 0 && value[end] != '.' && value[end] != ',') {
            break;
        }
        ++end;
    }

    consumed = end - offset;
    return true;
}

bool StartsNumber(std::string_view value, std::size_t offset, std::size_t& consumed) {
    consumed = 0;
    if (offset >= value.size() || std::isdigit(static_cast<unsigned char>(value[offset])) == 0) {
        return false;
    }

    std::size_t end = offset + 1;
    while (end < value.size()) {
        const unsigned char ch = static_cast<unsigned char>(value[end]);
        if (std::isdigit(ch) == 0 && value[end] != '.' && value[end] != ',') {
            break;
        }
        ++end;
    }

    if (end < value.size() && (value[end] == 'k' || value[end] == 'K')) {
        const std::size_t suffixStart = end + 1;
        end = suffixStart;
        while (end < value.size()) {
            const unsigned char ch = static_cast<unsigned char>(value[end]);
            if (std::isdigit(ch) == 0 && value[end] != '.' && value[end] != ',') {
                break;
            }
            ++end;
        }
        if (end == suffixStart) {
            end = suffixStart;
        }
    }

    consumed = end - offset;
    return true;
}

bool StartsPlayerIdTag(std::string_view value, std::size_t offset, std::size_t& consumed) {
    consumed = 0;
    if (offset + 3 > value.size() || value[offset] != '[' || std::isdigit(static_cast<unsigned char>(value[offset + 1])) == 0) {
        return false;
    }

    std::size_t end = offset + 2;
    while (end < value.size() && std::isdigit(static_cast<unsigned char>(value[end])) != 0) {
        ++end;
    }
    if (end >= value.size() || value[end] != ']') {
        return false;
    }

    consumed = end - offset + 1;
    return true;
}

bool StartsDomain(std::string_view value, std::size_t offset, std::size_t& consumed) {
    consumed = 0;
    if (offset >= value.size() || !std::isalnum(static_cast<unsigned char>(value[offset]))) {
        return false;
    }

    std::size_t end = offset;
    bool hasDot = false;
    while (end < value.size()) {
        const unsigned char ch = static_cast<unsigned char>(value[end]);
        if (std::isalnum(ch) != 0 || value[end] == '-' || value[end] == '.') {
            hasDot = hasDot || value[end] == '.';
            ++end;
            continue;
        }
        break;
    }

    if (!hasDot || end <= offset + 3) {
        return false;
    }

    const std::size_t dot = value.rfind('.', end - 1);
    if (dot == std::string_view::npos || dot <= offset || end - dot < 3) {
        return false;
    }
    for (std::size_t i = dot + 1; i < end; ++i) {
        if (std::isalpha(static_cast<unsigned char>(value[i])) == 0) {
            return false;
        }
    }

    while (end < value.size()) {
        const unsigned char ch = static_cast<unsigned char>(value[end]);
        if (std::isspace(ch) != 0 || value[end] == ')' || value[end] == ']' || value[end] == '}') {
            break;
        }
        ++end;
    }

    consumed = end - offset;
    return true;
}

bool IsAsciiNickChar(unsigned char ch) {
    return std::isalnum(ch) != 0 || ch == '_';
}

bool StartsNick(std::string_view value, std::size_t offset, std::size_t& consumed) {
    consumed = 0;
    if (offset >= value.size() || !IsAsciiNickChar(static_cast<unsigned char>(value[offset]))) {
        return false;
    }

    std::size_t end = offset;
    bool hasUnderscore = false;
    while (end < value.size() && IsAsciiNickChar(static_cast<unsigned char>(value[end]))) {
        hasUnderscore = hasUnderscore || value[end] == '_';
        ++end;
    }

    if (!hasUnderscore || end == offset) {
        return false;
    }

    consumed = end - offset;
    return true;
}

} // namespace

void UnwantedMessagesModule::OnProcessAttach() {
    ReloadConfig();
    debuglog::WriteInfo("UnwantedMessagesModule::OnProcessAttach rules=%zu", rules_.size());
}

void UnwantedMessagesModule::Shutdown() {
    std::lock_guard lock(mutex_);
    rules_.clear();
    selectedRuleIds_.clear();
    miscPageOpen_ = false;
}

void UnwantedMessagesModule::ReloadConfig() {
    const jsonutil::JsonObject section = AppConfig::Instance().ReadSectionObject(kUnwantedSectionName);

    {
        std::lock_guard lock(mutex_);
        LoadFromConfig(section);
        CompileRules();
    }

    debuglog::WriteInfo("UnwantedMessagesModule::ReloadConfig done");
}

bool UnwantedMessagesModule::ShouldBlock(const UnwantedMessageContext& context) {
    std::optional<PerfLogSnapshot> perfLog;
    bool blocked = false;

    {
        std::lock_guard lock(mutex_);
        if (!settings_.enabled || context.text.empty()) {
            return false;
        }

        const double beginMs = UnwantedPerfNowMs();
        const std::vector<std::string> candidates = BuildCandidates(context);
        std::size_t enabledRules = 0;
        std::size_t regexRules = 0;
        for (const Rule& rule : rules_) {
            if (!rule.enabled || !rule.error.empty()) {
                continue;
            }

            ++enabledRules;
            if (rule.type == RuleType::Regex) {
                ++regexRules;
            }
        }

        MatchResult result;
        blocked = MatchCandidates(candidates, context.source, &result);
        if (blocked) {
            ++blockedCount_;
            lastBlocked_ = std::move(result);
        }

        const double elapsedMs = UnwantedPerfNowMs() - beginMs;
        perfLog = AccumulatePerfStats(elapsedMs, candidates.size(), enabledRules, regexRules, blocked);
    }

    if (perfLog) {
        debuglog::WriteInfo(
            "[unwanted][perf] window=%llums messages=%llu blocked=%llu candidates=%llu maxCandidates=%zu rules=%zu regexRules=%zu ruleChecksUpper=%llu avg=%.3fms max=%.3fms",
            static_cast<unsigned long long>(perfLog->windowMs),
            static_cast<unsigned long long>(perfLog->messages),
            static_cast<unsigned long long>(perfLog->blocked),
            static_cast<unsigned long long>(perfLog->candidates),
            perfLog->maxCandidates,
            perfLog->maxRules,
            perfLog->maxRegexRules,
            static_cast<unsigned long long>(perfLog->ruleChecks),
            perfLog->avgMs,
            perfLog->maxMs);
    }

    return blocked;
}

std::optional<UnwantedMessagesModule::PerfLogSnapshot> UnwantedMessagesModule::AccumulatePerfStats(
    double elapsedMs,
    std::size_t candidateCount,
    std::size_t enabledRules,
    std::size_t regexRules,
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
    perfStats_.ruleChecks += static_cast<std::uint64_t>(candidateCount) * static_cast<std::uint64_t>(enabledRules);
    perfStats_.totalMs += elapsedMs;
    perfStats_.maxMs = std::max(perfStats_.maxMs, elapsedMs);
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
    snapshot.avgMs = perfStats_.messages > 0
        ? perfStats_.totalMs / static_cast<double>(perfStats_.messages)
        : 0.0;
    snapshot.maxMs = perfStats_.maxMs;
    snapshot.maxCandidates = perfStats_.maxCandidates;
    snapshot.maxRules = perfStats_.maxRules;
    snapshot.maxRegexRules = perfStats_.maxRegexRules;

    perfStats_ = {};
    return snapshot;
}

bool UnwantedMessagesModule::IsMiscPageOpen() const {
    return miscPageOpen_;
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
        miscPageOpen_ = true;
        return true;
    }
    return false;
}

void UnwantedMessagesModule::DrawMainPage() {
    bool reload = false;
    {
        std::lock_guard lock(mutex_);
        EnsureActiveRule();
        DrawHeader(reload);

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const bool wideLayout = avail.x >= ScaleUi(1080.0f);
        if (wideLayout) {
            const float gap = ScaleUi(10.0f);
            const float leftWidth = std::clamp(avail.x * 0.58f, ScaleUi(620.0f), avail.x - ScaleUi(440.0f));
            const float rightWidth = std::max(ScaleUi(380.0f), avail.x - leftWidth - gap);
            DrawRulesPane(ImVec2(leftWidth, avail.y));
            ImGui::SameLine(0.0f, gap);
            DrawInspectorPane(ImVec2(rightWidth, avail.y));
        } else {
            const float listHeight = std::min(std::max(ScaleUi(310.0f), avail.y * 0.48f), std::max(ScaleUi(280.0f), avail.y - ScaleUi(280.0f)));
            DrawRulesPane(ImVec2(0.0f, listHeight));
            ImGui::Spacing();
            DrawInspectorPane(ImVec2(0.0f, ImGui::GetContentRegionAvail().y));
        }

        DrawDeleteConfirmPopup();
    }

    if (reload) {
        ReloadConfig();
    }
}

jsonutil::JsonValue UnwantedMessagesModule::SerializeConfig() const {
    jsonutil::JsonObject root;
    root["schema_version"] = kUnwantedSchemaVersion;

    jsonutil::JsonObject settings;
    settings["enabled"] = settings_.enabled;
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
        item["enabled"] = rule.enabled;
        item["type"] = RuleTypeName(rule.type);
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
    ruleDraft_ = {};
    lastTesterMatch_ = {};
    nextRuleSerial_ = 1;

    const jsonutil::JsonObject* settings = jsonutil::JsonObjectOrNull(&section, "settings");
    settings_.enabled = jsonutil::JsonBoolOr(settings, "enabled", true);
    settings_.maxPatternLength = std::clamp(
        jsonutil::JsonNumberOr(settings, "max_pattern_len", 2048),
        1,
        kMaxConfigPatternLength);

    const jsonutil::JsonObject* normalizer = jsonutil::JsonObjectOrNull(settings, "normalizer");
    settings_.normalizer.stripColors = jsonutil::JsonBoolOr(normalizer, "strip_colors", false);
    settings_.normalizer.collapseWhitespace = jsonutil::JsonBoolOr(normalizer, "collapse_ws", false);
    settings_.normalizer.trim = jsonutil::JsonBoolOr(normalizer, "trim", false);

    const jsonutil::JsonArray* rules = jsonutil::JsonArrayOrNull(&section, "rules");
    if (!rules) {
        return;
    }

    for (const jsonutil::JsonValue& value : *rules) {
        const jsonutil::JsonObject* object = value.TryObject();
        if (!object) {
            continue;
        }

        Rule rule;
        rule.id = jsonutil::JsonStringOr(object, "id", {});
        if (rule.id.empty()) {
            rule.id = AllocateRuleId();
        } else if (rule.id.rfind("unwanted-", 0) == 0) {
            const std::string suffix = rule.id.substr(9);
            char* end = nullptr;
            const unsigned long long value = std::strtoull(suffix.c_str(), &end, 10);
            if (end && *end == '\0' && value >= nextRuleSerial_) {
                nextRuleSerial_ = value + 1;
            }
        }
        rule.enabled = jsonutil::JsonBoolOr(object, "enabled", true);
        const std::string type = jsonutil::JsonStringOr(object, "type", "literal");
        rule.type = type == "regex" ? RuleType::Regex : RuleType::Literal;
        rule.text = jsonutil::JsonStringOr(object, "text", {});
        rule.nocase = jsonutil::JsonBoolOr(object, "nocase", false);
        rule.wholeWord = jsonutil::JsonBoolOr(object, "whole_word", false);
        rules_.push_back(std::move(rule));
    }
}

void UnwantedMessagesModule::SaveConfig() const {
    AppConfig::Instance().QueueSectionReplace(std::string(kUnwantedSectionName), SerializeConfig());
}

void UnwantedMessagesModule::CompileRules() {
    for (Rule& rule : rules_) {
        rule.error.clear();
        rule.warning.clear();
        rule.compiledRegex.reset();

        if (static_cast<int>(rule.text.size()) > settings_.maxPatternLength) {
            rule.error = UiSettings::Instance().Format(
                UiText::UnwantedErrorTooLong,
                std::to_string(settings_.maxPatternLength).c_str());
            continue;
        }

        if (rule.text.empty()) {
            rule.error = UiSettings::Instance().Text(UiText::UnwantedErrorEmpty);
            continue;
        }

        if (rule.type != RuleType::Regex) {
            continue;
        }

        if (RegexLooksTooBroad(rule.text) || RegexHasRepeatedWildcard(rule.text) || RegexHasNestedQuantifier(rule.text)) {
            rule.error = UiSettings::Instance().Text(UiText::UnwantedRegexSafetyBlocked);
            continue;
        }
        if (!rule.text.empty() && rule.text.front() != '^' && rule.text.back() != '$') {
            rule.warning = UiSettings::Instance().Text(UiText::UnwantedRegexSafetyUnanchored);
        }

        try {
            auto flags = std::regex_constants::ECMAScript | std::regex_constants::optimize;
            if (rule.nocase) {
                flags |= std::regex_constants::icase;
            }
            rule.compiledRegex.emplace(rule.text, flags);
        } catch (const std::regex_error& error) {
            rule.error = error.what();
        }
    }
}

std::string UnwantedMessagesModule::AllocateRuleId() {
    return "unwanted-" + std::to_string(nextRuleSerial_++);
}

std::vector<std::string> UnwantedMessagesModule::BuildCandidates(const UnwantedMessageContext& context) const {
    std::vector<std::string> candidates;
    AddUnique(candidates, NormalizeCandidate(context.text));

    if (!context.prefix.empty()) {
        AddUnique(candidates, NormalizeCandidate(context.prefix + " " + context.text));
    }

    if (context.playerId >= 0) {
        const std::string id = std::to_string(context.playerId);
        const bool hasName = !context.playerName.empty() && context.playerName != "UNKNOWN";
        if (hasName) {
            AddUnique(candidates, NormalizeCandidate(context.playerName + "[" + id + "]: " + context.text));
            AddUnique(candidates, NormalizeCandidate(context.playerName + ": " + context.text));
            AddUnique(candidates, NormalizeCandidate(context.playerName + " " + context.text));
        }
        AddUnique(candidates, NormalizeCandidate("[" + id + "] " + context.text));
        AddUnique(candidates, NormalizeCandidate(id + " " + context.text));
    }

    return candidates;
}

std::string UnwantedMessagesModule::NormalizeCandidate(std::string_view text) const {
    std::string result(text);
    if (settings_.normalizer.stripColors) {
        result = StripColorTags(result);
    }
    if (settings_.normalizer.collapseWhitespace) {
        result = CollapseWhitespace(result);
    }
    if (settings_.normalizer.trim) {
        result = TrimAscii(result);
    }
    return result;
}

bool UnwantedMessagesModule::MatchCandidates(
    const std::vector<std::string>& candidates,
    UnwantedMessageSource source,
    MatchResult* result) const {
    for (const std::string& candidate : candidates) {
        for (const Rule& rule : rules_) {
            if (!rule.enabled || !rule.error.empty()) {
                continue;
            }
            if (!MatchRule(rule, candidate)) {
                continue;
            }

            if (result) {
                result->matched = true;
                result->ruleId = rule.id;
                result->ruleText = rule.text;
                result->candidate = candidate;
                result->source = source;
            }
            return true;
        }
    }
    return false;
}

bool UnwantedMessagesModule::MatchRule(const Rule& rule, std::string_view candidate) const {
    if (rule.type == RuleType::Literal) {
        return MatchLiteral(rule, candidate);
    }
    if (!rule.compiledRegex) {
        return false;
    }
    return std::regex_search(candidate.begin(), candidate.end(), *rule.compiledRegex);
}

bool UnwantedMessagesModule::MatchLiteral(const Rule& rule, std::string_view candidate) const {
    const std::string haystack = rule.nocase ? Utf8ToLower(candidate) : std::string(candidate);
    const std::string needle = rule.nocase ? Utf8ToLower(rule.text) : rule.text;
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

void UnwantedMessagesModule::DrawHeader(bool& reload) {
    UiSettings& ui = UiSettings::Instance();
    const UnwantedVisualStyle visual = UnwantedStyleTokens();
    const float height = ScaleUi(92.0f);

    if (!BeginUnwantedPanel("##unwanted_header", ImVec2(0.0f, height))) {
        EndUnwantedPanel();
        return;
    }

    if (UnwantedTextButton(ui_icons::ChevronLeft, ui.Text(UiText::EditorBack), "##unwanted_back", ScaleUi(112.0f, 0.0f))) {
        miscPageOpen_ = false;
        EndUnwantedPanel();
        return;
    }
    ImGui::SameLine();
    ImGui::Text("%s", ui.Text(UiText::UnwantedTitle));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ui.Text(UiText::TabMisc));

    const std::size_t invalid = std::count_if(rules_.begin(), rules_.end(), [](const Rule& rule) {
        return !rule.error.empty();
    });
    const std::size_t warnings = std::count_if(rules_.begin(), rules_.end(), [](const Rule& rule) {
        return rule.error.empty() && !rule.warning.empty();
    });
    const std::size_t enabled = std::count_if(rules_.begin(), rules_.end(), [](const Rule& rule) {
        return rule.enabled;
    });

    ImGui::SameLine();
    ImGui::TextColored(
        visual.mutedText,
        "%s",
        ui.Format(
            UiText::UnwantedStatsCompact,
            std::to_string(rules_.size()).c_str(),
            std::to_string(enabled).c_str(),
            std::to_string(invalid).c_str(),
            std::to_string(warnings).c_str(),
            std::to_string(blockedCount_).c_str()).c_str());

    ImGui::Spacing();
    bool enabledSetting = settings_.enabled;
    if (ImGui::Checkbox(ui.Text(UiText::UnwantedEnabled), &enabledSetting)) {
        settings_.enabled = enabledSetting;
        SaveConfig();
    }
    ImGui::SameLine();
    if (UnwantedTextButton(ui_icons::Plus, ui.Text(UiText::UnwantedNewRule), "##unwanted_new_rule", ScaleUi(136.0f, 0.0f))) {
        StartCreateRule();
    }
    ImGui::SameLine();
    if (UnwantedTextButton(ui_icons::Sliders, ui.Text(UiText::UnwantedTools), "##unwanted_tools", ScaleUi(132.0f, 0.0f))) {
        ImGui::OpenPopup("##unwanted_settings_popup");
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::UnwantedReload), ScaleUi(124.0f, 0.0f))) {
        reload = true;
    }

    if (lastBlocked_.matched) {
        ImGui::TextDisabled(
            "%s",
            ui.Format(
                UiText::UnwantedLastBlocked,
                SourceLabel(lastBlocked_.source).c_str(),
                lastBlocked_.ruleId.c_str(),
                lastBlocked_.candidate.c_str()).c_str());
    } else {
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedLastBlockedEmpty));
    }

    DrawSettingsPopup();
    EndUnwantedPanel();
    ImGui::Spacing();
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
    InputTextWithHintString("##unwanted_rule_search", searchHint.c_str(), ruleSearch_, ImGuiInputTextFlags_AutoSelectAll, 128);

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
        | ImGuiTableFlags_NoSavedSettings
        | ImGuiTableFlags_BordersInnerH;
    if (!ImGui::BeginTable("##unwanted_rules_table", 7, flags, ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
        return;
    }

    ImGui::TableSetupColumn("sel", ImGuiTableColumnFlags_WidthFixed, ScaleUi(28.0f));
    ImGui::TableSetupColumn("enabled", ImGuiTableColumnFlags_WidthFixed, ScaleUi(34.0f));
    ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed, ScaleUi(76.0f));
    ImGui::TableSetupColumn("text", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("flags", ImGuiTableColumnFlags_WidthFixed, ScaleUi(118.0f));
    ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthFixed, ScaleUi(86.0f));
    ImGui::TableSetupColumn("actions", ImGuiTableColumnFlags_WidthFixed, ScaleUi(44.0f));

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
                CompileRules();
                SaveConfig();
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
            const std::string label = (rule.text.empty() ? rule.id : rule.text) + "##rule_select";
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
                    SaveConfig();
                }
                if (ImGui::MenuItem(ui.Text(UiText::MoveDown), nullptr, false, index + 1 < rules_.size())) {
                    std::swap(rules_[index], rules_[index + 1]);
                    SaveConfig();
                }
                ImGui::Separator();
                if (ImGui::MenuItem(ui.Text(UiText::Delete))) {
                    DeleteRuleByIndex(index);
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

void UnwantedMessagesModule::DrawInspectorPane(const ImVec2& size) {
    if (!BeginUnwantedPanel("##unwanted_inspector_pane", size)) {
        EndUnwantedPanel();
        return;
    }

    DrawRegexHelperWizard();
    ImGui::Spacing();
    ImGui::Separator();
    DrawRuleEditor();
    ImGui::Spacing();
    ImGui::Separator();
    DrawTesterPanel();
    EndUnwantedPanel();
}

void UnwantedMessagesModule::DrawRuleEditor() {
    UiSettings& ui = UiSettings::Instance();
    const UnwantedVisualStyle visual = UnwantedStyleTokens();

    if (!ruleDraft_.active) {
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedNoSelection));
        if (ImGui::Button(ui.Text(UiText::UnwantedNewRule), ScaleUi(150.0f, 0.0f))) {
            StartCreateRule();
        }
        return;
    }

    ImGui::Text("%s", ui.Text(ruleDraft_.createMode ? UiText::UnwantedCreateRuleTitle : UiText::UnwantedEditRuleTitle));
    ImGui::SameLine();
    if (!ruleDraft_.createMode) {
        ImGui::TextDisabled("%s", ruleDraft_.id.c_str());
    }

    bool changed = false;
    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedRuleEnabled), &ruleDraft_.enabled);
    ImGui::SameLine();
    const char* typePreview = ruleDraft_.type == RuleType::Regex ? ui.Text(UiText::UnwantedTypeRegex) : ui.Text(UiText::UnwantedTypeLiteral);
    ImGui::SetNextItemWidth(ScaleUi(130.0f));
    if (ImGui::BeginCombo("##unwanted_draft_type", typePreview)) {
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
    ImGui::SameLine();
    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedNoCase), &ruleDraft_.nocase);
    ImGui::SameLine();
    ImGui::BeginDisabled(ruleDraft_.type != RuleType::Literal);
    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedWholeWord), &ruleDraft_.wholeWord);
    ImGui::EndDisabled();

    ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedRuleText));
    changed |= InputTextMultilineString(
        "##unwanted_draft_text",
        ruleDraft_.text,
        ImVec2(0.0f, ScaleUi(92.0f)),
        ImGuiInputTextFlags_None,
        1024);
    if (changed) {
        ruleDraft_.dirty = true;
    }

    std::string draftError;
    std::string draftWarning;
    const std::string trimmed = TrimAscii(ruleDraft_.text);
    if (trimmed.empty()) {
        draftError = ui.Text(UiText::UnwantedErrorEmpty);
    } else if (static_cast<int>(trimmed.size()) > settings_.maxPatternLength) {
        draftError = ui.Format(UiText::UnwantedErrorTooLong, std::to_string(settings_.maxPatternLength).c_str());
    } else if (ruleDraft_.type == RuleType::Regex) {
        if (RegexLooksTooBroad(trimmed) || RegexHasRepeatedWildcard(trimmed) || RegexHasNestedQuantifier(trimmed)) {
            draftError = ui.Text(UiText::UnwantedRegexSafetyBlocked);
        } else {
            if (trimmed.front() != '^' && trimmed.back() != '$') {
                draftWarning = ui.Text(UiText::UnwantedRegexSafetyUnanchored);
            }
            try {
                auto flags = std::regex_constants::ECMAScript | std::regex_constants::optimize;
                if (ruleDraft_.nocase) {
                    flags |= std::regex_constants::icase;
                }
                const std::regex compiled(trimmed, flags);
                (void)compiled;
            } catch (const std::regex_error& error) {
                draftError = error.what();
            }
        }
    }

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
            const int index = FindRuleIndexById(ruleDraft_.id);
            if (index >= 0) {
                DeleteRuleByIndex(static_cast<std::size_t>(index));
            }
        }
    }
}

void UnwantedMessagesModule::DrawTesterPanel() {
    UiSettings& ui = UiSettings::Instance();
    const UnwantedVisualStyle visual = UnwantedStyleTokens();

    ImGui::Text("%s", ui.Text(UiText::UnwantedTester));
    InputTextWithHintString("##unwanted_test_text", ui.Text(UiText::UnwantedTesterHint), testText_, ImGuiInputTextFlags_AutoSelectAll, 1024);
    const std::string normalized = NormalizeCandidate(testText_);
    if (!testText_.empty()) {
        ImGui::TextDisabled("%s", ui.Format(UiText::UnwantedTesterNormalizedFormat, normalized.c_str()).c_str());
    }

    if (UnwantedTextButton(ui_icons::Check, ui.Text(UiText::UnwantedTestAction), "##unwanted_test", ScaleUi(132.0f, 0.0f))) {
        UnwantedMessageContext context;
        context.source = UnwantedMessageSource::CChatAddEntry;
        context.text = testText_;
        MatchResult result;
        lastTesterMatch_ = {};
        if (MatchCandidates(BuildCandidates(context), context.source, &result)) {
            lastTesterMatch_ = std::move(result);
            selectedRuleIds_.clear();
            selectedRuleIds_.insert(lastTesterMatch_.ruleId);
            StartEditRule(lastTesterMatch_.ruleId);
        }
    }

    if (lastTesterMatch_.matched) {
        ImGui::TextColored(
            visual.ok,
            "%s",
            ui.Format(
                UiText::UnwantedTesterMatched,
                lastTesterMatch_.ruleId.c_str(),
                lastTesterMatch_.candidate.c_str()).c_str());
    } else {
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedTesterNoMatch));
    }
}

void UnwantedMessagesModule::DrawRegexHelperWizard() {
    UiSettings& ui = UiSettings::Instance();
    const UnwantedVisualStyle visual = UnwantedStyleTokens();

    ImGui::Text("%s", ui.Text(UiText::UnwantedRegexHelper));
    ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedHelperFlowHint));
    bool changed = InputTextWithHintString(
        "##unwanted_helper_sample",
        ui.Text(UiText::UnwantedHelperInputHint),
        helperSample_,
        ImGuiInputTextFlags_AutoSelectAll,
        1024);

    const auto drawOption = [&](bool& value, UiText label) {
        changed |= ImGui::Checkbox(ui.Text(label), &value);
        ImGui::SameLine();
    };
    drawOption(helperAnchors_, UiText::UnwantedHelperAnchors);
    drawOption(helperColors_, UiText::UnwantedHelperColors);
    drawOption(helperNumbers_, UiText::UnwantedHelperNumbers);
    drawOption(helperMoney_, UiText::UnwantedHelperMoney);
    drawOption(helperTime_, UiText::UnwantedHelperTime);
    drawOption(helperNick_, UiText::UnwantedHelperNick);
    drawOption(helperPlayerId_, UiText::UnwantedHelperPlayerId);
    drawOption(helperDomain_, UiText::UnwantedHelperDomain);
    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedHelperBracketTag), &helperBracketTag_);

    if (changed) {
        RegenerateHelperOutput();
    }
    if (!helperSample_.empty() && helperExact_.empty() && helperGeneralized_.empty() && helperContains_.empty()) {
        RegenerateHelperOutput();
    }

    const auto drawVariant = [&](UiText title, const std::string& pattern) {
        if (pattern.empty()) {
            return;
        }
        ImGui::PushID(static_cast<int>(title));
        ImGui::Spacing();
        ImGui::TextDisabled("%s", ui.Text(title));
        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped("%s", pattern.c_str());
        ImGui::PopTextWrapPos();

        if (RegexLooksTooBroad(pattern) || RegexHasRepeatedWildcard(pattern) || RegexHasNestedQuantifier(pattern)) {
            ImGui::TextColored(visual.danger, "%s", ui.Text(UiText::UnwantedRegexSafetyBlocked));
        } else if (!pattern.empty() && pattern.front() != '^' && pattern.back() != '$') {
            ImGui::TextColored(visual.warn, "%s", ui.Text(UiText::UnwantedRegexSafetyUnanchored));
        } else {
            ImGui::TextColored(visual.ok, "%s", ui.Text(UiText::UnwantedRuleOk));
        }

        if (ImGui::Button(ui.Text(UiText::UnwantedUseInDraft), ScaleUi(120.0f, 0.0f))) {
            StartCreateRule(pattern, RuleType::Regex);
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::UnwantedAddRuleAction), ScaleUi(120.0f, 0.0f))) {
            const std::string id = AddRule(RuleType::Regex, pattern, false, false);
            if (!id.empty()) {
                StartEditRule(id);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::UnwantedCopy), ScaleUi(108.0f, 0.0f))) {
            ImGui::SetClipboardText(pattern.c_str());
        }
        ImGui::PopID();
    };

    drawVariant(UiText::UnwantedHelperExact, helperExact_);
    drawVariant(UiText::UnwantedHelperGeneralized, helperGeneralized_);
    drawVariant(UiText::UnwantedHelperContains, helperContains_);
}

void UnwantedMessagesModule::DrawSettingsPopup() {
    UiSettings& ui = UiSettings::Instance();
    if (!ImGui::BeginPopup("##unwanted_settings_popup")) {
        return;
    }

    bool changed = false;
    ImGui::Text("%s", ui.Text(UiText::UnwantedNormalizer));
    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedStripColors), &settings_.normalizer.stripColors);
    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedCollapseWhitespace), &settings_.normalizer.collapseWhitespace);
    changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedTrim), &settings_.normalizer.trim);
    ImGui::SetNextItemWidth(ScaleUi(130.0f));
    changed |= ImGui::InputInt(ui.Text(UiText::UnwantedMaxPatternLength), &settings_.maxPatternLength, 0, 0);
    settings_.maxPatternLength = std::clamp(settings_.maxPatternLength, 1, kMaxConfigPatternLength);

    ImGui::Separator();
    if (ImGui::MenuItem(ui.Text(UiText::UnwantedEnableAll))) {
        SetAllRulesEnabled(true);
    }
    if (ImGui::MenuItem(ui.Text(UiText::UnwantedDisableAll))) {
        SetAllRulesEnabled(false);
    }
    if (ImGui::MenuItem(ui.Text(UiText::UnwantedRemoveDuplicates))) {
        RemoveDuplicateRules();
    }
    if (ImGui::MenuItem(ui.Text(UiText::UnwantedSortByType))) {
        SortRulesByType();
    }
    if (ImGui::MenuItem(ui.Text(UiText::UnwantedSortByText))) {
        SortRulesByText();
    }

    if (changed) {
        CompileRules();
        SaveConfig();
    }
    ImGui::EndPopup();
}

void UnwantedMessagesModule::DrawDeleteConfirmPopup() {
    UiSettings& ui = UiSettings::Instance();
    const std::string popupTitle = std::string(ui.Text(UiText::UnwantedDeleteSelected)) + "##unwanted_delete_selected_confirm";
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

std::string UnwantedMessagesModule::AddRule(RuleType type, std::string text, bool nocase, bool wholeWord) {
    if (text.empty()) {
        return {};
    }

    Rule rule;
    rule.id = AllocateRuleId();
    const std::string id = rule.id;
    rule.enabled = true;
    rule.type = type;
    rule.text = std::move(text);
    rule.nocase = nocase;
    rule.wholeWord = type == RuleType::Literal && wholeWord;
    rules_.push_back(std::move(rule));
    CompileRules();
    SaveConfig();
    return id;
}

void UnwantedMessagesModule::DeleteRuleByIndex(std::size_t index) {
    if (index >= rules_.size()) {
        return;
    }

    const std::string id = rules_[index].id;
    selectedRuleIds_.erase(rules_[index].id);
    rules_.erase(rules_.begin() + static_cast<std::ptrdiff_t>(index));
    if (activeRuleId_ == id || ruleDraft_.id == id) {
        activeRuleId_.clear();
        ruleDraft_ = {};
        EnsureActiveRule();
    }
    SaveConfig();
}

void UnwantedMessagesModule::DeleteSelectedRules() {
    if (selectedRuleIds_.empty()) {
        return;
    }

    rules_.erase(
        std::remove_if(rules_.begin(), rules_.end(), [this](const Rule& rule) {
            return selectedRuleIds_.find(rule.id) != selectedRuleIds_.end();
        }),
        rules_.end());
    if (selectedRuleIds_.find(activeRuleId_) != selectedRuleIds_.end()) {
        activeRuleId_.clear();
        ruleDraft_ = {};
    }
    selectedRuleIds_.clear();
    EnsureActiveRule();
    SaveConfig();
}

void UnwantedMessagesModule::SetAllRulesEnabled(bool enabled) {
    for (Rule& rule : rules_) {
        rule.enabled = enabled;
    }
    CompileRules();
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
    CompileRules();
    SaveConfig();
}

void UnwantedMessagesModule::RemoveDuplicateRules() {
    std::set<std::string> seen;
    rules_.erase(
        std::remove_if(rules_.begin(), rules_.end(), [&seen](const Rule& rule) {
            const std::string key = std::string(RuleTypeName(rule.type)) + "\n" + rule.text + "\n"
                + (rule.nocase ? "1" : "0") + "\n" + (rule.wholeWord ? "1" : "0");
            if (seen.find(key) != seen.end()) {
                return true;
            }
            seen.insert(key);
            return false;
        }),
        rules_.end());
    ClearSelection();
    EnsureActiveRule();
    CompileRules();
    SaveConfig();
}

void UnwantedMessagesModule::SortRulesByType() {
    std::stable_sort(rules_.begin(), rules_.end(), [](const Rule& lhs, const Rule& rhs) {
        if (lhs.type != rhs.type) {
            return lhs.type < rhs.type;
        }
        return Utf8ToLower(lhs.text) < Utf8ToLower(rhs.text);
    });
    SaveConfig();
}

void UnwantedMessagesModule::SortRulesByText() {
    std::stable_sort(rules_.begin(), rules_.end(), [](const Rule& lhs, const Rule& rhs) {
        const std::string left = Utf8ToLower(lhs.text);
        const std::string right = Utf8ToLower(rhs.text);
        if (left != right) {
            return left < right;
        }
        return lhs.type < rhs.type;
    });
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

void UnwantedMessagesModule::EnsureActiveRule() {
    if (ruleDraft_.createMode) {
        return;
    }
    if (!activeRuleId_.empty() && FindRuleById(activeRuleId_) != nullptr) {
        if (!ruleDraft_.active || ruleDraft_.id != activeRuleId_) {
            StartEditRule(activeRuleId_);
        }
        return;
    }

    activeRuleId_.clear();
    ruleDraft_ = {};
    if (!rules_.empty()) {
        StartEditRule(rules_.front().id);
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
    ruleDraft_ = {};
    ruleDraft_.active = true;
    ruleDraft_.createMode = true;
    ruleDraft_.enabled = true;
    ruleDraft_.type = type;
    ruleDraft_.text = std::move(text);
    ruleDraft_.wholeWord = false;
    ruleDraft_.dirty = !ruleDraft_.text.empty();
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
    ruleDraft_.enabled = rule->enabled;
    ruleDraft_.type = rule->type;
    ruleDraft_.text = rule->text;
    ruleDraft_.nocase = rule->nocase;
    ruleDraft_.wholeWord = rule->wholeWord;
    ruleDraft_.dirty = false;
}

bool UnwantedMessagesModule::SaveDraftRule() {
    if (!ruleDraft_.active) {
        return false;
    }

    std::string text = TrimAscii(ruleDraft_.text);
    if (text.empty()) {
        return false;
    }

    if (ruleDraft_.createMode) {
        const std::string id = AddRule(ruleDraft_.type, std::move(text), ruleDraft_.nocase, ruleDraft_.wholeWord);
        if (id.empty()) {
            return false;
        }
        StartEditRule(id);
        return true;
    }

    Rule* rule = FindRuleById(ruleDraft_.id);
    if (!rule) {
        return false;
    }

    rule->enabled = ruleDraft_.enabled;
    rule->type = ruleDraft_.type;
    rule->text = std::move(text);
    rule->nocase = ruleDraft_.nocase;
    rule->wholeWord = ruleDraft_.type == RuleType::Literal && ruleDraft_.wholeWord;
    CompileRules();
    SaveConfig();
    StartEditRule(rule->id);
    return true;
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

    std::string haystack = rule.id + "\n" + rule.text + "\n" + RuleTypeName(rule.type) + "\n" + rule.error + "\n" + rule.warning;
    haystack = Utf8ToLower(haystack);
    return haystack.find(loweredSearch) != std::string::npos;
}

void UnwantedMessagesModule::RegenerateHelperOutput() {
    helperExact_ = GenerateExactRegex(helperSample_);
    helperGeneralized_ = GenerateGeneralizedRegex(helperSample_);
    helperContains_ = GenerateContainsRegex(helperSample_);
}

std::string UnwantedMessagesModule::GenerateExactRegex(std::string_view sample) const {
    if (sample.empty()) {
        return {};
    }

    std::string out = EscapeRegex(sample);
    if (helperAnchors_) {
        out = "^" + out + "$";
    }
    return out;
}

std::string UnwantedMessagesModule::GenerateGeneralizedRegex(std::string_view sample) const {
    return GenerateGeneralizedRegex(sample, helperAnchors_);
}

std::string UnwantedMessagesModule::GenerateContainsRegex(std::string_view sample) const {
    return GenerateGeneralizedRegex(sample, false);
}

std::string UnwantedMessagesModule::GenerateGeneralizedRegex(std::string_view sample, bool anchors) const {
    if (sample.empty()) {
        return {};
    }

    std::string out;
    out.reserve(sample.size() * 2);

    for (std::size_t i = 0; i < sample.size();) {
        std::size_t consumed = 0;
        if (helperBracketTag_ && StartsBracketTag(sample, i, consumed)) {
            out += "\\[[^\\]]+\\]";
            i += consumed;
            continue;
        }
        if (helperPlayerId_ && StartsPlayerIdTag(sample, i, consumed)) {
            out += "\\[[0-9]+\\]";
            i += consumed;
            continue;
        }
        if (helperColors_ && TryColorTag(sample, i, consumed)) {
            out += "\\{[0-9A-Fa-f]{";
            out += consumed == 8 ? "6" : "8";
            out += "}\\}";
            i += consumed;
            continue;
        }
        if (helperDomain_ && StartsDomain(sample, i, consumed)) {
            out += "[A-Za-z0-9.-]+\\.[A-Za-z]{2,}(?:/[^\\s]*)?";
            i += consumed;
            continue;
        }
        if (helperMoney_ && StartsMoney(sample, i, consumed)) {
            out += "\\$[0-9][0-9.,]*";
            i += consumed;
            continue;
        }
        if (helperTime_ && StartsTime(sample, i, consumed)) {
            out += "[0-9]{2}:[0-9]{2}";
            i += consumed;
            continue;
        }
        if (helperNumbers_ && StartsNumber(sample, i, consumed)) {
            out += "[0-9]+(?:[.,][0-9]+)*(?:[kK][0-9]*(?:[.,][0-9]+)*)?";
            i += consumed;
            continue;
        }
        if (helperNick_ && StartsNick(sample, i, consumed)) {
            out += "[A-Za-z0-9]+_[A-Za-z0-9]+";
            i += consumed;
            continue;
        }

        std::uint32_t cp = 0;
        std::size_t charSize = 0;
        if (!DecodeUtf8At(sample, i, cp, charSize) || charSize == 0) {
            charSize = 1;
        }
        out += EscapeRegex(sample.substr(i, charSize));
        i += charSize;
    }

    if (anchors) {
        out = "^" + out + "$";
    }
    return out;
}
