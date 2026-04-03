#include "binder_module.h"

#include "app_config.h"
#include "debug_log.h"
#include "hotkey_utils.h"
#include "json_utils.h"
#include "samp_api.h"
#include "samp_hooks.h"
#include "samp_rak_hooks.h"
#include "tags_module.h"
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
#include <cstdint>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr UINT kDefaultConfirmKey = '1';
constexpr UINT kDefaultCancelKey = '2';
constexpr UINT kDefaultQuickMenuFallback = VK_XBUTTON1;
constexpr int kMaxToasts = 8;
constexpr int kMinMessageIntervalMs = 0;
constexpr double kHotkeyDebounceMs = 0.0;
constexpr int kDefaultRepeatIntervalMs = 500;
constexpr int kQuickMenuWidth = 320;
constexpr int kQuickMenuHeight = 360;
constexpr double kQuickMenuSubmenuCloseGraceSeconds = 0.5;
constexpr int kTextConfirmTimeoutMs = 5000;
constexpr int kOutgoingGuardTimeoutMs = 2000;
constexpr char kIconToggleOff[] = "\xEF\x88\x84";
constexpr char kIconToggleOn[] = "\xEF\x88\x85";
constexpr char kIconBolt[] = "\xEF\x83\xA7";
constexpr char kIconComment[] = "\xEF\x83\xA5";
constexpr char kIconMessageDots[] = "\xEF\x92\xA3";
constexpr char kIconTerminal[] = "\xEF\x84\xA0";
constexpr char kIconKeyboard[] = "\xEF\x84\x9C";
constexpr char kIconPlay[] = "\xEF\x81\x8B";
constexpr char kIconPause[] = "\xEF\x81\x8C";
constexpr char kIconStop[] = "\xEF\x81\x8D";
constexpr char kIconEdit[] = "\xEF\x81\x84";
constexpr char kIconDelete[] = "\xEF\x8B\xAD";
constexpr char kIconBars[] = "\xEF\x83\x89";
constexpr char kIconAngleDown[] = "\xEF\x84\x87";
constexpr char kIconAngleUp[] = "\xEF\x84\x86";
constexpr char kIconAngleRight[] = "\xEF\x84\x85";
constexpr char kIconBracketsCurly[] = "\xEF\x9F\xAA";
constexpr char kIconFolder[] = "\xEF\x84\x94";
constexpr char kIconClone[] = "\xEF\x89\x8D";
constexpr char kIconMoveRows[] = "\xEF\x81\xBD";
constexpr char kIconSliders[] = "\xEF\x87\x9E";
constexpr char kIconTags[] = "\xEF\x80\xAC";
constexpr char kIconSaveDisk[] = "\xEF\x83\x87";
constexpr char kIconChevronLeft[] = "\xEF\x81\x93";
constexpr char kIconChevronRight[] = "\xEF\x81\x94";
constexpr char kDialogCaptionLocalChatColorTag[] = "{E2C063}";
constexpr char kDialogSelectionLocalChatColorTag[] = "{E2C063}";
constexpr char kIconStar[] = "\xEF\x80\x86";

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

std::size_t CountUtf8Codepoints(std::string_view value) {
    std::size_t count = 0;
    while (!value.empty()) {
        ImWchar codepoint = 0;
        std::size_t advance = 1;
        if (DecodeFirstUtf8Codepoint(value, codepoint)) {
            const unsigned char first = static_cast<unsigned char>(value.front());
            if (first < 0x80) {
                advance = 1;
            } else if ((first & 0xE0) == 0xC0) {
                advance = 2;
            } else if ((first & 0xF0) == 0xE0) {
                advance = 3;
            } else if ((first & 0xF8) == 0xF0) {
                advance = 4;
            }
        }

        value.remove_prefix(std::min(advance, value.size()));
        ++count;
    }
    return count;
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

bool IconOnlyButton(
    const char* icon,
    const char* id,
    const char* tooltip,
    const ImVec2& size,
    bool active = true,
    ImDrawList* drawList = nullptr) {
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
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

    ImGuiCol iconColorId = active ? ImGuiCol_Text : ImGuiCol_TextDisabled;
    if (hovered || held) {
        iconColorId = ImGuiCol_Text;
    }
    if (!drawList) {
        drawList = ImGui::GetWindowDrawList();
    }
    drawList->AddText(iconPos, ImGui::GetColorU32(iconColorId), icon);

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

std::string FormatFolderLabel(std::string_view name) {
    if (name.empty()) {
        return kIconFolder;
    }
    return std::string(kIconFolder) + " " + std::string(name);
}

std::string FormatFolderPathLabel(const std::vector<std::string>& path) {
    return FormatFolderLabel(JoinPath(path));
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

using JsonArray = jsonutil::JsonArray;
using JsonObject = jsonutil::JsonObject;
using JsonValue = jsonutil::JsonValue;

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
    std::uint64_t runtimeId = 0;

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

struct LaunchCellContent {
    std::string primary;
    std::vector<std::string> secondary;
};

void AppendLaunchLabel(std::vector<std::string>& labels, const char* icon, std::string text) {
    if (text.empty()) {
        return;
    }

    labels.push_back(std::string(icon) + " " + text);
}

std::vector<std::string> BuildLaunchLabels(const HotkeyEntry& hotkey) {
    std::vector<std::string> labels;

    if (!hotkey.keys.empty()) {
        AppendLaunchLabel(labels, kIconKeyboard, ::hotkeys::ToString(hotkey.keys, hotkey.hotkeyMode));
    }

    const std::string commandText = Trim(hotkey.command);
    if (hotkey.commandEnabled && !commandText.empty()) {
        AppendLaunchLabel(labels, kIconTerminal, commandText);
    }

    const std::string triggerText = Trim(hotkey.textTrigger.text);
    if (hotkey.textTrigger.enabled && !triggerText.empty()) {
        AppendLaunchLabel(labels, kIconComment, triggerText);
    }

    return labels;
}

LaunchCellContent BuildLaunchCellContent(const HotkeyEntry& hotkey) {
    const std::vector<std::string> labels = BuildLaunchLabels(hotkey);
    if (labels.empty()) {
        return { UiSettings::Instance().Text(UiText::HotkeyNotSet), {} };
    }

    LaunchCellContent content;
    content.primary = labels.front();
    content.secondary.assign(labels.begin() + 1, labels.end());
    return content;
}

std::string JoinLaunchLabels(const std::vector<std::string>& labels, std::string_view separator) {
    std::ostringstream stream;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (i != 0) {
            stream << separator;
        }
        stream << labels[i];
    }
    return stream.str();
}

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
    std::uint64_t hotkeyRuntimeId = 0;
    std::map<std::string, std::string> inputValues;
    std::size_t messageIndex = 0;
    double nextAtMs = 0.0;
    std::string activationSource;
    std::string activationText;
    std::string bindCommand;
    bool paused = false;
    double remainingDelayMs = 0.0;
};

enum class BindTagAction {
    Disable,
    Enable,
    Start,
    Stop,
    Pause,
    Unpause,
    FastMenu,
    UnfastMenu,
    Random,
    Ended,
    StopAll,
    Popup,
    Unknown,
};

BindTagAction ParseBindTagActionName(std::string_view action) {
    const std::string normalized = ToLower(Trim(action));
    if (normalized == "disable") {
        return BindTagAction::Disable;
    }
    if (normalized == "enable") {
        return BindTagAction::Enable;
    }
    if (normalized == "start") {
        return BindTagAction::Start;
    }
    if (normalized == "stop") {
        return BindTagAction::Stop;
    }
    if (normalized == "pause") {
        return BindTagAction::Pause;
    }
    if (normalized == "unpause") {
        return BindTagAction::Unpause;
    }
    if (normalized == "fastmenu") {
        return BindTagAction::FastMenu;
    }
    if (normalized == "unfastmenu") {
        return BindTagAction::UnfastMenu;
    }
    if (normalized == "random") {
        return BindTagAction::Random;
    }
    if (normalized == "ended") {
        return BindTagAction::Ended;
    }
    if (normalized == "stopall") {
        return BindTagAction::StopAll;
    }
    if (normalized == "popup") {
        return BindTagAction::Popup;
    }
    return BindTagAction::Unknown;
}

struct BindTagToken {
    std::string value{};
    bool quoted = false;
};

std::vector<BindTagToken> TokenizeBindTagArgs(std::string_view raw) {
    std::vector<BindTagToken> out;
    std::size_t i = 0;
    while (i < raw.size()) {
        while (i < raw.size()) {
            const char ch = raw[i];
            if (ch == ',' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
                ++i;
            } else {
                break;
            }
        }
        if (i >= raw.size()) {
            break;
        }

        const char opener = raw[i];
        if (opener == '"' || opener == '\'') {
            const char quote = opener;
            std::string token;
            bool escaped = false;
            ++i;
            while (i < raw.size()) {
                const char ch = raw[i];
                if (escaped) {
                    token.push_back(ch);
                    escaped = false;
                } else if (ch == '\\') {
                    escaped = true;
                } else if (ch == quote) {
                    ++i;
                    break;
                } else {
                    token.push_back(ch);
                }
                ++i;
            }
            out.push_back(BindTagToken{ std::move(token), true });
            continue;
        }

        const std::size_t start = i;
        while (i < raw.size()) {
            const char ch = raw[i];
            if (ch == ',' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
                break;
            }
            ++i;
        }

        if (i > start) {
            out.push_back(BindTagToken{ std::string(raw.substr(start, i - start)), false });
        }
    }

    return out;
}

struct BindTagContextDesc {
    int hotkeyIndex = -1;
    std::string name{};
    std::string folder{};
};

struct BindTagSelector {
    bool hasTokens = false;
    int contextHotkeyIndex = -1;
    std::string folderQuery{};
    bool folderExact = false;
    bool all = false;
    bool byIndex = false;
    int index = 0;
    std::string name{};
    bool nameExact = false;
};

struct PendingBindTagAction {
    BindTagAction action = BindTagAction::Unknown;
    std::uint64_t sourceRuntimeId = 0;
    std::string actionName{};
    std::vector<int> targetIndices{};
};

struct InputDialogField {
    HotkeyInput input;
    std::string textValue;
    std::string searchValue;
    std::optional<int> selectedButtonIndex;
    std::set<int> selectedButtons;
};

struct InputDialogState {
    int hotkeyIndex = -1;
    int startDelayMs = 0;
    std::vector<InputDialogField> fields;
    std::string activationSource;
    std::string activationText;
    std::string bindCommand;
};

struct ButtonsTextParseStats {
    int total = 0;
    int used = 0;
    int ignored = 0;
    int extraPipes = 0;
};

struct ButtonsBulkPreviewState {
    bool active = false;
    int count = 0;
    ButtonsTextParseStats stats{};
};

enum class CaptureTarget {
    None,
    BindHotkey,
    QuickMenuHotkey,
    ConfirmKey,
    CancelKey,
};

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
std::size_t CountUtf8Codepoints(std::string_view value);
std::vector<InputButton> ParseButtonsText(std::string_view multiLine);
std::vector<InputButton> ParseButtonsTextEx(std::string_view multiLine, ButtonsTextParseStats* stats);
std::string SerializeButtonsText(const std::vector<InputButton>& buttons);
void AppendButtonsBulkLine(std::string& text, std::string_view line);
std::string BuildButtonsBulkTemplateLine(int index);
void AppendButtonsBulkTemplateLines(std::string& text, int count);
bool InputTextString(const char* label, std::string& value, ImGuiInputTextFlags flags = 0, std::size_t minBuffer = 256);
bool InputTextMultilineString(
    const char* label,
    std::string& value,
    const ImVec2& size,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 2048);
bool InputTextMultilineWithCounterString(
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

bool ContainsNoCase(std::string_view haystack, std::string_view needle) {
    const std::string loweredNeedle = ToLower(needle);
    if (loweredNeedle.empty()) {
        return true;
    }
    return ToLower(haystack).find(loweredNeedle) != std::string::npos;
}

std::string EscapeBindTagToken(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 4);
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string QuoteBindTagToken(std::string_view value) {
    return "\"" + EscapeBindTagToken(value) + "\"";
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

std::string EscapeButtonsField(std::string_view value) {
    std::string escaped;
    for (const char ch : NormalizeLineEndings(value)) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '|':
            escaped += "\\|";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

std::string UnescapeButtonsField(std::string_view value) {
    std::string unescaped;
    unescaped.reserve(value.size());
    bool escaped = false;
    for (const char ch : value) {
        if (escaped) {
            switch (ch) {
            case 'n':
                unescaped.push_back('\n');
                break;
            case '|':
            case '\\':
                unescaped.push_back(ch);
                break;
            default:
                unescaped.push_back(ch);
                break;
            }
            escaped = false;
            continue;
        }

        if (ch == '\\') {
            escaped = true;
            continue;
        }

        unescaped.push_back(ch);
    }

    if (escaped) {
        unescaped.push_back('\\');
    }
    return unescaped;
}

std::vector<std::string> SplitEscapedButtonsLine(std::string_view line) {
    std::vector<std::string> parts;
    std::string current;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '\\' && i + 1 < line.size()) {
            current.push_back(ch);
            current.push_back(line[i + 1]);
            ++i;
            continue;
        }

        if (ch == '|') {
            parts.push_back(std::move(current));
            current.clear();
            continue;
        }

        current.push_back(ch);
    }
    parts.push_back(std::move(current));
    return parts;
}

std::vector<InputButton> ParseButtonsTextEx(std::string_view multiLine, ButtonsTextParseStats* stats) {
    if (stats) {
        *stats = {};
    }

    std::vector<InputButton> buttons;
    std::istringstream stream(NormalizeLineEndings(multiLine));
    std::string line;
    while (std::getline(stream, line)) {
        if (stats) {
            ++stats->total;
        }
        line = Trim(line);
        if (line.empty() || line.front() == '#') {
            if (stats) {
                ++stats->ignored;
            }
            continue;
        }
        if (stats) {
            ++stats->used;
        }

        std::vector<std::string> parts = SplitEscapedButtonsLine(line);
        if (parts.size() > 4) {
            if (stats) {
                ++stats->extraPipes;
            }
            std::ostringstream extra;
            for (std::size_t i = 3; i < parts.size(); ++i) {
                if (i != 3) {
                    extra << '|';
                }
                extra << parts[i];
            }
            parts.resize(4);
            parts[3] = extra.str();
        }

        InputButton button;
        if (!parts.empty()) {
            button.label = Trim(UnescapeButtonsField(parts[0]));
        }
        if (parts.size() > 1) {
            button.text = Trim(UnescapeButtonsField(parts[1]));
        }
        if (parts.size() > 2) {
            button.hint = Trim(UnescapeButtonsField(parts[2]));
        }
        if (parts.size() > 3) {
            button.when = Trim(UnescapeButtonsField(parts[3]));
        }
        if (button.label.empty()) {
            button.label = UiSettings::Instance().Format(UiText::ButtonLabelFormat, static_cast<int>(buttons.size() + 1));
        }
        buttons.push_back(std::move(button));
    }
    return buttons;
}

std::vector<InputButton> ParseButtonsText(std::string_view multiLine) {
    return ParseButtonsTextEx(multiLine, nullptr);
}

std::string SerializeButtonsText(const std::vector<InputButton>& buttons) {
    std::ostringstream stream;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const InputButton& button = buttons[i];
        stream << EscapeButtonsField(button.label)
               << " | " << EscapeButtonsField(button.text)
               << " | " << EscapeButtonsField(button.hint)
               << " | " << EscapeButtonsField(button.when);
        if (i + 1 != buttons.size()) {
            stream << "\n";
        }
    }
    return stream.str();
}

void AppendButtonsBulkLine(std::string& text, std::string_view line) {
    if (!text.empty() && text.back() != '\n') {
        text.push_back('\n');
    }
    text.append(line.data(), line.size());
}

std::string BuildButtonsBulkTemplateLine(int index) {
    UiSettings& ui = UiSettings::Instance();
    return EscapeButtonsField(ui.Format(UiText::ButtonLabelFormat, index))
        + " | " + EscapeButtonsField(ui.Format(UiText::ButtonsBulkTemplateValueFormat, index))
        + " | " + EscapeButtonsField(ui.Format(UiText::ButtonsBulkTemplateHintFormat, index))
        + " | ";
}

void AppendButtonsBulkTemplateLines(std::string& text, int count) {
    const int startIndex = static_cast<int>(ParseButtonsText(text).size()) + 1;
    for (int i = 0; i < count; ++i) {
        AppendButtonsBulkLine(text, BuildButtonsBulkTemplateLine(startIndex + i));
    }
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

bool InputTextWithHintString(const char* label, const char* hint, std::string& value, ImGuiInputTextFlags flags, std::size_t minBuffer) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value, nullptr, nullptr };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputTextWithHint(
        label, hint, value.data(), value.capacity() + 1, flags, ImGuiStringResizeCallback, &userData);
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

bool InputTextMultilineWithCounterString(
    const char* label,
    std::string& value,
    const ImVec2& size,
    ImGuiInputTextFlags flags,
    std::size_t minBuffer) {
    const bool changed = InputTextMultilineString(label, value, size, flags, minBuffer);

    const std::string counter = std::to_string(CountUtf8Codepoints(value));
    const float counterWidth = ImGui::CalcTextSize(counter.c_str()).x;
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    if (availableWidth > counterWidth) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availableWidth - counterWidth));
    }
    ImGui::TextDisabled("%s", counter.c_str());

    return changed;
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

bool IsLegacyProtectedRootFolderName(std::string_view name) {
    return name == "Биндер" || name == "Binder";
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
    SampApi* sampApi = nullptr;
    SampHooks* sampHooks = nullptr;
    SampRakHooks* sampRakHooks = nullptr;
    TagsModule* tagsModule = nullptr;

    std::vector<std::unique_ptr<FolderNode>> folders{};
    std::vector<HotkeyEntry> hotkeys{};
    FolderNode* selectedFolder = nullptr;
    int nextFolderId = 1;
    std::uint64_t nextHotkeyRuntimeId = 1;
    bool configLoaded = false;
    bool chatHookBound = false;
    bool rakHooksBound = false;

    std::string bindSearch{};
    std::string folderSearch{};

    struct EditorState {
        enum class Tab {
            Scenario = 0,
            MultiInput = 1,
            InputFields = 2,
        };

        enum class PendingAction {
            None = 0,
            Close,
            Navigate,
        };

        bool active = false;
        bool isNew = false;
        int hotkeyIndex = -1;
        int selectedInputIndex = -1;
        int selectedInputButtonIndex = -1;
        int bulkMethod = 2;
        int bulkIntervalMs = 0;
        int pendingTargetIndex = -1;
        std::string selectedButtonsText{};
        std::vector<std::string> inputButtonsBulkDrafts{};
        std::vector<ButtonsBulkPreviewState> inputButtonsBulkPreviews{};
        std::string multiText{};
        Tab activeTab = Tab::Scenario;
        PendingAction pendingAction = PendingAction::None;
        bool tabSelectionPending = false;
        bool focusNamePending = false;
        bool startSectionCollapsed = false;
        bool conditionsPopupPending = false;
        bool quickConditionsPopupPending = false;
        bool variablesPopupPending = false;
        bool variablesTabSelectionPending = false;
        bool variablesKeyPickerPopupPending = false;
        bool previewPopupPending = false;
        bool discardPopupPending = false;
        int variablesActiveTab = 0;
        int selectedVariableInputIndex = -1;
        int selectedSimpleTagIndex = -1;
        int selectedFunctionTagIndex = -1;
        std::string variablesSearch{};
        std::string variablesKeyPickerSearch{};
        HotkeyEntry baseline{};
        HotkeyEntry draft{};
    } editor{};

    struct FolderPopupState {
        FolderNode* target = nullptr;
        FolderNode* parent = nullptr;
        std::string name{};
    } folderPopup{};

    FolderNode* folderDeleteTarget = nullptr;
    bool folderEditPopupPending = false;
    bool folderDeletePopupPending = false;
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
    hotkeys::CapturePopupState capturePopupState{};
    CaptureTarget captureTarget = CaptureTarget::None;
    int captureHotkeyIndex = -1;
    bool capturePopupInEditor = false;

    std::vector<UINT> quickMenuHotkey{};
    QuickMenuActivationMode quickMenuActivationMode = QuickMenuActivationMode::Hold;
    bool quickMenuOpen = false;
    bool quickMenuReopenBlocked = false;
    bool quickMenuToggleLatch = false;
    int quickMenuTabIndex = 0;
    int quickMenuTabSelectRequest = -1;
    ImVec2 quickMenuPos{ 0.0f, 0.0f };
    ImVec2 quickMenuSize{ static_cast<float>(kQuickMenuWidth), static_cast<float>(kQuickMenuHeight) };
    std::map<std::string, bool> quickMenuSubmenuOpen{};
    std::map<std::string, ImVec2> quickMenuSubmenuPos{};
    std::map<std::string, FolderNode*> quickMenuSubmenuNode{};
    std::map<std::string, double> quickMenuSubmenuCloseDeadline{};
    std::vector<std::string> quickMenuSubmenuPaths{};

    std::optional<InputDialogState> inputDialog{};
    std::vector<RunningBind> runningBinds{};
    std::deque<Toast> toasts{};
    std::vector<OutgoingGuard> outgoingGuards{};
    std::vector<PendingBindTagAction> pendingBindTagActions{};

    void EnsureInitialized();
    void OnProcessAttach(HMODULE moduleHandle);
    void SetSampApi(SampApi* api);
    void SetSampHooks(SampHooks* hooks);
    void SetSampRakHooks(SampRakHooks* hooks);
    void SetTagsModule(TagsModule* module);
    void ConnectHooks();
    FolderNode* EnsureRootFolder();
    std::uint64_t AllocateHotkeyRuntimeId();
    HotkeyEntry MakeDefaultHotkey();
    void RefreshNumbers();
    void SaveConfig();
    void LoadConfig();
    JsonValue SerializeFolder(const FolderNode& folder) const;
    std::unique_ptr<FolderNode> DeserializeFolder(const JsonObject& object, FolderNode* parent);
    JsonValue SerializeHotkey(const HotkeyEntry& hotkey) const;
    HotkeyEntry DeserializeHotkey(const JsonObject& object);
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
    bool ApplyCapturedKeys(const std::vector<UINT>& keys);
    bool DescribeMainWindowHotkeyConflict(const std::vector<UINT>& keys, std::string& description);
    bool DescribeConflictWithMenuToggleHotkey(const std::vector<UINT>& keys, HotkeyMode mode, std::string& description) const;
    bool DescribeQuickMenuConflictWithMenuToggleHotkey(const std::vector<UINT>& keys, std::string& description) const;
    std::vector<UINT> CurrentQuickMenuHotkey() const;
    bool IsQuickMenuComboPressed() const;
    bool IsMainWindowHotkeyPressed() const;
    bool CaptureUsesEditorPopup() const;
    void UpdateQuickMenuState();
    void ResetQuickMenuVisualState();
    void ProcessHotkeys();
    void ProcessRunningBinds();
    int FindHotkeyIndexByRuntimeId(std::uint64_t runtimeId) const;
    BindTagContextDesc DescribeBindTagContext(std::uint64_t runtimeId) const;
    std::string BuildThisbindTagValue(std::uint64_t runtimeId) const;
    bool IsRuntimeActive(std::uint64_t runtimeId) const;
    bool IsRuntimePaused(std::uint64_t runtimeId) const;
    bool PauseRuntime(std::uint64_t runtimeId);
    bool ResumeRuntime(std::uint64_t runtimeId);
    bool StopRuntime(std::uint64_t runtimeId);
    BinderModule::TagActionResult ExecuteTagAction(std::string_view action, std::string_view param, std::uint64_t sourceRuntimeId);
    RunningBind* FindRunningBind(std::uint64_t hotkeyRuntimeId);
    const RunningBind* FindRunningBind(std::uint64_t hotkeyRuntimeId) const;
    RunningBind* FindRunningBindForHotkey(int index);
    const RunningBind* FindRunningBindForHotkey(int index) const;
    bool IsHotkeyRunning(int index) const;
    bool IsHotkeyPaused(int index) const;
    bool PauseHotkey(int index);
    bool ResumeHotkey(int index);
    bool StopHotkey(int index);
    int StopAllHotkeys();
    void StopHotkeyByRuntimeId(std::uint64_t runtimeId);
    void ExecutePendingBindTagActions(std::uint64_t sourceRuntimeId);
    void StartRunningBind(
        const HotkeyEntry& hotkey,
        std::map<std::string, std::string> inputValues,
        int startDelayMs,
        std::string activationSource,
        std::string activationText,
        std::string bindCommand);
    void PruneOutgoingGuards();
    void RegisterOutgoingGuard(std::string kind, std::string text);
    bool ConsumeOutgoingGuard(std::string_view kind, std::string_view text);
    std::string NormalizeActivationText(std::string_view text) const;
    bool MatchesActivationCommand(std::string_view input, std::string_view command) const;
    bool OnOutgoingCommand(const std::string& text);
    void OnOutgoingChat(const std::string& text);
    void OnIncomingTextMessage(const std::string& text, std::string_view source);
    void ExpireTextConfirmations();
    bool ActivatePendingTextConfirmations(UINT keyCode);
    bool MatchTextTrigger(const std::string& source, const HotkeyEntry& hotkey);
    bool OnTextTriggerEvent(const std::string& sourceText, std::string_view sourceKind);
    std::string ApplyInputValues(std::string text, const std::map<std::string, std::string>& values) const;
    std::vector<int> ResolveBindTagTargets(
        BindTagAction action,
        std::string_view rawParam,
        std::uint64_t sourceRuntimeId,
        std::string& error) const;
    BinderModule::TagActionResult ExecuteBindTagActionNow(
        BindTagAction action,
        const std::vector<int>& targetIndices,
        std::string_view actionName);
    std::string DescribeBindTagError(std::string_view actionName, std::string_view error) const;
    bool RequestBindLinesPopup(int index);
    std::string BuildInputValue(const InputDialogField& field) const;
    std::vector<int> FilterButtons(const InputDialogState& dialog, std::size_t fieldIndex) const;
    bool TryEnqueueHotkey(HotkeyEntry& hotkey, int startDelayMs, std::string_view source, const std::string& sourceText);
    bool TryEnqueueHotkey(int index, int startDelayMs, std::string_view source, const std::string& sourceText);
    void DoSend(const std::string& text, int method);
    int RemapHotkeysFolderPrefix(const std::vector<std::string>& oldPath, const std::vector<std::string>& newPath);
    int MoveHotkeysFromFolderPath(const std::vector<std::string>& fromPath, const std::vector<std::string>& toPath);
    int DeleteHotkeysFromFolderPath(const std::vector<std::string>& fromPath);
    bool IsProtectedRootFolder(const FolderNode* folder) const;
    bool CanDeleteFolder(const FolderNode* folder) const;
    bool NormalizeProtectedRootFolderName();
    void BeginCapture(CaptureTarget target);
    void DrawCapturePopup(bool insideEditorPopup);
    void DrawQuickMenu();
    void DrawSettingsSection();
    void DrawInputDialog();
    std::vector<int> FilteredBindIndices() const;
    void StartEditing(int index, bool isNew);
    std::vector<HotkeyMessage> ParseEditorMultiMessages(const std::vector<HotkeyMessage>& reference) const;
    void SyncEditorMessagesToMulti();
    void ApplyEditorMultiToDraft(bool applyBulkToAll);
    void SetEditorTab(EditorState::Tab tab);
    HotkeyEntry BuildEditorComparableDraft() const;
    bool EditorHasUnsavedChanges() const;
    std::pair<int, int> EditorNeighborIndices() const;
    void RequestEditorAction(EditorState::PendingAction action, int targetIndex = -1);
    void ExecuteEditorPendingAction();
    bool ValidateEditor(std::vector<std::string>& errors);
    void SaveEditor();
    bool CopyTextToClipboard(std::string_view text, bool showSuccessToast = true);
    int FirstCatalogIndexForKind(TagsModule::TagKind kind) const;
    void DrawEditorVariableInputsTab();
    void DrawEditorVariableCatalogTab(TagsModule::TagKind kind);
    void DrawEditorVariableKeyPickerPopup();
    void DrawEditorConditionsPopup();
    void DrawEditorQuickConditionsPopup();
    void DrawEditorVariablesPopup();
    void DrawEditorPreviewPopup();
    void DrawEditorDiscardPopup();
    void DrawEditorInline();
    void DrawEditorScenarioTab();
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

bool TryParseSampColorTag(std::string_view text, std::size_t offset, std::size_t& consumed, std::uint32_t& color) {
    consumed = 0;
    color = 0;
    if (offset >= text.size() || text[offset] != '{') {
        return false;
    }

    const std::size_t close = text.find('}', offset + 1);
    if (close == std::string_view::npos) {
        return false;
    }

    const std::size_t hexLength = close - offset - 1;
    if (hexLength != 6 && hexLength != 8) {
        return false;
    }

    std::uint32_t parsed = 0;
    for (std::size_t i = offset + 1; i < close; ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (!std::isxdigit(ch)) {
            return false;
        }
        parsed = static_cast<std::uint32_t>(parsed * 16 + static_cast<std::uint32_t>(
            std::isdigit(ch) ? (ch - '0') : (std::tolower(ch) - 'a' + 10)));
    }

    consumed = close - offset + 1;
    // SA:MP line colors use 0xRRGGBBAA. Embedded text colors are usually
    // written as {RRGGBB}, so for local chat we normalize them to full-opacity
    // RGBA before passing the value to AddChatMessage.
    color = hexLength == 6 ? ((parsed << 8) | 0xFFu) : parsed;
    return true;
}

std::pair<std::string, std::uint32_t> ParseLeadingChatColor(std::string_view text) {
    std::size_t consumed = 0;
    std::uint32_t color = 0;
    if (TryParseSampColorTag(text, 0, consumed, color)) {
        return { std::string(text.substr(consumed)), color };
    }
    return { std::string(text), 0xFFFFFFFFu };
}

std::string StripSampColorTags(std::string_view text) {
    std::string cleaned;
    cleaned.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        std::size_t consumed = 0;
        std::uint32_t color = 0;
        if (TryParseSampColorTag(text, i, consumed, color)) {
            i += consumed;
            continue;
        }
        cleaned.push_back(text[i]);
        ++i;
    }
    return cleaned;
}

