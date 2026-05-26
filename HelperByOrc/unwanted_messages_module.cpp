#include "unwanted_messages_module.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "app_config.h"
#include "debug_log.h"
#include "ui_settings.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <utility>

namespace {

constexpr std::string_view kUnwantedSectionName = "unwanted";
constexpr int kUnwantedSchemaVersion = 1;
constexpr int kMaxConfigPatternLength = 65535;

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

float ScaleUi(float value) {
    return UiSettings::Instance().Scale(value);
}

ImVec2 ScaleUi(float x, float y) {
    return UiSettings::Instance().Scale(ImVec2(x, y));
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
    std::lock_guard lock(mutex_);
    if (!settings_.enabled || context.text.empty()) {
        return false;
    }

    MatchResult result;
    if (!MatchCandidates(BuildCandidates(context), context.source, &result)) {
        return false;
    }

    ++blockedCount_;
    lastBlocked_ = std::move(result);
    return true;
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
    UiSettings& ui = UiSettings::Instance();

    if (ImGui::Button(ui.Text(UiText::EditorBack), ScaleUi(120.0f, 0.0f))) {
        miscPageOpen_ = false;
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ui.Text(UiText::TabMisc));
    ImGui::Spacing();

    ImGui::SeparatorText(ui.Text(UiText::UnwantedTitle));
    ImGui::TextWrapped("%s", ui.Text(UiText::UnwantedIntro));
    ImGui::Spacing();

    DrawToolbar();
    DrawStats();
    DrawNormalizerControls();
    ImGui::Spacing();
    DrawRulesList();
    DrawAddRule();
    DrawTester();
    DrawRegexHelper();
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

void UnwantedMessagesModule::DrawToolbar() {
    UiSettings& ui = UiSettings::Instance();
    bool reload = false;

    {
        std::lock_guard lock(mutex_);

        if (ImGui::Checkbox(ui.Text(UiText::UnwantedEnabled), &settings_.enabled)) {
            SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::UnwantedReload), ScaleUi(120.0f, 0.0f))) {
            reload = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::UnwantedEnableAll), ScaleUi(120.0f, 0.0f))) {
            SetAllRulesEnabled(true);
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::UnwantedDisableAll), ScaleUi(130.0f, 0.0f))) {
            SetAllRulesEnabled(false);
        }
    }

    if (reload) {
        ReloadConfig();
    }
}

void UnwantedMessagesModule::DrawNormalizerControls() {
    UiSettings& ui = UiSettings::Instance();
    std::lock_guard lock(mutex_);

    if (ImGui::BeginChild("##unwanted_normalizer", ImVec2(0.0f, ScaleUi(96.0f)), ImGuiChildFlags_FrameStyle)) {
        ImGui::SeparatorText(ui.Text(UiText::UnwantedNormalizer));
        bool changed = false;
        changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedStripColors), &settings_.normalizer.stripColors);
        ImGui::SameLine();
        changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedCollapseWhitespace), &settings_.normalizer.collapseWhitespace);
        ImGui::SameLine();
        changed |= ImGui::Checkbox(ui.Text(UiText::UnwantedTrim), &settings_.normalizer.trim);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(ScaleUi(120.0f));
        changed |= ImGui::InputInt(ui.Text(UiText::UnwantedMaxPatternLength), &settings_.maxPatternLength, 0, 0);
        settings_.maxPatternLength = std::clamp(settings_.maxPatternLength, 1, kMaxConfigPatternLength);

        if (changed) {
            CompileRules();
            SaveConfig();
        }
    }
    ImGui::EndChild();
}

