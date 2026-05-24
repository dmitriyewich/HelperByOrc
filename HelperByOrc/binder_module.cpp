#include "binder_module.h"

#include "app_config.h"
#include "conditions_module.h"
#include "debug_log.h"
#include "hotkey_utils.h"
#include "incoming_message_router.h"
#include "json_utils.h"
#include "notification_manager.h"
#include "samp_api.h"
#include "samp_hooks.h"
#include "samp_rak_hooks.h"
#include "tags_module.h"
#include "text_encoding.h"
#include "ui_icons.h"
#include "ui_settings.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
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
constexpr int kMinMessageIntervalMs = 0;
constexpr double kHotkeyDebounceMs = 0.0;
constexpr int kDefaultRepeatIntervalMs = 500;
constexpr int kQuickMenuWidth = 214;
constexpr int kQuickMenuHeight = 277;
// Строка id для OpenPopup/BeginPopup: только ## — без заголовка в строке окна, заголовок рисуем внутри.
constexpr char kQuickMenuHostPopupId[] = "##helperbyorc_qm_host";
constexpr int kTextConfirmTimeoutMs = 5000;
constexpr int kOutgoingGuardTimeoutMs = 2000;
constexpr int kIncomingChatEchoGuardTimeoutMs = 1000;
constexpr char kDialogCaptionLocalChatColorTag[] = "{E2C063}";
constexpr char kDialogSelectionLocalChatColorTag[] = "{E2C063}";
constexpr char kBindDragPayload[] = "BINDER_HOTKEY_INDEX";
constexpr char kFolderDragPayload[] = "BINDER_FOLDER_ID";

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

bool ToggleChip(const char* icon, const char* label, const char* id, bool& value, float width = 0.0f) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const std::string text = std::string(icon) + " " + label;
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    const ImVec2 size(
        width > 0.0f ? width : std::ceil(textSize.x + style.FramePadding.x * 2.0f + style.ItemInnerSpacing.x),
        ImGui::GetFrameHeight());

    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    if (clicked) {
        value = !value;
    }

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImU32 bgColor = value
        ? ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_ButtonActive)
        : ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    const ImU32 borderColor = value
        ? ImGui::GetColorU32(ImVec4(0.48f, 0.63f, 0.96f, 0.95f))
        : ImGui::GetColorU32(ImGuiCol_Border);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, bgColor, style.FrameRounding);
    drawList->AddRect(min, max, borderColor, style.FrameRounding, 0, std::max(1.0f, style.FrameBorderSize));

    const ImVec2 textPos(
        std::floor(min.x + (size.x - textSize.x) * 0.5f),
        std::floor(min.y + (size.y - textSize.y) * 0.5f));
    drawList->AddText(textPos, ImGui::GetColorU32(value ? ImGuiCol_Text : ImGuiCol_TextDisabled), text.c_str());
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
        return ui_icons::Folder;
    }
    return std::string(ui_icons::Folder) + " " + std::string(name);
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

enum class QuickMenuStyle {
    Tree = 0,
    Cascade = 1,
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

struct CommandConfirmation {
    bool enabled = false;
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
    CommandConfirmation commandConfirmation;
    std::vector<bool> conditions;
    ConditionCombineMode conditionsCombine = ConditionCombineMode::RequireAny;
    bool repeatMode = false;
    int repeatIntervalMs = 0;
    bool enabled = true;
    bool quickMenu = false;
    std::string command;
    bool commandEnabled = false;
    std::string categoryId;
    std::vector<std::string> folderPath;
    std::string orderId;
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

std::string BuildBindDisplayLabel(const HotkeyEntry& hotkey) {
    std::string label = Trim(hotkey.label);
    if (!label.empty()) {
        return label;
    }

    for (const HotkeyMessage& message : hotkey.messages) {
        label = Trim(message.text);
        if (!label.empty()) {
            while (CountUtf8Codepoints(label) > 48) {
                label = Utf8TrimLastChar(label);
            }
            if (Trim(message.text) != label) {
                label += "...";
            }
            return label;
        }
    }

    return UiSettings::Instance().Text(UiText::BinderDefaultHotkey);
}

UiText ConfirmationSourceLabelId(std::string_view sourceKind) {
    return sourceKind == "command" ? UiText::EditorToggleCommandConfirm : UiText::EditorToggleTextConfirm;
}

const char* ConfirmationSourceLabel(std::string_view sourceKind) {
    return UiSettings::Instance().Text(ConfirmationSourceLabelId(sourceKind));
}

bool HasRequiredFirstMessage(const HotkeyEntry& hotkey) {
    return !hotkey.messages.empty() && !Trim(hotkey.messages.front().text).empty();
}

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
        AppendLaunchLabel(labels, ui_icons::Keyboard, ::hotkeys::ToString(hotkey.keys, hotkey.hotkeyMode));
    }

    const std::string commandText = Trim(hotkey.command);
    if (hotkey.commandEnabled && !commandText.empty()) {
        AppendLaunchLabel(labels, ui_icons::Terminal, commandText);
    }