std::string NormalizeDialogVisibleText(std::string_view text) {
    return Trim(StripSampColorTags(text));
}

std::string DecorateDialogLocalChatText(std::string_view text, SampApi* sampApi) {
    if (!sampApi || !sampApi->isDialogActive()) {
        return std::string(text);
    }

    std::size_t consumed = 0;
    std::uint32_t color = 0;
    if (TryParseSampColorTag(text, 0, consumed, color)) {
        return std::string(text);
    }

    const std::string caption = NormalizeDialogVisibleText(sampApi->get_dialog_caption());
    if (!caption.empty() && StartsWith(text, caption)) {
        return std::string(kDialogCaptionLocalChatColorTag) + std::string(text);
    }

    const SampApi::DialogSelectionText selection = sampApi->getDialogSelectedItemText();
    if (selection.found) {
        const std::string selectedItem = NormalizeDialogVisibleText(selection.text);
        if (!selectedItem.empty() && StartsWith(text, selectedItem)) {
            return std::string(kDialogSelectionLocalChatColorTag) + std::string(text);
        }
    }

    return std::string(text);
}

bool SetClipboardUtf8Text(std::string_view utf8Text) {
    const int sourceLength = static_cast<int>(utf8Text.size());
    const int wideLength =
        sourceLength == 0 ? 0 : MultiByteToWideChar(CP_UTF8, 0, utf8Text.data(), sourceLength, nullptr, 0);
    if (sourceLength != 0 && wideLength <= 0) {
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

    if (wideLength > 0) {
        MultiByteToWideChar(CP_UTF8, 0, utf8Text.data(), sourceLength, wideText, wideLength);
    }
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
constexpr char kEditorVariablesPopupId[] = "###binder_editor_variables";

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
    (void)moduleHandle;
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

void BinderModule::Impl::SetTagsModule(TagsModule* module) {
    tagsModule = module;
}

std::uint64_t BinderModule::Impl::AllocateHotkeyRuntimeId() {
    return nextHotkeyRuntimeId++;
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
            const bool handled = OnOutgoingCommand(ToUtf8ForDisplay(text));
            return !handled;
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

bool BinderModule::Impl::NormalizeProtectedRootFolderName() {
    FolderNode* root = EnsureRootFolder();
    if (!root) {
        return false;
    }

    const std::string desiredName = UiSettings::Instance().Text(UiText::BinderDefaultRootFolder);
    if (root->name == desiredName) {
        return false;
    }
    if (!root->name.empty() && !IsLegacyProtectedRootFolderName(root->name)) {
        return false;
    }

    const auto oldPath = BuildFolderPath(root);
    root->name = desiredName;
    RemapHotkeysFolderPrefix(oldPath, BuildFolderPath(root));
    return true;
}

HotkeyEntry BinderModule::Impl::MakeDefaultHotkey() {
    HotkeyEntry hotkey;
    hotkey.label = UiSettings::Instance().Text(UiText::BinderDefaultHotkey);
    hotkey.hotkeyMode = HotkeyMode::ModifierTrigger;
    hotkey.messages.push_back(HotkeyMessage{ "", 0, 2 });
    hotkey.conditions.assign(static_cast<std::size_t>(ConditionId::Count), false);
    hotkey.quickConditions.assign(static_cast<std::size_t>(ConditionId::Count), false);
    hotkey.repeatIntervalMs = kDefaultRepeatIntervalMs;
    hotkey.textConfirmation = TextConfirmation{};
    hotkey.runtimeId = AllocateHotkeyRuntimeId();
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

    AppConfig::Instance().QueueSectionReplace(std::string(kBinderConfigSectionName), JsonValue(std::move(root)));
}

void BinderModule::Impl::LoadConfig() {
    folders.clear();
    hotkeys.clear();
    selectedFolder = nullptr;
    nextFolderId = 1;
    nextHotkeyRuntimeId = 1;
    quickMenuHotkey.clear();
    quickMenuActivationMode = QuickMenuActivationMode::Hold;
    const jsonutil::JsonValue sharedSection = AppConfig::Instance().ReadSection(kBinderConfigSectionName);
    const JsonObject* root = sharedSection.TryObject();
    if (!root) {
        EnsureRootFolder();
        return;
    }

    quickMenuHotkey = ::hotkeys::NormalizeCombo(
        DeserializeUintArray(jsonutil::JsonArrayOrNull(root, "quick_menu_hotkey")), HotkeyMode::ModifierTrigger);
    quickMenuActivationMode =
        NormalizeQuickMenuActivationMode(jsonutil::JsonStringOr(root, "quick_menu_activation_mode", "hold"));

    if (const JsonArray* folderArray = jsonutil::JsonArrayOrNull(root, "folders")) {
        for (const JsonValue& item : *folderArray) {
            if (const JsonObject* object = item.TryObject()) {
                auto folder = DeserializeFolder(*object, nullptr);
                if (folder) {
                    folders.push_back(std::move(folder));
                }
            }
        }
    }

    if (const JsonArray* hotkeyArray = jsonutil::JsonArrayOrNull(root, "hotkeys")) {
        for (const JsonValue& item : *hotkeyArray) {
            if (const JsonObject* object = item.TryObject()) {
                hotkeys.push_back(DeserializeHotkey(*object));
            }
        }
    }

    EnsureRootFolder();
    const bool migratedProtectedRoot = NormalizeProtectedRootFolderName();
    selectedFolder = folders.front().get();
    RefreshNumbers();

    if (migratedProtectedRoot) {
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
    folder->name = SanitizeFolderName(
        jsonutil::JsonStringOr(&object, "name", UiSettings::Instance().Text(UiText::BinderDefaultFolder)));
    if (folder->name.empty()) {
        folder->name = UiSettings::Instance().Text(UiText::BinderDefaultFolder);
    }
    folder->quickMenu = jsonutil::JsonBoolOr(&object, "quick_menu", true);
    folder->quickConditions = DeserializeBoolArray(jsonutil::JsonArrayOrNull(&object, "quick_conditions"));
    if (folder->quickConditions.size() < static_cast<std::size_t>(ConditionId::Count)) {
        folder->quickConditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    }

    if (const JsonArray* children = jsonutil::JsonArrayOrNull(&object, "children")) {
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

HotkeyEntry BinderModule::Impl::DeserializeHotkey(const JsonObject& object) {
    HotkeyEntry hotkey = MakeDefaultHotkey();
    hotkey.label = jsonutil::JsonStringOr(&object, "label", hotkey.label);
    hotkey.hotkeyMode = NormalizeHotkeyMode(jsonutil::JsonStringOr(&object, "hotkey_mode", "modifier_trigger"));
    hotkey.keys = ::hotkeys::NormalizeCombo(
        DeserializeUintArray(jsonutil::JsonArrayOrNull(&object, "keys")), hotkey.hotkeyMode);
    hotkey.conditions = DeserializeBoolArray(jsonutil::JsonArrayOrNull(&object, "conditions"));
    hotkey.quickConditions = DeserializeBoolArray(jsonutil::JsonArrayOrNull(&object, "quick_conditions"));
    hotkey.conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    hotkey.quickConditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    hotkey.repeatMode = jsonutil::JsonBoolOr(&object, "repeat_mode", false);
    hotkey.repeatIntervalMs = jsonutil::JsonNumberOr<int>(&object, "repeat_interval_ms", kDefaultRepeatIntervalMs);
    hotkey.enabled = jsonutil::JsonBoolOr(&object, "enabled", true);
    hotkey.quickMenu = jsonutil::JsonBoolOr(&object, "quick_menu", false);
    hotkey.command = jsonutil::JsonStringOr(&object, "command", "");
    hotkey.commandEnabled = jsonutil::JsonBoolOr(&object, "command_enabled", false);
    hotkey.folderPath = DeserializeStringArray(jsonutil::JsonArrayOrNull(&object, "folder_path"));

    if (const JsonObject* trigger = jsonutil::JsonObjectOrNull(&object, "text_trigger")) {
        hotkey.textTrigger.text = jsonutil::JsonStringOr(trigger, "text", "");
        hotkey.textTrigger.enabled = jsonutil::JsonBoolOr(trigger, "enabled", false);
        hotkey.textTrigger.pattern = jsonutil::JsonBoolOr(trigger, "pattern", false);
    }

    if (const JsonObject* confirmation = jsonutil::JsonObjectOrNull(&object, "text_confirmation")) {
        hotkey.textConfirmation.enabled = jsonutil::JsonBoolOr(confirmation, "enabled", false);
        hotkey.textConfirmation.key =
            static_cast<UINT>(jsonutil::JsonNumberOr<double>(confirmation, "key", kDefaultConfirmKey));
        hotkey.textConfirmation.cancelKey =
            static_cast<UINT>(jsonutil::JsonNumberOr<double>(confirmation, "cancel_key", kDefaultCancelKey));
        hotkey.textConfirmation.waitForResolution =
            jsonutil::JsonBoolOr(confirmation, "wait_for_resolution", true);
    }

    hotkey.messages.clear();
    if (const JsonArray* messages = jsonutil::JsonArrayOrNull(&object, "messages")) {
        for (const JsonValue& messageValue : *messages) {
            const JsonObject* message = messageValue.TryObject();
            if (!message) {
                continue;
            }
            hotkey.messages.push_back(HotkeyMessage{
                jsonutil::JsonStringOr(message, "text", ""),
                jsonutil::JsonNumberOr<int>(message, "interval_ms", 0),
                jsonutil::JsonNumberOr<int>(message, "method", 0),
            });
        }
    }
    if (hotkey.messages.empty()) {
        hotkey.messages.push_back(HotkeyMessage{ "", 0, 0 });
    }

    hotkey.inputs.clear();
    if (const JsonArray* inputs = jsonutil::JsonArrayOrNull(&object, "inputs")) {
        for (const JsonValue& inputValue : *inputs) {
            const JsonObject* inputObject = inputValue.TryObject();
            if (!inputObject) {
                continue;
            }

            HotkeyInput input;
            input.key = NormalizeInputKey(jsonutil::JsonStringOr(inputObject, "key", ""));
            input.label = jsonutil::JsonStringOr(inputObject, "label", "");
            input.hint = jsonutil::JsonStringOr(inputObject, "hint", "");
            input.mode = NormalizeInputMode(jsonutil::JsonStringOr(inputObject, "mode", "text"));
            input.multiSelect = jsonutil::JsonBoolOr(inputObject, "multi_select", false);
            input.multiSeparator = jsonutil::JsonStringOr(inputObject, "multi_separator", ", ");
            input.cascadeParentKey = NormalizeInputKey(jsonutil::JsonStringOr(inputObject, "cascade_parent_key", ""));

            if (const JsonArray* buttons = jsonutil::JsonArrayOrNull(inputObject, "buttons")) {
                for (const JsonValue& buttonValue : *buttons) {
                    const JsonObject* buttonObject = buttonValue.TryObject();
                    if (!buttonObject) {
                        continue;
                    }
                    input.buttons.push_back(InputButton{
                        jsonutil::JsonStringOr(buttonObject, "label", ""),
                        jsonutil::JsonStringOr(buttonObject, "text", ""),
                        jsonutil::JsonStringOr(buttonObject, "hint", ""),
                        jsonutil::JsonStringOr(buttonObject, "when", ""),
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
        if (!hotkey.quickMenu) {
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

void BinderModule::Impl::ResetQuickMenuVisualState() {
    quickMenuSubmenuOpen.clear();
    quickMenuSubmenuPos.clear();
    quickMenuSubmenuNode.clear();
    quickMenuSubmenuCloseDeadline.clear();
    quickMenuSubmenuPaths.clear();
    quickMenuTabIndex = 0;
    quickMenuTabSelectRequest = -1;
}

void BinderModule::Impl::ResetInputState() {
    keyTracker.Reset();
    pressedKeys.clear();
    capture.Stop();
    hotkeys::ResetCapturePopupState(capturePopupState);
    captureTarget = CaptureTarget::None;
    captureHotkeyIndex = -1;
    capturePopupInEditor = false;
    quickMenuOpen = false;
    quickMenuToggleLatch = false;
    quickMenuReopenBlocked = false;
    ResetQuickMenuVisualState();

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
    inputDialog.reset();
    runningBinds.clear();
    toasts.clear();
    outgoingGuards.clear();
    pendingBindTagActions.clear();
    ResetInputState();
}

bool BinderModule::Impl::WantsOverlayRender() const {
    return quickMenuOpen
        || inputDialog.has_value()
        || capture.Active()
        || !toasts.empty()
        || bindLinesPopupPending
        || bindLinesTarget >= 0;
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
            if (!ApplyCapturedKeys(capturedKeys)) {
                capture.Start(capturedKeys);
            }
        } else if (canceled) {
            hotkeys::ResetCapturePopupState(capturePopupState);
            captureTarget = CaptureTarget::None;
            captureHotkeyIndex = -1;
            capturePopupInEditor = false;
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

bool BinderModule::Impl::ApplyCapturedKeys(const std::vector<UINT>& keys) {
    UiSettings& ui = UiSettings::Instance();
    switch (captureTarget) {
    case CaptureTarget::BindHotkey:
        if (editor.active) {
            editor.draft.keys = ::hotkeys::NormalizeCombo(keys, editor.draft.hotkeyMode);
        }
        break;
    case CaptureTarget::QuickMenuHotkey:
        if (std::string description; DescribeQuickMenuConflictWithMenuToggleHotkey(keys, description)) {
            PushToast(ui.Format(UiText::HotkeyConflictFormat, description.c_str()), ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2800.0);
            return false;
        }
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
        return false;
    }

    captureTarget = CaptureTarget::None;
    captureHotkeyIndex = -1;
    hotkeys::ResetCapturePopupState(capturePopupState);
    return true;
}

bool BinderModule::Impl::DescribeMainWindowHotkeyConflict(const std::vector<UINT>& keys, std::string& description) {
    EnsureInitialized();
    description.clear();

    const auto menuHotkey = ::hotkeys::NormalizeCombo(keys, HotkeyMode::ModifierTrigger);
    if (!::hotkeys::HasTriggerKey(menuHotkey)) {
        return false;
    }

    const auto quickMenuCombo = CurrentQuickMenuHotkey();
    if (::hotkeys::ContainsCombo(menuHotkey, quickMenuCombo, HotkeyMode::ModifierTrigger)) {
        description = UiSettings::Instance().Format(
            UiText::HotkeyConflictQuickMenuFormat,
            ::hotkeys::ToString(quickMenuCombo).c_str());
        return true;
    }

    for (const HotkeyEntry& hotkey : hotkeys) {
        if (!hotkey.enabled || hotkey.keys.empty()) {
            continue;
        }
        if (!::hotkeys::CombosConflict(menuHotkey, HotkeyMode::ModifierTrigger, hotkey.keys, hotkey.hotkeyMode)) {
            continue;
        }

        const std::string label = Trim(hotkey.label).empty()
            ? UiSettings::Instance().Text(UiText::BinderDefaultHotkey)
            : hotkey.label;
        description = UiSettings::Instance().Format(
            UiText::HotkeyConflictBindFormat,
            label.c_str(),
            ::hotkeys::ToString(hotkey.keys, hotkey.hotkeyMode).c_str());
        return true;
    }

    return false;
}

bool BinderModule::Impl::DescribeConflictWithMenuToggleHotkey(
    const std::vector<UINT>& keys,
    HotkeyMode mode,
    std::string& description) const {
    description.clear();

    const auto candidate = ::hotkeys::NormalizeCombo(keys, mode);
    const auto menuHotkey = ::hotkeys::NormalizeCombo(UiSettings::Instance().MenuToggleHotkey(), HotkeyMode::ModifierTrigger);
    if (candidate.empty() || !::hotkeys::HasTriggerKey(menuHotkey)) {
        return false;
    }
    if (!::hotkeys::CombosConflict(candidate, mode, menuHotkey, HotkeyMode::ModifierTrigger)) {
        return false;
    }

    description = UiSettings::Instance().Format(
        UiText::HotkeyConflictMainWindowFormat,
        ::hotkeys::ToString(menuHotkey).c_str());
    return true;
}

bool BinderModule::Impl::DescribeQuickMenuConflictWithMenuToggleHotkey(
    const std::vector<UINT>& keys,
    std::string& description) const {
    description.clear();

    std::vector<UINT> quickMenuCombo = ::hotkeys::NormalizeCombo(keys, HotkeyMode::ModifierTrigger);
    if (quickMenuCombo.empty()) {
        quickMenuCombo = { kDefaultQuickMenuFallback };
    }

    const auto menuHotkey = ::hotkeys::NormalizeCombo(UiSettings::Instance().MenuToggleHotkey(), HotkeyMode::ModifierTrigger);
    if (!::hotkeys::HasTriggerKey(menuHotkey)) {
        return false;
    }
    if (!::hotkeys::ContainsCombo(menuHotkey, quickMenuCombo, HotkeyMode::ModifierTrigger)) {
        return false;
    }

    description = UiSettings::Instance().Format(
        UiText::HotkeyConflictMainWindowFormat,
        ::hotkeys::ToString(menuHotkey).c_str());
    return true;
}

std::vector<UINT> BinderModule::Impl::CurrentQuickMenuHotkey() const {
    if (!quickMenuHotkey.empty()) {
        return ::hotkeys::NormalizeCombo(quickMenuHotkey, HotkeyMode::ModifierTrigger);
    }
    return { kDefaultQuickMenuFallback };
}

bool BinderModule::Impl::IsQuickMenuComboPressed() const {
    return ::hotkeys::ContainsCombo(pressedKeys, CurrentQuickMenuHotkey(), HotkeyMode::ModifierTrigger);
}

bool BinderModule::Impl::IsMainWindowHotkeyPressed() const {
    return ::hotkeys::ComboMatch(pressedKeys, UiSettings::Instance().MenuToggleHotkey(), HotkeyMode::ModifierTrigger);
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
    hotkeys::OpenCapturePopupCenteredOnCurrentWindow(capturePopupState);
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
        quickMenuOpen = comboHeld;
    }

    if (!quickMenuOpen) {
        ResetQuickMenuVisualState();
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

    if (quickMenuOpen || IsQuickMenuComboPressed() || inputDialog.has_value() || IsMainWindowHotkeyPressed()) {
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
                hotkey.debounceUntilMs = now + kHotkeyDebounceMs;
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
        const std::uint64_t currentRuntimeId = runningBinds[i].hotkeyRuntimeId;
        RunningBind& running = runningBinds[i];
        const int hotkeyIndex = FindHotkeyIndexByRuntimeId(currentRuntimeId);
        if (hotkeyIndex < 0) {
            runningBinds.erase(runningBinds.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }

        HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(hotkeyIndex)];
        if (running.messageIndex >= hotkey.messages.size()) {
            runningBinds.erase(runningBinds.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }

        if (running.paused) {
            ++i;
            continue;
        }

        if (now < running.nextAtMs) {
            ++i;
            continue;
        }

        const HotkeyMessage& message = hotkey.messages[running.messageIndex];
        const std::string finalText = ApplyInputValues(message.text, running.inputValues);
        if (!Trim(finalText).empty()) {
            if (tagsModule) {
                tagsModule->PushContext(TagsModule::EvaluationContext{
                    sampApi,
                    running.activationSource,
                    running.activationText,
                    running.bindCommand,
                    true,
                    running.hotkeyRuntimeId,
                });
                DoSend(finalText, message.method);
                tagsModule->PopContext();
            } else {
                DoSend(finalText, message.method);
            }
        }

        ++running.messageIndex;
        const bool hasNextMessage = running.messageIndex < hotkey.messages.size();
        if (hasNextMessage) {
            const std::optional<int> delayOverrideMs =
                tagsModule ? tagsModule->ConsumePendingBindDelayOverride(currentRuntimeId) : std::nullopt;
            running.remainingDelayMs = 0.0;
            const int nextDelayMs = delayOverrideMs.has_value() ? *delayOverrideMs : message.intervalMs;
            running.nextAtMs = now + std::max(nextDelayMs, kMinMessageIntervalMs);
        }

        ExecutePendingBindTagActions(currentRuntimeId);

        const bool currentStillRunning = i < runningBinds.size() && runningBinds[i].hotkeyRuntimeId == currentRuntimeId;
        if (!hasNextMessage) {
            if (currentStillRunning) {
                runningBinds.erase(runningBinds.begin() + static_cast<std::ptrdiff_t>(i));
            }
            continue;
        }

        if (!currentStillRunning) {
            continue;
        }

        ++i;
    }
}

int BinderModule::Impl::FindHotkeyIndexByRuntimeId(std::uint64_t runtimeId) const {
    if (runtimeId == 0) {
        return -1;
    }

    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        if (hotkeys[i].runtimeId == runtimeId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

BindTagContextDesc BinderModule::Impl::DescribeBindTagContext(std::uint64_t runtimeId) const {
    BindTagContextDesc desc;
    desc.hotkeyIndex = FindHotkeyIndexByRuntimeId(runtimeId);
    if (desc.hotkeyIndex < 0 || desc.hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        return desc;
    }

    const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(desc.hotkeyIndex)];
    desc.name = Trim(hotkey.label);
    if (desc.name.empty() && hotkey.number > 0) {
        desc.name = std::to_string(hotkey.number);
    }
    desc.folder = JoinPath(hotkey.folderPath);
    return desc;
}

std::string BinderModule::Impl::BuildThisbindTagValue(std::uint64_t runtimeId) const {
    const BindTagContextDesc desc = DescribeBindTagContext(runtimeId);
    if (desc.hotkeyIndex < 0 || desc.name.empty()) {
        return {};
    }

    std::string value = QuoteBindTagToken(desc.name);
    if (!desc.folder.empty()) {
        value += ' ';
        value += QuoteBindTagToken(desc.folder);
    }
    return value;
}

bool BinderModule::Impl::IsRuntimeActive(std::uint64_t runtimeId) const {
    if (runtimeId == 0) {
        return false;
    }

    if (const int hotkeyIndex = FindHotkeyIndexByRuntimeId(runtimeId); hotkeyIndex >= 0) {
        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(hotkeyIndex)];
        if (hotkey.awaitingInput || hotkey.waitingTextConfirmation) {
            return true;
        }
    }

    return FindRunningBind(runtimeId) != nullptr;
}

bool BinderModule::Impl::IsRuntimePaused(std::uint64_t runtimeId) const {
    if (const RunningBind* running = FindRunningBind(runtimeId)) {
        return running->paused;
    }
    return false;
}

bool BinderModule::Impl::PauseRuntime(std::uint64_t runtimeId) {
    const int hotkeyIndex = FindHotkeyIndexByRuntimeId(runtimeId);
    return hotkeyIndex >= 0 && PauseHotkey(hotkeyIndex);
}

bool BinderModule::Impl::ResumeRuntime(std::uint64_t runtimeId) {
    const int hotkeyIndex = FindHotkeyIndexByRuntimeId(runtimeId);
    return hotkeyIndex >= 0 && ResumeHotkey(hotkeyIndex);
}

bool BinderModule::Impl::StopRuntime(std::uint64_t runtimeId) {
    if (!IsRuntimeActive(runtimeId)) {
        return false;
    }

    StopHotkeyByRuntimeId(runtimeId);
    return true;
}

std::vector<int> BinderModule::Impl::ResolveBindTagTargets(
    BindTagAction action,
    std::string_view rawParam,
    std::uint64_t sourceRuntimeId,
    std::string& error) const {
    error.clear();

    const BindTagContextDesc context = DescribeBindTagContext(sourceRuntimeId);
    const std::vector<BindTagToken> tokens = TokenizeBindTagArgs(rawParam);

    BindTagSelector selector;
    selector.hasTokens = !tokens.empty();
    selector.contextHotkeyIndex = context.hotkeyIndex;

    if (tokens.empty()) {
        if (action == BindTagAction::Random) {
            selector.all = true;
        } else if (selector.contextHotkeyIndex < 0) {
            error = "param_required";
            return {};
        }
    } else {
        const BindTagToken& first = tokens.front();
        if (action == BindTagAction::Random && first.value != "*") {
            selector.all = true;
            selector.folderQuery = first.value;
            selector.folderExact = first.quoted;
        } else {
            if (first.value == "*" && !first.quoted) {
                selector.all = true;
            } else {
                int numericIndex = 0;
                if (!first.quoted && ParseInt(first.value, numericIndex)) {
                    selector.byIndex = true;
                    selector.index = numericIndex;
                } else {
                    selector.name = first.value;
                    selector.nameExact = first.quoted;
                }
            }

            if (tokens.size() >= 2 && !tokens[1].value.empty()) {
                selector.folderQuery = tokens[1].value;
                selector.folderExact = tokens[1].quoted;
            }
        }
    }

    if (!selector.hasTokens && action != BindTagAction::Random && selector.contextHotkeyIndex >= 0) {
        return { selector.contextHotkeyIndex };
    }

    std::vector<std::string> scopePath;
    if (!selector.folderQuery.empty()) {
        std::vector<std::vector<std::string>> folderPaths;
        CollectFolderPaths(folders, folderPaths);
        if (folderPaths.empty()) {
            error = "no_folders";
            return {};
        }

        for (const auto& path : folderPaths) {
            const std::string fullPath = JoinPath(path);
            const std::string folderName = path.empty() ? std::string() : path.back();
            const bool matched = selector.folderExact
                ? (EqualNoCase(folderName, selector.folderQuery) || EqualNoCase(fullPath, selector.folderQuery))
                : (ContainsNoCase(folderName, selector.folderQuery) || ContainsNoCase(fullPath, selector.folderQuery));
            if (matched) {
                scopePath = path;
                break;
            }
        }

        if (scopePath.empty()) {
            error = "folder_not_found";
            return {};
        }
    } else if (action == BindTagAction::Random && !selector.hasTokens && selector.contextHotkeyIndex >= 0) {
        scopePath = hotkeys[static_cast<std::size_t>(selector.contextHotkeyIndex)].folderPath;
    } else {
        if (folders.empty() || !folders.front()) {
            error = "no_folders";
            return {};
        }
        scopePath = BuildFolderPath(folders.front().get());
    }

    std::vector<int> candidates;
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        if (hotkeys[i].folderPath == scopePath) {
            candidates.push_back(static_cast<int>(i));
        }
    }

    if (selector.all) {
        return candidates;
    }

    const auto hotkeyDisplayName = [this](int index) {
        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
        std::string name = Trim(hotkey.label);
        if (name.empty() && hotkey.number > 0) {
            name = std::to_string(hotkey.number);
        }
        return name;
    };

    if (selector.byIndex) {
        if (selector.index < 1) {
            return {};
        }
        for (const int index : candidates) {
            if (hotkeys[static_cast<std::size_t>(index)].number == selector.index) {
                return { index };
            }
        }
        return {};
    }

    if (!selector.name.empty()) {
        const std::string query = ToLower(selector.name);
        int partialMatch = -1;
        for (const int index : candidates) {
            const std::string displayName = ToLower(hotkeyDisplayName(index));
            if (selector.nameExact) {
                if (displayName == query) {
                    return { index };
                }
            } else {
                if (displayName == query) {
                    return { index };
                }
                if (partialMatch < 0 && displayName.find(query) != std::string::npos) {
                    partialMatch = index;
                }
            }
        }
        if (partialMatch >= 0) {
            return { partialMatch };
        }
        return {};
    }

    if (selector.contextHotkeyIndex >= 0) {
        for (const int index : candidates) {
            if (index == selector.contextHotkeyIndex) {
                return { index };
            }
        }
    }

    if (!candidates.empty()) {
        return { candidates.front() };
    }

    return {};
}

std::string BinderModule::Impl::DescribeBindTagError(std::string_view actionName, std::string_view error) const {
    UiSettings& ui = UiSettings::Instance();
    const std::string token = "[" + std::string(actionName) + "]";

    if (error == "param_required") {
        return ui.Format(UiText::ToastBindTagTargetRequired, token.c_str());
    }
    if (error == "no_folders") {
        return ui.Text(UiText::ToastBindTagNoFolders);
    }
    if (error == "folder_not_found") {
        return ui.Format(UiText::ToastBindTagFolderNotFound, token.c_str());
    }
    if (error == "bind_not_found") {
        return ui.Format(UiText::ToastBindTagBindNotFound, token.c_str());
    }
    if (error == "bind_not_started") {
        return ui.Format(UiText::ToastBindTagNotStarted, token.c_str());
    }
    if (error == "bind_not_running") {
        return ui.Format(UiText::ToastBindTagNotRunning, token.c_str());
    }
    if (error == "bind_not_paused") {
        return ui.Format(UiText::ToastBindTagNotPaused, token.c_str());
    }
    if (error == "bind_no_changes") {
        return ui.Format(UiText::ToastBindTagNoChanges, token.c_str());
    }
    if (error == "bind_popup_unavailable") {
        return ui.Format(UiText::ToastBindTagPopupUnavailable, token.c_str());
    }
    if (error == "stopall_empty") {
        return ui.Text(UiText::ToastBindTagStopAllEmpty);
    }
    if (error == "unknown_action") {
        return ui.Format(UiText::ToastBindTagUnknownAction, std::string(actionName).c_str());
    }
    return ui.Format(UiText::ToastBindTagBindNotFound, token.c_str());
}

bool BinderModule::Impl::RequestBindLinesPopup(int index) {
    if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
        return false;
    }

    bindLinesTarget = index;
    bindLinesPopupPending = true;
    return true;
}

BinderModule::TagActionResult BinderModule::Impl::ExecuteBindTagActionNow(
    BindTagAction action,
    const std::vector<int>& targetIndices,
    std::string_view actionName) {
    (void)actionName;

    BinderModule::TagActionResult result;
    result.affected = 0;

    switch (action) {
    case BindTagAction::StopAll: {
        const int stopped = StopAllHotkeys();
        if (stopped > 0) {
            result.success = true;
            result.affected = stopped;
        } else {
            result.error = "stopall_empty";
        }
        return result;
    }
    case BindTagAction::Ended: {
        bool ended = true;
        for (const int index : targetIndices) {
            if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
                ended = false;
                break;
            }

            const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
            if (hotkey.awaitingInput || hotkey.waitingTextConfirmation || FindRunningBindForHotkey(index) != nullptr) {
                ended = false;
                break;
            }
        }

        result.success = true;
        result.affected = static_cast<int>(targetIndices.size());
        result.value = ended ? "1" : "0";
        return result;
    }
    case BindTagAction::Random: {
        std::vector<int> pool;
        pool.reserve(targetIndices.size());
        for (const int index : targetIndices) {
            if (index >= 0 && index < static_cast<int>(hotkeys.size()) && hotkeys[static_cast<std::size_t>(index)].enabled) {
                pool.push_back(index);
            }
        }

        if (pool.empty()) {
            result.error = "bind_not_started";
            return result;
        }

        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<std::size_t> distribution(0, pool.size() - 1);
        const int chosen = pool[distribution(rng)];
        if (TryEnqueueHotkey(chosen, 0, "bind_tag", std::string(actionName))) {
            result.success = true;
            result.affected = 1;
        } else {
            result.error = "bind_not_started";
        }
        return result;
    }
    case BindTagAction::Popup: {
        if (!targetIndices.empty() && RequestBindLinesPopup(targetIndices.front())) {
            result.success = true;
            result.affected = 1;
        } else {
            result.error = "bind_popup_unavailable";
        }
        return result;
    }
    case BindTagAction::Start: {
        int changed = 0;
        for (const int index : targetIndices) {
            if (TryEnqueueHotkey(index, 0, "bind_tag", std::string(actionName))) {
                ++changed;
            }
        }
        result.success = changed > 0;
        result.affected = changed;
        if (!result.success) {
            result.error = "bind_not_started";
        }
        return result;
    }
    case BindTagAction::Stop: {
        int changed = 0;
        for (const int index : targetIndices) {
            if (StopHotkey(index)) {
                ++changed;
            }
        }
        result.success = changed > 0;
        result.affected = changed;
        if (!result.success) {
            result.error = "bind_not_running";
        }
        return result;
    }
    case BindTagAction::Pause: {
        int changed = 0;
        for (const int index : targetIndices) {
            if (PauseHotkey(index)) {
                ++changed;
            }
        }
        result.success = changed > 0;
        result.affected = changed;
        if (!result.success) {
            result.error = "bind_not_running";
        }
        return result;
    }
    case BindTagAction::Unpause: {
        int changed = 0;
        for (const int index : targetIndices) {
            if (ResumeHotkey(index)) {
                ++changed;
            }
        }
        result.success = changed > 0;
        result.affected = changed;
        if (!result.success) {
            result.error = "bind_not_paused";
        }
        return result;
    }
    case BindTagAction::Disable: {
        int changed = 0;
        for (const int index : targetIndices) {
            if (index >= 0 && index < static_cast<int>(hotkeys.size()) && hotkeys[static_cast<std::size_t>(index)].enabled) {
                hotkeys[static_cast<std::size_t>(index)].enabled = false;
                ++changed;
            }
        }
        if (changed > 0) {
            SaveConfig();
            result.success = true;
            result.affected = changed;
        } else {
            result.error = "bind_no_changes";
        }
        return result;
    }
    case BindTagAction::Enable: {
        int changed = 0;
        for (const int index : targetIndices) {
            if (index >= 0 && index < static_cast<int>(hotkeys.size()) && !hotkeys[static_cast<std::size_t>(index)].enabled) {
                hotkeys[static_cast<std::size_t>(index)].enabled = true;
                ++changed;
            }
        }
        if (changed > 0) {
            SaveConfig();
            result.success = true;
            result.affected = changed;
        } else {
            result.error = "bind_no_changes";
        }
        return result;
    }
    case BindTagAction::FastMenu:
    case BindTagAction::UnfastMenu: {
        const bool desired = action == BindTagAction::FastMenu;
        int changed = 0;
        for (const int index : targetIndices) {
            if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
                continue;
            }
            HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
            if (hotkey.quickMenu != desired) {
                hotkey.quickMenu = desired;
                ++changed;
            }
        }
        if (changed > 0) {
            SaveConfig();
            result.success = true;
            result.affected = changed;
        } else {
            result.error = "bind_no_changes";
        }
        return result;
    }
    case BindTagAction::Unknown:
    default:
        result.error = "unknown_action";
        return result;
    }
}

BinderModule::TagActionResult BinderModule::Impl::ExecuteTagAction(
    std::string_view actionName,
    std::string_view param,
    std::uint64_t sourceRuntimeId) {
    BinderModule::TagActionResult result;
    const BindTagAction action = ParseBindTagActionName(actionName);
    if (action == BindTagAction::Unknown) {
        result.error = "unknown_action";
        PushToast(DescribeBindTagError(actionName, result.error), ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2600.0);
        return result;
    }

    if (action == BindTagAction::StopAll) {
        if (sourceRuntimeId != 0) {
            pendingBindTagActions.push_back(PendingBindTagAction{
                action,
                sourceRuntimeId,
                std::string(actionName),
                {},
            });
            result.success = true;
            result.affected = 1;
            return result;
        }

        result = ExecuteBindTagActionNow(action, {}, actionName);
        if (!result.success && !result.error.empty()) {
            PushToast(DescribeBindTagError(actionName, result.error), ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2600.0);
        }
        return result;
    }

    std::string error;
    std::vector<int> targetIndices = ResolveBindTagTargets(action, param, sourceRuntimeId, error);
    if (error.empty() && targetIndices.empty()) {
        error = "bind_not_found";
    }

    if (!error.empty()) {
        result.error = error;
        if (action == BindTagAction::Ended) {
            result.value = "0";
        }
        PushToast(DescribeBindTagError(actionName, error), ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2600.0);
        return result;
    }

    if (action != BindTagAction::Ended && sourceRuntimeId != 0) {
        pendingBindTagActions.push_back(PendingBindTagAction{
            action,
            sourceRuntimeId,
            std::string(actionName),
            std::move(targetIndices),
        });
        result.success = true;
        return result;
    }

    result = ExecuteBindTagActionNow(action, targetIndices, actionName);
    if (!result.success && !result.error.empty()) {
        if (action == BindTagAction::Ended) {
            result.value = "0";
        }
        PushToast(DescribeBindTagError(actionName, result.error), ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2600.0);
    }
    return result;
}

RunningBind* BinderModule::Impl::FindRunningBind(std::uint64_t hotkeyRuntimeId) {
    if (hotkeyRuntimeId == 0) {
        return nullptr;
    }

    const auto it = std::find_if(runningBinds.begin(), runningBinds.end(), [&](const RunningBind& running) {
        return running.hotkeyRuntimeId == hotkeyRuntimeId;
    });
    return it == runningBinds.end() ? nullptr : &(*it);
}

const RunningBind* BinderModule::Impl::FindRunningBind(std::uint64_t hotkeyRuntimeId) const {
    if (hotkeyRuntimeId == 0) {
        return nullptr;
    }

    const auto it = std::find_if(runningBinds.begin(), runningBinds.end(), [&](const RunningBind& running) {
        return running.hotkeyRuntimeId == hotkeyRuntimeId;
    });
    return it == runningBinds.end() ? nullptr : &(*it);
}

RunningBind* BinderModule::Impl::FindRunningBindForHotkey(int index) {
    if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
        return nullptr;
    }
    return FindRunningBind(hotkeys[static_cast<std::size_t>(index)].runtimeId);
}

const RunningBind* BinderModule::Impl::FindRunningBindForHotkey(int index) const {
    if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
        return nullptr;
    }
    return FindRunningBind(hotkeys[static_cast<std::size_t>(index)].runtimeId);
}

bool BinderModule::Impl::IsHotkeyRunning(int index) const {
    return FindRunningBindForHotkey(index) != nullptr;
}

bool BinderModule::Impl::IsHotkeyPaused(int index) const {
    if (const RunningBind* running = FindRunningBindForHotkey(index)) {
        return running->paused;
    }
    return false;
}

bool BinderModule::Impl::PauseHotkey(int index) {
    RunningBind* running = FindRunningBindForHotkey(index);
    if (!running || running->paused) {
        return false;
    }

    const double now = static_cast<double>(GetTickCount64());
    running->remainingDelayMs = std::max(0.0, running->nextAtMs - now);
    running->paused = true;
    return true;
}

bool BinderModule::Impl::ResumeHotkey(int index) {
    RunningBind* running = FindRunningBindForHotkey(index);
    if (!running || !running->paused) {
        return false;
    }

    const double now = static_cast<double>(GetTickCount64());
    running->paused = false;
    running->nextAtMs = now + std::max(running->remainingDelayMs, 0.0);
    running->remainingDelayMs = 0.0;
    return true;
}

int BinderModule::Impl::StopAllHotkeys() {
    std::vector<std::uint64_t> runtimeIds;
    runtimeIds.reserve(hotkeys.size());
    for (const HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.awaitingInput || hotkey.waitingTextConfirmation || FindRunningBind(hotkey.runtimeId) != nullptr) {
            runtimeIds.push_back(hotkey.runtimeId);
        }
    }

    std::sort(runtimeIds.begin(), runtimeIds.end());
    runtimeIds.erase(std::unique(runtimeIds.begin(), runtimeIds.end()), runtimeIds.end());
    for (const std::uint64_t runtimeId : runtimeIds) {
        StopHotkeyByRuntimeId(runtimeId);
    }

    return static_cast<int>(runtimeIds.size());
}

void BinderModule::Impl::StopHotkeyByRuntimeId(std::uint64_t runtimeId) {
    if (runtimeId == 0) {
        return;
    }

    if (inputDialog
        && inputDialog->hotkeyIndex >= 0
        && inputDialog->hotkeyIndex < static_cast<int>(hotkeys.size())
        && hotkeys[static_cast<std::size_t>(inputDialog->hotkeyIndex)].runtimeId == runtimeId) {
        hotkeys[static_cast<std::size_t>(inputDialog->hotkeyIndex)].awaitingInput = false;
        inputDialog.reset();
    }

    if (const int hotkeyIndex = FindHotkeyIndexByRuntimeId(runtimeId); hotkeyIndex >= 0) {
        HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(hotkeyIndex)];
        hotkey.awaitingInput = false;
        hotkey.waitingTextConfirmation = false;
        hotkey.textConfirmationDeadlineMs = 0.0;
        hotkey.pendingTriggerText.clear();
        hotkey.pendingTriggerSource.clear();
    }

    runningBinds.erase(
        std::remove_if(runningBinds.begin(), runningBinds.end(), [&](const RunningBind& running) {
            return running.hotkeyRuntimeId == runtimeId;
        }),
        runningBinds.end());

    pendingBindTagActions.erase(
        std::remove_if(pendingBindTagActions.begin(), pendingBindTagActions.end(), [&](const PendingBindTagAction& pending) {
            return pending.sourceRuntimeId == runtimeId;
        }),
        pendingBindTagActions.end());
}

bool BinderModule::Impl::StopHotkey(int index) {
    if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
        return false;
    }

    HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
    const bool hadWork = hotkey.awaitingInput || hotkey.waitingTextConfirmation || FindRunningBind(hotkey.runtimeId) != nullptr;
    StopHotkeyByRuntimeId(hotkey.runtimeId);
    return hadWork;
}

void BinderModule::Impl::ExecutePendingBindTagActions(std::uint64_t sourceRuntimeId) {
    std::vector<PendingBindTagAction> localActions;
    for (std::size_t i = 0; i < pendingBindTagActions.size();) {
        if (pendingBindTagActions[i].sourceRuntimeId != sourceRuntimeId) {
            ++i;
            continue;
        }

        localActions.push_back(std::move(pendingBindTagActions[i]));
        pendingBindTagActions.erase(pendingBindTagActions.begin() + static_cast<std::ptrdiff_t>(i));
    }

    for (PendingBindTagAction& pending : localActions) {
        BinderModule::TagActionResult result =
            ExecuteBindTagActionNow(pending.action, pending.targetIndices, pending.actionName);
        if (!result.success && !result.error.empty()) {
            PushToast(
                DescribeBindTagError(pending.actionName, result.error),
                ImVec4(0.55f, 0.20f, 0.20f, 0.95f),
                2600.0);
        }
    }
}

void BinderModule::Impl::StartRunningBind(
    const HotkeyEntry& hotkey,
    std::map<std::string, std::string> inputValues,
    int startDelayMs,
    std::string activationSource,
    std::string activationText,
    std::string bindCommand) {
    if (hotkey.runtimeId == 0 || FindRunningBind(hotkey.runtimeId) != nullptr) {
        return;
    }

    runningBinds.push_back(RunningBind{
        hotkey.runtimeId,
        std::move(inputValues),
        0,
        static_cast<double>(GetTickCount64() + std::max(startDelayMs, 0)),
        std::move(activationSource),
        std::move(activationText),
        std::move(bindCommand),
        false,
        0.0,
    });
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
    if (kind == "command" && !text.empty() && text.front() != '/') {
        text.insert(text.begin(), '/');
    }
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
    std::string normalized = NormalizeActivationText(text);
    if (kind == "command" && !normalized.empty() && normalized.front() != '/') {
        normalized.insert(normalized.begin(), '/');
    }
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
    const auto normalizeCommand = [&](std::string_view value) {
        std::string normalized = NormalizeActivationText(value);
        if (!normalized.empty() && normalized.front() != '/') {
            normalized.insert(normalized.begin(), '/');
        }
        return normalized;
    };

    const std::string normalizedInput = normalizeCommand(input);
    const std::string normalizedCommand = normalizeCommand(command);
    if (normalizedInput.empty() || normalizedCommand.empty()) {
        return false;
    }

    return StartsWith(normalizedInput, normalizedCommand)
        && (normalizedInput.size() == normalizedCommand.size()
            || std::isspace(static_cast<unsigned char>(normalizedInput[normalizedCommand.size()])) != 0);
}

bool BinderModule::Impl::OnOutgoingCommand(const std::string& text) {
    std::string normalized = NormalizeActivationText(text);
    if (!normalized.empty() && normalized.front() != '/') {
        normalized.insert(normalized.begin(), '/');
    }
    if (normalized.empty()) {
        return false;
    }

    const bool consumedGuard = ConsumeOutgoingGuard("command", normalized);
    bool handled = false;
    if (!consumedGuard) {
        handled = OnTextTriggerEvent(normalized, "outgoing_command");
    }

    const double now = static_cast<double>(GetTickCount64());
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        HotkeyEntry& hotkey = hotkeys[i];
        if (!hotkey.enabled || !hotkey.commandEnabled || hotkey.command.empty() || hotkey.awaitingInput) {
            continue;
        }
        if (!MatchesActivationCommand(normalized, hotkey.command)) {
            continue;
        }
        if (IsHotkeyRunning(static_cast<int>(i))) {
            continue;
        }
        if (now < hotkey.debounceUntilMs) {
            continue;
        }
        hotkey.debounceUntilMs = now + kHotkeyDebounceMs;
        handled = true;
        TryEnqueueHotkey(static_cast<int>(i), 0, "command", normalized);
    }
    return handled;
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

bool BinderModule::Impl::OnTextTriggerEvent(const std::string& sourceText, std::string_view sourceKind) {
    const double now = static_cast<double>(GetTickCount64());
    bool handled = false;
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        HotkeyEntry& hotkey = hotkeys[i];
        if (!hotkey.enabled || !MatchTextTrigger(sourceText, hotkey)) {
            continue;
        }
        if (now < hotkey.debounceUntilMs) {
            continue;
        }

        hotkey.debounceUntilMs = now + kHotkeyDebounceMs;
        handled = true;
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
    return handled;
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
        return button.text;
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
    if (!hotkey.enabled || hotkey.awaitingInput || hotkey.waitingTextConfirmation || IsHotkeyRunning(index)) {
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
        dialog.activationSource = std::string(source);
        dialog.activationText = sourceText;
        dialog.bindCommand = hotkey.command;
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

    StartRunningBind(hotkey, {}, startDelayMs, std::string(source), sourceText, hotkey.command);
    hotkey.awaitingInput = false;
    return true;
}

void BinderModule::Impl::DoSend(const std::string& text, int method) {
    const auto expandWithTags = [&](std::string_view source) {
        return tagsModule ? tagsModule->ExpandText(source) : std::string(source);
    };

    switch (method) {
    case 0: {
        const std::string expandedText = expandWithTags(text);
        const std::string decoratedText = DecorateDialogLocalChatText(expandedText, sampApi);
        const auto [messageText, color] = ParseLeadingChatColor(decoratedText);
        if (!sampApi || !sampApi->memoryAddMessageSamp(messageText, color, true)) {
            PushToast(UiSettings::Instance().Text(UiText::ToastSendLocalFailed), ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2500.0);
        }
        break;
    }
    case 1:
    {
        const std::string expandedText = expandWithTags(text);
        RegisterOutgoingGuard(!expandedText.empty() && expandedText.front() == '/' ? "command" : "chat", expandedText);
        RegisterOutgoingGuard("echo", NormalizeTriggerText(expandedText));
        if (!sampApi || !sampApi->process_chat_input(expandedText, true)) {
            PushToast(UiSettings::Instance().Text(UiText::ToastSendSampFailed), ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2500.0);
        }
        break;
    }
    case 2:
    {
        const std::string expandedText = expandWithTags(text);
        RegisterOutgoingGuard(!expandedText.empty() && expandedText.front() == '/' ? "command" : "chat", expandedText);
        RegisterOutgoingGuard("echo", NormalizeTriggerText(expandedText));
        if (!sampApi || !sampApi->send_chat(expandedText, true)) {
            PushToast(UiSettings::Instance().Text(UiText::ToastSendSampFailed), ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2500.0);
        }
        break;
    }
    case 3:
        static_cast<void>(expandWithTags(text));
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
        if (!SetClipboardUtf8Text(expandWithTags(text))) {
            PushToast(UiSettings::Instance().Text(UiText::ToastClipboardFailed), ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2500.0);
        }
        break;
    case 8:
        debuglog::Write("Binder log: %s", expandWithTags(text).c_str());
        break;
    case 9:
        PushToast(expandWithTags(text), ImVec4(0.20f, 0.35f, 0.18f, 0.95f), 2200.0);
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

int BinderModule::Impl::DeleteHotkeysFromFolderPath(const std::vector<std::string>& fromPath) {
    int removed = 0;
    std::vector<std::uint64_t> runtimeIdsToStop;
    runtimeIdsToStop.reserve(hotkeys.size());
    for (const HotkeyEntry& hotkey : hotkeys) {
        if (PathStartsWith(hotkey.folderPath, fromPath)) {
            runtimeIdsToStop.push_back(hotkey.runtimeId);
        }
    }
    for (const std::uint64_t runtimeId : runtimeIdsToStop) {
        StopHotkeyByRuntimeId(runtimeId);
    }

    hotkeys.erase(
        std::remove_if(hotkeys.begin(), hotkeys.end(), [&](const HotkeyEntry& hotkey) {
            if (!PathStartsWith(hotkey.folderPath, fromPath)) {
                return false;
            }
            ++removed;
            return true;
        }),
        hotkeys.end());

    if (removed > 0) {
        RefreshNumbers();
        selectedBindIndex = -1;
        bindDeleteTarget = -1;
        moveBindTarget = -1;
        bindLinesTarget = -1;
    }
    return removed;
}

bool BinderModule::Impl::IsProtectedRootFolder(const FolderNode* folder) const {
    return folder != nullptr && folder->parent == nullptr && !folders.empty() && folders.front().get() == folder;
}

bool BinderModule::Impl::CanDeleteFolder(const FolderNode* folder) const {
    if (!folder) {
        return false;
    }
    if (IsProtectedRootFolder(folder)) {
        return false;
    }
    if (folder->parent == nullptr && folders.size() <= 1) {
        return false;
    }
    return true;
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
    editor.selectedInputButtonIndex = -1;
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

    editor.inputButtonsBulkDrafts.reserve(editor.draft.inputs.size());
    for (const HotkeyInput& input : editor.draft.inputs) {
        editor.inputButtonsBulkDrafts.push_back(SerializeButtonsText(input.buttons));
    }
    editor.inputButtonsBulkPreviews.assign(editor.draft.inputs.size(), ButtonsBulkPreviewState{});

    if (!editor.draft.inputs.empty()) {
        editor.selectedInputIndex = 0;
        editor.selectedButtonsText = editor.inputButtonsBulkDrafts[0];
        editor.selectedInputButtonIndex = editor.draft.inputs[0].buttons.empty() ? -1 : 0;
    }

    if (!editor.draft.messages.empty()) {
        editor.bulkMethod = editor.draft.messages.front().method;
        editor.bulkIntervalMs = std::max(editor.draft.messages.front().intervalMs, 0);
    }

    SyncEditorMessagesToMulti();
    editor.activeTab = EditorState::Tab::Scenario;
    editor.tabSelectionPending = true;
    editor.baseline = editor.draft;
    editor.focusNamePending = true;
    editor.pendingAction = EditorState::PendingAction::None;
    editor.pendingTargetIndex = -1;
    selectedBindIndex = index;
}

std::vector<HotkeyMessage> BinderModule::Impl::ParseEditorMultiMessages(const std::vector<HotkeyMessage>& reference) const {
    std::vector<HotkeyMessage> messages;
    std::istringstream stream(NormalizeLineEndings(editor.multiText));
    std::string line;
    std::size_t lineIndex = 0;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }

        HotkeyMessage message;
        message.text = line;
        message.intervalMs = std::max(editor.bulkIntervalMs, 0);
        message.method = editor.bulkMethod;
        if (lineIndex < reference.size() && reference[lineIndex].text == line) {
            message.intervalMs = std::max(reference[lineIndex].intervalMs, 0);
            message.method = reference[lineIndex].method;
        }
        messages.push_back(std::move(message));
        ++lineIndex;
    }

    const bool allReferenceEmpty = !reference.empty() && std::all_of(reference.begin(), reference.end(), [](const HotkeyMessage& item) {
        return Trim(item.text).empty();
    });
    if (messages.empty() && allReferenceEmpty) {
        return reference;
    }

    return messages;
}

void BinderModule::Impl::SyncEditorMessagesToMulti() {
    std::ostringstream stream;
    for (std::size_t i = 0; i < editor.draft.messages.size(); ++i) {
        if (i != 0) {
            stream << '\n';
        }
        stream << editor.draft.messages[i].text;
    }
    editor.multiText = stream.str();
    if (!editor.draft.messages.empty()) {
        editor.bulkMethod = editor.draft.messages.front().method;
        editor.bulkIntervalMs = std::max(editor.draft.messages.front().intervalMs, 0);
    }
}

void BinderModule::Impl::ApplyEditorMultiToDraft(bool applyBulkToAll) {
    editor.draft.messages = ParseEditorMultiMessages(editor.draft.messages);
    if (applyBulkToAll) {
        for (HotkeyMessage& message : editor.draft.messages) {
            message.intervalMs = std::max(editor.bulkIntervalMs, 0);
            message.method = editor.bulkMethod;
        }
    }
}

void BinderModule::Impl::SetEditorTab(EditorState::Tab tab) {
    if (editor.activeTab == tab) {
        return;
    }

    if (tab == EditorState::Tab::MultiInput) {
        SyncEditorMessagesToMulti();
    }

    editor.activeTab = tab;
}

HotkeyEntry BinderModule::Impl::BuildEditorComparableDraft() const {
    HotkeyEntry comparable = editor.draft;
    comparable.conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    comparable.quickConditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    comparable.repeatIntervalMs = std::max(comparable.repeatIntervalMs, 0);
    return comparable;
}

bool BinderModule::Impl::EditorHasUnsavedChanges() const {
    if (!editor.active) {
        return false;
    }

    std::string currentSerialized;
    std::string baselineSerialized;
    jsonutil::WriteJson(SerializeHotkey(BuildEditorComparableDraft()), currentSerialized);
    jsonutil::WriteJson(SerializeHotkey(editor.baseline), baselineSerialized);
    return currentSerialized != baselineSerialized;
}

std::pair<int, int> BinderModule::Impl::EditorNeighborIndices() const {
    if (editor.hotkeyIndex < 0 || editor.hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        return { -1, -1 };
    }

    const std::vector<std::string>& folderPath = hotkeys[static_cast<std::size_t>(editor.hotkeyIndex)].folderPath;
    int previous = -1;
    int next = -1;
    int last = -1;
    for (int index = 0; index < static_cast<int>(hotkeys.size()); ++index) {
        if (hotkeys[static_cast<std::size_t>(index)].folderPath != folderPath) {
            continue;
        }
        if (index == editor.hotkeyIndex) {
            previous = last;
            continue;
        }
        if (previous != -1 || index > editor.hotkeyIndex) {
            if (index > editor.hotkeyIndex) {
                next = index;
                break;
            }
        }
        last = index;
    }
    if (previous == -1) {
        last = -1;
        for (int index = 0; index < editor.hotkeyIndex; ++index) {
            if (hotkeys[static_cast<std::size_t>(index)].folderPath == folderPath) {
                last = index;
            }
        }
        previous = last;
    }
    return { previous, next };
}

void BinderModule::Impl::RequestEditorAction(EditorState::PendingAction action, int targetIndex) {
    if (!editor.active) {
        return;
    }

    if (action == EditorState::PendingAction::Navigate
        && (targetIndex < 0 || targetIndex >= static_cast<int>(hotkeys.size()))) {
        return;
    }

    editor.pendingAction = action;
    editor.pendingTargetIndex = targetIndex;
    if (EditorHasUnsavedChanges()) {
        editor.discardPopupPending = true;
        return;
    }

    ExecuteEditorPendingAction();
}

void BinderModule::Impl::ExecuteEditorPendingAction() {
    const EditorState::PendingAction action = editor.pendingAction;
    const int targetIndex = editor.pendingTargetIndex;

    editor.pendingAction = EditorState::PendingAction::None;
    editor.pendingTargetIndex = -1;
    editor.discardPopupPending = false;

    if (capturePopupInEditor) {
        capture.Stop();
        hotkeys::ResetCapturePopupState(capturePopupState);
        captureTarget = CaptureTarget::None;
        captureHotkeyIndex = -1;
        capturePopupInEditor = false;
    }

    switch (action) {
    case EditorState::PendingAction::Close:
        editor = {};
        return;
    case EditorState::PendingAction::Navigate:
        if (targetIndex >= 0 && targetIndex < static_cast<int>(hotkeys.size())) {
            StartEditing(targetIndex, false);
        }
        return;
    case EditorState::PendingAction::None:
        return;
    }
}

bool BinderModule::Impl::ValidateEditor(std::vector<std::string>& errors) {
    UiSettings& ui = UiSettings::Instance();
    const HotkeyEntry current = BuildEditorComparableDraft();
    const std::string label = Trim(current.label);
    const std::string triggerText = Trim(current.textTrigger.text);
    const std::string commandText = Trim(current.command);
    if (label.empty()) {
        errors.push_back(ui.Text(UiText::ValidationBindNameRequired));
    }

    if (current.folderPath.empty() || !FindFolderByPath(folders, current.folderPath)) {
        errors.push_back(ui.Text(UiText::ValidationExistingFolderRequired));
    }

    if (current.repeatMode && current.repeatIntervalMs < kMinMessageIntervalMs) {
        errors.push_back(ui.Text(UiText::ValidationRepeatInterval));
    }

    if (current.textTrigger.enabled && triggerText.empty()) {
        errors.push_back(ui.Text(UiText::ValidationTriggerTextRequired));
    }

    if (current.commandEnabled && commandText.empty()) {
        errors.push_back(ui.Text(UiText::ValidationCommandRequired));
    }

    if (current.textConfirmation.enabled && current.textConfirmation.key == current.textConfirmation.cancelKey) {
        errors.push_back(ui.Text(UiText::ValidationConfirmCancelKeysDifferent));
    }

    if (current.enabled && !current.keys.empty()) {
        if (std::string description; DescribeConflictWithMenuToggleHotkey(current.keys, current.hotkeyMode, description)) {
            errors.push_back(ui.Format(UiText::HotkeyConflictFormat, description.c_str()));
        }
    }

    std::set<std::string> inputKeys;
    for (const HotkeyInput& input : current.inputs) {
        const std::string key = NormalizeInputKey(input.key);
        if (key.empty()) {
            errors.push_back(ui.Text(UiText::ValidationInputKeyRequired));
            continue;
        }
        if (!inputKeys.insert(key).second) {
            errors.push_back(ui.Text(UiText::ValidationInputKeyUnique));
        }
        if (InputModeUsesButtons(input.mode)) {
            if (input.buttons.empty()) {
                errors.push_back(ui.Text(UiText::ValidationButtonsRequired));
                continue;
            }

            const bool hasValueText = std::any_of(input.buttons.begin(), input.buttons.end(), [](const InputButton& button) {
                return !Trim(button.text).empty();
            });
            if (!hasValueText) {
                errors.push_back(ui.Text(UiText::ValidationButtonsTextRequired));
            }
        }
    }

    if (current.textTrigger.enabled && current.textTrigger.pattern && !triggerText.empty()) {
        try {
            std::regex test(triggerText);
            (void)test;
        } catch (const std::exception& ex) {
            errors.push_back(ui.Format(UiText::ValidationInvalidRegex, ex.what()));
        }
    }

    return errors.empty();
}

void BinderModule::Impl::SaveEditor() {
    HotkeyEntry saved = BuildEditorComparableDraft();
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
    if (!editor.isNew && editor.hotkeyIndex >= 0 && editor.hotkeyIndex < static_cast<int>(hotkeys.size())) {
        saved.runtimeId = hotkeys[static_cast<std::size_t>(editor.hotkeyIndex)].runtimeId;
    }

    if (editor.isNew || editor.hotkeyIndex < 0 || editor.hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        hotkeys.push_back(std::move(saved));
        selectedBindIndex = static_cast<int>(hotkeys.size() - 1);
    } else {
        hotkeys[editor.hotkeyIndex] = std::move(saved);
        selectedBindIndex = editor.hotkeyIndex;
    }

    RefreshNumbers();
    SaveConfig();
    editor = {};
}

bool BinderModule::Impl::CopyTextToClipboard(std::string_view text, bool showSuccessToast) {
    if (!SetClipboardUtf8Text(text)) {
        PushToast(UiSettings::Instance().Text(UiText::ToastClipboardFailed), ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2500.0);
        return false;
    }

    if (showSuccessToast) {
        PushToast(UiSettings::Instance().Text(UiText::ToastClipboardCopied), ImVec4(0.20f, 0.35f, 0.18f, 0.95f), 1400.0);
    }
    return true;
}

int BinderModule::Impl::FirstCatalogIndexForKind(TagsModule::TagKind kind) const {
    if (!tagsModule) {
        return -1;
    }

    const auto& entries = tagsModule->CatalogEntries();
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if (entries[static_cast<std::size_t>(i)].kind == kind) {
            return i;
        }
    }
    return -1;
}

void BinderModule::Impl::DrawEditorVariableInputsTab() {
    UiSettings& ui = UiSettings::Instance();
    if (editor.draft.inputs.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorVariablesEmpty));
        return;
    }

    if (editor.selectedVariableInputIndex < 0 || editor.selectedVariableInputIndex >= static_cast<int>(editor.draft.inputs.size())) {
        editor.selectedVariableInputIndex = 0;
    }

    if (!ImGui::BeginTable(
            "##binder_editor_variable_inputs_layout",
            2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings,
            ImVec2(0.0f, 0.0f))) {
        return;
    }

    ImGui::TableSetupColumn("list", ImGuiTableColumnFlags_WidthFixed, ScaleUi(320.0f));
    ImGui::TableSetupColumn("details", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    if (ImGui::BeginChild("##binder_editor_variable_inputs_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_FrameStyle)) {
        ImGui::TextDisabled("%s", ui.Text(UiText::InputFieldsListTitle));
        ImGui::Spacing();

        for (std::size_t i = 0; i < editor.draft.inputs.size(); ++i) {
            const HotkeyInput& input = editor.draft.inputs[i];
            const std::string normalizedKey = NormalizeInputKey(input.key);
            const std::string indexToken = "{{" + std::to_string(i + 1) + "}}";
            const std::string primaryToken = normalizedKey.empty() ? indexToken : "{{" + normalizedKey + "}}";
            const std::string label = input.label.empty() ? std::string(ui.Text(UiText::UnnamedField)) : input.label;
            const std::string rowLabel = primaryToken + "  " + label + "##binder_editor_variable_input_" + std::to_string(i);

            if (ImGui::Selectable(rowLabel.c_str(), editor.selectedVariableInputIndex == static_cast<int>(i))) {
                editor.selectedVariableInputIndex = static_cast<int>(i);
            }
        }
    }
    ImGui::EndChild();

    ImGui::TableSetColumnIndex(1);
    if (ImGui::BeginChild("##binder_editor_variable_inputs_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_FrameStyle)) {
        if (editor.selectedVariableInputIndex < 0
            || editor.selectedVariableInputIndex >= static_cast<int>(editor.draft.inputs.size())) {
            ImGui::TextDisabled("%s", ui.Text(UiText::EditorVariablesInspectorEmpty));
        } else {
            const std::size_t inputIndex = static_cast<std::size_t>(editor.selectedVariableInputIndex);
            const HotkeyInput& input = editor.draft.inputs[inputIndex];
            const std::string normalizedKey = NormalizeInputKey(input.key);
            const std::string keyToken = normalizedKey.empty() ? std::string() : "{{" + normalizedKey + "}}";
            const std::string indexToken = "{{" + std::to_string(inputIndex + 1) + "}}";
            const std::string primaryToken = keyToken.empty() ? indexToken : keyToken;
            const std::string label = input.label.empty() ? std::string(ui.Text(UiText::UnnamedField)) : input.label;
            const bool hasAlternateToken = !keyToken.empty() && keyToken != indexToken;

            ImGui::TextColored(ImVec4(0.55f, 0.86f, 0.98f, 1.0f), "%s", label.c_str());
            if (!input.hint.empty()) {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", input.hint.c_str());
            }

            ImGui::Spacing();
            ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesDescriptionLabel));
            ImGui::TextWrapped("%s", primaryToken.c_str());
            if (hasAlternateToken) {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesExampleLabel));
                ImGui::TextWrapped("%s", indexToken.c_str());
            }

            ImGui::Spacing();
            if (ImGui::Button(ui.Text(UiText::MiscVariablesCopyToken), ScaleUi(170.0f, 0.0f))) {
                CopyTextToClipboard(primaryToken);
            }
            if (hasAlternateToken) {
                ImGui::SameLine();
                if (ImGui::Button(ui.Text(UiText::MiscVariablesCopyExample), ScaleUi(170.0f, 0.0f))) {
                    CopyTextToClipboard(indexToken);
                }
            }

            if (keyToken.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", ui.Text(UiText::ValidationInputKeyRequired));
            } else {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", ui.Format(UiText::InputFieldPlaceholderFormat, normalizedKey.c_str()).c_str());
                ImGui::TextDisabled("%s", indexToken.c_str());
            }
        }
    }
    ImGui::EndChild();
    ImGui::EndTable();
}

void BinderModule::Impl::DrawEditorVariableCatalogTab(TagsModule::TagKind kind) {
    UiSettings& ui = UiSettings::Instance();
    if (!tagsModule) {
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorVariablesInspectorEmpty));
        return;
    }

    const auto& entries = tagsModule->CatalogEntries();
    const std::string query = ToLower(Trim(editor.variablesSearch));
    std::vector<int> visibleIndices;
    visibleIndices.reserve(entries.size());
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto& entry = entries[static_cast<std::size_t>(i)];
        if (entry.kind != kind) {
            continue;
        }

        const std::string haystack = ToLower(entry.token + " " + ui.Text(entry.descriptionText));
        if (query.empty() || haystack.find(query) != std::string::npos) {
            visibleIndices.push_back(i);
        }
    }

    int& selectedIndex = kind == TagsModule::TagKind::Simple ? editor.selectedSimpleTagIndex : editor.selectedFunctionTagIndex;
    if (visibleIndices.empty()) {
        selectedIndex = -1;
    } else if (std::find(visibleIndices.begin(), visibleIndices.end(), selectedIndex) == visibleIndices.end()) {
        selectedIndex = visibleIndices.front();
    }

    if (!ImGui::BeginTable(
            "##binder_editor_variable_catalog_layout",
            2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings,
            ImVec2(0.0f, 0.0f))) {
        return;
    }

    ImGui::TableSetupColumn("list", ImGuiTableColumnFlags_WidthFixed, ScaleUi(320.0f));
    ImGui::TableSetupColumn("details", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    if (ImGui::BeginChild("##binder_editor_variable_catalog_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_FrameStyle)) {
        InputTextWithHintString(
            "##binder_editor_variable_search",
            ui.Text(UiText::MiscVariablesSearchHint),
            editor.variablesSearch,
            ImGuiInputTextFlags_AutoSelectAll,
            128);
        ImGui::Spacing();

        if (ImGui::BeginChild("##binder_editor_variable_catalog_scroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
            if (visibleIndices.empty()) {
                ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesCatalogEmpty));
            } else {
                for (const int index : visibleIndices) {
                    const auto& entry = entries[static_cast<std::size_t>(index)];
                    const std::string rowLabel = entry.token + "##binder_editor_variable_catalog_" + std::to_string(index);
                    if (ImGui::Selectable(rowLabel.c_str(), selectedIndex == index)) {
                        selectedIndex = index;
                    }
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::TableSetColumnIndex(1);
    if (ImGui::BeginChild("##binder_editor_variable_catalog_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_FrameStyle)) {
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size())) {
            ImGui::TextDisabled("%s", ui.Text(UiText::EditorVariablesInspectorEmpty));
        } else {
            const auto& entry = entries[static_cast<std::size_t>(selectedIndex)];

            ImGui::TextColored(ImVec4(0.55f, 0.86f, 0.98f, 1.0f), "%s", entry.token.c_str());
            if (kind == TagsModule::TagKind::Function && std::string_view(entry.name) == "keyemulate") {
                ImGui::SameLine();
                if (ImGui::SmallButton(" + ")) {
                    editor.variablesKeyPickerPopupPending = true;
                }
            }

            ImGui::Separator();

            ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesDescriptionLabel));
            ImGui::TextWrapped("%s", ui.Text(entry.descriptionText));
            ImGui::Spacing();

            if (entry.example != entry.token) {
                ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesExampleLabel));
                ImGui::TextWrapped("%s", entry.example.c_str());
                ImGui::Spacing();
            }

            if (ImGui::Button(ui.Text(UiText::MiscVariablesCopyToken), ScaleUi(170.0f, 0.0f))) {
                CopyTextToClipboard(entry.token);
            }
            if (entry.example != entry.token) {
                ImGui::SameLine();
                if (ImGui::Button(ui.Text(UiText::MiscVariablesCopyExample), ScaleUi(170.0f, 0.0f))) {
                    CopyTextToClipboard(entry.example);
                }
            }

            if (kind == TagsModule::TagKind::Function && std::string_view(entry.name) == "paramcmd") {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesParamcmdNote));
            }
            if (kind == TagsModule::TagKind::Function && std::string_view(entry.name) == "keyemulate") {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesKeyEmulateNote));
            }
        }
    }
    ImGui::EndChild();
    ImGui::EndTable();
}