void UnwantedMessagesModule::DrawRulesList() {
    UiSettings& ui = UiSettings::Instance();
    std::lock_guard lock(mutex_);

    ImGui::SeparatorText(ui.Text(UiText::UnwantedRules));
    if (ImGui::Button(ui.Text(UiText::UnwantedSelectAll), ScaleUi(118.0f, 0.0f))) {
        SelectAllRules();
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::UnwantedClearSelection), ScaleUi(128.0f, 0.0f))) {
        ClearSelection();
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::UnwantedDeleteSelected), ScaleUi(148.0f, 0.0f))) {
        DeleteSelectedRules();
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::UnwantedEnableSelected), ScaleUi(146.0f, 0.0f))) {
        SetSelectedRulesEnabled(true);
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::UnwantedDisableSelected), ScaleUi(156.0f, 0.0f))) {
        SetSelectedRulesEnabled(false);
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::UnwantedRemoveDuplicates), ScaleUi(146.0f, 0.0f))) {
        RemoveDuplicateRules();
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::UnwantedSortByType), ScaleUi(118.0f, 0.0f))) {
        SortRulesByType();
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::UnwantedSortByText), ScaleUi(118.0f, 0.0f))) {
        SortRulesByText();
    }

    const float listHeight = std::max(ScaleUi(240.0f), ImGui::GetTextLineHeightWithSpacing() * 10.0f);
    if (ImGui::BeginChild("##unwanted_rules_list", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders)) {
        if (rules_.empty()) {
            ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedNoRules));
        } else if (ImGui::BeginTable(
                       "##unwanted_rules_table",
                       7,
                       ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("sel", ImGuiTableColumnFlags_WidthFixed, ScaleUi(34.0f));
            ImGui::TableSetupColumn("enabled", ImGuiTableColumnFlags_WidthFixed, ScaleUi(72.0f));
            ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed, ScaleUi(120.0f));
            ImGui::TableSetupColumn("text", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("flags", ImGuiTableColumnFlags_WidthFixed, ScaleUi(190.0f));
            ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthFixed, ScaleUi(180.0f));
            ImGui::TableSetupColumn("actions", ImGuiTableColumnFlags_WidthFixed, ScaleUi(118.0f));

            for (std::size_t index = 0; index < rules_.size(); ++index) {
                Rule& rule = rules_[index];
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
                const char* preview = rule.type == RuleType::Regex
                    ? ui.Text(UiText::UnwantedTypeRegex)
                    : ui.Text(UiText::UnwantedTypeLiteral);
                if (ImGui::BeginCombo("##type", preview)) {
                    if (ImGui::Selectable(ui.Text(UiText::UnwantedTypeLiteral), rule.type == RuleType::Literal)) {
                        rule.type = RuleType::Literal;
                        CompileRules();
                        SaveConfig();
                    }
                    if (ImGui::Selectable(ui.Text(UiText::UnwantedTypeRegex), rule.type == RuleType::Regex)) {
                        rule.type = RuleType::Regex;
                        CompileRules();
                        SaveConfig();
                    }
                    ImGui::EndCombo();
                }

                ImGui::TableSetColumnIndex(3);
                const bool textChanged = InputTextString("##text", rule.text, ImGuiInputTextFlags_None, static_cast<std::size_t>(settings_.maxPatternLength) + 1);
                if (textChanged && ImGui::IsItemDeactivatedAfterEdit()) {
                    CompileRules();
                    SaveConfig();
                }
                if (!textChanged && ImGui::IsItemDeactivatedAfterEdit()) {
                    CompileRules();
                    SaveConfig();
                }

                ImGui::TableSetColumnIndex(4);
                if (ImGui::Checkbox(ui.Text(UiText::UnwantedNoCase), &rule.nocase)) {
                    CompileRules();
                    SaveConfig();
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(rule.type != RuleType::Literal);
                if (ImGui::Checkbox(ui.Text(UiText::UnwantedWholeWord), &rule.wholeWord)) {
                    CompileRules();
                    SaveConfig();
                }
                ImGui::EndDisabled();

                ImGui::TableSetColumnIndex(5);
                if (!rule.error.empty()) {
                    ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "%s", ui.Text(UiText::UnwantedInvalidRule));
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", rule.error.c_str());
                    }
                } else {
                    ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedRuleOk));
                }

                ImGui::TableSetColumnIndex(6);
                if (ImGui::SmallButton("^") && index > 0) {
                    std::swap(rules_[index], rules_[index - 1]);
                    SaveConfig();
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("v") && index + 1 < rules_.size()) {
                    std::swap(rules_[index], rules_[index + 1]);
                    SaveConfig();
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    DeleteRuleByIndex(index);
                    ImGui::PopID();
                    break;
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

void UnwantedMessagesModule::DrawAddRule() {
    UiSettings& ui = UiSettings::Instance();
    std::lock_guard lock(mutex_);

    ImGui::SeparatorText(ui.Text(UiText::UnwantedAddRule));
    InputTextWithHintString("##unwanted_new_rule", ui.Text(UiText::UnwantedRuleTextHint), newRuleText_, 0, 512);
    ImGui::SameLine();
    ImGui::Checkbox(ui.Text(UiText::UnwantedTypeRegex), &newRuleIsRegex_);
    ImGui::SameLine();
    ImGui::Checkbox(ui.Text(UiText::UnwantedNoCase), &newRuleNoCase_);
    ImGui::SameLine();
    ImGui::BeginDisabled(newRuleIsRegex_);
    ImGui::Checkbox(ui.Text(UiText::UnwantedWholeWord), &newRuleWholeWord_);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::UnwantedAddRuleAction), ScaleUi(110.0f, 0.0f))) {
        AddRule(newRuleIsRegex_ ? RuleType::Regex : RuleType::Literal, TrimAscii(newRuleText_), newRuleNoCase_, newRuleWholeWord_);
        newRuleText_.clear();
        newRuleIsRegex_ = false;
        newRuleNoCase_ = false;
        newRuleWholeWord_ = false;
    }
}

