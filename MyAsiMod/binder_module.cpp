#include "binder_module.h"

#include "app_config.h"
#include "debug_log.h"
#include "samp_api.h"
#include "samp_hooks.h"
#include "samp_rak_hooks.h"
#include "text_encoding.h"
#include "ui_settings.h"

#include <game_sa/CPed.h>
#include <game_sa/common.h>
#include <extensions/ScriptCommands.h>
#include <game_sa/eScriptCommands.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr char kLegacyConfigFileName[] = "binder.json";
constexpr UINT kDefaultConfirmKey = '1';
constexpr UINT kDefaultCancelKey = '2';
constexpr UINT kDefaultQuickMenuFallback = VK_XBUTTON1;
constexpr int kMaxToasts = 8;
constexpr int kMinMessageIntervalMs = 50;
constexpr int kDefaultRepeatIntervalMs = 500;
constexpr int kQuickMenuWidth = 320;
constexpr int kQuickMenuHeight = 360;
constexpr int kTextConfirmTimeoutMs = 2000;
constexpr int kOutgoingGuardTimeoutMs = 2000;
constexpr char kIconToggleOff[] = "\xEF\x88\x84";
constexpr char kIconToggleOn[] = "\xEF\x88\x85";
constexpr char kIconBolt[] = "\xEF\x83\xA7";
constexpr char kIconComment[] = "\xEF\x83\xA5";
constexpr char kIconTerminal[] = "\xEF\x84\xA0";
constexpr char kIconKeyboard[] = "\xEF\x84\x9C";
constexpr char kIconPlay[] = "\xEF\x81\x8B";
constexpr char kIconEdit[] = "\xEF\x81\x84";
constexpr char kIconDelete[] = "\xEF\x8B\xAD";
constexpr char kIconBars[] = "\xEF\x83\x89";

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

std::string ToLower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::string SanitizeFolderName(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        if (ch == '.') {
            result.push_back('_');
        } else if (ch == '\r' || ch == '\n') {
            result.push_back(' ');
        } else {
            result.push_back(static_cast<char>(ch));
        }
    }
    return Trim(result);
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

std::string NormalizeLineEndings(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (ch == '\r') {
            if (i + 1 < value.size() && value[i + 1] == '\n') {
                ++i;
            }
            result.push_back('\n');
            continue;
        }
        result.push_back(ch);
    }
    return result;
}

bool SmallIconActionButton(const char* icon, const char* id, const char* tooltip, const ImVec2& size) {
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImU32 bgColor = ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    drawList->AddRectFilled(min, max, bgColor, style.FrameRounding);
    if (style.FrameBorderSize > 0.0f) {
        drawList->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding, 0, style.FrameBorderSize);
    }

    const ImVec2 buttonSize(max.x - min.x, max.y - min.y);
    ImVec2 iconPos{};
    bool iconPosResolved = false;
    ImWchar iconCodepoint = 0;
    if (DecodeFirstUtf8Codepoint(icon, iconCodepoint)) {
        if (ImFontBaked* bakedFont = ImGui::GetFontBaked()) {
            if (const ImFontGlyph* glyph = bakedFont->FindGlyphNoFallback(iconCodepoint)) {
                const float glyphWidth = glyph->X1 - glyph->X0;
                const float glyphHeight = glyph->Y1 - glyph->Y0;
                iconPos.x = min.x + (buttonSize.x - glyphWidth) * 0.5f - glyph->X0;
                iconPos.y = min.y + (buttonSize.y - glyphHeight) * 0.5f - glyph->Y0;
                iconPosResolved = true;
            }
        }
    }

    if (!iconPosResolved) {
        const ImVec2 iconSize = ImGui::CalcTextSize(icon);
        iconPos.x = min.x + (buttonSize.x - iconSize.x) * 0.5f;
        iconPos.y = min.y + (buttonSize.y - iconSize.y) * 0.5f;
    }

    iconPos.x = std::floor(iconPos.x);
    iconPos.y = std::floor(iconPos.y);
    drawList->AddText(iconPos, ImGui::GetColorU32(ImGuiCol_Text), icon);

    if (tooltip != nullptr && tooltip[0] != '\0' && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
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
        std::string candidate = result + kEllipsis;
        if (candidate.empty() || ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth) {
            return candidate;
        }
    }

    return kEllipsis;
}

void CenterNextItemHorizontally(float itemWidth) {
    const float availWidth = ImGui::GetContentRegionAvail().x;
    if (availWidth > itemWidth) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - itemWidth) * 0.5f);
    }
}

void DrawCenteredTableHeaderLabel(const char* label, const char* tooltip = nullptr) {
    if (!label) {
        label = "";
    }

    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    CenterNextItemHorizontally(labelSize.x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (tooltip && tooltip[0] != '\0' && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tooltip);
    }
}

std::string JoinPath(const std::vector<std::string>& path) {
    std::ostringstream stream;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i != 0) {
            stream << '/';
        }
        stream << path[i];
    }
    return stream.str();
}

std::vector<std::string> Split(std::string_view value, char delimiter) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t pos = value.find(delimiter, start);
        if (pos == std::string_view::npos) {
            parts.emplace_back(value.substr(start));
            break;
        }
        parts.emplace_back(value.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

std::string EscapeJsonString(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (ch < 0x20) {
                char buffer[7]{};
                std::snprintf(buffer, sizeof(buffer), "\\u%04X", ch);
                result += buffer;
            } else {
                result.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return result;
}

struct JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

struct JsonValue {
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject>;

    Storage storage = nullptr;

    JsonValue() = default;
    JsonValue(std::nullptr_t) : storage(nullptr) {
    }
    JsonValue(bool value) : storage(value) {
    }
    JsonValue(double value) : storage(value) {
    }
    JsonValue(int value) : storage(static_cast<double>(value)) {
    }
    JsonValue(std::string value) : storage(std::move(value)) {
    }
    JsonValue(const char* value) : storage(std::string(value ? value : "")) {
    }
    JsonValue(JsonArray value) : storage(std::move(value)) {
    }
    JsonValue(JsonObject value) : storage(std::move(value)) {
    }

    bool IsNull() const { return std::holds_alternative<std::nullptr_t>(storage); }
    bool IsBool() const { return std::holds_alternative<bool>(storage); }
    bool IsNumber() const { return std::holds_alternative<double>(storage); }
    bool IsString() const { return std::holds_alternative<std::string>(storage); }
    bool IsArray() const { return std::holds_alternative<JsonArray>(storage); }
    bool IsObject() const { return std::holds_alternative<JsonObject>(storage); }

    const JsonArray* TryArray() const { return std::get_if<JsonArray>(&storage); }
    const JsonObject* TryObject() const { return std::get_if<JsonObject>(&storage); }
    const std::string* TryString() const { return std::get_if<std::string>(&storage); }
    const bool* TryBool() const { return std::get_if<bool>(&storage); }
    const double* TryNumber() const { return std::get_if<double>(&storage); }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view source) : source_(source) {
    }

    std::optional<JsonValue> Parse(std::string& error) {
        SkipWhitespace();
        JsonValue value;
        if (!ParseValue(value, error)) {
            return std::nullopt;
        }
        SkipWhitespace();
        if (pos_ != source_.size()) {
            error = "unexpected trailing characters";
            return std::nullopt;
        }
        return value;
    }

private:
    void SkipWhitespace() {
        while (pos_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[pos_])) != 0) {
            ++pos_;
        }
    }

    bool Match(std::string_view token) {
        if (source_.substr(pos_, token.size()) == token) {
            pos_ += token.size();
            return true;
        }
        return false;
    }

    bool ParseValue(JsonValue& out, std::string& error);
    bool ParseObject(JsonValue& out, std::string& error);
    bool ParseArray(JsonValue& out, std::string& error);
    bool ParseString(std::string& out, std::string& error);
    bool ParseNumber(double& out, std::string& error);

    std::string_view source_;
    std::size_t pos_ = 0;
};

bool JsonParser::ParseValue(JsonValue& out, std::string& error) {
    if (pos_ >= source_.size()) {
        error = "unexpected end of file";
        return false;
    }

    const char ch = source_[pos_];
    if (ch == '{') {
        return ParseObject(out, error);
    }
    if (ch == '[') {
        return ParseArray(out, error);
    }
    if (ch == '"') {
        std::string text;
        if (!ParseString(text, error)) {
            return false;
        }
        out = JsonValue(std::move(text));
        return true;
    }
    if (ch == 't' && Match("true")) {
        out = JsonValue(true);
        return true;
    }
    if (ch == 'f' && Match("false")) {
        out = JsonValue(false);
        return true;
    }
    if (ch == 'n' && Match("null")) {
        out = JsonValue(nullptr);
        return true;
    }
    if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)) != 0) {
        double number = 0.0;
        if (!ParseNumber(number, error)) {
            return false;
        }
        out = JsonValue(number);
        return true;
    }

    error = "invalid token";
    return false;
}

bool JsonParser::ParseObject(JsonValue& out, std::string& error) {
    JsonObject object;
    ++pos_;
    SkipWhitespace();
    if (pos_ < source_.size() && source_[pos_] == '}') {
        ++pos_;
        out = JsonValue(std::move(object));
        return true;
    }

    while (pos_ < source_.size()) {
        std::string key;
        if (!ParseString(key, error)) {
            return false;
        }
        SkipWhitespace();
        if (pos_ >= source_.size() || source_[pos_] != ':') {
            error = "expected ':'";
            return false;
        }
        ++pos_;
        SkipWhitespace();

        JsonValue value;
        if (!ParseValue(value, error)) {
            return false;
        }
        object.emplace(std::move(key), std::move(value));

        SkipWhitespace();
        if (pos_ < source_.size() && source_[pos_] == ',') {
            ++pos_;
            SkipWhitespace();
            continue;
        }
        if (pos_ < source_.size() && source_[pos_] == '}') {
            ++pos_;
            out = JsonValue(std::move(object));
            return true;
        }

        error = "expected ',' or '}'";
        return false;
    }

    error = "unexpected end of object";
    return false;
}

bool JsonParser::ParseArray(JsonValue& out, std::string& error) {
    JsonArray array;
    ++pos_;
    SkipWhitespace();
    if (pos_ < source_.size() && source_[pos_] == ']') {
        ++pos_;
        out = JsonValue(std::move(array));
        return true;
    }

    while (pos_ < source_.size()) {
        JsonValue value;
        if (!ParseValue(value, error)) {
            return false;
        }
        array.push_back(std::move(value));

        SkipWhitespace();
        if (pos_ < source_.size() && source_[pos_] == ',') {
            ++pos_;
            SkipWhitespace();
            continue;
        }
        if (pos_ < source_.size() && source_[pos_] == ']') {
            ++pos_;
            out = JsonValue(std::move(array));
            return true;
        }

        error = "expected ',' or ']'";
        return false;
    }

    error = "unexpected end of array";
    return false;
}

bool JsonParser::ParseString(std::string& out, std::string& error) {
    if (pos_ >= source_.size() || source_[pos_] != '"') {
        error = "expected string";
        return false;
    }

    ++pos_;
    out.clear();
    while (pos_ < source_.size()) {
        const char ch = source_[pos_++];
        if (ch == '"') {
            return true;
        }
        if (ch != '\\') {
            out.push_back(ch);
            continue;
        }
        if (pos_ >= source_.size()) {
            error = "invalid escape sequence";
            return false;
        }
        const char esc = source_[pos_++];
        switch (esc) {
        case '"':
        case '\\':
        case '/':
            out.push_back(esc);
            break;
        case 'b':
            out.push_back('\b');
            break;
        case 'f':
            out.push_back('\f');
            break;
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;
        default:
            error = "unsupported escape sequence";
            return false;
        }
    }

    error = "unterminated string";
    return false;
}

bool JsonParser::ParseNumber(double& out, std::string& error) {
    const std::size_t start = pos_;
    if (source_[pos_] == '-') {
        ++pos_;
    }
    while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_])) != 0) {
        ++pos_;
    }
    if (pos_ < source_.size() && source_[pos_] == '.') {
        ++pos_;
        while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_])) != 0) {
            ++pos_;
        }
    }
    if (pos_ < source_.size() && (source_[pos_] == 'e' || source_[pos_] == 'E')) {
        ++pos_;
        if (pos_ < source_.size() && (source_[pos_] == '+' || source_[pos_] == '-')) {
            ++pos_;
        }
        while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_])) != 0) {
            ++pos_;
        }
    }

    const std::string token(source_.substr(start, pos_ - start));
    char* endPtr = nullptr;
    out = std::strtod(token.c_str(), &endPtr);
    if (!endPtr || *endPtr != '\0') {
        error = "invalid number";
        return false;
    }
    return true;
}

void WriteJson(const JsonValue& value, std::string& out, int indent = 0) {
    if (value.IsNull()) {
        out += "null";
        return;
    }
    if (const bool* boolValue = value.TryBool()) {
        out += *boolValue ? "true" : "false";
        return;
    }
    if (const double* number = value.TryNumber()) {
        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "%.15g", *number);
        out += buffer;
        return;
    }
    if (const std::string* stringValue = value.TryString()) {
        out.push_back('"');
        out += EscapeJsonString(*stringValue);
        out.push_back('"');
        return;
    }
    if (const JsonArray* array = value.TryArray()) {
        out += "[";
        if (!array->empty()) {
            out += "\n";
            for (std::size_t i = 0; i < array->size(); ++i) {
                out.append(static_cast<std::size_t>(indent + 2), ' ');
                WriteJson((*array)[i], out, indent + 2);
                if (i + 1 != array->size()) {
                    out += ",";
                }
                out += "\n";
            }
            out.append(static_cast<std::size_t>(indent), ' ');
        }
        out += "]";
        return;
    }
    if (const JsonObject* object = value.TryObject()) {
        out += "{";
        if (!object->empty()) {
            out += "\n";
            std::size_t index = 0;
            for (const auto& [key, child] : *object) {
                out.append(static_cast<std::size_t>(indent + 2), ' ');
                out.push_back('"');
                out += EscapeJsonString(key);
                out += "\": ";
                WriteJson(child, out, indent + 2);
                if (++index != object->size()) {
                    out += ",";
                }
                out += "\n";
            }
            out.append(static_cast<std::size_t>(indent), ' ');
        }
        out += "}";
    }
}

template <typename T>
T JsonNumberOr(const JsonObject* object, const char* key, T fallback) {
    if (!object) {
        return fallback;
    }
    const auto it = object->find(key);
    if (it == object->end()) {
        return fallback;
    }
    if (const double* number = it->second.TryNumber()) {
        return static_cast<T>(*number);
    }
    return fallback;
}

std::string JsonStringOr(const JsonObject* object, const char* key, std::string fallback = {}) {
    if (!object) {
        return fallback;
    }
    const auto it = object->find(key);
    if (it == object->end()) {
        return fallback;
    }
    if (const std::string* text = it->second.TryString()) {
        return *text;
    }
    return fallback;
}

bool JsonBoolOr(const JsonObject* object, const char* key, bool fallback) {
    if (!object) {
        return fallback;
    }
    const auto it = object->find(key);
    if (it == object->end()) {
        return fallback;
    }
    if (const bool* value = it->second.TryBool()) {
        return *value;
    }
    return fallback;
}

const JsonArray* JsonArrayOrNull(const JsonObject* object, const char* key) {
    if (!object) {
        return nullptr;
    }
    const auto it = object->find(key);
    if (it == object->end()) {
        return nullptr;
    }
    return it->second.TryArray();
}

const JsonObject* JsonObjectOrNull(const JsonObject* object, const char* key) {
    if (!object) {
        return nullptr;
    }
    const auto it = object->find(key);
    if (it == object->end()) {
        return nullptr;
    }
    return it->second.TryObject();
}

enum class HotkeyMode {
    ModifierTrigger,
    OrderedCombo,
};

enum class QuickMenuActivationMode {
    Hold,
    Toggle,
};

enum class InputMode {
    Text,
    ButtonsList,
    ButtonsListText,
};

struct HotkeyMessage {
    std::string text;
    int intervalMs = 0;
    int method = 0;
};

struct InputButton {
    std::string label;
    std::string text;
    std::string hint;
    std::string when;
};

struct HotkeyInput {
    std::string key;
    std::string label;
    std::string hint;
    InputMode mode = InputMode::Text;
    std::vector<InputButton> buttons;
    bool multiSelect = false;
    std::string multiSeparator = ", ";
    std::string cascadeParentKey;
};

struct TextTrigger {
    std::string text;
    bool enabled = false;
    bool pattern = false;
};

struct TextConfirmation {
    bool enabled = false;
    UINT key = kDefaultConfirmKey;
    UINT cancelKey = kDefaultCancelKey;
    bool waitForResolution = true;
};

struct HotkeyEntry {
    std::string label = UiSettings::Instance().Text(UiText::BinderDefaultHotkey);
    std::vector<UINT> keys;
    HotkeyMode hotkeyMode = HotkeyMode::ModifierTrigger;
    std::vector<HotkeyMessage> messages;
    std::vector<HotkeyInput> inputs;
    TextTrigger textTrigger;
    TextConfirmation textConfirmation;
    std::vector<bool> conditions;
    std::vector<bool> quickConditions;
    bool repeatMode = false;
    int repeatIntervalMs = 0;
    bool enabled = true;
    bool quickMenu = false;
    std::string command;
    bool commandEnabled = false;
    std::vector<std::string> folderPath;

    int number = 0;
    bool comboActive = false;
    std::vector<UINT> lastRepeatPressed;
    double lastActivatedAtMs = 0.0;
    double debounceUntilMs = 0.0;
    bool awaitingInput = false;
    bool waitingTextConfirmation = false;
    double textConfirmationDeadlineMs = 0.0;
    std::string pendingTriggerText;
    std::string pendingTriggerSource;
};

struct FolderNode {
    int id = 0;
    std::string name;
    FolderNode* parent = nullptr;
    std::vector<std::unique_ptr<FolderNode>> children;
    std::vector<bool> quickConditions;
    bool quickMenu = true;
    bool open = true;
};

struct Toast {
    std::string text;
    ImVec4 color{ 0.10f, 0.10f, 0.10f, 0.95f };
    double expiresAtMs = 0.0;
};

struct OutgoingGuard {
    std::string kind;
    std::string text;
    double expiresAtMs = 0.0;
};

struct RunningBind {
    int hotkeyIndex = -1;
    std::map<std::string, std::string> inputValues;
    std::size_t messageIndex = 0;
    double nextAtMs = 0.0;
};

struct InputDialogField {
    HotkeyInput input;
    std::string textValue;
    std::optional<int> selectedButtonIndex;
    std::set<int> selectedButtons;
};

struct InputDialogState {
    int hotkeyIndex = -1;
    int startDelayMs = 0;
    std::vector<InputDialogField> fields;
};

struct CaptureKeyInfo {
    UINT keyCode = 0;
    bool isDown = false;
    bool isUp = false;
};

enum class CaptureTarget {
    None,
    BindHotkey,
    QuickMenuHotkey,
    ConfirmKey,
    CancelKey,
};

namespace hotkeys {

UINT NormalizeKey(UINT key);
bool IsMouseKey(UINT key);
bool IsModifierKey(UINT key);
bool IsHotkeyKey(UINT key);
std::vector<UINT> NormalizeCombo(const std::vector<UINT>& keys, HotkeyMode mode);
bool ComboMatch(const std::vector<UINT>& pressed, const std::vector<UINT>& combo, HotkeyMode mode);
std::string KeyName(UINT key);
std::string ToString(const std::vector<UINT>& keys, HotkeyMode mode = HotkeyMode::ModifierTrigger);
std::optional<CaptureKeyInfo> GetMessageKeyInfo(UINT message, WPARAM wparam);

class KeyTracker {
public:
    void Reset();
    bool OnWindowMessage(UINT message, WPARAM wparam);
    const std::vector<UINT>& Ordered() const;
    void KeyDown(UINT key);
    void KeyUp(UINT key);
    void Rebuild();