void BinderModule::Impl::DrawEditorVariableKeyPickerPopup() {
    if (!tagsModule) {
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    ImGui::SetNextWindowSize(ScaleUi(560.0f, 520.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("##binder_editor_variable_keypicker")) {
        return;
    }

    ImGui::TextUnformatted(ui.Text(UiText::MiscVariablesKeyPickerTitle));
    ImGui::TextWrapped("%s", ui.Text(UiText::MiscVariablesKeyPickerIntro));
    ImGui::Separator();

    InputTextWithHintString(
        "##binder_editor_variable_keypicker_search",
        ui.Text(UiText::MiscVariablesKeyPickerSearchHint),
        editor.variablesKeyPickerSearch,
        ImGuiInputTextFlags_AutoSelectAll,
        96);
    ImGui::Spacing();

    const std::string filter = ToLower(editor.variablesKeyPickerSearch);
    bool hasMatches = false;
    if (ImGui::BeginChild("##binder_editor_variable_keypicker_list", ScaleUi(0.0f, 360.0f), ImGuiChildFlags_Borders)) {
        for (const auto& entry : tagsModule->VirtualKeyPickerEntries()) {
            if (!filter.empty() && entry.search.find(filter) == std::string::npos) {
                continue;
            }

            hasMatches = true;
            if (ImGui::Selectable(entry.label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                CopyTextToClipboard(TagsModule::MakeKeyEmulateToken(entry.code));
                editor.variablesKeyPickerSearch.clear();
                ImGui::CloseCurrentPopup();
            }
        }

        if (!hasMatches) {
            ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesKeyPickerEmpty));
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesKeyPickerCopyHint));
    ImGui::Spacing();
    if (ImGui::Button(ui.Text(UiText::Cancel))) {
        editor.variablesKeyPickerSearch.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
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
    duplicated.runtimeId = AllocateHotkeyRuntimeId();

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
    const std::string folderLabel = FormatFolderLabel(folder.name);
    const bool opened = ImGui::TreeNodeEx("##folder_node", flags, "%s", folderLabel.c_str());
    folder.open = opened;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
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
            folderEditPopupPending = true;
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::MenuItem(ui.Text(UiText::FolderRename))) {
            folderPopup = {};
            folderPopup.target = &folder;
            folderPopup.name = folder.name;
            folderEditPopupPending = true;
            ImGui::CloseCurrentPopup();
        }

        const bool canDelete = CanDeleteFolder(&folder);
        if (!canDelete) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem(ui.Text(UiText::Delete)) && canDelete) {
            folderDeleteTarget = &folder;
            folderDeletePopupPending = true;
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
        folderPopup.parent = nullptr;
        folderPopup.name = UiSettings::Instance().Text(UiText::BinderNewFolder);
        folderEditPopupPending = true;
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string(UiSettings::Instance().Text(UiText::FolderRename)) + "##folder_rename").c_str()) && selectedFolder) {
        folderPopup = {};
        folderPopup.target = selectedFolder;
        folderPopup.name = selectedFolder->name;
        folderEditPopupPending = true;
    }
    ImGui::SameLine();
    const bool canDeleteSelected = CanDeleteFolder(selectedFolder);
    if (!canDeleteSelected) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button((std::string(UiSettings::Instance().Text(UiText::Delete)) + "##folder_delete").c_str())
        && canDeleteSelected) {
        folderDeleteTarget = selectedFolder;
        folderDeletePopupPending = true;
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
    if (folderEditPopupPending) {
        ImGui::OpenPopup("##binder_folder_edit");
        folderEditPopupPending = false;
    }
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

    if (folderDeletePopupPending) {
        ImGui::OpenPopup("##binder_folder_delete");
        folderDeletePopupPending = false;
    }
    if (ImGui::BeginPopupModal("##binder_folder_delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", UiSettings::Instance().Text(UiText::DeleteFolderMoveBindsQuestion));
        if (folderDeleteTarget) {
            ImGui::TextDisabled("%s", folderDeleteTarget->name.c_str());
        }
        if (ImGui::Button(UiSettings::Instance().Text(UiText::Delete))) {
            if (CanDeleteFolder(folderDeleteTarget)) {
                FolderNode* fallbackFolder = folderDeleteTarget->parent ? folderDeleteTarget->parent : EnsureRootFolder();
                const auto removedPath = BuildFolderPath(folderDeleteTarget);
                const auto selectedPath = selectedFolder ? BuildFolderPath(selectedFolder) : std::vector<std::string>{};
                DeleteHotkeysFromFolderPath(removedPath);

                auto& siblings = folderDeleteTarget->parent ? folderDeleteTarget->parent->children : folders;
                siblings.erase(
                    std::remove_if(siblings.begin(), siblings.end(), [&](const std::unique_ptr<FolderNode>& item) {
                        return item.get() == folderDeleteTarget;
                    }),
                    siblings.end());

                if (!selectedPath.empty() && PathStartsWith(selectedPath, removedPath)) {
                    selectedFolder = fallbackFolder;
                } else if (selectedFolder == folderDeleteTarget) {
                    selectedFolder = fallbackFolder;
                }
                if (!selectedFolder) {
                    selectedFolder = EnsureRootFolder();
                }
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
    UiSettings& ui = UiSettings::Instance();
    const ImVec2 actionButtonSize = ScaleUi(26.0f, 26.0f);

    auto ensureBulkStateStorage = [&]() {
        if (editor.inputButtonsBulkDrafts.size() < editor.draft.inputs.size()) {
            const std::size_t oldSize = editor.inputButtonsBulkDrafts.size();
            editor.inputButtonsBulkDrafts.resize(editor.draft.inputs.size());
            for (std::size_t i = oldSize; i < editor.draft.inputs.size(); ++i) {
                editor.inputButtonsBulkDrafts[i] = SerializeButtonsText(editor.draft.inputs[i].buttons);
            }
        } else if (editor.inputButtonsBulkDrafts.size() > editor.draft.inputs.size()) {
            editor.inputButtonsBulkDrafts.resize(editor.draft.inputs.size());
        }

        if (editor.inputButtonsBulkPreviews.size() < editor.draft.inputs.size()) {
            editor.inputButtonsBulkPreviews.resize(editor.draft.inputs.size());
        } else if (editor.inputButtonsBulkPreviews.size() > editor.draft.inputs.size()) {
            editor.inputButtonsBulkPreviews.resize(editor.draft.inputs.size());
        }
    };

    auto selectInput = [&](int index) {
        ensureBulkStateStorage();
        if (index < 0 || index >= static_cast<int>(editor.draft.inputs.size())) {
            editor.selectedInputIndex = -1;
            editor.selectedInputButtonIndex = -1;
            editor.selectedButtonsText.clear();
            return;
        }

        editor.selectedInputIndex = index;
        editor.selectedButtonsText = editor.inputButtonsBulkDrafts[static_cast<std::size_t>(index)];
        editor.selectedInputButtonIndex =
            editor.draft.inputs[static_cast<std::size_t>(index)].buttons.empty() ? -1 : 0;
    };

    auto clampSelectedInput = [&]() {
        ensureBulkStateStorage();
        if (editor.draft.inputs.empty()) {
            editor.selectedInputIndex = -1;
            editor.selectedInputButtonIndex = -1;
            editor.selectedButtonsText.clear();
            return;
        }

        if (editor.selectedInputIndex < 0 || editor.selectedInputIndex >= static_cast<int>(editor.draft.inputs.size())) {
            editor.selectedInputIndex = 0;
            editor.selectedButtonsText = editor.inputButtonsBulkDrafts.front();
        }

        const HotkeyInput& input = editor.draft.inputs[static_cast<std::size_t>(editor.selectedInputIndex)];
        if (input.buttons.empty()) {
            editor.selectedInputButtonIndex = -1;
        } else if (editor.selectedInputButtonIndex < 0 || editor.selectedInputButtonIndex >= static_cast<int>(input.buttons.size())) {
            editor.selectedInputButtonIndex = 0;
        }
    };

    auto syncSelectedButtonsText = [&](const HotkeyInput& input) {
        ensureBulkStateStorage();
        if (editor.selectedInputIndex >= 0 && editor.selectedInputIndex < static_cast<int>(editor.inputButtonsBulkDrafts.size())) {
            const std::string text = SerializeButtonsText(input.buttons);
            editor.inputButtonsBulkDrafts[static_cast<std::size_t>(editor.selectedInputIndex)] = text;
            editor.inputButtonsBulkPreviews[static_cast<std::size_t>(editor.selectedInputIndex)] = {};
            editor.selectedButtonsText = text;
        } else {
            editor.selectedButtonsText.clear();
        }
        if (input.buttons.empty()) {
            editor.selectedInputButtonIndex = -1;
        } else if (editor.selectedInputButtonIndex < 0 || editor.selectedInputButtonIndex >= static_cast<int>(input.buttons.size())) {
            editor.selectedInputButtonIndex = 0;
        }
    };

    auto inputDisplayName = [&](const HotkeyInput& input, int index) {
        const std::string label = Trim(input.label);
        if (!label.empty()) {
            return label;
        }
        return ui.Format(UiText::FieldLabelFormat, index + 1);
    };
    auto buttonDisplayName = [&](const InputButton& button, int index) {
        const std::string label = Trim(button.label);
        if (!label.empty()) {
            return label;
        }
        return ui.Format(UiText::ButtonLabelFormat, index + 1);
    };

    struct InputKeyIssue {
        bool empty = false;
        bool invalid = false;
        bool duplicate = false;
        std::string normalized;
    };

    auto buildInputKeyIssues = [&]() {
        std::vector<InputKeyIssue> issues(editor.draft.inputs.size());
        std::map<std::string, int> usage;
        for (std::size_t i = 0; i < editor.draft.inputs.size(); ++i) {
            const std::string raw = Trim(editor.draft.inputs[i].key);
            const std::string normalized = NormalizeInputKey(raw);
            issues[i].empty = raw.empty();
            issues[i].invalid = !raw.empty() && normalized != raw;
            issues[i].normalized = normalized;
            if (!normalized.empty()) {
                ++usage[normalized];
            }
        }
        for (std::size_t i = 0; i < issues.size(); ++i) {
            if (!issues[i].normalized.empty() && usage[issues[i].normalized] > 1) {
                issues[i].duplicate = true;
            }
        }
        return issues;
    };

    const auto drawInspectorTable = [&](const char* tableId, const auto& drawRows) {
        if (!ImGui::BeginTable(
                tableId,
                2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            return;
        }

        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, ScaleUi(190.0f));
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        const auto drawRow = [&](UiText labelId, const auto& drawValue) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", ui.Text(labelId));
            ImGui::TableSetColumnIndex(1);
            drawValue();
        };

        drawRows(drawRow);
        ImGui::EndTable();
    };

    if (ImGui::Button(ui.Text(UiText::AddField))) {
        HotkeyInput input;
        input.key = "FIELD_" + std::to_string(editor.draft.inputs.size() + 1);
        input.label = ui.Format(UiText::FieldLabelFormat, static_cast<int>(editor.draft.inputs.size() + 1));
        editor.draft.inputs.push_back(std::move(input));
        editor.inputButtonsBulkDrafts.push_back({});
        editor.inputButtonsBulkPreviews.push_back({});
        selectInput(static_cast<int>(editor.draft.inputs.size() - 1));
    }

    ImGui::Separator();
    if (editor.draft.inputs.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::InputFieldsEmpty));
        return;
    }

    clampSelectedInput();
    std::vector<InputKeyIssue> inputIssues = buildInputKeyIssues();

    if (!ImGui::BeginTable(
            "##binder_input_editor_layout",
            2,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        return;
    }

    ImGui::TableSetupColumn("fields", ImGuiTableColumnFlags_WidthFixed, ScaleUi(240.0f));
    ImGui::TableSetupColumn("details", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    if (ImGui::BeginChild("##binder_input_fields_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        ImGui::TextDisabled("%s", ui.Text(UiText::InputFieldsListTitle));
        ImGui::Separator();

        for (std::size_t i = 0; i < editor.draft.inputs.size(); ++i) {
            const HotkeyInput& listInput = editor.draft.inputs[i];
            const InputKeyIssue& issue = inputIssues[i];
            const bool hasIssue = issue.empty || issue.invalid || issue.duplicate;
            const std::string title = (hasIssue ? "! " : "") + inputDisplayName(listInput, static_cast<int>(i));
            const float labelWidth = std::max(ScaleUi(60.0f), ImGui::GetContentRegionAvail().x - ScaleUi(24.0f));
            const std::string visibleLabel = EllipsizeText(title, labelWidth);
            const std::string selectableLabel = visibleLabel + "##binder_input_sel_" + std::to_string(i);

            if (ImGui::Selectable(
                    selectableLabel.c_str(),
                    editor.selectedInputIndex == static_cast<int>(i),
                    0,
                    ImVec2(0.0f, 0.0f))) {
                selectInput(static_cast<int>(i));
            }

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(title.c_str());
                ImGui::Separator();
                ImGui::TextDisabled("%s", ui.Text(UiText::ParameterResponseType));
                ImGui::SameLine();
                ImGui::TextUnformatted(InputModeLabel(listInput.mode));
                if (!Trim(listInput.key).empty()) {
                    ImGui::TextDisabled("%s", ui.Format(UiText::InputFieldPlaceholderFormat, NormalizeInputKey(listInput.key).c_str()).c_str());
                }
                if (issue.empty || issue.invalid) {
                    ImGui::TextDisabled("%s", ui.Text(UiText::ValidationInputKeyRequired));
                }
                if (issue.duplicate) {
                    ImGui::TextDisabled("%s", ui.Text(UiText::ValidationInputKeyUnique));
                }
                ImGui::EndTooltip();
            }
        }
    }
    ImGui::EndChild();

    ImGui::TableSetColumnIndex(1);
    if (ImGui::BeginChild("##binder_input_field_details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        clampSelectedInput();
        if (editor.selectedInputIndex >= 0 && editor.selectedInputIndex < static_cast<int>(editor.draft.inputs.size())) {
            int currentIndex = editor.selectedInputIndex;
            std::string fieldTitle = inputDisplayName(editor.draft.inputs[static_cast<std::size_t>(currentIndex)], currentIndex);
            ImGui::TextWrapped("%s", fieldTitle.c_str());
            ImGui::Separator();

            if (SmallIconActionButton(kIconAngleUp, "##binder_input_move_up", ui.Text(UiText::MoveUp), actionButtonSize)
                && currentIndex > 0) {
                std::swap(
                    editor.draft.inputs[static_cast<std::size_t>(currentIndex)],
                    editor.draft.inputs[static_cast<std::size_t>(currentIndex - 1)]);
                std::swap(
                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex)],
                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex - 1)]);
                std::swap(
                    editor.inputButtonsBulkPreviews[static_cast<std::size_t>(currentIndex)],
                    editor.inputButtonsBulkPreviews[static_cast<std::size_t>(currentIndex - 1)]);
                selectInput(currentIndex - 1);
            }
            ImGui::SameLine();
            if (SmallIconActionButton(kIconAngleDown, "##binder_input_move_down", ui.Text(UiText::MoveDown), actionButtonSize)
                && currentIndex + 1 < static_cast<int>(editor.draft.inputs.size())) {
                std::swap(
                    editor.draft.inputs[static_cast<std::size_t>(currentIndex)],
                    editor.draft.inputs[static_cast<std::size_t>(currentIndex + 1)]);
                std::swap(
                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex)],
                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex + 1)]);
                std::swap(
                    editor.inputButtonsBulkPreviews[static_cast<std::size_t>(currentIndex)],
                    editor.inputButtonsBulkPreviews[static_cast<std::size_t>(currentIndex + 1)]);
                selectInput(currentIndex + 1);
            }
            ImGui::SameLine();
            if (SmallIconActionButton(kIconClone, "##binder_input_duplicate", ui.Text(UiText::ActionDuplicate), actionButtonSize)) {
                HotkeyInput duplicate = editor.draft.inputs[static_cast<std::size_t>(currentIndex)];
                editor.draft.inputs.insert(editor.draft.inputs.begin() + currentIndex + 1, std::move(duplicate));
                editor.inputButtonsBulkDrafts.insert(
                    editor.inputButtonsBulkDrafts.begin() + currentIndex + 1,
                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(currentIndex)]);
                editor.inputButtonsBulkPreviews.insert(
                    editor.inputButtonsBulkPreviews.begin() + currentIndex + 1,
                    {});
                selectInput(currentIndex + 1);
            }
            ImGui::SameLine();
            if (SmallIconActionButton(kIconDelete, "##binder_input_delete", ui.Text(UiText::Delete), actionButtonSize)) {
                editor.draft.inputs.erase(editor.draft.inputs.begin() + currentIndex);
                editor.inputButtonsBulkDrafts.erase(editor.inputButtonsBulkDrafts.begin() + currentIndex);
                editor.inputButtonsBulkPreviews.erase(editor.inputButtonsBulkPreviews.begin() + currentIndex);
                if (editor.draft.inputs.empty()) {
                    editor.selectedInputIndex = -1;
                    editor.selectedInputButtonIndex = -1;
                    editor.selectedButtonsText.clear();
                } else {
                    selectInput(std::min(currentIndex, static_cast<int>(editor.draft.inputs.size()) - 1));
                }
            }

            clampSelectedInput();
            if (editor.selectedInputIndex >= 0 && editor.selectedInputIndex < static_cast<int>(editor.draft.inputs.size())) {
                HotkeyInput& input = editor.draft.inputs[static_cast<std::size_t>(editor.selectedInputIndex)];
                const InputKeyIssue currentIssue = inputIssues[static_cast<std::size_t>(editor.selectedInputIndex)];
                const auto drawModeCombo = [&]() {
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
                    if (ImGui::Combo("##binder_input_mode", &modeIndex, modeLabels, IM_ARRAYSIZE(modeLabels))) {
                        input.mode = modes[modeIndex];
                    }
                };

                ImGui::Spacing();
                ImGui::SeparatorText(ui.Text(UiText::ParameterQuestionSection));
                ImGui::TextDisabled("%s", ui.Text(UiText::ParameterQuestionHint));
                drawInspectorTable("##binder_input_question_table", [&](const auto& drawRow) {
                    drawRow(UiText::ParameterPrompt, [&]() {
                        InputTextString("##binder_input_name", input.label, ImGuiInputTextFlags_AutoSelectAll, 128);
                    });
                    drawRow(UiText::ParameterHintText, [&]() {
                        InputTextString("##binder_input_hint", input.hint, ImGuiInputTextFlags_AutoSelectAll, 256);
                    });
                });

                ImGui::Spacing();
                ImGui::SeparatorText(ui.Text(UiText::ParameterResponseSection));
                ImGui::TextDisabled("%s", ui.Text(UiText::ParameterResponseHint));
                drawInspectorTable("##binder_input_response_table", [&](const auto& drawRow) {
                    drawRow(UiText::ParameterResponseType, drawModeCombo);
                    if (InputModeUsesButtons(input.mode)) {
                        drawRow(UiText::ParameterAllowMultiple, [&]() {
                            ImGui::Checkbox("##binder_input_multi", &input.multiSelect);
                        });
                        if (input.multiSelect) {
                            drawRow(UiText::ParameterJoinSeparator, [&]() {
                                InputTextString(
                                    "##binder_input_separator",
                                    input.multiSeparator,
                                    ImGuiInputTextFlags_AutoSelectAll,
                                    64);
                            });
                        }
                    }
                });

                ImGui::Spacing();
                ImGui::SeparatorText(ui.Text(UiText::ParameterAdvancedSection));
                ImGui::TextDisabled("%s", ui.Text(UiText::ParameterAdvancedHint));
                drawInspectorTable("##binder_input_advanced_table", [&](const auto& drawRow) {
                    drawRow(UiText::ParameterSystemKey, [&]() {
                        InputTextString("##binder_input_key", input.key, ImGuiInputTextFlags_AutoSelectAll, 64);
                        input.key = NormalizeInputKey(input.key);
                    });
                    if (InputModeUsesButtons(input.mode)) {
                        drawRow(UiText::ParameterDependsOn, [&]() {
                            InputTextString("##binder_input_cascade", input.cascadeParentKey, ImGuiInputTextFlags_AutoSelectAll, 64);
                            input.cascadeParentKey = NormalizeInputKey(input.cascadeParentKey);
                        });
                    }
                });

                const std::string normalizedKey = NormalizeInputKey(input.key);
                if (!normalizedKey.empty()) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("%s", ui.Format(UiText::InputFieldPlaceholderFormat, normalizedKey.c_str()).c_str());
                    ImGui::SameLine();
                    if (SmallIconActionButton(kIconClone, "##binder_copy_input_placeholder", ui.Text(UiText::CopyPlaceholder), actionButtonSize)) {
                        ImGui::SetClipboardText(("{{" + normalizedKey + "}}").c_str());
                    }
                }
                if (currentIssue.empty || currentIssue.invalid) {
                    ImGui::TextDisabled("%s", ui.Text(UiText::ValidationInputKeyRequired));
                } else if (currentIssue.duplicate) {
                    ImGui::TextDisabled("%s", ui.Text(UiText::ValidationInputKeyUnique));
                }

                if (InputModeUsesButtons(input.mode)) {
                    ImGui::Spacing();
                    ImGui::SeparatorText(ui.Text(UiText::ParameterVariantsSection));
                    ImGui::TextDisabled("%s", ui.Text(UiText::ParameterVariantsHint));
                    if (ImGui::BeginTabBar("##binder_input_buttons_tabs")) {
                        if (ImGui::BeginTabItem(ui.Text(UiText::ButtonsStructuredTab))) {
                            if (ImGui::Button(ui.Text(UiText::AddButton))) {
                                input.buttons.push_back(InputButton{});
                                editor.selectedInputButtonIndex = static_cast<int>(input.buttons.size() - 1);
                                syncSelectedButtonsText(input);
                            }

                            const bool hasSelectedButton =
                                editor.selectedInputButtonIndex >= 0
                                && editor.selectedInputButtonIndex < static_cast<int>(input.buttons.size());
                            if (hasSelectedButton) {
                                ImGui::SameLine();
                                if (SmallIconActionButton(kIconClone, "##binder_button_duplicate", ui.Text(UiText::ActionDuplicate), actionButtonSize)) {
                                    const int selectedButtonIndex = editor.selectedInputButtonIndex;
                                    InputButton duplicate = input.buttons[static_cast<std::size_t>(selectedButtonIndex)];
                                    input.buttons.insert(input.buttons.begin() + selectedButtonIndex + 1, std::move(duplicate));
                                    editor.selectedInputButtonIndex = selectedButtonIndex + 1;
                                    syncSelectedButtonsText(input);
                                }
                                ImGui::SameLine();
                                if (SmallIconActionButton(kIconAngleUp, "##binder_button_move_up", ui.Text(UiText::MoveUp), actionButtonSize)
                                    && editor.selectedInputButtonIndex > 0) {
                                    const int selectedButtonIndex = editor.selectedInputButtonIndex;
                                    std::swap(
                                        input.buttons[static_cast<std::size_t>(selectedButtonIndex)],
                                        input.buttons[static_cast<std::size_t>(selectedButtonIndex - 1)]);
                                    editor.selectedInputButtonIndex = selectedButtonIndex - 1;
                                    syncSelectedButtonsText(input);
                                }
                                ImGui::SameLine();
                                if (SmallIconActionButton(kIconAngleDown, "##binder_button_move_down", ui.Text(UiText::MoveDown), actionButtonSize)
                                    && editor.selectedInputButtonIndex + 1 < static_cast<int>(input.buttons.size())) {
                                    const int selectedButtonIndex = editor.selectedInputButtonIndex;
                                    std::swap(
                                        input.buttons[static_cast<std::size_t>(selectedButtonIndex)],
                                        input.buttons[static_cast<std::size_t>(selectedButtonIndex + 1)]);
                                    editor.selectedInputButtonIndex = selectedButtonIndex + 1;
                                    syncSelectedButtonsText(input);
                                }
                                ImGui::SameLine();
                                if (SmallIconActionButton(kIconDelete, "##binder_button_delete", ui.Text(UiText::Delete), actionButtonSize)) {
                                    input.buttons.erase(input.buttons.begin() + editor.selectedInputButtonIndex);
                                    if (input.buttons.empty()) {
                                        editor.selectedInputButtonIndex = -1;
                                    } else {
                                        editor.selectedInputButtonIndex = std::min(
                                            editor.selectedInputButtonIndex,
                                            static_cast<int>(input.buttons.size()) - 1);
                                    }
                                    syncSelectedButtonsText(input);
                                }
                            }

                            const float buttonEditorHeight = std::max(ScaleUi(180.0f), ImGui::GetContentRegionAvail().y - ScaleUi(6.0f));
                            if (ImGui::BeginTable(
                                    "##binder_button_editor_layout",
                                    2,
                                    ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
                                ImGui::TableSetupColumn("buttons", ImGuiTableColumnFlags_WidthFixed, ScaleUi(230.0f));
                                ImGui::TableSetupColumn("properties", ImGuiTableColumnFlags_WidthStretch);
                                ImGui::TableNextRow();

                                ImGui::TableSetColumnIndex(0);
                                if (ImGui::BeginChild("##binder_button_list", ImVec2(0.0f, buttonEditorHeight), ImGuiChildFlags_Borders)) {
                                    ImGui::TextDisabled("%s", ui.Text(UiText::ButtonListTitle));
                                    ImGui::Separator();
                                    if (input.buttons.empty()) {
                                        ImGui::TextDisabled("%s", ui.Text(UiText::ButtonsEmpty));
                                    } else {
                                        for (std::size_t buttonIndex = 0; buttonIndex < input.buttons.size(); ++buttonIndex) {
                                            const InputButton& button = input.buttons[buttonIndex];
                                            const std::string title = buttonDisplayName(button, static_cast<int>(buttonIndex));
                                            const float labelWidth = std::max(ScaleUi(60.0f), ImGui::GetContentRegionAvail().x - ScaleUi(24.0f));
                                            const std::string visibleLabel = EllipsizeText(title, labelWidth);
                                            const std::string selectableLabel = visibleLabel + "##binder_button_sel_" + std::to_string(buttonIndex);
                                            if (ImGui::Selectable(
                                                    selectableLabel.c_str(),
                                                    editor.selectedInputButtonIndex == static_cast<int>(buttonIndex),
                                                    0,
                                                    ImVec2(0.0f, 0.0f))) {
                                                editor.selectedInputButtonIndex = static_cast<int>(buttonIndex);
                                            }

                                            if (ImGui::IsItemHovered()) {
                                                ImGui::BeginTooltip();
                                                ImGui::TextUnformatted(title.c_str());
                                                ImGui::Separator();
                                                if (!Trim(button.text).empty()) {
                                                    ImGui::TextDisabled("%s", ui.Text(UiText::OptionValue));
                                                    ImGui::TextWrapped("%s", button.text.c_str());
                                                }
                                                if (!Trim(button.hint).empty()) {
                                                    ImGui::TextDisabled("%s", ui.Text(UiText::OptionHint));
                                                    ImGui::TextWrapped("%s", button.hint.c_str());
                                                }
                                                ImGui::EndTooltip();
                                            }
                                        }
                                    }
                                }
                                ImGui::EndChild();

                                ImGui::TableSetColumnIndex(1);
                                if (ImGui::BeginChild("##binder_button_properties", ImVec2(0.0f, buttonEditorHeight), ImGuiChildFlags_Borders)) {
                                    if (editor.selectedInputButtonIndex >= 0
                                        && editor.selectedInputButtonIndex < static_cast<int>(input.buttons.size())) {
                                        InputButton& button = input.buttons[static_cast<std::size_t>(editor.selectedInputButtonIndex)];
                                        ImGui::TextDisabled("%s", ui.Text(UiText::ButtonPropertiesTitle));
                                        ImGui::Separator();

                                        bool buttonsChanged = false;
                                        buttonsChanged |= InputTextString(
                                            ui.Text(UiText::OptionName),
                                            button.label,
                                            ImGuiInputTextFlags_AutoSelectAll,
                                            128);
                                        buttonsChanged |= InputTextMultilineString(
                                            ui.Text(UiText::OptionValue),
                                            button.text,
                                            ImVec2(-FLT_MIN, ScaleUi(92.0f)),
                                            0,
                                            512);
                                        buttonsChanged |= InputTextString(
                                            ui.Text(UiText::OptionHint),
                                            button.hint,
                                            ImGuiInputTextFlags_AutoSelectAll,
                                            256);
                                        buttonsChanged |= InputTextString(
                                            ui.Text(UiText::ButtonWhen),
                                            button.when,
                                            ImGuiInputTextFlags_AutoSelectAll,
                                            256);
                                        ImGui::TextDisabled("%s", ui.Text(UiText::ButtonWhenHint));

                                        if (buttonsChanged) {
                                            syncSelectedButtonsText(input);
                                        }
                                    } else {
                                        ImGui::TextDisabled("%s", ui.Text(UiText::ButtonsEmpty));
                                    }
                                }
                                ImGui::EndChild();

                                ImGui::EndTable();
                            }

                            ImGui::EndTabItem();
                        }

                        if (ImGui::BeginTabItem(ui.Text(UiText::ButtonsBulkTab))) {
                            ButtonsBulkPreviewState* previewState = nullptr;
                            if (editor.selectedInputIndex >= 0
                                && editor.selectedInputIndex < static_cast<int>(editor.inputButtonsBulkPreviews.size())) {
                                previewState = &editor.inputButtonsBulkPreviews[static_cast<std::size_t>(editor.selectedInputIndex)];
                            }

                            if (ImGui::Button(ui.Text(UiText::ButtonsBulkAddLine))) {
                                AppendButtonsBulkTemplateLines(editor.selectedButtonsText, 1);
                                if (editor.selectedInputIndex >= 0
                                    && editor.selectedInputIndex < static_cast<int>(editor.inputButtonsBulkDrafts.size())) {
                                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(editor.selectedInputIndex)] =
                                        editor.selectedButtonsText;
                                }
                                if (previewState) {
                                    *previewState = {};
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(ui.Text(UiText::ButtonsBulkAddFiveLines))) {
                                AppendButtonsBulkTemplateLines(editor.selectedButtonsText, 5);
                                if (editor.selectedInputIndex >= 0
                                    && editor.selectedInputIndex < static_cast<int>(editor.inputButtonsBulkDrafts.size())) {
                                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(editor.selectedInputIndex)] =
                                        editor.selectedButtonsText;
                                }
                                if (previewState) {
                                    *previewState = {};
                                }
                            }

                            ImGui::Spacing();
                            if (ImGui::Button(ui.Text(UiText::ButtonsBulkSync))) {
                                syncSelectedButtonsText(input);
                                if (previewState) {
                                    *previewState = {};
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(ui.Text(UiText::ButtonsBulkCheck))) {
                                ButtonsTextParseStats stats;
                                const auto buttonsPreview = ParseButtonsTextEx(editor.selectedButtonsText, &stats);
                                if (previewState) {
                                    previewState->active = true;
                                    previewState->count = static_cast<int>(buttonsPreview.size());
                                    previewState->stats = stats;
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(ui.Text(UiText::ButtonsBulkNormalize))) {
                                ButtonsTextParseStats stats;
                                const auto normalizedButtons = ParseButtonsTextEx(editor.selectedButtonsText, &stats);
                                editor.selectedButtonsText = SerializeButtonsText(normalizedButtons);
                                if (editor.selectedInputIndex >= 0
                                    && editor.selectedInputIndex < static_cast<int>(editor.inputButtonsBulkDrafts.size())) {
                                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(editor.selectedInputIndex)] =
                                        editor.selectedButtonsText;
                                }
                                if (previewState) {
                                    *previewState = {};
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(ui.Text(UiText::ButtonsBulkApply))) {
                                input.buttons = ParseButtonsText(editor.selectedButtonsText);
                                syncSelectedButtonsText(input);
                            }

                            ImGui::Spacing();
                            ImGui::TextDisabled("%s", ui.Text(UiText::ButtonsFormatHint));
                            ImGui::TextDisabled("%s", ui.Text(UiText::ButtonsBulkEscapingHint));
                            if (InputTextMultilineString(
                                "##binder_input_buttons_bulk",
                                editor.selectedButtonsText,
                                ImVec2(-FLT_MIN, ScaleUi(220.0f)))) {
                                if (editor.selectedInputIndex >= 0
                                    && editor.selectedInputIndex < static_cast<int>(editor.inputButtonsBulkDrafts.size())) {
                                    editor.inputButtonsBulkDrafts[static_cast<std::size_t>(editor.selectedInputIndex)] =
                                        editor.selectedButtonsText;
                                }
                                if (previewState) {
                                    *previewState = {};
                                }
                            }

                            if (previewState && previewState->active) {
                                ImGui::Spacing();
                                ImGui::TextDisabled(
                                    "%s",
                                    ui.Format(
                                          UiText::ButtonsBulkPreviewFormat,
                                          previewState->stats.total,
                                          previewState->stats.used,
                                          previewState->stats.ignored,
                                          previewState->count)
                                        .c_str());
                                if (previewState->stats.extraPipes > 0) {
                                    ImGui::TextDisabled("%s", ui.Text(UiText::ButtonsBulkExtraPipesHint));
                                }
                            }
                            ImGui::EndTabItem();
                        }

                        ImGui::EndTabBar();
                    }
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::EndTable();
}

void BinderModule::Impl::DrawEditorConditionsPopup() {
    if (editor.conditionsPopupPending) {
        ImGui::OpenPopup("##binder_editor_conditions");
        editor.conditionsPopupPending = false;
    }

    if (!ImGui::BeginPopup("##binder_editor_conditions")) {
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    ImGui::TextUnformatted(ui.Text(UiText::BlockingConditions));
    ImGui::Separator();
    for (std::size_t i = 0; i < static_cast<std::size_t>(ConditionId::Count); ++i) {
        bool value = editor.draft.conditions[i];
        if (ImGui::Checkbox(ConditionLabel(static_cast<ConditionId>(i)), &value)) {
            editor.draft.conditions[i] = value;
        }
    }
    ImGui::Spacing();
    if (ImGui::Button(ui.Text(UiText::Save))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void BinderModule::Impl::DrawEditorQuickConditionsPopup() {
    if (editor.quickConditionsPopupPending) {
        ImGui::OpenPopup("##binder_editor_quick_conditions");
        editor.quickConditionsPopupPending = false;
    }

    if (!ImGui::BeginPopup("##binder_editor_quick_conditions")) {
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    ImGui::TextUnformatted(ui.Text(UiText::QuickMenuConditions));
    ImGui::Separator();
    for (std::size_t i = 0; i < static_cast<std::size_t>(ConditionId::Count); ++i) {
        bool value = editor.draft.quickConditions[i];
        if (ImGui::Checkbox(ConditionLabel(static_cast<ConditionId>(i)), &value)) {
            editor.draft.quickConditions[i] = value;
        }
    }
    ImGui::Spacing();
    if (ImGui::Button(ui.Text(UiText::Save))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void BinderModule::Impl::DrawEditorVariablesPopup() {
    if (editor.variablesPopupPending) {
        editor.variablesSearch.clear();
        editor.variablesKeyPickerSearch.clear();
        editor.variablesKeyPickerPopupPending = false;
        editor.variablesActiveTab = editor.draft.inputs.empty() ? 1 : 0;
        editor.variablesTabSelectionPending = true;
        editor.selectedVariableInputIndex = editor.draft.inputs.empty() ? -1 : 0;
        if (editor.selectedSimpleTagIndex < 0) {
            editor.selectedSimpleTagIndex = FirstCatalogIndexForKind(TagsModule::TagKind::Simple);
        }
        if (editor.selectedFunctionTagIndex < 0) {
            editor.selectedFunctionTagIndex = FirstCatalogIndexForKind(TagsModule::TagKind::Function);
        }
        ImGui::OpenPopup(kEditorVariablesPopupId);
        editor.variablesPopupPending = false;
    }

    UiSettings& ui = UiSettings::Instance();
    const std::string popupTitle = std::string(ui.Text(UiText::EditorVariablesTitle)) + kEditorVariablesPopupId;
    bool popupOpen = true;
    ImGui::SetNextWindowSize(ScaleUi(920.0f, 640.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(popupTitle.c_str(), &popupOpen, ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    const bool closeRequested = !popupOpen;
    const bool hasInputs = !editor.draft.inputs.empty();

    if (!hasInputs && editor.variablesActiveTab == 0) {
        editor.variablesActiveTab = 1;
        editor.variablesTabSelectionPending = true;
    }

    if (editor.variablesKeyPickerPopupPending) {
        ImGui::OpenPopup("##binder_editor_variable_keypicker");
        editor.variablesKeyPickerPopupPending = false;
    }

    ImGui::Separator();
    if (hasInputs) {
        ImGui::TextWrapped("%s", ui.Text(UiText::EditorVariablesHint));
        ImGui::Spacing();
    }

    if (ImGui::BeginTabBar("##binder_editor_variables_tabs")) {
        if (hasInputs) {
            const ImGuiTabItemFlags parametersFlags =
                editor.variablesTabSelectionPending && editor.variablesActiveTab == 0 ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(ui.Text(UiText::EditorVariablesParametersTab), nullptr, parametersFlags)) {
                editor.variablesActiveTab = 0;
                DrawEditorVariableInputsTab();
                ImGui::EndTabItem();
            }
        }

        const ImGuiTabItemFlags simpleFlags =
            editor.variablesTabSelectionPending && editor.variablesActiveTab == 1 ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem(ui.Text(UiText::EditorVariablesSimpleTab), nullptr, simpleFlags)) {
            editor.variablesActiveTab = 1;
            DrawEditorVariableCatalogTab(TagsModule::TagKind::Simple);
            ImGui::EndTabItem();
        }

        const ImGuiTabItemFlags functionFlags =
            editor.variablesTabSelectionPending && editor.variablesActiveTab == 2 ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem(ui.Text(UiText::EditorVariablesFunctionTab), nullptr, functionFlags)) {
            editor.variablesActiveTab = 2;
            DrawEditorVariableCatalogTab(TagsModule::TagKind::Function);
            ImGui::EndTabItem();
        }

        editor.variablesTabSelectionPending = false;
        ImGui::EndTabBar();
    }

    DrawEditorVariableKeyPickerPopup();
    if (closeRequested) {
        editor.variablesKeyPickerSearch.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void BinderModule::Impl::DrawEditorPreviewPopup() {
    if (editor.previewPopupPending) {
        ImGui::OpenPopup("##binder_editor_preview");
        editor.previewPopupPending = false;
    }

    if (!ImGui::BeginPopupModal("##binder_editor_preview", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    const HotkeyEntry preview = BuildEditorComparableDraft();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float destinationColumnWidth = std::ceil(
        ImGui::CalcTextSize(ui.Text(UiText::SendDirect)).x + style.FramePadding.x * 2.0f + ImGui::GetFrameHeight());
    const bool hasPreviewRows = std::any_of(preview.messages.begin(), preview.messages.end(), [](const HotkeyMessage& message) {
        return !Trim(message.text).empty() || message.intervalMs != 0 || message.method != 0;
    });

    ImGui::TextUnformatted(ui.Text(UiText::EditorPreviewTitle));
    ImGui::Separator();
    if (!hasPreviewRows) {
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorPreviewEmpty));
    } else if (ImGui::BeginTable(
                   "##binder_editor_preview_table",
                   3,
                   ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(ui.Text(UiText::EditorColumnMessage), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(ui.Text(UiText::EditorColumnPauseMs), ImGuiTableColumnFlags_WidthFixed, ScaleUi(110.0f));
        ImGui::TableSetupColumn(ui.Text(UiText::EditorColumnDestination), ImGuiTableColumnFlags_WidthFixed, destinationColumnWidth);
        ImGui::TableHeadersRow();

        for (const HotkeyMessage& message : preview.messages) {
            if (Trim(message.text).empty() && message.intervalMs == 0 && message.method == 0) {
                continue;
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextWrapped("%s", message.text.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", std::max(message.intervalMs, 0));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(SendMethodLabel(message.method));
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button(ui.Text(UiText::Cancel))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void BinderModule::Impl::DrawEditorDiscardPopup() {
    if (editor.discardPopupPending) {
        ImGui::OpenPopup("##binder_editor_discard");
        editor.discardPopupPending = false;
    }

    if (!ImGui::BeginPopupModal("##binder_editor_discard", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    ImGui::TextUnformatted(ui.Text(UiText::EditorDiscardTitle));
    ImGui::Separator();
    ImGui::TextWrapped("%s", ui.Text(UiText::EditorDiscardMessage));
    ImGui::Spacing();
    if (ImGui::Button(ui.Text(UiText::EditorDiscardAction), ScaleUi(170.0f, 0.0f))) {
        ExecuteEditorPendingAction();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::EditorStay), ScaleUi(120.0f, 0.0f))) {
        editor.pendingAction = EditorState::PendingAction::None;
        editor.pendingTargetIndex = -1;
        editor.discardPopupPending = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void BinderModule::Impl::DrawEditorScenarioTab() {
    UiSettings& ui = UiSettings::Instance();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float dragHandleWidth = std::ceil(ImGui::CalcTextSize(kIconMoveRows).x + ScaleUi(4.0f));
    const float dragHandleHeight = ImGui::GetFrameHeight();
    const float dragColumnWidth = std::ceil(dragHandleWidth + style.CellPadding.x * 2.0f);
    const float destinationColumnWidth = std::ceil(
        ImGui::CalcTextSize(ui.Text(UiText::SendDirect)).x + style.FramePadding.x * 2.0f + ImGui::GetFrameHeight());
    const ImVec2 actionButtonSize(ScaleUi(24.0f), ScaleUi(24.0f));
    const float actionButtonsSpacing = ScaleUi(4.0f);
    const float actionButtonsWidth = actionButtonSize.x * 2.0f + actionButtonsSpacing;
    const float actionsColumnWidth = std::ceil(actionButtonsWidth + style.CellPadding.x * 2.0f);
    if (editor.draft.messages.empty()) {
        editor.draft.messages.push_back(HotkeyMessage{ "", 0, editor.bulkMethod });
    }

    ImGui::TextDisabled("%s", ui.Text(UiText::EditorScenarioHint));
    ImGui::Spacing();

    int removeIndex = -1;
    int duplicateIndex = -1;
    int moveSourceIndex = -1;
    int moveTargetIndex = -1;
    const float addButtonReserve = ImGui::GetFrameHeightWithSpacing() + ScaleUi(8.0f);
    const ImVec2 stepsTableOuterSize(
        ImGui::GetContentRegionAvail().x,
        std::max(ScaleUi(180.0f), ImGui::GetContentRegionAvail().y - addButtonReserve));
    if (ImGui::BeginTable(
            "##binder_editor_steps",
            5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp
                | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
            stepsTableOuterSize)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##drag", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, dragColumnWidth);
        ImGui::TableSetupColumn(UiSettings::Instance().Text(UiText::EditorColumnMessage), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(UiSettings::Instance().Text(UiText::EditorColumnPauseMs), ImGuiTableColumnFlags_WidthFixed, ScaleUi(110.0f));
        ImGui::TableSetupColumn(UiSettings::Instance().Text(UiText::EditorColumnDestination), ImGuiTableColumnFlags_WidthFixed, destinationColumnWidth);
        ImGui::TableSetupColumn(UiSettings::Instance().Text(UiText::ColumnActions), ImGuiTableColumnFlags_WidthFixed, actionsColumnWidth);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < editor.draft.messages.size(); ++i) {
            HotkeyMessage& message = editor.draft.messages[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableSetColumnIndex(0);
            CenterNextItemHorizontally(dragHandleWidth);
            IconOnlyButton(kIconMoveRows, "##step_drag", ui.Text(UiText::EditorMoveStep), ImVec2(dragHandleWidth, dragHandleHeight));
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
                const int payloadIndex = static_cast<int>(i);
                ImGui::SetDragDropPayload("BINDER_EDITOR_STEP", &payloadIndex, sizeof(payloadIndex));
                ImGui::Text("%s %d", ui.Text(UiText::EditorScenarioTab), static_cast<int>(i + 1));
                std::string previewText = Trim(message.text);
                if (previewText.empty()) {
                    previewText = "...";
                } else {
                    while (previewText.size() > 77) {
                        previewText = Utf8TrimLastChar(previewText);
                    }
                }
                if (previewText != Trim(message.text)) {
                    previewText += "...";
                }
                ImGui::TextDisabled("%s", previewText.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            InputTextString("##step_text", message.text, 0, 256);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BINDER_EDITOR_STEP")) {
                    if (payload->Delivery && payload->DataSize == sizeof(int)) {
                        const int payloadIndex = *static_cast<const int*>(payload->Data);
                        if (payloadIndex >= 0 && payloadIndex < static_cast<int>(editor.draft.messages.size())
                            && payloadIndex != static_cast<int>(i)) {
                            moveSourceIndex = payloadIndex;
                            moveTargetIndex = static_cast<int>(i);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputInt("##step_delay", &message.intervalMs);
            if (message.intervalMs < 0) {
                message.intervalMs = 0;
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##step_method", SendMethodLabel(message.method))) {
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

            ImGui::TableSetColumnIndex(4);
            const float actionButtonsOffsetY = std::max(0.0f, std::floor((ImGui::GetFrameHeight() - actionButtonSize.y) * 0.5f));
            if (actionButtonsOffsetY > 0.0f) {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + actionButtonsOffsetY);
            }
            CenterNextItemHorizontally(actionButtonsWidth);
            if (SmallIconActionButton(kIconClone, "##step_duplicate", ui.Text(UiText::EditorDuplicateStep), actionButtonSize)) {
                duplicateIndex = static_cast<int>(i);
            }
            ImGui::SameLine(0.0f, actionButtonsSpacing);
            if (SmallIconActionButton(kIconDelete, "##step_delete", ui.Text(UiText::Delete), actionButtonSize)) {
                removeIndex = static_cast<int>(i);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (moveSourceIndex >= 0 && moveTargetIndex >= 0 && moveSourceIndex != moveTargetIndex) {
        HotkeyMessage moved = editor.draft.messages[static_cast<std::size_t>(moveSourceIndex)];
        editor.draft.messages.erase(editor.draft.messages.begin() + moveSourceIndex);
        editor.draft.messages.insert(editor.draft.messages.begin() + moveTargetIndex, std::move(moved));
    }
    if (duplicateIndex >= 0 && duplicateIndex < static_cast<int>(editor.draft.messages.size())) {
        editor.draft.messages.insert(
            editor.draft.messages.begin() + duplicateIndex + 1,
            editor.draft.messages[static_cast<std::size_t>(duplicateIndex)]);
    }
    if (removeIndex >= 0 && removeIndex < static_cast<int>(editor.draft.messages.size())) {
        if (editor.draft.messages.size() > 1) {
            editor.draft.messages.erase(editor.draft.messages.begin() + removeIndex);
        } else {
            editor.draft.messages.front() = HotkeyMessage{ "", 0, editor.bulkMethod };
        }
    }

    ImGui::Spacing();
    if (ImGui::Button(ui.Text(UiText::EditorAddStep))) {
        editor.draft.messages.push_back(HotkeyMessage{ "", 0, editor.bulkMethod });
    }

    SyncEditorMessagesToMulti();
}

void BinderModule::Impl::DrawEditorInline() {
    UiSettings& ui = UiSettings::Instance();
    EnsureRootFolder();
    const auto [prevIndex, nextIndex] = EditorNeighborIndices();
    const bool hasUnsavedChanges = EditorHasUnsavedChanges();

    const std::string title = ui.Text(editor.isNew ? UiText::NewBindTitle : UiText::EditBindTitle);
    std::string breadcrumb = ui.Text(UiText::BinderSectionTitle);
    for (const std::string& part : editor.draft.folderPath) {
        breadcrumb += " / " + part;
    }
    breadcrumb += " / " + (Trim(editor.draft.label).empty()
        ? std::string(ui.Text(editor.isNew ? UiText::NewBindTitle : UiText::EditBindTitle))
        : editor.draft.label);

    const ImVec4 headerBg(0.14f, 0.16f, 0.20f, 0.98f);
    const ImVec4 panelBg(0.12f, 0.14f, 0.18f, 0.98f);
    const ImVec4 footerBg(0.11f, 0.13f, 0.17f, 0.98f);
    const std::string conditionsLabel = std::string(kIconSliders) + " " + ui.Text(UiText::EditorOpenConditions);
    const std::string quickMenuLabel = std::string(kIconBolt) + " " + ui.Text(UiText::QuickMenuWindowTitle);
    const std::string backLabel = std::string(kIconChevronLeft) + " " + ui.Text(UiText::EditorBack);
    const std::string previousLabel = std::string(kIconChevronLeft) + " " + ui.Text(UiText::EditorPreviousBind);
    const std::string nextLabel = std::string(ui.Text(UiText::EditorNextBind)) + " " + std::string(kIconChevronRight);
    const std::string variablesLabel = std::string(kIconTags) + " " + ui.Text(UiText::EditorVariables);
    const std::string previewLabel = std::string(kIconComment) + " " + ui.Text(UiText::EditorPreview);
    const std::string saveLabel = std::string(kIconSaveDisk) + " " + ui.Text(UiText::Save);
    const float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 startToggleSize = ScaleUi(46.0f, 30.0f);

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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(14.0f, 10.0f));
    if (ImGui::BeginChild("##binder_editor_header", ImVec2(0.0f, ScaleUi(64.0f)), ImGuiChildFlags_Borders)) {
        if (ImGui::BeginTable("##binder_editor_header_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 0.34f);
            ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 0.66f);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            const float backButtonWidth = ScaleUi(92.0f);
            const ImVec2 headerStateIconSize(ScaleUi(20.0f), ImGui::GetFrameHeight());
            if (ImGui::Button(backLabel.c_str(), ImVec2(backButtonWidth, 0.0f))) {
                RequestEditorAction(EditorState::PendingAction::Close);
            }
            ImGui::SameLine(0.0f, ScaleUi(10.0f));
            if (IconOnlyButton(
                    editor.draft.enabled ? kIconToggleOn : kIconToggleOff,
                    "##binder_editor_enabled",
                    ui.Text(UiText::Enabled),
                    headerStateIconSize,
                    editor.draft.enabled)) {
                editor.draft.enabled = !editor.draft.enabled;
                if (!editor.draft.enabled) {
                    editor.draft.quickMenu = false;
                }
            }
            ImGui::SameLine(0.0f, ScaleUi(8.0f));
            const float headerTitleStartX = ImGui::GetCursorPosX();
            const float headerTitleTopY = ImGui::GetCursorPosY();
            ImGui::TextUnformatted(title.c_str());
            if (hasUnsavedChanges) {
                ImGui::SameLine(0.0f, ScaleUi(10.0f));
                ImGui::TextColored(ImVec4(0.96f, 0.68f, 0.25f, 1.0f), "%s", kIconStar);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::SetTooltip("%s", ui.Text(UiText::EditorUnsaved));
                }
            }
            const float headerBreadcrumbY = std::max(headerTitleTopY + ScaleUi(16.0f), ImGui::GetCursorPosY() - ScaleUi(6.0f));
            ImGui::SetCursorPos(ImVec2(headerTitleStartX, headerBreadcrumbY));
            ImGui::SetWindowFontScale(0.75f);
            const std::string headerBreadcrumb = EllipsizeText(breadcrumb, std::max(0.0f, ImGui::GetContentRegionAvail().x * 2.0f));
            ImGui::TextDisabled("%s", headerBreadcrumb.c_str());
            ImGui::SetWindowFontScale(1.0f);
            if (headerBreadcrumb != breadcrumb && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", breadcrumb.c_str());
            }

            ImGui::TableSetColumnIndex(1);
            const float conditionsButtonWidth = ScaleUi(116.0f);
            const float quickMenuButtonWidth = ScaleUi(134.0f);
            const float previousButtonWidth = ScaleUi(148.0f);
            const float nextButtonWidth = ScaleUi(136.0f);
            const float headerActionWidth = conditionsButtonWidth + quickMenuButtonWidth + previousButtonWidth + nextButtonWidth
                + itemSpacingX * 3.0f;
            alignRight(headerActionWidth);
            if (ImGui::Button(conditionsLabel.c_str(), ImVec2(conditionsButtonWidth, 0.0f))) {
                editor.conditionsPopupPending = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(quickMenuLabel.c_str(), ImVec2(quickMenuButtonWidth, 0.0f))) {
                editor.quickConditionsPopupPending = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(prevIndex < 0);
            if (ImGui::Button(previousLabel.c_str(), ImVec2(previousButtonWidth, 0.0f))) {
                RequestEditorAction(EditorState::PendingAction::Navigate, prevIndex);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(nextIndex < 0);
            if (ImGui::Button(nextLabel.c_str(), ImVec2(nextButtonWidth, 0.0f))) {
                RequestEditorAction(EditorState::PendingAction::Navigate, nextIndex);
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
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        editor.startSectionCollapsed ? ScaleUi(16.0f, 4.0f) : ScaleUi(16.0f, 14.0f));
    ImVec2 startPanelMin{};
    ImVec2 startPanelMax{};
    if (ImGui::BeginChild(
            "##binder_editor_start_panel",
            ImVec2(0.0f, 0.0f),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        if (!editor.startSectionCollapsed) {
            ImGui::SeparatorText(ui.Text(UiText::EditorStartSection));

            if (ImGui::BeginTable("##binder_editor_meta", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
                ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 0.58f);
                ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 0.42f);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", ui.Text(UiText::Name));
                if (editor.focusNamePending) {
                    ImGui::SetKeyboardFocusHere();
                    editor.focusNamePending = false;
                }
                ImGui::SetNextItemWidth(-FLT_MIN);
                InputTextString("##binder_editor_name", editor.draft.label, ImGuiInputTextFlags_AutoSelectAll, 160);

                ImGui::TableSetColumnIndex(1);
                ImGui::Dummy(ImVec2(0.0f, 0.0f));

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", ui.Text(UiText::ColumnHotkey));
                const std::string hotkeyText = editor.draft.keys.empty() ? ui.Text(UiText::HotkeyNotSet) : ::hotkeys::ToString(editor.draft.keys, editor.draft.hotkeyMode);
                if (ImGui::Button(hotkeyText.c_str(), ScaleUi(238.0f, 0.0f))) {
                    BeginCapture(CaptureTarget::BindHotkey);
                }
                ImGui::SameLine();
                const HotkeyMode hotkeyModes[] = { HotkeyMode::ModifierTrigger, HotkeyMode::OrderedCombo };
                const char* hotkeyModeLabels[] = { HotkeyModeLabel(HotkeyMode::ModifierTrigger), HotkeyModeLabel(HotkeyMode::OrderedCombo) };
                int hotkeyModeIndex = editor.draft.hotkeyMode == HotkeyMode::OrderedCombo ? 1 : 0;
                ImGui::SetNextItemWidth(ScaleUi(152.0f));
                if (ImGui::Combo("##binder_editor_hotkey_mode", &hotkeyModeIndex, hotkeyModeLabels, IM_ARRAYSIZE(hotkeyModeLabels))) {
                    editor.draft.hotkeyMode = hotkeyModes[hotkeyModeIndex];
                    editor.draft.keys = ::hotkeys::NormalizeCombo(editor.draft.keys, editor.draft.hotkeyMode);
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", ui.Text(UiText::Command));
                const ImVec2 commandIconSize(ScaleUi(20.0f), ImGui::GetFrameHeight());
                if (IconOnlyButton(
                        kIconTerminal,
                        "##binder_editor_command_enabled",
                        ui.Text(UiText::Command),
                        commandIconSize,
                        editor.draft.commandEnabled)) {
                    editor.draft.commandEnabled = !editor.draft.commandEnabled;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                InputTextString("##binder_editor_command", editor.draft.command, ImGuiInputTextFlags_AutoSelectAll, 128);

                ImGui::EndTable();
            }

            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", ui.Text(UiText::TextTrigger));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", ui.Text(UiText::EditorTriggerHint));
            }
            ImGui::SameLine();
            const ImVec2 triggerIconSize(ScaleUi(20.0f), ImGui::GetFrameHeight());
            if (IconOnlyButton(
                    kIconMessageDots,
                    "##binder_editor_trigger_enabled",
                    ui.Text(UiText::EditorTriggerToggleHint),
                    triggerIconSize,
                    editor.draft.textTrigger.enabled)) {
                editor.draft.textTrigger.enabled = !editor.draft.textTrigger.enabled;
            }
            ImGui::SameLine();
            if (IconOnlyButton(
                    kIconBracketsCurly,
                    "##binder_editor_trigger_pattern",
                    ui.Text(UiText::EditorTriggerPatternMode),
                    triggerIconSize,
                    editor.draft.textTrigger.pattern)) {
                editor.draft.textTrigger.pattern = !editor.draft.textTrigger.pattern;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            InputTextWithHintString(
                "##binder_editor_trigger",
                ui.Text(UiText::EditorTriggerExample),
                editor.draft.textTrigger.text,
                ImGuiInputTextFlags_AutoSelectAll,
                256);

            ImGui::Spacing();
            ImGui::Checkbox(ui.Text(UiText::ShowInQuickMenu), &editor.draft.quickMenu);
            ImGui::SameLine();
            ImGui::Checkbox(ui.Text(UiText::Repeat), &editor.draft.repeatMode);
            ImGui::SameLine();
            ImGui::BeginDisabled(!editor.draft.repeatMode);
            ImGui::SetNextItemWidth(ScaleUi(110.0f));
            ImGui::InputInt("##binder_editor_repeat", &editor.draft.repeatIntervalMs);
            if (editor.draft.repeatIntervalMs < 0) {
                editor.draft.repeatIntervalMs = 0;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Checkbox(ui.Text(UiText::TextConfirmation), &editor.draft.textConfirmation.enabled);
            if (editor.draft.textConfirmation.enabled) {
                ImGui::Spacing();
                ImGui::Checkbox(ui.Text(UiText::WaitWithoutTimeout), &editor.draft.textConfirmation.waitForResolution);

                if (ImGui::BeginTable("##binder_editor_confirmation_keys", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
                    ImGui::TableSetupColumn("confirm", ImGuiTableColumnFlags_WidthStretch, 0.5f);
                    ImGui::TableSetupColumn("cancel", ImGuiTableColumnFlags_WidthStretch, 0.5f);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled(
                        "%s",
                        ui.Format(UiText::ConfirmKeyFormat, ::hotkeys::KeyName(editor.draft.textConfirmation.key).c_str()).c_str());
                    if (ImGui::Button((std::string(kIconKeyboard) + " " + ui.Text(UiText::Change) + "##confirm").c_str(), ScaleUi(168.0f, 0.0f))) {
                        BeginCapture(CaptureTarget::ConfirmKey);
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled(
                        "%s",
                        ui.Format(UiText::CancelKeyFormat, ::hotkeys::KeyName(editor.draft.textConfirmation.cancelKey).c_str()).c_str());
                    if (ImGui::Button((std::string(kIconKeyboard) + " " + ui.Text(UiText::Change) + "##cancel").c_str(), ScaleUi(168.0f, 0.0f))) {
                        BeginCapture(CaptureTarget::CancelKey);
                    }

                    ImGui::EndTable();
                }
                ImGui::TextDisabled("%s", ui.Text(UiText::EditorConfirmationHint));
            }
        } else {
            ImGui::Dummy(ImVec2(0.0f, ScaleUi(2.0f)));
        }
    }
    ImGui::EndChild();
    startPanelMin = ImGui::GetItemRectMin();
    startPanelMax = ImGui::GetItemRectMax();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (!ImGui::IsPopupOpen(kEditorVariablesPopupId)) {
        const ImVec2 startPanelCursorRestore = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(
            std::floor(startPanelMin.x + (startPanelMax.x - startPanelMin.x - startToggleSize.x) * 0.5f),
            std::floor(startPanelMin.y - startToggleSize.y + ScaleUi(4.0f))));
        const char* startToggleIcon = editor.startSectionCollapsed ? kIconAngleDown : kIconAngleUp;
        const bool startToggleClicked = ImGui::InvisibleButton("##binder_editor_toggle_start_section", startToggleSize);
        const bool startToggleHovered = ImGui::IsItemHovered();
        const bool startToggleHeld = ImGui::IsItemActive();
        const ImVec2 startToggleMin = ImGui::GetItemRectMin();
        const ImVec2 startToggleMax = ImGui::GetItemRectMax();
        const ImVec2 startToggleIconSize = ImGui::CalcTextSize(startToggleIcon);
        ImVec2 startToggleIconPos(
            std::floor(startToggleMin.x + (startToggleSize.x - startToggleIconSize.x) * 0.5f),
            std::floor(startToggleMax.y - startToggleIconSize.y - ScaleUi(2.0f)));
        ImGuiCol startToggleColor = (startToggleHovered || startToggleHeld) ? ImGuiCol_Text : ImGuiCol_TextDisabled;
        ImGui::GetForegroundDrawList()->AddText(startToggleIconPos, ImGui::GetColorU32(startToggleColor), startToggleIcon);
        if (startToggleHovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s", ui.Text(editor.startSectionCollapsed ? UiText::EditorExpandStartSection : UiText::EditorCollapseStartSection));
        }
        if (startToggleClicked) {
            editor.startSectionCollapsed = !editor.startSectionCollapsed;
        }
        ImGui::SetCursorScreenPos(startPanelCursorRestore);
    }

    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, panelBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(8.0f, 10.0f));
    if (ImGui::BeginChild("##binder_editor_work", ImVec2(0.0f, -ScaleUi(50.0f)), ImGuiChildFlags_Borders)) {
        if (ImGui::BeginTabBar("##binder_editor_work_tabs")) {
            if (ImGui::BeginTabItem(
                    ui.Text(UiText::EditorScenarioTab),
                    nullptr,
                    editor.tabSelectionPending && editor.activeTab == EditorState::Tab::Scenario ? ImGuiTabItemFlags_SetSelected : 0)) {
                SetEditorTab(EditorState::Tab::Scenario);
                DrawEditorScenarioTab();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(
                    ui.Text(UiText::EditorMultiInputTab),
                    nullptr,
                    editor.tabSelectionPending && editor.activeTab == EditorState::Tab::MultiInput ? ImGuiTabItemFlags_SetSelected : 0)) {
                SetEditorTab(EditorState::Tab::MultiInput);

                bool bulkChanged = false;
                if (ImGui::BeginTable("##binder_editor_bulk_meta", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
                    ImGui::TableSetupColumn("method", ImGuiTableColumnFlags_WidthStretch, 0.70f);
                    ImGui::TableSetupColumn("interval", ImGuiTableColumnFlags_WidthStretch, 0.30f);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", ui.Text(UiText::EditorColumnDestination));
                    ImGui::SetNextItemWidth(ScaleUi(240.0f));
                    if (ImGui::BeginCombo("##binder_editor_bulk_method", SendMethodLabel(editor.bulkMethod))) {
                        for (int method = 0; method <= 9; ++method) {
                            const bool selected = method == editor.bulkMethod;
                            if (ImGui::Selectable(SendMethodLabel(method), selected)) {
                                editor.bulkMethod = method;
                                bulkChanged = true;
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled("%s", ui.Text(UiText::EditorColumnPauseMs));
                    ImGui::SetNextItemWidth(ScaleUi(110.0f));
                    if (ImGui::InputInt("##binder_editor_bulk_interval", &editor.bulkIntervalMs)) {
                        bulkChanged = true;
                    }
                    if (editor.bulkIntervalMs < 0) {
                        editor.bulkIntervalMs = 0;
                        bulkChanged = true;
                    }

                    ImGui::EndTable();
                }

                const float hintReserve = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
                const bool textChanged = InputTextMultilineString(
                    "##binder_editor_multi_text",
                    editor.multiText,
                    ImVec2(-FLT_MIN, std::max(ScaleUi(120.0f), ImGui::GetContentRegionAvail().y - hintReserve)),
                    0,
                    2048);
                if (bulkChanged || textChanged) {
                    ApplyEditorMultiToDraft(bulkChanged);
                }
                ImGui::TextDisabled("%s", ui.Text(UiText::EditorMultiInputHint));
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(
                    ui.Text(UiText::EditorInputFieldsTab),
                    nullptr,
                    editor.tabSelectionPending && editor.activeTab == EditorState::Tab::InputFields ? ImGuiTabItemFlags_SetSelected : 0)) {
                SetEditorTab(EditorState::Tab::InputFields);
                ImGui::TextDisabled("%s", ui.Text(UiText::EditorVariablesHint));
                ImGui::Spacing();
                DrawInputEditor();
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
    if (ImGui::BeginChild(
            "##binder_editor_footer",
            ImVec2(0.0f, 0.0f),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        if (ImGui::BeginTable("##binder_editor_footer_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 0.50f);
            ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 0.50f);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            if (ImGui::Button(variablesLabel.c_str(), ScaleUi(170.0f, 0.0f))) {
                editor.variablesPopupPending = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(previewLabel.c_str(), ScaleUi(170.0f, 0.0f))) {
                editor.previewPopupPending = true;
            }

            ImGui::TableSetColumnIndex(1);
            const float footerActionWidth = ScaleUi(190.0f) + ScaleUi(130.0f) + itemSpacingX;
            alignRight(footerActionWidth);
            pushPrimaryButtonStyle();
            if (ImGui::Button(saveLabel.c_str(), ScaleUi(190.0f, 0.0f))) {
                std::vector<std::string> errors;
                if (ValidateEditor(errors)) {
                    SaveEditor();
                    PushToast(ui.Text(UiText::ToastBindSaved), ImVec4(0.20f, 0.35f, 0.18f, 0.95f), 1800.0);
                } else {
                    for (const std::string& error : errors) {
                        PushToast(error, ImVec4(0.55f, 0.20f, 0.20f, 0.95f), 2800.0);
                    }
                }
            }
            popPrimaryButtonStyle();
            ImGui::SameLine();
            if (ImGui::Button(ui.Text(UiText::Cancel), ScaleUi(130.0f, 0.0f))) {
                RequestEditorAction(EditorState::PendingAction::Close);
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    DrawCapturePopup(true);
    DrawEditorConditionsPopup();
    DrawEditorQuickConditionsPopup();
    DrawEditorVariablesPopup();
    DrawEditorPreviewPopup();
    DrawEditorDiscardPopup();
}

void BinderModule::Impl::DrawEditor() {
    if (editor.active) {
        DrawEditorInline();
    }
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
    const float regularActionButtonGap = ScaleUi(4.0f);
    const float compactActionButtonGap = ScaleUi(4.0f);
    const float compactActionButtonSide = std::max(
        ScaleUi(10.0f),
        static_cast<float>(std::floor((actionButtonsWidth - compactActionButtonGap * 4.0f) / 5.0f)));
    const ImVec2 compactActionButtonSize(compactActionButtonSide, compactActionButtonSide);
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
            const LaunchCellContent launchContent = BuildLaunchCellContent(hotkey);
            const ImVec2 launchCellPos = ImGui::GetCursorScreenPos();
            const float launchCellWidth = ImGui::GetContentRegionAvail().x;
            const float launchCellHeight = ImGui::GetFrameHeight();
            const float launchPadX = ScaleUi(6.0f);
            const std::string launchLabel =
                EllipsizeText(launchContent.primary, std::max(0.0f, launchCellWidth - launchPadX * 2.0f));
            const ImVec2 launchLabelSize = ImGui::CalcTextSize(launchLabel.c_str());
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(
                    std::floor(launchCellPos.x + (launchCellWidth - launchLabelSize.x) * 0.5f),
                    std::floor(launchCellPos.y + (launchCellHeight - launchLabelSize.y) * 0.5f)),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                launchLabel.c_str());
            ImGui::InvisibleButton("##launch_summary", ImVec2(launchCellWidth, launchCellHeight));
            std::vector<std::string> launchTooltipLines;
            if (launchLabel != launchContent.primary) {
                launchTooltipLines.push_back(launchContent.primary);
            }
            launchTooltipLines.insert(
                launchTooltipLines.end(),
                launchContent.secondary.begin(),
                launchContent.secondary.end());
            if (!launchTooltipLines.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                const std::string launchTooltip = JoinLaunchLabels(launchTooltipLines, "\n");
                ImGui::SetTooltip("%s", launchTooltip.c_str());
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
            const bool isRunning = IsHotkeyRunning(index);
            const bool isPaused = IsHotkeyPaused(index);
            const ImVec2 currentActionButtonSize = isRunning ? compactActionButtonSize : iconButtonSize;
            const float currentActionButtonGap = isRunning ? compactActionButtonGap : regularActionButtonGap;
            const int actionButtonCount = isRunning ? 5 : 4;
            const float actionGroupWidth =
                currentActionButtonSize.x * static_cast<float>(actionButtonCount)
                + currentActionButtonGap * static_cast<float>(std::max(actionButtonCount - 1, 0));
            const ImVec2 actionCellPos = ImGui::GetCursorScreenPos();
            const float actionCellWidth = ImGui::GetContentRegionAvail().x;
            const float actionCellHeight = ImGui::GetFrameHeight();
            ImGui::SetCursorScreenPos(ImVec2(
                std::floor(actionCellPos.x + std::max(0.0f, (actionCellWidth - actionGroupWidth) * 0.5f)),
                std::floor(actionCellPos.y + std::max(0.0f, (actionCellHeight - currentActionButtonSize.y) * 0.5f))));
            if (!isRunning) {
                ImGui::BeginDisabled(!hotkey.enabled);
                if (SmallIconActionButton(kIconPlay, "##run", ui.Text(UiText::Run), currentActionButtonSize)) {
                    TryEnqueueHotkey(index, 0, "manual", "");
                }
                ImGui::EndDisabled();
            } else if (isPaused) {
                if (SmallIconActionButton(kIconPlay, "##resume", ui.Text(UiText::Resume), currentActionButtonSize)) {
                    ResumeHotkey(index);
                }
                ImGui::SameLine(0.0f, currentActionButtonGap);
                if (SmallIconActionButton(kIconStop, "##stop", ui.Text(UiText::Stop), currentActionButtonSize)) {
                    StopHotkey(index);
                }
            } else {
                if (SmallIconActionButton(kIconPause, "##pause", ui.Text(UiText::Pause), currentActionButtonSize)) {
                    PauseHotkey(index);
                }
                ImGui::SameLine(0.0f, currentActionButtonGap);
                if (SmallIconActionButton(kIconStop, "##stop", ui.Text(UiText::Stop), currentActionButtonSize)) {
                    StopHotkey(index);
                }
            }
            ImGui::SameLine(0.0f, currentActionButtonGap);
            if (SmallIconActionButton(kIconEdit, "##edit", ui.Text(UiText::Edit), currentActionButtonSize)) {
                StartEditing(index, false);
            }
            ImGui::SameLine(0.0f, currentActionButtonGap);
            if (SmallIconActionButton(kIconDelete, "##delete", ui.Text(UiText::Delete), currentActionButtonSize)) {
                bindDeleteTarget = index;
                bindDeletePopupPending = true;
            }
            ImGui::SameLine(0.0f, currentActionButtonGap);
            if (SmallIconActionButton(kIconBars, "##more", ui.Text(UiText::ColumnActions), currentActionButtonSize)) {
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
                StopHotkey(bindDeleteTarget);
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

        const std::string folderLabel = FormatFolderLabel(folder.name);
        const bool opened = ImGui::TreeNodeEx("##move_folder_node", flags, "%s", folderLabel.c_str());
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

    const HotkeyMode captureDisplayMode =
        captureTarget == CaptureTarget::BindHotkey && editor.active ? editor.draft.hotkeyMode : HotkeyMode::ModifierTrigger;

    hotkeys::DrawCapturePopupModal(
        "##binder_capture_popup",
        capturePopupState,
        capture,
        [this](const std::vector<UINT>& keys) {
            if (ApplyCapturedKeys(keys)) {
                capturePopupInEditor = false;
                return true;
            }
            return false;
        },
        true,
        captureDisplayMode,
        {},
        [this]() {
            captureTarget = CaptureTarget::None;
            captureHotkeyIndex = -1;
            capturePopupInEditor = false;
        });

    if (!capture.Active()) {
        capturePopupInEditor = false;
    }
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
        ResetQuickMenuVisualState();
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (quickMenuPos.x == 0.0f && quickMenuPos.y == 0.0f) {
        quickMenuSize = ScaleUi(static_cast<float>(kQuickMenuWidth), static_cast<float>(kQuickMenuHeight));
        quickMenuPos = ImVec2((io.DisplaySize.x - quickMenuSize.x) * 0.5f, (io.DisplaySize.y - quickMenuSize.y) * 0.5f);
    }

    const bool persistentOpen = quickMenuActivationMode == QuickMenuActivationMode::Toggle;
    bool windowOpen = true;
    int selectedHotkeyIndex = -1;
    bool hoveredAnyMenuWindow = false;
    bool submenuTriggerHovered = false;
    std::map<std::string, bool> shouldCloseSubmenu{};
    for (const auto& [path, _] : quickMenuSubmenuOpen) {
        shouldCloseSubmenu[path] = true;
    }

    const auto closeQuickMenuForSelection = [&]() {
        quickMenuOpen = false;
        quickMenuReopenBlocked = true;
        ResetQuickMenuVisualState();
    };

    const auto removeSubmenuState = [&](const std::string& path) {
        quickMenuSubmenuOpen.erase(path);
        quickMenuSubmenuPos.erase(path);
        quickMenuSubmenuNode.erase(path);
        quickMenuSubmenuCloseDeadline.erase(path);
    };

    const auto quickMenuItem = [&](const std::string& label, const char* shortcut, bool enabled) {
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
        const bool clicked = ImGui::MenuItem(label.c_str(), shortcut, false, enabled);
        ImGui::PopStyleVar();
        return clicked;
    };

    const auto drawNode = [&](auto&& self, FolderNode& node) -> void {
        if (selectedHotkeyIndex >= 0 || !FolderVisibleInQuickMenu(node)) {
            return;
        }

        for (const int index : QuickEntriesForFolder(node)) {
            if (selectedHotkeyIndex >= 0 || index < 0 || index >= static_cast<int>(hotkeys.size())) {
                break;
            }

            const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
            const std::string visibleLabel = std::string(kIconKeyboard) + " "
                + (hotkey.label.empty() ? UiSettings::Instance().Text(UiText::BinderDefaultHotkey) : hotkey.label);
            const std::string label = visibleLabel + "##quick_bind_" + std::to_string(index);
            const std::string shortcut = hotkey.keys.empty()
                ? std::string()
                : ::hotkeys::ToString(hotkey.keys, hotkey.hotkeyMode);
            if (quickMenuItem(label, shortcut.empty() ? nullptr : shortcut.c_str(), hotkey.enabled)) {
                selectedHotkeyIndex = index;
                closeQuickMenuForSelection();
                break;
            }
        }

        if (selectedHotkeyIndex >= 0) {
            return;
        }

        for (auto& child : node.children) {
            if (!child || !FolderHasVisibleQuickEntries(*child)) {
                continue;
            }

            const std::string path = JoinPath(BuildFolderPath(child.get()));
            const bool isOpen = quickMenuSubmenuOpen.contains(path);
            const std::string label = std::string(kIconFolder) + " " + child->name + " " + kIconAngleRight
                + "##quick_folder_" + path;

            ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
            ImGui::Selectable(label.c_str(), isOpen);
            ImGui::PopStyleVar();

            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            const float windowRight = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x;
            quickMenuSubmenuPos[path] = ImVec2(windowRight, itemMin.y - ScaleUi(10.0f));

            const bool pointerInRow = io.MousePos.x >= itemMin.x && io.MousePos.x <= windowRight
                && io.MousePos.y >= itemMin.y && io.MousePos.y <= itemMax.y;
            if (ImGui::IsItemHovered() || pointerInRow) {
                submenuTriggerHovered = true;
                quickMenuSubmenuOpen[path] = true;
                quickMenuSubmenuNode[path] = child.get();
                shouldCloseSubmenu[path] = false;
                quickMenuSubmenuCloseDeadline.erase(path);
            }
        }
    };

    ImGui::SetNextWindowPos(quickMenuPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(quickMenuSize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaleUi(4.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ScaleUi(4.0f, 4.0f));
    if (ImGui::Begin(
            UiSettings::Instance().Text(UiText::QuickMenuWindowTitle),
            persistentOpen ? &windowOpen : nullptr,
            ImGuiWindowFlags_NoCollapse)) {
        quickMenuPos = ImGui::GetWindowPos();
        quickMenuSize = ImGui::GetWindowSize();

        hoveredAnyMenuWindow = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        const int visibleCount = static_cast<int>(visibleFolders.size());
        if (visibleCount > 0) {
            const int clampedIndex = std::clamp(quickMenuTabIndex, 0, visibleCount - 1);
            if (clampedIndex != quickMenuTabIndex) {
                quickMenuTabIndex = clampedIndex;
                quickMenuTabSelectRequest = clampedIndex;
            }

            if (hoveredAnyMenuWindow) {
                const int scrollSteps = static_cast<int>(io.MouseWheel);
                if (scrollSteps != 0) {
                    quickMenuTabIndex += scrollSteps;
                    while (quickMenuTabIndex < 0) {
                        quickMenuTabIndex += visibleCount;
                    }
                    while (quickMenuTabIndex >= visibleCount) {
                        quickMenuTabIndex -= visibleCount;
                    }
                    quickMenuTabSelectRequest = quickMenuTabIndex;
                }
            }
        }

        if (ImGui::BeginTabBar("##quick_menu_tabs")) {
            for (std::size_t i = 0; i < visibleFolders.size(); ++i) {
                FolderNode& folder = *visibleFolders[i];
                ImGuiTabItemFlags flags = 0;
                if (quickMenuTabSelectRequest == static_cast<int>(i)) {
                    flags |= ImGuiTabItemFlags_SetSelected;
                }
                const std::string tabLabel = FormatFolderLabel(folder.name);
                if (ImGui::BeginTabItem(tabLabel.c_str(), nullptr, flags)) {
                    quickMenuTabIndex = static_cast<int>(i);
                    if (quickMenuTabSelectRequest == static_cast<int>(i)) {
                        quickMenuTabSelectRequest = -1;
                    }
                    drawNode(drawNode, folder);
                    ImGui::EndTabItem();
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (quickMenuTabIndex != static_cast<int>(i)) {
                        quickMenuTabIndex = static_cast<int>(i);
                    }
                    quickMenuTabSelectRequest = static_cast<int>(i);
                }
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);

    if (persistentOpen && !windowOpen) {
        closeQuickMenuForSelection();
        return;
    }

    if (selectedHotkeyIndex >= 0) {
        TryEnqueueHotkey(selectedHotkeyIndex, 0, "quick_menu", "");
        return;
    }

    quickMenuSubmenuPaths.clear();
    quickMenuSubmenuPaths.reserve(quickMenuSubmenuOpen.size());
    for (const auto& [path, _] : quickMenuSubmenuOpen) {
        quickMenuSubmenuPaths.push_back(path);
    }

    const ImGuiWindowFlags submenuFlags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoNav;

    for (const std::string& path : quickMenuSubmenuPaths) {
        FolderNode* node = nullptr;
        ImVec2* position = nullptr;

        if (const auto nodeIt = quickMenuSubmenuNode.find(path); nodeIt != quickMenuSubmenuNode.end()) {
            node = nodeIt->second;
        }
        if (const auto posIt = quickMenuSubmenuPos.find(path); posIt != quickMenuSubmenuPos.end()) {
            position = &posIt->second;
        }
        if (!node || !position) {
            continue;
        }

        ImGui::SetNextWindowPos(*position, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(8.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaleUi(4.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ScaleUi(4.0f, 4.0f));
        if (ImGui::Begin(("##quick_menu_sub_" + path).c_str(), nullptr, submenuFlags)) {
            drawNode(drawNode, *node);
            const ImGuiHoveredFlags hoveredFlags = ImGuiHoveredFlags_RootAndChildWindows
                | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem;
            if (ImGui::IsWindowHovered(hoveredFlags)) {
                shouldCloseSubmenu[path] = false;
                hoveredAnyMenuWindow = true;
                quickMenuSubmenuCloseDeadline.erase(path);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);

        if (selectedHotkeyIndex >= 0) {
            break;
        }
    }

    if (selectedHotkeyIndex >= 0) {
        TryEnqueueHotkey(selectedHotkeyIndex, 0, "quick_menu", "");
        return;
    }

    for (int pass = 0; pass < 3; ++pass) {
        for (const std::string& path : quickMenuSubmenuPaths) {
            auto closeIt = shouldCloseSubmenu.find(path);
            if (closeIt == shouldCloseSubmenu.end() || !closeIt->second) {
                continue;
            }

            FolderNode* node = nullptr;
            if (const auto nodeIt = quickMenuSubmenuNode.find(path); nodeIt != quickMenuSubmenuNode.end()) {
                node = nodeIt->second;
            }
            if (!node) {
                continue;
            }

            for (const auto& child : node->children) {
                if (!child) {
                    continue;
                }
                const std::string childPath = JoinPath(BuildFolderPath(child.get()));
                if (!quickMenuSubmenuOpen.contains(childPath)) {
                    continue;
                }

                const auto childCloseIt = shouldCloseSubmenu.find(childPath);
                if (childCloseIt != shouldCloseSubmenu.end() && !childCloseIt->second) {
                    closeIt->second = false;
                    break;
                }
            }
        }
    }

    const double now = ImGui::GetTime();
    for (const auto& [path, shouldClose] : shouldCloseSubmenu) {
        if (!shouldClose) {
            quickMenuSubmenuCloseDeadline.erase(path);
            continue;
        }

        if (submenuTriggerHovered) {
            removeSubmenuState(path);
            continue;
        }

        if (hoveredAnyMenuWindow) {
            quickMenuSubmenuCloseDeadline[path] = now + kQuickMenuSubmenuCloseGraceSeconds;
            continue;
        }

        const auto deadlineIt = quickMenuSubmenuCloseDeadline.find(path);
        if (deadlineIt == quickMenuSubmenuCloseDeadline.end()) {
            quickMenuSubmenuCloseDeadline[path] = now + kQuickMenuSubmenuCloseGraceSeconds;
            continue;
        }
        if (now >= deadlineIt->second) {
            removeSubmenuState(path);
        }
    }

    if (!windowOpen) {
        quickMenuOpen = false;
        quickMenuReopenBlocked = true;
        ResetQuickMenuVisualState();
    }
}

void BinderModule::Impl::DrawInputDialog() {
    if (inputDialog) {
        const bool compactDialog = inputDialog->fields.size() == 1;
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const ImVec2 minWindowSize = compactDialog ? ScaleUi(420.0f, 280.0f) : ScaleUi(560.0f, 360.0f);
        const ImVec2 preferredWindowSize = compactDialog ? ScaleUi(560.0f, 460.0f) : ScaleUi(720.0f, 560.0f);
        const ImVec2 maxWindowSize(
            std::max(minWindowSize.x, displaySize.x - ScaleUi(24.0f)),
            std::max(minWindowSize.y, displaySize.y - ScaleUi(24.0f)));
        ImGui::SetNextWindowSizeConstraints(minWindowSize, maxWindowSize);
        ImGui::SetNextWindowSize(
            ImVec2(std::min(preferredWindowSize.x, maxWindowSize.x), std::min(preferredWindowSize.y, maxWindowSize.y)),
            ImGuiCond_Appearing);
        ImGui::OpenPopup("##binder_input_dialog");
    }

    if (!ImGui::BeginPopupModal("##binder_input_dialog", nullptr)) {
        return;
    }

    if (!inputDialog || inputDialog->hotkeyIndex < 0 || inputDialog->hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        inputDialog.reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    HotkeyEntry& hotkey = hotkeys[inputDialog->hotkeyIndex];
    const bool compactDialog = inputDialog->fields.size() == 1;
    const bool dialogAppearing = ImGui::IsWindowAppearing();
    bool focusAssigned = false;
    bool submitRequested = false;
    bool cancelRequested = false;
    const bool focusSearchShortcut =
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F, ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteOverActive);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Enter, ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteOverActive)
        || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadEnter, ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteOverActive)) {
        submitRequested = true;
    }
    if (ImGui::Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteOverActive)) {
        cancelRequested = true;
    }

    ImGui::TextWrapped("%s", ui.Format(UiText::FillBindParametersFormat, hotkey.label.c_str()).c_str());
    ImGui::Separator();

    auto rebuildSelectedText = [](InputDialogField& field) {
        std::ostringstream stream;
        bool first = true;
        for (const int idx : field.selectedButtons) {
            if (idx < 0 || idx >= static_cast<int>(field.input.buttons.size())) {
                continue;
            }
            const InputButton& button = field.input.buttons[static_cast<std::size_t>(idx)];
            if (button.text.empty()) {
                continue;
            }
            if (!first) {
                stream << (field.input.multiSeparator.empty() ? ", " : field.input.multiSeparator);
            }
            stream << button.text;
            first = false;
        }
        field.textValue = stream.str();
    };
    auto dialogButtonLabel = [](const InputButton& button, int buttonIndex) {
        const std::string label = Trim(button.label);
        if (!label.empty()) {
            return label;
        }
        const std::string text = Trim(button.text);
        if (!text.empty()) {
            return text;
        }
        return std::to_string(buttonIndex + 1);
    };
    const auto dialogButtonMatchesQuery = [&](const InputButton& button, int buttonIndex, std::string_view query) {
        const std::string normalizedQuery = ToLower(Trim(query));
        if (normalizedQuery.empty()) {
            return true;
        }

        const std::string label = ToLower(dialogButtonLabel(button, buttonIndex));
        if (label.find(normalizedQuery) != std::string::npos) {
            return true;
        }
        if (ToLower(button.text).find(normalizedQuery) != std::string::npos) {
            return true;
        }
        return ToLower(button.hint).find(normalizedQuery) != std::string::npos;
    };
    const auto drawPreviewBlock = [&](bool compactPreview) {
        std::map<std::string, std::string> values;
        for (std::size_t fieldIndex = 0; fieldIndex < inputDialog->fields.size(); ++fieldIndex) {
            const InputDialogField& previewField = inputDialog->fields[fieldIndex];
            const std::string key = NormalizeInputKey(previewField.input.key);
            const std::string value = BuildInputValue(previewField);
            if (!key.empty()) {
                values[key] = value;
                values[ToLower(key)] = value;
            }
            values[std::to_string(fieldIndex + 1)] = value;
        }

        ImGui::SeparatorText(ui.Text(UiText::InputDialogPreviewTitle));
        const ImVec2 previewSize = compactPreview ? ImVec2(0.0f, ScaleUi(104.0f)) : ImVec2(0.0f, ScaleUi(124.0f));
        if (!ImGui::BeginChild("##binder_input_preview", previewSize, ImGuiChildFlags_FrameStyle)) {
            ImGui::EndChild();
            return;
        }

        bool hasPreviewRows = false;
        for (std::size_t messageIndex = 0; messageIndex < hotkey.messages.size(); ++messageIndex) {
            const HotkeyMessage& message = hotkey.messages[messageIndex];
            std::string previewText = ApplyInputValues(message.text, values);
            if (tagsModule) {
                previewText = tagsModule->ExpandText(previewText, TagsModule::EvaluationContext{
                                                                     sampApi,
                                                                     inputDialog->activationSource,
                                                                     inputDialog->activationText,
                                                                     inputDialog->bindCommand,
                                                                     false,
                                                                 });
            }
            std::replace(previewText.begin(), previewText.end(), '\r', ' ');
            std::replace(previewText.begin(), previewText.end(), '\n', ' ');
            if (Trim(previewText).empty()) {
                continue;
            }

            hasPreviewRows = true;
            ImGui::TextWrapped(
                "%d. [%s] %s",
                static_cast<int>(messageIndex + 1),
                SendMethodLabel(message.method),
                previewText.c_str());
        }

        if (!hasPreviewRows) {
            ImGui::TextDisabled("%s", ui.Text(UiText::InputDialogPreviewEmpty));
        }
        ImGui::EndChild();
    };
    const auto applyButtonSelection = [&](InputDialogField& field, int buttonIndex) {
        const InputButton& button = field.input.buttons[static_cast<std::size_t>(buttonIndex)];
        if (field.input.multiSelect) {
            field.selectedButtonIndex.reset();
            if (field.selectedButtons.contains(buttonIndex)) {
                field.selectedButtons.erase(buttonIndex);
            } else {
                field.selectedButtons.insert(buttonIndex);
            }
            rebuildSelectedText(field);
        } else {
            field.selectedButtons.clear();
            field.selectedButtonIndex = buttonIndex;
            if (field.input.mode == InputMode::ButtonsListText) {
                field.textValue = button.text;
            }
        }
    };
    const auto drawSearchInput = [&](InputDialogField& field, bool wantFocus) {
        if (wantFocus) {
            ImGui::SetKeyboardFocusHere();
            focusAssigned = true;
        }
        ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_F, ImGuiInputFlags_Tooltip);
        InputTextWithHintString(
            "##input_search",
            ui.Text(UiText::InputDialogSearchHint),
            field.searchValue,
            ImGuiInputTextFlags_AutoSelectAll,
            128);
    };
    const auto drawRegularButtonList = [&](InputDialogField& field, const std::vector<int>& shownButtons) -> bool {
        bool picked = false;
        if (ImGui::BeginChild("##input_buttons", ImVec2(0.0f, ScaleUi(160.0f)), ImGuiChildFlags_FrameStyle)) {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(shownButtons.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const int buttonIndex = shownButtons[static_cast<std::size_t>(row)];
                    const InputButton& button = field.input.buttons[static_cast<std::size_t>(buttonIndex)];
                    const std::string label = dialogButtonLabel(button, buttonIndex);
                    const bool selected =
                        field.input.multiSelect ? field.selectedButtons.contains(buttonIndex) : field.selectedButtonIndex.value_or(-1) == buttonIndex;
                    if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(-FLT_MIN, 0.0f))) {
                        applyButtonSelection(field, buttonIndex);
                        picked = true;
                    }
                    if (!button.hint.empty() && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", button.hint.c_str());
                    }
                }
            }
            if (shownButtons.empty()) {
                ImGui::TextDisabled("%s", ui.Text(UiText::InputDialogNoOptions));
            }
        }
        ImGui::EndChild();
        return picked;
    };
    const auto drawCompactButtonList = [&](InputDialogField& field, const std::vector<int>& shownButtons) -> bool {
        bool picked = false;
        const bool useTwoColumns = shownButtons.size() >= 4 && ImGui::GetContentRegionAvail().x >= ScaleUi(420.0f);
        if (ImGui::BeginChild("##input_buttons_compact", ImVec2(0.0f, ScaleUi(176.0f)), ImGuiChildFlags_FrameStyle)) {
            const float spacingX = ImGui::GetStyle().ItemSpacing.x;
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const float buttonWidth = useTwoColumns ? std::max(ScaleUi(120.0f), (availableWidth - spacingX) * 0.5f) : -FLT_MIN;
            for (std::size_t shownIndex = 0; shownIndex < shownButtons.size(); ++shownIndex) {
                const int buttonIndex = shownButtons[shownIndex];
                const InputButton& button = field.input.buttons[static_cast<std::size_t>(buttonIndex)];
                const bool selected =
                    field.input.multiSelect ? field.selectedButtons.contains(buttonIndex) : field.selectedButtonIndex.value_or(-1) == buttonIndex;
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                }
                if (ImGui::Button(dialogButtonLabel(button, buttonIndex).c_str(), ImVec2(buttonWidth, 0.0f))) {
                    applyButtonSelection(field, buttonIndex);
                    picked = true;
                }
                if (selected) {
                    ImGui::PopStyleColor(2);
                }
                if (!button.hint.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", button.hint.c_str());
                }
                if (useTwoColumns && (shownIndex % 2) == 0 && shownIndex + 1 < shownButtons.size()) {
                    ImGui::SameLine();
                }
            }
            if (shownButtons.empty()) {
                ImGui::TextDisabled("%s", ui.Text(UiText::InputDialogNoOptions));
            }
        }
        ImGui::EndChild();
        return picked;
    };
    const auto drawFieldTextEditor = [&](InputDialogField& field, bool wantFocus, bool compactEditor) {
        if (wantFocus) {
            ImGui::SetKeyboardFocusHere();
            focusAssigned = true;
        }
        const float height = compactEditor ? ScaleUi(148.0f) : ScaleUi(112.0f);
        const bool changed = InputTextMultilineWithCounterString(
            "##input_text_value",
            field.textValue,
            ImVec2(-FLT_MIN, height),
            ImGuiInputTextFlags_AllowTabInput,
            512);
        if (changed && field.input.mode == InputMode::ButtonsListText) {
            field.selectedButtonIndex.reset();
            field.selectedButtons.clear();
        }
    };
    const auto submitDialog = [&]() {
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

        StartRunningBind(
            hotkey,
            std::move(values),
            inputDialog->startDelayMs,
            inputDialog->activationSource,
            inputDialog->activationText,
            inputDialog->bindCommand);
        hotkey.awaitingInput = false;
        inputDialog.reset();
        ImGui::CloseCurrentPopup();
    };
    const auto cancelDialog = [&]() {
        hotkey.awaitingInput = false;
        inputDialog.reset();
        ImGui::CloseCurrentPopup();
    };

    for (std::size_t i = 0; i < inputDialog->fields.size(); ++i) {
        InputDialogField& field = inputDialog->fields[i];
        ImGui::PushID(static_cast<int>(i));
        if (!compactDialog || i == 0) {
            const std::string title = field.input.label.empty() ? field.input.key : field.input.label;
            ImGui::TextWrapped("%s", title.c_str());
            ImGui::Separator();
        }
        if (!field.input.hint.empty()) {
            ImGui::TextWrapped("%s", field.input.hint.c_str());
        }

        if (field.input.mode == InputMode::Text) {
            drawFieldTextEditor(field, !focusAssigned && dialogAppearing, compactDialog);
        } else {
            const auto filteredButtons = FilterButtons(*inputDialog, i);
            std::set<int> visibleButtons(filteredButtons.begin(), filteredButtons.end());
            if (field.input.multiSelect) {
                bool selectionChanged = false;
                for (auto it = field.selectedButtons.begin(); it != field.selectedButtons.end();) {
                    if (!visibleButtons.contains(*it)) {
                        it = field.selectedButtons.erase(it);
                        selectionChanged = true;
                    } else {
                        ++it;
                    }
                }
                if (selectionChanged) {
                    rebuildSelectedText(field);
                }
            } else if (field.selectedButtonIndex.has_value() && !visibleButtons.contains(*field.selectedButtonIndex)) {
                field.selectedButtonIndex.reset();
            }

            drawSearchInput(
                field,
                !focusAssigned && InputModeUsesButtons(field.input.mode) && (dialogAppearing || focusSearchShortcut));

            std::vector<int> shownButtons;
            shownButtons.reserve(filteredButtons.size());
            for (const int buttonIndex : filteredButtons) {
                const InputButton& button = field.input.buttons[static_cast<std::size_t>(buttonIndex)];
                if (dialogButtonMatchesQuery(button, buttonIndex, field.searchValue)) {
                    shownButtons.push_back(buttonIndex);
                }
            }

            const bool picked = compactDialog ? drawCompactButtonList(field, shownButtons) : drawRegularButtonList(field, shownButtons);
            if (compactDialog && picked && field.input.mode == InputMode::ButtonsList && !field.input.multiSelect) {
                submitRequested = true;
            }

            if (field.input.mode == InputMode::ButtonsListText) {
                drawFieldTextEditor(field, false, compactDialog);
            }
        }

        if (!compactDialog && i + 1 != inputDialog->fields.size()) {
            ImGui::Spacing();
        }
        ImGui::PopID();
    }

    drawPreviewBlock(compactDialog);
    ImGui::Separator();
    ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_Enter, ImGuiInputFlags_Tooltip);
    if (ImGui::Button(ui.Text(UiText::Launch))) {
        submitRequested = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemShortcut(ImGuiKey_Escape, ImGuiInputFlags_Tooltip);
    if (ImGui::Button(ui.Text(UiText::Cancel))) {
        cancelRequested = true;
    }

    if (submitRequested) {
        submitDialog();
    } else if (cancelRequested) {
        cancelDialog();
    }

    ImGui::EndPopup();
}

void BinderModule::Impl::DrawMainTab() {
    EnsureInitialized();

    if (editor.active) {
        DrawEditor();
        DrawFolderPopups();
        DrawMoveBindPopup();
        return;
    }

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
    DrawBindLinesPopup();
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

void BinderModule::SetTagsModule(TagsModule* tagsModule) {
    impl_->SetTagsModule(tagsModule);
}

void BinderModule::Tick() {
    impl_->Tick();
}

void BinderModule::Shutdown() {
    impl_->Shutdown();
}

std::string BinderModule::GetThisbindTagValue(std::uint64_t runtimeId) const {
    return impl_->BuildThisbindTagValue(runtimeId);
}

bool BinderModule::IsRuntimeActive(std::uint64_t runtimeId) const {
    return impl_->IsRuntimeActive(runtimeId);
}

bool BinderModule::IsRuntimePaused(std::uint64_t runtimeId) const {
    return impl_->IsRuntimePaused(runtimeId);
}

bool BinderModule::PauseRuntime(std::uint64_t runtimeId) {
    return impl_->PauseRuntime(runtimeId);
}

bool BinderModule::ResumeRuntime(std::uint64_t runtimeId) {
    return impl_->ResumeRuntime(runtimeId);
}

bool BinderModule::StopRuntime(std::uint64_t runtimeId) {
    return impl_->StopRuntime(runtimeId);
}

BinderModule::TagActionResult BinderModule::ExecuteTagAction(
    std::string_view action,
    std::string_view param,
    std::uint64_t sourceRuntimeId) {
    return impl_->ExecuteTagAction(action, param, sourceRuntimeId);
}

void BinderModule::ShowToast(std::string_view text, bool error, double durationMs) {
    if (!impl_ || text.empty()) {
        return;
    }

    impl_->PushToast(
        std::string(text),
        error ? ImVec4(0.55f, 0.20f, 0.20f, 0.95f) : ImVec4(0.20f, 0.35f, 0.18f, 0.95f),
        durationMs);
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

bool BinderModule::DescribeMainWindowHotkeyConflict(const std::vector<unsigned int>& keys, std::string& description) {
    return impl_->DescribeMainWindowHotkeyConflict(keys, description);
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