void UnwantedMessagesModule::DrawTester() {
    UiSettings& ui = UiSettings::Instance();
    std::lock_guard lock(mutex_);

    ImGui::SeparatorText(ui.Text(UiText::UnwantedTester));
    InputTextWithHintString("##unwanted_test_text", ui.Text(UiText::UnwantedTesterHint), testText_, 0, 1024);
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::UnwantedTestAction), ScaleUi(120.0f, 0.0f))) {
        UnwantedMessageContext context;
        context.source = UnwantedMessageSource::CChatAddEntry;
        context.text = testText_;
        MatchResult result;
        lastTesterMatch_ = {};
        if (MatchCandidates(BuildCandidates(context), context.source, &result)) {
            lastTesterMatch_ = std::move(result);
            selectedRuleIds_.clear();
            selectedRuleIds_.insert(lastTesterMatch_.ruleId);
        }
    }

    if (lastTesterMatch_.matched) {
        ImGui::TextColored(
            ImVec4(0.60f, 1.0f, 0.72f, 1.0f),
            "%s",
            ui.Format(
                UiText::UnwantedTesterMatched,
                lastTesterMatch_.ruleId.c_str(),
                lastTesterMatch_.candidate.c_str()).c_str());
    } else {
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedTesterNoMatch));
    }
}