    std::map<UINT, int> held_{};
    std::vector<UINT> ordered_{};
    int counter_ = 0;
};

class Capture {
public:
    void Start(const std::vector<UINT>& initial);
    void Stop();
    bool Active() const;
    void Clear();
    void ArmMouseCapture();
    bool MouseCaptureArmed() const;
    std::vector<UINT> Draft() const;
    bool Save(std::vector<UINT>& outKeys);
    bool OnWindowMessage(UINT message, WPARAM wparam, bool& canceled, bool& saved, std::vector<UINT>& outKeys);

private:
    bool active_ = false;
    KeyTracker tracker_{};
    std::vector<UINT> lastCombo_{};
    bool mouseCaptureArmed_ = false;
    UINT mousePendingKey_ = 0;
};

} // namespace hotkeys

enum class ConditionId : std::size_t {
    InWater = 0,
    Dead,
    InAir,
    InAnyCar,
    WithoutWeapon,
    WithWeapon,
    OnFoot,
    ChatOpened,
    DialogOpened,
    Count,
};

constexpr std::array<UiText, static_cast<std::size_t>(ConditionId::Count)> kConditionLabelIds = {
    UiText::ConditionInWater,
    UiText::ConditionDead,
    UiText::ConditionInAir,
    UiText::ConditionInAnyCar,
    UiText::ConditionWithoutWeapon,
    UiText::ConditionWithWeapon,
    UiText::ConditionOnFoot,
    UiText::ConditionChatOpened,
    UiText::ConditionDialogOpened,
};

bool CheckCondition(ConditionId condition, SampApi* sampApi);
bool ConditionsBlock(const std::vector<bool>& flags, SampApi* sampApi, std::string* message = nullptr);
bool InputModeUsesButtons(InputMode mode);
InputMode NormalizeInputMode(std::string_view value);
std::string InputModeId(InputMode mode);
HotkeyMode NormalizeHotkeyMode(std::string_view value);
std::string HotkeyModeId(HotkeyMode mode);
QuickMenuActivationMode NormalizeQuickMenuActivationMode(std::string_view value);
std::string QuickMenuActivationModeId(QuickMenuActivationMode mode);
std::string NormalizeInputKey(std::string_view value);
std::vector<InputButton> ParseButtonsText(std::string_view multiLine);
std::string SerializeButtonsText(const std::vector<InputButton>& buttons);
bool InputTextString(const char* label, std::string& value, ImGuiInputTextFlags flags = 0, std::size_t minBuffer = 256);
bool InputTextMultilineString(
    const char* label,
    std::string& value,
    const ImVec2& size,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 2048);
JsonValue SerializeBoolArray(const std::vector<bool>& flags);
std::vector<bool> DeserializeBoolArray(const JsonArray* array);
JsonValue SerializeUintArray(const std::vector<UINT>& values);
std::vector<UINT> DeserializeUintArray(const JsonArray* array);
JsonValue SerializeStringArray(const std::vector<std::string>& values);
std::vector<std::string> DeserializeStringArray(const JsonArray* array);
FolderNode* FindFolderByPath(const std::vector<std::unique_ptr<FolderNode>>& folders, const std::vector<std::string>& path);
void CollectFolderPaths(const std::vector<std::unique_ptr<FolderNode>>& folders, std::vector<std::vector<std::string>>& out, std::vector<std::string> prefix = {});
std::vector<std::string> BuildFolderPath(const FolderNode* folder);
bool PathStartsWith(const std::vector<std::string>& path, const std::vector<std::string>& prefix);
bool FolderMatchesSearch(const FolderNode& folder, std::string_view query);

const char* ConditionLabel(ConditionId condition) {
    return UiSettings::Instance().Text(kConditionLabelIds[static_cast<std::size_t>(condition)]);
}

namespace {

constexpr UINT kModifierMask[] = { VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN };

std::string StripColorTags(std::string_view text) {
    static const std::regex kColorTagRegex("\\{[0-9a-fA-F]{6,8}\\}");
    return std::regex_replace(std::string(text), kColorTagRegex, "");
}

std::string NormalizeTriggerText(std::string_view text) {
    return Trim(NormalizeLineEndings(StripColorTags(text)));
}

std::string ToUtf8ForDisplay(std::string_view text) {
    return textencoding::GameToUtf8(text);
}

std::string ToGameText(std::string_view text) {
    return textencoding::Utf8ToGame(text);
}

bool ParseInt(std::string_view text, int& value) {
    const std::string trimmed = Trim(text);
    if (trimmed.empty()) {
        return false;
    }

    char* end = nullptr;
    const long parsed = std::strtol(trimmed.c_str(), &end, 10);
    if (!end || *end != '\0') {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

UINT PickSingleCapturedKey(const std::vector<UINT>& keys, UINT fallback) {
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
        if (!hotkeys::IsModifierKey(*it)) {
            return *it;
        }
    }
    return keys.empty() ? fallback : keys.back();
}

const char* SendMethodLabel(int method) {
    UiSettings& ui = UiSettings::Instance();
    switch (method) {
    case 0:
        return ui.Text(UiText::SendLocalChat);
    case 1:
        return ui.Text(UiText::SendViaSamp);
    case 2:
        return ui.Text(UiText::SendDirect);
    case 3:
        return ui.Text(UiText::SendNoSend);
    case 4:
        return ui.Text(UiText::SendInsertChat);
    case 5:
        return ui.Text(UiText::SendOpenChat);
    case 6:
        return ui.Text(UiText::SendDialog);
    case 7:
        return ui.Text(UiText::SendClipboard);
    case 8:
        return ui.Text(UiText::SendLog);
    case 9:
        return ui.Text(UiText::SendToast);
    default:
        return ui.Text(UiText::SendUnknown);
    }
}

bool EqualNoCase(std::string_view lhs, std::string_view rhs) {
    return ToLower(lhs) == ToLower(rhs);
}

float ScaleUi(float value) {
    return UiSettings::Instance().Scale(value);
}

ImVec2 ScaleUi(float x, float y) {
    return UiSettings::Instance().Scale(ImVec2(x, y));
}

const char* InputModeLabel(InputMode mode) {
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

const char* HotkeyModeLabel(HotkeyMode mode) {
    return UiSettings::Instance().Text(
        mode == HotkeyMode::OrderedCombo ? UiText::HotkeyModeOrderedCombo : UiText::HotkeyModeModifierTrigger);
}

const char* QuickMenuModeLabel(QuickMenuActivationMode mode) {
    return UiSettings::Instance().Text(
        mode == QuickMenuActivationMode::Toggle ? UiText::QuickMenuModeToggle : UiText::QuickMenuModeHold);
}

} // namespace

UINT hotkeys::NormalizeKey(UINT key) {
    switch (key) {
    case VK_LCONTROL:
    case VK_RCONTROL:
        return VK_CONTROL;
    case VK_LSHIFT:
    case VK_RSHIFT:
        return VK_SHIFT;
    case VK_LMENU:
    case VK_RMENU:
        return VK_MENU;
    default:
        return key;
    }
}

bool hotkeys::IsMouseKey(UINT key) {
    switch (NormalizeKey(key)) {
    case VK_LBUTTON:
    case VK_RBUTTON:
    case VK_MBUTTON:
    case VK_XBUTTON1:
    case VK_XBUTTON2:
        return true;
    default:
        return false;
    }
}

bool hotkeys::IsModifierKey(UINT key) {
    switch (NormalizeKey(key)) {
    case VK_CONTROL:
    case VK_SHIFT:
    case VK_MENU:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    default:
        return false;
    }
}

bool hotkeys::IsHotkeyKey(UINT key) {
    const UINT normalized = NormalizeKey(key);
    return normalized != 0 && normalized <= 0xFF;
}

std::vector<UINT> hotkeys::NormalizeCombo(const std::vector<UINT>& keys, HotkeyMode mode) {
    std::vector<UINT> normalized;
    std::set<UINT> seen;
    normalized.reserve(keys.size());
    for (const UINT key : keys) {
        const UINT normalizedKey = NormalizeKey(key);
        if (!IsHotkeyKey(normalizedKey) || !seen.insert(normalizedKey).second) {
            continue;
        }
        normalized.push_back(normalizedKey);
    }

    if (mode == HotkeyMode::OrderedCombo) {
        return normalized;
    }

    std::vector<UINT> modifiers;
    std::vector<UINT> triggers;
    modifiers.reserve(normalized.size());
    triggers.reserve(normalized.size());
    for (const UINT key : normalized) {
        if (IsModifierKey(key)) {
            modifiers.push_back(key);
        } else {
            triggers.push_back(key);
        }
    }

    auto modifierOrder = [](UINT key) {
        switch (key) {
        case VK_CONTROL:
            return 1;
        case VK_SHIFT:
            return 2;
        case VK_MENU:
            return 3;
        case VK_LWIN:
            return 4;
        case VK_RWIN:
            return 5;
        default:
            return 100;
        }
    };

    std::sort(modifiers.begin(), modifiers.end(), [&](UINT lhs, UINT rhs) {
        const int leftOrder = modifierOrder(lhs);
        const int rightOrder = modifierOrder(rhs);
        return leftOrder == rightOrder ? lhs < rhs : leftOrder < rightOrder;
    });

    modifiers.insert(modifiers.end(), triggers.begin(), triggers.end());
    return modifiers;
}

bool hotkeys::ComboMatch(const std::vector<UINT>& pressed, const std::vector<UINT>& combo, HotkeyMode mode) {
    if (combo.empty()) {
        return false;
    }

    if (mode == HotkeyMode::OrderedCombo) {
        if (pressed.size() != combo.size()) {
            return false;
        }
        for (std::size_t i = 0; i < combo.size(); ++i) {
            if (NormalizeKey(pressed[i]) != NormalizeKey(combo[i])) {
                return false;
            }
        }
        return true;
    }

    const auto normalizedPressed = NormalizeCombo(pressed, mode);
    const auto normalizedCombo = NormalizeCombo(combo, mode);
    return normalizedPressed == normalizedCombo;
}

std::string hotkeys::KeyName(UINT key) {
    key = NormalizeKey(key);
    switch (key) {
    case VK_CONTROL:
        return "Ctrl";
    case VK_SHIFT:
        return "Shift";
    case VK_MENU:
        return "Alt";
    case VK_LWIN:
        return "LWin";
    case VK_RWIN:
        return "RWin";
    case VK_RETURN:
        return "Enter";
    case VK_SPACE:
        return "Space";
    case VK_TAB:
        return "Tab";
    case VK_ESCAPE:
        return "Esc";
    case VK_BACK:
        return "Backspace";
    case VK_DELETE:
        return "Delete";
    case VK_INSERT:
        return "Insert";
    case VK_HOME:
        return "Home";
    case VK_END:
        return "End";
    case VK_PRIOR:
        return "PageUp";
    case VK_NEXT:
        return "PageDown";
    case VK_LEFT:
        return "Left";
    case VK_RIGHT:
        return "Right";
    case VK_UP:
        return "Up";
    case VK_DOWN:
        return "Down";
    case VK_LBUTTON:
        return "Mouse1";
    case VK_RBUTTON:
        return "Mouse2";
    case VK_MBUTTON:
        return "Mouse3";
    case VK_XBUTTON1:
        return "XButton1";
    case VK_XBUTTON2:
        return "XButton2";
    default:
        break;
    }

    UINT scanCode = MapVirtualKeyA(key, MAPVK_VK_TO_VSC);
    if (key == VK_LEFT || key == VK_UP || key == VK_RIGHT || key == VK_DOWN || key == VK_PRIOR || key == VK_NEXT
        || key == VK_END || key == VK_HOME || key == VK_INSERT || key == VK_DELETE || key == VK_DIVIDE
        || key == VK_NUMLOCK) {
        scanCode |= 0x100;
    }

    char buffer[128]{};
    if (GetKeyNameTextA(static_cast<LONG>(scanCode << 16), buffer, static_cast<int>(std::size(buffer))) > 0) {
        return buffer;
    }

    if (key >= 'A' && key <= 'Z') {
        return std::string(1, static_cast<char>(key));
    }
    if (key >= '0' && key <= '9') {
        return std::string(1, static_cast<char>(key));
    }

    char fallback[16]{};
    std::snprintf(fallback, sizeof(fallback), "0x%02X", key);
    return fallback;
}

std::string hotkeys::ToString(const std::vector<UINT>& keys, HotkeyMode mode) {
    const auto normalized = NormalizeCombo(keys, mode);
    if (normalized.empty()) {
        return UiSettings::Instance().Text(UiText::HotkeyNotSet);
    }

    std::ostringstream stream;
    for (std::size_t i = 0; i < normalized.size(); ++i) {
        if (i != 0) {
            stream << " + ";
        }
        stream << KeyName(normalized[i]);
    }
    return stream.str();
}

std::optional<CaptureKeyInfo> hotkeys::GetMessageKeyInfo(UINT message, WPARAM wparam) {
    std::optional<CaptureKeyInfo> result;

    auto setDown = [&](UINT key) {
        if (!IsHotkeyKey(key)) {
            return;
        }
        result = CaptureKeyInfo{ NormalizeKey(key), true, false };
    };
    auto setUp = [&](UINT key) {
        if (!IsHotkeyKey(key)) {
            return;
        }
        result = CaptureKeyInfo{ NormalizeKey(key), false, true };
    };

    switch (message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        setDown(static_cast<UINT>(wparam));
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        setUp(static_cast<UINT>(wparam));
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        setDown(VK_LBUTTON);
        break;
    case WM_LBUTTONUP:
        setUp(VK_LBUTTON);
        break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        setDown(VK_RBUTTON);
        break;
    case WM_RBUTTONUP:
        setUp(VK_RBUTTON);
        break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
        setDown(VK_MBUTTON);
        break;
    case WM_MBUTTONUP:
        setUp(VK_MBUTTON);
        break;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK: {
        const UINT button = GET_XBUTTON_WPARAM(wparam);
        setDown(button == XBUTTON2 ? VK_XBUTTON2 : VK_XBUTTON1);
        break;
    }
    case WM_XBUTTONUP: {
        const UINT button = GET_XBUTTON_WPARAM(wparam);
        setUp(button == XBUTTON2 ? VK_XBUTTON2 : VK_XBUTTON1);
        break;
    }
    default:
        break;
    }

    return result;
}

void hotkeys::KeyTracker::Reset() {
    held_.clear();
    ordered_.clear();
    counter_ = 0;
}

void hotkeys::KeyTracker::KeyDown(UINT key) {
    key = NormalizeKey(key);
    if (!IsHotkeyKey(key) || held_.contains(key)) {
        return;
    }
    held_[key] = ++counter_;
    Rebuild();
}

void hotkeys::KeyTracker::KeyUp(UINT key) {
    key = NormalizeKey(key);
    const auto it = held_.find(key);
    if (it == held_.end()) {
        return;
    }
    held_.erase(it);
    Rebuild();
    if (held_.empty()) {
        counter_ = 0;
    }
}

void hotkeys::KeyTracker::Rebuild() {
    std::vector<std::pair<UINT, int>> orderedPairs;
    orderedPairs.reserve(held_.size());
    for (const auto& [key, order] : held_) {
        orderedPairs.emplace_back(key, order);
    }
    std::sort(orderedPairs.begin(), orderedPairs.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.second < rhs.second;
    });

    ordered_.clear();
    ordered_.reserve(orderedPairs.size());
    for (const auto& [key, order] : orderedPairs) {
        (void)order;
        ordered_.push_back(key);
    }
}

bool hotkeys::KeyTracker::OnWindowMessage(UINT message, WPARAM wparam) {
    const auto keyInfo = GetMessageKeyInfo(message, wparam);
    if (!keyInfo.has_value()) {
        return false;
    }

    if (keyInfo->isDown) {
        KeyDown(keyInfo->keyCode);
    } else if (keyInfo->isUp) {
        KeyUp(keyInfo->keyCode);
    }
    return true;
}

const std::vector<UINT>& hotkeys::KeyTracker::Ordered() const {
    return ordered_;
}

void hotkeys::Capture::Start(const std::vector<UINT>& initial) {
    active_ = true;
    tracker_.Reset();
    lastCombo_ = initial;
    mouseCaptureArmed_ = false;
    mousePendingKey_ = 0;
}

void hotkeys::Capture::Stop() {
    active_ = false;
    tracker_.Reset();
    lastCombo_.clear();
    mouseCaptureArmed_ = false;
    mousePendingKey_ = 0;
}

bool hotkeys::Capture::Active() const {
    return active_;
}

void hotkeys::Capture::Clear() {
    tracker_.Reset();
    lastCombo_.clear();
    mouseCaptureArmed_ = false;
    mousePendingKey_ = 0;
}

void hotkeys::Capture::ArmMouseCapture() {
    if (!active_) {
        return;
    }
    mouseCaptureArmed_ = true;
    mousePendingKey_ = 0;
}

bool hotkeys::Capture::MouseCaptureArmed() const {
    return mouseCaptureArmed_;
}

std::vector<UINT> hotkeys::Capture::Draft() const {
    if (!tracker_.Ordered().empty()) {
        return tracker_.Ordered();
    }
    return lastCombo_;
}

bool hotkeys::Capture::Save(std::vector<UINT>& outKeys) {
    outKeys = Draft();
    Stop();
    return true;
}

bool hotkeys::Capture::OnWindowMessage(UINT message, WPARAM wparam, bool& canceled, bool& saved, std::vector<UINT>& outKeys) {
    canceled = false;
    saved = false;
    outKeys.clear();
    if (!active_) {
        return false;
    }

    const auto keyInfo = GetMessageKeyInfo(message, wparam);
    if (!keyInfo.has_value()) {
        return false;
    }

    const UINT key = keyInfo->keyCode;
    if (IsMouseKey(key) && !mouseCaptureArmed_) {
        return false;
    }

    if (keyInfo->isDown) {
        if (key == VK_ESCAPE) {
            Stop();
            canceled = true;
            return true;
        }
        if (key == VK_RETURN) {
            Save(outKeys);
            saved = true;
            return true;
        }
        if (key == VK_BACK) {
            Clear();
            return true;
        }

        tracker_.KeyDown(key);
        if (!tracker_.Ordered().empty()) {
            lastCombo_ = tracker_.Ordered();
        }
        if (IsMouseKey(key)) {
            mousePendingKey_ = key;
        }
        return true;
    }

    if (keyInfo->isUp) {
        tracker_.KeyUp(key);
        if (IsMouseKey(key) && mousePendingKey_ == key) {
            mouseCaptureArmed_ = false;
            mousePendingKey_ = 0;
        }
        return true;
    }

    return false;
}

bool CheckCondition(ConditionId condition, SampApi* sampApi) {
    auto* player = FindPlayerPed();
    if (!player) {
        return false;
    }

    switch (condition) {
    case ConditionId::InWater:
        return plugin::Command<plugin::Commands::IS_CHAR_IN_WATER>(player);
    case ConditionId::Dead:
        return plugin::Command<plugin::Commands::IS_CHAR_DEAD>(player);
    case ConditionId::InAir:
        return plugin::Command<plugin::Commands::IS_CHAR_IN_AIR>(player);
    case ConditionId::InAnyCar:
        return plugin::Command<plugin::Commands::IS_CHAR_IN_ANY_CAR>(player);
    case ConditionId::WithoutWeapon: {
        int weapon = 0;
        plugin::Command<plugin::Commands::GET_CURRENT_CHAR_WEAPON>(player, &weapon);
        return weapon == 0;
    }
    case ConditionId::WithWeapon: {
        int weapon = 0;
        plugin::Command<plugin::Commands::GET_CURRENT_CHAR_WEAPON>(player, &weapon);
        return weapon != 0;
    }
    case ConditionId::OnFoot:
        return plugin::Command<plugin::Commands::IS_CHAR_ON_FOOT>(player);
    case ConditionId::ChatOpened:
        return sampApi ? sampApi->is_chat_opened() : false;
    case ConditionId::DialogOpened:
        return sampApi ? sampApi->isDialogActive() : false;
    case ConditionId::Count:
        break;
    }

    return false;
}

bool ConditionsBlock(const std::vector<bool>& flags, SampApi* sampApi, std::string* message) {
    for (std::size_t i = 0; i < flags.size() && i < static_cast<std::size_t>(ConditionId::Count); ++i) {
        if (!flags[i]) {
            continue;
        }
        if (CheckCondition(static_cast<ConditionId>(i), sampApi)) {
            if (message) {
                *message = ConditionLabel(static_cast<ConditionId>(i));
            }
            return true;
        }
    }
    return false;
}

bool InputModeUsesButtons(InputMode mode) {
    return mode == InputMode::ButtonsList || mode == InputMode::ButtonsListText;
}

InputMode NormalizeInputMode(std::string_view value) {
    const std::string normalized = ToLower(value);
    if (normalized == "buttons" || normalized == "buttons_combo" || normalized == "buttons_list_text") {
        return InputMode::ButtonsListText;
    }
    if (normalized == "buttons_list") {
        return InputMode::ButtonsList;
    }
    return InputMode::Text;
}

std::string InputModeId(InputMode mode) {
    switch (mode) {
    case InputMode::Text:
        return "text";
    case InputMode::ButtonsList:
        return "buttons_list";
    case InputMode::ButtonsListText:
        return "buttons_list_text";
    }
    return "text";
}

HotkeyMode NormalizeHotkeyMode(std::string_view value) {
    return ToLower(value) == "ordered_combo" ? HotkeyMode::OrderedCombo : HotkeyMode::ModifierTrigger;
}

std::string HotkeyModeId(HotkeyMode mode) {
    return mode == HotkeyMode::OrderedCombo ? "ordered_combo" : "modifier_trigger";
}

QuickMenuActivationMode NormalizeQuickMenuActivationMode(std::string_view value) {
    return ToLower(value) == "toggle" ? QuickMenuActivationMode::Toggle : QuickMenuActivationMode::Hold;
}

std::string QuickMenuActivationModeId(QuickMenuActivationMode mode) {
    return mode == QuickMenuActivationMode::Toggle ? "toggle" : "hold";
}

std::string NormalizeInputKey(std::string_view value) {
    std::string key;
    key.reserve(value.size());
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) != 0) {
            key.push_back(static_cast<char>(std::toupper(ch)));
        } else if (ch == '_' || std::isspace(ch) != 0) {
            key.push_back('_');
        }
    }

    while (key.find("__") != std::string::npos) {
        key.replace(key.find("__"), 2, "_");
    }
    while (!key.empty() && key.front() == '_') {
        key.erase(key.begin());
    }
    while (!key.empty() && key.back() == '_') {
        key.pop_back();
    }
    return key;
}

