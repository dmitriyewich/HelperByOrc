#pragma once

#include "binder_module.h"

#include "app_config.h"
#include "binder_editor.h"
#include "binder_types.h"
#include "conditions_module.h"
#include "debug_log.h"
#include "hotkey_utils.h"
#include "icon_picker_ui.h"
#include "icon_registry.h"
#include "incoming_message_router.h"
#include "json_utils.h"
#include "notification_manager.h"
#include "samp_api.h"
#include "samp_hooks.h"
#include "samp_rak_hooks.h"
#include "tags_module.h"
#include "text_pattern_builder.h"
#include "text_pattern_ui_support.h"
#include "text_encoding.h"
#include "ui_icons.h"
#include "ui_settings.h"
#include "variables_picker_ui.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <windows.h>
#include <windowsx.h>

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
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


namespace binder_internal {
inline bool InputTextWithHintString(const char* label, const char* hint, std::string& value, ImGuiInputTextFlags flags, std::size_t minBuffer);

constexpr UINT kDefaultConfirmKey = kBinderDefaultConfirmKey;
constexpr UINT kDefaultCancelKey = kBinderDefaultCancelKey;
constexpr UINT kDefaultQuickMenuFallback = VK_XBUTTON1;
constexpr int kMinMessageIntervalMs = 0;
constexpr int kNestedBindStartDelayMs = 1;
constexpr double kHotkeyDebounceMs = 0.0;
constexpr int kDefaultRepeatIntervalMs = 500;
constexpr int kQuickMenuWidth = 214;
constexpr int kQuickMenuHeight = 277;
// Hidden fixed quick-menu host window; the visible title is drawn manually over the top border.
constexpr char kQuickMenuHostWindowId[] = "##helperbyorc_qm_host";
constexpr int kQuickMenuFocusReassertFrames = 10;
constexpr uint64_t kQuickMenuInputTraceIntervalMs = 500;
constexpr int kTextConfirmTimeoutMs = 3000;
constexpr int kDefaultTextConfirmationWaitTimeoutMs = 60000;
constexpr int kMinTextConfirmationWaitTimeoutMs = 5000;
constexpr int kMaxTextConfirmationWaitTimeoutMs = 600000;
constexpr int kOutgoingGuardTimeoutMs = 2000;
constexpr int kIncomingChatEchoGuardTimeoutMs = 1000;
constexpr char kDialogCaptionLocalChatColorTag[] = "{E2C063}";
constexpr char kDialogSelectionLocalChatColorTag[] = "{E2C063}";
constexpr char kBindDragPayload[] = "BINDER_HOTKEY_INDEX";
constexpr char kFolderDragPayload[] = "BINDER_FOLDER_ID";

inline std::uint32_t MakeRandomSeed(std::uintptr_t salt) {
    const std::uint64_t tick = GetTickCount64();
    const std::uint64_t wideSalt = static_cast<std::uint64_t>(salt);
    std::uint32_t seed = static_cast<std::uint32_t>(tick);
    seed ^= static_cast<std::uint32_t>(tick >> 32);
    seed ^= static_cast<std::uint32_t>(wideSalt);
    seed ^= static_cast<std::uint32_t>(wideSalt >> 32);
    return seed != 0 ? seed : 0xA341316Cu;
}

inline std::uint32_t& BinderRandomState() {
    static std::uint32_t state = MakeRandomSeed(reinterpret_cast<std::uintptr_t>(&BinderRandomState));
    return state;
}

inline std::uint32_t NextRandomU32() {
    std::uint32_t& state = BinderRandomState();
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

inline std::uint32_t RandomBounded(std::uint32_t bound) {
    if (bound == 0) {
        return 0;
    }

    const std::uint32_t threshold = (0u - bound) % bound;
    for (;;) {
        const std::uint32_t value = NextRandomU32();
        if (value >= threshold) {
            return value % bound;
        }
    }
}

inline std::size_t RandomIndex(std::size_t size) {
    return static_cast<std::size_t>(RandomBounded(static_cast<std::uint32_t>(size)));
}

inline std::string Trim(std::string_view value) {
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

inline std::string ToLower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

inline bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

inline bool IsQuickMenuMouseButtonMessage(UINT message) {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        return true;
    default:
        return false;
    }
}

inline bool IsQuickMenuMouseButtonHeld() {
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
        || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0
        || (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
}

inline const char* DbgImGuiWindowName(const ImGuiWindow* window) {
    if (window == nullptr || window->Name == nullptr) {
        return "(null)";
    }
    return window->Name;
}

inline void TraceQuickMenuInput(
    const char* reason,
    const ImVec2& position,
    const ImVec2& size,
    int selectedHotkeyIndex,
    bool comboHeld,
    bool closeAfterMouseFrame,
    bool force = false,
    int manualHitIndex = -1,
    bool manualHit = false,
    bool imguiHovered = false) {
    static uint64_t s_lastTraceMs = 0;
    const uint64_t now = GetTickCount64();
    if (!force && now - s_lastTraceMs < kQuickMenuInputTraceIntervalMs) {
        return;
    }
    s_lastTraceMs = now;

    const ImGuiContext* context = GImGui;
    const ImGuiIO& io = ImGui::GetIO();
    debuglog::WriteInfo(
        "[ui] quickmenu input reason=%s rect=(%.1f,%.1f %.1fx%.1f) HovWin=\"%s\" ActId=0x%08X HovId=0x%08X "
        "MouseDown=(%d,%d,%d) MouseClicked0=%d MouseReleased0=%d selected=%d manualHitIndex=%d manualHit=%d imguiHover=%d comboHeld=%d closeAfterMouse=%d",
        reason ? reason : "",
        position.x,
        position.y,
        size.x,
        size.y,
        DbgImGuiWindowName(context ? context->HoveredWindow : nullptr),
        context ? static_cast<unsigned>(context->ActiveId) : 0,
        context ? static_cast<unsigned>(context->HoveredId) : 0,
        io.MouseDown[0] ? 1 : 0,
        io.MouseDown[1] ? 1 : 0,
        io.MouseDown[2] ? 1 : 0,
        io.MouseClicked[0] ? 1 : 0,
        io.MouseReleased[0] ? 1 : 0,
        selectedHotkeyIndex,
        manualHitIndex,
        manualHit ? 1 : 0,
        imguiHovered ? 1 : 0,
        comboHeld ? 1 : 0,
        closeAfterMouseFrame ? 1 : 0);
}

inline bool ClearDeprecatedHelperCondition(std::vector<bool>& flags) {
    const std::size_t index = static_cast<std::size_t>(ConditionId::HelperActive);
    if (index >= flags.size() || !flags[index]) {
        return false;
    }

    flags[index] = false;
    return true;
}

inline std::string SanitizeFolderName(std::string_view value) {
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

inline bool IsUtf8ContinuationByte(unsigned char value) {
    return (value & 0xC0) == 0x80;
}

inline bool DecodeFirstUtf8Codepoint(std::string_view text, ImWchar& outCodepoint) {
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

inline std::size_t CountUtf8Codepoints(std::string_view value) {
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

inline std::string NormalizeLineEndings(std::string_view value) {
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

inline bool TryCalcIconMetrics(
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
    glyphSize = ImVec2(
        (glyph->X1 - glyph->X0) * glyphScale,
        (glyph->Y1 - glyph->Y0) * glyphScale);
    glyphCenter = ImVec2(
        (glyph->X0 + glyph->X1) * glyphScale * 0.5f,
        (glyph->Y0 + glyph->Y1) * glyphScale * 0.5f);
    return true;
}

inline void DrawCenteredIconGlyph(
    ImDrawList* drawList,
    const char* icon,
    const ImRect& rect,
    const ImU32 color,
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
        const ImVec2 rectCenter(
            rect.Min.x + rect.GetWidth() * 0.5f,
            rect.Min.y + rect.GetHeight() * 0.5f);
        iconPos.x = std::floor(rectCenter.x - glyphCenter.x + 0.5f);
        iconPos.y = std::floor(rectCenter.y - glyphCenter.y + 0.5f);
    } else {
        const ImVec2 iconSize = font
            ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, icon)
            : ImGui::CalcTextSize(icon);
        iconPos.x = std::floor(rect.Min.x + (rect.GetWidth() - iconSize.x) * 0.5f + 0.5f);
        iconPos.y = std::floor(rect.Min.y + (rect.GetHeight() - iconSize.y) * 0.5f + 0.5f);
    }

    const ImVec4 clipRect(rect.Min.x, rect.Min.y, rect.Max.x, rect.Max.y);
    drawList->AddText(font, fontSize, iconPos, color, icon, nullptr, 0.0f, &clipRect);
}

inline bool SmallIconActionButton(const char* icon, const char* id, const char* tooltip, const ImVec2& size) {
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

    DrawCenteredIconGlyph(drawList, icon, ImRect(min, max), ImGui::GetColorU32(ImGuiCol_Text));

    if (tooltip != nullptr && tooltip[0] != '\0' && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
}

inline bool IconOnlyButton(
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
    ImGuiCol iconColorId = active ? ImGuiCol_Text : ImGuiCol_TextDisabled;
    if ((hovered && active) || held) {
        iconColorId = ImGuiCol_Text;
    }
    if (!drawList) {
        drawList = ImGui::GetWindowDrawList();
    }
    DrawCenteredIconGlyph(drawList, icon, ImRect(min, max), ImGui::GetColorU32(iconColorId));

    if (tooltip != nullptr && tooltip[0] != '\0' && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
}

inline std::string Utf8TrimLastChar(std::string_view value) {
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

inline std::string EllipsizeText(std::string_view text, float maxWidth) {
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

inline void CenterNextItemHorizontally(float itemWidth) {
    const float availWidth = ImGui::GetContentRegionAvail().x;
    if (availWidth > itemWidth) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - itemWidth) * 0.5f);
    }
}

inline void DrawCenteredTableHeaderLabel(const char* label, const char* tooltip = nullptr) {
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

inline std::string JoinPath(const std::vector<std::string>& path) {
    std::ostringstream stream;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i != 0) {
            stream << '/';
        }
        stream << path[i];
    }
    return stream.str();
}

inline std::string FormatFolderLabel(std::string_view name) {
    if (name.empty()) {
        return ui_icons::Folder;
    }
    return std::string(ui_icons::Folder) + " " + std::string(name);
}

inline std::string FormatFolderPathLabel(const std::vector<std::string>& path) {
    return FormatFolderLabel(JoinPath(path));
}

inline std::vector<std::string> Split(std::string_view value, char delimiter) {
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

enum class BindListStyle {
    Explorer = 0,
    TwoPane = 1,
};

enum class TwoPaneActivePane {
    Folders = 0,
    Binds = 1,
};

inline std::string BuildBindDisplayLabel(const HotkeyEntry& hotkey) {
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

inline UiText ConfirmationSourceLabelId(std::string_view sourceKind) {
    return sourceKind == "command" ? UiText::EditorToggleCommandConfirm : UiText::EditorToggleTextConfirm;
}

inline const char* ConfirmationSourceLabel(std::string_view sourceKind) {
    return UiSettings::Instance().Text(ConfirmationSourceLabelId(sourceKind));
}

inline bool HasRequiredFirstMessage(const HotkeyEntry& hotkey) {
    return !hotkey.messages.empty() && !Trim(hotkey.messages.front().text).empty();
}

struct LaunchCellContent {
    std::string primary;
    std::vector<std::string> secondary;
};

inline void AppendLaunchLabel(std::vector<std::string>& labels, const char* icon, std::string text) {
    if (text.empty()) {
        return;
    }

    labels.push_back(std::string(icon) + " " + text);
}

inline std::vector<std::string> BuildLaunchLabels(const HotkeyEntry& hotkey) {
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

inline LaunchCellContent BuildLaunchCellContent(const HotkeyEntry& hotkey) {
    const std::vector<std::string> labels = BuildLaunchLabels(hotkey);
    if (labels.empty()) {
        return { UiSettings::Instance().Text(UiText::HotkeyNotSet), {} };
    }

    LaunchCellContent content;
    content.primary = labels.front();
    content.secondary.assign(labels.begin() + 1, labels.end());
    return content;
}

inline std::string JoinLaunchLabels(const std::vector<std::string>& labels, std::string_view separator) {
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
    bool modernVisual = false;
};

struct FolderNode {
    int id = 0;
    std::string name;
    std::string iconId;
    FolderNode* parent = nullptr;
    std::vector<std::unique_ptr<FolderNode>> children;
    std::vector<ExplorerItem> items;
    std::vector<bool> conditions;
    ConditionCombineMode conditionsCombine = ConditionCombineMode::RequireAny;
    bool enabled = true;
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

inline std::string FolderIconGlyph(const FolderNode& folder) {
    if (!folder.iconId.empty()) {
        if (std::string glyph = icon_registry::ResolveGlyph(folder.iconId); !glyph.empty()) {
            return glyph;
        }
    }
    return ui_icons::Folder;
}

inline std::string BindIconGlyph(const HotkeyEntry& hotkey) {
    if (!hotkey.iconId.empty()) {
        if (std::string glyph = icon_registry::ResolveGlyph(hotkey.iconId); !glyph.empty()) {
            return glyph;
        }
    }
    return ui_icons::Keyboard;
}

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

inline BindTagAction ParseBindTagActionName(std::string_view action) {
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

struct BindTagContextDesc {
    int hotkeyIndex = -1;
    std::string name{};
    std::string folder{};
    std::string category{};
};

struct PendingBindTagAction {
    BindTagAction action = BindTagAction::Unknown;
    std::uint64_t sourceRuntimeId = 0;
    std::string actionName{};
    std::string selector{};
    std::vector<int> targetIndices{};
};

struct PendingCommandDispatch {
    std::uint64_t sourceRuntimeId = 0;
    std::string command{};
    int method = 2;
};

struct InputDialogField {
    HotkeyInput input;
    std::string textValue;
    std::string searchValue;
    std::optional<int> selectedButtonIndex;
    std::set<int> selectedButtons;
    int activeButtonIndex = -1;
};

struct InputDialogState {
    int hotkeyIndex = -1;
    int startDelayMs = 0;
    std::vector<InputDialogField> fields;
    std::string activationSource;
    std::string activationText;
    std::string bindCommand;
    int activeFieldIndex = 0;
    bool popupOpened = false;
};

enum class CaptureTarget {
    None,
    BindHotkey,
    QuickMenuHotkey,
    ConfirmKey,
    CancelKey,
};

inline bool InputModeUsesButtons(InputMode mode);
inline InputMode NormalizeInputMode(std::string_view value);
inline std::string InputModeId(InputMode mode);
inline HotkeyMode NormalizeHotkeyMode(std::string_view value);
inline std::string HotkeyModeId(HotkeyMode mode);
inline QuickMenuActivationMode NormalizeQuickMenuActivationMode(std::string_view value);
inline std::string QuickMenuActivationModeId(QuickMenuActivationMode mode);
inline BindListStyle NormalizeBindListStyle(std::string_view value);
inline std::string BindListStyleId(BindListStyle style);
inline std::string NormalizeInputKey(std::string_view value);
inline std::size_t CountUtf8Codepoints(std::string_view value);
inline std::vector<InputButton> ParseButtonsText(std::string_view multiLine);
inline std::vector<InputButton> ParseButtonsTextEx(std::string_view multiLine, ButtonsTextParseStats* stats);
inline std::string SerializeButtonsText(const std::vector<InputButton>& buttons);
inline void AppendButtonsBulkLine(std::string& text, std::string_view line);
inline std::string BuildButtonsBulkTemplateLine(int index);
inline void AppendButtonsBulkTemplateLines(std::string& text, int count);
inline bool InputTextString(
    const char* label,
    std::string& value,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 256,
    ImGuiInputTextCallback chain = nullptr,
    void* chainUserData = nullptr);
inline bool InputTextMultilineString(
    const char* label,
    std::string& value,
    const ImVec2& size,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 2048);
inline bool InputTextMultilineWithCounterString(
    const char* label,
    std::string& value,
    const ImVec2& size,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 2048);
inline JsonValue SerializeBoolArray(const std::vector<bool>& flags);
inline std::vector<bool> DeserializeBoolArray(const JsonArray* array);
inline JsonValue SerializeUintArray(const std::vector<UINT>& values);
inline std::vector<UINT> DeserializeUintArray(const JsonArray* array);
inline JsonValue SerializeStringArray(const std::vector<std::string>& values);
inline std::vector<std::string> DeserializeStringArray(const JsonArray* array);
inline FolderNode* FindFolderByPath(const std::vector<std::unique_ptr<FolderNode>>& folders, const std::vector<std::string>& path);
inline void CollectFolderPaths(const std::vector<std::unique_ptr<FolderNode>>& folders, std::vector<std::vector<std::string>>& out, std::vector<std::string> prefix = {});
inline std::vector<std::string> BuildFolderPath(const FolderNode* folder);
inline bool PathStartsWith(const std::vector<std::string>& path, const std::vector<std::string>& prefix);


template <typename Container>
inline auto FindRunningBindByRuntimeId(Container& runningBinds, std::uint64_t hotkeyRuntimeId)
    -> decltype(runningBinds.empty() ? nullptr : std::addressof(runningBinds.front())) {
    if (hotkeyRuntimeId == 0) {
        return nullptr;
    }

    const auto it = std::find_if(runningBinds.begin(), runningBinds.end(), [&](const auto& running) {
        return running.hotkeyRuntimeId == hotkeyRuntimeId;
    });
    return it == runningBinds.end() ? nullptr : std::addressof(*it);
}

inline std::uint64_t HotkeyRuntimeIdAt(const std::vector<HotkeyEntry>& hotkeys, int index) {
    if (index < 0 || index >= static_cast<int>(hotkeys.size())) {
        return 0;
    }
    return hotkeys[static_cast<std::size_t>(index)].runtimeId;
}

inline std::string StripColorTags(std::string_view text) {
    static const std::regex kColorTagRegex("\\{[0-9a-fA-F]{6,8}\\}");
    return std::regex_replace(std::string(text), kColorTagRegex, "");
}

inline std::string NormalizeTriggerText(std::string_view text) {
    return Trim(NormalizeLineEndings(StripColorTags(text)));
}

inline std::string ToUtf8ForDisplay(std::string_view text) {
    return textencoding::GameToUtf8(text);
}

inline std::string ToGameText(std::string_view text) {
    return textencoding::Utf8ToGame(text);
}

inline bool ParseInt(std::string_view text, int& value) {
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

inline UINT PickSingleCapturedKey(const std::vector<UINT>& keys, UINT fallback) {
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
        if (!hotkeys::IsModifierKey(*it)) {
            return *it;
        }
    }
    return keys.empty() ? fallback : keys.back();
}

inline const char* SendMethodLabel(int method) {
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

inline bool EqualNoCase(std::string_view lhs, std::string_view rhs) {
    return ToLower(lhs) == ToLower(rhs);
}

inline bool ContainsNoCase(std::string_view haystack, std::string_view needle) {
    const std::string loweredNeedle = ToLower(needle);
    if (loweredNeedle.empty()) {
        return true;
    }
    return ToLower(haystack).find(loweredNeedle) != std::string::npos;
}

inline bool EndsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

inline float ScaleUi(float value) {
    return UiSettings::Instance().Scale(value);
}

inline ImVec2 ScaleUi(float x, float y) {
    return UiSettings::Instance().Scale(ImVec2(x, y));
}

inline ImVec4 WithAlpha(ImVec4 color, float alpha) {
    color.w = alpha;
    return color;
}

inline ImVec4 BlendColor(const ImVec4& a, const ImVec4& b, const float t) {
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t);
}

} // namespace binder_internal

#include "binder_ui_common.h"

namespace binder_internal {



inline bool InputModeUsesButtons(InputMode mode) {
    return mode == InputMode::ButtonsList || mode == InputMode::ButtonsListText;
}

inline InputMode NormalizeInputMode(std::string_view value) {
    const std::string normalized = ToLower(value);
    if (normalized == "buttons" || normalized == "buttons_combo" || normalized == "buttons_list_text") {
        return InputMode::ButtonsListText;
    }
    if (normalized == "buttons_list") {
        return InputMode::ButtonsList;
    }
    return InputMode::Text;
}

inline std::string InputModeId(InputMode mode) {
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

inline HotkeyMode NormalizeHotkeyMode(std::string_view value) {
    return ToLower(value) == "ordered_combo" ? HotkeyMode::OrderedCombo : HotkeyMode::ModifierTrigger;
}

inline std::string HotkeyModeId(HotkeyMode mode) {
    return mode == HotkeyMode::OrderedCombo ? "ordered_combo" : "modifier_trigger";
}

inline QuickMenuActivationMode NormalizeQuickMenuActivationMode(std::string_view value) {
    return ToLower(value) == "toggle" ? QuickMenuActivationMode::Toggle : QuickMenuActivationMode::Hold;
}

inline std::string QuickMenuActivationModeId(QuickMenuActivationMode mode) {
    return mode == QuickMenuActivationMode::Toggle ? "toggle" : "hold";
}

inline BindListStyle NormalizeBindListStyle(std::string_view value) {
    const std::string normalized = ToLower(value);
    if (normalized == "two_pane" || normalized == "two-pane" || normalized == "twopane" || normalized == "split") {
        return BindListStyle::TwoPane;
    }
    return BindListStyle::Explorer;
}

inline std::string BindListStyleId(BindListStyle style) {
    return style == BindListStyle::TwoPane ? "two_pane" : "explorer";
}

inline QuickMenuStyle NormalizeQuickMenuStyle(std::string_view value) {
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

inline std::string QuickMenuStyleId(QuickMenuStyle style) {
    switch (style) {
    case QuickMenuStyle::Tree:
        return "tree";
    case QuickMenuStyle::Cascade:
        return "cascade";
    }
    return "cascade";
}

inline std::string NormalizeInputKey(std::string_view value) {
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

inline std::string EscapeButtonsField(std::string_view value) {
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

inline std::string UnescapeButtonsField(std::string_view value) {
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

inline std::vector<std::string> SplitEscapedButtonsLine(std::string_view line) {
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

inline std::vector<InputButton> ParseButtonsTextEx(std::string_view multiLine, ButtonsTextParseStats* stats) {
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

inline std::vector<InputButton> ParseButtonsText(std::string_view multiLine) {
    return ParseButtonsTextEx(multiLine, nullptr);
}

inline std::string SerializeButtonsText(const std::vector<InputButton>& buttons) {
    std::ostringstream stream;
    const bool includeWhen = std::any_of(buttons.begin(), buttons.end(), [](const InputButton& button) {
        return !Trim(button.when).empty();
    });
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const InputButton& button = buttons[i];
        stream << EscapeButtonsField(button.label)
               << " | " << EscapeButtonsField(button.text)
               << " | " << EscapeButtonsField(button.hint);
        if (includeWhen) {
            stream << " | " << EscapeButtonsField(button.when);
        }
        if (i + 1 != buttons.size()) {
            stream << "\n";
        }
    }
    return stream.str();
}

inline void AppendButtonsBulkLine(std::string& text, std::string_view line) {
    if (!text.empty() && text.back() != '\n') {
        text.push_back('\n');
    }
    text.append(line.data(), line.size());
}

inline std::string BuildButtonsBulkTemplateLine(int index) {
    UiSettings& ui = UiSettings::Instance();
    return EscapeButtonsField(ui.Format(UiText::ButtonLabelFormat, index))
        + " | " + EscapeButtonsField(ui.Format(UiText::ButtonsBulkTemplateValueFormat, index))
        + " | " + EscapeButtonsField(ui.Format(UiText::ButtonsBulkTemplateHintFormat, index));
}

inline void AppendButtonsBulkTemplateLines(std::string& text, int count) {
    const int startIndex = static_cast<int>(ParseButtonsText(text).size()) + 1;
    for (int i = 0; i < count; ++i) {
        AppendButtonsBulkLine(text, BuildButtonsBulkTemplateLine(startIndex + i));
    }
}


struct ImGuiStringUserData {
    std::string* value = nullptr;
    ImGuiInputTextCallback chain = nullptr;
    void* chainUserData = nullptr;
};

inline int ImGuiStringResizeCallback(ImGuiInputTextCallbackData* data) {
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

struct InputTextSetCaretData {
    int caretByte = -1;
    bool applied = false;
};

inline int SetInputTextCaretCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag != ImGuiInputTextFlags_CallbackAlways) {
        return 0;
    }

    if (auto* userData = static_cast<InputTextSetCaretData*>(data->UserData)) {
        const int caretByte = std::clamp(userData->caretByte, 0, data->BufTextLen);
        data->CursorPos = caretByte;
        data->SelectionStart = caretByte;
        data->SelectionEnd = caretByte;
        userData->applied = true;
    }
    return 0;
}


inline bool InputTextString(
    const char* label,
    std::string& value,
    ImGuiInputTextFlags flags,
    std::size_t minBuffer,
    ImGuiInputTextCallback chain,
    void* chainUserData) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value, chain, chainUserData };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputText(label, value.data(), value.capacity() + 1, flags, ImGuiStringResizeCallback, &userData);
}

inline bool InputTextWithHintString(const char* label, const char* hint, std::string& value, ImGuiInputTextFlags flags, std::size_t minBuffer) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value, nullptr, nullptr };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputTextWithHint(
        label, hint, value.data(), value.capacity() + 1, flags, ImGuiStringResizeCallback, &userData);
}

inline bool InputTextMultilineString(
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

inline bool InputTextMultilineWithCounterString(
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

inline JsonValue SerializeBoolArray(const std::vector<bool>& flags) {
    JsonArray array;
    array.reserve(flags.size());
    for (const bool flag : flags) {
        array.emplace_back(flag);
    }
    return JsonValue(std::move(array));
}

inline std::vector<bool> DeserializeBoolArray(const JsonArray* array) {
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

inline JsonValue SerializeUintArray(const std::vector<UINT>& values) {
    JsonArray array;
    array.reserve(values.size());
    for (const UINT value : values) {
        array.emplace_back(static_cast<double>(value));
    }
    return JsonValue(std::move(array));
}

inline std::vector<UINT> DeserializeUintArray(const JsonArray* array) {
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

inline JsonValue SerializeStringArray(const std::vector<std::string>& values) {
    JsonArray array;
    array.reserve(values.size());
    for (const std::string& value : values) {
        array.emplace_back(value);
    }
    return JsonValue(std::move(array));
}

inline std::vector<std::string> DeserializeStringArray(const JsonArray* array) {
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

inline FolderNode* FindFolderByPath(const std::vector<std::unique_ptr<FolderNode>>& folders, const std::vector<std::string>& path) {
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

inline void CollectFolderPaths(
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

inline std::vector<std::string> BuildFolderPath(const FolderNode* folder) {
    std::vector<std::string> path;
    for (auto* current = folder; current; current = current->parent) {
        path.push_back(current->name);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

inline bool PathStartsWith(const std::vector<std::string>& path, const std::vector<std::string>& prefix) {
    return path.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), path.begin());
}

inline bool IsLegacyRootFolderName(std::string_view name) {
    return name == "Биндер" || name == "Binder";
}

inline std::vector<std::string> ReplacePathPrefix(
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

inline bool FolderNameUnique(
    const std::vector<std::unique_ptr<FolderNode>>& folders,
    std::string_view name,
    const FolderNode* ignoredFolder = nullptr) {
    for (const auto& folder : folders) {
        if (!folder || folder.get() == ignoredFolder) {
            continue;
        }
        if (binder_tags::EqualNoCaseUtf8(folder->name, name)) {
            return false;
        }
    }
    return true;
}

inline void ExpandFolderBranch(FolderNode* folder) {
    for (FolderNode* current = folder; current; current = current->parent) {
        current->open = true;
    }
}



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

enum class ExplorerDirectoryDropStatus {
    None,
    Accept,
    Noop,
    Invalid,
};

struct FolderMoveUndo {
    int nodeId = 0;
    int oldListParentId = -1; // -1 = root `folders` list, >0 = id of parent folder
    int oldIndex = 0;
};

inline FolderDropZone ResolveFolderDropZone(const ImRect& rect) {
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

inline void DrawFolderDropPreview(const ImRect& rect, FolderDropZone zone) {
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

inline const ImGuiPayload* ActiveExplorerDragPayload() {
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (payload == nullptr) {
        return nullptr;
    }
    if (!payload->IsDataType(kBindDragPayload) && !payload->IsDataType(kFolderDragPayload)) {
        return nullptr;
    }
    return payload;
}

inline bool IsExplorerBindDragPayload(const ImGuiPayload* payload) {
    return payload != nullptr
        && payload->IsDataType(kBindDragPayload)
        && payload->Data != nullptr
        && payload->DataSize == sizeof(int);
}

inline bool TryGetExplorerFolderDragId(const ImGuiPayload* payload, int& folderId) {
    if (payload == nullptr
        || !payload->IsDataType(kFolderDragPayload)
        || payload->Data == nullptr
        || payload->DataSize != sizeof(int)) {
        return false;
    }
    folderId = *static_cast<const int*>(payload->Data);
    return true;
}

inline FolderNode* FindFolderByIdR(std::vector<std::unique_ptr<FolderNode>>& from, int id) {
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

inline bool FindFolderListPos(
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

inline FolderNode* FindListOwnerRecurse(FolderNode* node, const std::vector<std::unique_ptr<FolderNode>>* list) {
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

inline FolderNode* FindListOwner(
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

inline bool IsUnderOrEqual(const FolderNode* ancestor, const FolderNode* node) {
    for (const FolderNode* c = node; c; c = c->parent) {
        if (c == ancestor) {
            return true;
        }
    }
    return false;
}

inline bool TryParseSampColorTag(std::string_view text, std::size_t offset, std::size_t& consumed, std::uint32_t& color) {
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

inline std::pair<std::string, std::uint32_t> ParseLeadingChatColor(std::string_view text) {
    std::size_t consumed = 0;
    std::uint32_t color = 0;
    if (TryParseSampColorTag(text, 0, consumed, color)) {
        return { std::string(text.substr(consumed)), color };
    }
    return { std::string(text), 0xFFFFFFFFu };
}

inline std::string StripSampColorTags(std::string_view text) {
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

inline std::string NormalizeDialogVisibleText(std::string_view text) {
    return Trim(StripSampColorTags(text));
}

inline std::string DecorateDialogLocalChatText(std::string_view text, SampApi* sampApi) {
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

inline bool SetClipboardUtf8Text(std::string_view utf8Text) {
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



constexpr std::string_view kBinderConfigSectionName = "binder";
constexpr char kEditorVariablesPopupId[] = "###binder_editor_variables";

} // namespace binder_internal

using namespace binder_internal;

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
    bool helperUiActive_ = false;
    bool prevFrameGameInputForeground_ = true;
    bool deprecatedHelperConditionMigrated_ = false;
    int deprecatedHelperBindDisabledCount_ = 0;
    int deprecatedHelperCategoryQuickDisabledCount_ = 0;
    int deprecatedHelperFolderQuickDisabledCount_ = 0;

    std::string bindSearch{};
    std::string twoPaneFolderSearch{};
    std::string twoPaneBindSearch{};
    std::optional<FolderMoveUndo> folderMoveUndo_{};

    binder_editor::State editor{};
    icon_picker::State iconPickerState{};

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
    FolderNode* folderIconTarget = nullptr;
    bool folderIconPopupPending = false;
    std::string categoryRenameTargetId{};
    std::string categoryRenameBuffer{};
    bool categoryRenamePopupPending = false;
    std::string categoryConditionsTargetId{};
    bool categoryConditionsPopupPending = false;
    std::string categoryDeleteTargetId{};
    std::string categoryDeleteMoveTargetId{};
    bool categoryDeletePopupPending = false;
    std::string categoryTabSelectionTargetId{};
    std::uint64_t categoryTabOrderRevision = 0;
    int bindDeleteTarget = -1;
    bool bindDeletePopupPending = false;
    int moveBindTarget = -1;
    bool moveBindPopupPending = false;
    int moveFolderTarget = -1;
    bool moveFolderPopupPending = false;
    int bindLinesTarget = -1;
    int selectedBindIndex = -1;
    bool bindLinesPopupPending = false;

    struct QuickMenuHitItem {
        ImRect rect{};
        int hotkeyIndex = -1;
    };

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
    bool quickMenuShowScrollbar = true;
    BindListStyle bindListStyle = BindListStyle::Explorer;
    TwoPaneActivePane twoPaneActivePane = TwoPaneActivePane::Binds;
    int textConfirmationWaitTimeoutMs = kDefaultTextConfirmationWaitTimeoutMs;
    bool quickMenuOpen = false;
    std::string quickMenuActiveCategoryId{};
    bool quickMenuReopenBlocked = false;
    bool quickMenuToggleLatch = false;
    bool quickMenuFocusPending = false;
    int quickMenuFocusReassertFrames = 0;
    bool quickMenuMouseEventPending = false;
    bool quickMenuMouseEventInside = false;
    bool quickMenuCloseAfterMouseFrame = false;
    bool quickMenuMouseClientPosValid = false;
    ImVec2 quickMenuMouseClientPos{ 0.0f, 0.0f };
    std::vector<QuickMenuHitItem> quickMenuHitItems{};
    ImVec2 quickMenuPos{ 0.0f, 0.0f };
    ImVec2 quickMenuSize{ static_cast<float>(kQuickMenuWidth), static_cast<float>(kQuickMenuHeight) };
    std::optional<bool> quickMenuSampCursorActiveAtOpen{};
    std::optional<bool> quickMenuWindowsCursorActiveAtOpen{};

    std::optional<InputDialogState> inputDialog{};
    std::vector<RunningBind> runningBinds{};
    std::vector<OutgoingGuard> outgoingGuards{};
    std::vector<IncomingChatEchoGuard> incomingChatEchoGuards{};
    std::vector<PendingBindTagAction> pendingBindTagActions{};
    std::vector<PendingCommandDispatch> pendingCommandDispatches{};
    std::vector<std::uint64_t> pendingSelfRestarts{};

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
    bool MoveCategoryByOffset(std::string_view categoryId, int offset);
    void DeleteCategory(std::string_view categoryId, std::string_view moveTargetId, bool deleteContents);
    void MoveCategoryContents(BinderCategory& from, BinderCategory& to);
    FolderNode* EnsureRootFolder();
    std::uint64_t AllocateHotkeyRuntimeId();
    std::string AllocateHotkeyOrderId();
    HotkeyEntry MakeDefaultHotkey();
    void RefreshNumbers();
    void SaveConfig();
    void LoadConfig();
    bool MigrateDeprecatedHelperCondition(std::vector<bool>& conditions);
    void ApplyDeprecatedHelperBindMigration(HotkeyEntry& hotkey, bool helperOnly);
    void LogDeprecatedHelperConditionMigration() const;
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
    bool IsFolderEffectivelyEnabled(const FolderNode* folder) const;
    bool IsFolderPathEnabled(const BinderCategory& category, const std::vector<std::string>& path) const;
    bool IsHotkeyFolderEnabled(const HotkeyEntry& hotkey) const;
    bool IsHotkeyEffectivelyEnabled(const HotkeyEntry& hotkey) const;
    bool IsHotkeyEffectivelyEnabled(int index) const;
    bool IsExplorerSelectionVisibleInFolder(const ExplorerSelection& selection, FolderNode* directory);
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
    bool MoveFolderToCategoryDirectory(
        int folderId,
        std::string_view targetCategoryId,
        const std::vector<std::string>& targetFolderPath,
        std::string_view source);
    bool CanMoveFolderToExplorerDirectory(int folderId, FolderNode* targetFolder, int insertIndex, bool* noop = nullptr);
    ExplorerDirectoryDropStatus CanDropExplorerPayloadToDirectory(
        const ImGuiPayload* payload,
        FolderNode* targetFolder,
        bool rejectNoop);
    bool DropExplorerPayloadToTwoPaneDirectory(
        const ImGuiPayload* payload,
        FolderNode* targetFolder,
        std::optional<int> insertIndex,
        std::string_view source);
    bool DropExplorerPayloadToDirectory(const ImGuiPayload* payload, FolderNode* targetFolder, std::string_view source);
    ExplorerDirectoryDropStatus DrawExplorerDirectoryDropTarget(
        const ImRect& rect,
        FolderNode* targetFolder,
        std::string_view source,
        bool activeCategoryTarget,
        bool rejectCurrentFolder,
        bool rejectNoop);
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
    bool FolderVisibleInQuickMenu(const FolderNode& folder, const ConditionRuntimeContext& context) const;
    ConditionRuntimeContext MakeConditionContext(bool quickMenuContext = false) const;
    bool IsSilentActivationSource(std::string_view source) const;
    bool IsManualActivationSource(std::string_view source) const;
    bool CanToggleRunningHotkeyActivation(std::string_view source) const;
    bool ShouldNotifyRunningHotkeyToggle(std::string_view source) const;
    bool ShouldBlockHelperCursorActivation(const HotkeyEntry& hotkey, std::string_view source) const;
    bool ConditionsBlockHotkeyStart(
        const HotkeyEntry& hotkey,
        std::string_view source,
        std::string* message = nullptr) const;
    void ResetInputState();
    void Tick();
    void Shutdown();
    void ReloadConfig();
    bool WantsOverlayRender() const;
    bool WantsInputCapture() const;
    bool WantsInputRouting() const;
    bool IsQuickMenuOpen() const;
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
    void CenterQuickMenuCursorOnGameWindow();
    void ResetQuickMenuVisualState();
    void ProcessHotkeys();
    void ProcessRunningBinds();
    int FindHotkeyIndexByRuntimeId(std::uint64_t runtimeId) const;
    BindTagContextDesc DescribeBindTagContext(std::uint64_t runtimeId) const;
    binder_tags::Catalog BuildBindSelectorCatalog() const;
    static std::string EscapeBindTagLogValue(std::string_view value);
    std::string DescribeBindTagLogSource(std::uint64_t runtimeId) const;
    std::string DescribeBindTagLogTargets(const std::vector<int>& targetIndices) const;
    std::string BuildThisbindTagValue(std::uint64_t runtimeId) const;
    std::string BuildThisbindNameTagValue(std::uint64_t runtimeId) const;
    std::string BuildThisbindFolderTagValue(std::uint64_t runtimeId) const;
    std::string BuildThiscategoryTagValue(std::uint64_t runtimeId) const;
    bool IsRuntimeActive(std::uint64_t runtimeId) const;
    bool IsRuntimePaused(std::uint64_t runtimeId) const;
    bool PauseRuntime(std::uint64_t runtimeId);
    bool ResumeRuntime(std::uint64_t runtimeId);
    bool StopRuntime(std::uint64_t runtimeId);
    BinderModule::TagActionResult ExecuteTagAction(std::string_view action, std::string_view param, std::uint64_t sourceRuntimeId);
    void QueueSelfRestart(std::uint64_t runtimeId);
    bool StartPendingSelfRestart(std::uint64_t runtimeId);
    RunningBind* FindRunningBind(std::uint64_t hotkeyRuntimeId);
    const RunningBind* FindRunningBind(std::uint64_t hotkeyRuntimeId) const;
    RunningBind* FindRunningBindForHotkey(int index);
    const RunningBind* FindRunningBindForHotkey(int index) const;
    bool IsHotkeyRunning(int index) const;
    bool IsHotkeyPaused(int index) const;
    bool TryToggleRunningHotkeyActivation(int index, std::string_view source, double now);
    bool PauseHotkey(int index);
    bool ResumeHotkey(int index);
    bool StopHotkey(int index);
    int StopAllHotkeys();
    void StopHotkeyByRuntimeId(std::uint64_t runtimeId);
    void ExecutePendingBindTagActions(std::uint64_t sourceRuntimeId);
    void ExecutePendingCommandDispatches(std::uint64_t sourceRuntimeId);
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
    bool HasCommandTriggerCandidate(const std::string& normalizedCommand, double now) const;
    bool DispatchCommandTrigger(const std::string& normalizedCommand, int startDelayMs, bool allowRunningToggle = true);
    bool QueueCommandDispatchFromRunningBind(const std::string& normalizedCommand, std::uint64_t sourceRuntimeId, int method);
    bool TryBeginPendingConfirmation(
        HotkeyEntry& hotkey,
        std::string_view sourceKind,
        const std::string& sourceText,
        bool waitForResolution);
    bool OnOutgoingCommand(const std::string& text);
    bool OnOutgoingChat(const std::string& text);
    void OnIncomingMessage(const IncomingMessageEvent& message);
    void ExpireTextConfirmations();
    bool ActivatePendingTextConfirmations(UINT keyCode);
    bool MatchTextTrigger(const std::string& source, HotkeyEntry& hotkey);
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
        std::string_view actionName,
        std::uint64_t sourceRuntimeId = 0);
    std::string DescribeBindTagError(std::string_view actionName, std::string_view error) const;
    bool RequestBindLinesPopup(int index);
    std::string BuildInputValue(const InputDialogField& field) const;
    std::vector<int> FilterButtons(const InputDialogState& dialog, std::size_t fieldIndex) const;
    bool TryEnqueueHotkey(HotkeyEntry& hotkey, int startDelayMs, std::string_view source, const std::string& sourceText);
    bool TryEnqueueHotkey(int index, int startDelayMs, std::string_view source, const std::string& sourceText);
    void SendExpandedText(const std::string& expandedText, int method, const TagsModule::CursorIntents* cursorIntents = nullptr);
    bool DoSend(const std::string& text, int method, std::uint64_t sourceRuntimeId = 0);
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
    void DrawBinderSettingsSection(bool includeHeader);
    void DrawInputDialog();
    void StartEditing(int index, bool isNew);
    std::vector<HotkeyMessage> ParseEditorMultiMessages(const std::vector<HotkeyMessage>& reference) const;
    void SyncEditorMessagesToMulti();
    void ApplyEditorMultiToDraft(bool applyBulkToAll);
    HotkeyEntry BuildEditorComparableDraft() const;
    bool EditorHasUnsavedChanges() const;
    std::pair<int, int> EditorNeighborIndices() const;
    void RequestEditorAction(binder_editor::State::PendingAction action, int targetIndex = -1);
    void ExecuteEditorPendingAction();
    bool ValidateEditor(std::vector<std::string>& errors);
    void SaveEditor();
    bool CopyTextToClipboard(std::string_view text, bool showSuccessToast = true);
    std::vector<variables_picker::Entry> BuildEditorVariablePickerEntries() const;
    void HandleEditorVariablePickerRequest(const variables_picker::Request& request);
    bool InsertTextIntoEditorVariableTarget(std::string_view text);
    bool CommitEditorScenarioAppendText();
    void RememberEditorVariableInsertTarget(
        binder_editor::State::VariableInsertTarget target,
        int messageIndex,
        int cursorByte,
        int selectionStartByte,
        int selectionEndByte);
    bool DrawEditorVariableKeyPickerPopup();
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
    void DrawTwoPaneBinder();
    void DrawTwoPaneFolderPane();
    void DrawTwoPaneRootRow(int& rowIndex, const std::string& filter);
    bool TwoPaneFolderMatchesFilter(const FolderNode& folder, std::string_view filter) const;
    void DrawTwoPaneFolderNode(FolderNode& folder, int depth, int& rowIndex, const std::string& filter);
    void DrawTwoPaneFolderInlineEditRow(FolderNode* parent, int depth, int& rowIndex);
    void DrawTwoPaneBindPane();
    void DrawTwoPaneBindDirectory();
    bool TwoPaneBindMatchesFilter(const HotkeyEntry& hotkey, std::string_view filter) const;
    int FolderOrderIndex(FolderNode* folder) const;
    void SetTwoPaneActivePane(TwoPaneActivePane pane);
    void DrawTwoPaneKeyboardShortcuts(bool focused);
    void DrawTwoPaneFolderKeyboardShortcuts();
    void DrawTwoPaneBindKeyboardShortcuts();
    void MoveTwoPaneFolderSelection(int delta, const std::string& filter);
    void MoveTwoPaneBindSelection(int delta, const std::string& filter);
    std::vector<FolderNode*> CollectTwoPaneVisibleFolders(const std::string& filter);
    void CollectTwoPaneVisibleFolderNodes(FolderNode& folder, const std::string& filter, std::vector<FolderNode*>& out);
    std::vector<int> CollectTwoPaneVisibleBindIndices(const std::string& filter);
    void DrawBindDeletePopup();
    void DrawExplorerToolbar();
    void DrawExplorerSearchResults();
    void DrawExplorerDirectory();
    void DrawExplorerEmptyAreaContextMenu(const char* popupId);
    void DrawExplorerInlineFolderEditRow(int rowIndex, const ExplorerListLayout& layout, const ImRect& rowRect);
    void DrawExplorerInlineFolderEditContent(const ExplorerListLayout& layout, const ImRect& rowRect, bool selected);
    void DrawExplorerFolderRow(FolderNode& folder, int rowIndex, const ExplorerListLayout& layout, const ImRect& rowRect);
    void DrawExplorerBindRow(
        int index,
        int rowIndex,
        const ExplorerListLayout& layout,
        const ImRect& rowRect,
        bool acceptFolderPayload = true);
    void DrawExplorerKeyboardShortcuts(bool focused);
    void DrawExplorerBreadcrumb();
    void DrawMoveBindPopup();
    void DrawMoveFolderPopup();
    void DrawBindLinesPopup();
    void DrawInputEditor();
    void DrawEditor();
    void DuplicateHotkeyAt(int index);
    void DrawMainTab();
    void DrawOverlay();
};