void UnwantedMessagesModule::DrawRegexHelper() {
    UiSettings& ui = UiSettings::Instance();
    std::lock_guard lock(mutex_);

    ImGui::SeparatorText(ui.Text(UiText::UnwantedRegexHelper));
    InputTextWithHintString("##unwanted_helper_sample", ui.Text(UiText::UnwantedHelperInputHint), helperSample_, 0, 1024);

    ImGui::Checkbox(ui.Text(UiText::UnwantedHelperAnchors), &helperAnchors_);
    ImGui::SameLine();
    ImGui::Checkbox(ui.Text(UiText::UnwantedHelperColors), &helperColors_);
    ImGui::SameLine();
    ImGui::Checkbox(ui.Text(UiText::UnwantedHelperNumbers), &helperNumbers_);
    ImGui::SameLine();
    ImGui::Checkbox(ui.Text(UiText::UnwantedHelperMoney), &helperMoney_);
    ImGui::SameLine();
    ImGui::Checkbox(ui.Text(UiText::UnwantedHelperTime), &helperTime_);
    ImGui::SameLine();
    ImGui::Checkbox(ui.Text(UiText::UnwantedHelperNick), &helperNick_);
    ImGui::SameLine();
    ImGui::Checkbox(ui.Text(UiText::UnwantedHelperBracketTag), &helperBracketTag_);

    if (ImGui::Button(ui.Text(UiText::UnwantedHelperGenerate), ScaleUi(140.0f, 0.0f))) {
        helperExact_ = GenerateExactRegex(helperSample_);
        helperGeneralized_ = GenerateGeneralizedRegex(helperSample_);
    }

    if (!helperExact_.empty() || !helperGeneralized_.empty()) {
        if (!helperExact_.empty()) {
            ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedHelperExact));
            ImGui::TextWrapped("%s", helperExact_.c_str());
            if (ImGui::Button(ui.Text(UiText::UnwantedAddExact), ScaleUi(128.0f, 0.0f))) {
                AddRule(RuleType::Regex, helperExact_, false, false);
            }
            ImGui::SameLine();
            if (ImGui::Button(ui.Text(UiText::UnwantedCopyExact), ScaleUi(128.0f, 0.0f))) {
                ImGui::SetClipboardText(helperExact_.c_str());
            }
        }
        if (!helperGeneralized_.empty()) {
            ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedHelperGeneralized));
            ImGui::TextWrapped("%s", helperGeneralized_.c_str());
            if (ImGui::Button(ui.Text(UiText::UnwantedAddGeneralized), ScaleUi(148.0f, 0.0f))) {
                AddRule(RuleType::Regex, helperGeneralized_, false, false);
            }
            ImGui::SameLine();
            if (ImGui::Button(ui.Text(UiText::UnwantedCopyGeneralized), ScaleUi(148.0f, 0.0f))) {
                ImGui::SetClipboardText(helperGeneralized_.c_str());
            }
        }
    }
}

void UnwantedMessagesModule::DrawStats() const {
    UiSettings& ui = UiSettings::Instance();
    std::lock_guard lock(mutex_);

    if (ImGui::BeginChild("##unwanted_stats", ImVec2(0.0f, ScaleUi(76.0f)), ImGuiChildFlags_FrameStyle)) {
        const std::size_t invalid = std::count_if(rules_.begin(), rules_.end(), [](const Rule& rule) {
            return !rule.error.empty();
        });
        ImGui::Text(
            "%s",
            ui.Format(
                UiText::UnwantedStats,
                std::to_string(rules_.size()).c_str(),
                std::to_string(invalid).c_str(),
                std::to_string(blockedCount_).c_str()).c_str());
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
    }
    ImGui::EndChild();
}

void UnwantedMessagesModule::AddRule(RuleType type, std::string text, bool nocase, bool wholeWord) {
    if (text.empty()) {
        return;
    }

    Rule rule;
    rule.id = AllocateRuleId();
    rule.enabled = true;
    rule.type = type;
    rule.text = std::move(text);
    rule.nocase = nocase;
    rule.wholeWord = type == RuleType::Literal && wholeWord;
    rules_.push_back(std::move(rule));
    CompileRules();
    SaveConfig();
}

void UnwantedMessagesModule::DeleteRuleByIndex(std::size_t index) {
    if (index >= rules_.size()) {
        return;
    }

    selectedRuleIds_.erase(rules_[index].id);
    rules_.erase(rules_.begin() + static_cast<std::ptrdiff_t>(index));
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
    selectedRuleIds_.clear();
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
        if (helperColors_ && TryColorTag(sample, i, consumed)) {
            out += "\\{[0-9A-Fa-f]{";
            out += consumed == 8 ? "6" : "8";
            out += "}\\}";
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
            out += "[0-9]+(?:[.,][0-9]+)*";
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

    if (helperAnchors_) {
        out = "^" + out + "$";
    }
    return out;
}