std::vector<InputButton> ParseButtonsText(std::string_view multiLine) {
    std::vector<InputButton> buttons;
    std::istringstream stream(NormalizeLineEndings(multiLine));
    std::string line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        const auto parts = Split(line, '|');
        InputButton button;
        if (!parts.empty()) {
            button.label = Trim(parts[0]);
        }
        if (parts.size() > 1) {
            button.text = Trim(parts[1]);
        }
        if (parts.size() > 2) {
            button.hint = Trim(parts[2]);
        }
        if (parts.size() > 3) {
            button.when = Trim(parts[3]);
        }
        buttons.push_back(std::move(button));
    }
    return buttons;
}

std::string SerializeButtonsText(const std::vector<InputButton>& buttons) {
    std::ostringstream stream;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const InputButton& button = buttons[i];
        stream << button.label << " | " << button.text;
        if (!button.hint.empty() || !button.when.empty()) {
            stream << " | " << button.hint;
        }
        if (!button.when.empty()) {
            stream << " | " << button.when;
        }
        if (i + 1 != buttons.size()) {
            stream << "\n";
        }
    }
    return stream.str();
}

namespace {

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

} // namespace

bool InputTextString(const char* label, std::string& value, ImGuiInputTextFlags flags, std::size_t minBuffer) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value, nullptr, nullptr };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputText(label, value.data(), value.capacity() + 1, flags, ImGuiStringResizeCallback, &userData);
}

bool InputTextMultilineString(
    const char* label,
    std::string& value,
    const ImVec2& size,
    ImGuiInputTextFlags flags,
    std::size_t minBuffer) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value, nullptr, nullptr };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputTextMultiline(
        label, value.data(), value.capacity() + 1, size, flags, ImGuiStringResizeCallback, &userData);
}

JsonValue SerializeBoolArray(const std::vector<bool>& flags) {
    JsonArray array;
    array.reserve(flags.size());
    for (const bool flag : flags) {
        array.emplace_back(flag);
    }
    return JsonValue(std::move(array));
}

std::vector<bool> DeserializeBoolArray(const JsonArray* array) {
    std::vector<bool> flags;
    if (!array) {
        return flags;
    }
    flags.reserve(array->size());
    for (const JsonValue& item : *array) {
        if (const bool* flag = item.TryBool()) {
            flags.push_back(*flag);
        } else {
            flags.push_back(false);
        }
    }
    return flags;
}

JsonValue SerializeUintArray(const std::vector<UINT>& values) {
    JsonArray array;
    array.reserve(values.size());
    for (const UINT value : values) {
        array.emplace_back(static_cast<double>(value));
    }
    return JsonValue(std::move(array));
}

std::vector<UINT> DeserializeUintArray(const JsonArray* array) {
    std::vector<UINT> values;
    if (!array) {
        return values;
    }
    values.reserve(array->size());
    for (const JsonValue& item : *array) {
        if (const double* number = item.TryNumber()) {
            values.push_back(static_cast<UINT>(*number));
        }
    }
    return values;
}

JsonValue SerializeStringArray(const std::vector<std::string>& values) {
    JsonArray array;
    array.reserve(values.size());
    for (const std::string& value : values) {
        array.emplace_back(value);
    }
    return JsonValue(std::move(array));
}

std::vector<std::string> DeserializeStringArray(const JsonArray* array) {
    std::vector<std::string> values;
    if (!array) {
        return values;
    }
    values.reserve(array->size());
    for (const JsonValue& item : *array) {
        if (const std::string* text = item.TryString()) {
            values.push_back(*text);
        }
    }
    return values;
}

FolderNode* FindFolderByPath(const std::vector<std::unique_ptr<FolderNode>>& folders, const std::vector<std::string>& path) {
    if (path.empty()) {
        return folders.empty() ? nullptr : folders.front().get();
    }

    for (const auto& folder : folders) {
        FolderNode* current = folder.get();
        if (!current || current->name != path.front()) {
            continue;
        }
        if (path.size() == 1) {
            return current;
        }

        for (std::size_t index = 1; current && index < path.size(); ++index) {
            FolderNode* next = nullptr;
            for (const auto& child : current->children) {
                if (child && child->name == path[index]) {
                    next = child.get();
                    break;
                }
            }
            current = next;
        }
        if (current) {
            return current;
        }
    }
    return nullptr;
}

void CollectFolderPaths(
    const std::vector<std::unique_ptr<FolderNode>>& folders,
    std::vector<std::vector<std::string>>& out,
    std::vector<std::string> prefix) {
    for (const auto& folder : folders) {
        if (!folder) {
            continue;
        }
        auto current = prefix;
        current.push_back(folder->name);
        out.push_back(current);
        CollectFolderPaths(folder->children, out, std::move(current));
    }
}

std::vector<std::string> BuildFolderPath(const FolderNode* folder) {
    std::vector<std::string> path;
    for (auto* current = folder; current; current = current->parent) {
        path.push_back(current->name);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

bool PathStartsWith(const std::vector<std::string>& path, const std::vector<std::string>& prefix) {
    return path.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), path.begin());
}

std::vector<std::string> ReplacePathPrefix(
    const std::vector<std::string>& path,
    const std::vector<std::string>& oldPrefix,
    const std::vector<std::string>& newPrefix) {
    if (!PathStartsWith(path, oldPrefix)) {
        return path;
    }

    std::vector<std::string> result;
    result.reserve(newPrefix.size() + (path.size() - oldPrefix.size()));
    result.insert(result.end(), newPrefix.begin(), newPrefix.end());
    result.insert(result.end(), path.begin() + static_cast<std::ptrdiff_t>(oldPrefix.size()), path.end());
    return result;
}

bool FolderNameUnique(
    const std::vector<std::unique_ptr<FolderNode>>& folders,
    std::string_view name,
    const FolderNode* ignoredFolder = nullptr) {
    for (const auto& folder : folders) {
        if (!folder || folder.get() == ignoredFolder) {
            continue;
        }
        if (folder->name == name) {
            return false;
        }
    }
    return true;
}

void ExpandFolderBranch(FolderNode* folder) {
    for (FolderNode* current = folder; current; current = current->parent) {
        current->open = true;
    }
}

bool FolderMatchesSearch(const FolderNode& folder, std::string_view query) {
    const std::string normalizedQuery = ToLower(Trim(query));
    if (normalizedQuery.empty()) {
        return true;
    }

    if (ToLower(folder.name).find(normalizedQuery) != std::string::npos) {
        return true;
    }
    for (const auto& child : folder.children) {
        if (child && FolderMatchesSearch(*child, normalizedQuery)) {
            return true;
        }
    }
    return false;
}

} // namespace

struct BinderModule::Impl {
    HMODULE module = nullptr;
    std::filesystem::path legacyConfigPath{};
    SampApi* sampApi = nullptr;
    SampHooks* sampHooks = nullptr;
    SampRakHooks* sampRakHooks = nullptr;

    std::vector<std::unique_ptr<FolderNode>> folders{};
    std::vector<HotkeyEntry> hotkeys{};
    FolderNode* selectedFolder = nullptr;
    int nextFolderId = 1;
    bool configLoaded = false;
    bool chatHookBound = false;
    bool rakHooksBound = false;

    std::string bindSearch{};
    std::string folderSearch{};

    struct EditorState {
        bool active = false;
        bool isNew = false;
        int hotkeyIndex = -1;
        int selectedInputIndex = -1;
        std::string selectedButtonsText{};
        HotkeyEntry draft{};
    } editor{};

    struct FolderPopupState {
        FolderNode* target = nullptr;
        FolderNode* parent = nullptr;
        std::string name{};
    } folderPopup{};

    FolderNode* folderDeleteTarget = nullptr;
    int bindDeleteTarget = -1;
    bool bindDeletePopupPending = false;
    int moveBindTarget = -1;
    bool moveBindPopupPending = false;
    int bindLinesTarget = -1;
    int selectedBindIndex = -1;
    bool bindLinesPopupPending = false;

    hotkeys::KeyTracker keyTracker{};
    std::vector<UINT> pressedKeys{};
    hotkeys::Capture capture{};
    CaptureTarget captureTarget = CaptureTarget::None;
    int captureHotkeyIndex = -1;
    bool editorPopupPending = false;
    bool capturePopupPending = false;
    bool capturePopupInEditor = false;

    std::vector<UINT> quickMenuHotkey{};
    QuickMenuActivationMode quickMenuActivationMode = QuickMenuActivationMode::Hold;
    bool quickMenuOpen = false;
    bool quickMenuReopenBlocked = false;
    bool quickMenuToggleLatch = false;
    int quickMenuTabIndex = 0;
    ImVec2 quickMenuPos{ 0.0f, 0.0f };
    ImVec2 quickMenuSize{ static_cast<float>(kQuickMenuWidth), static_cast<float>(kQuickMenuHeight) };

    std::optional<InputDialogState> inputDialog{};
    std::vector<RunningBind> runningBinds{};
    std::deque<Toast> toasts{};
    std::vector<OutgoingGuard> outgoingGuards{};

    void EnsureInitialized();
    void OnProcessAttach(HMODULE moduleHandle);
    void SetSampApi(SampApi* api);
    void SetSampHooks(SampHooks* hooks);
    void SetSampRakHooks(SampRakHooks* hooks);
    void ConnectHooks();
    FolderNode* EnsureRootFolder();
    HotkeyEntry MakeDefaultHotkey() const;
    void RefreshNumbers();
    void SaveConfig();
    void LoadConfig();
    JsonValue SerializeFolder(const FolderNode& folder) const;
    std::unique_ptr<FolderNode> DeserializeFolder(const JsonObject& object, FolderNode* parent);
    JsonValue SerializeHotkey(const HotkeyEntry& hotkey) const;
    HotkeyEntry DeserializeHotkey(const JsonObject& object) const;
    void PushToast(std::string text, const ImVec4& color, double durationMs);
    void PruneToasts();
    void DrawToasts();
    bool VisibleQuickMenuEntriesExist() const;
    bool FolderVisibleInQuickMenu(const FolderNode& folder) const;
    bool FolderHasVisibleQuickEntries(const FolderNode& folder) const;
    std::vector<int> QuickEntriesForFolder(const FolderNode& folder) const;
    void ResetInputState();
    void Tick();
    void Shutdown();
    bool WantsOverlayRender() const;
    bool WantsInputCapture() const;
    bool WantsQuickMenuCursor() const;
    bool OnWindowMessage(UINT message, WPARAM wparam, LPARAM lparam);
    void ApplyCapturedKeys(const std::vector<UINT>& keys);
    std::vector<UINT> CurrentQuickMenuHotkey() const;
    bool IsQuickMenuComboPressed() const;
    bool CaptureUsesEditorPopup() const;
    void UpdateQuickMenuState();
    void ProcessHotkeys();
    void ProcessRunningBinds();
    void PruneOutgoingGuards();
    void RegisterOutgoingGuard(std::string kind, std::string text);
    bool ConsumeOutgoingGuard(std::string_view kind, std::string_view text);
    std::string NormalizeActivationText(std::string_view text) const;
    bool MatchesActivationCommand(std::string_view input, std::string_view command) const;
    void OnOutgoingCommand(const std::string& text);
    void OnOutgoingChat(const std::string& text);
    void OnIncomingTextMessage(const std::string& text, std::string_view source);
    void ExpireTextConfirmations();
    bool ActivatePendingTextConfirmations(UINT keyCode);
    bool MatchTextTrigger(const std::string& source, const HotkeyEntry& hotkey);
    void OnTextTriggerEvent(const std::string& sourceText, std::string_view sourceKind);
    std::string ApplyInputValues(std::string text, const std::map<std::string, std::string>& values) const;
    std::string BuildInputValue(const InputDialogField& field) const;
    std::vector<int> FilterButtons(const InputDialogState& dialog, std::size_t fieldIndex) const;
    bool TryEnqueueHotkey(HotkeyEntry& hotkey, int startDelayMs, std::string_view source, const std::string& sourceText);
    bool TryEnqueueHotkey(int index, int startDelayMs, std::string_view source, const std::string& sourceText);
    void DoSend(const std::string& text, int method);
    int RemapHotkeysFolderPrefix(const std::vector<std::string>& oldPath, const std::vector<std::string>& newPath);
    int MoveHotkeysFromFolderPath(const std::vector<std::string>& fromPath, const std::vector<std::string>& toPath);
    void BeginCapture(CaptureTarget target);
    void DrawCapturePopup(bool insideEditorPopup);
    void DrawQuickMenu();
    void DrawSettingsSection();
    void DrawQuickFolderRecursive(FolderNode& folder);
    void DrawInputDialog();
    std::vector<int> FilteredBindIndices() const;
    std::string BuildLaunchSummary(const HotkeyEntry& hotkey) const;
    void StartEditing(int index, bool isNew);
    bool ValidateEditor(std::vector<std::string>& errors);
    void SaveEditor();
    void DrawFolderTreeNode(FolderNode& folder);
    void DrawFolderPane();
    void DrawFolderPopups();
    void DrawBindPane();
    void DrawMoveBindPopup();
    void DrawBindLinesPopup();
    void DrawInputEditor();
    void DrawEditor();
    void DuplicateHotkeyAt(int index);
    void DrawMainTab();
    void DrawOverlay();
};

namespace {

std::pair<std::string, std::uint32_t> ParseLeadingChatColor(std::string_view text) {
    static const std::regex kColor8("^\\{([0-9A-Fa-f]{8})\\}(.*)$");
    static const std::regex kColor6("^\\{([0-9A-Fa-f]{6})\\}(.*)$");
    std::cmatch match;
    const std::string source(text);
    if (std::regex_match(source.c_str(), match, kColor8) && match.size() == 3) {
        return { match[2].str(), static_cast<std::uint32_t>(std::strtoul(match[1].str().c_str(), nullptr, 16)) };
    }
    if (std::regex_match(source.c_str(), match, kColor6) && match.size() == 3) {
        return { match[2].str(), static_cast<std::uint32_t>(std::strtoul(match[1].str().c_str(), nullptr, 16)) };
    }
    return { source, 0xFFFFFFFFu };
}

bool SetClipboardUtf8Text(std::string_view utf8Text) {
    const int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8Text.data(), static_cast<int>(utf8Text.size()), nullptr, 0);
    if (wideLength <= 0) {
        return false;
    }

    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>((wideLength + 1) * sizeof(wchar_t)));
    if (!handle) {
        return false;
    }

    auto* wideText = static_cast<wchar_t*>(GlobalLock(handle));
    if (!wideText) {
        GlobalFree(handle);
        return false;
    }

    MultiByteToWideChar(CP_UTF8, 0, utf8Text.data(), static_cast<int>(utf8Text.size()), wideText, wideLength);
    wideText[wideLength] = L'\0';
    GlobalUnlock(handle);

    if (!OpenClipboard(nullptr)) {
        GlobalFree(handle);
        return false;
    }

    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, handle)) {
        CloseClipboard();
        GlobalFree(handle);
        return false;
    }

    CloseClipboard();
    return true;
}

} // namespace

namespace {

constexpr std::string_view kBinderConfigSectionName = "binder";

std::optional<JsonObject> ParseLegacyBinderObject(std::string_view content, std::string& error) {
    JsonParser parser(content);
    const auto rootValue = parser.Parse(error);
    const JsonObject* root = rootValue ? rootValue->TryObject() : nullptr;
    if (!root) {
        return std::nullopt;
    }

    return *root;
}

std::optional<JsonObject> ConvertSharedBinderObject(const jsonutil::JsonObject& object, std::string& error) {
    std::string serialized;
    jsonutil::WriteJson(jsonutil::JsonValue(object), serialized, 0);
    return ParseLegacyBinderObject(serialized, error);
}

jsonutil::JsonValue ConvertLegacyBinderValue(const JsonValue& value) {
    std::string serialized;
    WriteJson(value, serialized, 0);

    std::string error;
    const auto parsed = jsonutil::ParseJson(serialized, error);
    if (!parsed) {
        debuglog::Write("Binder: failed to convert config to shared json: %s", error.c_str());
        return jsonutil::JsonValue(nullptr);
    }

    return *parsed;
}

} // namespace

void BinderModule::Impl::EnsureInitialized() {
    if (configLoaded) {
        ConnectHooks();
        EnsureRootFolder();
        return;
    }

    LoadConfig();
    configLoaded = true;
    EnsureRootFolder();
    RefreshNumbers();
    ConnectHooks();
}

void BinderModule::Impl::OnProcessAttach(HMODULE moduleHandle) {
    module = moduleHandle;
    WCHAR path[MAX_PATH]{};
    if (module && GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path))) > 0) {
        legacyConfigPath = std::filesystem::path(path).parent_path() / kLegacyConfigFileName;
    } else {
        legacyConfigPath = std::filesystem::current_path() / kLegacyConfigFileName;
    }
}

void BinderModule::Impl::SetSampApi(SampApi* api) {
    sampApi = api;
    ConnectHooks();
}

void BinderModule::Impl::SetSampHooks(SampHooks* hooks) {
    sampHooks = hooks;
    ConnectHooks();
}

void BinderModule::Impl::SetSampRakHooks(SampRakHooks* hooks) {
    sampRakHooks = hooks;
    ConnectHooks();
}