    const std::string triggerText = Trim(hotkey.textTrigger.text);
    if (hotkey.textTrigger.enabled && !triggerText.empty()) {
        AppendLaunchLabel(labels, ui_icons::Comment, triggerText);
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

enum class ExplorerItemKind {
    Folder,
    Bind,
};

enum class ExplorerSelectionKind {
    None,
    Folder,
    Bind,
};

struct ExplorerItem {
    ExplorerItemKind kind = ExplorerItemKind::Folder;
    std::string key;
};

struct ExplorerSelection {
    ExplorerSelectionKind kind = ExplorerSelectionKind::None;
    int folderId = 0;
    std::string bindOrderId{};
};

struct ExplorerListLayout {
    float width = 0.0f;
    float rowHeight = 0.0f;
    float headerHeight = 0.0f;
    float iconX = 0.0f;
    float iconW = 0.0f;
    float nameX = 0.0f;
    float nameW = 0.0f;
    float actionsX = 0.0f;
    float actionsW = 0.0f;
};

struct FolderNode {
    int id = 0;
    std::string name;
    FolderNode* parent = nullptr;
    std::vector<std::unique_ptr<FolderNode>> children;
    std::vector<ExplorerItem> items;
    std::vector<bool> conditions;
    ConditionCombineMode conditionsCombine = ConditionCombineMode::RequireAny;
    bool quickMenu = true;
    bool open = true;
};

struct BinderCategory {
    std::string id;
    std::string name;
    bool quickMenu = true;
    std::vector<bool> conditions;
    ConditionCombineMode conditionsCombine = ConditionCombineMode::RequireAny;
    std::vector<std::unique_ptr<FolderNode>> folders;
    std::vector<ExplorerItem> rootItems;
    std::vector<std::string> lastOpenFolderPath;
    std::vector<std::vector<std::string>> navigationBackStack;
};

struct OutgoingGuard {
    std::string kind;
    std::string text;
    double expiresAtMs = 0.0;
};

struct IncomingChatEchoGuard {
    std::string text;
    std::string localPlayerName;
    double expiresAtMs = 0.0;
};

struct RunningBind {
    std::uint64_t hotkeyRuntimeId = 0;
    std::string categoryId;
    std::string categoryName;
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
    std::string category{};
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

namespace {

template <typename Container>
auto FindRunningBindByRuntimeId(Container& runningBinds, std::uint64_t hotkeyRuntimeId)
    -> decltype(runningBinds.empty() ? nullptr : std::addressof(runningBinds.front())) {
    if (hotkeyRuntimeId == 0) {
        return nullptr;
    }

    const auto it = std::find_if(runningBinds.begin(), runningBinds.end(), [&](const auto& running) {
        return running.hotkeyRuntimeId == hotkeyRuntimeId;
    });
    return it == runningBinds.end() ? nullptr : std::addressof(*it);
}

std::uint64_t HotkeyRuntimeIdAt(const std::vector<HotkeyEntry>& hotkeys, int index) {
    if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
        return 0;
    }
    return hotkeys[static_cast<std::size_t>(index)].runtimeId;
}

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

bool EndsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
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

QuickMenuStyle NormalizeQuickMenuStyle(std::string_view value) {
    const std::string normalized = ToLower(value);
    if (normalized == "tree" || normalized == "style1" || normalized == "1") {
        return QuickMenuStyle::Tree;
    }
    // Legacy style ids from older configs now map to the single cascade branch.
    if (normalized == "cascade" || normalized == "style2" || normalized == "style3" || normalized == "2"
        || normalized == "3") {
        return QuickMenuStyle::Cascade;
    }
    return QuickMenuStyle::Cascade;
}

std::string QuickMenuStyleId(QuickMenuStyle style) {
    switch (style) {
    case QuickMenuStyle::Tree:
        return "tree";
    case QuickMenuStyle::Cascade:
        return "cascade";
    }
    return "cascade";
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

bool IsLegacyRootFolderName(std::string_view name) {
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

} // namespace

namespace {

struct FolderListPos {
    std::vector<std::unique_ptr<FolderNode>>* list = nullptr;
    int index = 0;
    FolderNode* listParent = nullptr;
};

enum class FolderDropZone {
    Before,
    Into,
    After,
};

struct FolderMoveUndo {
    int nodeId = 0;
    int oldListParentId = -1; // -1 = root `folders` list, >0 = id of parent folder
    int oldIndex = 0;
};

FolderDropZone ResolveFolderDropZone(const ImRect& rect) {
    const float height = std::max(1.0f, rect.GetHeight());
    const float edge = std::min(height * 0.40f, std::max(ScaleUi(4.0f), height * 0.28f));
    const float mouseY = ImGui::GetIO().MousePos.y;
    if (mouseY < rect.Min.y + edge) {
        return FolderDropZone::Before;
    }
    if (mouseY > rect.Max.y - edge) {
        return FolderDropZone::After;
    }
    return FolderDropZone::Into;
}

void DrawFolderDropPreview(const ImRect& rect, FolderDropZone zone) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
    ImVec4 glow = ImGui::ColorConvertU32ToFloat4(color);
    glow.w = std::min(0.40f, glow.w * 0.26f);
    if (zone == FolderDropZone::Into) {
        const ImRect inner(
            ImVec2(rect.Min.x + ScaleUi(2.0f), rect.Min.y + ScaleUi(2.0f)),
            ImVec2(rect.Max.x - ScaleUi(2.0f), rect.Max.y - ScaleUi(2.0f)));
        drawList->AddRectFilled(inner.Min, inner.Max, ImGui::GetColorU32(glow), ScaleUi(3.0f));
        drawList->AddRect(inner.Min, inner.Max, color, ScaleUi(3.0f), 0, ScaleUi(2.0f));
        drawList->AddRectFilled(
            ImVec2(inner.Min.x, inner.Min.y),
            ImVec2(inner.Min.x + ScaleUi(4.0f), inner.Max.y),
            color,
            ScaleUi(2.0f));
        return;
    }

    const float y = zone == FolderDropZone::Before ? rect.Min.y : rect.Max.y;
    const float snappedY = std::floor(y) + 0.5f;
    drawList->AddRectFilled(
        ImVec2(rect.Min.x, snappedY - ScaleUi(5.0f)),
        ImVec2(rect.Max.x, snappedY + ScaleUi(5.0f)),
        ImGui::GetColorU32(glow),
        ScaleUi(2.0f));
    drawList->AddRectFilled(
        ImVec2(rect.Min.x, snappedY - ScaleUi(2.0f)),
        ImVec2(rect.Max.x, snappedY + ScaleUi(2.0f)),
        color,
        ScaleUi(1.0f));
    drawList->AddCircleFilled(ImVec2(rect.Min.x + ScaleUi(5.0f), snappedY), ScaleUi(4.0f), color);
}

const ImGuiPayload* ActiveExplorerDragPayload() {
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (payload == nullptr) {
        return nullptr;
    }
    if (!payload->IsDataType(kBindDragPayload) && !payload->IsDataType(kFolderDragPayload)) {
        return nullptr;
    }
    return payload;
}

bool IsExplorerBindDragPayload(const ImGuiPayload* payload) {
    return payload != nullptr
        && payload->IsDataType(kBindDragPayload)
        && payload->Data != nullptr
        && payload->DataSize == sizeof(int);
}

bool TryGetExplorerFolderDragId(const ImGuiPayload* payload, int& folderId) {
    if (payload == nullptr
        || !payload->IsDataType(kFolderDragPayload)
        || payload->Data == nullptr
        || payload->DataSize != sizeof(int)) {
        return false;
    }
    folderId = *static_cast<const int*>(payload->Data);
    return true;
}

FolderNode* FindFolderByIdR(std::vector<std::unique_ptr<FolderNode>>& from, int id) {
    for (auto& u : from) {
        if (!u) {
            continue;
        }
        if (u->id == id) {
            return u.get();
        }
        if (FolderNode* child = FindFolderByIdR(u->children, id)) {
            return child;
        }
    }
    return nullptr;
}

bool FindFolderListPos(
    std::vector<std::unique_ptr<FolderNode>>& from,
    FolderNode* listParent,
    int id,
    FolderListPos& out) {
    for (int i = 0; i < static_cast<int>(from.size()); ++i) {
        if (!from[static_cast<std::size_t>(i)]) {
            continue;
        }
        if (from[static_cast<std::size_t>(i)]->id == id) {
            out.list = &from;
            out.index = i;
            out.listParent = listParent;
            return true;
        }
        if (FindFolderListPos(
                from[static_cast<std::size_t>(i)]->children, from[static_cast<std::size_t>(i)].get(), id, out)) {
            return true;
        }
    }
    return false;
}

FolderNode* FindListOwnerRecurse(FolderNode* node, const std::vector<std::unique_ptr<FolderNode>>* list) {
    if (!node) {
        return nullptr;
    }
    if (list == &node->children) {
        return node;
    }
    for (auto& c : node->children) {
        if (c) {
            if (FolderNode* f = FindListOwnerRecurse(c.get(), list)) {
                return f;
            }
        }
    }
    return nullptr;
}

FolderNode* FindListOwner(
    const std::vector<std::unique_ptr<FolderNode>>& roots, const std::vector<std::unique_ptr<FolderNode>>* list) {
    if (list == &roots) {
        return nullptr;
    }
    for (const auto& u : roots) {
        if (u) {
            if (FolderNode* f = FindListOwnerRecurse(u.get(), list)) {
                return f;
            }
        }
    }
    return nullptr;
}

bool IsUnderOrEqual(const FolderNode* ancestor, const FolderNode* node) {
    for (const FolderNode* c = node; c; c = c->parent) {
        if (c == ancestor) {
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
    IncomingMessageRouter* incomingMessageRouter = nullptr;
    TagsModule* tagsModule = nullptr;
    NotificationManager* notificationManager = nullptr;

    std::vector<BinderCategory> categories{};
    std::string activeCategoryId{};
    std::vector<HotkeyEntry> hotkeys{};
    FolderNode* currentFolder = nullptr;
    FolderNode* selectedFolder = nullptr;
    ExplorerSelection explorerSelection{};
    bool explorerSelectionScrollPending = false;
    int nextFolderId = 1;
    std::uint64_t nextCategoryId = 1;
    std::uint64_t nextHotkeyRuntimeId = 1;
    std::uint64_t nextHotkeyOrderId = 1;
    bool configLoaded = false;
    bool incomingMessageRouterBound = false;
    bool rakHooksBound = false;
    bool gameInputForeground_ = true;
    bool prevFrameGameInputForeground_ = true;

    std::string bindSearch{};
    std::optional<FolderMoveUndo> folderMoveUndo_{};

    struct EditorState {
        enum class Tab {
            Scenario = 0,
            InputFields = 1,
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
        bool conditionsPopupPending = false;
        bool variablesPopupPending = false;
        bool multiInputPopupPending = false;
        bool variablesTabSelectionPending = false;
        bool variablesKeyPickerPopupPending = false;
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

    enum class FolderInlineEditMode {
        None = 0,
        Create,
        Rename,
    };

    struct FolderInlineEditState {
        FolderInlineEditMode mode = FolderInlineEditMode::None;
        FolderNode* target = nullptr;
        FolderNode* parent = nullptr;
        std::string name{};
        bool focusPending = false;
    } folderInlineEdit{};

    FolderNode* folderDeleteTarget = nullptr;
    bool folderDeletePopupPending = false;
    FolderNode* folderConditionsTarget = nullptr;
    bool folderConditionsPopupPending = false;
    std::string categoryRenameTargetId{};
    std::string categoryRenameBuffer{};
    bool categoryRenamePopupPending = false;
    std::string categoryConditionsTargetId{};
    bool categoryConditionsPopupPending = false;
    std::string categoryDeleteTargetId{};
    std::string categoryDeleteMoveTargetId{};
    bool categoryDeletePopupPending = false;
    std::string categoryTabSelectionTargetId{};
    int bindDeleteTarget = -1;
    bool bindDeletePopupPending = false;
    int moveBindTarget = -1;
    bool moveBindPopupPending = false;
    int bindLinesTarget = -1;
    int selectedBindIndex = -1;
    bool bindLinesPopupPending = false;

    hotkeys::KeyTracker keyTracker{};
    std::vector<UINT> pressedKeys{};
    std::array<bool, 256> asyncKeysDown{};
    hotkeys::Capture capture{};
    hotkeys::CapturePopupState capturePopupState{};
    CaptureTarget captureTarget = CaptureTarget::None;
    int captureHotkeyIndex = -1;
    bool capturePopupInEditor = false;

    std::vector<UINT> quickMenuHotkey{};
    QuickMenuActivationMode quickMenuActivationMode = QuickMenuActivationMode::Hold;
    QuickMenuStyle quickMenuStyle = QuickMenuStyle::Cascade;
    bool quickMenuOpen = false;
    std::string quickMenuActiveCategoryId{};
    bool quickMenuReopenBlocked = false;
    bool quickMenuToggleLatch = false;
    bool quickMenuFocusPending = false;
    ImVec2 quickMenuPos{ 0.0f, 0.0f };
    ImVec2 quickMenuSize{ static_cast<float>(kQuickMenuWidth), static_cast<float>(kQuickMenuHeight) };
    std::optional<bool> quickMenuSampCursorActiveAtOpen{};
    std::optional<bool> quickMenuWindowsCursorActiveAtOpen{};

    std::optional<InputDialogState> inputDialog{};
    std::vector<RunningBind> runningBinds{};
    std::vector<OutgoingGuard> outgoingGuards{};
    std::vector<IncomingChatEchoGuard> incomingChatEchoGuards{};
    std::vector<PendingBindTagAction> pendingBindTagActions{};

    void EnsureInitialized();
    void OnProcessAttach(HMODULE moduleHandle);
    void SetSampApi(SampApi* api);
    void SetSampHooks(SampHooks* hooks);
    void SetSampRakHooks(SampRakHooks* hooks);
    void SetIncomingMessageRouter(IncomingMessageRouter* router);
    void SetNotificationManager(NotificationManager* manager);
    void SetTagsModule(TagsModule* module);
    void ConnectHooks();
    std::string AllocateCategoryId();
    BinderCategory MakeDefaultCategory();
    void EnsureCategories();
    BinderCategory* FindCategoryById(std::string_view id);
    const BinderCategory* FindCategoryById(std::string_view id) const;
    BinderCategory& ActiveCategory();
    const BinderCategory& ActiveCategory() const;
    std::vector<std::unique_ptr<FolderNode>>& ActiveFolders();
    const std::vector<std::unique_ptr<FolderNode>>& ActiveFolders() const;
    std::vector<ExplorerItem>& ActiveRootItems();
    const std::vector<ExplorerItem>& ActiveRootItems() const;
    std::vector<std::vector<std::string>>& ActiveNavigationBackStack();
    const std::vector<std::vector<std::string>>& ActiveNavigationBackStack() const;
    void SelectCategory(std::string_view categoryId);
    std::string NextCategoryName() const;
    bool CategoryNameUnique(std::string_view name, const BinderCategory* ignoredCategory = nullptr) const;
    void BeginRenameCategory(std::string_view categoryId);
    void MoveCategoryByOffset(std::string_view categoryId, int offset);
    void DeleteCategory(std::string_view categoryId, std::string_view moveTargetId, bool deleteContents);
    void MoveCategoryContents(BinderCategory& from, BinderCategory& to);
    FolderNode* EnsureRootFolder();
    std::uint64_t AllocateHotkeyRuntimeId();
    std::string AllocateHotkeyOrderId();
    HotkeyEntry MakeDefaultHotkey();
    void RefreshNumbers();
    void SaveConfig();
    void LoadConfig();
    void EnsureHotkeyOrderIds();
    std::vector<ExplorerItem>& ItemsForFolder(FolderNode* folder);
    const std::vector<ExplorerItem>& ItemsForFolder(const FolderNode* folder) const;
    std::vector<std::string> CurrentFolderPath() const;
    std::vector<std::string> FolderPathForDirectory(const FolderNode* folder) const;
    void ClearExplorerSelection();
    void SelectExplorerFolder(FolderNode* folder, bool scrollIntoView = false);
    void SelectExplorerBind(int index, bool scrollIntoView = false);
    void SelectExplorerItem(const ExplorerItem& item, FolderNode* directory, bool scrollIntoView = false);
    bool IsExplorerFolderSelected(const FolderNode* folder) const;
    bool IsExplorerBindSelected(int index) const;
    int FindExplorerSelectionIndex(const std::vector<ExplorerItem>& items, FolderNode* directory) const;
    void MoveExplorerSelection(int delta);
    void SelectExplorerNeighborAfterRemoval(
        FolderNode* directory,
        const ExplorerItem& removedItem,
        const std::vector<ExplorerItem>& beforeItems);
    void OpenFolder(FolderNode* folder, bool pushHistory = true);
    void OpenFolderPath(const std::vector<std::string>& path, bool pushHistory = true);
    void NavigateUp();
    void NavigateBack();
    void NormalizeExplorerOrders();
    void NormalizeExplorerOrderForDirectory(FolderNode* folder);
    void NormalizeExplorerOrderForDirectory(BinderCategory& category, FolderNode* folder);
    void RemoveBindFromExplorerOrders(const std::string& orderId);
    void RemoveFolderFromParentOrder(FolderNode* folder);
    bool InsertExplorerItem(FolderNode* folder, ExplorerItem item, int index);
    void AppendExplorerItemIfMissing(FolderNode* folder, ExplorerItem item);
    bool MoveBindToExplorerDirectory(
        int hotkeyIndex,
        FolderNode* targetFolder,
        std::optional<int> insertIndex,
        std::string_view source);
    bool MoveBindToCategoryRoot(int hotkeyIndex, std::string_view targetCategoryId, std::string_view source);
    bool MoveFolderToExplorerDirectory(
        int folderId,
        FolderNode* targetFolder,
        int insertIndex,
        std::string_view source,
        bool recordUndo);
    bool MoveFolderToCategoryRoot(int folderId, std::string_view targetCategoryId, std::string_view source);
    bool CanMoveFolderToExplorerDirectory(int folderId, FolderNode* targetFolder, int insertIndex, bool* noop = nullptr);
    FolderNode* FindFolderByNameInDirectory(FolderNode* folder, std::string_view name) const;
    FolderNode* FindFolderByNameInDirectory(const BinderCategory& category, FolderNode* folder, std::string_view name) const;
    int FindHotkeyIndexByOrderId(std::string_view orderId) const;
    JsonValue SerializeCategory(const BinderCategory& category) const;
    BinderCategory DeserializeCategory(const JsonObject& object);
    JsonValue SerializeFolder(const FolderNode& folder) const;
    std::unique_ptr<FolderNode> DeserializeFolder(const JsonObject& object, FolderNode* parent);
    JsonValue SerializeExplorerItems(const std::vector<ExplorerItem>& items) const;
    std::vector<ExplorerItem> DeserializeExplorerItems(const JsonArray* array) const;
    JsonValue SerializeHotkey(const HotkeyEntry& hotkey) const;
    HotkeyEntry DeserializeHotkey(const JsonObject& object);
    void LogExplorerOrderValidation(std::string_view source);
    void Notify(NotificationGroup group, NotificationSeverity severity, std::string_view text, double durationMs = 0.0);
    void ShowUserPopup(std::string_view text, double durationMs = 2200.0);
    void CaptureQuickMenuConditionSnapshot();
    void ClearQuickMenuConditionSnapshot();
    bool VisibleQuickMenuEntriesExist() const;
    bool FolderVisibleInQuickMenu(const FolderNode& folder) const;
    ConditionRuntimeContext MakeConditionContext(bool helperUiCursorActive = false) const;
    void ResetInputState();
    void Tick();
    void Shutdown();
    void ReloadConfig();
    bool WantsOverlayRender() const;
    bool WantsInputCapture() const;
    bool WantsQuickMenuCursor() const;
    bool OnWindowMessage(UINT message, WPARAM wparam, LPARAM lparam);
    bool ApplyCapturedKeys(const std::vector<UINT>& keys);
    void SyncPressedKeysWithAsyncState();
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
    std::string BuildThiscategoryTagValue(std::uint64_t runtimeId) const;
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
    void PruneIncomingChatEchoGuards();
    std::string CurrentLocalPlayerName() const;
    void RegisterIncomingChatEchoGuard(std::string text);
    bool ConsumeIncomingChatEchoGuard(std::string_view normalizedText, std::string_view normalizedPrefixedText);
    std::string NormalizeActivationText(std::string_view text) const;
    bool MatchesActivationCommand(std::string_view input, std::string_view command) const;
    bool TryBeginPendingConfirmation(
        HotkeyEntry& hotkey,
        std::string_view sourceKind,
        const std::string& sourceText,
        bool waitForResolution);
    bool OnOutgoingCommand(const std::string& text);
    void OnOutgoingChat(const std::string& text);
    void OnIncomingMessage(const IncomingMessageEvent& message);
    void ExpireTextConfirmations();
    bool ActivatePendingTextConfirmations(UINT keyCode);
    bool MatchTextTrigger(const std::string& source, const HotkeyEntry& hotkey);
    bool TryDispatchTextTriggerMatch(
        int index,
        HotkeyEntry& hotkey,
        const std::string& sourceText,
        std::string_view sourceKind,
        double now);
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
    void MoveBindToFolderPath(int hotkeyIndex, const std::vector<std::string>& folderPath, std::string_view source);
    std::string NextFolderNameForParent(FolderNode* parent) const;
    void BeginInlineCreateFolder(FolderNode* parent);
    void BeginInlineRenameFolder(FolderNode* folder);
    bool CommitInlineFolderEdit();
    void CancelInlineFolderEdit();
    bool IsInlineRenamingFolder(const FolderNode* folder) const;
    bool CanDeleteFolder(const FolderNode* folder) const;
    bool NormalizeLegacyRootFolderName();
    void BeginCapture(CaptureTarget target);
    void DrawCapturePopup(bool insideEditorPopup);
    void DrawQuickMenu();
    std::string QuickMenuHotkeyText() const;
    void DrawSettingsSection(bool includeHeader);
    void DrawInputDialog();
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
    void DrawEditorVariablesPopup();
    void DrawEditorDiscardPopup();
    void DrawEditorInline();
    void DrawEditorScenarioTab();
    void DrawEditorMultiInputPopup();
    bool IsValidFolderDropTarget(int moveId, const FolderListPos& dest, bool* noop = nullptr);
    bool RelocateFolderNode(int moveId, FolderListPos dest, bool recordUndo);
    void ClearFolderMoveUndo() { folderMoveUndo_.reset(); }
    void ApplyFolderMoveUndo();

    void DrawFolderPopups();
    void DrawCategoryTabs();
    void DrawCategoryPopups();
    void DrawExplorerPane();
    void DrawExplorerToolbar();
    void DrawExplorerSearchResults();
    void DrawExplorerDirectory();
    void DrawExplorerEmptyAreaContextMenu(const char* popupId);
    void DrawExplorerInlineFolderEditRow(int rowIndex, const ExplorerListLayout& layout, const ImRect& rowRect);
    void DrawExplorerInlineFolderEditContent(const ExplorerListLayout& layout, const ImRect& rowRect, bool selected);
    void DrawExplorerFolderRow(FolderNode& folder, int rowIndex, const ExplorerListLayout& layout, const ImRect& rowRect);
    void DrawExplorerBindRow(int index, int rowIndex, const ExplorerListLayout& layout, const ImRect& rowRect);
    void DrawExplorerKeyboardShortcuts(bool focused);
    void DrawExplorerBreadcrumb();
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

void BinderModule::Impl::SetIncomingMessageRouter(IncomingMessageRouter* router) {
    incomingMessageRouter = router;
    ConnectHooks();
}

void BinderModule::Impl::SetNotificationManager(NotificationManager* manager) {
    notificationManager = manager;
}

void BinderModule::Impl::SetTagsModule(TagsModule* module) {
    tagsModule = module;
}

std::string BinderModule::Impl::AllocateCategoryId() {
    return "category-" + std::to_string(nextCategoryId++);
}

BinderCategory BinderModule::Impl::MakeDefaultCategory() {
    BinderCategory category;
    category.id = AllocateCategoryId();
    category.name = UiSettings::Instance().Text(UiText::BinderDefaultRootFolder);
    category.conditions.assign(static_cast<std::size_t>(ConditionId::Count), false);
    return category;
}

void BinderModule::Impl::EnsureCategories() {
    if (categories.empty()) {
        categories.push_back(MakeDefaultCategory());
    }

    for (BinderCategory& category : categories) {
        if (category.id.empty()) {
            category.id = AllocateCategoryId();
        }
        category.name = SanitizeFolderName(category.name);
        if (category.name.empty()) {
            category.name = UiSettings::Instance().Text(UiText::BinderDefaultRootFolder);
        }
        if (category.conditions.size() < static_cast<std::size_t>(ConditionId::Count)) {
            category.conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
        }
    }

    if (!FindCategoryById(activeCategoryId)) {
        activeCategoryId = categories.front().id;
    }
}

BinderCategory* BinderModule::Impl::FindCategoryById(std::string_view id) {
    for (BinderCategory& category : categories) {
        if (category.id == id) {
            return &category;
        }
    }
    return nullptr;
}

const BinderCategory* BinderModule::Impl::FindCategoryById(std::string_view id) const {
    for (const BinderCategory& category : categories) {
        if (category.id == id) {
            return &category;
        }
    }
    return nullptr;
}

BinderCategory& BinderModule::Impl::ActiveCategory() {
    EnsureCategories();
    if (BinderCategory* category = FindCategoryById(activeCategoryId)) {
        return *category;
    }
    activeCategoryId = categories.front().id;
    return categories.front();
}

const BinderCategory& BinderModule::Impl::ActiveCategory() const {
    if (const BinderCategory* category = FindCategoryById(activeCategoryId)) {
        return *category;
    }
    return categories.front();
}

std::vector<std::unique_ptr<FolderNode>>& BinderModule::Impl::ActiveFolders() {
    return ActiveCategory().folders;
}

const std::vector<std::unique_ptr<FolderNode>>& BinderModule::Impl::ActiveFolders() const {
    return ActiveCategory().folders;
}

std::vector<ExplorerItem>& BinderModule::Impl::ActiveRootItems() {
    return ActiveCategory().rootItems;
}

const std::vector<ExplorerItem>& BinderModule::Impl::ActiveRootItems() const {
    return ActiveCategory().rootItems;
}

std::vector<std::vector<std::string>>& BinderModule::Impl::ActiveNavigationBackStack() {
    return ActiveCategory().navigationBackStack;
}

const std::vector<std::vector<std::string>>& BinderModule::Impl::ActiveNavigationBackStack() const {
    return ActiveCategory().navigationBackStack;
}

void BinderModule::Impl::SelectCategory(std::string_view categoryId) {
    if (categoryId == activeCategoryId || !FindCategoryById(categoryId)) {
        return;
    }

    CancelInlineFolderEdit();
    folderDeleteTarget = nullptr;
    folderDeletePopupPending = false;
    folderConditionsTarget = nullptr;
    folderConditionsPopupPending = false;
    activeCategoryId = std::string(categoryId);
    currentFolder = nullptr;
    if (BinderCategory* category = FindCategoryById(activeCategoryId)) {
        if (!category->lastOpenFolderPath.empty()) {
            currentFolder = FindFolderByPath(category->folders, category->lastOpenFolderPath);
            if (!currentFolder) {
                category->lastOpenFolderPath.clear();
            }
        }
    }
    ClearExplorerSelection();
    categoryTabSelectionTargetId = activeCategoryId;
    SaveConfig();
}

std::string BinderModule::Impl::NextCategoryName() const {
    const std::string baseName = UiSettings::Instance().Text(UiText::BinderNewCategory);
    std::string name = baseName;
    for (int suffix = 2; !CategoryNameUnique(name); ++suffix) {
        name = baseName + " " + std::to_string(suffix);
    }
    return name;
}

bool BinderModule::Impl::CategoryNameUnique(std::string_view name, const BinderCategory* ignoredCategory) const {
    for (const BinderCategory& category : categories) {
        if (&category == ignoredCategory) {
            continue;
        }
        if (category.name == name) {
            return false;
        }
    }
    return true;
}

void BinderModule::Impl::BeginRenameCategory(std::string_view categoryId) {
    BinderCategory* category = FindCategoryById(categoryId);
    if (!category) {
        return;
    }
    categoryRenameTargetId = category->id;
    categoryRenameBuffer = category->name;
    categoryRenamePopupPending = true;
}

void BinderModule::Impl::MoveCategoryByOffset(std::string_view categoryId, int offset) {
    if (offset == 0) {
        return;
    }
    const auto it = std::find_if(categories.begin(), categories.end(), [&](const BinderCategory& category) {
        return category.id == categoryId;
    });
    if (it == categories.end()) {
        return;
    }
    const int index = static_cast<int>(std::distance(categories.begin(), it));
    const int target = std::clamp(index + offset, 0, static_cast<int>(categories.size()) - 1);
    if (target == index) {
        return;
    }
    std::iter_swap(categories.begin() + index, categories.begin() + target);
    SaveConfig();
}

void BinderModule::Impl::MoveCategoryContents(BinderCategory& from, BinderCategory& to) {
    NormalizeExplorerOrders();
    std::vector<ExplorerItem> movedOrder;
    movedOrder.reserve(from.rootItems.size());

    const auto findRootFolderByName = [](std::vector<std::unique_ptr<FolderNode>>& roots, std::string_view name) {
        return std::find_if(roots.begin(), roots.end(), [&](const std::unique_ptr<FolderNode>& folder) {
            return folder && folder->name == name;
        });
    };

    for (const ExplorerItem& item : from.rootItems) {
        if (item.kind == ExplorerItemKind::Bind) {
            const int index = FindHotkeyIndexByOrderId(item.key);
            if (index >= 0 && hotkeys[static_cast<std::size_t>(index)].categoryId == from.id) {
                hotkeys[static_cast<std::size_t>(index)].categoryId = to.id;
                movedOrder.push_back(item);
            }
            continue;
        }

        auto folderIt = findRootFolderByName(from.folders, item.key);
        if (folderIt == from.folders.end()) {
            continue;
        }

        FolderNode* folder = folderIt->get();
        const std::vector<std::string> oldPath = BuildFolderPath(folder);
        const std::string baseName = folder->name;
        std::string newName = baseName;
        for (int suffix = 2; !FolderNameUnique(to.folders, newName, nullptr); ++suffix) {
            newName = baseName + " " + std::to_string(suffix);
        }
        folder->name = newName;
        folder->parent = nullptr;
        const std::vector<std::string> newPath{ newName };
        for (HotkeyEntry& hotkey : hotkeys) {
            if (hotkey.categoryId == from.id && PathStartsWith(hotkey.folderPath, oldPath)) {
                hotkey.categoryId = to.id;
                hotkey.folderPath = ReplacePathPrefix(hotkey.folderPath, oldPath, newPath);
            }
        }

        to.folders.push_back(std::move(*folderIt));
        from.folders.erase(folderIt);
        movedOrder.push_back(ExplorerItem{ ExplorerItemKind::Folder, newName });
    }

    for (HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.categoryId == from.id) {
            hotkey.categoryId = to.id;
            if (hotkey.folderPath.empty() && !hotkey.orderId.empty()) {
                movedOrder.push_back(ExplorerItem{ ExplorerItemKind::Bind, hotkey.orderId });
            }
        }
    }

    to.rootItems.insert(to.rootItems.end(), movedOrder.begin(), movedOrder.end());
    from.rootItems.clear();
    from.folders.clear();
}

void BinderModule::Impl::DeleteCategory(
    std::string_view categoryId,
    std::string_view moveTargetId,
    const bool deleteContents) {
    if (categories.size() <= 1) {
        Notify(
            NotificationGroup::Validation,
            NotificationSeverity::Error,
            UiSettings::Instance().Text(UiText::CategoryCannotDeleteLast),
            2200.0);
        return;
    }

    auto sourceIt = std::find_if(categories.begin(), categories.end(), [&](const BinderCategory& category) {
        return category.id == categoryId;
    });
    if (sourceIt == categories.end()) {
        return;
    }

    if (!deleteContents) {
        BinderCategory* target = FindCategoryById(moveTargetId);
        if (!target || target->id == sourceIt->id) {
            return;
        }
        MoveCategoryContents(*sourceIt, *target);
    } else {
        std::vector<std::uint64_t> runtimeIds;
        for (const HotkeyEntry& hotkey : hotkeys) {
            if (hotkey.categoryId == sourceIt->id) {
                runtimeIds.push_back(hotkey.runtimeId);
            }
        }
        for (const std::uint64_t runtimeId : runtimeIds) {
            StopHotkeyByRuntimeId(runtimeId);
        }
        hotkeys.erase(
            std::remove_if(hotkeys.begin(), hotkeys.end(), [&](const HotkeyEntry& hotkey) {
                return hotkey.categoryId == sourceIt->id;
            }),
            hotkeys.end());
        RefreshNumbers();
    }

    const bool deletingActive = sourceIt->id == activeCategoryId;
    const std::string fallbackId = !deleteContents && !moveTargetId.empty()
        ? std::string(moveTargetId)
        : (sourceIt == categories.begin() ? (sourceIt + 1)->id : (sourceIt - 1)->id);
    categories.erase(sourceIt);
    EnsureCategories();
    if (deletingActive || !FindCategoryById(activeCategoryId)) {
        activeCategoryId = FindCategoryById(fallbackId) ? fallbackId : categories.front().id;
        currentFolder = nullptr;
    }
    ClearExplorerSelection();
    NormalizeExplorerOrders();
    SaveConfig();
}

std::uint64_t BinderModule::Impl::AllocateHotkeyRuntimeId() {
    return nextHotkeyRuntimeId++;
}

std::string BinderModule::Impl::AllocateHotkeyOrderId() {
    return "bind-" + std::to_string(nextHotkeyOrderId++);
}

void BinderModule::Impl::ConnectHooks() {
    if (!incomingMessageRouterBound && incomingMessageRouter) {
        incomingMessageRouter->AddOnMessageHandler([this](const IncomingMessageEvent& message) {
            OnIncomingMessage(message);
        });
        incomingMessageRouterBound = true;
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
        rakHooksBound = true;
    }
}

FolderNode* BinderModule::Impl::EnsureRootFolder() {
    EnsureCategories();
    return ActiveFolders().empty() ? nullptr : ActiveFolders().front().get();
}

bool BinderModule::Impl::NormalizeLegacyRootFolderName() {
    FolderNode* root = ActiveFolders().empty() ? nullptr : ActiveFolders().front().get();
    if (!root) {
        return false;
    }

    const std::string desiredName = UiSettings::Instance().Text(UiText::BinderDefaultRootFolder);
    if (root->name == desiredName) {
        return false;
    }
    if (!root->name.empty() && !IsLegacyRootFolderName(root->name)) {
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
    hotkey.repeatIntervalMs = kDefaultRepeatIntervalMs;
    hotkey.textConfirmation = TextConfirmation{};
    hotkey.runtimeId = AllocateHotkeyRuntimeId();
    hotkey.orderId = AllocateHotkeyOrderId();
    hotkey.categoryId = ActiveCategory().id;
    return hotkey;
}

void BinderModule::Impl::RefreshNumbers() {
    int number = 1;
    for (HotkeyEntry& hotkey : hotkeys) {
        hotkey.number = number++;
    }
}

void BinderModule::Impl::SaveConfig() {
    EnsureCategories();
    ActiveCategory().lastOpenFolderPath = CurrentFolderPath();
    NormalizeExplorerOrders();
    LogExplorerOrderValidation("save");
    JsonObject root;
    root["quick_menu_hotkey"] = SerializeUintArray(quickMenuHotkey);
    root["quick_menu_activation_mode"] = QuickMenuActivationModeId(quickMenuActivationMode);
    root["quick_menu_style"] = QuickMenuStyleId(quickMenuStyle);
    root["active_category_id"] = activeCategoryId;

    JsonArray categoryArray;
    for (const BinderCategory& category : categories) {
        categoryArray.push_back(SerializeCategory(category));
    }
    root["categories"] = JsonValue(std::move(categoryArray));

    JsonArray hotkeyArray;
    for (const HotkeyEntry& hotkey : hotkeys) {
        hotkeyArray.push_back(SerializeHotkey(hotkey));
    }
    root["hotkeys"] = JsonValue(std::move(hotkeyArray));

    AppConfig::Instance().QueueSectionReplace(std::string(kBinderConfigSectionName), JsonValue(std::move(root)));
}

void BinderModule::Impl::LoadConfig() {
    categories.clear();
    activeCategoryId.clear();
    categoryTabSelectionTargetId.clear();
    hotkeys.clear();
    currentFolder = nullptr;
    selectedFolder = nullptr;
    explorerSelection = {};
    explorerSelectionScrollPending = false;
    nextFolderId = 1;
    nextCategoryId = 1;
    nextHotkeyRuntimeId = 1;
    nextHotkeyOrderId = 1;
    quickMenuHotkey.clear();
    quickMenuActivationMode = QuickMenuActivationMode::Hold;
    quickMenuStyle = QuickMenuStyle::Cascade;
    quickMenuActiveCategoryId.clear();
    const jsonutil::JsonValue sharedSection = AppConfig::Instance().ReadSection(kBinderConfigSectionName);
    const JsonObject* root = sharedSection.TryObject();
    if (!root) {
        EnsureCategories();
        return;
    }

    quickMenuHotkey = ::hotkeys::NormalizeCombo(
        DeserializeUintArray(jsonutil::JsonArrayOrNull(root, "quick_menu_hotkey")), HotkeyMode::ModifierTrigger);
    quickMenuActivationMode =
        NormalizeQuickMenuActivationMode(jsonutil::JsonStringOr(root, "quick_menu_activation_mode", "hold"));
    quickMenuStyle =
        NormalizeQuickMenuStyle(jsonutil::JsonStringOr(root, "quick_menu_style", "cascade"));
    activeCategoryId = jsonutil::JsonStringOr(root, "active_category_id", "");

    if (const JsonArray* categoryArray = jsonutil::JsonArrayOrNull(root, "categories")) {
        for (const JsonValue& item : *categoryArray) {
            if (const JsonObject* object = item.TryObject()) {
                categories.push_back(DeserializeCategory(*object));
            }
        }
    }

    const bool legacyFormat = categories.empty();
    if (legacyFormat) {
        BinderCategory category = MakeDefaultCategory();
        category.rootItems = DeserializeExplorerItems(jsonutil::JsonArrayOrNull(root, "root_order"));
        category.lastOpenFolderPath =
            DeserializeStringArray(jsonutil::JsonArrayOrNull(root, "last_open_folder_path"));

        if (const JsonArray* folderArray = jsonutil::JsonArrayOrNull(root, "folders")) {
            for (const JsonValue& item : *folderArray) {
                if (const JsonObject* object = item.TryObject()) {
                    auto folder = DeserializeFolder(*object, nullptr);
                    if (folder) {
                        category.folders.push_back(std::move(folder));
                    }
                }
            }
        }
        activeCategoryId = category.id;
        categories.push_back(std::move(category));
    }

    if (const JsonArray* hotkeyArray = jsonutil::JsonArrayOrNull(root, "hotkeys")) {
        for (const JsonValue& item : *hotkeyArray) {
            if (const JsonObject* object = item.TryObject()) {
                hotkeys.push_back(DeserializeHotkey(*object));
            }
        }
    }

    EnsureCategories();
    if (!FindCategoryById(activeCategoryId)) {
        activeCategoryId = categories.front().id;
    }
    for (HotkeyEntry& hotkey : hotkeys) {
        if (!FindCategoryById(hotkey.categoryId)) {
            hotkey.categoryId = categories.front().id;
        }
    }
    const bool migratedLegacyRoot = NormalizeLegacyRootFolderName();
    EnsureHotkeyOrderIds();
    NormalizeExplorerOrders();
    BinderCategory& active = ActiveCategory();
    currentFolder = active.lastOpenFolderPath.empty() ? nullptr : FindFolderByPath(active.folders, active.lastOpenFolderPath);
    if (!currentFolder) {
        active.lastOpenFolderPath.clear();
    }
    categoryTabSelectionTargetId = activeCategoryId;
    ClearExplorerSelection();
    RefreshNumbers();

    if (legacyFormat || migratedLegacyRoot) {
        SaveConfig();
    }
}

JsonValue BinderModule::Impl::SerializeCategory(const BinderCategory& category) const {
    JsonObject object;
    object["id"] = category.id;
    object["name"] = category.name;
    object["quick_menu"] = category.quickMenu;
    object["conditions"] = SerializeBoolArray(category.conditions);
    object["conditions_combine"] = ConditionCombineModeId(category.conditionsCombine);
    object["root_order"] = SerializeExplorerItems(category.rootItems);
    object["last_open_folder_path"] = SerializeStringArray(category.lastOpenFolderPath);

    JsonArray folderArray;
    for (const auto& folder : category.folders) {
        if (folder) {
            folderArray.push_back(SerializeFolder(*folder));
        }
    }
    object["folders"] = JsonValue(std::move(folderArray));
    return JsonValue(std::move(object));
}

BinderCategory BinderModule::Impl::DeserializeCategory(const JsonObject& object) {
    BinderCategory category;
    category.id = jsonutil::JsonStringOr(&object, "id", "");
    if (category.id.empty()) {
        category.id = AllocateCategoryId();
    } else if (category.id.rfind("category-", 0) == 0) {
        const std::string suffix = category.id.substr(9);
        if (!suffix.empty() && std::all_of(suffix.begin(), suffix.end(), [](const unsigned char ch) {
                return std::isdigit(ch) != 0;
            })) {
            try {
                const auto parsed = static_cast<std::uint64_t>(std::stoull(suffix));
                if (parsed < std::numeric_limits<std::uint64_t>::max()) {
                    nextCategoryId = std::max(nextCategoryId, parsed + 1);
                }
            } catch (...) {
                // Keep loading configs with malformed user-edited category ids.
            }
        }
    }
    category.name = SanitizeFolderName(
        jsonutil::JsonStringOr(&object, "name", UiSettings::Instance().Text(UiText::BinderDefaultRootFolder)));
    if (category.name.empty()) {
        category.name = UiSettings::Instance().Text(UiText::BinderDefaultRootFolder);
    }
    category.quickMenu = jsonutil::JsonBoolOr(&object, "quick_menu", true);
    category.conditions = DeserializeBoolArray(jsonutil::JsonArrayOrNull(&object, "conditions"));
    if (category.conditions.size() < static_cast<std::size_t>(ConditionId::Count)) {
        category.conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    }
    category.conditionsCombine =
        NormalizeConditionCombineMode(jsonutil::JsonStringOr(&object, "conditions_combine", "require_any"));
    category.rootItems = DeserializeExplorerItems(jsonutil::JsonArrayOrNull(&object, "root_order"));
    category.lastOpenFolderPath =
        DeserializeStringArray(jsonutil::JsonArrayOrNull(&object, "last_open_folder_path"));

    if (const JsonArray* folderArray = jsonutil::JsonArrayOrNull(&object, "folders")) {
        for (const JsonValue& item : *folderArray) {
            if (const JsonObject* folderObject = item.TryObject()) {
                auto folder = DeserializeFolder(*folderObject, nullptr);
                if (folder) {
                    category.folders.push_back(std::move(folder));
                }
            }
        }
    }
    return category;
}

JsonValue BinderModule::Impl::SerializeFolder(const FolderNode& folder) const {
    JsonObject object;
    object["name"] = folder.name;
    object["quick_menu"] = folder.quickMenu;
    object["conditions"] = SerializeBoolArray(folder.conditions);
    object["conditions_combine"] = ConditionCombineModeId(folder.conditionsCombine);
    object["items"] = SerializeExplorerItems(folder.items);

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
    const JsonArray* conditionsArray = jsonutil::JsonArrayOrNull(&object, "conditions");
    if (!conditionsArray) {
        conditionsArray = jsonutil::JsonArrayOrNull(&object, "quick_conditions");
    }
    folder->conditions = DeserializeBoolArray(conditionsArray);
    if (folder->conditions.size() < static_cast<std::size_t>(ConditionId::Count)) {
        folder->conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    }
    folder->conditionsCombine = NormalizeConditionCombineMode(jsonutil::JsonStringOr(
        &object,
        "conditions_combine",
        jsonutil::JsonStringOr(&object, "quick_conditions_combine", "require_all")));
    folder->items = DeserializeExplorerItems(jsonutil::JsonArrayOrNull(&object, "items"));

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

JsonValue BinderModule::Impl::SerializeExplorerItems(const std::vector<ExplorerItem>& items) const {
    JsonArray array;
    for (const ExplorerItem& item : items) {
        if (item.key.empty()) {
            continue;
        }
        JsonObject object;
        object["type"] = item.kind == ExplorerItemKind::Folder ? "folder" : "bind";
        object["key"] = item.key;
        array.emplace_back(std::move(object));
    }
    return JsonValue(std::move(array));
}

std::vector<ExplorerItem> BinderModule::Impl::DeserializeExplorerItems(const JsonArray* array) const {
    std::vector<ExplorerItem> items;
    if (!array) {
        return items;
    }

    for (const JsonValue& value : *array) {
        const JsonObject* object = value.TryObject();
        if (!object) {
            continue;
        }
        const std::string type = ToLower(jsonutil::JsonStringOr(object, "type", ""));
        const std::string key = jsonutil::JsonStringOr(object, "key", "");
        if (key.empty()) {
            continue;
        }
        if (type == "folder") {
            items.push_back(ExplorerItem{ ExplorerItemKind::Folder, key });
        } else if (type == "bind") {
            items.push_back(ExplorerItem{ ExplorerItemKind::Bind, key });
        }
    }
    return items;
}

JsonValue BinderModule::Impl::SerializeHotkey(const HotkeyEntry& hotkey) const {
    JsonObject object;
    object["label"] = hotkey.label;
    object["keys"] = SerializeUintArray(hotkey.keys);
    object["hotkey_mode"] = HotkeyModeId(hotkey.hotkeyMode);
    object["conditions"] = SerializeBoolArray(hotkey.conditions);
    object["conditions_combine"] = ConditionCombineModeId(hotkey.conditionsCombine);
    object["repeat_mode"] = hotkey.repeatMode;
    object["repeat_interval_ms"] = hotkey.repeatIntervalMs;
    object["enabled"] = hotkey.enabled;
    object["quick_menu"] = hotkey.quickMenu;
    object["command"] = hotkey.command;
    object["command_enabled"] = hotkey.commandEnabled;
    object["category_id"] = hotkey.categoryId;
    object["folder_path"] = SerializeStringArray(hotkey.folderPath);
    object["order_id"] = hotkey.orderId;

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

    JsonObject commandConfirmation;
    commandConfirmation["enabled"] = hotkey.commandConfirmation.enabled;
    commandConfirmation["wait_for_resolution"] = hotkey.commandConfirmation.waitForResolution;
    object["command_confirmation"] = JsonValue(std::move(commandConfirmation));

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
    hotkey.conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    hotkey.conditionsCombine =
        NormalizeConditionCombineMode(jsonutil::JsonStringOr(&object, "conditions_combine", "require_all"));
    std::vector<bool> legacyQuickConditions =
        DeserializeBoolArray(jsonutil::JsonArrayOrNull(&object, "quick_conditions"));
    if (!HasSelectedCondition(hotkey.conditions) && HasSelectedCondition(legacyQuickConditions)) {
        hotkey.conditions = std::move(legacyQuickConditions);
        hotkey.conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
        hotkey.conditionsCombine =
            NormalizeConditionCombineMode(jsonutil::JsonStringOr(&object, "quick_conditions_combine", "require_all"));
    }
    hotkey.repeatMode = jsonutil::JsonBoolOr(&object, "repeat_mode", false);
    hotkey.repeatIntervalMs = jsonutil::JsonNumberOr<int>(&object, "repeat_interval_ms", kDefaultRepeatIntervalMs);
    hotkey.enabled = jsonutil::JsonBoolOr(&object, "enabled", true);
    hotkey.quickMenu = jsonutil::JsonBoolOr(&object, "quick_menu", false);
    hotkey.command = jsonutil::JsonStringOr(&object, "command", "");
    hotkey.commandEnabled = jsonutil::JsonBoolOr(&object, "command_enabled", false);
    hotkey.categoryId = jsonutil::JsonStringOr(&object, "category_id", "");
    hotkey.folderPath = DeserializeStringArray(jsonutil::JsonArrayOrNull(&object, "folder_path"));
    hotkey.orderId = jsonutil::JsonStringOr(&object, "order_id", "");

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

    if (const JsonObject* commandConfirmation = jsonutil::JsonObjectOrNull(&object, "command_confirmation")) {
        hotkey.commandConfirmation.enabled = jsonutil::JsonBoolOr(commandConfirmation, "enabled", false);
        hotkey.commandConfirmation.waitForResolution =
            jsonutil::JsonBoolOr(commandConfirmation, "wait_for_resolution", true);
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

void BinderModule::Impl::EnsureHotkeyOrderIds() {
    for (const HotkeyEntry& hotkey : hotkeys) {
        constexpr std::string_view prefix = "bind-";
        if (!StartsWith(hotkey.orderId, prefix)) {
            continue;
        }
        std::uint64_t value = 0;
        bool valid = false;
        for (const char ch : hotkey.orderId.substr(prefix.size())) {
            if (ch < '0' || ch > '9') {
                valid = false;
                break;
            }
            valid = true;
            value = value * 10 + static_cast<std::uint64_t>(ch - '0');
        }
        if (valid && value >= nextHotkeyOrderId) {
            nextHotkeyOrderId = value + 1;
        }
    }

    std::set<std::string> used;
    for (HotkeyEntry& hotkey : hotkeys) {
        if (!hotkey.orderId.empty() && used.insert(hotkey.orderId).second) {
            continue;
        }

        do {
            hotkey.orderId = AllocateHotkeyOrderId();
        } while (!used.insert(hotkey.orderId).second);
    }
}

std::vector<ExplorerItem>& BinderModule::Impl::ItemsForFolder(FolderNode* folder) {
    return folder ? folder->items : ActiveRootItems();
}

const std::vector<ExplorerItem>& BinderModule::Impl::ItemsForFolder(const FolderNode* folder) const {
    return folder ? folder->items : ActiveRootItems();
}

std::vector<std::string> BinderModule::Impl::FolderPathForDirectory(const FolderNode* folder) const {
    return folder ? BuildFolderPath(folder) : std::vector<std::string>{};
}

std::vector<std::string> BinderModule::Impl::CurrentFolderPath() const {
    return FolderPathForDirectory(currentFolder);
}

void BinderModule::Impl::ClearExplorerSelection() {
    explorerSelection = {};
    explorerSelectionScrollPending = false;
    selectedFolder = nullptr;
    selectedBindIndex = -1;
}

void BinderModule::Impl::SelectExplorerFolder(FolderNode* folder, const bool scrollIntoView) {
    if (!folder) {
        ClearExplorerSelection();
        return;
    }

    explorerSelection.kind = ExplorerSelectionKind::Folder;
    explorerSelection.folderId = folder->id;
    explorerSelection.bindOrderId.clear();
    explorerSelectionScrollPending = explorerSelectionScrollPending || scrollIntoView;
    selectedFolder = folder;
    selectedBindIndex = -1;
}

void BinderModule::Impl::SelectExplorerBind(const int index, const bool scrollIntoView) {
    if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
        ClearExplorerSelection();
        return;
    }

    explorerSelection.kind = ExplorerSelectionKind::Bind;
    explorerSelection.folderId = 0;
    explorerSelection.bindOrderId = hotkeys[static_cast<std::size_t>(index)].orderId;
    explorerSelectionScrollPending = explorerSelectionScrollPending || scrollIntoView;
    selectedFolder = nullptr;
    selectedBindIndex = index;
}

void BinderModule::Impl::SelectExplorerItem(
    const ExplorerItem& item,
    FolderNode* directory,
    const bool scrollIntoView) {
    if (item.kind == ExplorerItemKind::Folder) {
        SelectExplorerFolder(FindFolderByNameInDirectory(directory, item.key), scrollIntoView);
        return;
    }

    SelectExplorerBind(FindHotkeyIndexByOrderId(item.key), scrollIntoView);
}

bool BinderModule::Impl::IsExplorerFolderSelected(const FolderNode* folder) const {
    return folder != nullptr
        && explorerSelection.kind == ExplorerSelectionKind::Folder
        && explorerSelection.folderId == folder->id;
}

bool BinderModule::Impl::IsExplorerBindSelected(const int index) const {
    return index >= 0
        && index < static_cast<int>(hotkeys.size())
        && explorerSelection.kind == ExplorerSelectionKind::Bind
        && explorerSelection.bindOrderId == hotkeys[static_cast<std::size_t>(index)].orderId;
}

int BinderModule::Impl::FindExplorerSelectionIndex(
    const std::vector<ExplorerItem>& items,
    FolderNode* directory) const {
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const ExplorerItem& item = items[static_cast<std::size_t>(i)];
        if (item.kind == ExplorerItemKind::Folder) {
            const FolderNode* folder = FindFolderByNameInDirectory(directory, item.key);
            if (IsExplorerFolderSelected(folder)) {
                return i;
            }
            continue;
        }

        if (explorerSelection.kind == ExplorerSelectionKind::Bind && explorerSelection.bindOrderId == item.key) {
            return i;
        }
    }
    return -1;
}

void BinderModule::Impl::MoveExplorerSelection(const int delta) {
    if (delta == 0) {
        return;
    }

    NormalizeExplorerOrders();
    const std::vector<ExplorerItem>& items = ItemsForFolder(currentFolder);
    if (items.empty()) {
        ClearExplorerSelection();
        return;
    }

    int index = FindExplorerSelectionIndex(items, currentFolder);
    if (index < 0) {
        index = delta > 0 ? 0 : static_cast<int>(items.size()) - 1;
    } else {
        index = std::clamp(index + delta, 0, static_cast<int>(items.size()) - 1);
    }

    SelectExplorerItem(items[static_cast<std::size_t>(index)], currentFolder, true);
}

void BinderModule::Impl::SelectExplorerNeighborAfterRemoval(
    FolderNode* directory,
    const ExplorerItem& removedItem,
    const std::vector<ExplorerItem>& beforeItems) {
    const std::vector<ExplorerItem>& afterItems = ItemsForFolder(directory);
    if (afterItems.empty()) {
        ClearExplorerSelection();
        return;
    }

    int removedIndex = -1;
    for (int i = 0; i < static_cast<int>(beforeItems.size()); ++i) {
        const ExplorerItem& item = beforeItems[static_cast<std::size_t>(i)];
        if (item.kind == removedItem.kind && item.key == removedItem.key) {
            removedIndex = i;
            break;
        }
    }

    if (removedIndex < 0) {
        removedIndex = 0;
    }
    const int nextIndex = std::clamp(removedIndex, 0, static_cast<int>(afterItems.size()) - 1);
    SelectExplorerItem(afterItems[static_cast<std::size_t>(nextIndex)], directory, true);
}

FolderNode* BinderModule::Impl::FindFolderByNameInDirectory(FolderNode* folder, std::string_view name) const {
    return FindFolderByNameInDirectory(ActiveCategory(), folder, name);
}

FolderNode* BinderModule::Impl::FindFolderByNameInDirectory(
    const BinderCategory& category,
    FolderNode* folder,
    std::string_view name) const {
    const auto& children = folder ? folder->children : category.folders;
    for (const auto& child : children) {
        if (child && child->name == name) {
            return child.get();
        }
    }
    return nullptr;
}

int BinderModule::Impl::FindHotkeyIndexByOrderId(std::string_view orderId) const {
    if (orderId.empty()) {
        return -1;
    }
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        if (hotkeys[i].orderId == orderId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void BinderModule::Impl::OpenFolder(FolderNode* folder, const bool pushHistory) {
    const std::vector<std::string> oldPath = CurrentFolderPath();
    const std::vector<std::string> newPath = FolderPathForDirectory(folder);
    if (oldPath == newPath) {
        currentFolder = folder;
        ClearExplorerSelection();
        return;
    }
    CancelInlineFolderEdit();
    if (pushHistory) {
        auto& navigationBackStack = ActiveNavigationBackStack();
        navigationBackStack.push_back(oldPath);
        if (navigationBackStack.size() > 64) {
            navigationBackStack.erase(navigationBackStack.begin());
        }
    }
    currentFolder = folder;
    ActiveCategory().lastOpenFolderPath = newPath;
    ClearExplorerSelection();
    SaveConfig();
}

void BinderModule::Impl::OpenFolderPath(const std::vector<std::string>& path, const bool pushHistory) {
    if (path.empty()) {
        OpenFolder(nullptr, pushHistory);
        return;
    }
    if (FolderNode* folder = FindFolderByPath(ActiveFolders(), path)) {
        OpenFolder(folder, pushHistory);
    }
}

void BinderModule::Impl::NavigateUp() {
    if (!currentFolder) {
        return;
    }
    OpenFolder(currentFolder->parent, true);
}

void BinderModule::Impl::NavigateBack() {
    auto& navigationBackStack = ActiveNavigationBackStack();
    if (navigationBackStack.empty()) {
        return;
    }
    const std::vector<std::string> path = navigationBackStack.back();
    navigationBackStack.pop_back();
    OpenFolderPath(path, false);
}

void BinderModule::Impl::NormalizeExplorerOrderForDirectory(FolderNode* folder) {
    NormalizeExplorerOrderForDirectory(ActiveCategory(), folder);
}

void BinderModule::Impl::NormalizeExplorerOrderForDirectory(BinderCategory& category, FolderNode* folder) {
    const std::vector<std::string> path = FolderPathForDirectory(folder);
    auto& items = folder ? folder->items : category.rootItems;
    std::vector<ExplorerItem> normalized;
    normalized.reserve(items.size());
    std::set<std::string> seenFolders;
    std::set<std::string> seenBinds;

    const auto folderExists = [&](std::string_view name) {
        return FindFolderByNameInDirectory(category, folder, name) != nullptr;
    };
    const auto bindBelongsHere = [&](std::string_view orderId) {
        const int index = FindHotkeyIndexByOrderId(orderId);
        return index >= 0
            && hotkeys[static_cast<std::size_t>(index)].categoryId == category.id
            && hotkeys[static_cast<std::size_t>(index)].folderPath == path;
    };

    for (const ExplorerItem& item : items) {
        if (item.kind == ExplorerItemKind::Folder) {
            if (folderExists(item.key) && seenFolders.insert(item.key).second) {
                normalized.push_back(item);
            }
        } else if (bindBelongsHere(item.key) && seenBinds.insert(item.key).second) {
            normalized.push_back(item);
        }
    }

    const auto& children = folder ? folder->children : category.folders;
    for (const auto& child : children) {
        if (child && seenFolders.insert(child->name).second) {
            normalized.push_back(ExplorerItem{ ExplorerItemKind::Folder, child->name });
        }
    }
    for (const HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.categoryId == category.id
            && hotkey.folderPath == path
            && !hotkey.orderId.empty()
            && seenBinds.insert(hotkey.orderId).second) {
            normalized.push_back(ExplorerItem{ ExplorerItemKind::Bind, hotkey.orderId });
        }
    }

    items = std::move(normalized);
    for (auto& child : children) {
        if (child) {
            NormalizeExplorerOrderForDirectory(category, child.get());
        }
    }
}

void BinderModule::Impl::NormalizeExplorerOrders() {
    EnsureCategories();
    EnsureHotkeyOrderIds();
    for (BinderCategory& category : categories) {
        NormalizeExplorerOrderForDirectory(category, nullptr);
    }
}

void BinderModule::Impl::LogExplorerOrderValidation(std::string_view source) {
    int errors = 0;
    const auto logError = [&](const char* message, const BinderCategory& category, const std::vector<std::string>& path, const std::string& key) {
        if (errors++ >= 8) {
            return;
        }
        const std::string dirText = path.empty() ? std::string("<root>") : JoinPath(path);
        debuglog::WriteError(
            "[binder] explorer order invalid source=%.*s category=%s dir=%s key=%s reason=%s",
            static_cast<int>(source.size()),
            source.data(),
            category.id.c_str(),
            dirText.c_str(),
            key.c_str(),
            message);
    };

    const auto validateDirectory = [&](auto&& self, BinderCategory& category, FolderNode* folder) -> void {
        const std::vector<std::string> path = FolderPathForDirectory(folder);
        std::set<std::string> expectedFolders;
        const auto& children = folder ? folder->children : category.folders;
        for (const auto& child : children) {
            if (child) {
                expectedFolders.insert(child->name);
            }
        }

        std::set<std::string> expectedBinds;
        for (const HotkeyEntry& hotkey : hotkeys) {
            if (hotkey.categoryId == category.id && hotkey.folderPath == path && !hotkey.orderId.empty()) {
                expectedBinds.insert(hotkey.orderId);
            }
        }

        std::set<std::string> seenFolders;
        std::set<std::string> seenBinds;
        const std::vector<ExplorerItem>& items = folder ? folder->items : category.rootItems;
        for (const ExplorerItem& item : items) {
            if (item.kind == ExplorerItemKind::Folder) {
                if (expectedFolders.find(item.key) == expectedFolders.end()) {
                    logError("missing_folder", category, path, item.key);
                } else if (!seenFolders.insert(item.key).second) {
                    logError("duplicate_folder", category, path, item.key);
                }
                continue;
            }

            if (expectedBinds.find(item.key) == expectedBinds.end()) {
                logError("missing_bind", category, path, item.key);
            } else if (!seenBinds.insert(item.key).second) {
                logError("duplicate_bind", category, path, item.key);
            }
        }

        for (const std::string& name : expectedFolders) {
            if (seenFolders.find(name) == seenFolders.end()) {
                logError("folder_not_in_order", category, path, name);
            }
        }
        for (const std::string& orderId : expectedBinds) {
            if (seenBinds.find(orderId) == seenBinds.end()) {
                logError("bind_not_in_order", category, path, orderId);
            }
        }

        for (const auto& child : children) {
            if (child) {
                self(self, category, child.get());
            }
        }
    };

    for (BinderCategory& category : categories) {
        validateDirectory(validateDirectory, category, nullptr);
    }
}

void BinderModule::Impl::RemoveBindFromExplorerOrders(const std::string& orderId) {
    if (orderId.empty()) {
        return;
    }

    const auto removeFrom = [&](std::vector<ExplorerItem>& items) {
        items.erase(
            std::remove_if(items.begin(), items.end(), [&](const ExplorerItem& item) {
                return item.kind == ExplorerItemKind::Bind && item.key == orderId;
            }),
            items.end());
    };

    const auto recurse = [&](auto&& self, std::vector<std::unique_ptr<FolderNode>>& nodes) -> void {
        for (auto& node : nodes) {
            if (!node) {
                continue;
            }
            removeFrom(node->items);
            self(self, node->children);
        }
    };
    for (BinderCategory& category : categories) {
        removeFrom(category.rootItems);
        recurse(recurse, category.folders);
    }
}

void BinderModule::Impl::RemoveFolderFromParentOrder(FolderNode* folder) {
    if (!folder) {
        return;
    }
    auto& items = ItemsForFolder(folder->parent);
    items.erase(
        std::remove_if(items.begin(), items.end(), [&](const ExplorerItem& item) {
            return item.kind == ExplorerItemKind::Folder && item.key == folder->name;
        }),
        items.end());
}

bool BinderModule::Impl::InsertExplorerItem(FolderNode* folder, ExplorerItem item, int index) {
    if (item.key.empty()) {
        return false;
    }
    auto& items = ItemsForFolder(folder);
    items.erase(
        std::remove_if(items.begin(), items.end(), [&](const ExplorerItem& existing) {
            return existing.kind == item.kind && existing.key == item.key;
        }),
        items.end());
    index = std::clamp(index, 0, static_cast<int>(items.size()));
    items.insert(items.begin() + index, std::move(item));
    return true;
}

void BinderModule::Impl::AppendExplorerItemIfMissing(FolderNode* folder, ExplorerItem item) {
    const auto& items = ItemsForFolder(folder);
    const bool exists = std::any_of(items.begin(), items.end(), [&](const ExplorerItem& existing) {
        return existing.kind == item.kind && existing.key == item.key;
    });
    if (!exists) {
        InsertExplorerItem(folder, std::move(item), static_cast<int>(items.size()));
    }
}

bool BinderModule::Impl::MoveBindToExplorerDirectory(
    const int hotkeyIndex,
    FolderNode* targetFolder,
    std::optional<int> insertIndex,
    std::string_view source) {
    if (hotkeyIndex < 0 || hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        return false;
    }

    HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(hotkeyIndex)];
    const std::string orderId = hotkey.orderId.empty() ? AllocateHotkeyOrderId() : hotkey.orderId;
    hotkey.orderId = orderId;
    auto& targetItems = ItemsForFolder(targetFolder);
    if (insertIndex) {
        const auto existingIt = std::find_if(targetItems.begin(), targetItems.end(), [&](const ExplorerItem& item) {
            return item.kind == ExplorerItemKind::Bind && item.key == orderId;
        });
        if (existingIt != targetItems.end()) {
            const int oldIndex = static_cast<int>(std::distance(targetItems.begin(), existingIt));
            if (oldIndex < *insertIndex) {
                *insertIndex -= 1;
            }
        }
    }

    RemoveBindFromExplorerOrders(orderId);
    hotkey.categoryId = ActiveCategory().id;
    hotkey.folderPath = FolderPathForDirectory(targetFolder);
    InsertExplorerItem(
        targetFolder,
        ExplorerItem{ ExplorerItemKind::Bind, orderId },
        insertIndex.value_or(static_cast<int>(targetItems.size())));
    if (!StartsWith(source, "explorer_")) {
        currentFolder = targetFolder;
    }
    if (currentFolder == targetFolder) {
        SelectExplorerBind(hotkeyIndex, true);
    } else {
        SelectExplorerFolder(targetFolder, true);
    }

    debuglog::WriteInfo(
        "[binder] bind moved index=%d source=%.*s folder=%s",
        hotkeyIndex,
        static_cast<int>(source.size()),
        source.data(),
        JoinPath(hotkey.folderPath).c_str());
    SaveConfig();
    return true;
}

bool BinderModule::Impl::MoveBindToCategoryRoot(
    const int hotkeyIndex,
    std::string_view targetCategoryId,
    std::string_view source) {
    if (hotkeyIndex < 0 || hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        return false;
    }

    BinderCategory* targetCategory = FindCategoryById(targetCategoryId);
    if (!targetCategory) {
        return false;
    }
    if (targetCategory->id == ActiveCategory().id) {
        return MoveBindToExplorerDirectory(hotkeyIndex, nullptr, std::nullopt, source);
    }

    HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(hotkeyIndex)];
    const std::string orderId = hotkey.orderId.empty() ? AllocateHotkeyOrderId() : hotkey.orderId;
    hotkey.orderId = orderId;

    RemoveBindFromExplorerOrders(orderId);
    hotkey.categoryId = targetCategory->id;
    hotkey.folderPath.clear();
    targetCategory->rootItems.push_back(ExplorerItem{ ExplorerItemKind::Bind, orderId });
    targetCategory->lastOpenFolderPath.clear();
    ClearExplorerSelection();

    debuglog::WriteInfo(
        "[binder] bind moved index=%d source=%.*s category=%s folder=<root>",
        hotkeyIndex,
        static_cast<int>(source.size()),
        source.data(),
        targetCategory->id.c_str());
    SaveConfig();
    return true;
}

bool BinderModule::Impl::CanMoveFolderToExplorerDirectory(
    const int folderId,
    FolderNode* targetFolder,
    const int insertIndex,
    bool* noop) {
    if (noop) {
        *noop = false;
    }

    FolderListPos from;
    if (!FindFolderListPos(ActiveFolders(), nullptr, folderId, from)) {
        return false;
    }
    FolderNode* moving = (*from.list)[static_cast<std::size_t>(from.index)].get();
    if (!moving) {
        return false;
    }

    const std::vector<ExplorerItem>& targetOrder = ItemsForFolder(targetFolder);
    const auto folderVectorIndexForOrder = [&](int orderIndex) {
        int folderIndex = 0;
        for (int i = 0; i < std::min(orderIndex, static_cast<int>(targetOrder.size())); ++i) {
            const ExplorerItem& item = targetOrder[static_cast<std::size_t>(i)];
            if (item.kind == ExplorerItemKind::Folder) {
                ++folderIndex;
            }
        }
        return folderIndex;
    };

    FolderListPos dest{};
    dest.list = targetFolder ? &targetFolder->children : &ActiveFolders();
    dest.listParent = targetFolder;
    dest.index = folderVectorIndexForOrder(insertIndex);
    return IsValidFolderDropTarget(folderId, dest, noop);
}

bool BinderModule::Impl::MoveFolderToExplorerDirectory(
    const int folderId,
    FolderNode* targetFolder,
    int insertIndex,
    std::string_view source,
    const bool recordUndo) {
    FolderListPos from;
    if (!FindFolderListPos(ActiveFolders(), nullptr, folderId, from)) {
        return false;
    }
    FolderNode* moving = (*from.list)[static_cast<std::size_t>(from.index)].get();
    if (!moving) {
        return false;
    }

    const std::vector<ExplorerItem>& targetOrder = ItemsForFolder(targetFolder);
    const auto folderVectorIndexForOrder = [&](int orderIndex) {
        int folderIndex = 0;
        for (int i = 0; i < std::min(orderIndex, static_cast<int>(targetOrder.size())); ++i) {
            const ExplorerItem& item = targetOrder[static_cast<std::size_t>(i)];
            if (item.kind == ExplorerItemKind::Folder) {
                ++folderIndex;
            }
        }
        return folderIndex;
    };

    FolderListPos dest{};
    dest.list = targetFolder ? &targetFolder->children : &ActiveFolders();
    dest.listParent = targetFolder;
    dest.index = folderVectorIndexForOrder(insertIndex);

    bool noop = false;
    if (!IsValidFolderDropTarget(folderId, dest, &noop)) {
        debuglog::WriteError(
            "[binder] explorer folder move rejected id=%d source=%.*s",
            folderId,
            static_cast<int>(source.size()),
            source.data());
        Notify(
            NotificationGroup::Validation,
            NotificationSeverity::Error,
            UiSettings::Instance().Text(UiText::ToastFolderMoveInvalid),
            2200.0);
        return false;
    }

    FolderNode* oldParent = moving->parent;
    const std::string folderName = moving->name;
    auto& targetItems = ItemsForFolder(targetFolder);
    if (oldParent == targetFolder) {
        const auto oldOrderIt = std::find_if(targetItems.begin(), targetItems.end(), [&](const ExplorerItem& item) {
            return item.kind == ExplorerItemKind::Folder && item.key == folderName;
        });
        if (oldOrderIt != targetItems.end()) {
            const int oldOrderIndex = static_cast<int>(std::distance(targetItems.begin(), oldOrderIt));
            if (oldOrderIndex < insertIndex) {
                --insertIndex;
            }
        }
    }

    if (noop) {
        RemoveFolderFromParentOrder(moving);
        InsertExplorerItem(targetFolder, ExplorerItem{ ExplorerItemKind::Folder, folderName }, insertIndex);
        SelectExplorerFolder(moving, true);
        SaveConfig();
        return true;
    }

    RemoveFolderFromParentOrder(moving);
    if (!RelocateFolderNode(folderId, dest, recordUndo)) {
        AppendExplorerItemIfMissing(oldParent, ExplorerItem{ ExplorerItemKind::Folder, folderName });
        return false;
    }

    InsertExplorerItem(targetFolder, ExplorerItem{ ExplorerItemKind::Folder, folderName }, insertIndex);
    if (!StartsWith(source, "explorer_")) {
        currentFolder = targetFolder;
    }
    if (currentFolder == targetFolder) {
        SelectExplorerFolder(moving, true);
    } else {
        SelectExplorerFolder(targetFolder, true);
    }
    debuglog::WriteInfo(
        "[binder] explorer folder moved id=%d source=%.*s",
        folderId,
        static_cast<int>(source.size()),
        source.data());
    SaveConfig();
    return true;
}

bool BinderModule::Impl::MoveFolderToCategoryRoot(
    const int folderId,
    std::string_view targetCategoryId,
    std::string_view source) {
    BinderCategory& sourceCategory = ActiveCategory();
    BinderCategory* targetCategory = FindCategoryById(targetCategoryId);
    if (!targetCategory) {
        return false;
    }
    if (targetCategory->id == sourceCategory.id) {
        return MoveFolderToExplorerDirectory(
            folderId,
            nullptr,
            static_cast<int>(sourceCategory.rootItems.size()),
            source,
            true);
    }

    FolderListPos from;
    if (!FindFolderListPos(sourceCategory.folders, nullptr, folderId, from)) {
        return false;
    }

    FolderNode* moving = (*from.list)[static_cast<std::size_t>(from.index)].get();
    if (!moving) {
        return false;
    }

    const std::vector<std::string> oldPath = BuildFolderPath(moving);
    const std::string baseName = moving->name;
    std::string newName = baseName;
    for (int suffix = 2; !FolderNameUnique(targetCategory->folders, newName, nullptr); ++suffix) {
        newName = baseName + " " + std::to_string(suffix);
    }

    RemoveFolderFromParentOrder(moving);
    std::unique_ptr<FolderNode> extracted = std::move((*from.list)[static_cast<std::size_t>(from.index)]);
    from.list->erase(from.list->begin() + from.index);
    extracted->name = newName;
    extracted->parent = nullptr;

    targetCategory->folders.push_back(std::move(extracted));
    targetCategory->rootItems.push_back(ExplorerItem{ ExplorerItemKind::Folder, newName });
    targetCategory->lastOpenFolderPath.clear();

    const std::vector<std::string> newPath{ newName };
    for (HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.categoryId == sourceCategory.id && PathStartsWith(hotkey.folderPath, oldPath)) {
            hotkey.categoryId = targetCategory->id;
            hotkey.folderPath = ReplacePathPrefix(hotkey.folderPath, oldPath, newPath);
        }
    }

    folderMoveUndo_.reset();
    ClearExplorerSelection();

    debuglog::WriteInfo(
        "[binder] folder moved id=%d source=%.*s category=%s oldPath=%s newPath=%s",
        folderId,
        static_cast<int>(source.size()),
        source.data(),
        targetCategory->id.c_str(),
        JoinPath(oldPath).c_str(),
        JoinPath(newPath).c_str());
    SaveConfig();
    return true;
}

void BinderModule::Impl::Notify(
    NotificationGroup group,
    NotificationSeverity severity,
    std::string_view text,
    double durationMs) {
    if (!notificationManager || text.empty()) {
        return;
    }

    notificationManager->Notify(group, severity, text, durationMs);
}

void BinderModule::Impl::ShowUserPopup(std::string_view text, double durationMs) {
    if (!notificationManager || text.empty()) {
        return;
    }

    notificationManager->ShowUserPopup(text, NotificationSeverity::Success, durationMs);
}

void BinderModule::Impl::CaptureQuickMenuConditionSnapshot() {
    bool sampCursorActive = false;
    if (sampApi) {
        sampCursorActive = sampApi->is_chat_opened() || sampApi->isDialogActive() || sampApi->IsSampCursorActive();
    }

    ConditionRuntimeContext rawContext{};
    rawContext.helperUiCursorActive = false;
    rawContext.gameWindowForeground = gameInputForeground_;

    const bool windowsCursorActive = IsWindowsCursorActiveForConditions(&rawContext);
    quickMenuSampCursorActiveAtOpen = sampCursorActive;
    quickMenuWindowsCursorActiveAtOpen = windowsCursorActive;

    debuglog::WriteInfo(
        "[ui] quickmenu condition snapshot sampCursor=%d windowsCursor=%d",
        sampCursorActive ? 1 : 0,
        windowsCursorActive ? 1 : 0);
}

void BinderModule::Impl::ClearQuickMenuConditionSnapshot() {
    quickMenuSampCursorActiveAtOpen.reset();
    quickMenuWindowsCursorActiveAtOpen.reset();
}

ConditionRuntimeContext BinderModule::Impl::MakeConditionContext(bool helperUiCursorActive) const {
    ConditionRuntimeContext context{};
    context.helperUiCursorActive = helperUiCursorActive;
    context.gameWindowForeground = gameInputForeground_;
    if (helperUiCursorActive) {
        context.sampCursorActiveOverride = quickMenuSampCursorActiveAtOpen;
        context.windowsCursorActiveOverride = quickMenuWindowsCursorActiveAtOpen;
    }
    return context;
}

bool BinderModule::Impl::VisibleQuickMenuEntriesExist() const {
    const auto hotkeyVisible = [&](const int index) {
        if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
            return false;
        }
        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
        if (!hotkey.enabled || !hotkey.quickMenu) {
            return false;
        }
        const ConditionRuntimeContext context = MakeConditionContext(quickMenuOpen);
        return !ConditionsBlocked(hotkey.conditions, hotkey.conditionsCombine, sampApi, &context);
    };

    const auto categoryVisible = [&](const BinderCategory& category) {
        const ConditionRuntimeContext context = MakeConditionContext(quickMenuOpen);
        return category.quickMenu && !ConditionsBlocked(category.conditions, category.conditionsCombine, sampApi, &context);
    };

    const auto directoryHasVisibleEntries = [&](auto&& self, const BinderCategory& category, const FolderNode* folder) -> bool {
        const std::vector<ExplorerItem>& items = folder ? folder->items : category.rootItems;
        for (const ExplorerItem& item : items) {
            if (item.kind == ExplorerItemKind::Bind) {
                const int index = FindHotkeyIndexByOrderId(item.key);
                if (index >= 0 && hotkeys[static_cast<std::size_t>(index)].categoryId == category.id && hotkeyVisible(index)) {
                    return true;
                }
                continue;
            }

            FolderNode* child = FindFolderByNameInDirectory(category, const_cast<FolderNode*>(folder), item.key);
            if (child && FolderVisibleInQuickMenu(*child) && self(self, category, child)) {
                return true;
            }
        }
        return false;
    };

    for (const BinderCategory& category : categories) {
        if (categoryVisible(category) && directoryHasVisibleEntries(directoryHasVisibleEntries, category, nullptr)) {
            return true;
        }
    }
    return false;
}

bool BinderModule::Impl::FolderVisibleInQuickMenu(const FolderNode& folder) const {
    const ConditionRuntimeContext context = MakeConditionContext(quickMenuOpen);
    if (!folder.quickMenu || ConditionsBlocked(folder.conditions, folder.conditionsCombine, sampApi, &context)) {
        return false;
    }
    return !folder.parent || FolderVisibleInQuickMenu(*folder.parent);
}

void BinderModule::Impl::ResetQuickMenuVisualState() {
}

void BinderModule::Impl::ResetInputState() {
    keyTracker.Reset();
    pressedKeys.clear();
    asyncKeysDown.fill(false);
    capture.Stop();
    hotkeys::ResetCapturePopupState(capturePopupState);
    captureTarget = CaptureTarget::None;
    captureHotkeyIndex = -1;
    capturePopupInEditor = false;
    quickMenuOpen = false;
    ClearQuickMenuConditionSnapshot();
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

void BinderModule::Impl::SyncPressedKeysWithAsyncState() {
    auto isKeyDown = [](UINT key) {
        switch (key) {
        case VK_CONTROL:
            return (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0 || (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0;
        case VK_SHIFT:
            return (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0 || (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
        case VK_MENU:
            return (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0 || (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
        default:
            return (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0;
        }
    };

    bool changed = false;
    for (UINT key = 1; key <= 0xFF; ++key) {
        if (::hotkeys::NormalizeKey(key) != key || !::hotkeys::IsHotkeyKey(key)) {
            continue;
        }

        const bool down = isKeyDown(key);
        bool& wasDown = asyncKeysDown[static_cast<std::size_t>(key)];
        if (down == wasDown) {
            continue;
        }

        wasDown = down;
        if (down) {
            keyTracker.KeyDown(key);
        } else {
            keyTracker.KeyUp(key);
        }
        changed = true;
    }

    if (changed) {
        pressedKeys = keyTracker.Ordered();
    }
}

void BinderModule::Impl::Tick() {
    EnsureInitialized();
    PruneOutgoingGuards();
    PruneIncomingChatEchoGuards();
    ExpireTextConfirmations();

    if (prevFrameGameInputForeground_ && !gameInputForeground_) {
        ResetInputState();
    }
    if (!gameInputForeground_) {
        ProcessRunningBinds();
        prevFrameGameInputForeground_ = gameInputForeground_;
        return;
    }

    SyncPressedKeysWithAsyncState();
    const bool quickWasOpen = quickMenuOpen;
    const bool quickWasBlocked = quickMenuReopenBlocked;
    UpdateQuickMenuState();
    if (!quickWasOpen && quickMenuOpen) {
        CaptureQuickMenuConditionSnapshot();
    } else if (!quickMenuOpen) {
        ClearQuickMenuConditionSnapshot();
    }
    if (quickMenuOpen != quickWasOpen || quickMenuReopenBlocked != quickWasBlocked) {
        debuglog::WriteInfo(
            "[ui] quickmenu open %d->%d blocked %d->%d activation=%s comboHeld=%d overlayRender=%d",
            quickWasOpen ? 1 : 0,
            quickMenuOpen ? 1 : 0,
            quickWasBlocked ? 1 : 0,
            quickMenuReopenBlocked ? 1 : 0,
            quickMenuActivationMode == QuickMenuActivationMode::Toggle ? "toggle" : "hold",
            IsQuickMenuComboPressed() ? 1 : 0,
            WantsOverlayRender() ? 1 : 0);
    }
    if (!quickWasOpen && quickMenuOpen) {
        quickMenuFocusPending = true;
    } else if (!quickMenuOpen) {
        quickMenuFocusPending = false;
    }
    ProcessHotkeys();
    ProcessRunningBinds();
    prevFrameGameInputForeground_ = gameInputForeground_;
}

void BinderModule::Impl::Shutdown() {
    if (inputDialog && inputDialog->hotkeyIndex >= 0 && inputDialog->hotkeyIndex < static_cast<int>(hotkeys.size())) {
        hotkeys[inputDialog->hotkeyIndex].awaitingInput = false;
    }

    editor.active = false;
    inputDialog.reset();
    runningBinds.clear();
    outgoingGuards.clear();
    incomingChatEchoGuards.clear();
    pendingBindTagActions.clear();
    ResetInputState();
}

void BinderModule::Impl::ReloadConfig() {
    debuglog::WriteInfo("BinderModule::ReloadConfig begin");
    Shutdown();
    LoadConfig();
    configLoaded = true;
    EnsureRootFolder();
    RefreshNumbers();
    ConnectHooks();
    debuglog::WriteInfo(
        "BinderModule::ReloadConfig done categories=%llu hotkeys=%llu",
        static_cast<unsigned long long>(categories.size()),
        static_cast<unsigned long long>(hotkeys.size()));
}

bool BinderModule::Impl::WantsOverlayRender() const {
    return quickMenuOpen
        || inputDialog.has_value()
        || capture.Active()
        || bindLinesPopupPending
        || bindLinesTarget >= 0;
}

bool BinderModule::Impl::WantsInputCapture() const {
    return inputDialog.has_value()
        || capture.Active()
        || bindLinesPopupPending
        || bindLinesTarget >= 0;
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
            Notify(
                NotificationGroup::BinderErrors,
                NotificationSeverity::Error,
                ui.Format(UiText::HotkeyConflictFormat, description.c_str()),
                2800.0);
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

        const std::string label = BuildBindDisplayLabel(hotkey);
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

std::string BinderModule::Impl::QuickMenuHotkeyText() const {
    return ::hotkeys::ToString(CurrentQuickMenuHotkey());
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
    desc.name = BuildBindDisplayLabel(hotkey);
    desc.folder = JoinPath(hotkey.folderPath);
    if (const RunningBind* running = FindRunningBind(runtimeId); running && !running->categoryName.empty()) {
        desc.category = running->categoryName;
    } else if (const BinderCategory* category = FindCategoryById(hotkey.categoryId)) {
        desc.category = category->name;
    }
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

std::string BinderModule::Impl::BuildThiscategoryTagValue(std::uint64_t runtimeId) const {
    const BindTagContextDesc desc = DescribeBindTagContext(runtimeId);
    return desc.hotkeyIndex < 0 ? std::string{} : desc.category;
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
    const std::string scopeCategoryId = selector.contextHotkeyIndex >= 0
        ? hotkeys[static_cast<std::size_t>(selector.contextHotkeyIndex)].categoryId
        : activeCategoryId;
    const BinderCategory* scopeCategory = FindCategoryById(scopeCategoryId);
    if (!scopeCategory) {
        scopeCategory = &ActiveCategory();
    }
    if (!selector.folderQuery.empty()) {
        std::vector<std::vector<std::string>> folderPaths;
        CollectFolderPaths(scopeCategory->folders, folderPaths);
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
        if (scopeCategory->folders.empty() || !scopeCategory->folders.front()) {
            error = "no_folders";
            return {};
        }
        scopePath = BuildFolderPath(scopeCategory->folders.front().get());
    }

    std::vector<int> candidates;
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        if (hotkeys[i].categoryId == scopeCategory->id && hotkeys[i].folderPath == scopePath) {
            candidates.push_back(static_cast<int>(i));
        }
    }

    if (selector.all) {
        return candidates;
    }

    const auto hotkeyDisplayName = [this](int index) {
        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
        return BuildBindDisplayLabel(hotkey);
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
        Notify(NotificationGroup::TagErrors, NotificationSeverity::Error, DescribeBindTagError(actionName, result.error), 2600.0);
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
            Notify(NotificationGroup::TagErrors, NotificationSeverity::Error, DescribeBindTagError(actionName, result.error), 2600.0);
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
        Notify(NotificationGroup::TagErrors, NotificationSeverity::Error, DescribeBindTagError(actionName, error), 2600.0);
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
        Notify(NotificationGroup::TagErrors, NotificationSeverity::Error, DescribeBindTagError(actionName, result.error), 2600.0);
    }
    return result;
}

RunningBind* BinderModule::Impl::FindRunningBind(std::uint64_t hotkeyRuntimeId) {
    return FindRunningBindByRuntimeId(runningBinds, hotkeyRuntimeId);
}

const RunningBind* BinderModule::Impl::FindRunningBind(std::uint64_t hotkeyRuntimeId) const {
    return FindRunningBindByRuntimeId(runningBinds, hotkeyRuntimeId);
}

RunningBind* BinderModule::Impl::FindRunningBindForHotkey(int index) {
    return FindRunningBind(HotkeyRuntimeIdAt(hotkeys, index));
}

const RunningBind* BinderModule::Impl::FindRunningBindForHotkey(int index) const {
    return FindRunningBind(HotkeyRuntimeIdAt(hotkeys, index));
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
            Notify(
                NotificationGroup::TagErrors,
                NotificationSeverity::Error,
                DescribeBindTagError(pending.actionName, result.error),
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
    const bool alreadyRunning = FindRunningBind(hotkey.runtimeId) != nullptr;
    if (hotkey.runtimeId == 0 || alreadyRunning) {
        return;
    }

    const BinderCategory* category = FindCategoryById(hotkey.categoryId);
    runningBinds.push_back(RunningBind{
        hotkey.runtimeId,
        hotkey.categoryId,
        category ? category->name : std::string{},
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

void BinderModule::Impl::PruneIncomingChatEchoGuards() {
    const double now = static_cast<double>(GetTickCount64());
    incomingChatEchoGuards.erase(
        std::remove_if(incomingChatEchoGuards.begin(), incomingChatEchoGuards.end(), [&](const IncomingChatEchoGuard& guard) {
            return guard.expiresAtMs <= now;
        }),
        incomingChatEchoGuards.end());
}

std::string BinderModule::Impl::CurrentLocalPlayerName() const {
    if (!sampApi) {
        return {};
    }

    const int localId = sampApi->Local_ID();
    if (localId < 0) {
        return {};
    }

    const std::string localName = Trim(sampApi->GetNameID(localId));
    return EqualNoCase(localName, "UNKNOWN") ? std::string() : localName;
}

void BinderModule::Impl::RegisterIncomingChatEchoGuard(std::string text) {
    text = NormalizeTriggerText(text);
    if (text.empty()) {
        return;
    }

    incomingChatEchoGuards.push_back(IncomingChatEchoGuard{
        std::move(text),
        CurrentLocalPlayerName(),
        static_cast<double>(GetTickCount64() + kIncomingChatEchoGuardTimeoutMs),
    });
    while (incomingChatEchoGuards.size() > 64) {
        incomingChatEchoGuards.erase(incomingChatEchoGuards.begin());
    }
}

bool BinderModule::Impl::ConsumeIncomingChatEchoGuard(
    std::string_view normalizedText,
    std::string_view normalizedPrefixedText) {
    const auto matchesGuard = [](std::string_view candidate, const IncomingChatEchoGuard& guard) {
        if (candidate.empty() || guard.text.empty()) {
            return false;
        }
        if (candidate == guard.text) {
            return true;
        }
        if (guard.localPlayerName.empty()) {
            return false;
        }
        return ContainsNoCase(candidate, guard.localPlayerName) && EndsWith(candidate, guard.text);
    };

    for (auto it = incomingChatEchoGuards.begin(); it != incomingChatEchoGuards.end(); ++it) {
        if (matchesGuard(normalizedText, *it) || matchesGuard(normalizedPrefixedText, *it)) {
            incomingChatEchoGuards.erase(it);
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

bool BinderModule::Impl::TryBeginPendingConfirmation(
    HotkeyEntry& hotkey,
    std::string_view sourceKind,
    const std::string& sourceText,
    bool waitForResolution) {
    const ConditionRuntimeContext context = MakeConditionContext(false);
    if (hotkey.waitingTextConfirmation || hotkey.awaitingInput
        || ConditionsBlocked(hotkey.conditions, hotkey.conditionsCombine, sampApi, &context)) {
        return false;
    }

    const double now = static_cast<double>(GetTickCount64());
    hotkey.waitingTextConfirmation = true;
    hotkey.pendingTriggerText = sourceText;
    hotkey.pendingTriggerSource = std::string(sourceKind);
    hotkey.textConfirmationDeadlineMs = waitForResolution ? 0.0 : now + kTextConfirmTimeoutMs;

    UiSettings& ui = UiSettings::Instance();
    const std::string confirmText = ui.Format(
        UiText::ToastConfirmPrompt,
        ConfirmationSourceLabel(sourceKind),
        BuildBindDisplayLabel(hotkey).c_str(),
        ::hotkeys::KeyName(hotkey.textConfirmation.key).c_str(),
        ::hotkeys::KeyName(hotkey.textConfirmation.cancelKey).c_str());
    Notify(
        NotificationGroup::Confirmation,
        NotificationSeverity::Warning,
        confirmText,
        waitForResolution ? 4000.0 : 2500.0);
    return true;
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
        if (hotkey.commandConfirmation.enabled
            && TryBeginPendingConfirmation(hotkey, "command", normalized, hotkey.commandConfirmation.waitForResolution)) {
            continue;
        }
        TryEnqueueHotkey(static_cast<int>(i), 0, "command", normalized);
    }
    return handled;
}

void BinderModule::Impl::OnOutgoingChat(const std::string& text) {
    const std::string normalized = NormalizeActivationText(text);
    if (normalized.empty() || ConsumeOutgoingGuard("chat", normalized)) {
        return;
    }
    if (!normalized.empty() && normalized.front() == '/') {
        return;
    }
    const bool handled = OnTextTriggerEvent(normalized, "outgoing_chat");
    if (handled) {
        RegisterIncomingChatEchoGuard(normalized);
    }
}

void BinderModule::Impl::OnIncomingMessage(const IncomingMessageEvent& message) {
    const std::string normalizedText = NormalizeTriggerText(message.text);
    std::string normalizedPrefixedText;
    if (!Trim(message.prefix).empty()) {
        normalizedPrefixedText = NormalizeTriggerText(message.prefix + " " + message.text);
    }

    if (normalizedText.empty() || ConsumeOutgoingGuard("echo", normalizedText)) {
        return;
    }
    if (ConsumeIncomingChatEchoGuard(normalizedText, normalizedPrefixedText)) {
        return;
    }

    const double now = static_cast<double>(GetTickCount64());
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        HotkeyEntry& hotkey = hotkeys[i];
        if (!hotkey.enabled) {
            continue;
        }

        const std::string* matchedSource = nullptr;
        if (MatchTextTrigger(normalizedText, hotkey)) {
            matchedSource = &normalizedText;
        } else if (!normalizedPrefixedText.empty() && MatchTextTrigger(normalizedPrefixedText, hotkey)) {
            matchedSource = &normalizedPrefixedText;
        }

        if (!matchedSource) {
            continue;
        }

        TryDispatchTextTriggerMatch(static_cast<int>(i), hotkey, *matchedSource, "incoming_server", now);
    }
}

void BinderModule::Impl::ExpireTextConfirmations() {
    const double now = static_cast<double>(GetTickCount64());
    for (HotkeyEntry& hotkey : hotkeys) {
        if (!hotkey.waitingTextConfirmation || hotkey.textConfirmationDeadlineMs <= 0.0) {
            continue;
        }
        if (now >= hotkey.textConfirmationDeadlineMs) {
            const char* confirmationLabel = ConfirmationSourceLabel(hotkey.pendingTriggerSource);
            hotkey.waitingTextConfirmation = false;
            hotkey.textConfirmationDeadlineMs = 0.0;
            hotkey.pendingTriggerText.clear();
            hotkey.pendingTriggerSource.clear();
            Notify(
                NotificationGroup::Confirmation,
                NotificationSeverity::Warning,
                UiSettings::Instance().Format(
                    UiText::ToastBindConfirmExpired,
                    confirmationLabel,
                    BuildBindDisplayLabel(hotkey).c_str()),
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
            const char* confirmationLabel = ConfirmationSourceLabel(hotkey.pendingTriggerSource);
            hotkey.waitingTextConfirmation = false;
            hotkey.textConfirmationDeadlineMs = 0.0;
            hotkey.pendingTriggerText.clear();
            hotkey.pendingTriggerSource.clear();
            Notify(
                NotificationGroup::Confirmation,
                NotificationSeverity::Warning,
                UiSettings::Instance().Format(
                    UiText::ToastBindCanceled,
                    confirmationLabel,
                    BuildBindDisplayLabel(hotkey).c_str()),
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

bool BinderModule::Impl::TryDispatchTextTriggerMatch(
    int index,
    HotkeyEntry& hotkey,
    const std::string& sourceText,
    std::string_view sourceKind,
    double now) {
    if (now < hotkey.debounceUntilMs) {
        return false;
    }

    hotkey.debounceUntilMs = now + kHotkeyDebounceMs;
    if (hotkey.textConfirmation.enabled
        && TryBeginPendingConfirmation(hotkey, sourceKind, sourceText, hotkey.textConfirmation.waitForResolution)) {
        return true;
    }

    TryEnqueueHotkey(index, 0, sourceKind, sourceText);
    return true;
}

bool BinderModule::Impl::OnTextTriggerEvent(const std::string& sourceText, std::string_view sourceKind) {
    const double now = static_cast<double>(GetTickCount64());
    bool handled = false;
    for (std::size_t i = 0; i < hotkeys.size(); ++i) {
        HotkeyEntry& hotkey = hotkeys[i];
        if (!hotkey.enabled || !MatchTextTrigger(sourceText, hotkey)) {
            continue;
        }
        handled = TryDispatchTextTriggerMatch(static_cast<int>(i), hotkey, sourceText, sourceKind, now) || handled;
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
    const bool isRunning = IsHotkeyRunning(index);
    if (!hotkey.enabled || hotkey.awaitingInput || hotkey.waitingTextConfirmation || isRunning) {
        return false;
    }

    std::string conditionMessage;
    const ConditionRuntimeContext context = MakeConditionContext(source == "quick_menu");
    if (ConditionsBlocked(hotkey.conditions, hotkey.conditionsCombine, sampApi, &context, &conditionMessage)) {
        if (!conditionMessage.empty() && source != "incoming_server" && source != "outgoing_chat" && source != "outgoing_command") {
            Notify(
                NotificationGroup::BinderErrors,
                NotificationSeverity::Warning,
                UiSettings::Instance().Format(UiText::ToastConditionBlocked, conditionMessage.c_str()),
                2200.0);
        }
        return false;
    }

    if (!hotkey.inputs.empty()) {
        if (inputDialog.has_value() && inputDialog->hotkeyIndex != index) {
            Notify(
                NotificationGroup::BinderErrors,
                NotificationSeverity::Warning,
                UiSettings::Instance().Text(UiText::ToastFinishActiveInput),
                2500.0);
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
            Notify(NotificationGroup::BinderErrors, NotificationSeverity::Error, UiSettings::Instance().Text(UiText::ToastSendLocalFailed), 2500.0);
        }
        break;
    }
    case 1:
    {
        const std::string expandedText = expandWithTags(text);
        RegisterOutgoingGuard(!expandedText.empty() && expandedText.front() == '/' ? "command" : "chat", expandedText);
        RegisterOutgoingGuard("echo", NormalizeTriggerText(expandedText));
        if (!sampApi || !sampApi->process_chat_input(expandedText, true)) {
            Notify(NotificationGroup::BinderErrors, NotificationSeverity::Error, UiSettings::Instance().Text(UiText::ToastSendSampFailed), 2500.0);
        }
        break;
    }
    case 2:
    {
        const std::string expandedText = expandWithTags(text);
        const auto previewText = [](std::string_view value) {
            constexpr std::size_t kMaxPreviewLength = 96;
            if (value.size() <= kMaxPreviewLength) {
                return std::string(value);
            }
            return std::string(value.substr(0, kMaxPreviewLength - 3)) + "...";
        };
        const char* const sendKind = !expandedText.empty() && expandedText.front() == '/' ? "command" : "chat";
        const std::string preview = previewText(expandedText);
        debuglog::WriteInfo(
            "Binder DoSend via_samp begin kind=%s len=%llu text=%s",
            sendKind,
            static_cast<unsigned long long>(expandedText.size()),
            preview.c_str());
        RegisterOutgoingGuard(sendKind, expandedText);
        RegisterOutgoingGuard("echo", NormalizeTriggerText(expandedText));
        const bool ok = sampApi && sampApi->send_chat(expandedText, true);
        if (!ok) {
            const std::string error = sampApi ? sampApi->lastError() : "SampApi is null";
            debuglog::WriteError(
                "Binder DoSend via_samp failed kind=%s len=%llu text=%s error=%s",
                sendKind,
                static_cast<unsigned long long>(expandedText.size()),
                preview.c_str(),
                error.c_str());
            Notify(NotificationGroup::BinderErrors, NotificationSeverity::Error, UiSettings::Instance().Text(UiText::ToastSendSampFailed), 2500.0);
        } else {
            debuglog::WriteInfo(
                "Binder DoSend via_samp ok kind=%s len=%llu text=%s",
                sendKind,
                static_cast<unsigned long long>(expandedText.size()),
                preview.c_str());
        }
        break;
    }
    case 3:
        static_cast<void>(expandWithTags(text));
        break;
    case 4:
        if (!sampApi || !sampApi->Set_ChatInputText(text, false, true)) {
            Notify(NotificationGroup::BinderErrors, NotificationSeverity::Error, UiSettings::Instance().Text(UiText::ToastInsertChatFailed), 2500.0);
        }
        break;
    case 5:
        if (!sampApi || !sampApi->Set_ChatInputText(text, true, true)) {
            Notify(NotificationGroup::BinderErrors, NotificationSeverity::Error, UiSettings::Instance().Text(UiText::ToastOpenChatFailed), 2500.0);
        }
        break;
    case 6:
        if (!sampApi || !sampApi->sampSetDialogEditboxText(text, true)) {
            Notify(NotificationGroup::SampDialogErrors, NotificationSeverity::Error, UiSettings::Instance().Text(UiText::ToastInsertDialogFailed), 2500.0);
        }
        break;
    case 7:
        if (!SetClipboardUtf8Text(expandWithTags(text))) {
            Notify(NotificationGroup::BinderErrors, NotificationSeverity::Error, UiSettings::Instance().Text(UiText::ToastClipboardFailed), 2500.0);
        }
        break;
    case 8:
        debuglog::WriteInfo("Binder log: %s", expandWithTags(text).c_str());
        break;
    case 9:
        ShowUserPopup(expandWithTags(text), 2200.0);
        break;
    default:
        Notify(
            NotificationGroup::BinderErrors,
            NotificationSeverity::Error,
            UiSettings::Instance().Format(UiText::ToastUnknownSendMethod, method),
            2500.0);
        break;
    }
}

int BinderModule::Impl::RemapHotkeysFolderPrefix(
    const std::vector<std::string>& oldPath,
    const std::vector<std::string>& newPath) {
    int changed = 0;
    const std::string categoryId = ActiveCategory().id;
    for (HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.categoryId != categoryId || !PathStartsWith(hotkey.folderPath, oldPath)) {
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
    const std::string categoryId = ActiveCategory().id;
    for (HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.categoryId != categoryId || !PathStartsWith(hotkey.folderPath, fromPath)) {
            continue;
        }
        hotkey.folderPath = toPath;
        ++changed;
    }
    return changed;
}

int BinderModule::Impl::DeleteHotkeysFromFolderPath(const std::vector<std::string>& fromPath) {
    int removed = 0;
    const std::string categoryId = ActiveCategory().id;
    std::vector<std::uint64_t> runtimeIdsToStop;
    runtimeIdsToStop.reserve(hotkeys.size());
    for (const HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.categoryId == categoryId && PathStartsWith(hotkey.folderPath, fromPath)) {
            runtimeIdsToStop.push_back(hotkey.runtimeId);
        }
    }
    for (const std::uint64_t runtimeId : runtimeIdsToStop) {
        StopHotkeyByRuntimeId(runtimeId);
    }
    for (const HotkeyEntry& hotkey : hotkeys) {
        if (hotkey.categoryId == categoryId && PathStartsWith(hotkey.folderPath, fromPath)) {
            RemoveBindFromExplorerOrders(hotkey.orderId);
        }
    }

    hotkeys.erase(
        std::remove_if(hotkeys.begin(), hotkeys.end(), [&](const HotkeyEntry& hotkey) {
            if (hotkey.categoryId != categoryId || !PathStartsWith(hotkey.folderPath, fromPath)) {
                return false;
            }
            ++removed;
            return true;
        }),
        hotkeys.end());

    if (removed > 0) {
        RefreshNumbers();
        if (explorerSelection.kind == ExplorerSelectionKind::Bind) {
            ClearExplorerSelection();
        }
        bindDeleteTarget = -1;
        moveBindTarget = -1;
        bindLinesTarget = -1;
    }
    return removed;
}

void BinderModule::Impl::MoveBindToFolderPath(
    const int hotkeyIndex,
    const std::vector<std::string>& folderPath,
    std::string_view source) {
    if (hotkeyIndex < 0 || hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        debuglog::WriteError(
            "[binder] bind move rejected index=%d source=%.*s reason=out_of_range",
            hotkeyIndex,
            static_cast<int>(source.size()),
            source.data());
        return;
    }

    FolderNode* targetFolder = nullptr;
    const std::string folderPathText = folderPath.empty() ? std::string("<unfiled>") : JoinPath(folderPath);
    if (!folderPath.empty()) {
        targetFolder = FindFolderByPath(ActiveFolders(), folderPath);
        if (!targetFolder) {
            debuglog::WriteError(
                "[binder] bind move rejected index=%d source=%.*s reason=folder_not_found path=%s",
                hotkeyIndex,
                static_cast<int>(source.size()),
                source.data(),
                folderPathText.c_str());
            return;
        }
    }

    (void)MoveBindToExplorerDirectory(hotkeyIndex, targetFolder, std::nullopt, source);
}

std::string BinderModule::Impl::NextFolderNameForParent(FolderNode* parent) const {
    const auto& siblings = parent ? parent->children : ActiveFolders();
    const std::string baseName = UiSettings::Instance().Text(UiText::BinderNewFolder);
    std::string name = baseName;
    for (int suffix = 2; !FolderNameUnique(siblings, name); ++suffix) {
        name = baseName + " " + std::to_string(suffix);
    }
    return name;
}

void BinderModule::Impl::BeginInlineCreateFolder(FolderNode* parent) {
    folderInlineEdit = {};
    folderInlineEdit.mode = FolderInlineEditMode::Create;
    folderInlineEdit.parent = parent;
    folderInlineEdit.name = NextFolderNameForParent(parent);
    folderInlineEdit.focusPending = true;
    ClearExplorerSelection();
}

void BinderModule::Impl::BeginInlineRenameFolder(FolderNode* folder) {
    if (!folder) {
        return;
    }

    folderInlineEdit = {};
    folderInlineEdit.mode = FolderInlineEditMode::Rename;
    folderInlineEdit.target = folder;
    folderInlineEdit.parent = folder->parent;
    folderInlineEdit.name = folder->name;
    folderInlineEdit.focusPending = true;
    SelectExplorerFolder(folder);
}

void BinderModule::Impl::CancelInlineFolderEdit() {
    folderInlineEdit = {};
}

bool BinderModule::Impl::IsInlineRenamingFolder(const FolderNode* folder) const {
    return folderInlineEdit.mode == FolderInlineEditMode::Rename && folderInlineEdit.target == folder;
}

bool BinderModule::Impl::CommitInlineFolderEdit() {
    const std::string name = SanitizeFolderName(folderInlineEdit.name);
    if (name.empty()) {
        Notify(
            NotificationGroup::Validation,
            NotificationSeverity::Error,
            UiSettings::Instance().Text(UiText::ValidationFolderNameRequired),
            2200.0);
        folderInlineEdit.focusPending = true;
        return false;
    }

    if (folderInlineEdit.mode == FolderInlineEditMode::Rename) {
        FolderNode* target = folderInlineEdit.target;
        if (!target) {
            CancelInlineFolderEdit();
            return false;
        }

        auto& siblings = target->parent ? target->parent->children : ActiveFolders();
        if (!FolderNameUnique(siblings, name, target)) {
            Notify(
                NotificationGroup::Validation,
                NotificationSeverity::Error,
                UiSettings::Instance().Text(UiText::ValidationFolderNameUnique),
                2200.0);
            folderInlineEdit.focusPending = true;
            return false;
        }

        if (target->name != name) {
            const std::vector<std::string> oldPath = BuildFolderPath(target);
            const std::string oldName = target->name;
            target->name = name;
            const std::vector<std::string> newPath = BuildFolderPath(target);
            RemapHotkeysFolderPrefix(oldPath, newPath);
            auto& order = ItemsForFolder(target->parent);
            for (ExplorerItem& item : order) {
                if (item.kind == ExplorerItemKind::Folder && item.key == oldName) {
                    item.key = name;
                    break;
                }
            }
        }

        SelectExplorerFolder(target);
        CancelInlineFolderEdit();
        SaveConfig();
        return true;
    }

    if (folderInlineEdit.mode == FolderInlineEditMode::Create) {
        FolderNode* parent = folderInlineEdit.parent;
        auto& siblings = parent ? parent->children : ActiveFolders();
        if (!FolderNameUnique(siblings, name)) {
            Notify(
                NotificationGroup::Validation,
                NotificationSeverity::Error,
                UiSettings::Instance().Text(UiText::ValidationFolderNameUnique),
                2200.0);
            folderInlineEdit.focusPending = true;
            return false;
        }

        auto folder = std::make_unique<FolderNode>();
        folder->id = nextFolderId++;
        folder->name = name;
        folder->conditions.assign(static_cast<std::size_t>(ConditionId::Count), false);
        folder->parent = parent;
        FolderNode* created = folder.get();
        if (parent) {
            parent->children.push_back(std::move(folder));
        } else {
            ActiveFolders().push_back(std::move(folder));
        }
        AppendExplorerItemIfMissing(parent, ExplorerItem{ ExplorerItemKind::Folder, created->name });

        currentFolder = parent;
        SelectExplorerFolder(created, true);
        CancelInlineFolderEdit();
        SaveConfig();
        return true;
    }

    CancelInlineFolderEdit();
    return false;
}

bool BinderModule::Impl::CanDeleteFolder(const FolderNode* folder) const {
    if (!folder) {
        return false;
    }
    return true;
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

    if (editor.isNew) {
        editor.draft.folderPath = CurrentFolderPath();
        editor.draft.label.clear();
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
    if (index >= 0) {
        SelectExplorerBind(index);
    } else {
        ClearExplorerSelection();
    }
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

    editor.activeTab = tab;
}

HotkeyEntry BinderModule::Impl::BuildEditorComparableDraft() const {
    HotkeyEntry comparable = editor.draft;
    comparable.conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    comparable.conditionsCombine = ConditionCombineMode::RequireAny;
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

    const HotkeyEntry& current = hotkeys[static_cast<std::size_t>(editor.hotkeyIndex)];
    const std::vector<std::string>& folderPath = current.folderPath;
    const std::string& categoryId = current.categoryId;
    int previous = -1;
    int next = -1;
    int last = -1;
    for (int index = 0; index < static_cast<int>(hotkeys.size()); ++index) {
        if (hotkeys[static_cast<std::size_t>(index)].categoryId != categoryId
            || hotkeys[static_cast<std::size_t>(index)].folderPath != folderPath) {
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
            if (hotkeys[static_cast<std::size_t>(index)].categoryId == categoryId
                && hotkeys[static_cast<std::size_t>(index)].folderPath == folderPath) {
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
    const std::string triggerText = Trim(current.textTrigger.text);
    const std::string commandText = Trim(current.command);
    if (!HasRequiredFirstMessage(current)) {
        errors.push_back(ui.Text(UiText::ValidationFirstMessageRequired));
    }

    const BinderCategory* editorCategory = FindCategoryById(current.categoryId);
    if (!editorCategory) {
        editorCategory = &ActiveCategory();
    }
    if (!current.folderPath.empty() && !FindFolderByPath(editorCategory->folders, current.folderPath)) {
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

    if ((current.textConfirmation.enabled || current.commandConfirmation.enabled)
        && current.textConfirmation.key == current.textConfirmation.cancelKey) {
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
    const bool replacingExisting =
        !editor.isNew && editor.hotkeyIndex >= 0 && editor.hotkeyIndex < static_cast<int>(hotkeys.size());
    const std::string previousOrderId = replacingExisting
        ? hotkeys[static_cast<std::size_t>(editor.hotkeyIndex)].orderId
        : std::string{};
    const std::vector<std::string> previousFolderPath = replacingExisting
        ? hotkeys[static_cast<std::size_t>(editor.hotkeyIndex)].folderPath
        : std::vector<std::string>{};
    HotkeyEntry saved = BuildEditorComparableDraft();
    saved.label = Trim(saved.label);
    saved.keys = ::hotkeys::NormalizeCombo(saved.keys, saved.hotkeyMode);
    saved.command = Trim(saved.command);
    saved.textTrigger.text = Trim(saved.textTrigger.text);
    saved.conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    saved.conditionsCombine = ConditionCombineMode::RequireAny;
    saved.comboActive = false;
    saved.awaitingInput = false;
    saved.waitingTextConfirmation = false;
    saved.lastRepeatPressed.clear();
    saved.pendingTriggerText.clear();
    saved.pendingTriggerSource.clear();
    saved.lastActivatedAtMs = 0.0;
    saved.debounceUntilMs = 0.0;
    if (saved.categoryId.empty() || !FindCategoryById(saved.categoryId)) {
        saved.categoryId = ActiveCategory().id;
    }
    if (replacingExisting) {
        saved.runtimeId = hotkeys[static_cast<std::size_t>(editor.hotkeyIndex)].runtimeId;
        saved.orderId = previousOrderId;
    }
    if (saved.orderId.empty()) {
        saved.orderId = AllocateHotkeyOrderId();
    }

    if (editor.isNew || editor.hotkeyIndex < 0 || editor.hotkeyIndex >= static_cast<int>(hotkeys.size())) {
        hotkeys.push_back(std::move(saved));
        selectedBindIndex = static_cast<int>(hotkeys.size() - 1);
    } else {
        hotkeys[editor.hotkeyIndex] = std::move(saved);
        selectedBindIndex = editor.hotkeyIndex;
    }

    if (selectedBindIndex >= 0 && selectedBindIndex < static_cast<int>(hotkeys.size())) {
        HotkeyEntry& selected = hotkeys[static_cast<std::size_t>(selectedBindIndex)];
        BinderCategory* category = FindCategoryById(selected.categoryId);
        if (!category) {
            selected.categoryId = ActiveCategory().id;
            category = &ActiveCategory();
        }
        if (category->id != ActiveCategory().id) {
            SelectCategory(category->id);
        }
        FolderNode* folder = selected.folderPath.empty() ? nullptr : FindFolderByPath(category->folders, selected.folderPath);
        if (!selected.folderPath.empty() && !folder) {
            selected.folderPath.clear();
        }
        if (!replacingExisting || previousFolderPath != selected.folderPath) {
            RemoveBindFromExplorerOrders(selected.orderId);
        }
        AppendExplorerItemIfMissing(folder, ExplorerItem{ ExplorerItemKind::Bind, selected.orderId });
        currentFolder = folder;
        SelectExplorerBind(selectedBindIndex, true);
    }

    RefreshNumbers();
    SaveConfig();
    editor = {};
}

bool BinderModule::Impl::CopyTextToClipboard(std::string_view text, bool showSuccessToast) {
    if (!SetClipboardUtf8Text(text)) {
        Notify(
            NotificationGroup::BinderErrors,
            NotificationSeverity::Error,
            UiSettings::Instance().Text(UiText::ToastClipboardFailed),
            2500.0);
        return false;
    }

    if (showSuccessToast) {
        Notify(
            NotificationGroup::Success,
            NotificationSeverity::Success,
            UiSettings::Instance().Text(UiText::ToastClipboardCopied),
            1400.0);
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
    duplicated.orderId = AllocateHotkeyOrderId();

    hotkeys.insert(hotkeys.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(duplicated));
    RefreshNumbers();
    selectedBindIndex = index + 1;
    BinderCategory* category = FindCategoryById(hotkeys[static_cast<std::size_t>(selectedBindIndex)].categoryId);
    if (!category) {
        category = &ActiveCategory();
        hotkeys[static_cast<std::size_t>(selectedBindIndex)].categoryId = category->id;
    }
    FolderNode* folder = hotkeys[static_cast<std::size_t>(selectedBindIndex)].folderPath.empty()
        ? nullptr
        : FindFolderByPath(category->folders, hotkeys[static_cast<std::size_t>(selectedBindIndex)].folderPath);
    if (category->id != ActiveCategory().id) {
        SelectCategory(category->id);
    }
    SelectExplorerBind(selectedBindIndex, true);
    auto& items = ItemsForFolder(folder);
    int insertIndex = static_cast<int>(items.size());
    const std::string originalOrderId = hotkeys[static_cast<std::size_t>(index)].orderId;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].kind == ExplorerItemKind::Bind && items[i].key == originalOrderId) {
            insertIndex = static_cast<int>(i + 1);
            break;
        }
    }
    InsertExplorerItem(
        folder,
        ExplorerItem{ ExplorerItemKind::Bind, hotkeys[static_cast<std::size_t>(selectedBindIndex)].orderId },
        insertIndex);
    SaveConfig();
}

bool BinderModule::Impl::IsValidFolderDropTarget(int moveId, const FolderListPos& dest, bool* noop) {
    if (noop) {
        *noop = false;
    }

    FolderListPos from{};
    if (!FindFolderListPos(ActiveFolders(), nullptr, moveId, from) || !dest.list) {
        return false;
    }

    FolderNode* node = (*from.list)[static_cast<std::size_t>(from.index)].get();
    if (dest.list == &node->children) {
        return false;
    }
    if (from.list != dest.list && !FolderNameUnique(*dest.list, node->name, nullptr)) {
        return false;
    }

    FolderNode* const destListOwner = FindListOwner(ActiveFolders(), dest.list);
    if (destListOwner && node != destListOwner && IsUnderOrEqual(node, destListOwner)) {
        return false;
    }

    if (from.list == dest.list && (dest.index == from.index || dest.index == from.index + 1)) {
        if (noop) {
            *noop = true;
        }
        return true;
    }
    return true;
}

bool BinderModule::Impl::RelocateFolderNode(int moveId, FolderListPos dest, bool recordUndo) {
    FolderListPos from;
    auto& activeFolders = ActiveFolders();
    if (!FindFolderListPos(activeFolders, nullptr, moveId, from)) {
        return false;
    }
    if (!dest.list) {
        return false;
    }
    FolderNode* node = (*from.list)[static_cast<std::size_t>(from.index)].get();
    if (dest.list == &node->children) {
        return false;
    }
    if (from.list != dest.list) {
        if (!FolderNameUnique(*dest.list, node->name, nullptr)) {
            return false;
        }
    }
    {
        FolderNode* const destListOwner = FindListOwner(activeFolders, dest.list);
        if (destListOwner) {
            if (node != destListOwner && IsUnderOrEqual(node, destListOwner)) {
                return false;
            }
        }
    }
    if (from.list == dest.list && from.index == dest.index) {
        return false;
    }

    const std::vector<std::string> oldPath = BuildFolderPath(node);
    const int savedListParentId =
        from.list == &activeFolders ? -1 : (from.listParent != nullptr ? from.listParent->id : -1);
    const int savedListIndex = from.index;

    int dix = dest.index;
    if (from.list == dest.list && from.index < dix) {
        dix -= 1;
    }

    std::unique_ptr<FolderNode> extracted = std::move((*from.list)[static_cast<std::size_t>(from.index)]);
    from.list->erase(from.list->begin() + from.index);
    {
        const int destSz = static_cast<int>(dest.list->size());
        if (dix < 0) {
            dix = 0;
        }
        if (dix > destSz) {
            dix = destSz;
        }
    }

    FolderNode* newParent = FindListOwner(activeFolders, dest.list);
    if (dest.list == &activeFolders) {
        newParent = nullptr;
    }
    node->parent = newParent;
    dest.list->insert(dest.list->begin() + dix, std::move(extracted));

    const std::vector<std::string> newPath = BuildFolderPath(node);
    if (oldPath != newPath) {
        RemapHotkeysFolderPrefix(oldPath, newPath);
    }
    if (recordUndo) {
        folderMoveUndo_ = FolderMoveUndo{node->id, savedListParentId, savedListIndex};
    } else {
        folderMoveUndo_.reset();
    }
    debuglog::WriteInfo(
        "[binder] folder moved id=%d oldParent=%d oldIndex=%d newParent=%d newIndex=%d",
        node->id,
        savedListParentId,
        savedListIndex,
        newParent ? newParent->id : -1,
        dix);
    ExpandFolderBranch(node);
    SaveConfig();
    return true;
}

void BinderModule::Impl::ApplyFolderMoveUndo() {
    if (!folderMoveUndo_) {
        return;
    }
    const int moveId = folderMoveUndo_->nodeId;
    const int oldP = folderMoveUndo_->oldListParentId;
    const int oldIdx = folderMoveUndo_->oldIndex;
    folderMoveUndo_.reset();
    FolderListPos d{};
    if (oldP < 0) {
        d.list = &ActiveFolders();
        d.index = oldIdx;
        d.listParent = nullptr;
    } else {
        FolderNode* p = FindFolderByIdR(ActiveFolders(), oldP);
        if (!p) {
            return;
        }
        d.list = &p->children;
        d.index = oldIdx;
        d.listParent = p;
    }
    if (!RelocateFolderNode(moveId, d, false)) {
        Notify(
            NotificationGroup::Validation,
            NotificationSeverity::Error,
            UiSettings::Instance().Text(UiText::ToastFolderMoveInvalid),
            2000.0);
    }
}

void BinderModule::Impl::DrawCategoryTabs() {
    EnsureCategories();
    UiSettings& ui = UiSettings::Instance();

    if (!ImGui::BeginTabBar("##binder_category_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        return;
    }

    for (std::size_t i = 0; i < categories.size(); ++i) {
        BinderCategory& category = categories[i];
        const std::string categoryId = category.id;
        const bool active = category.id == activeCategoryId;
        const ImGuiTabItemFlags flags = categoryTabSelectionTargetId == category.id
            ? ImGuiTabItemFlags_SetSelected
            : 0;
        const std::string label = category.name + "##binder_category_" + category.id;
        const bool tabOpen = ImGui::BeginTabItem(label.c_str(), nullptr, flags);
        if (tabOpen) {
            if (!active) {
                SelectCategory(category.id);
            }
            if (categoryTabSelectionTargetId == category.id) {
                categoryTabSelectionTargetId.clear();
            }
        }

        if (ImGui::BeginDragDropTarget()) {
            bool accepted = false;
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(kBindDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                    accepted = true;
                    if (payload->IsDelivery()) {
                        const int hotkeyIndex = *static_cast<const int*>(payload->Data);
                        MoveBindToCategoryRoot(hotkeyIndex, categoryId, "category_tab_drop");
                    }
                }
            }
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(kFolderDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                    accepted = true;
                    if (payload->IsDelivery()) {
                        const int folderId = *static_cast<const int*>(payload->Data);
                        MoveFolderToCategoryRoot(folderId, categoryId, "category_tab_drop");
                    }
                }
            }
            if (accepted) {
                ImGui::SetTooltip("%s", category.name.c_str());
            }
            ImGui::EndDragDropTarget();
        }

        const std::string contextId = "##binder_category_context_" + categoryId;
        if (ImGui::BeginPopupContextItem(contextId.c_str())) {
            if (ImGui::MenuItem(ui.Text(UiText::FolderRename))) {
                BeginRenameCategory(categoryId);
            }
            if (ImGui::MenuItem(ui.Text(UiText::ShowInQuickMenu), nullptr, category.quickMenu)) {
                category.quickMenu = !category.quickMenu;
                SaveConfig();
            }
            if (ImGui::MenuItem(ui.Text(UiText::CategoryQuickMenuConditions))) {
                categoryConditionsTargetId = categoryId;
                categoryConditionsPopupPending = true;
            }
            ImGui::Separator();
            const bool canMoveLeft = i > 0;
            if (!canMoveLeft) {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem(ui.Text(UiText::CategoryMoveLeft)) && canMoveLeft) {
                MoveCategoryByOffset(categoryId, -1);
            }
            if (!canMoveLeft) {
                ImGui::EndDisabled();
            }
            const bool canMoveRight = i + 1 < categories.size();
            if (!canMoveRight) {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem(ui.Text(UiText::CategoryMoveRight)) && canMoveRight) {
                MoveCategoryByOffset(categoryId, 1);
            }
            if (!canMoveRight) {
                ImGui::EndDisabled();
            }
            ImGui::Separator();
            const bool canDelete = categories.size() > 1;
            if (!canDelete) {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem(ui.Text(UiText::Delete)) && canDelete) {
                categoryDeleteTargetId = categoryId;
                categoryDeleteMoveTargetId.clear();
                for (const BinderCategory& target : categories) {
                    if (target.id != categoryId) {
                        categoryDeleteMoveTargetId = target.id;
                        break;
                    }
                }
                categoryDeletePopupPending = true;
            }
            if (!canDelete) {
                ImGui::EndDisabled();
            }
            ImGui::EndPopup();
        }

        if (tabOpen) {
            ImGui::EndTabItem();
        }
    }

    if (ImGui::TabItemButton("+##binder_add_category", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
        BinderCategory category = MakeDefaultCategory();
        category.name = NextCategoryName();
        const std::string id = category.id;
        categories.push_back(std::move(category));
        SelectCategory(id);
        BeginRenameCategory(id);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", ui.Text(UiText::CategoryAdd));
    }

    ImGui::EndTabBar();
}

void BinderModule::Impl::DrawCategoryPopups() {
    UiSettings& ui = UiSettings::Instance();

    if (categoryConditionsPopupPending) {
        BinderCategory* category = FindCategoryById(categoryConditionsTargetId);
        if (category) {
            if (DrawConditionFlagsPopup(
                    "##binder_category_conditions",
                    categoryConditionsPopupPending,
                    UiText::CategoryConditions,
                    category->conditions,
                    &category->conditionsCombine)) {
                SaveConfig();
            }
            if (!categoryConditionsPopupPending && !ImGui::IsPopupOpen("##binder_category_conditions")) {
                categoryConditionsTargetId.clear();
            }
        } else {
            categoryConditionsPopupPending = false;
            categoryConditionsTargetId.clear();
        }
    }

    if (categoryRenamePopupPending) {
        ImGui::OpenPopup("##binder_category_rename");
        categoryRenamePopupPending = false;
    }
    if (ImGui::BeginPopupModal("##binder_category_rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        BinderCategory* category = FindCategoryById(categoryRenameTargetId);
        if (!category) {
            categoryRenameTargetId.clear();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        ImGui::TextUnformatted(ui.Text(UiText::CategoryRenameTitle));
        ImGui::Separator();
        InputTextString("##binder_category_name", categoryRenameBuffer, ImGuiInputTextFlags_AutoSelectAll, 128);
        bool save = ImGui::Button(ui.Text(UiText::Save));
        ImGui::SameLine();
        const bool cancel = ImGui::Button(ui.Text(UiText::Cancel));
        if (ImGui::Shortcut(ImGuiKey_Enter, ImGuiInputFlags_RouteFocused | ImGuiInputFlags_RouteOverActive)) {
            save = true;
        }
        if (save) {
            const std::string name = SanitizeFolderName(categoryRenameBuffer);
            if (name.empty()) {
                Notify(NotificationGroup::Validation, NotificationSeverity::Error, ui.Text(UiText::ValidationCategoryNameRequired), 2200.0);
                categoryRenamePopupPending = true;
            } else if (!CategoryNameUnique(name, category)) {
                Notify(NotificationGroup::Validation, NotificationSeverity::Error, ui.Text(UiText::ValidationCategoryNameUnique), 2200.0);
                categoryRenamePopupPending = true;
            } else {
                category->name = name;
                categoryRenameTargetId.clear();
                SaveConfig();
                ImGui::CloseCurrentPopup();
            }
        } else if (cancel) {
            categoryRenameTargetId.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (categoryDeletePopupPending) {
        ImGui::OpenPopup("##binder_category_delete");
        categoryDeletePopupPending = false;
    }
    if (ImGui::BeginPopupModal("##binder_category_delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        BinderCategory* category = FindCategoryById(categoryDeleteTargetId);
        if (!category || categories.size() <= 1) {
            categoryDeleteTargetId.clear();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        ImGui::TextWrapped("%s", ui.Text(UiText::DeleteCategoryQuestion));
        ImGui::TextDisabled("%s", category->name.c_str());
        ImGui::Spacing();
        if (ImGui::BeginCombo(
                ui.Text(UiText::CategoryMoveContentsTarget),
                FindCategoryById(categoryDeleteMoveTargetId) ? FindCategoryById(categoryDeleteMoveTargetId)->name.c_str() : "")) {
            for (const BinderCategory& target : categories) {
                if (target.id == category->id) {
                    continue;
                }
                const bool selected = target.id == categoryDeleteMoveTargetId;
                if (ImGui::Selectable(target.name.c_str(), selected)) {
                    categoryDeleteMoveTargetId = target.id;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button(ui.Text(UiText::Cancel))) {
            categoryDeleteTargetId.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::DeleteCategoryMoveContents))) {
            DeleteCategory(categoryDeleteTargetId, categoryDeleteMoveTargetId, false);
            categoryDeleteTargetId.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::DeleteCategoryAll))) {
            DeleteCategory(categoryDeleteTargetId, {}, true);
            categoryDeleteTargetId.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void BinderModule::Impl::DrawFolderPopups() {
    if (folderConditionsTarget) {
        if (DrawConditionFlagsPopup(
                "##binder_folder_conditions",
                folderConditionsPopupPending,
                UiText::FolderConditions,
                folderConditionsTarget->conditions,
                &folderConditionsTarget->conditionsCombine)) {
            SaveConfig();
        }
        if (!folderConditionsPopupPending && !ImGui::IsPopupOpen("##binder_folder_conditions")) {
            folderConditionsTarget = nullptr;
        }
    } else {
        folderConditionsPopupPending = false;
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
        auto removeFolderNode = [&]() {
            auto& siblings = folderDeleteTarget->parent ? folderDeleteTarget->parent->children : ActiveFolders();
            siblings.erase(
                std::remove_if(siblings.begin(), siblings.end(), [&](const std::unique_ptr<FolderNode>& item) {
                    return item.get() == folderDeleteTarget;
                }),
                siblings.end());
        };
        auto finishFolderDelete = [&](FolderNode* fallbackFolder, const std::vector<std::string>& removedPath) {
            const auto selectedPath = selectedFolder ? BuildFolderPath(selectedFolder) : std::vector<std::string>{};
            const auto currentPath = CurrentFolderPath();
            FolderNode* removedParent = folderDeleteTarget ? folderDeleteTarget->parent : nullptr;
            const ExplorerItem removedItem{
                ExplorerItemKind::Folder,
                folderDeleteTarget ? folderDeleteTarget->name : std::string{},
            };
            const std::vector<ExplorerItem> beforeItems = ItemsForFolder(removedParent);
            const bool currentWasInRemovedTree =
                (!currentPath.empty() && PathStartsWith(currentPath, removedPath)) || currentFolder == folderDeleteTarget;
            const bool selectionWasInRemovedTree =
                (!selectedPath.empty() && PathStartsWith(selectedPath, removedPath)) || selectedFolder == folderDeleteTarget;
            if (folderConditionsTarget && IsUnderOrEqual(folderDeleteTarget, folderConditionsTarget)) {
                folderConditionsTarget = nullptr;
                folderConditionsPopupPending = false;
            }
            if ((folderInlineEdit.target && IsUnderOrEqual(folderDeleteTarget, folderInlineEdit.target))
                || (folderInlineEdit.parent && IsUnderOrEqual(folderDeleteTarget, folderInlineEdit.parent))) {
                CancelInlineFolderEdit();
            }
            RemoveFolderFromParentOrder(folderDeleteTarget);
            removeFolderNode();
            if (currentWasInRemovedTree) {
                currentFolder = fallbackFolder;
            }
            folderDeleteTarget = nullptr;
            SaveConfig();
            if ((currentWasInRemovedTree || selectionWasInRemovedTree) && currentFolder == removedParent) {
                SelectExplorerNeighborAfterRemoval(removedParent, removedItem, beforeItems);
            } else if (selectionWasInRemovedTree) {
                ClearExplorerSelection();
            }
            ImGui::CloseCurrentPopup();
        };

        if (ImGui::Button(UiSettings::Instance().Text(UiText::Cancel))) {
            folderDeleteTarget = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button(UiSettings::Instance().Text(UiText::DeleteFolderMoveContentsHere))) {
            if (CanDeleteFolder(folderDeleteTarget)) {
                NormalizeExplorerOrders();
                FolderNode* fallbackFolder = folderDeleteTarget->parent;
                const std::vector<std::string> removedPath = BuildFolderPath(folderDeleteTarget);
                const std::vector<std::string> targetPath = FolderPathForDirectory(fallbackFolder);
                auto& targetChildren = fallbackFolder ? fallbackFolder->children : ActiveFolders();

                const std::vector<ExplorerItem> oldItems = folderDeleteTarget->items;
                for (const ExplorerItem& item : oldItems) {
                    if (item.kind == ExplorerItemKind::Bind) {
                        const int index = FindHotkeyIndexByOrderId(item.key);
                        if (index >= 0
                            && hotkeys[static_cast<std::size_t>(index)].categoryId == ActiveCategory().id
                            && hotkeys[static_cast<std::size_t>(index)].folderPath == removedPath) {
                            RemoveBindFromExplorerOrders(item.key);
                            hotkeys[static_cast<std::size_t>(index)].folderPath = targetPath;
                            AppendExplorerItemIfMissing(fallbackFolder, ExplorerItem{ ExplorerItemKind::Bind, item.key });
                        }
                        continue;
                    }

                    auto childIt = std::find_if(
                        folderDeleteTarget->children.begin(),
                        folderDeleteTarget->children.end(),
                        [&](const std::unique_ptr<FolderNode>& child) {
                            return child && child->name == item.key;
                        });
                    if (childIt == folderDeleteTarget->children.end()) {
                        continue;
                    }

                    FolderNode* child = childIt->get();
                    const std::vector<std::string> oldChildPath = BuildFolderPath(child);
                    const std::string baseName = child->name;
                    std::string newName = baseName;
                    for (int suffix = 2; !FolderNameUnique(targetChildren, newName, folderDeleteTarget); ++suffix) {
                        newName = baseName + " " + std::to_string(suffix);
                    }
                    child->name = newName;
                    child->parent = fallbackFolder;
                    const std::vector<std::string> newChildPath = targetPath;
                    std::vector<std::string> fullNewChildPath = newChildPath;
                    fullNewChildPath.push_back(newName);
                    RemapHotkeysFolderPrefix(oldChildPath, fullNewChildPath);

                    std::unique_ptr<FolderNode> extracted = std::move(*childIt);
                    folderDeleteTarget->children.erase(childIt);
                    targetChildren.push_back(std::move(extracted));
                    AppendExplorerItemIfMissing(fallbackFolder, ExplorerItem{ ExplorerItemKind::Folder, newName });
                }

                for (HotkeyEntry& hotkey : hotkeys) {
                    if (hotkey.categoryId == ActiveCategory().id && hotkey.folderPath == removedPath) {
                        RemoveBindFromExplorerOrders(hotkey.orderId);
                        hotkey.folderPath = targetPath;
                        AppendExplorerItemIfMissing(fallbackFolder, ExplorerItem{ ExplorerItemKind::Bind, hotkey.orderId });
                    }
                }

                finishFolderDelete(fallbackFolder, removedPath);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(UiSettings::Instance().Text(UiText::DeleteFolderAll))) {
            if (CanDeleteFolder(folderDeleteTarget)) {
                FolderNode* fallbackFolder = folderDeleteTarget->parent;
                const auto removedPath = BuildFolderPath(folderDeleteTarget);
                DeleteHotkeysFromFolderPath(removedPath);
                finishFolderDelete(fallbackFolder, removedPath);
            }
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

            if (SmallIconActionButton(ui_icons::AngleUp, "##binder_input_move_up", ui.Text(UiText::MoveUp), actionButtonSize)
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
            if (SmallIconActionButton(ui_icons::AngleDown, "##binder_input_move_down", ui.Text(UiText::MoveDown), actionButtonSize)
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
            if (SmallIconActionButton(ui_icons::Clone, "##binder_input_duplicate", ui.Text(UiText::ActionDuplicate), actionButtonSize)) {
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
            if (SmallIconActionButton(ui_icons::Delete, "##binder_input_delete", ui.Text(UiText::Delete), actionButtonSize)) {
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
                    if (SmallIconActionButton(ui_icons::Clone, "##binder_copy_input_placeholder", ui.Text(UiText::CopyPlaceholder), actionButtonSize)) {
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
                                if (SmallIconActionButton(ui_icons::Clone, "##binder_button_duplicate", ui.Text(UiText::ActionDuplicate), actionButtonSize)) {
                                    const int selectedButtonIndex = editor.selectedInputButtonIndex;
                                    InputButton duplicate = input.buttons[static_cast<std::size_t>(selectedButtonIndex)];
                                    input.buttons.insert(input.buttons.begin() + selectedButtonIndex + 1, std::move(duplicate));
                                    editor.selectedInputButtonIndex = selectedButtonIndex + 1;
                                    syncSelectedButtonsText(input);
                                }
                                ImGui::SameLine();
                                if (SmallIconActionButton(ui_icons::AngleUp, "##binder_button_move_up", ui.Text(UiText::MoveUp), actionButtonSize)
                                    && editor.selectedInputButtonIndex > 0) {
                                    const int selectedButtonIndex = editor.selectedInputButtonIndex;
                                    std::swap(
                                        input.buttons[static_cast<std::size_t>(selectedButtonIndex)],
                                        input.buttons[static_cast<std::size_t>(selectedButtonIndex - 1)]);
                                    editor.selectedInputButtonIndex = selectedButtonIndex - 1;
                                    syncSelectedButtonsText(input);
                                }
                                ImGui::SameLine();
                                if (SmallIconActionButton(ui_icons::AngleDown, "##binder_button_move_down", ui.Text(UiText::MoveDown), actionButtonSize)
                                    && editor.selectedInputButtonIndex + 1 < static_cast<int>(input.buttons.size())) {
                                    const int selectedButtonIndex = editor.selectedInputButtonIndex;
                                    std::swap(
                                        input.buttons[static_cast<std::size_t>(selectedButtonIndex)],
                                        input.buttons[static_cast<std::size_t>(selectedButtonIndex + 1)]);
                                    editor.selectedInputButtonIndex = selectedButtonIndex + 1;
                                    syncSelectedButtonsText(input);
                                }
                                ImGui::SameLine();
                                if (SmallIconActionButton(ui_icons::Delete, "##binder_button_delete", ui.Text(UiText::Delete), actionButtonSize)) {
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
    (void)DrawConditionFlagsPopup(
        "##binder_editor_conditions",
        editor.conditionsPopupPending,
        UiText::BlockingConditions,
        editor.draft.conditions,
        &editor.draft.conditionsCombine);
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
    const float dragHandleWidth = std::ceil(ImGui::CalcTextSize(ui_icons::MoveRows).x + ScaleUi(4.0f));
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

    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", ui.Text(UiText::EditorScenarioHint));
    ImGui::SameLine();
    const std::string addStepButton = std::string(ui_icons::Plus) + " " + ui.Text(UiText::EditorAddStep);
    const std::string multiInputButton = std::string(ui_icons::Bars) + " " + ui.Text(UiText::EditorOpenMultiInput);
    const float addStepButtonWidth = ScaleUi(96.0f);
    const float multiInputButtonWidth = ScaleUi(142.0f);
    const float currentX = ImGui::GetCursorPosX();
    const float availableX = ImGui::GetContentRegionAvail().x;
    const float buttonsWidth = addStepButtonWidth + style.ItemSpacing.x + multiInputButtonWidth;
    if (availableX > buttonsWidth) {
        ImGui::SetCursorPosX(currentX + availableX - buttonsWidth);
    }
    if (ImGui::Button(addStepButton.c_str(), ImVec2(addStepButtonWidth, 0.0f))) {
        editor.draft.messages.push_back(HotkeyMessage{ "", 0, editor.bulkMethod });
    }
    ImGui::SameLine();
    if (ImGui::Button(multiInputButton.c_str(), ImVec2(multiInputButtonWidth, 0.0f))) {
        SyncEditorMessagesToMulti();
        editor.multiInputPopupPending = true;
    }
    ImGui::Spacing();

    int removeIndex = -1;
    int duplicateIndex = -1;
    int moveSourceIndex = -1;
    int moveTargetIndex = -1;
    const ImVec2 stepsTableOuterSize(
        ImGui::GetContentRegionAvail().x,
        std::max(ScaleUi(180.0f), ImGui::GetContentRegionAvail().y));
    if (ImGui::BeginTable(
            "##binder_editor_steps",
            5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp
                | ImGuiTableFlags_ScrollY,
            stepsTableOuterSize)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##drag", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, dragColumnWidth);
        ImGui::TableSetupColumn(
            UiSettings::Instance().Text(UiText::EditorColumnMessage),
            ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize);
        ImGui::TableSetupColumn(
            UiSettings::Instance().Text(UiText::EditorColumnPauseMs),
            ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
            ScaleUi(110.0f));
        ImGui::TableSetupColumn(
            UiSettings::Instance().Text(UiText::EditorColumnDestination),
            ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
            destinationColumnWidth);
        ImGui::TableSetupColumn(
            UiSettings::Instance().Text(UiText::ColumnActions),
            ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
            actionsColumnWidth);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < editor.draft.messages.size(); ++i) {
            HotkeyMessage& message = editor.draft.messages[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableSetColumnIndex(0);
            CenterNextItemHorizontally(dragHandleWidth);
            IconOnlyButton(ui_icons::MoveRows, "##step_drag", ui.Text(UiText::EditorMoveStep), ImVec2(dragHandleWidth, dragHandleHeight));
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
            if (SmallIconActionButton(ui_icons::Clone, "##step_duplicate", ui.Text(UiText::EditorDuplicateStep), actionButtonSize)) {
                duplicateIndex = static_cast<int>(i);
            }
            ImGui::SameLine(0.0f, actionButtonsSpacing);
            if (SmallIconActionButton(ui_icons::Delete, "##step_delete", ui.Text(UiText::Delete), actionButtonSize)) {
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

    SyncEditorMessagesToMulti();
}

void BinderModule::Impl::DrawEditorMultiInputPopup() {
    UiSettings& ui = UiSettings::Instance();
    const std::string title = std::string(ui.Text(UiText::EditorMultiInputTitle)) + "##binder_editor_multi_input_popup";
    if (editor.multiInputPopupPending) {
        ImGui::OpenPopup(title.c_str());
        editor.multiInputPopupPending = false;
    }
    ImGui::SetNextWindowSize(ScaleUi(640.0f, 460.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    bool bulkChanged = false;
    if (ImGui::BeginTable("##binder_editor_bulk_meta_popup", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("method", ImGuiTableColumnFlags_WidthStretch, 0.70f);
        ImGui::TableSetupColumn("interval", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorColumnDestination));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##binder_editor_bulk_method_popup", SendMethodLabel(editor.bulkMethod))) {
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
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputInt("##binder_editor_bulk_interval_popup", &editor.bulkIntervalMs)) {
            bulkChanged = true;
        }
        if (editor.bulkIntervalMs < 0) {
            editor.bulkIntervalMs = 0;
            bulkChanged = true;
        }

        ImGui::EndTable();
    }

    const float footerReserve = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing() + ScaleUi(12.0f);
    const bool textChanged = InputTextMultilineString(
        "##binder_editor_multi_text_popup",
        editor.multiText,
        ImVec2(-FLT_MIN, std::max(ScaleUi(180.0f), ImGui::GetContentRegionAvail().y - footerReserve)),
        0,
        2048);
    if (bulkChanged || textChanged) {
        ApplyEditorMultiToDraft(bulkChanged);
    }
    ImGui::TextDisabled("%s", ui.Text(UiText::EditorMultiInputHint));
    ImGui::Spacing();
    const float doneWidth = ScaleUi(120.0f);
    const float cursorX = ImGui::GetCursorPosX();
    const float availX = ImGui::GetContentRegionAvail().x;
    if (availX > doneWidth) {
        ImGui::SetCursorPosX(cursorX + availX - doneWidth);
    }
    if (ImGui::Button(ui.Text(UiText::Done), ImVec2(doneWidth, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void BinderModule::Impl::DrawEditorInline() {
    UiSettings& ui = UiSettings::Instance();
    EnsureRootFolder();
    const auto [prevIndex, nextIndex] = EditorNeighborIndices();
    const bool hasUnsavedChanges = EditorHasUnsavedChanges();

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
                RequestEditorAction(EditorState::PendingAction::Close);
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
            ImGui::SetWindowFontScale(0.75f);
            const std::string headerBreadcrumb = EllipsizeText(breadcrumb, std::max(0.0f, ImGui::GetContentRegionAvail().x * 2.0f));
            ImGui::TextDisabled("%s", headerBreadcrumb.c_str());
            ImGui::SetWindowFontScale(1.0f);
            if (headerBreadcrumb != breadcrumb && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", breadcrumb.c_str());
            }

            ImGui::TableSetColumnIndex(1);
            const float previousButtonWidth = ScaleUi(148.0f);
            const float nextButtonWidth = ScaleUi(136.0f);
            const float headerActionWidth = previousButtonWidth + nextButtonWidth + itemSpacingX;
            alignRight(headerActionWidth);
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(16.0f, 14.0f));
    if (ImGui::BeginChild(
            "##binder_editor_launch_panel",
            ImVec2(0.0f, 0.0f),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        ImGui::SeparatorText(ui.Text(UiText::EditorPrimaryLaunch));

        if (ImGui::BeginTable("##binder_editor_primary_launch", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 0.50f);
            ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 0.50f);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", ui.Text(UiText::NameOptional));
            if (editor.focusNamePending) {
                ImGui::SetKeyboardFocusHere();
                editor.focusNamePending = false;
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            InputTextString("##binder_editor_name", editor.draft.label, ImGuiInputTextFlags_AutoSelectAll, 160);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", ui.Text(UiText::ColumnHotkey));
            const std::string hotkeyText = editor.draft.keys.empty()
                ? ui.Text(UiText::HotkeyNotSet)
                : ::hotkeys::ToString(editor.draft.keys, editor.draft.hotkeyMode);
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

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", ui.Text(UiText::Command));
            ToggleChip(ui_icons::Terminal, ui.Text(UiText::Command), "##binder_editor_command_enabled", editor.draft.commandEnabled, ScaleUi(118.0f));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            InputTextString("##binder_editor_command", editor.draft.command, ImGuiInputTextFlags_AutoSelectAll, 128);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", ui.Text(UiText::EditorOpenConditions));
            const int activeConditions = static_cast<int>(std::count(editor.draft.conditions.begin(), editor.draft.conditions.end(), true));
            const std::string conditionsSummary = activeConditions > 0
                ? ui.Format(UiText::EditorConditionsCount, activeConditions)
                : std::string(ui.Text(UiText::EditorConditionsNone));
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", conditionsSummary.c_str());
            ImGui::SameLine();
            if (ImGui::Button((std::string(ui_icons::Sliders) + " " + ui.Text(UiText::Change) + "##conditions").c_str(), ScaleUi(132.0f, 0.0f))) {
                editor.conditionsPopupPending = true;
            }

            ImGui::EndTable();
        }

        const bool hasAdvancedLaunch =
            editor.draft.quickMenu
            || editor.draft.repeatMode
            || editor.draft.textTrigger.enabled
            || editor.draft.textTrigger.pattern
            || editor.draft.textConfirmation.enabled
            || editor.draft.commandConfirmation.enabled;
        ImGui::Spacing();
        ImGui::SetNextItemOpen(hasAdvancedLaunch, ImGuiCond_Once);
        if (ImGui::CollapsingHeader(ui.Text(UiText::EditorAdvancedLaunch))) {
            ImGui::Spacing();
            ToggleChip(ui_icons::Bolt, ui.Text(UiText::EditorToggleQuickMenu), "##binder_editor_quick_menu", editor.draft.quickMenu, ScaleUi(150.0f));
            ImGui::SameLine();
            ToggleChip(ui_icons::AngleDown, ui.Text(UiText::Repeat), "##binder_editor_repeat_mode", editor.draft.repeatMode, ScaleUi(112.0f));
            ImGui::SameLine();
            ImGui::BeginDisabled(!editor.draft.repeatMode);
            ImGui::SetNextItemWidth(ScaleUi(112.0f));
            ImGui::InputInt("##binder_editor_repeat", &editor.draft.repeatIntervalMs);
            if (editor.draft.repeatIntervalMs < 0) {
                editor.draft.repeatIntervalMs = 0;
            }
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::TextDisabled("%s", ui.Text(UiText::TextTrigger));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", ui.Text(UiText::EditorTriggerHint));
            }
            ToggleChip(ui_icons::MessageDots, ui.Text(UiText::EditorToggleTrigger), "##binder_editor_trigger_enabled", editor.draft.textTrigger.enabled, ScaleUi(118.0f));
            ImGui::SameLine();
            ImGui::BeginDisabled(!editor.draft.textTrigger.enabled);
            ToggleChip(ui_icons::BracketsCurly, ui.Text(UiText::EditorTogglePattern), "##binder_editor_trigger_pattern", editor.draft.textTrigger.pattern, ScaleUi(118.0f));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            InputTextWithHintString(
                "##binder_editor_trigger",
                ui.Text(UiText::EditorTriggerExample),
                editor.draft.textTrigger.text,
                ImGuiInputTextFlags_AutoSelectAll,
                256);
            ImGui::EndDisabled();

            ImGui::Spacing();
            ToggleChip(
                ui_icons::Check,
                ui.Text(UiText::EditorToggleTextConfirm),
                "##binder_editor_text_confirm",
                editor.draft.textConfirmation.enabled,
                ScaleUi(164.0f));
            ImGui::SameLine();
            ToggleChip(
                ui_icons::Check,
                ui.Text(UiText::EditorToggleCommandConfirm),
                "##binder_editor_command_confirm",
                editor.draft.commandConfirmation.enabled,
                ScaleUi(172.0f));
            if (editor.draft.textConfirmation.enabled || editor.draft.commandConfirmation.enabled) {
                ImGui::Spacing();
                if (editor.draft.textConfirmation.enabled) {
                    ImGui::Checkbox(ui.Text(UiText::TriggerWaitWithoutTimeout), &editor.draft.textConfirmation.waitForResolution);
                }
                if (editor.draft.commandConfirmation.enabled) {
                    ImGui::Checkbox(ui.Text(UiText::CommandWaitWithoutTimeout), &editor.draft.commandConfirmation.waitForResolution);
                }

                if (ImGui::BeginTable("##binder_editor_confirmation_keys", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
                    ImGui::TableSetupColumn("confirm", ImGuiTableColumnFlags_WidthStretch, 0.5f);
                    ImGui::TableSetupColumn("cancel", ImGuiTableColumnFlags_WidthStretch, 0.5f);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled(
                        "%s",
                        ui.Format(UiText::ConfirmKeyFormat, ::hotkeys::KeyName(editor.draft.textConfirmation.key).c_str()).c_str());
                    if (ImGui::Button((std::string(ui_icons::Keyboard) + " " + ui.Text(UiText::Change) + "##confirm").c_str(), ScaleUi(168.0f, 0.0f))) {
                        BeginCapture(CaptureTarget::ConfirmKey);
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled(
                        "%s",
                        ui.Format(UiText::CancelKeyFormat, ::hotkeys::KeyName(editor.draft.textConfirmation.cancelKey).c_str()).c_str());
                    if (ImGui::Button((std::string(ui_icons::Keyboard) + " " + ui.Text(UiText::Change) + "##cancel").c_str(), ScaleUi(168.0f, 0.0f))) {
                        BeginCapture(CaptureTarget::CancelKey);
                    }

                    ImGui::EndTable();
                }
                ImGui::TextDisabled("%s", ui.Text(UiText::EditorConfirmationHint));
            }
        }
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
                    editor.tabSelectionPending && editor.activeTab == EditorState::Tab::Scenario ? ImGuiTabItemFlags_SetSelected : 0)) {
                SetEditorTab(EditorState::Tab::Scenario);
                DrawEditorScenarioTab();
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

            ImGui::TableSetColumnIndex(1);
            const float footerActionWidth = ScaleUi(190.0f) + ScaleUi(130.0f) + itemSpacingX;
            alignRight(footerActionWidth);
            pushPrimaryButtonStyle();
            if (ImGui::Button(saveLabel.c_str(), ScaleUi(190.0f, 0.0f))) {
                std::vector<std::string> errors;
                if (ValidateEditor(errors)) {
                    SaveEditor();
                    Notify(NotificationGroup::Success, NotificationSeverity::Success, ui.Text(UiText::ToastBindSaved), 1800.0);
                } else {
                    for (const std::string& error : errors) {
                        Notify(NotificationGroup::Validation, NotificationSeverity::Error, error, 2800.0);
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
    DrawEditorVariablesPopup();
    DrawEditorMultiInputPopup();
    DrawEditorDiscardPopup();
}

void BinderModule::Impl::DrawEditor() {
    if (editor.active) {
        DrawEditorInline();
    }
}

void BinderModule::Impl::DrawExplorerBreadcrumb() {
    UiSettings& ui = UiSettings::Instance();
    std::vector<FolderNode*> chain;
    for (FolderNode* folder = currentFolder; folder != nullptr; folder = folder->parent) {
        chain.push_back(folder);
    }
    std::reverse(chain.begin(), chain.end());

    std::string fullPath = ui.Text(UiText::BinderRootName);
    for (const FolderNode* folder : chain) {
        if (folder) {
            fullPath += " / " + folder->name;
        }
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const auto separatorWidth = [&]() {
        return ImGui::CalcTextSize("/").x + ScaleUi(8.0f);
    };
    const auto crumbWidth = [&](std::string_view label) {
        return ImGui::CalcTextSize(std::string(label).c_str()).x + style.FramePadding.x * 2.0f + style.ItemSpacing.x;
    };

    float requiredWidth = crumbWidth(ui.Text(UiText::BinderRootName));
    for (const FolderNode* folder : chain) {
        if (folder) {
            requiredWidth += separatorWidth() + crumbWidth(folder->name);
        }
    }

    const bool collapse = chain.size() > 2
        && ImGui::GetContentRegionAvail().x > ScaleUi(80.0f)
        && requiredWidth > ImGui::GetContentRegionAvail().x;

    const auto drawSeparator = [&]() {
        ImGui::SameLine(0.0f, ScaleUi(4.0f));
        ImGui::TextDisabled("/");
        ImGui::SameLine(0.0f, ScaleUi(4.0f));
    };

    const auto drawTooltip = [&]() {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s", fullPath.c_str());
        }
    };

    if (ImGui::SmallButton((std::string(ui.Text(UiText::BinderRootName)) + "##binder_breadcrumb_root").c_str())) {
        OpenFolder(nullptr, true);
    }
    drawTooltip();

    if (collapse) {
        drawSeparator();
        ImGui::BeginDisabled();
        ImGui::SmallButton("...##binder_breadcrumb_ellipsis");
        ImGui::EndDisabled();
        drawTooltip();

        const std::size_t start = chain.size() > 2 ? chain.size() - 2 : 0;
        for (std::size_t i = start; i < chain.size(); ++i) {
            FolderNode* folder = chain[i];
            if (!folder) {
                continue;
            }
            drawSeparator();
            if (ImGui::SmallButton((folder->name + "##bc_" + std::to_string(folder->id)).c_str())) {
                OpenFolder(folder, true);
            }
            drawTooltip();
        }
        return;
    }

    for (FolderNode* folder : chain) {
        if (!folder) {
            continue;
        }
        drawSeparator();
        if (ImGui::SmallButton((folder->name + "##bc_" + std::to_string(folder->id)).c_str())) {
            OpenFolder(folder, true);
        }
        drawTooltip();
    }
}

void BinderModule::Impl::DrawExplorerToolbar() {
    UiSettings& ui = UiSettings::Instance();
    const ImVec2 navButtonSize = ScaleUi(30.0f, 0.0f);

    const bool backDisabled = ActiveNavigationBackStack().empty();
    if (backDisabled) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button((std::string(ui_icons::ChevronLeft) + "##binder_back").c_str(), navButtonSize)) {
        NavigateBack();
    }
    if (backDisabled) {
        ImGui::EndDisabled();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", ui.Text(UiText::EditorBack));
    }

    ImGui::SameLine();
    const bool upDisabled = currentFolder == nullptr;
    if (upDisabled) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button((std::string(ui_icons::AngleUp) + "##binder_up").c_str(), navButtonSize)) {
        NavigateUp();
    }
    if (upDisabled) {
        ImGui::EndDisabled();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", ui.Text(UiText::BinderGoUp));
    }

    ImGui::SameLine();
    if (ImGui::Button((std::string(ui.Text(UiText::AddBind)) + "##explorer_add_bind").c_str())) {
        StartEditing(-1, true);
    }

    ImGui::SameLine();
    if (ImGui::Button((std::string(ui.Text(UiText::FolderAdd)) + "##explorer_add_folder").c_str())) {
        BeginInlineCreateFolder(currentFolder);
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(ScaleUi(180.0f), ImGui::GetContentRegionAvail().x * 0.35f));
    InputTextString(ui.Text(UiText::BinderSearchGlobal), bindSearch, ImGuiInputTextFlags_AutoSelectAll, 128);

    if (folderMoveUndo_) {
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string(ui.Text(UiText::UndoFolderMove)) + "##explorer_folder_undo").c_str())) {
            ApplyFolderMoveUndo();
        }
    }

    DrawExplorerBreadcrumb();
}

void BinderModule::Impl::DrawExplorerInlineFolderEditContent(
    const ExplorerListLayout& layout,
    const ImRect& rowRect,
    const bool selected) {
    UiSettings& ui = UiSettings::Instance();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float textY = rowRect.Min.y + std::floor((layout.rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
    const ImVec2 folderIconSize = ImGui::CalcTextSize(ui_icons::Folder);
    ImVec4 iconColor = style.Colors[ImGuiCol_Text];
    iconColor.w = selected ? 1.0f : 0.92f;
    drawList->AddText(
        ImVec2(layout.iconX + std::floor(std::max(0.0f, (layout.iconW - folderIconSize.x) * 0.5f)), textY),
        ImGui::GetColorU32(iconColor),
        ui_icons::Folder);

    const float buttonSide = std::ceil(ImGui::GetFrameHeight() - ScaleUi(1.0f));
    const ImVec2 buttonSize(buttonSide, buttonSide);
    const float buttonGap = ScaleUi(4.0f);
    const float actionGroupWidth = buttonSide * 2.0f + buttonGap;
    const float inputX = layout.nameX + ScaleUi(6.0f);
    const float inputY = rowRect.Min.y + std::floor(std::max(0.0f, (layout.rowHeight - ImGui::GetFrameHeight()) * 0.5f));
    const float inputMaxX = std::max(inputX + ScaleUi(48.0f), layout.actionsX - ScaleUi(8.0f));
    const float inputWidth = std::max(ScaleUi(48.0f), inputMaxX - inputX);

    if (folderInlineEdit.focusPending) {
        ImGui::SetKeyboardFocusHere();
        folderInlineEdit.focusPending = false;
    }
    ImGui::SetCursorScreenPos(ImVec2(inputX, inputY));
    ImGui::SetNextItemWidth(inputWidth);
    const bool submitted = InputTextString(
        "##folder_inline_name",
        folderInlineEdit.name,
        ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue,
        128);
    const bool escapePressed = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    const bool deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SetCursorScreenPos(ImVec2(
        std::floor(layout.actionsX + layout.actionsW - actionGroupWidth),
        std::floor(rowRect.Min.y + std::max(0.0f, (layout.rowHeight - buttonSide) * 0.5f))));
    const bool saveClicked = SmallIconActionButton(ui_icons::Check, "##folder_inline_save", ui.Text(UiText::Save), buttonSize);
    ImGui::SameLine(0.0f, buttonGap);
    const bool cancelClicked = SmallIconActionButton(ui_icons::Delete, "##folder_inline_cancel", ui.Text(UiText::Cancel), buttonSize);

    if (cancelClicked || escapePressed) {
        CancelInlineFolderEdit();
        return;
    }
    if (saveClicked || submitted || deactivatedAfterEdit) {
        (void)CommitInlineFolderEdit();
    }
}

void BinderModule::Impl::DrawExplorerInlineFolderEditRow(
    const int rowIndex,
    const ExplorerListLayout& layout,
    const ImRect& rowRect) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec4 rowBg = style.Colors[ImGuiCol_HeaderActive];
    rowBg.w = 0.22f;
    if (rowIndex % 2 != 0) {
        rowBg.w = 0.26f;
    }
    drawList->AddRectFilled(rowRect.Min, rowRect.Max, ImGui::GetColorU32(rowBg), ScaleUi(2.0f));
    drawList->AddLine(
        ImVec2(rowRect.Min.x, rowRect.Max.y),
        rowRect.Max,
        ImGui::GetColorU32(ImGuiCol_Border, 0.24f));

    ImGui::PushID("folder_inline_create");
    DrawExplorerInlineFolderEditContent(layout, rowRect, true);
    ImGui::SetCursorScreenPos(ImVec2(rowRect.Min.x, rowRect.Max.y));
    ImGui::PopID();
}

void BinderModule::Impl::DrawExplorerFolderRow(
    FolderNode& folder,
    const int rowIndex,
    const ExplorerListLayout& layout,
    const ImRect& rowRect) {
    UiSettings& ui = UiSettings::Instance();
    const bool selected = IsExplorerFolderSelected(&folder);
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const bool hovered = ImGui::IsMouseHoveringRect(rowRect.Min, rowRect.Max, true);

    ImVec4 rowBg = style.Colors[ImGuiCol_HeaderHovered];
    rowBg.w = hovered ? 0.14f : 0.0f;
    if (rowIndex % 2 != 0 && !hovered && !selected) {
        rowBg = style.Colors[ImGuiCol_FrameBg];
        rowBg.w = 0.10f;
    }
    if (selected) {
        rowBg = style.Colors[ImGuiCol_HeaderActive];
        rowBg.w = 0.26f;
    }
    if (rowBg.w > 0.0f) {
        drawList->AddRectFilled(rowRect.Min, rowRect.Max, ImGui::GetColorU32(rowBg), ScaleUi(2.0f));
    }
    drawList->AddLine(
        ImVec2(rowRect.Min.x, rowRect.Max.y),
        rowRect.Max,
        ImGui::GetColorU32(ImGuiCol_Border, 0.24f));

    ImGui::PushID(folder.id);
    if (IsInlineRenamingFolder(&folder)) {
        DrawExplorerInlineFolderEditContent(layout, rowRect, true);
        ImGui::SetCursorScreenPos(ImVec2(rowRect.Min.x, rowRect.Max.y));
        ImGui::PopID();
        return;
    }

    ImGui::SetCursorScreenPos(rowRect.Min);
    const float rowHitWidth = std::max(1.0f, layout.actionsX - rowRect.Min.x - ScaleUi(4.0f));
    const bool clicked = ImGui::InvisibleButton(
        "##folder_row",
        ImVec2(rowHitWidth, layout.rowHeight),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool rowItemHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool doubleClicked = rowItemHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    std::optional<FolderDropZone> dropPreviewZone{};
    const auto drawRowDropPreview = [&](FolderDropZone zone) {
        ImGui::PushClipRect(rowRect.Min, rowRect.Max, true);
        DrawFolderDropPreview(rowRect, zone);
        ImGui::PopClipRect();
    };
    if (clicked || doubleClicked) {
        SelectExplorerFolder(&folder);
    }
    if (doubleClicked) {
        OpenFolder(&folder, true);
    }
    if (selected && explorerSelectionScrollPending) {
        ImGui::SetScrollHereY(0.50f);
        explorerSelectionScrollPending = false;
    }

    if (hovered) {
        if (const ImGuiPayload* payload = ActiveExplorerDragPayload()) {
            const FolderDropZone zone = ResolveFolderDropZone(rowRect);
            if (IsExplorerBindDragPayload(payload)) {
                dropPreviewZone = zone;
            } else {
                int folderId = 0;
                if (TryGetExplorerFolderDragId(payload, folderId)) {
                    const bool into = zone == FolderDropZone::Into;
                    FolderNode* targetFolder = into ? &folder : currentFolder;
                    const int insertIndex = into
                        ? static_cast<int>(folder.items.size())
                        : rowIndex + (zone == FolderDropZone::After ? 1 : 0);
                    if (CanMoveFolderToExplorerDirectory(folderId, targetFolder, insertIndex)) {
                        dropPreviewZone = zone;
                    }
                }
            }
        }
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
        const int folderId = folder.id;
        ImGui::SetDragDropPayload(kFolderDragPayload, &folderId, sizeof(folderId));
        ImGui::TextUnformatted(folder.name.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        const FolderDropZone zone = ResolveFolderDropZone(rowRect);
        const bool into = zone == FolderDropZone::Into;
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kBindDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                dropPreviewZone = zone;
                if (payload->IsDelivery()) {
                    const int hotkeyIndex = *static_cast<const int*>(payload->Data);
                    if (into) {
                        MoveBindToExplorerDirectory(hotkeyIndex, &folder, std::nullopt, "explorer_folder_drop");
                    } else {
                        MoveBindToExplorerDirectory(
                            hotkeyIndex,
                            currentFolder,
                            rowIndex + (zone == FolderDropZone::After ? 1 : 0),
                            "explorer_bind_reorder");
                    }
                }
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kFolderDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                const int folderId = *static_cast<const int*>(payload->Data);
                FolderNode* targetFolder = into ? &folder : currentFolder;
                const int insertIndex = into
                    ? static_cast<int>(folder.items.size())
                    : rowIndex + (zone == FolderDropZone::After ? 1 : 0);
                const bool validTarget = CanMoveFolderToExplorerDirectory(
                    folderId,
                    targetFolder,
                    insertIndex);
                if (validTarget) {
                    dropPreviewZone = zone;
                }
                if (payload->IsDelivery()) {
                    if (!validTarget) {
                        debuglog::WriteError(
                            "[binder] explorer folder drop rejected id=%d target=folder_row zone=%d",
                            folderId,
                            static_cast<int>(zone));
                    } else {
                        if (into) {
                            MoveFolderToExplorerDirectory(
                                folderId,
                                &folder,
                                static_cast<int>(folder.items.size()),
                                "explorer_folder_into",
                                true);
                        } else {
                            MoveFolderToExplorerDirectory(
                                folderId,
                                currentFolder,
                                rowIndex + (zone == FolderDropZone::After ? 1 : 0),
                                "explorer_folder_reorder",
                                true);
                        }
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    const float textY = rowRect.Min.y + std::floor((layout.rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
    const ImVec2 iconSize = ImGui::CalcTextSize(ui_icons::Folder);
    ImVec4 iconColor = style.Colors[ImGuiCol_Text];
    iconColor.w = selected || hovered ? 1.0f : 0.92f;
    drawList->AddText(
        ImVec2(layout.iconX + std::floor(std::max(0.0f, (layout.iconW - iconSize.x) * 0.5f)), textY),
        ImGui::GetColorU32(iconColor),
        ui_icons::Folder);

    const bool hasConditions = HasSelectedCondition(folder.conditions);
    const std::string marker = std::string(ui_icons::Sliders);
    const ImVec2 markerSize = hasConditions ? ImGui::CalcTextSize(marker.c_str()) : ImVec2(0.0f, 0.0f);
    const float markerReserve = hasConditions ? markerSize.x + ScaleUi(18.0f) : 0.0f;
    const ImRect nameRect(
        ImVec2(layout.nameX, rowRect.Min.y),
        ImVec2(layout.actionsX - ScaleUi(8.0f), rowRect.Max.y));
    const std::string folderLabel = EllipsizeText(
        folder.name,
        std::max(0.0f, nameRect.GetWidth() - markerReserve));
    ImVec4 textColor = style.Colors[ImGuiCol_Text];
    textColor.w = selected || hovered ? 1.0f : 0.92f;
    ImGui::PushClipRect(nameRect.Min, nameRect.Max, true);
    drawList->AddText(
        ImVec2(nameRect.Min.x, textY),
        ImGui::GetColorU32(textColor),
        folderLabel.c_str());
    if (hasConditions) {
        drawList->AddText(
            ImVec2(nameRect.Max.x - markerSize.x - ScaleUi(4.0f), textY),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            marker.c_str());
    }
    ImGui::PopClipRect();

    if (ImGui::BeginPopupContextItem("##folder_context")) {
        if (ImGui::MenuItem(ui.Text(UiText::BinderOpenFolder))) {
            OpenFolder(&folder, true);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem(ui.Text(UiText::AddBind))) {
            OpenFolder(&folder, true);
            StartEditing(-1, true);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem(ui.Text(UiText::FolderAdd))) {
            OpenFolder(&folder, true);
            BeginInlineCreateFolder(&folder);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem(ui.Text(UiText::FolderRename))) {
            SelectExplorerFolder(&folder);
            BeginInlineRenameFolder(&folder);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem(ui.Text(UiText::EditorOpenConditions))) {
            folderConditionsTarget = &folder;
            folderConditionsPopupPending = true;
            ImGui::CloseCurrentPopup();
        }
        const bool canDelete = CanDeleteFolder(&folder);
        if (!canDelete) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem(ui.Text(UiText::Delete)) && canDelete) {
            SelectExplorerFolder(&folder);
            folderDeleteTarget = &folder;
            folderDeletePopupPending = true;
            ImGui::CloseCurrentPopup();
        }
        if (!canDelete) {
            ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }

    if (hovered || selected) {
        const float buttonSide = std::ceil(ImGui::GetFrameHeight() - ScaleUi(1.0f));
        const ImVec2 buttonSize(buttonSide, buttonSide);
        const float gap = ScaleUi(4.0f);
        const float groupWidth = buttonSide * 3.0f + gap * 2.0f;
        ImGui::SetCursorScreenPos(ImVec2(
            std::floor(layout.actionsX + layout.actionsW - groupWidth),
            std::floor(rowRect.Min.y + std::max(0.0f, (layout.rowHeight - buttonSide) * 0.5f))));
        if (SmallIconActionButton(ui_icons::Edit, "##folder_rename", ui.Text(UiText::FolderRename), buttonSize)) {
            SelectExplorerFolder(&folder);
            BeginInlineRenameFolder(&folder);
        }
        ImGui::SameLine(0.0f, gap);
        if (SmallIconActionButton(ui_icons::Sliders, "##folder_conditions", ui.Text(UiText::EditorOpenConditions), buttonSize)) {
            folderConditionsTarget = &folder;
            folderConditionsPopupPending = true;
        }
        ImGui::SameLine(0.0f, gap);
        const bool canDelete = CanDeleteFolder(&folder);
        if (!canDelete) {
            ImGui::BeginDisabled();
        }
        if (SmallIconActionButton(ui_icons::Delete, "##folder_delete", ui.Text(UiText::Delete), buttonSize) && canDelete) {
            SelectExplorerFolder(&folder);
            folderDeleteTarget = &folder;
            folderDeletePopupPending = true;
        }
        if (!canDelete) {
            ImGui::EndDisabled();
        }
    }
    if (dropPreviewZone.has_value()) {
        drawRowDropPreview(*dropPreviewZone);
    }
    ImGui::SetCursorScreenPos(ImVec2(rowRect.Min.x, rowRect.Max.y));
    ImGui::PopID();
}

void BinderModule::Impl::DrawExplorerBindRow(
    const int index,
    const int rowIndex,
    const ExplorerListLayout& layout,
    const ImRect& rowRect) {
    if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
        return;
    }

    HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
    UiSettings& ui = UiSettings::Instance();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float iconButtonSide = std::ceil(ImGui::GetFrameHeight() - ScaleUi(1.0f));
    const ImVec2 iconButtonSize(iconButtonSide, iconButtonSide);
    const float actionButtonGap = ScaleUi(4.0f);
    const bool rowHovered = ImGui::IsMouseHoveringRect(rowRect.Min, rowRect.Max, true);
    const bool selected = IsExplorerBindSelected(index);

    ImVec4 rowBg = style.Colors[ImGuiCol_HeaderHovered];
    rowBg.w = rowHovered ? 0.14f : 0.0f;
    if (rowIndex % 2 != 0 && !rowHovered && !selected) {
        rowBg = style.Colors[ImGuiCol_FrameBg];
        rowBg.w = 0.10f;
    }
    if (selected) {
        rowBg = style.Colors[ImGuiCol_HeaderActive];
        rowBg.w = 0.26f;
    }
    if (rowBg.w > 0.0f) {
        drawList->AddRectFilled(rowRect.Min, rowRect.Max, ImGui::GetColorU32(rowBg), ScaleUi(2.0f));
    }
    drawList->AddLine(
        ImVec2(rowRect.Min.x, rowRect.Max.y),
        rowRect.Max,
        ImGui::GetColorU32(ImGuiCol_Border, 0.24f));

    ImGui::PushID(index);

    const ImRect rowHitRect(
        rowRect.Min,
        ImVec2(std::max(rowRect.Min.x + ScaleUi(1.0f), layout.actionsX - ScaleUi(4.0f)), rowRect.Max.y));
    ImGui::SetCursorScreenPos(rowHitRect.Min);
    const bool bindClicked = ImGui::InvisibleButton(
        "##bind_row",
        rowHitRect.GetSize(),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool bindHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool showBindTooltip = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
    std::optional<FolderDropZone> dropPreviewZone{};
    const auto drawRowDropPreview = [&](FolderDropZone zone) {
        ImGui::PushClipRect(rowRect.Min, rowRect.Max, true);
        DrawFolderDropPreview(rowRect, zone);
        ImGui::PopClipRect();
    };
    if (bindClicked) {
        SelectExplorerBind(index);
    }

    const ImVec2 bindIconSize = ImGui::CalcTextSize(ui_icons::Keyboard);
    ImVec4 iconColor = style.Colors[hotkey.enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled];
    iconColor.w = hotkey.enabled ? 0.92f : 0.62f;
    if (selected || bindHovered) {
        iconColor.w = hotkey.enabled ? 1.0f : 0.78f;
    }
    const float bindTextY = rowRect.Min.y + (rowRect.GetHeight() - ImGui::GetTextLineHeight()) * 0.5f;
    drawList->AddText(
        ImVec2(layout.iconX + std::floor(std::max(0.0f, (layout.iconW - bindIconSize.x) * 0.5f)), bindTextY),
        ImGui::GetColorU32(iconColor),
        ui_icons::Keyboard);

    const ImRect bindRect(
        ImVec2(layout.nameX, rowRect.Min.y),
        ImVec2(layout.nameX + layout.nameW, rowRect.Max.y));
    const float bindPadX = 0.0f;
    const float textAvailableWidth = std::max(0.0f, bindRect.GetWidth() - bindPadX * 2.0f);
    const std::string displayLabel = BuildBindDisplayLabel(hotkey);
    const std::string bindTitle = "\xE2\x84\x96" + std::to_string(hotkey.number) + " " + displayLabel;
    const LaunchCellContent launchContent = BuildLaunchCellContent(hotkey);
    const std::string launchRaw = launchContent.primary.empty() ? std::string{} : " (" + launchContent.primary + ")";
    const float launchGap = ScaleUi(6.0f);
    const float titleWidth = ImGui::CalcTextSize(bindTitle.c_str()).x;
    const float minLaunchWidth = ScaleUi(42.0f);
    const float remainingForLaunch = textAvailableWidth - titleWidth - launchGap;
    const bool drawLaunch = !launchRaw.empty() && remainingForLaunch >= minLaunchWidth;
    const std::string launchLabel = drawLaunch
        ? EllipsizeText(launchRaw, std::max(0.0f, remainingForLaunch))
        : std::string{};
    const float launchWidth = drawLaunch ? ImGui::CalcTextSize(launchLabel.c_str()).x : 0.0f;
    const float titleMaxWidth = std::max(0.0f, textAvailableWidth - (drawLaunch ? launchWidth + launchGap : 0.0f));
    const std::string bindName = EllipsizeText(bindTitle, titleMaxWidth);
    ImVec4 bindNameColor = style.Colors[hotkey.enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled];
    bindNameColor.w = hotkey.enabled ? 0.96f : 0.82f;
    ImVec4 launchTextColor = style.Colors[ImGuiCol_TextDisabled];
    launchTextColor.w = hotkey.enabled ? 0.78f : 0.54f;
    if (selected) {
        bindNameColor = style.Colors[ImGuiCol_Text];
        launchTextColor.w = 0.86f;
    } else if (bindHovered && hotkey.enabled) {
        bindNameColor.w = 1.00f;
        launchTextColor.w = 0.88f;
    }

    ImGui::PushClipRect(bindRect.Min, bindRect.Max, true);
    const float textStartX = bindRect.Min.x + bindPadX;
    drawList->AddText(
        ImVec2(textStartX, bindTextY),
        ImGui::GetColorU32(bindNameColor),
        bindName.c_str());
    if (drawLaunch) {
        drawList->AddText(
            ImVec2(textStartX + ImGui::CalcTextSize(bindName.c_str()).x + launchGap, bindTextY),
            ImGui::GetColorU32(launchTextColor),
            launchLabel.c_str());
    }
    ImGui::PopClipRect();
    if (bindHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        SelectExplorerBind(index);
        StartEditing(index, false);
    }
    if (selected && explorerSelectionScrollPending) {
        ImGui::SetScrollHereY(0.50f);
        explorerSelectionScrollPending = false;
    }
    if (rowHovered) {
        if (const ImGuiPayload* payload = ActiveExplorerDragPayload()) {
            FolderDropZone zone = ResolveFolderDropZone(rowRect);
            if (zone == FolderDropZone::Into) {
                zone = FolderDropZone::After;
            }
            if (IsExplorerBindDragPayload(payload)) {
                dropPreviewZone = zone;
            } else {
                int folderId = 0;
                if (TryGetExplorerFolderDragId(payload, folderId)) {
                    const int insertIndex = rowIndex + (zone == FolderDropZone::After ? 1 : 0);
                    if (CanMoveFolderToExplorerDirectory(folderId, currentFolder, insertIndex)) {
                        dropPreviewZone = zone;
                    }
                }
            }
        }
    }
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
        const int hotkeyIndex = index;
        ImGui::SetDragDropPayload(kBindDragPayload, &hotkeyIndex, sizeof(hotkeyIndex));
        ImGui::TextUnformatted(bindName.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        FolderDropZone zone = ResolveFolderDropZone(rowRect);
        if (zone == FolderDropZone::Into) {
            zone = FolderDropZone::After;
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kBindDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                dropPreviewZone = zone;
                if (payload->IsDelivery()) {
                    const int hotkeyIndex = *static_cast<const int*>(payload->Data);
                    MoveBindToExplorerDirectory(
                        hotkeyIndex,
                        currentFolder,
                        rowIndex + (zone == FolderDropZone::After ? 1 : 0),
                        "explorer_bind_reorder");
                }
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kFolderDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                const int folderId = *static_cast<const int*>(payload->Data);
                const int insertIndex = rowIndex + (zone == FolderDropZone::After ? 1 : 0);
                const bool validTarget = CanMoveFolderToExplorerDirectory(folderId, currentFolder, insertIndex);
                if (validTarget) {
                    dropPreviewZone = zone;
                }
                if (payload->IsDelivery()) {
                    if (!validTarget) {
                        debuglog::WriteError(
                            "[binder] explorer folder drop rejected id=%d target=bind_row zone=%d",
                            folderId,
                            static_cast<int>(zone));
                    } else {
                        MoveFolderToExplorerDirectory(
                            folderId,
                            currentFolder,
                            rowIndex + (zone == FolderDropZone::After ? 1 : 0),
                            "explorer_folder_reorder",
                            true);
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (showBindTooltip) {
        std::vector<std::string> tooltipLines{
            ui.Format(UiText::BindListEntryFormat, hotkey.number, displayLabel.c_str()),
        };
        const std::vector<std::string> launchLabels = BuildLaunchLabels(hotkey);
        if (!launchLabels.empty()) {
            tooltipLines.push_back(JoinLaunchLabels(launchLabels, "\n"));
        }
        ImGui::SetTooltip("%s", JoinLaunchLabels(tooltipLines, "\n").c_str());
    }

    const bool isRunning = IsHotkeyRunning(index);
    const bool isPaused = IsHotkeyPaused(index);
    const int actionButtonCount = isRunning ? 7 : 6;
    const float actionGroupWidth =
        iconButtonSize.x * static_cast<float>(actionButtonCount)
        + actionButtonGap * static_cast<float>(std::max(actionButtonCount - 1, 0));
    ImGui::SetCursorScreenPos(ImVec2(
        std::floor(layout.actionsX + layout.actionsW - actionGroupWidth),
        std::floor(rowRect.Min.y + std::max(0.0f, (layout.rowHeight - iconButtonSize.y) * 0.5f))));
    if (SmallIconActionButton(
            hotkey.enabled ? ui_icons::ToggleOn : ui_icons::ToggleOff, "##enabled", ui.Text(UiText::Enabled), iconButtonSize)) {
        hotkey.enabled = !hotkey.enabled;
        SaveConfig();
    }
    ImGui::SameLine(0.0f, actionButtonGap);
    const bool dimQuickButton = !hotkey.enabled || !hotkey.quickMenu;
    if (dimQuickButton) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }
    if (!hotkey.enabled) {
        ImGui::BeginDisabled();
    }
    if (SmallIconActionButton(ui_icons::Bolt, "##quick", ui.Text(UiText::ShowInQuickMenu), iconButtonSize)) {
        hotkey.quickMenu = !hotkey.quickMenu;
        SaveConfig();
    }
    if (!hotkey.enabled) {
        ImGui::EndDisabled();
    }
    if (dimQuickButton) {
        ImGui::PopStyleColor();
    }
    ImGui::SameLine(0.0f, actionButtonGap);
    if (!isRunning) {
        ImGui::BeginDisabled(!hotkey.enabled);
        if (SmallIconActionButton(ui_icons::Play, "##run", ui.Text(UiText::Run), iconButtonSize)) {
            TryEnqueueHotkey(index, 0, "manual", "");
        }
        ImGui::EndDisabled();
    } else if (isPaused) {
        if (SmallIconActionButton(ui_icons::Play, "##resume", ui.Text(UiText::Resume), iconButtonSize)) {
            ResumeHotkey(index);
        }
        ImGui::SameLine(0.0f, actionButtonGap);
        if (SmallIconActionButton(ui_icons::Stop, "##stop", ui.Text(UiText::Stop), iconButtonSize)) {
            StopHotkey(index);
        }
    } else {
        if (SmallIconActionButton(ui_icons::Pause, "##pause", ui.Text(UiText::Pause), iconButtonSize)) {
            PauseHotkey(index);
        }
        ImGui::SameLine(0.0f, actionButtonGap);
        if (SmallIconActionButton(ui_icons::Stop, "##stop", ui.Text(UiText::Stop), iconButtonSize)) {
            StopHotkey(index);
        }
    }
    ImGui::SameLine(0.0f, actionButtonGap);
    if (SmallIconActionButton(ui_icons::Edit, "##edit", ui.Text(UiText::Edit), iconButtonSize)) {
        SelectExplorerBind(index);
        StartEditing(index, false);
    }
    ImGui::SameLine(0.0f, actionButtonGap);
    if (SmallIconActionButton(ui_icons::Delete, "##delete", ui.Text(UiText::Delete), iconButtonSize)) {
        SelectExplorerBind(index);
        bindDeleteTarget = index;
        bindDeletePopupPending = true;
    }
    ImGui::SameLine(0.0f, actionButtonGap);
    if (SmallIconActionButton(ui_icons::Bars, "##more", ui.Text(UiText::ColumnActions), iconButtonSize)) {
        ImGui::OpenPopup("##binder_bind_actions");
    }
    if (ImGui::BeginPopup("##binder_bind_actions")) {
        if (ImGui::MenuItem(ui.Text(UiText::ActionMoveTo))) {
            SelectExplorerBind(index);
            moveBindTarget = index;
            moveBindPopupPending = true;
        }
        if (ImGui::MenuItem(ui.Text(UiText::ActionDuplicate))) {
            SelectExplorerBind(index);
            DuplicateHotkeyAt(index);
        }
        if (ImGui::MenuItem(ui.Text(UiText::ActionBindLines))) {
            SelectExplorerBind(index);
            bindLinesTarget = index;
            bindLinesPopupPending = true;
        }
        ImGui::EndPopup();
    }

    if (dropPreviewZone.has_value()) {
        drawRowDropPreview(*dropPreviewZone);
    }
    ImGui::SetCursorScreenPos(ImVec2(rowRect.Min.x, rowRect.Max.y));
    ImGui::PopID();
}

void BinderModule::Impl::DrawExplorerKeyboardShortcuts(const bool focused) {
    if (!focused || ImGui::GetActiveID() != 0) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !bindSearch.empty()) {
        bindSearch.clear();
        return;
    }

    if (!Trim(bindSearch).empty()) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
        MoveExplorerSelection(-1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
        MoveExplorerSelection(1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        const int bindIndex = explorerSelection.kind == ExplorerSelectionKind::Bind
            ? FindHotkeyIndexByOrderId(explorerSelection.bindOrderId)
            : selectedBindIndex;
        if (bindIndex >= 0 && bindIndex < static_cast<int>(hotkeys.size())) {
            StartEditing(bindIndex, false);
        } else if (selectedFolder && selectedFolder != currentFolder) {
            OpenFolder(selectedFolder, true);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
        NavigateUp();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        const int bindIndex = explorerSelection.kind == ExplorerSelectionKind::Bind
            ? FindHotkeyIndexByOrderId(explorerSelection.bindOrderId)
            : selectedBindIndex;
        if (bindIndex >= 0 && bindIndex < static_cast<int>(hotkeys.size())) {
            bindDeleteTarget = bindIndex;
            bindDeletePopupPending = true;
        } else if (CanDeleteFolder(selectedFolder)) {
            folderDeleteTarget = selectedFolder;
            folderDeletePopupPending = true;
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        if (selectedFolder && selectedFolder != currentFolder) {
            BeginInlineRenameFolder(selectedFolder);
        }
    }
}

void BinderModule::Impl::DrawExplorerSearchResults() {
    const std::string query = ToLower(Trim(bindSearch));
    if (query.empty()) {
        return;
    }

    struct SearchResult {
        std::string categoryId;
        ExplorerItem item;
        std::vector<std::string> folderPath;
        bool categoryOnly = false;
    };

    std::vector<SearchResult> results;
    for (const BinderCategory& category : categories) {
        const std::string hay = ToLower(category.name);
        if (hay.find(query) != std::string::npos) {
            results.push_back(SearchResult{
                category.id,
                ExplorerItem{ ExplorerItemKind::Folder, category.name },
                {},
                true,
            });
        }
    }

    const auto collect = [&](auto&& self, const BinderCategory& category, FolderNode* folder) -> void {
        const std::vector<std::string> path = FolderPathForDirectory(folder);
        const std::vector<ExplorerItem>& items = folder ? folder->items : category.rootItems;
        for (const ExplorerItem& item : items) {
            if (item.kind == ExplorerItemKind::Folder) {
                FolderNode* child = FindFolderByNameInDirectory(category, folder, item.key);
                if (!child) {
                    continue;
                }
                const std::string hay = ToLower(category.name + " " + child->name + " " + JoinPath(BuildFolderPath(child)));
                if (hay.find(query) != std::string::npos) {
                    results.push_back(SearchResult{ category.id, item, path, false });
                }
                self(self, category, child);
            } else {
                const int index = FindHotkeyIndexByOrderId(item.key);
                if (index < 0) {
                    continue;
                }
                const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
                if (hotkey.categoryId != category.id) {
                    continue;
                }
                const std::string hay = ToLower(category.name + " " + hotkey.label + " " + hotkey.command + " " + JoinPath(hotkey.folderPath));
                if (hay.find(query) != std::string::npos) {
                    results.push_back(SearchResult{ category.id, item, path, false });
                }
            }
        }
    };
    for (const BinderCategory& category : categories) {
        collect(collect, category, nullptr);
    }

    if (results.empty()) {
        ImGui::TextDisabled("%s", UiSettings::Instance().Text(UiText::MiscVariablesCatalogEmpty));
        return;
    }

    if (ImGui::BeginTable(
            "##binder_explorer_search",
            2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
            ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn(UiSettings::Instance().Text(UiText::ColumnName), ImGuiTableColumnFlags_WidthStretch, 0.70f);
        ImGui::TableSetupColumn(UiSettings::Instance().Text(UiText::Folder), ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableHeadersRow();
        int row = 0;
        for (const SearchResult& result : results) {
            ImGui::PushID(row++);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (result.categoryOnly) {
                const std::string label = std::string(ui_icons::Folder) + " " + result.item.key;
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)
                    && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    SelectCategory(result.categoryId);
                    OpenFolder(nullptr, true);
                    bindSearch.clear();
                }
            } else if (result.item.kind == ExplorerItemKind::Folder) {
                const std::string label = std::string(ui_icons::Folder) + " " + result.item.key;
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)
                    && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    SelectCategory(result.categoryId);
                    const BinderCategory& category = ActiveCategory();
                    FolderNode* folder = FindFolderByNameInDirectory(
                        category,
                        result.folderPath.empty() ? nullptr : FindFolderByPath(category.folders, result.folderPath),
                        result.item.key);
                    if (folder) {
                        OpenFolder(folder, true);
                        bindSearch.clear();
                    }
                }
            } else {
                const int index = FindHotkeyIndexByOrderId(result.item.key);
                const std::string label = index >= 0
                    ? std::string(ui_icons::Keyboard) + " " + hotkeys[static_cast<std::size_t>(index)].label
                    : std::string(ui_icons::Keyboard);
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)
                    && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && index >= 0) {
                    SelectCategory(result.categoryId);
                    SelectExplorerBind(index);
                    bindSearch.clear();
                    StartEditing(index, false);
                }
            }
            ImGui::TableSetColumnIndex(1);
            const BinderCategory* category = FindCategoryById(result.categoryId);
            std::string path = category ? category->name : std::string{};
            if (!result.folderPath.empty()) {
                path += " / " + JoinPath(result.folderPath);
            }
            ImGui::TextDisabled("%s", path.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void BinderModule::Impl::DrawExplorerEmptyAreaContextMenu(const char* popupId) {
    if (ImGui::BeginPopupContextItem(popupId)) {
        UiSettings& ui = UiSettings::Instance();
        if (ImGui::MenuItem(ui.Text(UiText::AddBind))) {
            StartEditing(-1, true);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem(ui.Text(UiText::FolderAdd))) {
            BeginInlineCreateFolder(currentFolder);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void BinderModule::Impl::DrawExplorerDirectory() {
    NormalizeExplorerOrders();
    const std::vector<ExplorerItem> items = ItemsForFolder(currentFolder);

    UiSettings& ui = UiSettings::Instance();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float iconButtonSide = std::ceil(ImGui::GetFrameHeight() - ScaleUi(1.0f));
    const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float columnGap = ScaleUi(8.0f);
    const float minNameWidth = ScaleUi(90.0f);

    ExplorerListLayout layout;
    layout.width = availableWidth;
    layout.rowHeight = std::max(ImGui::GetFrameHeight() + ScaleUi(2.0f), ScaleUi(28.0f));
    layout.headerHeight = std::max(ImGui::GetTextLineHeight() + ScaleUi(10.0f), ScaleUi(26.0f));
    layout.iconW = std::ceil(iconButtonSide + ScaleUi(18.0f));
    layout.actionsW = std::ceil(iconButtonSide * 7.0f + ScaleUi(4.0f) * 6.0f + ScaleUi(8.0f));
    layout.actionsW = std::max(1.0f, layout.actionsW);

    layout.iconX = start.x;
    layout.actionsX = std::max(start.x + layout.iconW + minNameWidth + columnGap, start.x + layout.width - layout.actionsW);
    layout.nameX = layout.iconX + layout.iconW;
    layout.nameW = std::max(1.0f, layout.actionsX - layout.nameX - columnGap);

    const ImRect headerRect(start, ImVec2(start.x + layout.width, start.y + layout.headerHeight));
    ImVec4 headerBg = style.Colors[ImGuiCol_FrameBg];
    headerBg.w = 0.72f;
    drawList->AddRectFilled(headerRect.Min, headerRect.Max, ImGui::GetColorU32(headerBg), ScaleUi(2.0f));
    drawList->AddLine(
        ImVec2(headerRect.Min.x, headerRect.Max.y),
        headerRect.Max,
        ImGui::GetColorU32(ImGuiCol_Border, 0.55f));

    const auto drawHeaderLabel = [&](const char* label, float x, float width, const char* tooltip, bool centered) {
        if (label == nullptr) {
            label = "";
        }
        const ImRect cell(ImVec2(x, headerRect.Min.y), ImVec2(x + width, headerRect.Max.y));
        const float padX = ScaleUi(6.0f);
        const std::string clipped = EllipsizeText(label, std::max(0.0f, cell.GetWidth() - padX * 2.0f));
        const ImVec2 labelSize = ImGui::CalcTextSize(clipped.c_str());
        const float textX = centered
            ? std::floor(cell.Min.x + std::max(0.0f, (cell.GetWidth() - labelSize.x) * 0.5f))
            : std::floor(cell.Min.x + padX);
        const float textY = std::floor(cell.Min.y + std::max(0.0f, (cell.GetHeight() - labelSize.y) * 0.5f));
        ImGui::PushClipRect(cell.Min, cell.Max, true);
        drawList->AddText(ImVec2(textX, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), clipped.c_str());
        ImGui::PopClipRect();
        if (tooltip != nullptr && tooltip[0] != '\0' && ImGui::IsMouseHoveringRect(cell.Min, cell.Max, true)) {
            ImGui::SetTooltip("%s", tooltip);
        }
    };

    drawHeaderLabel("", layout.iconX, layout.iconW, nullptr, true);
    drawHeaderLabel(ui.Text(UiText::ColumnName), layout.nameX, layout.nameW, nullptr, false);
    drawHeaderLabel(ui.Text(UiText::ColumnActions), layout.actionsX, layout.actionsW, nullptr, true);

    ImGui::Dummy(ImVec2(layout.width, layout.headerHeight));

    int rowIndex = 0;
    for (const ExplorerItem& item : items) {
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        const ImRect rowRect(rowPos, ImVec2(rowPos.x + layout.width, rowPos.y + layout.rowHeight));
        bool rowDrawn = false;
        if (item.kind == ExplorerItemKind::Folder) {
            if (FolderNode* folder = FindFolderByNameInDirectory(currentFolder, item.key)) {
                DrawExplorerFolderRow(*folder, rowIndex, layout, rowRect);
                rowDrawn = true;
            }
        } else {
            const int hotkeyIndex = FindHotkeyIndexByOrderId(item.key);
            if (hotkeyIndex >= 0) {
                DrawExplorerBindRow(hotkeyIndex, rowIndex, layout, rowRect);
                rowDrawn = true;
            }
        }
        if (rowDrawn) {
            ++rowIndex;
        }
    }

    const bool creatingInCurrentFolder =
        folderInlineEdit.mode == FolderInlineEditMode::Create && folderInlineEdit.parent == currentFolder;
    if (creatingInCurrentFolder) {
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        const ImRect rowRect(rowPos, ImVec2(rowPos.x + layout.width, rowPos.y + layout.rowHeight));
        DrawExplorerInlineFolderEditRow(rowIndex, layout, rowRect);
        ++rowIndex;
    }

    if (items.empty() && !creatingInCurrentFolder) {
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        const ImRect emptyRect(rowPos, ImVec2(rowPos.x + layout.width, rowPos.y + layout.rowHeight));
        const float textY = emptyRect.Min.y + std::floor((layout.rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
        drawList->AddText(
            ImVec2(emptyRect.Min.x + ScaleUi(12.0f), textY),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            ui.Text(UiText::BinderEmptyFolder));
        ImGui::SetCursorScreenPos(emptyRect.Min);
        ImGui::InvisibleButton("##explorer_empty_message_drop", emptyRect.GetSize());
        const bool emptyMessageHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            ClearExplorerSelection();
        }
        DrawExplorerEmptyAreaContextMenu("##explorer_empty_message_context");
        std::optional<FolderDropZone> emptyMessagePreview{};
        if (emptyMessageHovered) {
            if (const ImGuiPayload* payload = ActiveExplorerDragPayload()) {
                if (IsExplorerBindDragPayload(payload)) {
                    emptyMessagePreview = FolderDropZone::Into;
                } else {
                    int folderId = 0;
                    if (TryGetExplorerFolderDragId(payload, folderId)
                        && CanMoveFolderToExplorerDirectory(folderId, currentFolder, 0)) {
                        emptyMessagePreview = FolderDropZone::Into;
                    }
                }
            }
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kBindDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                    emptyMessagePreview = FolderDropZone::Into;
                    if (payload->IsDelivery()) {
                        MoveBindToExplorerDirectory(
                            *static_cast<const int*>(payload->Data),
                            currentFolder,
                            0,
                            "explorer_empty_drop");
                    }
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kFolderDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                    const int folderId = *static_cast<const int*>(payload->Data);
                    const bool validTarget = CanMoveFolderToExplorerDirectory(folderId, currentFolder, 0);
                    if (validTarget) {
                        emptyMessagePreview = FolderDropZone::Into;
                    }
                    if (payload->IsDelivery()) {
                        if (!validTarget) {
                            debuglog::WriteError(
                                "[binder] explorer folder drop rejected id=%d target=empty_message",
                                folderId);
                        } else {
                            MoveFolderToExplorerDirectory(
                                folderId,
                                currentFolder,
                                0,
                                "explorer_empty_drop",
                                true);
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (emptyMessagePreview.has_value()) {
            DrawFolderDropPreview(emptyRect, *emptyMessagePreview);
        }
        ImGui::SetCursorScreenPos(ImVec2(emptyRect.Min.x, emptyRect.Max.y));
    }

    const float remainingHeight = ImGui::GetContentRegionAvail().y;
    if (remainingHeight > ScaleUi(8.0f)) {
        const ImVec2 dropPos = ImGui::GetCursorScreenPos();
        const ImRect dropRect(dropPos, ImVec2(dropPos.x + layout.width, dropPos.y + remainingHeight));
        ImGui::InvisibleButton("##explorer_empty_area_drop", dropRect.GetSize());
        const bool emptyAreaHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            ClearExplorerSelection();
        }
        DrawExplorerEmptyAreaContextMenu("##explorer_empty_area_context");
        const FolderDropZone emptyAreaZone = items.empty() ? FolderDropZone::Into : FolderDropZone::Before;
        std::optional<FolderDropZone> emptyAreaPreview{};
        if (emptyAreaHovered) {
            if (const ImGuiPayload* payload = ActiveExplorerDragPayload()) {
                if (IsExplorerBindDragPayload(payload)) {
                    emptyAreaPreview = emptyAreaZone;
                } else {
                    int folderId = 0;
                    if (TryGetExplorerFolderDragId(payload, folderId)
                        && CanMoveFolderToExplorerDirectory(folderId, currentFolder, static_cast<int>(items.size()))) {
                        emptyAreaPreview = emptyAreaZone;
                    }
                }
            }
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kBindDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                    emptyAreaPreview = emptyAreaZone;
                    if (payload->IsDelivery()) {
                        MoveBindToExplorerDirectory(
                            *static_cast<const int*>(payload->Data),
                            currentFolder,
                            static_cast<int>(items.size()),
                            "explorer_empty_drop");
                    }
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kFolderDragPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->Data != nullptr && payload->DataSize == sizeof(int)) {
                    const int folderId = *static_cast<const int*>(payload->Data);
                    const bool validTarget =
                        CanMoveFolderToExplorerDirectory(folderId, currentFolder, static_cast<int>(items.size()));
                    if (validTarget) {
                        emptyAreaPreview = emptyAreaZone;
                    }
                    if (payload->IsDelivery()) {
                        if (!validTarget) {
                            debuglog::WriteError(
                                "[binder] explorer folder drop rejected id=%d target=empty_area",
                                folderId);
                        } else {
                            MoveFolderToExplorerDirectory(
                                folderId,
                                currentFolder,
                                static_cast<int>(items.size()),
                                "explorer_empty_drop",
                                true);
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (emptyAreaPreview.has_value()) {
            DrawFolderDropPreview(dropRect, *emptyAreaPreview);
        }
    }

    // The custom list advances layout with SetCursorScreenPos(); commit the final
    // position even when the list overflows and no empty-area item is emitted.
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void BinderModule::Impl::DrawExplorerPane() {
    EnsureRootFolder();
    DrawExplorerToolbar();
    ImGui::Separator();

    const bool searchActive = !Trim(bindSearch).empty();
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
        || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    if (searchActive) {
        DrawExplorerSearchResults();
    } else {
        DrawExplorerDirectory();
    }
    DrawExplorerKeyboardShortcuts(focused);

    if (bindDeletePopupPending) {
        ImGui::OpenPopup("##binder_bind_delete");
        bindDeletePopupPending = false;
    }

    if (ImGui::BeginPopupModal("##binder_bind_delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", UiSettings::Instance().Text(UiText::DeleteSelectedBindQuestion));
        if (bindDeleteTarget >= 0 && bindDeleteTarget < static_cast<int>(hotkeys.size())) {
            ImGui::TextDisabled("%s", hotkeys[static_cast<std::size_t>(bindDeleteTarget)].label.c_str());
        }
        if (ImGui::Button(UiSettings::Instance().Text(UiText::Cancel))) {
            bindDeleteTarget = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button(UiSettings::Instance().Text(UiText::Delete))) {
            if (bindDeleteTarget >= 0 && bindDeleteTarget < static_cast<int>(hotkeys.size())) {
                const HotkeyEntry& target = hotkeys[static_cast<std::size_t>(bindDeleteTarget)];
                const ExplorerItem removedItem{ ExplorerItemKind::Bind, target.orderId };
                const std::vector<ExplorerItem> beforeItems = ItemsForFolder(currentFolder);
                const bool removedFromCurrentDirectory =
                    target.categoryId == ActiveCategory().id && target.folderPath == CurrentFolderPath();
                RemoveBindFromExplorerOrders(target.orderId);
                StopHotkey(bindDeleteTarget);
                hotkeys.erase(hotkeys.begin() + bindDeleteTarget);
                RefreshNumbers();
                SaveConfig();
                if (removedFromCurrentDirectory) {
                    SelectExplorerNeighborAfterRemoval(currentFolder, removedItem, beforeItems);
                } else if (explorerSelection.kind == ExplorerSelectionKind::Bind
                    && explorerSelection.bindOrderId == removedItem.key) {
                    ClearExplorerSelection();
                }
            }
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

    ImGui::TextDisabled("%s", ui.Format(UiText::BindListEntryFormat, hotkey->number, BuildBindDisplayLabel(*hotkey).c_str()).c_str());
    ImGui::Spacing();

    const auto drawFolderNode = [&](auto&& self, const BinderCategory& category, FolderNode& folder) -> void {
        ImGui::PushID(folder.id);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (folder.children.empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        const std::string folderLabel = FormatFolderLabel(folder.name);
        const bool opened = ImGui::TreeNodeEx("##move_folder_node", flags, "%s", folderLabel.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            const std::vector<std::string> targetPath = BuildFolderPath(&folder);
            SelectCategory(category.id);
            MoveBindToFolderPath(moveBindTarget, targetPath, "move_popup");
            moveBindTarget = -1;
            ImGui::CloseCurrentPopup();
        }

        if (opened && !folder.children.empty()) {
            for (auto& child : folder.children) {
                if (child) {
                    self(self, category, *child);
                }
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    };

    if (ImGui::BeginChild("##binder_move_bind_folders", ScaleUi(360.0f, 240.0f), ImGuiChildFlags_Borders)) {
        for (const BinderCategory& category : categories) {
            ImGui::PushID(category.id.c_str());
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;
            const std::string categoryLabel = std::string(ui_icons::Folder) + " " + category.name;
            const bool opened = ImGui::TreeNodeEx("##move_bind_category", flags, "%s", categoryLabel.c_str());
            if (ImGui::BeginPopupContextItem("##move_bind_category_context")) {
                ImGui::TextDisabled("%s", category.name.c_str());
                ImGui::EndPopup();
            }
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                SelectCategory(category.id);
                MoveBindToFolderPath(moveBindTarget, {}, "move_popup");
                moveBindTarget = -1;
                ImGui::CloseCurrentPopup();
            }
            if (opened) {
                const std::string rootLabel = std::string(ui_icons::Folder) + " " + ui.Text(UiText::BinderRootName) + "##move_bind_root";
                const bool rootSelected = hotkey->categoryId == category.id && hotkey->folderPath.empty();
                if (ImGui::Selectable(rootLabel.c_str(), rootSelected, ImGuiSelectableFlags_SpanAvailWidth)) {
                    SelectCategory(category.id);
                    MoveBindToFolderPath(moveBindTarget, {}, "move_popup");
                    moveBindTarget = -1;
                    ImGui::CloseCurrentPopup();
                }
                for (auto& folder : category.folders) {
                    if (folder) {
                        drawFolderNode(drawFolderNode, category, *folder);
                    }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
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

    ImGui::TextDisabled("%s", ui.Format(UiText::BindListEntryFormat, hotkey->number, BuildBindDisplayLabel(*hotkey).c_str()).c_str());
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

void BinderModule::Impl::DrawSettingsSection(bool includeHeader) {
    EnsureInitialized();

    UiSettings& ui = UiSettings::Instance();
    if (includeHeader) {
        ImGui::SeparatorText(ui.Text(UiText::QuickMenuWindowTitle));
    }
    ImGui::Text("%s", ui.Format(UiText::QuickMenuFormat, QuickMenuHotkeyText().c_str()).c_str());
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

    const QuickMenuStyle quickStyles[] = { QuickMenuStyle::Tree, QuickMenuStyle::Cascade };
    const char* quickStyleLabels[] = {
        ui.Text(UiText::QuickMenuStyleTree),
        ui.Text(UiText::QuickMenuStyleCascade),
    };
    int quickStyle = quickMenuStyle == QuickMenuStyle::Cascade ? 1 : 0;
    ImGui::SetNextItemWidth(ScaleUi(180.0f));
    if (ImGui::Combo(ui.Text(UiText::QuickMenuStyle), &quickStyle, quickStyleLabels, IM_ARRAYSIZE(quickStyleLabels))) {
        quickMenuStyle = quickStyles[quickStyle];
        ResetQuickMenuVisualState();
        SaveConfig();
    }
}

void BinderModule::Impl::DrawQuickMenu() {
    if (!quickMenuOpen) {
        return;
    }

    NormalizeExplorerOrders();

    const auto hotkeyVisible = [&](const int index) {
        if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
            return false;
        }
        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
        if (!hotkey.enabled || !hotkey.quickMenu) {
            return false;
        }
        const ConditionRuntimeContext context = MakeConditionContext(quickMenuOpen);
        return !ConditionsBlocked(hotkey.conditions, hotkey.conditionsCombine, sampApi, &context);
    };

    const auto categoryVisible = [&](const BinderCategory& category) {
        const ConditionRuntimeContext context = MakeConditionContext(quickMenuOpen);
        return category.quickMenu && !ConditionsBlocked(category.conditions, category.conditionsCombine, sampApi, &context);
    };

    const auto directoryHasVisibleEntries = [&](auto&& self, const BinderCategory& category, const FolderNode* folder) -> bool {
        const std::vector<ExplorerItem>& items = folder ? folder->items : category.rootItems;
        for (const ExplorerItem& item : items) {
            if (item.kind == ExplorerItemKind::Bind) {
                const int index = FindHotkeyIndexByOrderId(item.key);
                if (index >= 0 && hotkeys[static_cast<std::size_t>(index)].categoryId == category.id && hotkeyVisible(index)) {
                    return true;
                }
                continue;
            }
            FolderNode* child = FindFolderByNameInDirectory(category, const_cast<FolderNode*>(folder), item.key);
            if (child && FolderVisibleInQuickMenu(*child) && self(self, category, child)) {
                return true;
            }
        }
        return false;
    };

    std::vector<const BinderCategory*> visibleCategories;
    for (const BinderCategory& category : categories) {
        if (categoryVisible(category) && directoryHasVisibleEntries(directoryHasVisibleEntries, category, nullptr)) {
            visibleCategories.push_back(&category);
        }
    }

    if (visibleCategories.empty()) {
        quickMenuOpen = false;
        ResetQuickMenuVisualState();
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const auto syncOsMouseToImGui = []() {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        if (vp == nullptr || vp->PlatformHandle == nullptr) {
            return;
        }
        const HWND hwnd = reinterpret_cast<HWND>(vp->PlatformHandle);
        POINT pt{};
        if (::GetCursorPos(&pt) == FALSE || ::ScreenToClient(hwnd, &pt) == FALSE) {
            return;
        }
        ImGui::GetIO().AddMousePosEvent(static_cast<float>(pt.x), static_cast<float>(pt.y));
    };
    if (quickMenuPos.x == 0.0f && quickMenuPos.y == 0.0f) {
        quickMenuSize = ScaleUi(static_cast<float>(kQuickMenuWidth), static_cast<float>(kQuickMenuHeight));
        quickMenuPos = ImVec2((io.DisplaySize.x - quickMenuSize.x) * 0.5f, (io.DisplaySize.y - quickMenuSize.y) * 0.5f);
    }

    const bool persistentOpen = quickMenuActivationMode == QuickMenuActivationMode::Toggle;
    bool windowOpen = true;
    int selectedHotkeyIndex = -1;

    const auto closeQuickMenuForSelection = [&]() {
        quickMenuOpen = false;
        quickMenuReopenBlocked = true;
        ResetQuickMenuVisualState();
    };

    const auto hotkeyLabel = [&](const int index, const char* idPrefix) {
        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
        return std::string(ui_icons::Keyboard) + " "
            + BuildBindDisplayLabel(hotkey)
            + "##" + idPrefix + std::to_string(index);
    };

    const auto hotkeyShortcut = [&](const int index) {
        const HotkeyEntry& hotkey = hotkeys[static_cast<std::size_t>(index)];
        return hotkey.keys.empty() ? std::string() : ::hotkeys::ToString(hotkey.keys, hotkey.hotkeyMode);
    };

    ImGui::SetNextWindowPos(quickMenuPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(quickMenuSize, ImGuiCond_Always);
    if (quickMenuFocusPending) {
        ImGui::SetNextWindowFocus();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaleUi(4.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ScaleUi(4.0f, 4.0f));
    if (!ImGui::IsPopupOpen(kQuickMenuHostPopupId)) {
        ImGui::OpenPopup(kQuickMenuHostPopupId);
    }
    if (!ImGui::BeginPopup(kQuickMenuHostPopupId, ImGuiWindowFlags_NoCollapse)) {
        ImGui::PopStyleVar(3);
        if (quickMenuFocusPending) {
            quickMenuFocusPending = false;
        }
        return;
    }
    if (quickMenuFocusPending) {
        ImGui::BringWindowToFocusFront(ImGui::GetCurrentWindow());
        syncOsMouseToImGui();
    }
    quickMenuPos = ImGui::GetWindowPos();
    quickMenuSize = ImGui::GetWindowSize();

    if (persistentOpen && ImGui::Button(UiSettings::Instance().Text(UiText::Cancel))) {
        windowOpen = false;
    }
    ImGui::SeparatorText(UiSettings::Instance().Text(UiText::QuickMenuWindowTitle));

    if (quickMenuStyle == QuickMenuStyle::Cascade) {
        const auto drawCascadeDirectory = [&](auto&& self, const BinderCategory& category, FolderNode* folder, const char* idPrefix) -> void {
            const std::vector<ExplorerItem>& items = folder ? folder->items : category.rootItems;
            for (const ExplorerItem& item : items) {
                if (selectedHotkeyIndex >= 0) {
                    return;
                }
                if (item.kind == ExplorerItemKind::Bind) {
                    const int index = FindHotkeyIndexByOrderId(item.key);
                    if (index < 0 || hotkeys[static_cast<std::size_t>(index)].categoryId != category.id || !hotkeyVisible(index)) {
                        continue;
                    }
                    const std::string label = hotkeyLabel(index, idPrefix);
                    const std::string shortcut = hotkeyShortcut(index);
                    if (ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), false, true)) {
                        selectedHotkeyIndex = index;
                        return;
                    }
                    continue;
                }

                FolderNode* child = FindFolderByNameInDirectory(category, folder, item.key);
                if (!child || !FolderVisibleInQuickMenu(*child) || !directoryHasVisibleEntries(directoryHasVisibleEntries, category, child)) {
                    continue;
                }
                const std::string path = category.id + "/" + JoinPath(BuildFolderPath(child));
                const std::string label = std::string(ui_icons::Folder) + " " + child->name + "##qm_folder_" + path;
                if (ImGui::BeginMenu(label.c_str())) {
                    self(self, category, child, "qm_bind_");
                    ImGui::EndMenu();
                }
            }
        };

        if (visibleCategories.size() > 1) {
            if (ImGui::BeginTabBar("##quick_menu_category_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
                for (const BinderCategory* category : visibleCategories) {
                    if (!category) {
                        continue;
                    }
                    const std::string label = category->name + "##qm_category_tab_" + category->id;
                    if (ImGui::BeginTabItem(label.c_str())) {
                        quickMenuActiveCategoryId = category->id;
                        drawCascadeDirectory(drawCascadeDirectory, *category, nullptr, "qm_root_bind_");
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
        } else {
            quickMenuActiveCategoryId = visibleCategories.front()->id;
            drawCascadeDirectory(drawCascadeDirectory, *visibleCategories.front(), nullptr, "qm_root_bind_");
        }
    } else {
        const auto quickSelectable = [&](const int index, const char* idPrefix) {
            const std::string shortcut = hotkeyShortcut(index);
            std::string label = hotkeyLabel(index, idPrefix);
            if (!shortcut.empty()) {
                label += "\t";
                label += shortcut;
            }
            if (ImGui::Selectable(label.c_str(), false)) {
                selectedHotkeyIndex = index;
            }
        };
        const auto drawTreeDirectory = [&](auto&& self, const BinderCategory& category, FolderNode* folder, int depth) -> void {
            const std::vector<ExplorerItem>& items = folder ? folder->items : category.rootItems;
            for (const ExplorerItem& item : items) {
                if (selectedHotkeyIndex >= 0) {
                    return;
                }
                if (item.kind == ExplorerItemKind::Bind) {
                    const int index = FindHotkeyIndexByOrderId(item.key);
                    if (index >= 0 && hotkeys[static_cast<std::size_t>(index)].categoryId == category.id && hotkeyVisible(index)) {
                        quickSelectable(index, "qm_tree_bind_");
                    }
                    continue;
                }

                FolderNode* child = FindFolderByNameInDirectory(category, folder, item.key);
                if (!child || !FolderVisibleInQuickMenu(*child) || !directoryHasVisibleEntries(directoryHasVisibleEntries, category, child)) {
                    continue;
                }
                const std::string path = category.id + "/" + JoinPath(BuildFolderPath(child));
                const std::string label = std::string(ui_icons::Folder) + " " + child->name + "##qm_tree_folder_" + path;
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
                if (depth == 0) {
                    flags |= ImGuiTreeNodeFlags_DefaultOpen;
                }
                if (ImGui::TreeNodeEx(label.c_str(), flags)) {
                    self(self, category, child, depth + 1);
                    ImGui::TreePop();
                }
            }
        };

        const auto drawTreeList = [&](const BinderCategory& category) {
            if (ImGui::BeginListBox(
                    "##quick_menu_mixed_tree",
                    ImVec2(-FLT_MIN, std::max(ScaleUi(120.0f), ImGui::GetContentRegionAvail().y)))) {
                drawTreeDirectory(drawTreeDirectory, category, nullptr, 0);
                ImGui::EndListBox();
            }
        };

        if (visibleCategories.size() > 1) {
            if (ImGui::BeginTabBar("##quick_menu_category_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
                for (const BinderCategory* category : visibleCategories) {
                    if (!category) {
                        continue;
                    }
                    const std::string label = category->name + "##qm_category_tab_" + category->id;
                    if (ImGui::BeginTabItem(label.c_str())) {
                        quickMenuActiveCategoryId = category->id;
                        drawTreeList(*category);
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
        } else {
            quickMenuActiveCategoryId = visibleCategories.front()->id;
            drawTreeList(*visibleCategories.front());
        }
    }

    if (persistentOpen && !windowOpen) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    ImGui::PopStyleVar(3);
    if (quickMenuFocusPending) {
        quickMenuFocusPending = false;
    }

    if (persistentOpen && !windowOpen) {
        closeQuickMenuForSelection();
        return;
    }
    if (selectedHotkeyIndex >= 0) {
        closeQuickMenuForSelection();
        TryEnqueueHotkey(selectedHotkeyIndex, 0, "quick_menu", "");
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

    ImGui::TextWrapped("%s", ui.Format(UiText::FillBindParametersFormat, BuildBindDisplayLabel(hotkey).c_str()).c_str());
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
        ImGui::SetNextItemWidth(-1.0f);
        InputTextWithHintString(
            "##input_search",
            ui.Text(UiText::InputDialogSearchHint),
            field.searchValue,
            ImGuiInputTextFlags_AutoSelectAll,
            128);
    };
    const auto drawButtonChoice = [&](const std::string& label, bool selected) {
        if (selected) {
            const ImVec4 activeColor = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
            ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, activeColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
        }
        const bool clicked = ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f));
        if (selected) {
            ImGui::PopStyleColor(3);
        }
        return clicked;
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
                    std::string label = dialogButtonLabel(button, buttonIndex);
                    const bool selected =
                        field.input.multiSelect ? field.selectedButtons.contains(buttonIndex) : field.selectedButtonIndex.value_or(-1) == buttonIndex;
                    if (selected && field.input.multiSelect) {
                        label = std::string(ui_icons::Check) + " " + label;
                    }
                    if (drawButtonChoice(label, selected)) {
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
        if (ImGui::BeginChild("##input_buttons_compact", ImVec2(0.0f, ScaleUi(176.0f)), ImGuiChildFlags_FrameStyle)) {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(shownButtons.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const int buttonIndex = shownButtons[static_cast<std::size_t>(row)];
                    const InputButton& button = field.input.buttons[static_cast<std::size_t>(buttonIndex)];
                    std::string label = dialogButtonLabel(button, buttonIndex);
                    const bool selected =
                        field.input.multiSelect ? field.selectedButtons.contains(buttonIndex) : field.selectedButtonIndex.value_or(-1) == buttonIndex;
                    if (selected && field.input.multiSelect) {
                        label = std::string(ui_icons::Check) + " " + label;
                    }
                    if (drawButtonChoice(label, selected)) {
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
        DrawCategoryPopups();
        DrawFolderPopups();
        DrawMoveBindPopup();
        return;
    }

    ImGui::SeparatorText(UiSettings::Instance().Text(UiText::BinderSectionTitle));
    DrawCategoryTabs();
    DrawExplorerPane();

    DrawCategoryPopups();
    DrawFolderPopups();
    DrawEditor();
    DrawMoveBindPopup();
}

void BinderModule::Impl::DrawOverlay() {
    DrawQuickMenu();
    DrawInputDialog();
    DrawCapturePopup(false);
    DrawBindLinesPopup();
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

void BinderModule::SetIncomingMessageRouter(IncomingMessageRouter* incomingMessageRouter) {
    impl_->SetIncomingMessageRouter(incomingMessageRouter);
}

void BinderModule::SetNotificationManager(NotificationManager* notificationManager) {
    impl_->SetNotificationManager(notificationManager);
}

void BinderModule::SetTagsModule(TagsModule* tagsModule) {
    impl_->SetTagsModule(tagsModule);
}

void BinderModule::Tick() {
    impl_->Tick();
}

void BinderModule::SetGameInputForeground(bool gameWindowForeground) {
    impl_->gameInputForeground_ = gameWindowForeground;
}

void BinderModule::Shutdown() {
    impl_->Shutdown();
}

void BinderModule::ReloadConfig() {
    impl_->ReloadConfig();
}

std::string BinderModule::GetThisbindTagValue(std::uint64_t runtimeId) const {
    return impl_->BuildThisbindTagValue(runtimeId);
}

std::string BinderModule::GetThiscategoryTagValue(std::uint64_t runtimeId) const {
    return impl_->BuildThiscategoryTagValue(runtimeId);
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

std::string BinderModule::QuickMenuHotkeyText() const {
    return impl_->QuickMenuHotkeyText();
}

void BinderModule::DrawSettingsSection(bool includeHeader) {
    impl_->DrawSettingsSection(includeHeader);
}

void BinderModule::DrawOverlay() {
    impl_->DrawOverlay();
}