void BinderModule::Impl::ConnectHooks() {
    if (!chatHookBound && sampHooks) {
        sampHooks->AddOnChatMessageHandler([this](
                                              int type,
                                              const std::string& text,
                                              const std::string& prefix,
                                              std::uint32_t textColor,
                                              std::uint32_t prefixColor) {
            (void)type;
            (void)textColor;
            (void)prefixColor;
            if (!prefix.empty()) {
                OnIncomingTextMessage(prefix + " " + text, "incoming_server");
            }
            OnIncomingTextMessage(text, "incoming_server");
        });
        chatHookBound = true;
    }

    if (!rakHooksBound && sampRakHooks) {
        sampRakHooks->AddOnSendCommandHandler([this](std::string& text) {
            OnOutgoingCommand(ToUtf8ForDisplay(text));
            return true;
        });
        sampRakHooks->AddOnSendChatHandler([this](std::string& text) {
            OnOutgoingChat(ToUtf8ForDisplay(text));
            return true;
        });
        sampRakHooks->AddOnServerMessageHandler([this](std::int32_t& color, std::string& text) {
            (void)color;
            OnIncomingTextMessage(ToUtf8ForDisplay(text), "incoming_server");
            return true;
        });
        rakHooksBound = true;
    }
}

FolderNode* BinderModule::Impl::EnsureRootFolder() {
    if (folders.empty()) {
        auto root = std::make_unique<FolderNode>();
        root->id = nextFolderId++;
        root->name = UiSettings::Instance().Text(UiText::BinderDefaultRootFolder);
        root->quickConditions.assign(static_cast<std::size_t>(ConditionId::Count), false);
        folders.push_back(std::move(root));
    }

    if (!selectedFolder) {
        selectedFolder = folders.front().get();
    }
    return folders.front().get();
}

HotkeyEntry BinderModule::Impl::MakeDefaultHotkey() const {
    HotkeyEntry hotkey;
    hotkey.label = UiSettings::Instance().Text(UiText::BinderDefaultHotkey);
    hotkey.hotkeyMode = HotkeyMode::ModifierTrigger;
    hotkey.messages.push_back(HotkeyMessage{ "", 0, 0 });
    hotkey.conditions.assign(static_cast<std::size_t>(ConditionId::Count), false);
    hotkey.quickConditions.assign(static_cast<std::size_t>(ConditionId::Count), false);
    hotkey.repeatIntervalMs = kDefaultRepeatIntervalMs;
    hotkey.textConfirmation = TextConfirmation{};
    return hotkey;
}

void BinderModule::Impl::RefreshNumbers() {
    int number = 1;
    for (HotkeyEntry& hotkey : hotkeys) {
        hotkey.number = number++;
    }
}

void BinderModule::Impl::SaveConfig() {
    EnsureRootFolder();
    JsonObject root;
    root["quick_menu_hotkey"] = SerializeUintArray(quickMenuHotkey);
    root["quick_menu_activation_mode"] = QuickMenuActivationModeId(quickMenuActivationMode);

    JsonArray folderArray;
    for (const auto& folder : folders) {
        if (folder) {
            folderArray.push_back(SerializeFolder(*folder));
        }
    }
    root["folders"] = JsonValue(std::move(folderArray));

    JsonArray hotkeyArray;
    for (const HotkeyEntry& hotkey : hotkeys) {
        hotkeyArray.push_back(SerializeHotkey(hotkey));
    }
    root["hotkeys"] = JsonValue(std::move(hotkeyArray));

    const jsonutil::JsonValue sharedRoot = ConvertLegacyBinderValue(JsonValue(std::move(root)));
    if (sharedRoot.IsNull()) {
        return;
    }

    AppConfig::Instance().QueueSectionReplace(std::string(kBinderConfigSectionName), sharedRoot);
}

void BinderModule::Impl::LoadConfig() {
    folders.clear();
    hotkeys.clear();
    selectedFolder = nullptr;
    nextFolderId = 1;
    quickMenuHotkey.clear();
    quickMenuActivationMode = QuickMenuActivationMode::Hold;
    std::optional<JsonObject> loadedRoot;
    bool migratedLegacy = false;

    const jsonutil::JsonValue sharedSection = AppConfig::Instance().ReadSection(kBinderConfigSectionName);
    if (const jsonutil::JsonObject* sharedRoot = sharedSection.TryObject()) {
        std::string error;
        loadedRoot = ConvertSharedBinderObject(*sharedRoot, error);
        if (!loadedRoot) {
            debuglog::Write("Binder: failed to read unified config section, using defaults: %s", error.c_str());
        }
    }

    if (!loadedRoot && !legacyConfigPath.empty() && std::filesystem::exists(legacyConfigPath)) {
        std::ifstream file(legacyConfigPath, std::ios::binary);
        if (file) {
            const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            std::string error;
            loadedRoot = ParseLegacyBinderObject(content, error);
            if (!loadedRoot) {
                debuglog::Write("Binder: invalid legacy config, using defaults: %s", error.c_str());
            } else {
                migratedLegacy = true;
            }
        }
    }

    if (!loadedRoot) {
        EnsureRootFolder();
        return;
    }

    const JsonObject* root = &*loadedRoot;

    quickMenuHotkey = ::hotkeys::NormalizeCombo(
        DeserializeUintArray(JsonArrayOrNull(root, "quick_menu_hotkey")), HotkeyMode::ModifierTrigger);
    quickMenuActivationMode = NormalizeQuickMenuActivationMode(JsonStringOr(root, "quick_menu_activation_mode", "hold"));

    if (const JsonArray* folderArray = JsonArrayOrNull(root, "folders")) {
        for (const JsonValue& item : *folderArray) {
            if (const JsonObject* object = item.TryObject()) {
                auto folder = DeserializeFolder(*object, nullptr);
                if (folder) {
                    folders.push_back(std::move(folder));
                }
            }
        }
    }

    if (const JsonArray* hotkeyArray = JsonArrayOrNull(root, "hotkeys")) {
        for (const JsonValue& item : *hotkeyArray) {
            if (const JsonObject* object = item.TryObject()) {
                hotkeys.push_back(DeserializeHotkey(*object));
            }
        }
    }

    EnsureRootFolder();
    selectedFolder = folders.front().get();
    RefreshNumbers();

    if (migratedLegacy) {
        SaveConfig();
    }
}

JsonValue BinderModule::Impl::SerializeFolder(const FolderNode& folder) const {
    JsonObject object;
    object["name"] = folder.name;
    object["quick_menu"] = folder.quickMenu;
    object["quick_conditions"] = SerializeBoolArray(folder.quickConditions);

    JsonArray childrenArray;
    for (const auto& child : folder.children) {
        if (child) {
            childrenArray.push_back(SerializeFolder(*child));
        }
    }
    object["children"] = JsonValue(std::move(childrenArray));
    return JsonValue(std::move(object));
}

std::unique_ptr<FolderNode> BinderModule::Impl::DeserializeFolder(const JsonObject& object, FolderNode* parent) {
    auto folder = std::make_unique<FolderNode>();
    folder->id = nextFolderId++;
    folder->parent = parent;
    folder->name = SanitizeFolderName(JsonStringOr(&object, "name", UiSettings::Instance().Text(UiText::BinderDefaultFolder)));
    if (folder->name.empty()) {
        folder->name = UiSettings::Instance().Text(UiText::BinderDefaultFolder);
    }
    folder->quickMenu = JsonBoolOr(&object, "quick_menu", true);
    folder->quickConditions = DeserializeBoolArray(JsonArrayOrNull(&object, "quick_conditions"));
    if (folder->quickConditions.size() < static_cast<std::size_t>(ConditionId::Count)) {
        folder->quickConditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    }

    if (const JsonArray* children = JsonArrayOrNull(&object, "children")) {
        for (const JsonValue& childValue : *children) {
            if (const JsonObject* childObject = childValue.TryObject()) {
                auto child = DeserializeFolder(*childObject, folder.get());
                if (child) {
                    folder->children.push_back(std::move(child));
                }
            }
        }
    }
    return folder;
}

JsonValue BinderModule::Impl::SerializeHotkey(const HotkeyEntry& hotkey) const {
    JsonObject object;
    object["label"] = hotkey.label;
    object["keys"] = SerializeUintArray(hotkey.keys);
    object["hotkey_mode"] = HotkeyModeId(hotkey.hotkeyMode);
    object["conditions"] = SerializeBoolArray(hotkey.conditions);
    object["quick_conditions"] = SerializeBoolArray(hotkey.quickConditions);
    object["repeat_mode"] = hotkey.repeatMode;
    object["repeat_interval_ms"] = hotkey.repeatIntervalMs;
    object["enabled"] = hotkey.enabled;
    object["quick_menu"] = hotkey.quickMenu;
    object["command"] = hotkey.command;
    object["command_enabled"] = hotkey.commandEnabled;
    object["folder_path"] = SerializeStringArray(hotkey.folderPath);

    JsonObject trigger;
    trigger["text"] = hotkey.textTrigger.text;
    trigger["enabled"] = hotkey.textTrigger.enabled;
    trigger["pattern"] = hotkey.textTrigger.pattern;
    object["text_trigger"] = JsonValue(std::move(trigger));

    JsonObject confirmation;
    confirmation["enabled"] = hotkey.textConfirmation.enabled;
    confirmation["key"] = static_cast<double>(hotkey.textConfirmation.key);
    confirmation["cancel_key"] = static_cast<double>(hotkey.textConfirmation.cancelKey);
    confirmation["wait_for_resolution"] = hotkey.textConfirmation.waitForResolution;
    object["text_confirmation"] = JsonValue(std::move(confirmation));

    JsonArray messages;
    for (const HotkeyMessage& message : hotkey.messages) {
        JsonObject item;
        item["text"] = message.text;
        item["interval_ms"] = message.intervalMs;
        item["method"] = message.method;
        messages.emplace_back(std::move(item));
    }
    object["messages"] = JsonValue(std::move(messages));

    JsonArray inputs;
    for (const HotkeyInput& input : hotkey.inputs) {
        JsonObject item;
        item["key"] = input.key;
        item["label"] = input.label;
        item["hint"] = input.hint;
        item["mode"] = InputModeId(input.mode);
        item["multi_select"] = input.multiSelect;
        item["multi_separator"] = input.multiSeparator;
        item["cascade_parent_key"] = input.cascadeParentKey;

        JsonArray buttons;
        for (const InputButton& button : input.buttons) {
            JsonObject buttonObject;
            buttonObject["label"] = button.label;
            buttonObject["text"] = button.text;
            buttonObject["hint"] = button.hint;
            buttonObject["when"] = button.when;
            buttons.emplace_back(std::move(buttonObject));
        }
        item["buttons"] = JsonValue(std::move(buttons));
        inputs.emplace_back(std::move(item));
    }
    object["inputs"] = JsonValue(std::move(inputs));
    return JsonValue(std::move(object));
}

HotkeyEntry BinderModule::Impl::DeserializeHotkey(const JsonObject& object) const {
    HotkeyEntry hotkey = MakeDefaultHotkey();
    hotkey.label = JsonStringOr(&object, "label", hotkey.label);
    hotkey.hotkeyMode = NormalizeHotkeyMode(JsonStringOr(&object, "hotkey_mode", "modifier_trigger"));
    hotkey.keys = ::hotkeys::NormalizeCombo(
        DeserializeUintArray(JsonArrayOrNull(&object, "keys")), hotkey.hotkeyMode);
    hotkey.conditions = DeserializeBoolArray(JsonArrayOrNull(&object, "conditions"));
    hotkey.quickConditions = DeserializeBoolArray(JsonArrayOrNull(&object, "quick_conditions"));
    hotkey.conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    hotkey.quickConditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    hotkey.repeatMode = JsonBoolOr(&object, "repeat_mode", false);
    hotkey.repeatIntervalMs = JsonNumberOr<int>(&object, "repeat_interval_ms", kDefaultRepeatIntervalMs);
    hotkey.enabled = JsonBoolOr(&object, "enabled", true);
    hotkey.quickMenu = JsonBoolOr(&object, "quick_menu", false);
    hotkey.command = JsonStringOr(&object, "command", "");
    hotkey.commandEnabled = JsonBoolOr(&object, "command_enabled", false);
    hotkey.folderPath = DeserializeStringArray(JsonArrayOrNull(&object, "folder_path"));

    if (const JsonObject* trigger = JsonObjectOrNull(&object, "text_trigger")) {
        hotkey.textTrigger.text = JsonStringOr(trigger, "text", "");
        hotkey.textTrigger.enabled = JsonBoolOr(trigger, "enabled", false);
        hotkey.textTrigger.pattern = JsonBoolOr(trigger, "pattern", false);
    }

    if (const JsonObject* confirmation = JsonObjectOrNull(&object, "text_confirmation")) {
        hotkey.textConfirmation.enabled = JsonBoolOr(confirmation, "enabled", false);
        hotkey.textConfirmation.key = static_cast<UINT>(JsonNumberOr<double>(confirmation, "key", kDefaultConfirmKey));
        hotkey.textConfirmation.cancelKey =
            static_cast<UINT>(JsonNumberOr<double>(confirmation, "cancel_key", kDefaultCancelKey));
        hotkey.textConfirmation.waitForResolution = JsonBoolOr(confirmation, "wait_for_resolution", true);
    }

    hotkey.messages.clear();
    if (const JsonArray* messages = JsonArrayOrNull(&object, "messages")) {
        for (const JsonValue& messageValue : *messages) {
            const JsonObject* message = messageValue.TryObject();
            if (!message) {
                continue;
            }
            hotkey.messages.push_back(HotkeyMessage{
                JsonStringOr(message, "text", ""),
                JsonNumberOr<int>(message, "interval_ms", 0),
                JsonNumberOr<int>(message, "method", 0),
            });
        }
    }
    if (hotkey.messages.empty()) {
        hotkey.messages.push_back(HotkeyMessage{ "", 0, 0 });
    }

    hotkey.inputs.clear();
    if (const JsonArray* inputs = JsonArrayOrNull(&object, "inputs")) {
        for (const JsonValue& inputValue : *inputs) {
            const JsonObject* inputObject = inputValue.TryObject();
            if (!inputObject) {
                continue;
            }

            HotkeyInput input;
            input.key = NormalizeInputKey(JsonStringOr(inputObject, "key", ""));
            input.label = JsonStringOr(inputObject, "label", "");
            input.hint = JsonStringOr(inputObject, "hint", "");
            input.mode = NormalizeInputMode(JsonStringOr(inputObject, "mode", "text"));
            input.multiSelect = JsonBoolOr(inputObject, "multi_select", false);
            input.multiSeparator = JsonStringOr(inputObject, "multi_separator", ", ");
            input.cascadeParentKey = NormalizeInputKey(JsonStringOr(inputObject, "cascade_parent_key", ""));

            if (const JsonArray* buttons = JsonArrayOrNull(inputObject, "buttons")) {
                for (const JsonValue& buttonValue : *buttons) {
                    const JsonObject* buttonObject = buttonValue.TryObject();
                    if (!buttonObject) {
                        continue;
                    }
                    input.buttons.push_back(InputButton{
                        JsonStringOr(buttonObject, "label", ""),
                        JsonStringOr(buttonObject, "text", ""),
                        JsonStringOr(buttonObject, "hint", ""),
                        JsonStringOr(buttonObject, "when", ""),
                    });
                }
            }

            if (InputModeUsesButtons(input.mode) && input.buttons.empty()) {
                input.mode = InputMode::Text;
            }
            hotkey.inputs.push_back(std::move(input));
        }
    }

    return hotkey;
}

void BinderModule::Impl::PushToast(std::string text, const ImVec4& color, double durationMs) {
    if (text.empty()) {
        return;
    }

    const double now = static_cast<double>(GetTickCount64());
    toasts.push_back(Toast{ std::move(text), color, now + durationMs });
    while (toasts.size() > static_cast<std::size_t>(kMaxToasts)) {
        toasts.pop_front();
    }
}

void BinderModule::Impl::PruneToasts() {
    const double now = static_cast<double>(GetTickCount64());
    while (!toasts.empty() && toasts.front().expiresAtMs <= now) {
        toasts.pop_front();
    }
}

void BinderModule::Impl::DrawToasts() {
    PruneToasts();
    if (toasts.empty()) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - ScaleUi(20.0f), ScaleUi(20.0f)), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoNav;

    if (ImGui::Begin("##binder_toasts", nullptr, flags)) {
        for (std::size_t i = 0; i < toasts.size(); ++i) {
            const Toast& toast = toasts[i];
            ImGui::PushStyleColor(ImGuiCol_ChildBg, toast.color);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ScaleUi(6.0f));
            if (ImGui::BeginChild(
                    ("toast_" + std::to_string(i)).c_str(),
                    ImVec2(ScaleUi(320.0f), 0.0f),
                    ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                ImGui::TextWrapped("%s", toast.text.c_str());
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }
    }
    ImGui::End();
}

bool BinderModule::Impl::VisibleQuickMenuEntriesExist() const {
    for (const auto& folder : folders) {
        if (folder && FolderHasVisibleQuickEntries(*folder)) {
            return true;
        }
    }
    return false;
}

bool BinderModule::Impl::FolderVisibleInQuickMenu(const FolderNode& folder) const {
    if (!folder.quickMenu || ConditionsBlock(folder.quickConditions, sampApi)) {
        return false;
    }
    return !folder.parent || FolderVisibleInQuickMenu(*folder.parent);
}

bool BinderModule::Impl::FolderHasVisibleQuickEntries(const FolderNode& folder) const {
    if (!FolderVisibleInQuickMenu(folder)) {
        return false;
    }
    if (!QuickEntriesForFolder(folder).empty()) {
        return true;
    }
    for (const auto& child : folder.children) {
        if (child && FolderHasVisibleQuickEntries(*child)) {
            return true;
        }
    }
    return false;
}

std::vector<int> BinderModule::Impl::QuickEntriesForFolder(const FolderNode& folder) const {
    std::vector<int> result;
    const auto path = BuildFolderPath(&folder);
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        const HotkeyEntry& hotkey = hotkeys[i];
        if (!hotkey.quickMenu || !hotkey.enabled) {
            continue;
        }
        if (hotkey.folderPath != path) {
            continue;
        }
        if (ConditionsBlock(hotkey.quickConditions, sampApi)) {
            continue;
        }
        result.push_back(static_cast<int>(i));
    }
    return result;
}

void BinderModule::Impl::ResetInputState() {
    keyTracker.Reset();
    pressedKeys.clear();
    capture.Stop();
    captureTarget = CaptureTarget::None;
    captureHotkeyIndex = -1;
    capturePopupPending = false;
    capturePopupInEditor = false;
    quickMenuOpen = false;
    quickMenuToggleLatch = false;
    quickMenuReopenBlocked = false;

    if (inputDialog) {
        if (inputDialog->hotkeyIndex >= 0 && inputDialog->hotkeyIndex < static_cast<int>(hotkeys.size())) {
            hotkeys[inputDialog->hotkeyIndex].awaitingInput = false;
        }
        inputDialog.reset();
    }
}

void BinderModule::Impl::Tick() {
    EnsureInitialized();
    PruneOutgoingGuards();
    ExpireTextConfirmations();
    UpdateQuickMenuState();
    ProcessHotkeys();
    ProcessRunningBinds();
    PruneToasts();
}

void BinderModule::Impl::Shutdown() {
    if (inputDialog && inputDialog->hotkeyIndex >= 0 && inputDialog->hotkeyIndex < static_cast<int>(hotkeys.size())) {
        hotkeys[inputDialog->hotkeyIndex].awaitingInput = false;
    }

    editor.active = false;
    editorPopupPending = false;
    inputDialog.reset();
    runningBinds.clear();
    toasts.clear();
    outgoingGuards.clear();
    ResetInputState();
}

bool BinderModule::Impl::WantsOverlayRender() const {
    return quickMenuOpen || inputDialog.has_value() || capture.Active() || !toasts.empty();
}

bool BinderModule::Impl::WantsInputCapture() const {
    return inputDialog.has_value() || capture.Active();
}

bool BinderModule::Impl::WantsQuickMenuCursor() const {
    return quickMenuOpen;
}

bool BinderModule::Impl::OnWindowMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    (void)lparam;
    EnsureInitialized();

    const WORD activateState = LOWORD(static_cast<DWORD>(wparam));
    const bool lostFocus = message == WM_KILLFOCUS
        || (message == WM_ACTIVATEAPP && wparam == 0)
        || (message == WM_ACTIVATE && activateState == WA_INACTIVE);
    const bool gainedFocus = message == WM_SETFOCUS
        || (message == WM_ACTIVATEAPP && wparam != 0)
        || (message == WM_ACTIVATE && (activateState == WA_ACTIVE || activateState == WA_CLICKACTIVE));

    if (lostFocus || gainedFocus) {
        ResetInputState();
        return false;
    }

    bool canceled = false;
    bool saved = false;
    std::vector<UINT> capturedKeys;
    if (capture.Active() && capture.OnWindowMessage(message, wparam, canceled, saved, capturedKeys)) {
        if (saved) {
            ApplyCapturedKeys(capturedKeys);
        } else if (canceled) {
            captureTarget = CaptureTarget::None;
            captureHotkeyIndex = -1;
            capturePopupPending = false;
        }
        return true;
    }

    const auto keyInfo = ::hotkeys::GetMessageKeyInfo(message, wparam);
    if (keyInfo && keyInfo->isDown && ActivatePendingTextConfirmations(keyInfo->keyCode)) {
        return true;
    }

    if (keyTracker.OnWindowMessage(message, wparam)) {
        pressedKeys = keyTracker.Ordered();
    }
    return false;
}

void BinderModule::Impl::ApplyCapturedKeys(const std::vector<UINT>& keys) {
    switch (captureTarget) {
    case CaptureTarget::BindHotkey:
        if (editor.active) {
            editor.draft.keys = ::hotkeys::NormalizeCombo(keys, editor.draft.hotkeyMode);
        }
        break;
    case CaptureTarget::QuickMenuHotkey:
        quickMenuHotkey = ::hotkeys::NormalizeCombo(keys, HotkeyMode::ModifierTrigger);
        SaveConfig();
        break;
    case CaptureTarget::ConfirmKey:
        if (editor.active) {
            editor.draft.textConfirmation.key = PickSingleCapturedKey(keys, kDefaultConfirmKey);
        }
        break;
    case CaptureTarget::CancelKey:
        if (editor.active) {
            editor.draft.textConfirmation.cancelKey = PickSingleCapturedKey(keys, kDefaultCancelKey);
        }
        break;
    case CaptureTarget::None:
        break;
    }

    captureTarget = CaptureTarget::None;
    captureHotkeyIndex = -1;
    capturePopupPending = false;
}

std::vector<UINT> BinderModule::Impl::CurrentQuickMenuHotkey() const {
    if (!quickMenuHotkey.empty()) {
        return ::hotkeys::NormalizeCombo(quickMenuHotkey, HotkeyMode::ModifierTrigger);
    }
    return { kDefaultQuickMenuFallback };
}

bool BinderModule::Impl::IsQuickMenuComboPressed() const {
    const auto normalizedPressed = ::hotkeys::NormalizeCombo(pressedKeys, HotkeyMode::ModifierTrigger);
    const auto normalizedCombo = ::hotkeys::NormalizeCombo(CurrentQuickMenuHotkey(), HotkeyMode::ModifierTrigger);
    if (normalizedCombo.empty() || normalizedPressed.size() < normalizedCombo.size()) {
        return false;
    }

    for (const UINT key : normalizedCombo) {
        if (std::find(normalizedPressed.begin(), normalizedPressed.end(), key) == normalizedPressed.end()) {
            return false;
        }
    }

    return true;
}

bool BinderModule::Impl::CaptureUsesEditorPopup() const {
    return capturePopupInEditor;
}

void BinderModule::Impl::BeginCapture(CaptureTarget target) {
    captureTarget = target;
    captureHotkeyIndex = editor.hotkeyIndex;
    capturePopupInEditor = editor.active
        && (target == CaptureTarget::BindHotkey || target == CaptureTarget::ConfirmKey || target == CaptureTarget::CancelKey);

    std::vector<UINT> initial;
    switch (target) {
    case CaptureTarget::BindHotkey:
        if (editor.active) {
            initial = editor.draft.keys;
        }
        break;
    case CaptureTarget::QuickMenuHotkey:
        initial = quickMenuHotkey;
        break;
    case CaptureTarget::ConfirmKey:
        if (editor.active && editor.draft.textConfirmation.key != 0) {
            initial = { editor.draft.textConfirmation.key };
        }
        break;
    case CaptureTarget::CancelKey:
        if (editor.active && editor.draft.textConfirmation.cancelKey != 0) {
            initial = { editor.draft.textConfirmation.cancelKey };
        }
        break;
    case CaptureTarget::None:
        break;
    }

    capture.Start(initial);
    capturePopupPending = true;
}

void BinderModule::Impl::UpdateQuickMenuState() {
    if (capture.Active()) {
        quickMenuOpen = false;
        return;
    }

    const bool hasEntries = VisibleQuickMenuEntriesExist();
    if (!hasEntries) {
        quickMenuOpen = false;
        return;
    }

    const bool comboHeld = IsQuickMenuComboPressed();
    if (quickMenuReopenBlocked) {
        quickMenuOpen = false;
        if (!comboHeld) {
            quickMenuReopenBlocked = false;
            quickMenuToggleLatch = false;
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
        quickMenuOpen = comboHeld;
    }

    if (!quickMenuOpen) {
        quickMenuTabIndex = 0;
    }
}

void BinderModule::Impl::ProcessHotkeys() {
    const double now = static_cast<double>(GetTickCount64());

    if (pressedKeys.empty()) {
        for (HotkeyEntry& hotkey : hotkeys) {
            hotkey.comboActive = false;
            hotkey.lastRepeatPressed.clear();
        }
        return;
    }

    if (quickMenuOpen || IsQuickMenuComboPressed() || inputDialog.has_value()) {
        return;
    }

    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        HotkeyEntry& hotkey = hotkeys[i];
        if (!hotkey.enabled || hotkey.keys.empty()) {
            hotkey.comboActive = false;
            hotkey.lastRepeatPressed.clear();
            continue;
        }

        const bool comboNow = ::hotkeys::ComboMatch(pressedKeys, hotkey.keys, hotkey.hotkeyMode);
        if (hotkey.repeatMode) {
            if (comboNow) {
                const int interval = std::max(hotkey.repeatIntervalMs, kMinMessageIntervalMs);
                if (hotkey.lastRepeatPressed.empty()
                    || !::hotkeys::ComboMatch(hotkey.lastRepeatPressed, hotkey.keys, hotkey.hotkeyMode)
                    || now >= hotkey.lastActivatedAtMs + interval) {
                    TryEnqueueHotkey(static_cast<int>(i), 0, "hotkey", "");
                    hotkey.lastActivatedAtMs = now;
                    hotkey.lastRepeatPressed = pressedKeys;
                }
            } else {
                hotkey.lastRepeatPressed.clear();
                hotkey.comboActive = false;
            }
            continue;
        }

        if (comboNow && !hotkey.comboActive) {
            if (now >= hotkey.debounceUntilMs) {
                TryEnqueueHotkey(static_cast<int>(i), 0, "hotkey", "");
                hotkey.debounceUntilMs = now + 40.0;
            }
            hotkey.comboActive = true;
        } else if (!comboNow) {
            hotkey.comboActive = false;
        }
    }
}

void BinderModule::Impl::ProcessRunningBinds() {
    const double now = static_cast<double>(GetTickCount64());
    for (std::size_t i = 0; i < runningBinds.size();) {
        RunningBind& running = runningBinds[i];
        if (running.hotkeyIndex < 0 || running.hotkeyIndex >= static_cast<int>(hotkeys.size())) {
            runningBinds.erase(runningBinds.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }

        HotkeyEntry& hotkey = hotkeys[running.hotkeyIndex];
        if (running.messageIndex >= hotkey.messages.size()) {
            runningBinds.erase(runningBinds.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }

        if (now < running.nextAtMs) {
            ++i;
            continue;
        }

        const HotkeyMessage& message = hotkey.messages[running.messageIndex];
        const std::string finalText = ApplyInputValues(message.text, running.inputValues);
        if (!Trim(finalText).empty()) {
            DoSend(finalText, message.method);
        }

        ++running.messageIndex;
        if (running.messageIndex >= hotkey.messages.size()) {
            runningBinds.erase(runningBinds.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }

        running.nextAtMs = now + std::max(message.intervalMs, kMinMessageIntervalMs);
        ++i;
    }
}

void BinderModule::Impl::PruneOutgoingGuards() {
    const double now = static_cast<double>(GetTickCount64());
    outgoingGuards.erase(
        std::remove_if(outgoingGuards.begin(), outgoingGuards.end(), [&](const OutgoingGuard& guard) {
            return guard.expiresAtMs <= now;
        }),
        outgoingGuards.end());
}

void BinderModule::Impl::RegisterOutgoingGuard(std::string kind, std::string text) {
    text = NormalizeActivationText(text);
    if (text.empty()) {
        return;
    }

    outgoingGuards.push_back(OutgoingGuard{
        std::move(kind),
        std::move(text),
        static_cast<double>(GetTickCount64() + kOutgoingGuardTimeoutMs),
    });
    while (outgoingGuards.size() > 64) {
        outgoingGuards.erase(outgoingGuards.begin());
    }
}

bool BinderModule::Impl::ConsumeOutgoingGuard(std::string_view kind, std::string_view text) {
    const std::string normalized = NormalizeActivationText(text);
    if (normalized.empty()) {
        return false;
    }

    for (auto it = outgoingGuards.begin(); it != outgoingGuards.end(); ++it) {
        if (it->kind == kind && it->text == normalized) {
            outgoingGuards.erase(it);
            return true;
        }
    }
    return false;
}

std::string BinderModule::Impl::NormalizeActivationText(std::string_view text) const {
    return Trim(NormalizeLineEndings(text));
}

bool BinderModule::Impl::MatchesActivationCommand(std::string_view input, std::string_view command) const {
    const std::string normalizedInput = NormalizeActivationText(input);
    const std::string normalizedCommand = NormalizeActivationText(command);
    if (normalizedInput.empty() || normalizedCommand.empty()) {
        return false;
    }

    return StartsWith(normalizedInput, normalizedCommand)
        && (normalizedInput.size() == normalizedCommand.size()
            || std::isspace(static_cast<unsigned char>(normalizedInput[normalizedCommand.size()])) != 0);
}

void BinderModule::Impl::OnOutgoingCommand(const std::string& text) {
    const std::string normalized = NormalizeActivationText(text);
    if (normalized.empty() || ConsumeOutgoingGuard("command", normalized)) {
        return;
    }

    OnTextTriggerEvent(normalized, "outgoing_command");
    const double now = static_cast<double>(GetTickCount64());
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        HotkeyEntry& hotkey = hotkeys[i];
        if (!hotkey.commandEnabled || hotkey.command.empty() || hotkey.awaitingInput) {
            continue;
        }
        if (!MatchesActivationCommand(normalized, hotkey.command)) {
            continue;
        }
        if (now < hotkey.debounceUntilMs) {
            continue;
        }
        hotkey.debounceUntilMs = now + 40.0;
        TryEnqueueHotkey(static_cast<int>(i), 0, "command", normalized);
    }
}

void BinderModule::Impl::OnOutgoingChat(const std::string& text) {
    const std::string normalized = NormalizeActivationText(text);
    if (normalized.empty() || ConsumeOutgoingGuard("chat", normalized)) {
        return;
    }
    OnTextTriggerEvent(normalized, "outgoing_chat");
}

void BinderModule::Impl::OnIncomingTextMessage(const std::string& text, std::string_view source) {
    const std::string normalized = NormalizeTriggerText(text);
    if (normalized.empty() || ConsumeOutgoingGuard("echo", normalized)) {
        return;
    }
    OnTextTriggerEvent(normalized, source);
}

void BinderModule::Impl::ExpireTextConfirmations() {
    const double now = static_cast<double>(GetTickCount64());
    for (HotkeyEntry& hotkey : hotkeys) {
        if (!hotkey.waitingTextConfirmation || hotkey.textConfirmationDeadlineMs <= 0.0) {
            continue;
        }
        if (now >= hotkey.textConfirmationDeadlineMs) {
            hotkey.waitingTextConfirmation = false;
            hotkey.textConfirmationDeadlineMs = 0.0;
            hotkey.pendingTriggerText.clear();
            hotkey.pendingTriggerSource.clear();
            PushToast(
                UiSettings::Instance().Format(UiText::ToastBindConfirmExpired, hotkey.label.c_str()),
                ImVec4(0.55f, 0.30f, 0.10f, 0.95f),
                2500.0);
        }
    }
}

bool BinderModule::Impl::ActivatePendingTextConfirmations(UINT keyCode) {
    bool handled = false;
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        HotkeyEntry& hotkey = hotkeys[i];
        if (!hotkey.waitingTextConfirmation) {
            continue;
        }
        if (!hotkey.enabled) {
            hotkey.waitingTextConfirmation = false;
            hotkey.textConfirmationDeadlineMs = 0.0;
            hotkey.pendingTriggerText.clear();
            hotkey.pendingTriggerSource.clear();
            continue;
        }

        if (keyCode == hotkey.textConfirmation.key) {
            const std::string pendingText = hotkey.pendingTriggerText;
            const std::string pendingSource = hotkey.pendingTriggerSource;
            hotkey.waitingTextConfirmation = false;
            hotkey.textConfirmationDeadlineMs = 0.0;
            hotkey.pendingTriggerText.clear();
            hotkey.pendingTriggerSource.clear();
            TryEnqueueHotkey(static_cast<int>(i), 0, pendingSource, pendingText);
            handled = true;
        } else if (keyCode == hotkey.textConfirmation.cancelKey) {
            hotkey.waitingTextConfirmation = false;
            hotkey.textConfirmationDeadlineMs = 0.0;
            hotkey.pendingTriggerText.clear();
            hotkey.pendingTriggerSource.clear();
            PushToast(
                UiSettings::Instance().Format(UiText::ToastBindCanceled, hotkey.label.c_str()),
                ImVec4(0.55f, 0.30f, 0.10f, 0.95f),
                2200.0);
            handled = true;
        }
    }
    return handled;
}

bool BinderModule::Impl::MatchTextTrigger(const std::string& source, const HotkeyEntry& hotkey) {
    const TextTrigger& trigger = hotkey.textTrigger;
    if (!trigger.enabled || Trim(trigger.text).empty()) {
        return false;
    }

    const std::string normalizedSource = NormalizeTriggerText(source);
    const std::string normalizedTarget = NormalizeTriggerText(trigger.text);
    if (normalizedTarget.empty()) {
        return false;
    }

    if (!trigger.pattern) {
        return normalizedSource == normalizedTarget;
    }

    try {
        if (std::regex_search(normalizedSource, std::regex(trigger.text))) {
            return true;
        }
    } catch (const std::exception&) {
        return normalizedSource == normalizedTarget;
    }
    return normalizedSource == normalizedTarget;
}

void BinderModule::Impl::OnTextTriggerEvent(const std::string& sourceText, std::string_view sourceKind) {
    const double now = static_cast<double>(GetTickCount64());
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        HotkeyEntry& hotkey = hotkeys[i];
        if (!hotkey.enabled || !MatchTextTrigger(sourceText, hotkey)) {
            continue;
        }
        if (now < hotkey.debounceUntilMs) {
            continue;
        }

        hotkey.debounceUntilMs = now + 40.0;
        if (hotkey.textConfirmation.enabled && !hotkey.waitingTextConfirmation && !hotkey.awaitingInput
            && !ConditionsBlock(hotkey.conditions, sampApi)) {
            hotkey.waitingTextConfirmation = true;
            hotkey.pendingTriggerText = sourceText;
            hotkey.pendingTriggerSource = std::string(sourceKind);
            hotkey.textConfirmationDeadlineMs =
                hotkey.textConfirmation.waitForResolution ? 0.0 : now + kTextConfirmTimeoutMs;

            const std::string confirmText = UiSettings::Instance().Format(
                UiText::ToastConfirmPrompt,
                hotkey.label.c_str(),
                ::hotkeys::KeyName(hotkey.textConfirmation.key).c_str(),
                ::hotkeys::KeyName(hotkey.textConfirmation.cancelKey).c_str());
            PushToast(confirmText, ImVec4(0.55f, 0.30f, 0.10f, 0.95f),
                hotkey.textConfirmation.waitForResolution ? 4000.0 : 2500.0);
            continue;
        }

        TryEnqueueHotkey(static_cast<int>(i), 0, sourceKind, sourceText);
    }
}

std::string BinderModule::Impl::ApplyInputValues(std::string text, const std::map<std::string, std::string>& values) const {
    static const std::regex kPlaceholder("\\{\\{([A-Za-z0-9_]+)\\}\\}");
    std::string result;
    std::sregex_iterator it(text.begin(), text.end(), kPlaceholder);
    std::sregex_iterator end;
    std::size_t lastPos = 0;
    for (; it != end; ++it) {
        const auto& match = *it;
        result.append(text, lastPos, static_cast<std::size_t>(match.position()) - lastPos);
        const std::string key = match[1].str();
        auto valueIt = values.find(key);
        if (valueIt == values.end()) {
            valueIt = values.find(ToLower(key));
        }
        if (valueIt == values.end()) {
            valueIt = values.find(NormalizeInputKey(key));
        }
        if (valueIt != values.end()) {
            result += valueIt->second;
        }
        lastPos = static_cast<std::size_t>(match.position() + match.length());
    }
    result.append(text, lastPos, std::string::npos);
    return result;
}

std::string BinderModule::Impl::BuildInputValue(const InputDialogField& field) const {
    const auto buttonText = [&](int index) -> std::string {
        if (index < 0 || index >= static_cast<int>(field.input.buttons.size())) {
            return {};
        }
        const InputButton& button = field.input.buttons[static_cast<std::size_t>(index)];
        return !button.text.empty() ? button.text : button.label;
    };

    if (field.input.mode == InputMode::Text) {
        return field.textValue;
    }

    if (field.input.multiSelect) {
        std::ostringstream stream;
        bool first = true;
        for (const int selected : field.selectedButtons) {
            const std::string textValue = buttonText(selected);
            if (textValue.empty()) {
                continue;
            }
            if (!first) {
                stream << (field.input.multiSeparator.empty() ? ", " : field.input.multiSeparator);
            }
            stream << textValue;
            first = false;
        }
        if (!field.textValue.empty() && field.input.mode == InputMode::ButtonsListText) {
            return field.textValue;
        }
        return stream.str();
    }

    if (field.selectedButtonIndex.has_value()) {
        const std::string textValue = buttonText(*field.selectedButtonIndex);
        if (!textValue.empty()) {
            return field.input.mode == InputMode::ButtonsListText && !field.textValue.empty() ? field.textValue : textValue;
        }
    }

    return field.textValue;
}

std::vector<int> BinderModule::Impl::FilterButtons(const InputDialogState& dialog, std::size_t fieldIndex) const {
    std::vector<int> result;
    if (fieldIndex >= dialog.fields.size()) {
        return result;
    }

    const HotkeyInput& input = dialog.fields[fieldIndex].input;
    const std::string parentKey = NormalizeInputKey(input.cascadeParentKey);
    int parentIndex = -1;
    if (!parentKey.empty()) {
        for (std::size_t i = 0; i < dialog.fields.size(); ++i) {
            if (i == fieldIndex) {
                continue;
            }
            if (NormalizeInputKey(dialog.fields[i].input.key) == parentKey) {
                parentIndex = static_cast<int>(i);
                break;
            }
        }
    }

    std::set<std::string> selectedTokens;
    if (parentIndex >= 0) {
        const InputDialogField& parent = dialog.fields[static_cast<std::size_t>(parentIndex)];
        const auto addToken = [&](std::string token) {
            token = ToLower(Trim(token));
            if (!token.empty()) {
                selectedTokens.insert(std::move(token));
            }
        };

        if (parent.selectedButtonIndex.has_value()) {
            const int idx = *parent.selectedButtonIndex;
            if (idx >= 0 && idx < static_cast<int>(parent.input.buttons.size())) {
                addToken(parent.input.buttons[static_cast<std::size_t>(idx)].label);
                addToken(parent.input.buttons[static_cast<std::size_t>(idx)].text);
            }
        }
        for (const int idx : parent.selectedButtons) {
            if (idx >= 0 && idx < static_cast<int>(parent.input.buttons.size())) {
                addToken(parent.input.buttons[static_cast<std::size_t>(idx)].label);
                addToken(parent.input.buttons[static_cast<std::size_t>(idx)].text);
            }
        }
    }

    for (std::size_t i = 0; i < input.buttons.size(); ++i) {
        const InputButton& button = input.buttons[i];
        if (parentIndex >= 0 && !Trim(button.when).empty()) {
            bool matches = false;
            for (const std::string& token : Split(button.when, '|')) {
                if (selectedTokens.contains(ToLower(Trim(token)))) {
                    matches = true;
                    break;
                }
            }
            if (!matches) {
                continue;
            }
        }
        result.push_back(static_cast<int>(i));
    }
    return result;
}

bool BinderModule::Impl::TryEnqueueHotkey(
    HotkeyEntry& hotkey,
    int startDelayMs,
    std::string_view source,
    const std::string& sourceText) {
    const auto it = std::find_if(hotkeys.begin(), hotkeys.end(), [&](const HotkeyEntry& item) { return &item == &hotkey; });
    if (it == hotkeys.end()) {
        return false;
    }
    return TryEnqueueHotkey(static_cast<int>(std::distance(hotkeys.begin(), it)), startDelayMs, source, sourceText);
}

bool BinderModule::Impl::TryEnqueueHotkey(
    int index,
    int startDelayMs,
    std::string_view source,
    const std::string& sourceText) {
    if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
        return false;
    }

    HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
    hotkey.pendingTriggerSource = std::string(source);
    hotkey.pendingTriggerText = sourceText;

    if (!hotkey.enabled || hotkey.awaitingInput || hotkey.waitingTextConfirmation) {
        return false;
    }

    std::string conditionMessage;
    if (ConditionsBlock(hotkey.conditions, sampApi, &conditionMessage)) {
        if (!conditionMessage.empty() && source != "incoming_server" && source != "outgoing_chat" && source != "outgoing_command") {
            PushToast(
                UiSettings::Instance().Format(UiText::ToastConditionBlocked, conditionMessage.c_str()),
                ImVec4(0.55f, 0.30f, 0.10f, 0.95f),
                2200.0);
        }
        return false;
    }

    if (!hotkey.inputs.empty()) {
        if (inputDialog.has_value() && inputDialog->hotkeyIndex != index) {
            PushToast(UiSettings::Instance().Text(UiText::ToastFinishActiveInput), ImVec4(0.55f, 0.30f, 0.10f, 0.95f), 2500.0);
            return false;
        }

        InputDialogState dialog;
        dialog.hotkeyIndex = index;
        dialog.startDelayMs = startDelayMs;
        dialog.fields.reserve(hotkey.inputs.size());
        for (const HotkeyInput& input : hotkey.inputs) {
            InputDialogField field;
            field.input = input;
            dialog.fields.push_back(std::move(field));
        }
        inputDialog = std::move(dialog);
        hotkey.awaitingInput = true;
        return true;
    }

    if (hotkey.messages.empty()) {
        return false;
    }

    runningBinds.push_back(RunningBind{
        index,
        {},
        0,
        static_cast<double>(GetTickCount64() + std::max(startDelayMs, 0)),
    });
    hotkey.awaitingInput = false;
    return true;
}

void BinderModule::Impl::DoSend(const std::string& text, int method) {
    if (method == 3) {
        return;
    }

    switch (method) {
    case 0: {
        const auto [messageText, color] = ParseLeadingChatColor(text);
        if (!sampApi || !sampApi->memoryAddMessageSamp(messageText, color, true)) {
            PushToast(UiSettings::Instance().Text(UiText::ToastSendLocalFailed), ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2500.0);
        }
        break;
    }
    case 1:
    case 2:
        RegisterOutgoingGuard(!text.empty() && text.front() == '/' ? "command" : "chat", text);
        RegisterOutgoingGuard("echo", NormalizeTriggerText(text));
        if (!sampApi || !sampApi->send_chat(text, true)) {
            PushToast(UiSettings::Instance().Text(UiText::ToastSendSampFailed), ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2500.0);
        }
        break;
    case 4:
        if (!sampApi || !sampApi->Set_ChatInputText(text, true, true)) {
            PushToast(UiSettings::Instance().Text(UiText::ToastInsertChatFailed), ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2500.0);
        } else {
            sampApi->pCInput_Open_Close(false);
        }
        break;
    case 5:
        if (!sampApi || !sampApi->Set_ChatInputText(text, true, true)) {
            PushToast(UiSettings::Instance().Text(UiText::ToastOpenChatFailed), ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2500.0);
        }
        break;
    case 6:
        if (!sampApi || !sampApi->sampSetDialogEditboxText(text, true)) {
            PushToast(UiSettings::Instance().Text(UiText::ToastInsertDialogFailed), ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2500.0);
        }
        break;
    case 7:
        if (!SetClipboardUtf8Text(text)) {
            PushToast(UiSettings::Instance().Text(UiText::ToastClipboardFailed), ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2500.0);
        }
        break;
    case 8:
        debuglog::Write("Binder log: %s", text.c_str());
        break;
    case 9:
        PushToast(text, ImVec4(0.20f, 0.35f, 0.18f, 0.95f), 2200.0);
        break;
    default:
        PushToast(
            UiSettings::Instance().Format(UiText::ToastUnknownSendMethod, method),
            ImVec4(0.55f, 0.20f, 0.20f, 0.95f),
            2500.0);
        break;
    }
}

int BinderModule::Impl::RemapHotkeysFolderPrefix(
    const std::vector<std::string>& oldPath,
    const std::vector<std::string>& newPath) {
    int changed = 0;
    for (HotkeyEntry& hotkey : hotkeys) {
        if (!PathStartsWith(hotkey.folderPath, oldPath)) {
            continue;
        }
        hotkey.folderPath = ReplacePathPrefix(hotkey.folderPath, oldPath, newPath);
        ++changed;
    }
    return changed;
}

int BinderModule::Impl::MoveHotkeysFromFolderPath(
    const std::vector<std::string>& fromPath,
    const std::vector<std::string>& toPath) {
    int changed = 0;
    for (HotkeyEntry& hotkey : hotkeys) {
        if (!PathStartsWith(hotkey.folderPath, fromPath)) {
            continue;
        }
        hotkey.folderPath = toPath;
        ++changed;
    }
    return changed;
}

std::vector<int> BinderModule::Impl::FilteredBindIndices() const {
    std::vector<int> indices;
    if (!selectedFolder) {
        return indices;
    }

    const std::string query = ToLower(Trim(bindSearch));
    const auto folderPath = BuildFolderPath(selectedFolder);
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        const HotkeyEntry& hotkey = hotkeys[i];
        if (hotkey.folderPath != folderPath) {
            continue;
        }
        if (!query.empty()) {
            const std::string hay = ToLower(hotkey.label + " " + hotkey.command + " " + std::to_string(hotkey.number));
            if (hay.find(query) == std::string::npos) {
                continue;
            }
        }
        indices.push_back(static_cast<int>(i));
    }
    return indices;
}

void BinderModule::Impl::StartEditing(int index, bool isNew) {
    editor = {};
    editor.active = true;
    editor.isNew = isNew;
    editor.hotkeyIndex = index;
    editor.selectedInputIndex = -1;
    editor.draft = (isNew || index < 0 || index >= static_cast<int>(hotkeys.size())) ? MakeDefaultHotkey() : hotkeys[index];
    editor.draft.comboActive = false;
    editor.draft.awaitingInput = false;
    editor.draft.waitingTextConfirmation = false;
    editor.draft.lastRepeatPressed.clear();
    editor.draft.pendingTriggerText.clear();
    editor.draft.pendingTriggerSource.clear();
    editor.draft.lastActivatedAtMs = 0.0;
    editor.draft.debounceUntilMs = 0.0;

    if (editor.draft.folderPath.empty()) {
        editor.draft.folderPath = BuildFolderPath(selectedFolder ? selectedFolder : EnsureRootFolder());
    }

    if (!editor.draft.inputs.empty()) {
        editor.selectedInputIndex = 0;
        editor.selectedButtonsText = SerializeButtonsText(editor.draft.inputs[0].buttons);
    }

    editorPopupPending = true;
}

bool BinderModule::Impl::ValidateEditor(std::vector<std::string>& errors) {
    const std::string label = Trim(editor.draft.label);
    if (label.empty()) {
        errors.push_back(UiSettings::Instance().Text(UiText::ValidationBindNameRequired));
    }

    if (editor.draft.folderPath.empty() || !FindFolderByPath(folders, editor.draft.folderPath)) {
        errors.push_back(UiSettings::Instance().Text(UiText::ValidationExistingFolderRequired));
    }

    if (editor.draft.repeatMode && editor.draft.repeatIntervalMs < kMinMessageIntervalMs) {
        errors.push_back(UiSettings::Instance().Text(UiText::ValidationRepeatInterval));
    }

    std::set<std::string> inputKeys;
    for (const HotkeyInput& input : editor.draft.inputs) {
        const std::string key = NormalizeInputKey(input.key);
        if (key.empty()) {
            errors.push_back(UiSettings::Instance().Text(UiText::ValidationInputKeyRequired));
            continue;
        }
        if (!inputKeys.insert(key).second) {
            errors.push_back(UiSettings::Instance().Text(UiText::ValidationInputKeyUnique));
        }
        if (InputModeUsesButtons(input.mode) && input.buttons.empty()) {
            errors.push_back(UiSettings::Instance().Text(UiText::ValidationButtonsRequired));
        }
    }

    if (editor.draft.textTrigger.enabled && editor.draft.textTrigger.pattern) {
        try {
            std::regex test(editor.draft.textTrigger.text);
            (void)test;
        } catch (const std::exception& ex) {
            errors.push_back(UiSettings::Instance().Format(UiText::ValidationInvalidRegex, ex.what()));
        }
    }

    return errors.empty();
}

void BinderModule::Impl::SaveEditor() {
    HotkeyEntry saved = editor.draft;
    saved.label = Trim(saved.label);
    saved.keys = ::hotkeys::NormalizeCombo(saved.keys, saved.hotkeyMode);
    saved.command = Trim(saved.command);
    saved.textTrigger.text = Trim(saved.textTrigger.text);
    saved.conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    saved.quickConditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    saved.comboActive = false;
    saved.awaitingInput = false;
    saved.waitingTextConfirmation = false;
    saved.lastRepeatPressed.clear();
    saved.pendingTriggerText.clear();
    saved.pendingTriggerSource.clear();
    saved.lastActivatedAtMs = 0.0;
    saved.debounceUntilMs = 0.0;

    if (editor.isNew || editor.hotkeyIndex < 0 || editor.hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        hotkeys.push_back(std::move(saved));
    } else {
        hotkeys[editor.hotkeyIndex] = std::move(saved);
    }

    RefreshNumbers();
    SaveConfig();
    editor.active = false;
    editorPopupPending = false;
}

void BinderModule::Impl::DuplicateHotkeyAt(int index) {
    if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
        return;
    }

    HotkeyEntry duplicated = hotkeys[static_cast<std::size_t>(index)];
    duplicated.comboActive = false;
    duplicated.awaitingInput = false;
    duplicated.waitingTextConfirmation = false;
    duplicated.lastRepeatPressed.clear();
    duplicated.pendingTriggerText.clear();
    duplicated.pendingTriggerSource.clear();
    duplicated.lastActivatedAtMs = 0.0;
    duplicated.debounceUntilMs = 0.0;
    duplicated.textConfirmationDeadlineMs = 0.0;

    hotkeys.insert(hotkeys.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(duplicated));
    RefreshNumbers();
    selectedBindIndex = index + 1;
    SaveConfig();
}

void BinderModule::Impl::DrawFolderTreeNode(FolderNode& folder) {
    if (!FolderMatchesSearch(folder, folderSearch)) {
        return;
    }

    ImGui::PushID(folder.id);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (&folder == selectedFolder) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (folder.children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    ImGui::SetNextItemOpen(folder.open, ImGuiCond_Always);
    const bool opened = ImGui::TreeNodeEx("##folder_node", flags, "%s", folder.name.c_str());
    folder.open = opened;
    if (ImGui::IsItemClicked()) {
        selectedFolder = &folder;
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BINDER_HOTKEY_INDEX")) {
            if (payload->Data != nullptr && payload->DataSize == sizeof(int) && payload->IsDelivery()) {
                const int hotkeyIndex = *static_cast<const int*>(payload->Data);
                if (hotkeyIndex >= 0 && hotkeyIndex < static_cast<int>(hotkeys.size())) {
                    hotkeys[static_cast<std::size_t>(hotkeyIndex)].folderPath = BuildFolderPath(&folder);
                    selectedFolder = &folder;
                    ExpandFolderBranch(&folder);
                    SaveConfig();
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem("##folder_context")) {
        UiSettings& ui = UiSettings::Instance();
        if (ImGui::MenuItem(ui.Text(UiText::FolderAdd))) {
            folderPopup = {};
            folderPopup.parent = &folder;
            folderPopup.name = ui.Text(UiText::BinderNewFolder);
            ExpandFolderBranch(&folder);
            ImGui::OpenPopup("##binder_folder_edit");
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::MenuItem(ui.Text(UiText::FolderRename))) {
            folderPopup = {};
            folderPopup.target = &folder;
            folderPopup.name = folder.name;
            ImGui::OpenPopup("##binder_folder_edit");
            ImGui::CloseCurrentPopup();
        }

        const bool canDelete = folder.parent != nullptr;
        if (!canDelete) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem(ui.Text(UiText::Delete)) && canDelete) {
            folderDeleteTarget = &folder;
            ImGui::OpenPopup("##binder_folder_delete");
            ImGui::CloseCurrentPopup();
        }
        if (!canDelete) {
            ImGui::EndDisabled();
        }

        ImGui::EndPopup();
    }

    if (opened && !folder.children.empty()) {
        for (auto& child : folder.children) {
            if (child) {
                DrawFolderTreeNode(*child);
            }
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void BinderModule::Impl::DrawFolderPane() {
    EnsureRootFolder();
    if (ImGui::Button((std::string(UiSettings::Instance().Text(UiText::FolderAdd)) + "##folder_add").c_str())) {
        folderPopup = {};
        folderPopup.parent = selectedFolder ? selectedFolder : EnsureRootFolder();
        folderPopup.name = UiSettings::Instance().Text(UiText::BinderNewFolder);
        if (folderPopup.parent) {
            ExpandFolderBranch(folderPopup.parent);
        }
        ImGui::OpenPopup("##binder_folder_edit");
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string(UiSettings::Instance().Text(UiText::FolderRename)) + "##folder_rename").c_str()) && selectedFolder) {
        folderPopup = {};
        folderPopup.target = selectedFolder;
        folderPopup.name = selectedFolder->name;
        ImGui::OpenPopup("##binder_folder_edit");
    }
    ImGui::SameLine();
    const bool canDeleteSelected = selectedFolder && selectedFolder->parent;
    if (!canDeleteSelected) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button((std::string(UiSettings::Instance().Text(UiText::Delete)) + "##folder_delete").c_str())
        && canDeleteSelected) {
        folderDeleteTarget = selectedFolder;
        ImGui::OpenPopup("##binder_folder_delete");
    }
    if (!canDeleteSelected) {
        ImGui::EndDisabled();
    }

    InputTextString(UiSettings::Instance().Text(UiText::SearchFolders), folderSearch, ImGuiInputTextFlags_AutoSelectAll, 128);
    ImGui::Separator();

    if (ImGui::BeginChild("##binder_folders_tree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        for (auto& folder : folders) {
            if (folder) {
                DrawFolderTreeNode(*folder);
            }
        }
    }
    ImGui::EndChild();
}

void BinderModule::Impl::DrawFolderPopups() {
    if (ImGui::BeginPopupModal("##binder_folder_edit", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(UiSettings::Instance().Text(folderPopup.target ? UiText::FolderRename : UiText::FolderAdd));
        ImGui::Separator();
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        InputTextString(UiSettings::Instance().Text(UiText::Name), folderPopup.name, ImGuiInputTextFlags_AutoSelectAll, 128);
        if (ImGui::Button(UiSettings::Instance().Text(UiText::Save))) {
            const std::string name = SanitizeFolderName(folderPopup.name);
            if (!name.empty()) {
                bool applied = false;
                if (folderPopup.target) {
                    auto& siblings = folderPopup.target->parent ? folderPopup.target->parent->children : folders;
                    if (FolderNameUnique(siblings, name, folderPopup.target)) {
                        if (folderPopup.target->name != name) {
                            const auto oldPath = BuildFolderPath(folderPopup.target);
                            folderPopup.target->name = name;
                            const auto newPath = BuildFolderPath(folderPopup.target);
                            RemapHotkeysFolderPrefix(oldPath, newPath);
                        }
                        applied = true;
                    }
                } else {
                    auto& siblings = folderPopup.parent ? folderPopup.parent->children : folders;
                    if (FolderNameUnique(siblings, name)) {
                        auto folder = std::make_unique<FolderNode>();
                        folder->id = nextFolderId++;
                        folder->name = name;
                        folder->quickConditions.assign(static_cast<std::size_t>(ConditionId::Count), false);
                        folder->parent = folderPopup.parent;
                        FolderNode* created = folder.get();
                        if (folderPopup.parent) {
                            ExpandFolderBranch(folderPopup.parent);
                            folderPopup.parent->children.push_back(std::move(folder));
                        } else {
                            folders.push_back(std::move(folder));
                        }
                        selectedFolder = created;
                        ExpandFolderBranch(selectedFolder);
                        applied = true;
                    }
                }
                if (applied) {
                    SaveConfig();
                    folderPopup = {};
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(UiSettings::Instance().Text(UiText::Cancel))) {
            folderPopup = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("##binder_folder_delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", UiSettings::Instance().Text(UiText::DeleteFolderMoveBindsQuestion));
        if (folderDeleteTarget) {
            ImGui::TextDisabled("%s", folderDeleteTarget->name.c_str());
        }
        if (ImGui::Button(UiSettings::Instance().Text(UiText::Delete))) {
            if (folderDeleteTarget && folderDeleteTarget->parent) {
                FolderNode* parent = folderDeleteTarget->parent;
                const auto removedPath = BuildFolderPath(folderDeleteTarget);
                const auto fallbackPath = BuildFolderPath(parent);
                MoveHotkeysFromFolderPath(removedPath, fallbackPath);

                auto& siblings = parent->children;
                siblings.erase(
                    std::remove_if(siblings.begin(), siblings.end(), [&](const std::unique_ptr<FolderNode>& item) {
                        return item.get() == folderDeleteTarget;
                    }),
                    siblings.end());
                selectedFolder = parent;
                folderDeleteTarget = nullptr;
                SaveConfig();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(UiSettings::Instance().Text(UiText::Cancel))) {
            folderDeleteTarget = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void BinderModule::Impl::DrawInputEditor() {
    if (ImGui::Button(UiSettings::Instance().Text(UiText::AddField))) {
        HotkeyInput input;
        input.key = "FIELD_" + std::to_string(editor.draft.inputs.size() + 1);
        input.label = UiSettings::Instance().Format(UiText::FieldLabelFormat, static_cast<int>(editor.draft.inputs.size() + 1));
        editor.draft.inputs.push_back(std::move(input));
        editor.selectedInputIndex = static_cast<int>(editor.draft.inputs.size() - 1);
        editor.selectedButtonsText.clear();
    }

    ImGui::Separator();
    if (ImGui::BeginTable("##binder_inputs_table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(UiSettings::Instance().Text(UiText::ColumnName));
        ImGui::TableSetupColumn(UiSettings::Instance().Text(UiText::Key));
        ImGui::TableSetupColumn(UiSettings::Instance().Text(UiText::Mode));
        ImGui::TableSetupColumn(UiSettings::Instance().Text(UiText::ColumnSpacer), ImGuiTableColumnFlags_WidthFixed, ScaleUi(32.0f));
        ImGui::TableHeadersRow();

        int removeIndex = -1;
        for (std::size_t i = 0; i < editor.draft.inputs.size(); ++i) {
            HotkeyInput& input = editor.draft.inputs[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(
                    (input.label.empty() ? UiSettings::Instance().Text(UiText::UnnamedField) : input.label.c_str()),
                    editor.selectedInputIndex == static_cast<int>(i),
                    ImGuiSelectableFlags_AllowDoubleClick,
                    ImVec2(-FLT_MIN, 0.0f))) {
                editor.selectedInputIndex = static_cast<int>(i);
                editor.selectedButtonsText = SerializeButtonsText(input.buttons);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(input.key.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(InputModeLabel(input.mode));
            ImGui::TableSetColumnIndex(3);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton(UiSettings::Instance().Text(UiText::ActionRemoveShort))) {
                removeIndex = static_cast<int>(i);
            }
            ImGui::PopID();
        }

        if (removeIndex >= 0) {
            editor.draft.inputs.erase(editor.draft.inputs.begin() + removeIndex);
            if (editor.selectedInputIndex == removeIndex) {
                editor.selectedInputIndex = editor.draft.inputs.empty() ? -1 : 0;
            } else if (editor.selectedInputIndex > removeIndex) {
                --editor.selectedInputIndex;
            }
            if (editor.selectedInputIndex >= 0 && editor.selectedInputIndex < static_cast<int>(editor.draft.inputs.size())) {
                editor.selectedButtonsText = SerializeButtonsText(editor.draft.inputs[editor.selectedInputIndex].buttons);
            } else {
                editor.selectedButtonsText.clear();
            }
        }

        ImGui::EndTable();
    }

    if (editor.selectedInputIndex >= 0 && editor.selectedInputIndex < static_cast<int>(editor.draft.inputs.size())) {
        HotkeyInput& input = editor.draft.inputs[editor.selectedInputIndex];
        ImGui::SeparatorText(UiSettings::Instance().Text(UiText::FieldProperties));
        InputTextString(UiSettings::Instance().Text(UiText::Key), input.key, ImGuiInputTextFlags_AutoSelectAll, 64);
        input.key = NormalizeInputKey(input.key);
        InputTextString(UiSettings::Instance().Text(UiText::Name), input.label, ImGuiInputTextFlags_AutoSelectAll, 128);
        InputTextString(UiSettings::Instance().Text(UiText::Hint), input.hint, ImGuiInputTextFlags_AutoSelectAll, 256);

        const InputMode modes[] = { InputMode::Text, InputMode::ButtonsList, InputMode::ButtonsListText };
        const char* modeLabels[] = {
            InputModeLabel(InputMode::Text),
            InputModeLabel(InputMode::ButtonsList),
            InputModeLabel(InputMode::ButtonsListText),
        };
        int modeIndex = 0;
        for (int i = 0; i < 3; ++i) {
            if (input.mode == modes[i]) {
                modeIndex = i;
                break;
            }
        }
        if (ImGui::Combo(UiSettings::Instance().Text(UiText::Mode), &modeIndex, modeLabels, IM_ARRAYSIZE(modeLabels))) {
            input.mode = modes[modeIndex];
        }
        ImGui::Checkbox(UiSettings::Instance().Text(UiText::MultiSelect), &input.multiSelect);
        InputTextString(UiSettings::Instance().Text(UiText::Separator), input.multiSeparator, ImGuiInputTextFlags_AutoSelectAll, 64);
        InputTextString(UiSettings::Instance().Text(UiText::CascadeFromKey), input.cascadeParentKey, ImGuiInputTextFlags_AutoSelectAll, 64);
        input.cascadeParentKey = NormalizeInputKey(input.cascadeParentKey);

        if (InputModeUsesButtons(input.mode)) {
            ImGui::SeparatorText(UiSettings::Instance().Text(UiText::Buttons));
            ImGui::TextDisabled("%s", UiSettings::Instance().Text(UiText::ButtonsFormatHint));
            InputTextMultilineString("##input_buttons_src", editor.selectedButtonsText, ImVec2(-FLT_MIN, ScaleUi(180.0f)));
            input.buttons = ParseButtonsText(editor.selectedButtonsText);
        }
    }
}

void BinderModule::Impl::DrawEditor() {
    if (editorPopupPending) {
        ImGui::OpenPopup("##binder_editor_popup");
        editorPopupPending = false;
    }

    ImGui::SetNextWindowSize(ScaleUi(900.0f, 700.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal("##binder_editor_popup", nullptr, ImGuiWindowFlags_NoScrollbar)) {
        if (editor.active && !ImGui::IsPopupOpen("##binder_editor_popup")) {
            editor.active = false;
        }
        return;
    }

    EnsureRootFolder();
    ImGui::TextUnformatted(UiSettings::Instance().Text(editor.isNew ? UiText::NewBindTitle : UiText::EditBindTitle));
    ImGui::Separator();

    InputTextString(UiSettings::Instance().Text(UiText::Name), editor.draft.label, ImGuiInputTextFlags_AutoSelectAll, 128);
    ImGui::Checkbox(UiSettings::Instance().Text(UiText::Enabled), &editor.draft.enabled);

    std::vector<std::vector<std::string>> folderPaths;
    CollectFolderPaths(folders, folderPaths);
    std::string currentFolder = JoinPath(editor.draft.folderPath);
    if (ImGui::BeginCombo(UiSettings::Instance().Text(UiText::Folder),
            currentFolder.empty() ? UiSettings::Instance().Text(UiText::SelectFolder) : currentFolder.c_str())) {
        for (const auto& path : folderPaths) {
            const std::string label = JoinPath(path);
            const bool selected = path == editor.draft.folderPath;
            if (ImGui::Selectable(label.c_str(), selected)) {
                editor.draft.folderPath = path;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    const HotkeyMode hotkeyModes[] = { HotkeyMode::ModifierTrigger, HotkeyMode::OrderedCombo };
    const char* hotkeyModeLabels[] = {
        HotkeyModeLabel(HotkeyMode::ModifierTrigger),
        HotkeyModeLabel(HotkeyMode::OrderedCombo),
    };
    int hotkeyModeIndex = editor.draft.hotkeyMode == HotkeyMode::OrderedCombo ? 1 : 0;
    if (ImGui::Combo(UiSettings::Instance().Text(UiText::HotkeyMode), &hotkeyModeIndex, hotkeyModeLabels, IM_ARRAYSIZE(hotkeyModeLabels))) {
        editor.draft.hotkeyMode = hotkeyModes[hotkeyModeIndex];
        editor.draft.keys = ::hotkeys::NormalizeCombo(editor.draft.keys, editor.draft.hotkeyMode);
    }
    ImGui::Text("%s", UiSettings::Instance().Format(
        UiText::HotkeyFormat,
        ::hotkeys::ToString(editor.draft.keys, editor.draft.hotkeyMode).c_str()).c_str());
    ImGui::SameLine();
    if (ImGui::Button(UiSettings::Instance().Text(UiText::ChangeHotkey))) {
        BeginCapture(CaptureTarget::BindHotkey);
    }

    ImGui::Checkbox(UiSettings::Instance().Text(UiText::ShowInQuickMenu), &editor.draft.quickMenu);
    ImGui::SameLine();
    ImGui::Checkbox(UiSettings::Instance().Text(UiText::Repeat), &editor.draft.repeatMode);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ScaleUi(120.0f));
    ImGui::InputInt(UiSettings::Instance().Text(UiText::RepeatInterval), &editor.draft.repeatIntervalMs);
    if (editor.draft.repeatIntervalMs < kMinMessageIntervalMs) {
        editor.draft.repeatIntervalMs = kMinMessageIntervalMs;
    }

    if (ImGui::BeginTabBar("##binder_edit_tabs")) {
        if (ImGui::BeginTabItem(UiSettings::Instance().Text(UiText::MessagesTab))) {
            if (ImGui::Button(UiSettings::Instance().Text(UiText::AddRow))) {
                editor.draft.messages.push_back(HotkeyMessage{ "", 0, 0 });
            }
            ImGui::Separator();
            if (ImGui::BeginTable("##binder_messages_table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn(UiSettings::Instance().Text(UiText::ColumnText), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(UiSettings::Instance().Text(UiText::ColumnDelay), ImGuiTableColumnFlags_WidthFixed, ScaleUi(95.0f));
                ImGui::TableSetupColumn(UiSettings::Instance().Text(UiText::ColumnMethod), ImGuiTableColumnFlags_WidthFixed, ScaleUi(160.0f));
                ImGui::TableSetupColumn(UiSettings::Instance().Text(UiText::ColumnSpacer), ImGuiTableColumnFlags_WidthFixed, ScaleUi(32.0f));
                ImGui::TableHeadersRow();

                int removeIndex = -1;
                for (std::size_t i = 0; i < editor.draft.messages.size(); ++i) {
                    HotkeyMessage& message = editor.draft.messages[i];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    InputTextString("##msg_text", message.text, 0, 256);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputInt("##msg_delay", &message.intervalMs);
                    if (message.intervalMs < 0) {
                        message.intervalMs = 0;
                    }
                    ImGui::TableSetColumnIndex(2);
                    if (ImGui::BeginCombo("##msg_method", SendMethodLabel(message.method))) {
                        for (int method = 0; method <= 9; ++method) {
                            const bool selected = method == message.method;
                            if (ImGui::Selectable(SendMethodLabel(method), selected)) {
                                message.method = method;
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::TableSetColumnIndex(3);
                    if (ImGui::SmallButton(UiSettings::Instance().Text(UiText::ActionRemoveShort))) {
                        removeIndex = static_cast<int>(i);
                    }
                    ImGui::PopID();
                }

                if (removeIndex >= 0 && editor.draft.messages.size() > 1) {
                    editor.draft.messages.erase(editor.draft.messages.begin() + removeIndex);
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(UiSettings::Instance().Text(UiText::TriggersTab))) {
            InputTextString(UiSettings::Instance().Text(UiText::TextTrigger), editor.draft.textTrigger.text, ImGuiInputTextFlags_AutoSelectAll, 256);
            ImGui::Checkbox(UiSettings::Instance().Text(UiText::EnableTextTrigger), &editor.draft.textTrigger.enabled);
            ImGui::Checkbox(UiSettings::Instance().Text(UiText::UseRegex), &editor.draft.textTrigger.pattern);
            ImGui::Separator();
            ImGui::Checkbox(UiSettings::Instance().Text(UiText::CommandActivator), &editor.draft.commandEnabled);
            InputTextString(UiSettings::Instance().Text(UiText::Command), editor.draft.command, ImGuiInputTextFlags_AutoSelectAll, 128);
            ImGui::Separator();
            ImGui::Checkbox(UiSettings::Instance().Text(UiText::TextConfirmation), &editor.draft.textConfirmation.enabled);
            ImGui::Checkbox(UiSettings::Instance().Text(UiText::WaitWithoutTimeout), &editor.draft.textConfirmation.waitForResolution);
            ImGui::Text("%s", UiSettings::Instance().Format(
                UiText::ConfirmKeyFormat,
                ::hotkeys::KeyName(editor.draft.textConfirmation.key).c_str()).c_str());
            ImGui::SameLine();
            if (ImGui::Button((std::string(UiSettings::Instance().Text(UiText::Change)) + "##confirm").c_str())) {
                BeginCapture(CaptureTarget::ConfirmKey);
            }
            ImGui::Text("%s", UiSettings::Instance().Format(
                UiText::CancelKeyFormat,
                ::hotkeys::KeyName(editor.draft.textConfirmation.cancelKey).c_str()).c_str());
            ImGui::SameLine();
            if (ImGui::Button((std::string(UiSettings::Instance().Text(UiText::Change)) + "##cancel").c_str())) {
                BeginCapture(CaptureTarget::CancelKey);
            }

            ImGui::SeparatorText(UiSettings::Instance().Text(UiText::BlockingConditions));
            for (std::size_t i = 0; i < static_cast<std::size_t>(ConditionId::Count); ++i) {
                bool value = editor.draft.conditions[i];
                if (ImGui::Checkbox(ConditionLabel(static_cast<ConditionId>(i)), &value)) {
                    editor.draft.conditions[i] = value;
                }
            }
            ImGui::SeparatorText(UiSettings::Instance().Text(UiText::QuickMenuConditions));
            for (std::size_t i = 0; i < static_cast<std::size_t>(ConditionId::Count); ++i) {
                bool value = editor.draft.quickConditions[i];
                if (ImGui::Checkbox((std::string("QM##") + std::to_string(i)).c_str(), &value)) {
                    editor.draft.quickConditions[i] = value;
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(ConditionLabel(static_cast<ConditionId>(i)));
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(UiSettings::Instance().Text(UiText::InputTab))) {
            DrawInputEditor();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    DrawCapturePopup(true);

    ImGui::Separator();
    if (ImGui::Button(UiSettings::Instance().Text(UiText::Save))) {
        std::vector<std::string> errors;
        if (ValidateEditor(errors)) {
            SaveEditor();
            PushToast(UiSettings::Instance().Text(UiText::ToastBindSaved), ImVec4(0.20f, 0.35f, 0.18f, 0.95f), 1800.0);
            ImGui::CloseCurrentPopup();
        } else {
            for (const std::string& error : errors) {
                PushToast(error, ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2800.0);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(UiSettings::Instance().Text(UiText::Cancel))) {
        editor.active = false;
        editorPopupPending = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void BinderModule::Impl::DrawBindPane() {
    EnsureRootFolder();
    const auto filtered = FilteredBindIndices();
    if (selectedBindIndex >= 0
        && std::find(filtered.begin(), filtered.end(), selectedBindIndex) == filtered.end()) {
        selectedBindIndex = -1;
    }

    if (ImGui::Button((std::string(UiSettings::Instance().Text(UiText::AddBind)) + "##bind_add").c_str())) {
        StartEditing(-1, true);
    }

    InputTextString(UiSettings::Instance().Text(UiText::SearchBinds), bindSearch, ImGuiInputTextFlags_AutoSelectAll, 128);
    ImGui::Separator();

    UiSettings& ui = UiSettings::Instance();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float iconButtonSide = std::ceil(ImGui::GetFrameHeight() - ScaleUi(1.0f));
    const ImVec2 iconButtonSize(iconButtonSide, iconButtonSide);
    const float actionButtonsWidth = iconButtonSide * 4.0f + style.ItemSpacing.x * 3.0f;
    const float toggleColumnWidth = std::ceil(iconButtonSide + style.CellPadding.x * 2.0f);
    const float quickColumnWidth = std::ceil(iconButtonSide + style.CellPadding.x * 2.0f);
    const float actionsColumnWidth = std::ceil(actionButtonsWidth + style.CellPadding.x * 2.0f);
    if (ImGui::BeginTable(
            "##binder_binds_table",
            5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY
                | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(kIconToggleOn, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, toggleColumnWidth);
        ImGui::TableSetupColumn(kIconBolt, ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, quickColumnWidth);
        ImGui::TableSetupColumn(ui.Text(UiText::ColumnLaunch), ImGuiTableColumnFlags_WidthFixed, ScaleUi(90.0f));
        ImGui::TableSetupColumn(ui.Text(UiText::ColumnBind), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(
            ui.Text(UiText::ColumnActions),
            ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
            actionsColumnWidth);
        const float headerRowHeight = ImGui::GetTextLineHeight() + style.CellPadding.y * 2.0f;
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers, headerRowHeight);
        ImGui::TableSetColumnIndex(0);
        DrawCenteredTableHeaderLabel(kIconToggleOn, ui.Text(UiText::Enabled));
        ImGui::TableSetColumnIndex(1);
        DrawCenteredTableHeaderLabel(kIconBolt, ui.Text(UiText::ShowInQuickMenu));
        ImGui::TableSetColumnIndex(2);
        DrawCenteredTableHeaderLabel(ui.Text(UiText::ColumnLaunch));
        ImGui::TableSetColumnIndex(3);
        DrawCenteredTableHeaderLabel(ui.Text(UiText::ColumnBind));
        ImGui::TableSetColumnIndex(4);
        DrawCenteredTableHeaderLabel(ui.Text(UiText::ColumnActions));

        for (const int index : filtered) {
            HotkeyEntry& hotkey = hotkeys[index];
            const bool selected = selectedBindIndex == index;
            ImGui::TableNextRow();
            if (selected) {
                ImVec4 selectedColor = ImGui::GetStyle().Colors[ImGuiCol_Header];
                selectedColor.w *= 0.35f;
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(selectedColor));
            }
            ImGui::PushID(index);

            ImGui::TableSetColumnIndex(0);
            CenterNextItemHorizontally(iconButtonSide);
            if (SmallIconActionButton(
                    hotkey.enabled ? kIconToggleOn : kIconToggleOff, "##enabled", ui.Text(UiText::Enabled), iconButtonSize)) {
                hotkey.enabled = !hotkey.enabled;
                if (!hotkey.enabled) {
                    hotkey.quickMenu = false;
                }
                SaveConfig();
            }

            ImGui::TableSetColumnIndex(1);
            const bool dimQuickIcon = !hotkey.enabled || !hotkey.quickMenu;
            CenterNextItemHorizontally(iconButtonSide);
            if (dimQuickIcon) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }
            if (!hotkey.enabled) {
                ImGui::BeginDisabled();
            }
            if (SmallIconActionButton(kIconBolt, "##quick", ui.Text(UiText::ShowInQuickMenu), iconButtonSize)) {
                hotkey.quickMenu = !hotkey.quickMenu;
                SaveConfig();
            }
            if (!hotkey.enabled) {
                ImGui::EndDisabled();
            }
            if (dimQuickIcon) {
                ImGui::PopStyleColor();
            }

            ImGui::TableSetColumnIndex(2);
            const std::string launchSummary = BuildLaunchSummary(hotkey);
            ImGui::TextDisabled("%s", launchSummary.c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", launchSummary.c_str());
            }

            ImGui::TableSetColumnIndex(3);
            const ImVec2 bindCellPos = ImGui::GetCursorScreenPos();
            const float bindCellWidth = ImGui::GetContentRegionAvail().x;
            const float bindCellHeight = ImGui::GetFrameHeight();
            const float bindPadX = ScaleUi(6.0f);
            const float bindGap = ScaleUi(8.0f);
            const std::string bindNumber = "\xE2\x84\x96" + std::to_string(hotkey.number);
            const ImVec2 bindNumberSize = ImGui::CalcTextSize(bindNumber.c_str());
            const float bindTextY = bindCellPos.y + (bindCellHeight - ImGui::GetTextLineHeight()) * 0.5f;
            const float bindTextMinX = bindCellPos.x + bindPadX + bindNumberSize.x + bindGap;
            const float bindTextMaxWidth = std::max(0.0f, bindCellWidth - bindPadX * 2.0f - bindNumberSize.x - bindGap);
            const std::string bindName = EllipsizeText(hotkey.label, bindTextMaxWidth);
            const ImVec2 bindNameSize = ImGui::CalcTextSize(bindName.c_str());
            float bindNameX = bindCellPos.x + (bindCellWidth - bindNameSize.x) * 0.5f;
            const float bindNameMinX = bindTextMinX;
            const float bindNameMaxX =
                std::max(bindNameMinX, bindCellPos.x + bindCellWidth - bindPadX - bindNameSize.x);
            if (bindNameX < bindNameMinX) {
                bindNameX = bindNameMinX;
            }
            if (bindNameX > bindNameMaxX) {
                bindNameX = bindNameMaxX;
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddText(
                ImVec2(bindCellPos.x + bindPadX, bindTextY),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                bindNumber.c_str());
            drawList->AddText(
                ImVec2(bindNameX, bindTextY),
                ImGui::GetColorU32(ImGuiCol_Text),
                bindName.c_str());

            const bool bindClicked = ImGui::InvisibleButton("##bind_select", ImVec2(bindCellWidth, bindCellHeight));
            const bool bindHovered = ImGui::IsItemHovered();
            if (bindClicked) {
                selectedBindIndex = index;
            }
            if (bindHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                selectedBindIndex = index;
                StartEditing(index, false);
            }
            if (ImGui::BeginDragDropSource()) {
                const int hotkeyIndex = index;
                ImGui::SetDragDropPayload("BINDER_HOTKEY_INDEX", &hotkeyIndex, sizeof(hotkeyIndex));
                ImGui::TextUnformatted(bindName.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", ui.Format(UiText::BindListEntryFormat, hotkey.number, hotkey.label.c_str()).c_str());
            }

            ImGui::TableSetColumnIndex(4);
            CenterNextItemHorizontally(actionButtonsWidth);
            if (SmallIconActionButton(kIconPlay, "##run", ui.Text(UiText::Run), iconButtonSize)) {
                TryEnqueueHotkey(index, 0, "manual", "");
            }
            ImGui::SameLine(0.0f, ScaleUi(4.0f));
            if (SmallIconActionButton(kIconEdit, "##edit", ui.Text(UiText::Edit), iconButtonSize)) {
                StartEditing(index, false);
            }
            ImGui::SameLine(0.0f, ScaleUi(4.0f));
            if (SmallIconActionButton(kIconDelete, "##delete", ui.Text(UiText::Delete), iconButtonSize)) {
                bindDeleteTarget = index;
                bindDeletePopupPending = true;
            }
            ImGui::SameLine(0.0f, ScaleUi(4.0f));
            if (SmallIconActionButton(kIconBars, "##more", ui.Text(UiText::ColumnActions), iconButtonSize)) {
                ImGui::OpenPopup("##binder_bind_actions");
            }
            if (ImGui::BeginPopup("##binder_bind_actions")) {
                if (ImGui::MenuItem(ui.Text(UiText::ActionMoveTo))) {
                    moveBindTarget = index;
                    moveBindPopupPending = true;
                }
                if (ImGui::MenuItem(ui.Text(UiText::ActionDuplicate))) {
                    DuplicateHotkeyAt(index);
                }
                if (ImGui::MenuItem(ui.Text(UiText::ActionBindLines))) {
                    bindLinesTarget = index;
                    bindLinesPopupPending = true;
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (bindDeletePopupPending) {
        ImGui::OpenPopup("##binder_bind_delete");
        bindDeletePopupPending = false;
    }

    if (ImGui::BeginPopupModal("##binder_bind_delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", UiSettings::Instance().Text(UiText::DeleteSelectedBindQuestion));
        if (bindDeleteTarget >= 0 && bindDeleteTarget < static_cast<int>(hotkeys.size())) {
            ImGui::TextDisabled("%s", hotkeys[bindDeleteTarget].label.c_str());
        }
        if (ImGui::Button(UiSettings::Instance().Text(UiText::Delete))) {
            if (bindDeleteTarget >= 0 && bindDeleteTarget < static_cast<int>(hotkeys.size())) {
                hotkeys.erase(hotkeys.begin() + bindDeleteTarget);
                RefreshNumbers();
                SaveConfig();
                if (selectedBindIndex == bindDeleteTarget) {
                    selectedBindIndex = -1;
                }
            }
            bindDeleteTarget = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(UiSettings::Instance().Text(UiText::Cancel))) {
            bindDeleteTarget = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    DrawBindLinesPopup();
}

void BinderModule::Impl::DrawMoveBindPopup() {
    if (moveBindPopupPending) {
        ImGui::OpenPopup("##binder_move_bind");
        moveBindPopupPending = false;
    }

    if (!ImGui::BeginPopupModal("##binder_move_bind", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    HotkeyEntry* hotkey = nullptr;
    if (moveBindTarget >= 0 && moveBindTarget < static_cast<int>(hotkeys.size())) {
        hotkey = &hotkeys[static_cast<std::size_t>(moveBindTarget)];
    }

    ImGui::TextUnformatted(ui.Text(UiText::ActionMoveTo));
    ImGui::Separator();

    if (!hotkey) {
        moveBindTarget = -1;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextDisabled("%s", ui.Format(UiText::BindListEntryFormat, hotkey->number, hotkey->label.c_str()).c_str());
    ImGui::Spacing();

    const auto drawFolderNode = [&](auto&& self, FolderNode& folder) -> void {
        ImGui::PushID(folder.id);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (folder.children.empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        const bool opened = ImGui::TreeNodeEx(folder.name.c_str(), flags, "%s", folder.name.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            hotkey->folderPath = BuildFolderPath(&folder);
            SaveConfig();
            moveBindTarget = -1;
            ImGui::CloseCurrentPopup();
        }

        if (opened && !folder.children.empty()) {
            for (auto& child : folder.children) {
                if (child) {
                    self(self, *child);
                }
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    };

    if (ImGui::BeginChild("##binder_move_bind_folders", ScaleUi(360.0f, 240.0f), ImGuiChildFlags_Borders)) {
        for (auto& folder : folders) {
            if (folder) {
                drawFolderNode(drawFolderNode, *folder);
            }
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button(ui.Text(UiText::Cancel))) {
        moveBindTarget = -1;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void BinderModule::Impl::DrawBindLinesPopup() {
    if (bindLinesPopupPending) {
        ImGui::OpenPopup("##binder_bind_lines");
        bindLinesPopupPending = false;
    }

    if (!ImGui::BeginPopupModal("##binder_bind_lines", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    ImGui::TextUnformatted(ui.Text(UiText::BindLinesTitle));
    ImGui::Separator();

    HotkeyEntry* hotkey = nullptr;
    if (bindLinesTarget >= 0 && bindLinesTarget < static_cast<int>(hotkeys.size())) {
        hotkey = &hotkeys[static_cast<std::size_t>(bindLinesTarget)];
    }

    if (!hotkey) {
        bindLinesTarget = -1;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextDisabled("%s", ui.Format(UiText::BindListEntryFormat, hotkey->number, hotkey->label.c_str()).c_str());
    ImGui::Spacing();

    if (hotkey->messages.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::BindLinesEmpty));
    } else if (ImGui::BeginTable(
                   "##binder_bind_lines_table",
                   4,
                   ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                   ScaleUi(760.0f, 260.0f))) {
        ImGui::TableSetupColumn(ui.Text(UiText::ColumnText), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(ui.Text(UiText::ColumnDelay), ImGuiTableColumnFlags_WidthFixed, ScaleUi(90.0f));
        ImGui::TableSetupColumn(ui.Text(UiText::ColumnMethod), ImGuiTableColumnFlags_WidthFixed, ScaleUi(150.0f));
        ImGui::TableSetupColumn(ui.Text(UiText::ColumnActions), ImGuiTableColumnFlags_WidthFixed, ScaleUi(95.0f));
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < hotkey->messages.size(); ++i) {
            const HotkeyMessage& message = hotkey->messages[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            ImGui::TableSetColumnIndex(0);
            ImGui::TextWrapped("%s", message.text.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", message.intervalMs);

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(SendMethodLabel(message.method));

            ImGui::TableSetColumnIndex(3);
            if (ImGui::Button(ui.Text(UiText::Send))) {
                DoSend(message.text, message.method);
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button(ui.Text(UiText::Cancel))) {
        bindLinesTarget = -1;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void BinderModule::Impl::DrawCapturePopup(bool insideEditorPopup) {
    if (insideEditorPopup != CaptureUsesEditorPopup()) {
        return;
    }

    if (capturePopupPending) {
        ImGui::OpenPopup("##binder_capture_popup");
        capturePopupPending = false;
    }

    if (!ImGui::BeginPopupModal("##binder_capture_popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    if (!capture.Active()) {
        ImGui::CloseCurrentPopup();
        capturePopupInEditor = false;
        ImGui::EndPopup();
        return;
    }

    ImGui::TextWrapped("%s", UiSettings::Instance().Text(UiText::CapturePrompt));
    ImGui::Text("%s", UiSettings::Instance().Format(
        UiText::CurrentCombinationFormat,
        ::hotkeys::ToString(capture.Draft()).c_str()).c_str());
    if (capture.MouseCaptureArmed()) {
        ImGui::TextDisabled("%s", UiSettings::Instance().Text(UiText::WaitingMouseButton));
    }

    if (!capture.MouseCaptureArmed()) {
        std::vector<UINT> outKeys;
        if (ImGui::Button(UiSettings::Instance().Text(UiText::Save))) {
            capture.Save(outKeys);
            ApplyCapturedKeys(outKeys);
            capturePopupInEditor = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(UiSettings::Instance().Text(UiText::Clear))) {
            capture.Clear();
        }
        ImGui::SameLine();
        if (ImGui::Button(UiSettings::Instance().Text(UiText::Mouse))) {
            capture.ArmMouseCapture();
        }
        ImGui::SameLine();
        if (ImGui::Button(UiSettings::Instance().Text(UiText::Cancel))) {
            capture.Stop();
            captureTarget = CaptureTarget::None;
            captureHotkeyIndex = -1;
            capturePopupPending = false;
            capturePopupInEditor = false;
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();
}

void BinderModule::Impl::DrawQuickFolderRecursive(FolderNode& folder) {
    for (const int index : QuickEntriesForFolder(folder)) {
        HotkeyEntry& hotkey = hotkeys[index];
        const std::string buttonLabel = hotkey.label + "##quick_" + std::to_string(index);
        if (ImGui::Button(buttonLabel.c_str(), ImVec2(-FLT_MIN, 0.0f))) {
            quickMenuOpen = false;
            quickMenuReopenBlocked = true;
            TryEnqueueHotkey(index, 0, "quick_menu", "");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ::hotkeys::ToString(hotkey.keys, hotkey.hotkeyMode).c_str());
    }

    for (auto& child : folder.children) {
        if (!child || !FolderHasVisibleQuickEntries(*child)) {
            continue;
        }
        if (ImGui::TreeNodeEx(child.get(), ImGuiTreeNodeFlags_DefaultOpen, "%s", child->name.c_str())) {
            DrawQuickFolderRecursive(*child);
            ImGui::TreePop();
        }
    }
}

std::string BinderModule::Impl::BuildLaunchSummary(const HotkeyEntry& hotkey) const {
    std::vector<std::string> parts;

    const std::string triggerText = Trim(hotkey.textTrigger.text);
    if (hotkey.textTrigger.enabled && !triggerText.empty()) {
        parts.push_back(std::string(kIconComment) + " " + triggerText);
    }

    const std::string commandText = Trim(hotkey.command);
    if (hotkey.commandEnabled && !commandText.empty()) {
        parts.push_back(std::string(kIconTerminal) + " " + commandText);
    }

    if (!hotkey.keys.empty()) {
        parts.push_back(std::string(kIconKeyboard) + " " + ::hotkeys::ToString(hotkey.keys, hotkey.hotkeyMode));
    }

    if (parts.empty()) {
        return UiSettings::Instance().Text(UiText::HotkeyNotSet);
    }

    std::ostringstream stream;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            stream << "   ";
        }
        stream << parts[i];
    }
    return stream.str();
}

void BinderModule::Impl::DrawSettingsSection() {
    EnsureInitialized();

    UiSettings& ui = UiSettings::Instance();
    ImGui::SeparatorText(ui.Text(UiText::QuickMenuWindowTitle));
    ImGui::Text("%s", ui.Format(UiText::QuickMenuFormat, ::hotkeys::ToString(CurrentQuickMenuHotkey()).c_str()).c_str());
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::ChangeQuickMenuHotkey))) {
        BeginCapture(CaptureTarget::QuickMenuHotkey);
    }

    const QuickMenuActivationMode quickModes[] = { QuickMenuActivationMode::Hold, QuickMenuActivationMode::Toggle };
    const char* quickModeLabels[] = {
        QuickMenuModeLabel(QuickMenuActivationMode::Hold),
        QuickMenuModeLabel(QuickMenuActivationMode::Toggle),
    };
    int quickMode = quickMenuActivationMode == QuickMenuActivationMode::Toggle ? 1 : 0;
    ImGui::SetNextItemWidth(ScaleUi(180.0f));
    if (ImGui::Combo(ui.Text(UiText::QuickMenuMode), &quickMode, quickModeLabels, IM_ARRAYSIZE(quickModeLabels))) {
        quickMenuActivationMode = quickModes[quickMode];
        SaveConfig();
    }
}

void BinderModule::Impl::DrawQuickMenu() {
    if (!quickMenuOpen) {
        return;
    }

    std::vector<FolderNode*> visibleFolders;
    for (auto& folder : folders) {
        if (folder && FolderHasVisibleQuickEntries(*folder)) {
            visibleFolders.push_back(folder.get());
        }
    }
    if (visibleFolders.empty()) {
        quickMenuOpen = false;
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (quickMenuPos.x == 0.0f && quickMenuPos.y == 0.0f) {
        quickMenuSize = ScaleUi(static_cast<float>(kQuickMenuWidth), static_cast<float>(kQuickMenuHeight));
        quickMenuPos = ImVec2((io.DisplaySize.x - quickMenuSize.x) * 0.5f, (io.DisplaySize.y - quickMenuSize.y) * 0.5f);
    }

    bool windowOpen = true;
    ImGui::SetNextWindowPos(quickMenuPos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(quickMenuSize, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(UiSettings::Instance().Text(UiText::QuickMenuWindowTitle), &windowOpen, ImGuiWindowFlags_NoCollapse)) {
        quickMenuPos = ImGui::GetWindowPos();
        quickMenuSize = ImGui::GetWindowSize();

        if (ImGui::BeginTabBar("##quick_menu_tabs")) {
            for (std::size_t i = 0; i < visibleFolders.size(); ++i) {
                FolderNode& folder = *visibleFolders[i];
                if (ImGui::BeginTabItem(folder.name.c_str())) {
                    quickMenuTabIndex = static_cast<int>(i);
                    DrawQuickFolderRecursive(folder);
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    if (!windowOpen) {
        quickMenuOpen = false;
        quickMenuReopenBlocked = true;
    }
}

void BinderModule::Impl::DrawInputDialog() {
    if (inputDialog) {
        ImGui::OpenPopup("##binder_input_dialog");
    }

    if (!ImGui::BeginPopupModal("##binder_input_dialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    if (!inputDialog || inputDialog->hotkeyIndex < 0 || inputDialog->hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        inputDialog.reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    HotkeyEntry& hotkey = hotkeys[inputDialog->hotkeyIndex];
    ImGui::TextWrapped("%s", UiSettings::Instance().Format(UiText::FillBindParametersFormat, hotkey.label.c_str()).c_str());
    ImGui::Separator();

    auto rebuildSelectedText = [](InputDialogField& field) {
        std::ostringstream stream;
        bool first = true;
        for (const int idx : field.selectedButtons) {
            if (idx < 0 || idx >= static_cast<int>(field.input.buttons.size())) {
                continue;
            }
            const InputButton& button = field.input.buttons[static_cast<std::size_t>(idx)];
            const std::string value = !button.text.empty() ? button.text : button.label;
            if (value.empty()) {
                continue;
            }
            if (!first) {
                stream << (field.input.multiSeparator.empty() ? ", " : field.input.multiSeparator);
            }
            stream << value;
            first = false;
        }
        field.textValue = stream.str();
    };

    for (std::size_t i = 0; i < inputDialog->fields.size(); ++i) {
        InputDialogField& field = inputDialog->fields[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::SeparatorText(field.input.label.empty() ? field.input.key.c_str() : field.input.label.c_str());
        if (!field.input.hint.empty()) {
            ImGui::TextDisabled("%s", field.input.hint.c_str());
        }

        if (field.input.mode == InputMode::Text) {
            InputTextString("##input_value", field.textValue, 0, 256);
        } else {
            const auto filteredButtons = FilterButtons(*inputDialog, i);
            if (ImGui::BeginChild("##input_buttons", ScaleUi(420.0f, 140.0f), ImGuiChildFlags_Borders)) {
                for (const int buttonIndex : filteredButtons) {
                    const InputButton& button = field.input.buttons[static_cast<std::size_t>(buttonIndex)];
                    const std::string value = !button.text.empty() ? button.text : button.label;
                    if (field.input.multiSelect) {
                        const bool selected = field.selectedButtons.contains(buttonIndex);
                        if (ImGui::Selectable(button.label.c_str(), selected)) {
                            if (selected) {
                                field.selectedButtons.erase(buttonIndex);
                            } else {
                                field.selectedButtons.insert(buttonIndex);
                            }
                            rebuildSelectedText(field);
                        }
                    } else {
                        const bool selected = field.selectedButtonIndex.value_or(-1) == buttonIndex;
                        if (ImGui::Selectable(button.label.c_str(), selected)) {
                            field.selectedButtonIndex = buttonIndex;
                            if (field.input.mode == InputMode::ButtonsListText) {
                                field.textValue = value;
                            }
                        }
                    }
                    if (!button.hint.empty() && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", button.hint.c_str());
                    }
                }
            }
            ImGui::EndChild();

            if (field.input.mode == InputMode::ButtonsListText) {
                InputTextString("##input_text_value", field.textValue, 0, 256);
            }
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button(UiSettings::Instance().Text(UiText::Launch))) {
        std::map<std::string, std::string> values;
        for (std::size_t i = 0; i < inputDialog->fields.size(); ++i) {
            const InputDialogField& field = inputDialog->fields[i];
            const std::string key = NormalizeInputKey(field.input.key);
            const std::string value = BuildInputValue(field);
            if (!key.empty()) {
                values[key] = value;
                values[ToLower(key)] = value;
            }
            values[std::to_string(i + 1)] = value;
        }

        runningBinds.push_back(RunningBind{
            inputDialog->hotkeyIndex,
            std::move(values),
            0,
            static_cast<double>(GetTickCount64() + std::max(inputDialog->startDelayMs, 0)),
        });
        hotkey.awaitingInput = false;
        inputDialog.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(UiSettings::Instance().Text(UiText::Cancel))) {
        hotkey.awaitingInput = false;
        inputDialog.reset();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void BinderModule::Impl::DrawMainTab() {
    EnsureInitialized();

    ImGui::SeparatorText(UiSettings::Instance().Text(UiText::BinderSectionTitle));

    if (ImGui::BeginTable("##binder_layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(UiSettings::Instance().Text(UiText::ColumnFolders), ImGuiTableColumnFlags_WidthFixed, ScaleUi(260.0f));
        ImGui::TableSetupColumn(UiSettings::Instance().Text(UiText::ColumnBinds), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawFolderPane();
        ImGui::TableSetColumnIndex(1);
        DrawBindPane();
        ImGui::EndTable();
    }

    DrawFolderPopups();
    DrawEditor();
    DrawMoveBindPopup();
}

void BinderModule::Impl::DrawOverlay() {
    DrawQuickMenu();
    DrawInputDialog();
    DrawCapturePopup(false);
    DrawToasts();
}

BinderModule::BinderModule() : impl_(std::make_unique<Impl>()) {
}

BinderModule::~BinderModule() = default;

BinderModule::BinderModule(BinderModule&&) noexcept = default;
BinderModule& BinderModule::operator=(BinderModule&&) noexcept = default;

void BinderModule::OnProcessAttach(HMODULE module) {
    impl_->OnProcessAttach(module);
}

void BinderModule::SetSampApi(SampApi* sampApi) {
    impl_->SetSampApi(sampApi);
}

void BinderModule::SetSampHooks(SampHooks* sampHooks) {
    impl_->SetSampHooks(sampHooks);
}

void BinderModule::SetSampRakHooks(SampRakHooks* sampRakHooks) {
    impl_->SetSampRakHooks(sampRakHooks);
}

void BinderModule::Tick() {
    impl_->Tick();
}

void BinderModule::Shutdown() {
    impl_->Shutdown();
}

bool BinderModule::OnWindowMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    return impl_->OnWindowMessage(message, wparam, lparam);
}

bool BinderModule::WantsOverlayRender() const {
    return impl_->WantsOverlayRender();
}

bool BinderModule::WantsInputCapture() const {
    return impl_->WantsInputCapture();
}

bool BinderModule::WantsQuickMenuCursor() const {
    return impl_->WantsQuickMenuCursor();
}

void BinderModule::DrawMainTab() {
    impl_->DrawMainTab();
}

void BinderModule::DrawSettingsSection() {
    impl_->DrawSettingsSection();
}

void BinderModule::DrawOverlay() {
    impl_->DrawOverlay();
}
