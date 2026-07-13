#include "hud_module.h"

#include "app_config.h"
#include "conditions_module.h"
#include "debug_log.h"
#include "hud_v3_editor_tools.h"
#include "hud_v3_model.h"
#include "hud_v3_runtime.h"
#include "icon_picker_ui.h"
#include "icon_registry.h"
#include "json_utils.h"
#include "markup_renderer.h"
#include "native_file_dialog.h"
#include "notepad_module.h"
#include "samp_api.h"
#include "tags_module.h"
#include "ui_fonts.h"
#include "ui_icons.h"
#include "ui_settings.h"
#include "variables_picker_ui.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <shellapi.h>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kHudSectionName = "hud";
constexpr int kHudSchemaVersion = 3;
constexpr int kPreviousHudSchemaVersion = 2;
constexpr int kLegacyHudSchemaVersion = 1;
constexpr wchar_t kHudAssetsFolder[] = L"hud";
constexpr wchar_t kHudImagesFolder[] = L"images";
constexpr wchar_t kHudExportFolder[] = L"export";
constexpr wchar_t kHudBackupFolder[] = L"backup";
constexpr wchar_t kHudMigrationBackupFile[] = L"hud-before-v3.json";
constexpr int kDefaultRefreshMs = 200;
constexpr int kUndoLimit = 50;
constexpr std::uint64_t kHudTextSaveDebounceMs = 300;
constexpr float kVirtualWidth = 1920.0f;
constexpr float kVirtualHeight = 1080.0f;

double HudPerfNowMs() {
    static const double s_invFrequencyMs = [] {
        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
            return 0.0;
        }
        return 1000.0 / static_cast<double>(frequency.QuadPart);
    }();

    if (s_invFrequencyMs <= 0.0) {
        return static_cast<double>(GetTickCount64());
    }

    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) * s_invFrequencyMs;
}

enum class SourceMode {
    Inline,
    NotepadNote,
};

enum class Anchor {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

enum class ScalePolicy {
    Fixed,
    ScaleWithWidth,
    ScaleWithHeight,
    ScaleUniform,
};

enum class ElementType {
    Text,
    TextMarkup,
    Image,
    Shape,
    Line,
    Icon,
    ProgressBar,
    Group,
};

enum class ImageFit {
    Contain,
    Cover,
    Stretch,
};

enum class TextAlign {
    Left,
    Center,
    Right,
};

enum class HudBottomPanel {
    Widgets,
    Layers,
    Properties,
};

enum class HudPropertiesCategory {
    Content,
    Geometry,
    Appearance,
    Visibility,
};

enum class HudPropertyScope {
    Widget,
    Element,
};

enum class HudAlignAction {
    Left,
    HorizontalCenter,
    Right,
    Top,
    VerticalCenter,
    Bottom,
    DistributeHorizontal,
    DistributeVertical,
};

enum class HudInsertTarget {
    None,
    Text,
    Markup,
    ImagePath,
    ProgressExpression,
};

enum class HudTextMode {
    Plain,
    Markup,
    Notepad,
};

enum class ResizeHandle {
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight,
    Bottom,
    BottomLeft,
    Left,
};

enum class CanvasInteractionMode {
    Idle,
    HoverElement,
    HoverHandle,
    Dragging,
    Resizing,
    MarqueeSelecting,
    Panning,
};

struct HudVisibility {
    std::vector<bool> conditions{};
    ConditionCombineMode conditionsCombine = ConditionCombineMode::RequireAny;
};

struct HudPosition {
    Anchor anchor = Anchor::TopLeft;
    float offsetX = 40.0f;
    float offsetY = 40.0f;
};

struct HudElementStyle {
    ImVec4 fill = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);
    ImVec4 stroke = ImVec4(0.34f, 0.39f, 0.48f, 1.0f);
    ImVec4 text = ImVec4(0.90f, 0.92f, 0.97f, 1.0f);
    ImVec4 shadow = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 outline = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 tint = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    ImVec4 progressFill = ImVec4(0.35f, 0.78f, 1.0f, 1.0f);
    float fillAlpha = 0.72f;
    float strokeAlpha = 0.82f;
    float textAlpha = 1.0f;
    float shadowAlpha = 0.28f;
    float outlineAlpha = 0.82f;
    float tintAlpha = 1.0f;
    float progressFillAlpha = 0.92f;
    float rounding = 6.0f;
    float strokeSize = 1.0f;
    float shadowOffsetX = 4.0f;
    float shadowOffsetY = 5.0f;
    float outlineSize = 1.0f;
    bool fillEnabled = true;
    bool strokeEnabled = false;
    bool shadowEnabled = false;
    bool outlineEnabled = false;
};

struct HudElementData {
    SourceMode sourceMode = SourceMode::Inline;
    std::string text{};
    std::string noteId{};
    std::string imagePath{};
    ImageFit imageFit = ImageFit::Contain;
    std::string icon = "star";
    std::string expression = "0";
    float minValue = 0.0f;
    float maxValue = 100.0f;
    float defaultValue = 0.0f;
    int fontSize = 16;
    TextAlign align = TextAlign::Left;
};

struct HudElement {
    std::string id{};
    ElementType type = ElementType::Text;
    std::string name{};
    std::string parentId{};
    float x = 0.0f;
    float y = 0.0f;
    float width = 160.0f;
    float height = 36.0f;
    int z = 0;
    float opacity = 1.0f;
    bool locked = false;
    bool hidden = false;
    HudVisibility visibility{};
    HudElementStyle style{};
    HudElementData data{};

    std::string cachedText{};
    std::string cachedImagePath{};
    std::string cachedIconName{};
    std::string cachedIconGlyph{};
    std::string cachedStaticSource{};
    ElementType cachedStaticType = ElementType::Text;
    std::uint64_t cachedCoarseClockUntilMs = 0;
    float cachedNumber = 0.0f;
    bool cachedStaticValueReady = false;
    bool noteMissing = false;
};

struct HudWidget {
    std::string id{};
    std::string name{};
    bool enabled = true;
    HudPosition position{};
    float canvasWidth = 320.0f;
    float canvasHeight = 140.0f;
    ScalePolicy scalePolicy = ScalePolicy::ScaleUniform;
    HudVisibility visibility{};
    int refreshMs = kDefaultRefreshMs;
    std::vector<HudElement> elements{};

    std::uint64_t nextRefreshAtMs = 0;
    std::vector<HudElement*> elementsByZAscCache{};
    std::vector<HudElement*> elementsByZDescCache{};
    bool elementsByZAscDirty = true;
    bool elementsByZDescDirty = true;
};

struct CanvasElementSnapshot {
    std::string id{};
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct CanvasHitResult {
    std::string elementId{};
    std::optional<ResizeHandle> handle{};

    bool HasElement() const {
        return !elementId.empty();
    }

    bool HasHandle() const {
        return handle.has_value() && HasElement();
    }
};

struct HudEditorVisualStyle {
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
    ImVec4 danger{};
    ImVec4 toolbarBg{};
    ImVec4 canvasBg{};
    ImVec4 canvasBorder{};
    ImVec4 gridMajor{};
    ImVec4 gridMinor{};
    ImVec4 buttonBg{};
    ImVec4 buttonHover{};
    ImVec4 buttonActive{};
};

struct ImGuiStringUserData {
    std::string* value = nullptr;
    int* cursorPos = nullptr;
    bool* cursorValid = nullptr;
};

std::uint64_t TickNow() {
    return static_cast<std::uint64_t>(GetTickCount64());
}

float ScaleUi(float value) {
    return UiSettings::Instance().Scale(value);
}

ImVec2 ScaleUi(float x, float y) {
    return UiSettings::Instance().Scale(ImVec2(x, y));
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

std::string LowerAscii(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

std::string LowerUtf8(std::string_view value) {
    std::wstring wide = MarkupRenderer::Utf8ToWide(value);
    if (wide.empty()) {
        return LowerAscii(value);
    }
    ::CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));
    return MarkupRenderer::WideToUtf8(wide);
}

bool ContainsTokenWithBoundary(std::string_view text, std::string_view needle) {
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string_view::npos) {
        const std::size_t end = pos + needle.size();
        if (end >= text.size()) {
            return true;
        }
        const unsigned char next = static_cast<unsigned char>(text[end]);
        if (std::isalnum(next) == 0 && next != '_') {
            return true;
        }
        pos = end;
    }
    return false;
}

bool ContainsHudActionTag(std::string_view text) {
    const std::string lowered = LowerAscii(text);
    constexpr std::string_view kSimpleActions[] = {
        "bindstopall",
        "screen",
        "tphoto",
        "dialogwaitopen",
        "dialogwaitclose",
    };
    constexpr std::string_view kFunctionActions[] = {
        "keyemulate",
        "keydown",
        "screen",
        "wait",
        "dialogclose",
        "dialogsettext",
        "dialogitem",
        "arzdialogitem",
        "dialogselect",
        "dialogwaitid",
        "dialogresponse",
        "save_dialog",
        "binddisable",
        "bindenable",
        "bindstart",
        "bindstop",
        "bindpause",
        "bindunpause",
        "bindfastmenu",
        "bindunfastmenu",
        "bindrandom",
        "bindended",
        "bindpopup",
    };

    for (std::string_view name : kSimpleActions) {
        const std::string simpleCurly = "{" + std::string(name) + "}";
        if (lowered.find(simpleCurly) != std::string::npos) {
            return true;
        }
    }
    for (std::string_view name : kFunctionActions) {
        const std::string bracketPrefix = "[" + std::string(name);
        if (ContainsTokenWithBoundary(lowered, bracketPrefix)) {
            return true;
        }
    }
    return false;
}

bool MightContainHudTag(std::string_view text) {
    return text.find('{') != std::string_view::npos || text.find('[') != std::string_view::npos;
}

enum class CoarseClockRefresh {
    None,
    Second,
    Minute,
    Day,
    Dynamic,
};

CoarseClockRefresh MergeCoarseRefresh(CoarseClockRefresh current, CoarseClockRefresh next) {
    if (current == CoarseClockRefresh::Dynamic || next == CoarseClockRefresh::Dynamic) {
        return CoarseClockRefresh::Dynamic;
    }
    if (current == CoarseClockRefresh::None) {
        return next;
    }
    if (next == CoarseClockRefresh::None || current == next) {
        return current;
    }
    if (current == CoarseClockRefresh::Second || next == CoarseClockRefresh::Second) {
        return CoarseClockRefresh::Second;
    }
    if (current == CoarseClockRefresh::Minute || next == CoarseClockRefresh::Minute) {
        return CoarseClockRefresh::Minute;
    }
    return CoarseClockRefresh::Day;
}

CoarseClockRefresh ClassifyCoarseClockHudTags(std::string_view text) {
    if (text.find('[') != std::string_view::npos) {
        return CoarseClockRefresh::Dynamic;
    }

    CoarseClockRefresh refresh = CoarseClockRefresh::None;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t start = text.find('{', pos);
        if (start == std::string_view::npos) {
            break;
        }
        const std::size_t end = text.find('}', start + 1);
        if (end == std::string_view::npos) {
            return CoarseClockRefresh::Dynamic;
        }

        const std::string name = LowerAscii(text.substr(start + 1, end - start - 1));
        CoarseClockRefresh tokenRefresh = CoarseClockRefresh::Dynamic;
        if (name == "time") {
            tokenRefresh = CoarseClockRefresh::Second;
        } else if (name == "timenosec") {
            tokenRefresh = CoarseClockRefresh::Minute;
        } else if (name == "date") {
            tokenRefresh = CoarseClockRefresh::Day;
        }

        refresh = MergeCoarseRefresh(refresh, tokenRefresh);
        if (refresh == CoarseClockRefresh::Dynamic) {
            return refresh;
        }
        pos = end + 1;
    }

    return refresh;
}

std::uint64_t CoarseClockRefreshDelayMs(CoarseClockRefresh refresh) {
    SYSTEMTIME localTime{};
    GetLocalTime(&localTime);
    switch (refresh) {
    case CoarseClockRefresh::Second:
        return static_cast<std::uint64_t>(std::max(1, 1000 - static_cast<int>(localTime.wMilliseconds)));
    case CoarseClockRefresh::Minute:
        return static_cast<std::uint64_t>(
            std::max(1, (59 - static_cast<int>(localTime.wSecond)) * 1000 + 1000 - static_cast<int>(localTime.wMilliseconds)));
    case CoarseClockRefresh::Day:
        return static_cast<std::uint64_t>(
            std::max(1, (23 - static_cast<int>(localTime.wHour)) * 60 * 60 * 1000
                    + (59 - static_cast<int>(localTime.wMinute)) * 60 * 1000
                    + (59 - static_cast<int>(localTime.wSecond)) * 1000
                    + 1000 - static_cast<int>(localTime.wMilliseconds)));
    case CoarseClockRefresh::None:
    case CoarseClockRefresh::Dynamic:
    default:
        return 0;
    }
}

int ImGuiStringResizeCallback(ImGuiInputTextCallbackData* data) {
    auto* userData = static_cast<ImGuiStringUserData*>(data->UserData);
    if (!userData || !userData->value) {
        return 0;
    }
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        userData->value->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = userData->value->data();
    }
    if (userData->cursorPos) {
        *userData->cursorPos = data->CursorPos;
        if (userData->cursorValid) {
            *userData->cursorValid = true;
        }
    }
    return 0;
}

bool InputTextString(
    const char* label,
    std::string& value,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 128) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }
    ImGuiStringUserData userData{ &value };
    return ImGui::InputText(
        label,
        value.data(),
        value.capacity() + 1,
        flags | ImGuiInputTextFlags_CallbackResize,
        ImGuiStringResizeCallback,
        &userData);
}

bool InputTextWithHintString(
    const char* label,
    const char* hint,
    std::string& value,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 128) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }
    ImGuiStringUserData userData{ &value };
    return ImGui::InputTextWithHint(
        label,
        hint,
        value.data(),
        value.capacity() + 1,
        flags | ImGuiInputTextFlags_CallbackResize,
        ImGuiStringResizeCallback,
        &userData);
}

bool InputTextMultilineString(
    const char* label,
    std::string& value,
    const ImVec2& size,
    ImGuiInputTextFlags flags = 0,
    int* cursorPos = nullptr,
    bool* cursorValid = nullptr) {
    if (value.capacity() < 4096) {
        value.reserve(4096);
    }
    ImGuiStringUserData userData{ &value, cursorPos, cursorValid };
    if (cursorPos) {
        flags |= ImGuiInputTextFlags_CallbackAlways;
    }
    return ImGui::InputTextMultiline(
        label,
        value.data(),
        value.capacity() + 1,
        size,
        flags | ImGuiInputTextFlags_CallbackResize,
        ImGuiStringResizeCallback,
        &userData);
}

std::string PathToUtf8(const fs::path& path) {
    return MarkupRenderer::WideToUtf8(path.wstring());
}

std::wstring BuildDialogFilter(std::initializer_list<std::pair<UiText, const wchar_t*>> entries) {
    std::wstring filter;
    for (const auto& [labelId, pattern] : entries) {
        filter += MarkupRenderer::Utf8ToWide(UiSettings::Instance().Text(labelId));
        filter.push_back(L'\0');
        filter += pattern;
        filter.push_back(L'\0');
    }
    filter.push_back(L'\0');
    return filter;
}

std::string SanitizeFileStem(std::string_view value, std::string_view fallback) {
    std::wstring wide = MarkupRenderer::Utf8ToWide(value);
    std::wstring output;
    output.reserve(wide.size());
    for (wchar_t ch : wide) {
        const bool invalid = ch < 32
            || ch == L'\\'
            || ch == L'/'
            || ch == L':'
            || ch == L'*'
            || ch == L'?'
            || ch == L'"'
            || ch == L'<'
            || ch == L'>'
            || ch == L'|';
        output.push_back(invalid ? L'_' : ch);
    }
    while (!output.empty() && (output.back() == L'.' || output.back() == L' ')) {
        output.pop_back();
    }
    if (output.empty()) {
        return std::string(fallback);
    }
    return MarkupRenderer::WideToUtf8(output);
}

ImVec4 WithAlpha(ImVec4 color, float alpha) {
    color.w *= std::clamp(alpha, 0.0f, 1.0f);
    return color;
}

ImVec4 BlendColor(const ImVec4& a, const ImVec4& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t);
}

ImU32 ColorU32(ImVec4 color, float alphaMultiplier = 1.0f) {
    color.w *= std::clamp(alphaMultiplier, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(color);
}

HudEditorVisualStyle HudEditorStyleTokens() {
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

    HudEditorVisualStyle style;
    style.panelBg = WithAlpha(BlendColor(childBg, windowBg, 0.16f), childBg.w);
    style.panelBorder = WithAlpha(border, 0.40f);
    style.headerText = text;
    style.mutedText = WithAlpha(BlendColor(textDisabled, text, 0.30f), 0.92f);
    style.faintText = WithAlpha(textDisabled, 0.78f);
    style.rowHover = WithAlpha(headerHovered, 0.20f);
    style.rowSelected = WithAlpha(headerActive, 0.30f);
    style.rowSelectedHover = WithAlpha(headerActive, 0.38f);
    style.rowAlt = WithAlpha(text, 0.026f);
    style.separator = WithAlpha(border, 0.18f);
    style.accent = WithAlpha(buttonActive, 0.96f);
    style.danger = WithAlpha(BlendColor(text, buttonHovered, 0.24f), 0.94f);
    style.toolbarBg = WithAlpha(BlendColor(childBg, windowBg, 0.08f), 0.98f);
    style.canvasBg = WithAlpha(BlendColor(windowBg, frameBg, 0.22f), 0.98f);
    style.canvasBorder = WithAlpha(border, 0.26f);
    style.gridMajor = WithAlpha(BlendColor(buttonActive, text, 0.10f), 0.22f);
    style.gridMinor = WithAlpha(BlendColor(textDisabled, frameBg, 0.35f), 0.18f);
    style.buttonBg = WithAlpha(button, 0.94f);
    style.buttonHover = buttonHovered;
    style.buttonActive = buttonActive;
    return style;
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

void DrawCenteredIconGlyph(ImDrawList* drawList, const char* icon, const ImRect& rect, ImU32 color, float preferredFontSize = 0.0f) {
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

void DrawHudPanelBackground(const ImVec2& size, const HudEditorVisualStyle& visual) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImRect rect(pos, ImVec2(pos.x + std::max(1.0f, size.x), pos.y + std::max(1.0f, size.y)));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float rounding = ScaleUi(7.0f);
    drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(visual.panelBg), rounding);
    drawList->AddRect(rect.Min, rect.Max, ImGui::GetColorU32(visual.panelBorder), rounding, 0, ScaleUi(1.0f));
}

bool BeginHudPanel(const char* id, const ImVec2& size, const HudEditorVisualStyle& visual, ImGuiWindowFlags flags = 0) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 visualSize = size;
    if (visualSize.x == 0.0f) {
        visualSize.x = avail.x;
    }
    if (visualSize.y == 0.0f) {
        visualSize.y = avail.y;
    }
    DrawHudPanelBackground(visualSize, visual);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(8.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    return ImGui::BeginChild(id, size, ImGuiChildFlags_None, flags | ImGuiWindowFlags_NoBackground);
}

void EndHudPanel() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void DrawHudRowBackground(ImDrawList* drawList, const ImRect& rowRect, int rowIndex, bool selected, bool hovered, const HudEditorVisualStyle& visual) {
    const ImVec4 bg = selected
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

bool HudFlatIconButton(
    const char* icon,
    const char* id,
    const char* tooltip,
    const ImVec2& size,
    const HudEditorVisualStyle& visual,
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

float HudTextActionButtonWidth(const char* icon, const char* label) {
    const float iconReserve = icon && icon[0] != '\0' ? ScaleUi(20.0f) : 0.0f;
    const float iconGap = iconReserve > 0.0f ? ScaleUi(5.0f) : 0.0f;
    return std::ceil(ScaleUi(10.0f) + iconReserve + iconGap + ImGui::CalcTextSize(label ? label : "").x + ScaleUi(10.0f));
}

bool HudTextActionButton(
    const char* icon,
    const char* label,
    const char* id,
    const char* tooltip,
    const ImVec2& size,
    const HudEditorVisualStyle& visual,
    bool enabled = true,
    bool primary = false) {
    const bool clickedRaw = ImGui::InvisibleButton(id, size);
    const bool clicked = enabled && clickedRaw;
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const bool held = enabled && ImGui::IsItemActive();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImVec4 bg = primary ? WithAlpha(visual.accent, 0.86f) : visual.buttonBg;
    if (!enabled) {
        bg = WithAlpha(visual.buttonBg, 0.38f);
    } else if (held) {
        bg = visual.buttonActive;
    } else if (hovered) {
        bg = visual.buttonHover;
    }
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(bg), ScaleUi(5.0f));
    drawList->AddRect(min, max, ImGui::GetColorU32(WithAlpha(visual.panelBorder, primary ? 0.42f : 0.26f)), ScaleUi(5.0f), 0, ScaleUi(1.0f));

    const ImVec4 textColor = enabled ? visual.headerText : visual.faintText;
    const float padX = ScaleUi(10.0f);
    const float iconW = icon && icon[0] != '\0' ? ScaleUi(20.0f) : 0.0f;
    const float iconGap = iconW > 0.0f ? ScaleUi(5.0f) : 0.0f;
    float x = min.x + padX;
    if (iconW > 0.0f) {
        DrawCenteredIconGlyph(
            drawList,
            icon,
            ImRect(ImVec2(x, min.y), ImVec2(x + iconW, max.y)),
            ImGui::GetColorU32(textColor),
            std::floor(ImGui::GetFontSize() * 0.90f));
        x += iconW + iconGap;
    }

    const char* text = label ? label : "";
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const float textY = min.y + std::floor(std::max(0.0f, (size.y - textSize.y) * 0.5f));
    const ImVec4 clip(min.x, min.y, max.x, max.y);
    drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, textY), ImGui::GetColorU32(textColor), text, nullptr, 0.0f, &clip);

    if (tooltip && tooltip[0] != '\0' && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked;
}

bool DrawHudSearchBox(const char* id, const char* hint, std::string& value, const HudEditorVisualStyle& visual) {
    const float gap = ScaleUi(4.0f);
    const float clearSide = ImGui::GetFrameHeight();
    const bool hasClear = !value.empty();
    const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float inputWidth = hasClear ? std::max(ScaleUi(48.0f), availableWidth - clearSide - gap) : availableWidth;
    const std::string searchHint = std::string(ui_icons::Search) + " " + (hint ? hint : "");

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ScaleUi(6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, ScaleUi(1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ScaleUi(8.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, visual.buttonBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, visual.buttonHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, visual.buttonActive);
    ImGui::PushStyleColor(ImGuiCol_Border, WithAlpha(visual.panelBorder, 0.24f));
    ImGui::SetNextItemWidth(inputWidth);
    bool changed = InputTextWithHintString(id, searchHint.c_str(), value, ImGuiInputTextFlags_AutoSelectAll, 128);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);

    if (hasClear) {
        ImGui::SameLine(0.0f, gap);
        const std::string clearId = std::string(id ? id : "##search") + "_clear";
        if (HudFlatIconButton(ui_icons::Xmark, clearId.c_str(), UiSettings::Instance().Text(UiText::BinderClearSearch), ImVec2(clearSide, clearSide), visual)) {
            value.clear();
            changed = true;
        }
    }
    return changed;
}

std::string ColorToHex(const ImVec4& color) {
    const int r = std::clamp(static_cast<int>(std::lround(color.x * 255.0f)), 0, 255);
    const int g = std::clamp(static_cast<int>(std::lround(color.y * 255.0f)), 0, 255);
    const int b = std::clamp(static_cast<int>(std::lround(color.z * 255.0f)), 0, 255);
    char buffer[8]{};
    std::snprintf(buffer, sizeof(buffer), "%02X%02X%02X", r, g, b);
    return buffer;
}

ImVec4 HexToColor(std::string_view value, const ImVec4& fallback) {
    if (value.size() != 6) {
        return fallback;
    }
    auto hex = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    int parts[6]{};
    for (std::size_t i = 0; i < value.size(); ++i) {
        parts[i] = hex(value[i]);
        if (parts[i] < 0) {
            return fallback;
        }
    }
    return ImVec4(
        (parts[0] * 16 + parts[1]) / 255.0f,
        (parts[2] * 16 + parts[3]) / 255.0f,
        (parts[4] * 16 + parts[5]) / 255.0f,
        1.0f);
}

jsonutil::JsonValue SerializeBoolArray(const std::vector<bool>& flags) {
    jsonutil::JsonArray array;
    array.reserve(flags.size());
    for (bool flag : flags) {
        array.emplace_back(flag);
    }
    return jsonutil::JsonValue(std::move(array));
}

std::vector<bool> DeserializeBoolArray(const jsonutil::JsonArray* array) {
    std::vector<bool> flags;
    if (!array) {
        return flags;
    }
    flags.reserve(array->size());
    for (const jsonutil::JsonValue& value : *array) {
        const bool* flag = value.TryBool();
        flags.push_back(flag ? *flag : false);
    }
    return flags;
}

bool ClearRawConditionFlag(std::vector<bool>& flags, ConditionId condition) {
    const std::size_t index = static_cast<std::size_t>(condition);
    if (index >= flags.size() || !flags[index]) {
        return false;
    }

    flags[index] = false;
    return true;
}

jsonutil::JsonObject SerializeVisibility(const HudVisibility& visibility) {
    jsonutil::JsonObject object;
    std::vector<bool> conditions = visibility.conditions;
    NormalizeConditionFlags(conditions);
    object["conditions"] = SerializeBoolArray(conditions);
    object["conditions_combine"] = ConditionCombineModeId(visibility.conditionsCombine);
    return object;
}

bool DeserializeVisibility(const jsonutil::JsonObject* object, HudVisibility& visibility, bool& migratedHelperCondition) {
    if (!object) {
        NormalizeConditionFlags(visibility.conditions);
        return false;
    }

    bool migrated = false;
    if (const jsonutil::JsonArray* conditionsArray = jsonutil::JsonArrayOrNull(object, "conditions")) {
        visibility.conditions = DeserializeBoolArray(conditionsArray);
    }
    if (ClearRawConditionFlag(visibility.conditions, ConditionId::HelperActive)) {
        migrated = true;
        migratedHelperCondition = true;
    }
    NormalizeConditionFlags(visibility.conditions);
    visibility.conditionsCombine =
        NormalizeConditionCombineMode(jsonutil::JsonStringOr(object, "conditions_combine", "require_any"));

    const auto migrateLegacyVisibilityFlag = [&](const char* key, ConditionId condition) {
        const auto it = object->find(key);
        if (it == object->end()) {
            return;
        }
        if (const bool* value = it->second.TryBool()) {
            SetConditionFlag(visibility.conditions, condition, *value);
            migrated = true;
        }
    };
    if (object->find("hide_when_helper_open") != object->end()) {
        migrated = true;
        migratedHelperCondition = true;
    }
    migrateLegacyVisibilityFlag("hide_when_chat_open", ConditionId::ChatOpened);
    migrateLegacyVisibilityFlag("hide_when_dialog_open", ConditionId::DialogOpened);
    return migrated;
}

std::string AnchorToString(Anchor anchor) {
    switch (anchor) {
    case Anchor::TopLeft: return "top_left";
    case Anchor::TopCenter: return "top_center";
    case Anchor::TopRight: return "top_right";
    case Anchor::CenterLeft: return "center_left";
    case Anchor::Center: return "center";
    case Anchor::CenterRight: return "center_right";
    case Anchor::BottomLeft: return "bottom_left";
    case Anchor::BottomCenter: return "bottom_center";
    case Anchor::BottomRight: return "bottom_right";
    }
    return "top_left";
}

Anchor AnchorFromString(std::string_view value) {
    const std::string lowered = LowerAscii(value);
    if (lowered == "top_center") return Anchor::TopCenter;
    if (lowered == "top_right") return Anchor::TopRight;
    if (lowered == "center_left") return Anchor::CenterLeft;
    if (lowered == "center") return Anchor::Center;
    if (lowered == "center_right") return Anchor::CenterRight;
    if (lowered == "bottom_left") return Anchor::BottomLeft;
    if (lowered == "bottom_center") return Anchor::BottomCenter;
    if (lowered == "bottom_right") return Anchor::BottomRight;
    return Anchor::TopLeft;
}

const char* AnchorLabel(Anchor anchor, UiSettings& ui) {
    switch (anchor) {
    case Anchor::TopLeft: return ui.Text(UiText::HudAnchorTopLeft);
    case Anchor::TopCenter: return ui.Text(UiText::HudAnchorTopCenter);
    case Anchor::TopRight: return ui.Text(UiText::HudAnchorTopRight);
    case Anchor::CenterLeft: return ui.Text(UiText::HudAnchorCenterLeft);
    case Anchor::Center: return ui.Text(UiText::HudAnchorCenter);
    case Anchor::CenterRight: return ui.Text(UiText::HudAnchorCenterRight);
    case Anchor::BottomLeft: return ui.Text(UiText::HudAnchorBottomLeft);
    case Anchor::BottomCenter: return ui.Text(UiText::HudAnchorBottomCenter);
    case Anchor::BottomRight: return ui.Text(UiText::HudAnchorBottomRight);
    }
    return ui.Text(UiText::HudAnchorTopLeft);
}

std::string SourceModeToString(SourceMode mode) {
    return mode == SourceMode::NotepadNote ? "notepad_note" : "inline";
}

SourceMode SourceModeFromString(std::string_view value) {
    return LowerAscii(value) == "notepad_note" ? SourceMode::NotepadNote : SourceMode::Inline;
}

std::string ScalePolicyToString(ScalePolicy policy) {
    switch (policy) {
    case ScalePolicy::ScaleWithWidth: return "scale_with_width";
    case ScalePolicy::ScaleWithHeight: return "scale_with_height";
    case ScalePolicy::ScaleUniform: return "scale_uniform";
    case ScalePolicy::Fixed:
    default:
        return "fixed";
    }
}

ScalePolicy ScalePolicyFromString(std::string_view value) {
    const std::string lowered = LowerAscii(value);
    if (lowered == "scale_with_width") return ScalePolicy::ScaleWithWidth;
    if (lowered == "scale_with_height") return ScalePolicy::ScaleWithHeight;
    if (lowered == "scale_uniform") return ScalePolicy::ScaleUniform;
    return ScalePolicy::Fixed;
}

std::string ElementTypeToString(ElementType type) {
    switch (type) {
    case ElementType::Text: return "text";
    case ElementType::TextMarkup: return "text_markup";
    case ElementType::Image: return "image";
    case ElementType::Shape: return "shape";
    case ElementType::Line: return "line";
    case ElementType::Icon: return "icon";
    case ElementType::ProgressBar: return "progress_bar";
    case ElementType::Group: return "group";
    }
    return "text";
}

ElementType ElementTypeFromString(std::string_view value) {
    const std::string lowered = LowerAscii(value);
    if (lowered == "text_markup" || lowered == "markup") return ElementType::TextMarkup;
    if (lowered == "image") return ElementType::Image;
    if (lowered == "shape") return ElementType::Shape;
    if (lowered == "line") return ElementType::Line;
    if (lowered == "icon") return ElementType::Icon;
    if (lowered == "progress_bar" || lowered == "progress") return ElementType::ProgressBar;
    if (lowered == "group") return ElementType::Group;
    return ElementType::Text;
}

UiText ElementTypeLabelId(ElementType type) {
    switch (type) {
    case ElementType::Text: return UiText::HudElementText;
    case ElementType::TextMarkup: return UiText::HudElementMarkup;
    case ElementType::Image: return UiText::HudElementImage;
    case ElementType::Shape: return UiText::HudElementShape;
    case ElementType::Line: return UiText::HudElementLine;
    case ElementType::Icon: return UiText::HudElementIcon;
    case ElementType::ProgressBar: return UiText::HudElementProgress;
    case ElementType::Group: return UiText::HudElementGroup;
    }
    return UiText::HudElementText;
}

std::string ImageFitToString(ImageFit fit) {
    switch (fit) {
    case ImageFit::Cover: return "cover";
    case ImageFit::Stretch: return "stretch";
    case ImageFit::Contain:
    default:
        return "contain";
    }
}

ImageFit ImageFitFromString(std::string_view value) {
    const std::string lowered = LowerAscii(value);
    if (lowered == "cover") return ImageFit::Cover;
    if (lowered == "stretch") return ImageFit::Stretch;
    return ImageFit::Contain;
}

std::string TextAlignToString(TextAlign align) {
    switch (align) {
    case TextAlign::Center: return "center";
    case TextAlign::Right: return "right";
    case TextAlign::Left:
    default:
        return "left";
    }
}

TextAlign TextAlignFromString(std::string_view value) {
    const std::string lowered = LowerAscii(value);
    if (lowered == "center") return TextAlign::Center;
    if (lowered == "right") return TextAlign::Right;
    return TextAlign::Left;
}

ImVec2 AnchorBase(Anchor anchor, const ImVec2& displaySize) {
    switch (anchor) {
    case Anchor::TopLeft: return ImVec2(0.0f, 0.0f);
    case Anchor::TopCenter: return ImVec2(displaySize.x * 0.5f, 0.0f);
    case Anchor::TopRight: return ImVec2(displaySize.x, 0.0f);
    case Anchor::CenterLeft: return ImVec2(0.0f, displaySize.y * 0.5f);
    case Anchor::Center: return ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f);
    case Anchor::CenterRight: return ImVec2(displaySize.x, displaySize.y * 0.5f);
    case Anchor::BottomLeft: return ImVec2(0.0f, displaySize.y);
    case Anchor::BottomCenter: return ImVec2(displaySize.x * 0.5f, displaySize.y);
    case Anchor::BottomRight: return ImVec2(displaySize.x, displaySize.y);
    }
    return ImVec2(0.0f, 0.0f);
}

ImVec2 AnchorPivot(Anchor anchor) {
    switch (anchor) {
    case Anchor::TopLeft: return ImVec2(0.0f, 0.0f);
    case Anchor::TopCenter: return ImVec2(0.5f, 0.0f);
    case Anchor::TopRight: return ImVec2(1.0f, 0.0f);
    case Anchor::CenterLeft: return ImVec2(0.0f, 0.5f);
    case Anchor::Center: return ImVec2(0.5f, 0.5f);
    case Anchor::CenterRight: return ImVec2(1.0f, 0.5f);
    case Anchor::BottomLeft: return ImVec2(0.0f, 1.0f);
    case Anchor::BottomCenter: return ImVec2(0.5f, 1.0f);
    case Anchor::BottomRight: return ImVec2(1.0f, 1.0f);
    }
    return ImVec2(0.0f, 0.0f);
}

jsonutil::JsonObject SerializeStyle(const HudElementStyle& style) {
    jsonutil::JsonObject object;
    object["fill_color"] = ColorToHex(style.fill);
    object["fill_alpha"] = style.fillAlpha;
    object["fill"] = style.fillEnabled;
    object["stroke_color"] = ColorToHex(style.stroke);
    object["stroke_alpha"] = style.strokeAlpha;
    object["stroke"] = style.strokeEnabled;
    object["stroke_size"] = style.strokeSize;
    object["text_color"] = ColorToHex(style.text);
    object["text_alpha"] = style.textAlpha;
    object["shadow"] = style.shadowEnabled;
    object["shadow_color"] = ColorToHex(style.shadow);
    object["shadow_alpha"] = style.shadowAlpha;
    object["shadow_offset_x"] = style.shadowOffsetX;
    object["shadow_offset_y"] = style.shadowOffsetY;
    object["outline"] = style.outlineEnabled;
    object["outline_color"] = ColorToHex(style.outline);
    object["outline_alpha"] = style.outlineAlpha;
    object["outline_size"] = style.outlineSize;
    object["tint_color"] = ColorToHex(style.tint);
    object["tint_alpha"] = style.tintAlpha;
    object["progress_fill_color"] = ColorToHex(style.progressFill);
    object["progress_fill_alpha"] = style.progressFillAlpha;
    object["rounding"] = style.rounding;
    return object;
}

HudElementStyle DeserializeStyle(const jsonutil::JsonObject* object, const HudElementStyle& fallback = HudElementStyle{}) {
    HudElementStyle style = fallback;
    if (!object) {
        return style;
    }

    style.fill = HexToColor(jsonutil::JsonStringOr(object, "fill_color", ColorToHex(style.fill)), style.fill);
    style.fillAlpha = std::clamp(jsonutil::JsonNumberOr(object, "fill_alpha", style.fillAlpha), 0.0f, 1.0f);
    style.fillEnabled = jsonutil::JsonBoolOr(object, "fill", style.fillEnabled);
    style.stroke = HexToColor(jsonutil::JsonStringOr(object, "stroke_color", ColorToHex(style.stroke)), style.stroke);
    style.strokeAlpha = std::clamp(jsonutil::JsonNumberOr(object, "stroke_alpha", style.strokeAlpha), 0.0f, 1.0f);
    style.strokeEnabled = jsonutil::JsonBoolOr(object, "stroke", style.strokeEnabled);
    style.strokeSize = std::clamp(jsonutil::JsonNumberOr(object, "stroke_size", style.strokeSize), 0.0f, 32.0f);
    style.text = HexToColor(jsonutil::JsonStringOr(object, "text_color", ColorToHex(style.text)), style.text);
    style.textAlpha = std::clamp(jsonutil::JsonNumberOr(object, "text_alpha", style.textAlpha), 0.0f, 1.0f);
    style.shadowEnabled = jsonutil::JsonBoolOr(object, "shadow", style.shadowEnabled);
    style.shadow = HexToColor(jsonutil::JsonStringOr(object, "shadow_color", ColorToHex(style.shadow)), style.shadow);
    style.shadowAlpha = std::clamp(jsonutil::JsonNumberOr(object, "shadow_alpha", style.shadowAlpha), 0.0f, 1.0f);
    style.shadowOffsetX = std::clamp(jsonutil::JsonNumberOr(object, "shadow_offset_x", style.shadowOffsetX), -120.0f, 120.0f);
    style.shadowOffsetY = std::clamp(jsonutil::JsonNumberOr(object, "shadow_offset_y", style.shadowOffsetY), -120.0f, 120.0f);
    style.outlineEnabled = jsonutil::JsonBoolOr(object, "outline", style.outlineEnabled);
    style.outline = HexToColor(jsonutil::JsonStringOr(object, "outline_color", ColorToHex(style.outline)), style.outline);
    style.outlineAlpha = std::clamp(jsonutil::JsonNumberOr(object, "outline_alpha", style.outlineAlpha), 0.0f, 1.0f);
    style.outlineSize = std::clamp(jsonutil::JsonNumberOr(object, "outline_size", style.outlineSize), 0.0f, 12.0f);
    style.tint = HexToColor(jsonutil::JsonStringOr(object, "tint_color", ColorToHex(style.tint)), style.tint);
    style.tintAlpha = std::clamp(jsonutil::JsonNumberOr(object, "tint_alpha", style.tintAlpha), 0.0f, 1.0f);
    style.progressFill = HexToColor(jsonutil::JsonStringOr(object, "progress_fill_color", ColorToHex(style.progressFill)), style.progressFill);
    style.progressFillAlpha = std::clamp(jsonutil::JsonNumberOr(object, "progress_fill_alpha", style.progressFillAlpha), 0.0f, 1.0f);
    style.rounding = std::clamp(jsonutil::JsonNumberOr(object, "rounding", style.rounding), 0.0f, 80.0f);
    return style;
}

jsonutil::JsonObject SerializeData(const HudElementData& data) {
    jsonutil::JsonObject object;
    object["source_mode"] = SourceModeToString(data.sourceMode);
    object["text"] = data.text;
    object["note_id"] = data.noteId;
    object["image_path"] = data.imagePath;
    object["image_fit"] = ImageFitToString(data.imageFit);
    object["icon"] = data.icon;
    object["expression"] = data.expression;
    object["min"] = data.minValue;
    object["max"] = data.maxValue;
    object["default"] = data.defaultValue;
    object["font_size"] = data.fontSize;
    object["align"] = TextAlignToString(data.align);
    return object;
}

HudElementData DeserializeData(const jsonutil::JsonObject* object) {
    HudElementData data;
    if (!object) {
        return data;
    }
    data.sourceMode = SourceModeFromString(jsonutil::JsonStringOr(object, "source_mode", SourceModeToString(data.sourceMode)));
    data.text = jsonutil::JsonStringOr(object, "text", data.text);
    data.noteId = jsonutil::JsonStringOr(object, "note_id", data.noteId);
    data.imagePath = jsonutil::JsonStringOr(object, "image_path", data.imagePath);
    data.imageFit = ImageFitFromString(jsonutil::JsonStringOr(object, "image_fit", ImageFitToString(data.imageFit)));
    data.icon = icon_registry::NormalizeIconId(jsonutil::JsonStringOr(object, "icon", data.icon));
    if (data.icon.empty()) {
        data.icon = "star";
    }
    data.expression = jsonutil::JsonStringOr(object, "expression", data.expression);
    data.minValue = jsonutil::JsonNumberOr(object, "min", data.minValue);
    data.maxValue = jsonutil::JsonNumberOr(object, "max", data.maxValue);
    data.defaultValue = jsonutil::JsonNumberOr(object, "default", data.defaultValue);
    data.fontSize = std::clamp(jsonutil::JsonNumberOr(object, "font_size", data.fontSize), 8, 96);
    data.align = TextAlignFromString(jsonutil::JsonStringOr(object, "align", TextAlignToString(data.align)));
    return data;
}

struct ExpressionParser {
    explicit ExpressionParser(std::string_view source) : source(source) {
    }

    bool Parse(float& out) {
        pos = 0;
        const double value = ParseExpression();
        SkipWhitespace();
        if (!ok || pos != source.size() || !std::isfinite(value)) {
            return false;
        }
        out = static_cast<float>(value);
        return true;
    }

    void SkipWhitespace() {
        while (pos < source.size() && std::isspace(static_cast<unsigned char>(source[pos])) != 0) {
            ++pos;
        }
    }

    double ParseExpression() {
        double value = ParseTerm();
        for (;;) {
            SkipWhitespace();
            if (Match('+')) {
                value += ParseTerm();
            } else if (Match('-')) {
                value -= ParseTerm();
            } else {
                return value;
            }
        }
    }

    double ParseTerm() {
        double value = ParseFactor();
        for (;;) {
            SkipWhitespace();
            if (Match('*')) {
                value *= ParseFactor();
            } else if (Match('/')) {
                const double divisor = ParseFactor();
                if (std::abs(divisor) < 0.000001) {
                    ok = false;
                    return 0.0;
                }
                value /= divisor;
            } else {
                return value;
            }
        }
    }

    double ParseFactor() {
        SkipWhitespace();
        if (Match('+')) {
            return ParseFactor();
        }
        if (Match('-')) {
            return -ParseFactor();
        }
        if (Match('(')) {
            const double value = ParseExpression();
            if (!Match(')')) {
                ok = false;
            }
            return value;
        }
        return ParseNumber();
    }

    double ParseNumber() {
        SkipWhitespace();
        const std::size_t begin = pos;
        bool hasDigit = false;
        while (pos < source.size()) {
            const char ch = source[pos];
            if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
                hasDigit = true;
                ++pos;
            } else if (ch == '.') {
                ++pos;
            } else {
                break;
            }
        }
        if (!hasDigit) {
            ok = false;
            return 0.0;
        }
        return std::strtod(std::string(source.substr(begin, pos - begin)).c_str(), nullptr);
    }

    bool Match(char expected) {
        SkipWhitespace();
        if (pos < source.size() && source[pos] == expected) {
            ++pos;
            return true;
        }
        return false;
    }

    std::string_view source;
    std::size_t pos = 0;
    bool ok = true;
};

float EvaluateNumberExpression(std::string_view expression, float fallback) {
    const std::string trimmed = TrimAscii(expression);
    if (trimmed.empty()) {
        return fallback;
    }

    float value = fallback;
    ExpressionParser parser(trimmed);
    if (parser.Parse(value)) {
        return value;
    }

    char* end = nullptr;
    const double direct = std::strtod(trimmed.c_str(), &end);
    if (end && *end == '\0' && std::isfinite(direct)) {
        return static_cast<float>(direct);
    }
    return fallback;
}

std::string ResolveHudIconGlyph(std::string_view name) {
    if (std::string glyph = icon_registry::ResolveGlyph(name); !glyph.empty()) {
        return glyph;
    }
    return ui_icons::Star;
}

const char* ElementTypeIcon(ElementType type) {
    switch (type) {
    case ElementType::Text:
    case ElementType::TextMarkup:
        return ui_icons::Comment;
    case ElementType::Image:
        return ui_icons::Image;
    case ElementType::Shape:
    case ElementType::Group:
        return ui_icons::Cubes;
    case ElementType::Line:
        return ui_icons::MoveRows;
    case ElementType::Icon:
        return ui_icons::Star;
    case ElementType::ProgressBar:
        return ui_icons::Sliders;
    }
    return ui_icons::Sliders;
}

HudTextMode TextModeForElement(const HudElement& element) {
    if (element.type == ElementType::TextMarkup) {
        return element.data.sourceMode == SourceMode::NotepadNote ? HudTextMode::Notepad : HudTextMode::Markup;
    }
    return HudTextMode::Plain;
}

const char* TextModeLabel(HudTextMode mode, UiSettings& ui) {
    switch (mode) {
    case HudTextMode::Markup:
        return ui.Text(UiText::HudTextModeMarkup);
    case HudTextMode::Notepad:
        return ui.Text(UiText::HudTextModeNotepad);
    case HudTextMode::Plain:
    default:
        return ui.Text(UiText::HudTextModePlain);
    }
}

std::string BottomPanelToString(HudBottomPanel panel) {
    switch (panel) {
    case HudBottomPanel::Layers: return "layers";
    case HudBottomPanel::Properties: return "properties";
    case HudBottomPanel::Widgets:
    default: return "widgets";
    }
}

HudBottomPanel BottomPanelFromString(std::string_view value) {
    const std::string lowered = LowerAscii(value);
    if (lowered == "layers") return HudBottomPanel::Layers;
    if (lowered == "properties") return HudBottomPanel::Properties;
    return HudBottomPanel::Widgets;
}

std::string PropertiesCategoryToString(HudPropertiesCategory category) {
    switch (category) {
    case HudPropertiesCategory::Geometry: return "geometry";
    case HudPropertiesCategory::Appearance: return "appearance";
    case HudPropertiesCategory::Visibility: return "visibility";
    case HudPropertiesCategory::Content:
    default: return "content";
    }
}

HudPropertiesCategory PropertiesCategoryFromString(std::string_view value) {
    const std::string lowered = LowerAscii(value);
    if (lowered == "geometry") return HudPropertiesCategory::Geometry;
    if (lowered == "appearance") return HudPropertiesCategory::Appearance;
    if (lowered == "visibility") return HudPropertiesCategory::Visibility;
    return HudPropertiesCategory::Content;
}

} // namespace

struct HudModule::Impl {
    using DocumentSnapshot = jsonutil::JsonObject;

    HMODULE module = nullptr;
    TagsModule* tagsModule = nullptr;
    NotepadModule* notepadModule = nullptr;
    SampApi* sampApi = nullptr;
    bool configLoaded = false;
    std::vector<HudWidget> widgets;
    std::string selectedWidgetId;
    std::vector<std::string> selectedElementIds;
    std::string searchQuery;
    std::string statusMessage;
    MarkupRenderer renderer;
    OverlayStats lastOverlayStats{};
    EditorStats lastEditorStats{};
    std::uint64_t idCounter = 0;
    bool placementMode = false;
    std::string placementWidgetId;
    bool placementInputBlocked = false;
    bool placementUndoCaptured = false;
    bool conditionsPopupPending = false;
    bool elementConditionsPopupPending = false;
    bool deprecatedHelperVisibilityMigrated = false;
    bool configMigratedToV2 = false;
    bool configMigratedToV3 = false;
    bool configSaveBlocked = false;
    bool migrationBackupFailed = false;
    bool snapEnabled = true;
    bool guidesEnabled = true;
    float gridSize = 8.0f;
    bool canvasFitZoom = true;
    float canvasZoom = 1.0f;
    HudBottomPanel bottomPanel = HudBottomPanel::Widgets;
    HudPropertiesCategory propertiesCategory = HudPropertiesCategory::Content;
    HudPropertyScope propertyScope = HudPropertyScope::Element;
    float bottomPanelHeight = 250.0f;
    bool bottomPanelCollapsed = false;
    float editorCanvasHeight = 360.0f;
    bool editorCanvasAutoHeight = true;
    std::string layerSearchQuery;
    std::string propertiesSearchQuery;
    bool helpWindowOpen = false;
    bool deleteWidgetConfirmPending = false;
    std::string pendingDeleteWidgetId;
    bool importMissingAssetsConfirmPending = false;
    std::optional<HudWidget> pendingImportWidget;
    std::vector<std::string> pendingImportMissingAssets;
    fs::path pendingImportPath;
    int pendingImportSchema = 0;
    HudInsertTarget activeInsertTarget = HudInsertTarget::None;
    int textInsertCursor = 0;
    int markupInsertCursor = 0;
    bool textInsertCursorValid = false;
    bool markupInsertCursorValid = false;
    std::string textInsertElementId;
    std::string markupInsertElementId;
    bool variablesPopupPending = false;
    variables_picker::State variablesPickerState{};
    std::vector<variables_picker::Entry> variablesPopupEntries;
    icon_picker::State iconPickerState{};
    int helpSection = 0;
    std::vector<DocumentSnapshot> undoStack;
    std::vector<DocumentSnapshot> redoStack;
    DocumentSnapshot lastCommittedSnapshot;
    bool editTransactionActive = false;
    bool editTransactionChanged = false;
    bool textSavePending = false;
    std::uint64_t textSaveDueAtMs = 0;
    std::string inlineEditElementId;
    CanvasInteractionMode canvasMode = CanvasInteractionMode::Idle;
    std::string canvasActiveElementId;
    std::string canvasHoverElementId;
    std::optional<ResizeHandle> canvasActiveHandle{};
    std::optional<ResizeHandle> canvasHoverHandle{};
    ImVec2 canvasGestureStartScreen{};
    ImVec2 canvasGestureStartPan{};
    ImVec2 canvasPan{};
    ImRect canvasMarqueeRect{};
    std::vector<CanvasElementSnapshot> canvasGestureSnapshots;
    bool canvasGestureMoved = false;
    bool canvasGestureUndoCaptured = false;
    bool canvasGestureAdditive = false;
    bool canvasGestureSavePending = false;
    int canvasGestureButton = 0;
    std::optional<float> activeGuideX{};
    std::optional<float> activeGuideY{};
    std::vector<float> guideCandidatesX;
    std::vector<float> guideCandidatesY;
    double lastValidationMs = 0.0;
    double lastSaveTransactionMs = 0.0;
    double lastGuideMs = 0.0;
    double lastBottomPanelMs = 0.0;
    hud_v3::ConditionSnapshot conditionSnapshot;

    void OnProcessAttach(HMODULE moduleHandle) {
        module = moduleHandle;
    }

    void ResetCanvasInteraction(bool resetView = false) {
        canvasMode = CanvasInteractionMode::Idle;
        canvasActiveElementId.clear();
        canvasHoverElementId.clear();
        canvasActiveHandle.reset();
        canvasHoverHandle.reset();
        canvasGestureStartScreen = ImVec2(0.0f, 0.0f);
        canvasGestureStartPan = ImVec2(0.0f, 0.0f);
        canvasMarqueeRect = ImRect();
        canvasGestureSnapshots.clear();
        canvasGestureMoved = false;
        canvasGestureUndoCaptured = false;
        canvasGestureAdditive = false;
        canvasGestureSavePending = false;
        canvasGestureButton = 0;
        activeGuideX.reset();
        activeGuideY.reset();
        guideCandidatesX.clear();
        guideCandidatesY.clear();
        if (resetView) {
            canvasPan = ImVec2(0.0f, 0.0f);
        }
    }

    void Shutdown() {
        FlushPendingSaves();
        ReleaseDeviceResources();
        widgets.clear();
        selectedWidgetId.clear();
        selectedElementIds.clear();
        configLoaded = false;
        placementMode = false;
        placementWidgetId.clear();
        placementUndoCaptured = false;
        canvasFitZoom = true;
        canvasZoom = 1.0f;
        ResetCanvasInteraction(true);
        bottomPanel = HudBottomPanel::Widgets;
        propertiesCategory = HudPropertiesCategory::Content;
        propertyScope = HudPropertyScope::Element;
        bottomPanelHeight = 250.0f;
        bottomPanelCollapsed = false;
        editorCanvasHeight = 360.0f;
        editorCanvasAutoHeight = true;
        layerSearchQuery.clear();
        propertiesSearchQuery.clear();
        helpWindowOpen = false;
        deleteWidgetConfirmPending = false;
        pendingDeleteWidgetId.clear();
        importMissingAssetsConfirmPending = false;
        pendingImportWidget.reset();
        pendingImportMissingAssets.clear();
        pendingImportPath.clear();
        pendingImportSchema = 0;
        activeInsertTarget = HudInsertTarget::None;
        variablesPopupPending = false;
        variablesPopupEntries.clear();
        helpSection = 0;
        variablesPickerState = {};
        iconPickerState = {};
        undoStack.clear();
        redoStack.clear();
        lastCommittedSnapshot.clear();
        editTransactionActive = false;
        editTransactionChanged = false;
        textSavePending = false;
        textSaveDueAtMs = 0;
        deprecatedHelperVisibilityMigrated = false;
        configMigratedToV2 = false;
        configMigratedToV3 = false;
        configSaveBlocked = false;
        migrationBackupFailed = false;
        lastOverlayStats = {};
        lastEditorStats = {};
    }

    void ReloadConfig() {
        ReleaseDeviceResources();
        widgets.clear();
        selectedWidgetId.clear();
        selectedElementIds.clear();
        searchQuery.clear();
        statusMessage.clear();
        configLoaded = false;
        placementMode = false;
        placementWidgetId.clear();
        placementUndoCaptured = false;
        inlineEditElementId.clear();
        canvasFitZoom = true;
        canvasZoom = 1.0f;
        ResetCanvasInteraction(true);
        bottomPanel = HudBottomPanel::Widgets;
        propertiesCategory = HudPropertiesCategory::Content;
        propertyScope = HudPropertyScope::Element;
        bottomPanelHeight = 250.0f;
        bottomPanelCollapsed = false;
        editorCanvasHeight = 360.0f;
        editorCanvasAutoHeight = true;
        layerSearchQuery.clear();
        propertiesSearchQuery.clear();
        helpWindowOpen = false;
        deleteWidgetConfirmPending = false;
        pendingDeleteWidgetId.clear();
        importMissingAssetsConfirmPending = false;
        pendingImportWidget.reset();
        pendingImportMissingAssets.clear();
        pendingImportPath.clear();
        pendingImportSchema = 0;
        activeInsertTarget = HudInsertTarget::None;
        variablesPopupPending = false;
        variablesPopupEntries.clear();
        helpSection = 0;
        variablesPickerState = {};
        iconPickerState = {};
        undoStack.clear();
        redoStack.clear();
        lastCommittedSnapshot.clear();
        editTransactionActive = false;
        editTransactionChanged = false;
        textSavePending = false;
        textSaveDueAtMs = 0;
        deprecatedHelperVisibilityMigrated = false;
        configMigratedToV2 = false;
        configMigratedToV3 = false;
        configSaveBlocked = false;
        migrationBackupFailed = false;
        lastOverlayStats = {};
        lastEditorStats = {};
    }

    void FlushPendingSaves() {
        if (canvasGestureSavePending || textSavePending || (editTransactionActive && editTransactionChanged)) {
            const double beginMs = HudPerfNowMs();
            QueueSave();
            lastCommittedSnapshot = Snapshot();
            lastSaveTransactionMs = HudPerfNowMs() - beginMs;
        }
        canvasGestureSavePending = false;
        textSavePending = false;
        editTransactionActive = false;
        editTransactionChanged = false;
    }

    void ReleaseDeviceResources() {
        renderer.ReleaseDeviceResources();
    }

    void EnsureLoaded() {
        if (configLoaded) {
            return;
        }
        LoadConfig();
        configLoaded = true;
    }

    fs::path ProfileDirectory() const {
        return AppConfig::Instance().ActiveProfileDirectory();
    }

    fs::path HudDirectory() const {
        return ProfileDirectory() / kHudAssetsFolder;
    }

    fs::path HudImagesDirectory() const {
        return HudDirectory() / kHudImagesFolder;
    }

    fs::path HudExportDirectory() const {
        return HudDirectory() / kHudExportFolder;
    }

    fs::path HudBackupDirectory() const {
        return HudDirectory() / kHudBackupFolder;
    }

    fs::path HudMigrationBackupPath() const {
        return HudBackupDirectory() / kHudMigrationBackupFile;
    }

    void EnsureAssetDirectories() const {
        std::error_code error;
        fs::create_directories(HudImagesDirectory(), error);
        if (error) {
            debuglog::WriteError("[hud] failed to create images directory: %ls error=%d", HudImagesDirectory().c_str(), error.value());
        }
        error.clear();
        fs::create_directories(HudExportDirectory(), error);
        if (error) {
            debuglog::WriteError("[hud] failed to create export directory: %ls error=%d", HudExportDirectory().c_str(), error.value());
        }
        error.clear();
        fs::create_directories(HudBackupDirectory(), error);
        if (error) {
            debuglog::WriteError("[hud] failed to create backup directory: %ls error=%d", HudBackupDirectory().c_str(), error.value());
        }
    }

    bool WriteMigrationBackup(const jsonutil::JsonObject& section, int sourceSchema) const {
        const fs::path target = HudMigrationBackupPath();
        std::error_code error;
        const bool backupExists = fs::exists(target, error);
        if (error) {
            debuglog::WriteError("[hud] migration backup stat failed path=%ls error=%d", target.c_str(), error.value());
            return false;
        }
        if (backupExists) {
            return true;
        }

        jsonutil::JsonObject backup;
        backup["backup_version"] = 1;
        backup["source_schema_version"] = sourceSchema;
        backup["hud"] = section;
        std::string output;
        jsonutil::WriteJson(jsonutil::JsonValue(std::move(backup)), output);

        const fs::path temporary = target.wstring() + L".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) {
                debuglog::WriteError("[hud] migration backup open failed path=%ls", temporary.c_str());
                return false;
            }
            file.write(output.data(), static_cast<std::streamsize>(output.size()));
            file.flush();
            if (!file) {
                debuglog::WriteError("[hud] migration backup write failed path=%ls", temporary.c_str());
                file.close();
                fs::remove(temporary, error);
                return false;
            }
        }

        fs::rename(temporary, target, error);
        if (error) {
            debuglog::WriteError("[hud] migration backup rename failed temp=%ls path=%ls error=%d", temporary.c_str(), target.c_str(), error.value());
            fs::remove(temporary, error);
            return false;
        }
        debuglog::WriteInfo("[hud] migration backup saved schema=%d path=%ls bytes=%zu", sourceSchema, target.c_str(), output.size());
        return true;
    }

    std::optional<fs::path> OpenFileDialog(UiText titleId, const std::wstring& filter) const {
        const std::wstring title = MarkupRenderer::Utf8ToWide(UiSettings::Instance().Text(titleId));
        return native_file_dialog::OpenFile(title, filter);
    }

    std::optional<fs::path> MakeUniquePath(const fs::path& directory, const fs::path& desiredName) const {
        fs::path candidate = directory / desiredName.filename();
        const fs::path stem = candidate.stem();
        const fs::path ext = candidate.extension();
        int suffix = 1;
        for (;;) {
            std::error_code existsError;
            const bool candidateExists = fs::exists(candidate, existsError);
            if (existsError) {
                debuglog::WriteError("[hud] unique image path stat failed path=%ls error=%d", candidate.c_str(), existsError.value());
                return std::nullopt;
            }
            if (!candidateExists) {
                return candidate;
            }
            candidate = directory / (stem.wstring() + L"_" + std::to_wstring(suffix++) + ext.wstring());
        }
    }

    std::optional<std::string> CopyImageIntoHudProfile() {
        const auto source = OpenFileDialog(
            UiText::HudInsertImage,
            BuildDialogFilter({
                { UiText::NotepadImageFilesFilter, L"*.png;*.jpg;*.jpeg;*.bmp;*.gif" },
                { UiText::NotepadAllFilesFilter, L"*.*" },
            }));
        if (!source.has_value()) {
            return std::nullopt;
        }

        EnsureAssetDirectories();
        const std::string sanitized = SanitizeFileStem(PathToUtf8(source->stem()), "image");
        const fs::path desiredName = fs::path(MarkupRenderer::Utf8ToWide(sanitized)).replace_extension(source->extension());
        const std::optional<fs::path> target = MakeUniquePath(HudImagesDirectory(), desiredName);
        if (!target) {
            statusMessage = UiSettings::Instance().Text(UiText::HudImageInsertFailed);
            return std::nullopt;
        }
        std::error_code copyError;
        fs::copy_file(*source, *target, fs::copy_options::none, copyError);
        if (copyError) {
            statusMessage = UiSettings::Instance().Text(UiText::HudImageInsertFailed);
            debuglog::WriteError("[hud] image copy failed source=%ls target=%ls error=%d", source->c_str(), target->c_str(), copyError.value());
            return std::nullopt;
        }

        statusMessage = UiSettings::Instance().Text(UiText::HudImageCopied);
        return PathToUtf8(target->filename());
    }

    HudWidget* FindWidget(std::string_view id) {
        const auto it = std::find_if(widgets.begin(), widgets.end(), [&](const HudWidget& widget) {
            return widget.id == id;
        });
        return it == widgets.end() ? nullptr : &(*it);
    }

    const HudWidget* FindWidget(std::string_view id) const {
        const auto it = std::find_if(widgets.begin(), widgets.end(), [&](const HudWidget& widget) {
            return widget.id == id;
        });
        return it == widgets.end() ? nullptr : &(*it);
    }

    HudWidget* SelectedWidget() {
        EnsureLoaded();
        if (selectedWidgetId.empty() && !widgets.empty()) {
            selectedWidgetId = widgets.front().id;
        }
        return selectedWidgetId.empty() ? nullptr : FindWidget(selectedWidgetId);
    }

    HudElement* FindElement(HudWidget& widget, std::string_view id) {
        const auto it = std::find_if(widget.elements.begin(), widget.elements.end(), [&](const HudElement& element) {
            return element.id == id;
        });
        return it == widget.elements.end() ? nullptr : &(*it);
    }

    const HudElement* FindElement(const HudWidget& widget, std::string_view id) const {
        const auto it = std::find_if(widget.elements.begin(), widget.elements.end(), [&](const HudElement& element) {
            return element.id == id;
        });
        return it == widget.elements.end() ? nullptr : &(*it);
    }

    void ResetElementRuntimeCache(HudElement& element) const {
        element.cachedText.clear();
        element.cachedImagePath.clear();
        element.cachedIconName.clear();
        element.cachedIconGlyph.clear();
        element.cachedStaticSource.clear();
        element.cachedCoarseClockUntilMs = 0;
        element.cachedStaticValueReady = false;
        element.noteMissing = false;
    }

    void RemapClonedElementIds(std::vector<HudElement>& elements) {
        std::unordered_map<std::string, std::string> idMap;
        idMap.reserve(elements.size());
        for (HudElement& element : elements) {
            const std::string oldId = element.id;
            element.id = GenerateId(element.type == ElementType::Group ? "hud_group" : "hud_el");
            if (!oldId.empty()) {
                idMap.emplace(oldId, element.id);
            }
            ResetElementRuntimeCache(element);
        }
        for (HudElement& element : elements) {
            if (element.parentId.empty()) {
                continue;
            }
            const auto it = idMap.find(element.parentId);
            element.parentId = it == idMap.end() ? std::string{} : it->second;
        }
    }

    bool ValidateWidgetModel(HudWidget& widget, std::unordered_set<std::string>& widgetIds) {
        bool repaired = false;
        if (widget.id.empty() || !widgetIds.insert(widget.id).second) {
            widget.id = GenerateId("hud");
            widgetIds.insert(widget.id);
            repaired = true;
        }
        if (widget.name.empty()) {
            widget.name = UiSettings::Instance().Text(UiText::HudDefaultWidgetName);
            repaired = true;
        }
        auto finiteOr = [&](float& value, float fallback) {
            if (!std::isfinite(value)) {
                value = fallback;
                repaired = true;
            }
        };
        finiteOr(widget.canvasWidth, 320.0f);
        finiteOr(widget.canvasHeight, 140.0f);
        finiteOr(widget.position.offsetX, 40.0f);
        finiteOr(widget.position.offsetY, 40.0f);
        const float clampedOffsetX = std::clamp(widget.position.offsetX, -kVirtualWidth, kVirtualWidth);
        const float clampedOffsetY = std::clamp(widget.position.offsetY, -kVirtualHeight, kVirtualHeight);
        repaired |= clampedOffsetX != widget.position.offsetX || clampedOffsetY != widget.position.offsetY;
        widget.position.offsetX = clampedOffsetX;
        widget.position.offsetY = clampedOffsetY;
        const float clampedCanvasWidth = std::clamp(widget.canvasWidth, 16.0f, 2000.0f);
        const float clampedCanvasHeight = std::clamp(widget.canvasHeight, 16.0f, 2000.0f);
        repaired |= clampedCanvasWidth != widget.canvasWidth || clampedCanvasHeight != widget.canvasHeight;
        widget.canvasWidth = clampedCanvasWidth;
        widget.canvasHeight = clampedCanvasHeight;
        if (widget.refreshMs < 0) {
            widget.refreshMs = 0;
            repaired = true;
        }
        NormalizeConditionFlags(widget.visibility.conditions);

        std::unordered_set<std::string> elementIds;
        elementIds.reserve(widget.elements.size());
        for (HudElement& element : widget.elements) {
            if (element.id.empty() || !elementIds.insert(element.id).second) {
                element.id = GenerateId(element.type == ElementType::Group ? "hud_group" : "hud_el");
                elementIds.insert(element.id);
                repaired = true;
            }
            if (element.name.empty()) {
                element.name = UiSettings::Instance().Text(ElementTypeLabelId(element.type));
                repaired = true;
            }
            finiteOr(element.x, 0.0f);
            finiteOr(element.y, 0.0f);
            finiteOr(element.width, 1.0f);
            finiteOr(element.height, element.type == ElementType::Line ? 0.0f : 1.0f);
            finiteOr(element.opacity, 1.0f);
            const float clampedX = std::clamp(element.x, -4000.0f, 4000.0f);
            const float clampedY = std::clamp(element.y, -4000.0f, 4000.0f);
            const float clampedWidth = std::clamp(element.width, 1.0f, 4000.0f);
            const float clampedHeight = std::clamp(element.height, 0.0f, 4000.0f);
            const float clampedOpacity = std::clamp(element.opacity, 0.0f, 1.0f);
            repaired |= clampedX != element.x || clampedY != element.y || clampedWidth != element.width || clampedHeight != element.height || clampedOpacity != element.opacity;
            element.x = clampedX;
            element.y = clampedY;
            element.width = clampedWidth;
            element.height = clampedHeight;
            element.opacity = clampedOpacity;
            finiteOr(element.data.minValue, 0.0f);
            finiteOr(element.data.maxValue, 100.0f);
            finiteOr(element.data.defaultValue, 0.0f);
            NormalizeConditionFlags(element.visibility.conditions);
            ResetElementRuntimeCache(element);
            if (element.type == ElementType::Image && !MarkupRenderer::IsSafeRelativeAssetPath(element.data.imagePath)) {
                debuglog::WriteError("[hud] unsafe image path widget=%s element=%s", widget.id.c_str(), element.id.c_str());
            }
            if (element.type == ElementType::TextMarkup && element.data.sourceMode == SourceMode::NotepadNote && !element.data.noteId.empty() && notepadModule) {
                NotepadModule::NoteContent note;
                if (!notepadModule->TryGetNote(element.data.noteId, note)) {
                    element.noteMissing = true;
                    debuglog::WriteError("[hud] linked note missing widget=%s element=%s note=%s", widget.id.c_str(), element.id.c_str(), element.data.noteId.c_str());
                }
            }
        }

        for (HudElement& element : widget.elements) {
            if (element.type == ElementType::Group && !element.parentId.empty()) {
                element.parentId.clear();
                repaired = true;
                continue;
            }
            if (element.parentId.empty()) {
                continue;
            }
            const HudElement* parent = FindElement(widget, element.parentId);
            if (!parent || parent->type != ElementType::Group || parent->id == element.id) {
                element.parentId.clear();
                repaired = true;
            }
        }
        InvalidateElementOrderCache(widget);
        return repaired;
    }

    bool ValidateModel() {
        const double beginMs = HudPerfNowMs();
        bool repaired = false;
        std::unordered_set<std::string> widgetIds;
        widgetIds.reserve(widgets.size());
        for (HudWidget& widget : widgets) {
            repaired |= ValidateWidgetModel(widget, widgetIds);
        }
        if (!selectedWidgetId.empty() && !FindWidget(selectedWidgetId)) {
            selectedWidgetId = widgets.empty() ? std::string{} : widgets.front().id;
            selectedElementIds.clear();
            repaired = true;
        }
        lastValidationMs = HudPerfNowMs() - beginMs;
        if (repaired) {
            debuglog::WriteInfo("[hud] model repaired widgets=%zu validation=%.2fms", widgets.size(), lastValidationMs);
        }
        return repaired;
    }

    HudElement* PrimarySelectedElement(HudWidget& widget) {
        if (selectedElementIds.empty()) {
            return nullptr;
        }
        HudElement* element = FindElement(widget, selectedElementIds.back());
        if (!element) {
            selectedElementIds.clear();
        }
        return element;
    }

    bool IsElementSelected(std::string_view id) const {
        return std::find(selectedElementIds.begin(), selectedElementIds.end(), id) != selectedElementIds.end();
    }

    void SelectElement(std::string_view id, bool additive) {
        if (!additive) {
            selectedElementIds.clear();
        }
        const auto it = std::find(selectedElementIds.begin(), selectedElementIds.end(), id);
        if (it != selectedElementIds.end()) {
            if (additive) {
                selectedElementIds.erase(it);
            }
            return;
        }
        selectedElementIds.emplace_back(id);
    }

    int NextZ(const HudWidget& widget) const {
        int z = 0;
        for (const HudElement& element : widget.elements) {
            z = std::max(z, element.z + 1);
        }
        return z;
    }

    std::string GenerateId(std::string_view prefix) {
        const std::uint64_t now = TickNow();
        char buffer[80]{};
        std::snprintf(buffer, sizeof(buffer), "%.*s_%llx_%llx",
            static_cast<int>(prefix.size()),
            prefix.data(),
            static_cast<unsigned long long>(now),
            static_cast<unsigned long long>(++idCounter));
        return buffer;
    }

    HudElement MakeElement(ElementType type, std::string name, float x, float y, float w, float h) {
        HudElement element;
        element.id = GenerateId("hud_el");
        element.type = type;
        element.name = std::move(name);
        element.x = x;
        element.y = y;
        element.width = w;
        element.height = h;
        NormalizeConditionFlags(element.visibility.conditions);
        switch (type) {
        case ElementType::Text:
            element.data.text = "{time}";
            element.style.fillEnabled = false;
            break;
        case ElementType::TextMarkup:
            element.data.text = "{8AF7FF}HelperByOrc HUD{FFFFFF}\n#font14{time}";
            element.style.fillEnabled = false;
            break;
        case ElementType::Image:
            element.data.imagePath = "weapons/{myweaponid}.png";
            element.style.fillAlpha = 0.18f;
            element.style.strokeEnabled = true;
            break;
        case ElementType::Shape:
            element.style.fillEnabled = true;
            element.style.strokeEnabled = true;
            break;
        case ElementType::Line:
            element.height = std::max(1.0f, h);
            element.style.fillEnabled = false;
            element.style.strokeEnabled = true;
            element.style.strokeSize = 2.0f;
            break;
        case ElementType::Icon:
            element.data.icon = "star";
            element.style.fillEnabled = false;
            break;
        case ElementType::ProgressBar:
            element.data.expression = "{health}";
            element.data.maxValue = 100.0f;
            element.style.fillEnabled = true;
            element.style.strokeEnabled = true;
            element.style.rounding = 4.0f;
            break;
        case ElementType::Group:
            element.style.fillEnabled = false;
            element.style.strokeEnabled = true;
            element.style.strokeAlpha = 0.35f;
            break;
        }
        return element;
    }

    HudWidget MakeBaseWidget(std::string name) {
        HudWidget widget;
        widget.id = GenerateId("hud");
        widget.name = std::move(name);
        widget.position.anchor = Anchor::TopLeft;
        widget.position.offsetX = 40.0f;
        widget.position.offsetY = 40.0f;
        NormalizeConditionFlags(widget.visibility.conditions);
        return widget;
    }

    HudWidget MakeDefaultWidget() {
        HudWidget widget = MakeBaseWidget(UiSettings::Instance().Text(UiText::HudDefaultWidgetName));
        widget.canvasWidth = 260.0f;
        widget.canvasHeight = 72.0f;
        HudElement text = MakeElement(ElementType::Text, UiSettings::Instance().Text(UiText::HudElementText), 12.0f, 12.0f, 236.0f, 36.0f);
        text.data.text = "{time}";
        text.z = 0;
        widget.elements.push_back(std::move(text));
        return widget;
    }

    HudWidget MakeWeaponPreset() {
        HudWidget widget = MakeBaseWidget(UiSettings::Instance().Text(UiText::HudPresetWeapon));
        widget.position.anchor = Anchor::BottomCenter;
        widget.position.offsetX = 0.0f;
        widget.position.offsetY = -180.0f;
        widget.canvasWidth = 280.0f;
        widget.canvasHeight = 88.0f;

        HudElement image = MakeElement(ElementType::Image, UiSettings::Instance().Text(UiText::HudElementImage), 0.0f, 6.0f, 76.0f, 76.0f);
        image.data.imagePath = "weapons/{myweaponid}.png";
        image.style.fillEnabled = false;
        image.style.strokeEnabled = false;
        image.z = 0;
        widget.elements.push_back(std::move(image));

        HudElement text = MakeElement(ElementType::Text, UiSettings::Instance().Text(UiText::HudElementText), 86.0f, 28.0f, 188.0f, 34.0f);
        text.data.text = "[ifandor(\"{myweapon}\"!=\"Fist\"?{myweapon} - {myweaponclip}:)]";
        text.data.fontSize = 18;
        text.style.text = ImVec4(0.54f, 0.97f, 1.0f, 1.0f);
        text.style.fillEnabled = false;
        text.style.shadowEnabled = true;
        text.z = 1;
        widget.elements.push_back(std::move(text));
        return widget;
    }

    HudWidget MakeFreeTextPreset() {
        HudWidget widget = MakeBaseWidget(UiSettings::Instance().Text(UiText::HudPresetFreeText));
        widget.position.offsetY = 160.0f;
        widget.canvasWidth = 320.0f;
        widget.canvasHeight = 96.0f;
        HudElement box = MakeElement(ElementType::Shape, UiSettings::Instance().Text(UiText::HudElementShape), 0.0f, 0.0f, 320.0f, 96.0f);
        box.style.fillAlpha = 0.58f;
        box.style.strokeEnabled = true;
        box.style.shadowEnabled = true;
        box.z = 0;
        widget.elements.push_back(std::move(box));

        HudElement text = MakeElement(ElementType::TextMarkup, UiSettings::Instance().Text(UiText::HudElementMarkup), 12.0f, 12.0f, 296.0f, 72.0f);
        text.data.text = "{8AF7FF}HelperByOrc HUD{FFFFFF}\n#font14{time}";
        text.z = 1;
        widget.elements.push_back(std::move(text));
        return widget;
    }

    HudWidget MakePlayerStatusPreset() {
        HudWidget widget = MakeBaseWidget(UiSettings::Instance().Text(UiText::HudPresetPlayerStatus));
        widget.canvasWidth = 260.0f;
        widget.canvasHeight = 112.0f;
        HudElement bg = MakeElement(ElementType::Shape, UiSettings::Instance().Text(UiText::HudElementShape), 0.0f, 0.0f, 260.0f, 112.0f);
        bg.style.fillAlpha = 0.62f;
        bg.style.strokeEnabled = true;
        bg.z = 0;
        widget.elements.push_back(std::move(bg));
        HudElement text = MakeElement(ElementType::Text, UiSettings::Instance().Text(UiText::HudElementText), 12.0f, 10.0f, 236.0f, 28.0f);
        text.data.text = "{nick} [{id}]";
        text.data.fontSize = 16;
        text.style.fillEnabled = false;
        text.z = 1;
        widget.elements.push_back(std::move(text));
        HudElement hp = MakeElement(ElementType::ProgressBar, "HP", 12.0f, 48.0f, 236.0f, 16.0f);
        hp.data.expression = "{health}";
        hp.style.progressFill = ImVec4(0.20f, 0.82f, 0.38f, 1.0f);
        hp.z = 2;
        widget.elements.push_back(std::move(hp));
        HudElement armor = MakeElement(ElementType::ProgressBar, "Armor", 12.0f, 74.0f, 236.0f, 16.0f);
        armor.data.expression = "{armour}";
        armor.style.progressFill = ImVec4(0.35f, 0.62f, 1.0f, 1.0f);
        armor.z = 3;
        widget.elements.push_back(std::move(armor));
        return widget;
    }

    HudWidget MakeVehiclePreset() {
        HudWidget widget = MakeBaseWidget(UiSettings::Instance().Text(UiText::HudPresetVehicle));
        widget.position.anchor = Anchor::BottomRight;
        widget.position.offsetX = -420.0f;
        widget.position.offsetY = -240.0f;
        widget.canvasWidth = 300.0f;
        widget.canvasHeight = 92.0f;
        HudElement bg = MakeElement(ElementType::Shape, UiSettings::Instance().Text(UiText::HudElementShape), 0.0f, 0.0f, 300.0f, 92.0f);
        bg.style.fillAlpha = 0.58f;
        bg.style.strokeEnabled = true;
        bg.z = 0;
        widget.elements.push_back(std::move(bg));
        HudElement text = MakeElement(ElementType::Text, UiSettings::Instance().Text(UiText::HudElementText), 12.0f, 12.0f, 276.0f, 28.0f);
        text.data.text = "[car({id})]";
        text.z = 1;
        widget.elements.push_back(std::move(text));
        HudElement progress = MakeElement(ElementType::ProgressBar, UiSettings::Instance().Text(UiText::HudElementProgress), 12.0f, 52.0f, 276.0f, 18.0f);
        progress.data.expression = "[carhealth({id})]";
        progress.data.maxValue = 1000.0f;
        progress.style.progressFill = ImVec4(0.95f, 0.72f, 0.22f, 1.0f);
        progress.z = 2;
        widget.elements.push_back(std::move(progress));
        return widget;
    }

    HudWidget MakeNotePreset() {
        UiSettings& ui = UiSettings::Instance();
        HudWidget widget = MakeBaseWidget(UiSettings::Instance().Text(UiText::HudPresetNoteCard));
        widget.canvasWidth = 360.0f;
        widget.canvasHeight = 180.0f;
        HudElement bg = MakeElement(ElementType::Shape, UiSettings::Instance().Text(UiText::HudElementShape), 0.0f, 0.0f, 360.0f, 180.0f);
        bg.style.fillAlpha = 0.68f;
        bg.style.strokeEnabled = true;
        bg.z = 0;
        widget.elements.push_back(std::move(bg));
        HudElement note = MakeElement(ElementType::TextMarkup, UiSettings::Instance().Text(UiText::HudElementMarkup), 14.0f, 12.0f, 332.0f, 156.0f);
        note.data.text = "#font18{8AF7FF}" + std::string(ui.Text(UiText::HudPresetNoteTitle))
            + "{FFFFFF}\n#hr\n#font14" + ui.Text(UiText::HudPresetNoteBody);
        note.z = 1;
        widget.elements.push_back(std::move(note));
        return widget;
    }

    HudWidget MakeTimerPreset() {
        HudWidget widget = MakeBaseWidget(UiSettings::Instance().Text(UiText::HudPresetTimer));
        widget.position.anchor = Anchor::TopCenter;
        widget.position.offsetX = 0.0f;
        widget.position.offsetY = 80.0f;
        widget.canvasWidth = 220.0f;
        widget.canvasHeight = 64.0f;
        HudElement bg = MakeElement(ElementType::Shape, UiSettings::Instance().Text(UiText::HudElementShape), 0.0f, 0.0f, 220.0f, 64.0f);
        bg.style.fillAlpha = 0.48f;
        bg.style.strokeEnabled = true;
        bg.style.rounding = 12.0f;
        bg.z = 0;
        widget.elements.push_back(std::move(bg));
        HudElement text = MakeElement(ElementType::Text, UiSettings::Instance().Text(UiText::HudElementText), 10.0f, 14.0f, 200.0f, 36.0f);
        text.data.text = "{time}";
        text.data.fontSize = 30;
        text.data.align = TextAlign::Center;
        text.style.fillEnabled = false;
        text.z = 1;
        widget.elements.push_back(std::move(text));
        return widget;
    }

    HudWidget MakeDashboardPreset() {
        HudWidget widget = MakePlayerStatusPreset();
        widget.name = UiSettings::Instance().Text(UiText::HudPresetDashboard);
        widget.canvasWidth = 420.0f;
        widget.canvasHeight = 150.0f;
        HudElement title = MakeElement(ElementType::Text, UiSettings::Instance().Text(UiText::HudElementText), 280.0f, 12.0f, 126.0f, 28.0f);
        title.data.text = "{time}";
        title.data.align = TextAlign::Right;
        title.style.fillEnabled = false;
        title.z = 4;
        widget.elements.push_back(std::move(title));
        HudElement line = MakeElement(ElementType::Line, UiSettings::Instance().Text(UiText::HudElementLine), 12.0f, 112.0f, 396.0f, 0.0f);
        line.z = 5;
        widget.elements.push_back(std::move(line));
        HudElement info = MakeElement(ElementType::Text, UiSettings::Instance().Text(UiText::HudElementText), 12.0f, 120.0f, 396.0f, 24.0f);
        info.data.text = "ID: {id} | Weapon: {myweapon}";
        info.data.fontSize = 14;
        info.style.fillEnabled = false;
        info.z = 6;
        widget.elements.push_back(std::move(info));
        return widget;
    }

    void AddWidget(HudWidget widget) {
        EnsureLoaded();
        selectedWidgetId = widget.id;
        selectedElementIds.clear();
        widgets.push_back(std::move(widget));
        MarkChanged(true);
    }

    void DuplicateSelectedWidget() {
        HudWidget* selected = SelectedWidget();
        if (!selected) {
            return;
        }
        HudWidget copy = *selected;
        copy.id = GenerateId("hud");
        copy.name += " copy";
        copy.position.offsetX += 24.0f;
        copy.position.offsetY += 24.0f;
        copy.nextRefreshAtMs = 0;
        RemapClonedElementIds(copy.elements);
        selectedWidgetId = copy.id;
        selectedElementIds.clear();
        widgets.push_back(std::move(copy));
        MarkChanged(true);
    }

    void DeleteSelectedWidget() {
        EnsureLoaded();
        const std::string deletedId = selectedWidgetId;
        const auto it = std::remove_if(widgets.begin(), widgets.end(), [&](const HudWidget& widget) {
            return widget.id == deletedId;
        });
        if (it == widgets.end()) {
            return;
        }
        widgets.erase(it, widgets.end());
        selectedWidgetId = widgets.empty() ? std::string() : widgets.front().id;
        selectedElementIds.clear();
        if (placementWidgetId == deletedId) {
            placementMode = false;
            placementWidgetId.clear();
        }
        MarkChanged(true);
    }

    void AddElement(ElementType type) {
        HudWidget* widget = SelectedWidget();
        if (!widget) {
            return;
        }
        UiSettings& ui = UiSettings::Instance();
        HudElement element = MakeElement(type, ui.Text(ElementTypeLabelId(type)), 24.0f, 24.0f, 180.0f, 48.0f);
        if (type == ElementType::Shape) {
            element.width = 220.0f;
            element.height = 84.0f;
        } else if (type == ElementType::Image) {
            element.width = 96.0f;
            element.height = 96.0f;
        } else if (type == ElementType::Line) {
            element.width = 180.0f;
            element.height = 0.0f;
        } else if (type == ElementType::ProgressBar) {
            element.width = 220.0f;
            element.height = 18.0f;
        }
        element.z = NextZ(*widget);
        selectedElementIds = { element.id };
        widget->elements.push_back(std::move(element));
        MarkChanged(true);
    }

    void DuplicateSelectedElements() {
        HudWidget* widget = SelectedWidget();
        if (!widget || selectedElementIds.empty()) {
            return;
        }
        std::unordered_set<std::string> sourceIds(selectedElementIds.begin(), selectedElementIds.end());
        for (const std::string& id : selectedElementIds) {
            const HudElement* element = FindElement(*widget, id);
            if (!element || element->type != ElementType::Group) {
                continue;
            }
            for (const HudElement& child : widget->elements) {
                if (child.parentId == element->id) {
                    sourceIds.insert(child.id);
                }
            }
        }

        std::vector<HudElement> copies;
        std::vector<std::string> newSelection;
        copies.reserve(sourceIds.size());
        for (const HudElement& element : widget->elements) {
            if (sourceIds.find(element.id) == sourceIds.end()) {
                continue;
            }
            HudElement copy = element;
            copy.name += " copy";
            copy.x += 16.0f;
            copy.y += 16.0f;
            copy.z = NextZ(*widget) + static_cast<int>(copies.size());
            copies.push_back(std::move(copy));
        }
        if (copies.empty()) {
            return;
        }
        RemapClonedElementIds(copies);
        for (const HudElement& copy : copies) {
            if (copy.type == ElementType::Group || sourceIds.find(copy.parentId) == sourceIds.end()) {
                newSelection.push_back(copy.id);
            }
        }
        if (newSelection.empty()) {
            for (const HudElement& copy : copies) {
                newSelection.push_back(copy.id);
            }
        }
        widget->elements.insert(widget->elements.end(), copies.begin(), copies.end());
        selectedElementIds = std::move(newSelection);
        MarkChanged(true);
    }

    void DeleteSelectedElements() {
        HudWidget* widget = SelectedWidget();
        if (!widget || selectedElementIds.empty()) {
            return;
        }
        std::set<std::string> ids(selectedElementIds.begin(), selectedElementIds.end());
        std::unordered_set<std::string> affectedGroups;
        for (const HudElement& element : widget->elements) {
            if (!element.parentId.empty() && ids.find(element.parentId) != ids.end()) {
                ids.insert(element.id);
            }
            if (!element.parentId.empty() && ids.find(element.id) != ids.end() && ids.find(element.parentId) == ids.end()) {
                affectedGroups.insert(element.parentId);
            }
        }
        const auto it = std::remove_if(widget->elements.begin(), widget->elements.end(), [&](const HudElement& element) {
            return ids.find(element.id) != ids.end();
        });
        if (it == widget->elements.end()) {
            return;
        }
        widget->elements.erase(it, widget->elements.end());
        for (const std::string& groupId : affectedGroups) {
            RecomputeGroupBounds(*widget, groupId);
        }
        selectedElementIds.clear();
        MarkChanged(true);
    }

    void GroupSelectedElements() {
        HudWidget* widget = SelectedWidget();
        if (!widget || selectedElementIds.size() < 2) {
            return;
        }

        bool hasRect = false;
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        for (const std::string& id : selectedElementIds) {
            const HudElement* element = FindElement(*widget, id);
            if (!element || element->type == ElementType::Group) {
                continue;
            }
            const float x2 = element->x + element->width;
            const float y2 = element->y + element->height;
            if (!hasRect) {
                minX = element->x;
                minY = element->y;
                maxX = x2;
                maxY = y2;
                hasRect = true;
            } else {
                minX = std::min(minX, element->x);
                minY = std::min(minY, element->y);
                maxX = std::max(maxX, x2);
                maxY = std::max(maxY, y2);
            }
        }
        if (!hasRect) {
            return;
        }

        HudElement group = MakeElement(ElementType::Group, UiSettings::Instance().Text(UiText::HudElementGroup), minX, minY, maxX - minX, maxY - minY);
        group.id = GenerateId("hud_group");
        group.z = NextZ(*widget);
        const std::string groupId = group.id;
        std::unordered_set<std::string> previousGroups;
        for (const std::string& id : selectedElementIds) {
            if (HudElement* element = FindElement(*widget, id)) {
                if (element->type != ElementType::Group) {
                    if (!element->parentId.empty()) previousGroups.insert(element->parentId);
                    element->parentId = groupId;
                }
            }
        }
        widget->elements.push_back(std::move(group));
        for (const std::string& previousGroup : previousGroups) {
            RecomputeGroupBounds(*widget, previousGroup);
        }
        selectedElementIds = { groupId };
        MarkChanged(true);
    }

    void UngroupSelectedElements() {
        HudWidget* widget = SelectedWidget();
        if (!widget || selectedElementIds.empty()) {
            return;
        }

        std::set<std::string> groupIds;
        for (const std::string& id : selectedElementIds) {
            const HudElement* element = FindElement(*widget, id);
            if (element && element->type == ElementType::Group) {
                groupIds.insert(id);
            }
        }
        if (groupIds.empty()) {
            return;
        }

        for (HudElement& element : widget->elements) {
            if (groupIds.find(element.parentId) != groupIds.end()) {
                element.parentId.clear();
            }
        }
        const auto it = std::remove_if(widget->elements.begin(), widget->elements.end(), [&](const HudElement& element) {
            return groupIds.find(element.id) != groupIds.end();
        });
        widget->elements.erase(it, widget->elements.end());
        selectedElementIds.clear();
        MarkChanged(true);
    }

    std::vector<HudElement*> EditableSelectionRoots(HudWidget& widget) {
        std::unordered_set<std::string> selected(selectedElementIds.begin(), selectedElementIds.end());
        std::vector<HudElement*> result;
        for (const std::string& id : selectedElementIds) {
            HudElement* element = FindElement(widget, id);
            if (!element || element->locked) {
                continue;
            }
            if (!element->parentId.empty() && selected.find(element->parentId) != selected.end()) {
                continue;
            }
            result.push_back(element);
        }
        return result;
    }

    void MoveElementWithChildren(HudWidget& widget, HudElement& element, float dx, float dy) {
        element.x += dx;
        element.y += dy;
        if (element.type != ElementType::Group) {
            return;
        }
        for (HudElement& child : widget.elements) {
            if (child.parentId == element.id && !child.locked) {
                child.x += dx;
                child.y += dy;
            }
        }
    }

    void AlignSelectedElements(HudAlignAction action) {
        HudWidget* widget = SelectedWidget();
        if (!widget) {
            return;
        }
        std::vector<HudElement*> elements = EditableSelectionRoots(*widget);
        const std::size_t required = action == HudAlignAction::DistributeHorizontal || action == HudAlignAction::DistributeVertical ? 3u : 2u;
        if (elements.size() < required) {
            return;
        }

        float minX = elements.front()->x;
        float minY = elements.front()->y;
        float maxX = elements.front()->x + elements.front()->width;
        float maxY = elements.front()->y + elements.front()->height;
        for (const HudElement* element : elements) {
            minX = std::min(minX, element->x);
            minY = std::min(minY, element->y);
            maxX = std::max(maxX, element->x + element->width);
            maxY = std::max(maxY, element->y + element->height);
        }

        if (action == HudAlignAction::DistributeHorizontal) {
            std::sort(elements.begin(), elements.end(), [](const HudElement* left, const HudElement* right) { return left->x < right->x; });
            const float firstCenter = elements.front()->x + elements.front()->width * 0.5f;
            const float lastCenter = elements.back()->x + elements.back()->width * 0.5f;
            const float step = (lastCenter - firstCenter) / static_cast<float>(elements.size() - 1);
            for (std::size_t i = 1; i + 1 < elements.size(); ++i) {
                const float target = firstCenter + step * static_cast<float>(i);
                MoveElementWithChildren(*widget, *elements[i], target - (elements[i]->x + elements[i]->width * 0.5f), 0.0f);
            }
        } else if (action == HudAlignAction::DistributeVertical) {
            std::sort(elements.begin(), elements.end(), [](const HudElement* left, const HudElement* right) { return left->y < right->y; });
            const float firstCenter = elements.front()->y + elements.front()->height * 0.5f;
            const float lastCenter = elements.back()->y + elements.back()->height * 0.5f;
            const float step = (lastCenter - firstCenter) / static_cast<float>(elements.size() - 1);
            for (std::size_t i = 1; i + 1 < elements.size(); ++i) {
                const float target = firstCenter + step * static_cast<float>(i);
                MoveElementWithChildren(*widget, *elements[i], 0.0f, target - (elements[i]->y + elements[i]->height * 0.5f));
            }
        } else {
            for (HudElement* element : elements) {
                float dx = 0.0f;
                float dy = 0.0f;
                switch (action) {
                case HudAlignAction::Left: dx = minX - element->x; break;
                case HudAlignAction::HorizontalCenter: dx = (minX + maxX - element->width) * 0.5f - element->x; break;
                case HudAlignAction::Right: dx = maxX - element->width - element->x; break;
                case HudAlignAction::Top: dy = minY - element->y; break;
                case HudAlignAction::VerticalCenter: dy = (minY + maxY - element->height) * 0.5f - element->y; break;
                case HudAlignAction::Bottom: dy = maxY - element->height - element->y; break;
                case HudAlignAction::DistributeHorizontal:
                case HudAlignAction::DistributeVertical: break;
                }
                MoveElementWithChildren(*widget, *element, dx, dy);
            }
        }
        MarkChanged();
    }

    jsonutil::JsonObject SerializeElement(const HudElement& element) const {
        jsonutil::JsonObject root;
        root["id"] = element.id;
        root["type"] = ElementTypeToString(element.type);
        root["name"] = element.name;
        root["parent_id"] = element.parentId;
        root["x"] = element.x;
        root["y"] = element.y;
        root["w"] = element.width;
        root["h"] = element.height;
        root["z"] = element.z;
        root["opacity"] = element.opacity;
        root["locked"] = element.locked;
        root["hidden"] = element.hidden;
        root["visibility"] = SerializeVisibility(element.visibility);
        root["style"] = SerializeStyle(element.style);
        root["data"] = SerializeData(element.data);
        return root;
    }

    HudElement DeserializeElement(const jsonutil::JsonObject& object) {
        HudElement element;
        element.id = jsonutil::JsonStringOr(&object, "id", GenerateId("hud_el"));
        element.type = ElementTypeFromString(jsonutil::JsonStringOr(&object, "type", ElementTypeToString(element.type)));
        element.name = jsonutil::JsonStringOr(&object, "name", UiSettings::Instance().Text(ElementTypeLabelId(element.type)));
        element.parentId = jsonutil::JsonStringOr(&object, "parent_id", "");
        element.x = jsonutil::JsonNumberOr(&object, "x", element.x);
        element.y = jsonutil::JsonNumberOr(&object, "y", element.y);
        element.width = std::max(1.0f, jsonutil::JsonNumberOr(&object, "w", element.width));
        element.height = std::max(0.0f, jsonutil::JsonNumberOr(&object, "h", element.height));
        element.z = jsonutil::JsonNumberOr(&object, "z", element.z);
        element.opacity = std::clamp(jsonutil::JsonNumberOr(&object, "opacity", element.opacity), 0.0f, 1.0f);
        element.locked = jsonutil::JsonBoolOr(&object, "locked", element.locked);
        element.hidden = jsonutil::JsonBoolOr(&object, "hidden", element.hidden);
        bool migrated = false;
        DeserializeVisibility(jsonutil::JsonObjectOrNull(&object, "visibility"), element.visibility, migrated);
        element.style = DeserializeStyle(jsonutil::JsonObjectOrNull(&object, "style"), element.style);
        element.data = DeserializeData(jsonutil::JsonObjectOrNull(&object, "data"));
        element.cachedNumber = element.data.defaultValue;
        return element;
    }

    jsonutil::JsonObject SerializeWidget(const HudWidget& widget) const {
        jsonutil::JsonObject root;
        root["id"] = widget.id;
        root["name"] = widget.name;
        root["enabled"] = widget.enabled;
        root["anchor"] = AnchorToString(widget.position.anchor);
        root["offset_x"] = widget.position.offsetX;
        root["offset_y"] = widget.position.offsetY;

        jsonutil::JsonObject canvas;
        canvas["width"] = widget.canvasWidth;
        canvas["height"] = widget.canvasHeight;
        root["canvas_size"] = std::move(canvas);
        root["scale_policy"] = ScalePolicyToString(widget.scalePolicy);
        root["visibility"] = SerializeVisibility(widget.visibility);
        root["refresh_ms"] = widget.refreshMs;

        jsonutil::JsonArray elements;
        for (const HudElement& element : widget.elements) {
            elements.emplace_back(SerializeElement(element));
        }
        root["elements"] = std::move(elements);
        return root;
    }

    jsonutil::JsonObject SerializeConfig() const {
        jsonutil::JsonObject root;
        root["schema_version"] = kHudSchemaVersion;
        root["selected_widget_id"] = selectedWidgetId;
        root["editor"] = hud_v3::SerializeEditorState(hud_v3::EditorState{
            BottomPanelToString(bottomPanel),
            bottomPanelHeight,
            bottomPanelCollapsed,
            PropertiesCategoryToString(propertiesCategory),
            snapEnabled,
            guidesEnabled,
            gridSize,
            editorCanvasHeight,
            editorCanvasAutoHeight,
        });
        jsonutil::JsonArray widgetArray;
        for (const HudWidget& widget : widgets) {
            widgetArray.emplace_back(SerializeWidget(widget));
        }
        root["widgets"] = std::move(widgetArray);
        return root;
    }

    HudWidget DeserializeWidgetV2(const jsonutil::JsonObject& object) {
        HudWidget widget = MakeDefaultWidget();
        widget.elements.clear();
        widget.id = jsonutil::JsonStringOr(&object, "id", widget.id);
        widget.name = jsonutil::JsonStringOr(&object, "name", widget.name);
        widget.enabled = jsonutil::JsonBoolOr(&object, "enabled", widget.enabled);
        widget.position.anchor = AnchorFromString(jsonutil::JsonStringOr(&object, "anchor", AnchorToString(widget.position.anchor)));
        widget.position.offsetX = jsonutil::JsonNumberOr(&object, "offset_x", widget.position.offsetX);
        widget.position.offsetY = jsonutil::JsonNumberOr(&object, "offset_y", widget.position.offsetY);
        if (const jsonutil::JsonObject* canvas = jsonutil::JsonObjectOrNull(&object, "canvas_size")) {
            widget.canvasWidth = std::max(16.0f, jsonutil::JsonNumberOr(canvas, "width", widget.canvasWidth));
            widget.canvasHeight = std::max(16.0f, jsonutil::JsonNumberOr(canvas, "height", widget.canvasHeight));
        }
        widget.scalePolicy = ScalePolicyFromString(jsonutil::JsonStringOr(&object, "scale_policy", ScalePolicyToString(widget.scalePolicy)));
        DeserializeVisibility(jsonutil::JsonObjectOrNull(&object, "visibility"), widget.visibility, deprecatedHelperVisibilityMigrated);
        widget.refreshMs = std::max(0, jsonutil::JsonNumberOr(&object, "refresh_ms", widget.refreshMs));
        if (const jsonutil::JsonArray* array = jsonutil::JsonArrayOrNull(&object, "elements")) {
            for (const jsonutil::JsonValue& value : *array) {
                const jsonutil::JsonObject* elementObject = value.TryObject();
                if (!elementObject) {
                    continue;
                }
                HudElement element = DeserializeElement(*elementObject);
                if (!element.id.empty() && !FindElement(widget, element.id)) {
                    widget.elements.push_back(std::move(element));
                }
            }
        }
        if (widget.elements.empty()) {
            widget.elements.push_back(MakeElement(ElementType::Text, UiSettings::Instance().Text(UiText::HudElementText), 12.0f, 12.0f, 220.0f, 32.0f));
        }
        return widget;
    }

    HudWidget DeserializeLegacyWidget(const jsonutil::JsonObject& object) {
        configMigratedToV2 = true;
        HudWidget widget = MakeBaseWidget(jsonutil::JsonStringOr(&object, "name", UiSettings::Instance().Text(UiText::HudDefaultWidgetName)));
        widget.id = jsonutil::JsonStringOr(&object, "id", widget.id);
        widget.enabled = jsonutil::JsonBoolOr(&object, "enabled", widget.enabled);
        if (const jsonutil::JsonObject* position = jsonutil::JsonObjectOrNull(&object, "position")) {
            widget.position.anchor = AnchorFromString(jsonutil::JsonStringOr(position, "anchor", AnchorToString(widget.position.anchor)));
            widget.position.offsetX = jsonutil::JsonNumberOr(position, "offset_x", widget.position.offsetX);
            widget.position.offsetY = jsonutil::JsonNumberOr(position, "offset_y", widget.position.offsetY);
        }
        float legacyScale = 1.0f;
        bool autoSize = true;
        if (const jsonutil::JsonObject* size = jsonutil::JsonObjectOrNull(&object, "size")) {
            autoSize = jsonutil::JsonBoolOr(size, "auto_size", autoSize);
            widget.canvasWidth = std::max(32.0f, jsonutil::JsonNumberOr(size, "width", widget.canvasWidth));
            widget.canvasHeight = std::max(24.0f, jsonutil::JsonNumberOr(size, "height", widget.canvasHeight));
            legacyScale = std::clamp(jsonutil::JsonNumberOr(size, "scale", legacyScale), 0.5f, 3.0f);
        }
        if (autoSize) {
            widget.canvasWidth = std::max(widget.canvasWidth, 260.0f);
            widget.canvasHeight = std::max(widget.canvasHeight, 96.0f);
        }
        DeserializeVisibility(jsonutil::JsonObjectOrNull(&object, "visibility"), widget.visibility, deprecatedHelperVisibilityMigrated);
        widget.refreshMs = std::max(0, jsonutil::JsonNumberOr(&object, "refresh_ms", widget.refreshMs));

        HudElement element = MakeElement(ElementType::TextMarkup, UiSettings::Instance().Text(UiText::HudElementMarkup), 0.0f, 0.0f, widget.canvasWidth, widget.canvasHeight);
        element.data.fontSize = std::clamp(static_cast<int>(std::lround(16.0f * legacyScale)), 8, 96);
        if (const jsonutil::JsonObject* source = jsonutil::JsonObjectOrNull(&object, "source")) {
            element.data.sourceMode = SourceModeFromString(jsonutil::JsonStringOr(source, "mode", SourceModeToString(element.data.sourceMode)));
            element.data.text = jsonutil::JsonStringOr(source, "text", element.data.text);
            element.data.noteId = jsonutil::JsonStringOr(source, "note_id", element.data.noteId);
        }
        if (const jsonutil::JsonObject* legacyStyle = jsonutil::JsonObjectOrNull(&object, "style")) {
            element.style.fill = HexToColor(jsonutil::JsonStringOr(legacyStyle, "background_color", ColorToHex(element.style.fill)), element.style.fill);
            element.style.fillAlpha = std::clamp(jsonutil::JsonNumberOr(legacyStyle, "background_alpha", element.style.fillAlpha), 0.0f, 1.0f);
            element.style.fillEnabled = element.style.fillAlpha > 0.001f;
            element.style.text = HexToColor(jsonutil::JsonStringOr(legacyStyle, "text_color", ColorToHex(element.style.text)), element.style.text);
            element.style.textAlpha = std::clamp(jsonutil::JsonNumberOr(legacyStyle, "text_alpha", element.style.textAlpha), 0.0f, 1.0f);
            element.style.strokeEnabled = jsonutil::JsonBoolOr(legacyStyle, "border", element.style.strokeEnabled);
            element.style.stroke = HexToColor(jsonutil::JsonStringOr(legacyStyle, "border_color", ColorToHex(element.style.stroke)), element.style.stroke);
            element.style.strokeAlpha = std::clamp(jsonutil::JsonNumberOr(legacyStyle, "border_alpha", element.style.strokeAlpha), 0.0f, 1.0f);
            element.style.strokeSize = std::clamp(jsonutil::JsonNumberOr(legacyStyle, "border_size", element.style.strokeSize), 0.0f, 12.0f);
            element.style.shadowEnabled = jsonutil::JsonBoolOr(legacyStyle, "shadow", element.style.shadowEnabled);
            element.style.shadow = HexToColor(jsonutil::JsonStringOr(legacyStyle, "shadow_color", ColorToHex(element.style.shadow)), element.style.shadow);
            element.style.shadowAlpha = std::clamp(jsonutil::JsonNumberOr(legacyStyle, "shadow_alpha", element.style.shadowAlpha), 0.0f, 1.0f);
            element.style.shadowOffsetX = std::clamp(jsonutil::JsonNumberOr(legacyStyle, "shadow_offset_x", element.style.shadowOffsetX), -80.0f, 80.0f);
            element.style.shadowOffsetY = std::clamp(jsonutil::JsonNumberOr(legacyStyle, "shadow_offset_y", element.style.shadowOffsetY), -80.0f, 80.0f);
            element.style.rounding = std::clamp(jsonutil::JsonNumberOr(legacyStyle, "rounding", element.style.rounding), 0.0f, 40.0f);
        }
        widget.elements.push_back(std::move(element));
        return widget;
    }

    void LoadFromSection(const jsonutil::JsonObject& section, bool saveMigrations) {
        widgets.clear();
        selectedElementIds.clear();
        ResetCanvasInteraction(false);
        deprecatedHelperVisibilityMigrated = false;
        configMigratedToV2 = false;
        configMigratedToV3 = false;

        selectedWidgetId = jsonutil::JsonStringOr(&section, "selected_widget_id", "");
        const int schema = jsonutil::JsonNumberOr(&section, "schema_version", kLegacyHudSchemaVersion);
        if (schema > kHudSchemaVersion) {
            configSaveBlocked = true;
            statusMessage = UiSettings::Instance().Text(UiText::HudFutureSchemaReadOnly);
            debuglog::WriteError("[hud] future schema is read-only schema=%d supported=%d", schema, kHudSchemaVersion);
        }
        const hud_v3::EditorState editorState = hud_v3::DeserializeEditorState(
            jsonutil::JsonObjectOrNull(&section, "editor"),
            hud_v3::EditorState{
                BottomPanelToString(bottomPanel), bottomPanelHeight, bottomPanelCollapsed,
                PropertiesCategoryToString(propertiesCategory), snapEnabled, guidesEnabled, gridSize,
                editorCanvasHeight, editorCanvasAutoHeight,
            });
        bottomPanel = BottomPanelFromString(editorState.activePanel);
        bottomPanelHeight = editorState.panelHeight;
        bottomPanelCollapsed = editorState.panelCollapsed;
        propertiesCategory = PropertiesCategoryFromString(editorState.propertiesCategory);
        snapEnabled = editorState.snapEnabled;
        guidesEnabled = editorState.guidesEnabled;
        gridSize = editorState.gridSize;
        editorCanvasHeight = editorState.canvasHeight;
        editorCanvasAutoHeight = editorState.canvasAutoHeight;
        if (const jsonutil::JsonArray* array = jsonutil::JsonArrayOrNull(&section, "widgets")) {
            for (const jsonutil::JsonValue& value : *array) {
                const jsonutil::JsonObject* object = value.TryObject();
                if (!object) {
                    continue;
                }
                HudWidget widget = schema >= kPreviousHudSchemaVersion
                    ? DeserializeWidgetV2(*object)
                    : DeserializeLegacyWidget(*object);
                if (!widget.id.empty() && !FindWidget(widget.id)) {
                    widgets.push_back(std::move(widget));
                }
            }
        }

        if (!selectedWidgetId.empty() && !FindWidget(selectedWidgetId)) {
            selectedWidgetId.clear();
        }
        if (selectedWidgetId.empty() && !widgets.empty()) {
            selectedWidgetId = widgets.front().id;
        }
        const bool repaired = ValidateModel();
        configMigratedToV3 = schema < kHudSchemaVersion && section.find("widgets") != section.end();
        if (saveMigrations && !configSaveBlocked && (configMigratedToV2 || configMigratedToV3 || deprecatedHelperVisibilityMigrated || repaired)) {
            debuglog::WriteInfo("[hud] migrated config to schema v3 source=%d widgets=%zu", schema, widgets.size());
            QueueSave();
        }
        InvalidateAllElementOrderCaches();
    }

    void LoadConfig() {
        EnsureAssetDirectories();
        const jsonutil::JsonObject section = AppConfig::Instance().ReadSectionObject(kHudSectionName);
        const int schema = jsonutil::JsonNumberOr(&section, "schema_version", kLegacyHudSchemaVersion);
        const bool needsBackup = !section.empty() && section.find("widgets") != section.end() && schema < kHudSchemaVersion;
        configSaveBlocked = schema > kHudSchemaVersion;
        migrationBackupFailed = needsBackup && !WriteMigrationBackup(section, schema);
        if (migrationBackupFailed) {
            configSaveBlocked = true;
            statusMessage = UiSettings::Instance().Text(UiText::HudMigrationBackupFailed);
        }
        LoadFromSection(section, true);
        lastCommittedSnapshot = Snapshot();
        debuglog::WriteInfo("[hud] config loaded schema=%d widgets=%zu", kHudSchemaVersion, widgets.size());
    }

    void QueueSave() const {
        if (configSaveBlocked) {
            return;
        }
        AppConfig::Instance().QueueSectionReplace(std::string(kHudSectionName), SerializeConfig());
    }

    DocumentSnapshot Snapshot() const {
        jsonutil::JsonObject document;
        document["schema_version"] = kHudSchemaVersion;
        jsonutil::JsonArray widgetArray;
        for (const HudWidget& widget : widgets) {
            widgetArray.emplace_back(SerializeWidget(widget));
        }
        document["widgets"] = std::move(widgetArray);
        return document;
    }

    void PushUndoSnapshot(DocumentSnapshot snapshot) {
        if (snapshot.empty()) {
            return;
        }
        undoStack.push_back(std::move(snapshot));
        if (undoStack.size() > kUndoLimit) {
            undoStack.erase(undoStack.begin());
        }
        redoStack.clear();
    }

    void BeginEditorFrame() {
        if (lastCommittedSnapshot.empty()) {
            lastCommittedSnapshot = Snapshot();
        }
        if (editTransactionActive && !ImGui::IsAnyItemActive() && canvasMode != CanvasInteractionMode::Dragging && canvasMode != CanvasInteractionMode::Resizing) {
            const double beginMs = HudPerfNowMs();
            if (editTransactionChanged) {
                QueueSave();
                lastCommittedSnapshot = Snapshot();
            }
            editTransactionActive = false;
            editTransactionChanged = false;
            textSavePending = false;
            lastSaveTransactionMs = HudPerfNowMs() - beginMs;
        }
        if (textSavePending && TickNow() >= textSaveDueAtMs && editTransactionChanged) {
            const double beginMs = HudPerfNowMs();
            QueueSave();
            textSavePending = false;
            lastSaveTransactionMs = HudPerfNowMs() - beginMs;
        }
    }

    void MarkChanged(bool orderChanged = false, HudWidget* affectedWidget = nullptr) {
        EnsureLoaded();
        if (!editTransactionActive) {
            PushUndoSnapshot(lastCommittedSnapshot.empty() ? Snapshot() : lastCommittedSnapshot);
            editTransactionActive = true;
        }
        editTransactionChanged = true;
        if (!affectedWidget) {
            affectedWidget = SelectedWidget();
        }
        if (affectedWidget) {
            affectedWidget->nextRefreshAtMs = 0;
            if (orderChanged) InvalidateElementOrderCache(*affectedWidget);
        } else if (orderChanged) {
            InvalidateAllElementOrderCaches();
        }
        textSavePending = true;
        textSaveDueAtMs = TickNow() + kHudTextSaveDebounceMs;
    }

    bool RestoreSnapshot(const DocumentSnapshot& snapshot) {
        if (snapshot.empty()) {
            debuglog::WriteError("[hud] failed to restore empty editor snapshot");
            return false;
        }
        const std::string preservedWidgetId = selectedWidgetId;
        LoadFromSection(snapshot, false);
        if (!preservedWidgetId.empty() && FindWidget(preservedWidgetId)) {
            selectedWidgetId = preservedWidgetId;
        }
        selectedElementIds.clear();
        QueueSave();
        lastCommittedSnapshot = Snapshot();
        editTransactionActive = false;
        editTransactionChanged = false;
        textSavePending = false;
        return true;
    }

    void Undo() {
        if (undoStack.empty()) {
            return;
        }
        DocumentSnapshot current = Snapshot();
        DocumentSnapshot previous = std::move(undoStack.back());
        undoStack.pop_back();
        redoStack.push_back(std::move(current));
        RestoreSnapshot(previous);
    }

    void Redo() {
        if (redoStack.empty()) {
            return;
        }
        DocumentSnapshot current = Snapshot();
        DocumentSnapshot next = std::move(redoStack.back());
        redoStack.pop_back();
        undoStack.push_back(std::move(current));
        RestoreSnapshot(next);
    }

    std::string SourceText(HudElement& element, fs::path& imageRoot) {
        element.noteMissing = false;
        imageRoot = HudImagesDirectory();
        if (element.data.sourceMode == SourceMode::Inline) {
            return element.data.text;
        }
        if (!notepadModule || element.data.noteId.empty()) {
            element.noteMissing = true;
            return {};
        }
        NotepadModule::NoteContent note;
        if (!notepadModule->TryGetNote(element.data.noteId, note)) {
            element.noteMissing = true;
            return {};
        }
        imageRoot = notepadModule->ImagesDirectoryPath();
        return note.text;
    }

    fs::path ImageRootForElement(HudElement& element) {
        if (element.type == ElementType::TextMarkup && element.data.sourceMode == SourceMode::NotepadNote && notepadModule) {
            NotepadModule::NoteContent note;
            if (notepadModule->TryGetNote(element.data.noteId, note)) {
                return notepadModule->ImagesDirectoryPath();
            }
        }
        return HudImagesDirectory();
    }

    std::string ExpandText(std::string_view source) const {
        return tagsModule ? tagsModule->ExpandHudText(std::string(source)) : std::string(source);
    }

    bool StaticCacheHit(const HudElement& element, std::string_view source) const {
        return element.cachedStaticValueReady
            && element.cachedStaticType == element.type
            && element.cachedStaticSource.size() == source.size()
            && std::equal(source.begin(), source.end(), element.cachedStaticSource.begin());
    }

    void StoreStaticCache(HudElement& element, std::string_view source) const {
        element.cachedStaticType = element.type;
        element.cachedStaticSource.assign(source.begin(), source.end());
        element.cachedStaticValueReady = true;
    }

    void ClearStaticCache(HudElement& element) const {
        element.cachedStaticValueReady = false;
        element.cachedStaticSource.clear();
        element.cachedCoarseClockUntilMs = 0;
    }

    bool TryUseCoarseClockTextCache(HudElement& element, std::string_view source) {
        const CoarseClockRefresh refresh = ClassifyCoarseClockHudTags(source);
        if (refresh == CoarseClockRefresh::None) {
            return false;
        }
        if (refresh == CoarseClockRefresh::Dynamic) {
            element.cachedCoarseClockUntilMs = 0;
            return false;
        }

        const std::uint64_t now = TickNow();
        if (StaticCacheHit(element, source) && now < element.cachedCoarseClockUntilMs) {
            ++lastOverlayStats.staticRefreshSkips;
            return true;
        }

        element.cachedText = ExpandText(source);
        StoreStaticCache(element, source);
        element.cachedCoarseClockUntilMs = now + CoarseClockRefreshDelayMs(refresh);
        return true;
    }

    bool TryUseStaticTextCache(HudElement& element, std::string_view source) {
        if (MightContainHudTag(source)) {
            ClearStaticCache(element);
            return false;
        }
        if (StaticCacheHit(element, source)) {
            ++lastOverlayStats.staticRefreshSkips;
            return true;
        }
        element.cachedText.assign(source.begin(), source.end());
        StoreStaticCache(element, source);
        return true;
    }

    bool TryUseStaticImagePathCache(HudElement& element, std::string_view source) {
        if (MightContainHudTag(source)) {
            ClearStaticCache(element);
            return false;
        }
        if (StaticCacheHit(element, source)) {
            ++lastOverlayStats.staticRefreshSkips;
            return true;
        }
        element.cachedImagePath.assign(source.begin(), source.end());
        StoreStaticCache(element, source);
        return true;
    }

    bool TryUseStaticNumberCache(HudElement& element, std::string_view source) {
        if (MightContainHudTag(source)) {
            ClearStaticCache(element);
            return false;
        }
        if (StaticCacheHit(element, source)) {
            ++lastOverlayStats.staticRefreshSkips;
            return true;
        }
        element.cachedText.assign(source.begin(), source.end());
        element.cachedNumber = EvaluateNumberExpression(element.cachedText, element.data.defaultValue);
        StoreStaticCache(element, source);
        return true;
    }

    void RefreshWidgetCache(HudWidget& widget) {
        const std::uint64_t now = TickNow();
        if (now < widget.nextRefreshAtMs) {
            return;
        }

        ++lastOverlayStats.refreshedWidgets;
        for (HudElement& element : widget.elements) {
            if (element.hidden || VisibilityBlocked(element.visibility)) {
                continue;
            }
            if (element.type == ElementType::Text) {
                if (TryUseCoarseClockTextCache(element, element.data.text)
                    || TryUseStaticTextCache(element, element.data.text)) {
                    continue;
                }
                ++lastOverlayStats.expandedElements;
                element.cachedText = ExpandText(element.data.text);
            } else if (element.type == ElementType::TextMarkup) {
                fs::path imageRoot;
                const std::string source = SourceText(element, imageRoot);
                if (element.data.sourceMode == SourceMode::Inline && TryUseStaticTextCache(element, source)) {
                    continue;
                }
                ClearStaticCache(element);
                ++lastOverlayStats.expandedElements;
                element.cachedText = ExpandText(source);
            } else if (element.type == ElementType::Image) {
                if (TryUseStaticImagePathCache(element, element.data.imagePath)) {
                    continue;
                }
                ++lastOverlayStats.expandedElements;
                element.cachedImagePath = ExpandText(element.data.imagePath);
            } else if (element.type == ElementType::ProgressBar) {
                if (TryUseStaticNumberCache(element, element.data.expression)) {
                    continue;
                }
                ++lastOverlayStats.expandedElements;
                const std::string expression = ExpandText(element.data.expression);
                element.cachedText = expression;
                element.cachedNumber = EvaluateNumberExpression(expression, element.data.defaultValue);
            }
        }

        widget.nextRefreshAtMs = widget.refreshMs <= 0
            ? 0
            : now + static_cast<std::uint64_t>(widget.refreshMs);
    }

    void PrepareConditionSnapshot() {
        conditionSnapshot.BeginFrame();
    }

    bool VisibilityBlocked(const HudVisibility& visibility) const {
        return conditionSnapshot.IsBlocked(visibility.conditions, sampApi);
    }

    bool WidgetVisible(HudWidget& widget) const {
        return widget.enabled && !VisibilityBlocked(widget.visibility);
    }

    bool ElementVisible(const HudElement& element) const {
        return !element.hidden && !VisibilityBlocked(element.visibility);
    }

    float CanvasScale(const HudWidget& widget, const ImVec2& displaySize) const {
        const float uiScale = UiSettings::Instance().CurrentScale();
        const float widthScale = displaySize.x > 0.0f ? displaySize.x / kVirtualWidth : 1.0f;
        const float heightScale = displaySize.y > 0.0f ? displaySize.y / kVirtualHeight : 1.0f;
        switch (widget.scalePolicy) {
        case ScalePolicy::ScaleWithWidth:
            return std::max(0.01f, uiScale * widthScale);
        case ScalePolicy::ScaleWithHeight:
            return std::max(0.01f, uiScale * heightScale);
        case ScalePolicy::ScaleUniform:
            return std::max(0.01f, uiScale * std::min(widthScale, heightScale));
        case ScalePolicy::Fixed:
        default:
            return std::max(0.01f, uiScale);
        }
    }

    ImVec2 ScreenPosition(const HudWidget& widget, const ImVec2& displaySize) const {
        const float xScale = displaySize.x / kVirtualWidth;
        const float yScale = displaySize.y / kVirtualHeight;
        const ImVec2 base = AnchorBase(widget.position.anchor, displaySize);
        return ImVec2(
            base.x + widget.position.offsetX * xScale,
            base.y + widget.position.offsetY * yScale);
    }

    void UpdateOffsetFromWindowPos(HudWidget& widget, const ImVec2& displaySize, const ImVec2& windowPos, const ImVec2& windowSize) {
        const ImVec2 base = AnchorBase(widget.position.anchor, displaySize);
        const ImVec2 pivot = AnchorPivot(widget.position.anchor);
        const ImVec2 pivotPos(
            windowPos.x + windowSize.x * pivot.x,
            windowPos.y + windowSize.y * pivot.y);
        const float xScale = displaySize.x / kVirtualWidth;
        const float yScale = displaySize.y / kVirtualHeight;
        widget.position.offsetX = (pivotPos.x - base.x) / std::max(0.001f, xScale);
        widget.position.offsetY = (pivotPos.y - base.y) / std::max(0.001f, yScale);
    }

    ImRect ElementRect(const HudElement& element, const ImVec2& origin, float scale) const {
        return ImRect(
            ImVec2(origin.x + element.x * scale, origin.y + element.y * scale),
            ImVec2(origin.x + (element.x + element.width) * scale, origin.y + (element.y + element.height) * scale));
    }

    void DrawShape(ImDrawList* drawList, const HudElement& element, const ImRect& rect, float scale, bool editor) const {
        const float rounding = element.style.rounding * scale;
        if (element.style.shadowEnabled) {
            const ImVec2 offset(element.style.shadowOffsetX * scale, element.style.shadowOffsetY * scale);
            drawList->AddRectFilled(
                ImVec2(rect.Min.x + offset.x, rect.Min.y + offset.y),
                ImVec2(rect.Max.x + offset.x, rect.Max.y + offset.y),
                ColorU32(element.style.shadow, element.style.shadowAlpha * element.opacity),
                rounding);
        }
        if (element.style.fillEnabled) {
            drawList->AddRectFilled(rect.Min, rect.Max, ColorU32(element.style.fill, element.style.fillAlpha * element.opacity), rounding);
        }
        if (element.style.strokeEnabled || editor) {
            drawList->AddRect(
                rect.Min,
                rect.Max,
                ColorU32(element.style.stroke, (editor && !element.style.strokeEnabled ? 0.35f : element.style.strokeAlpha) * element.opacity),
                rounding,
                0,
                std::max(1.0f, element.style.strokeSize * scale));
        }
    }

    void DrawTextLine(
        ImDrawList* drawList,
        const HudElement& element,
        const ImRect& rect,
        const char* begin,
        const char* end,
        ImVec2 pos,
        ImFont* font,
        float fontSize,
        float scale,
        const ImVec4& clip) const {
        const ImU32 textColor = ColorU32(element.style.text, element.style.textAlpha * element.opacity);
        pos = ui_fonts::SnapPixel(pos);
        if (element.style.shadowEnabled) {
            const ImVec2 offset(
                ui_fonts::SnapPixel(element.style.shadowOffsetX * scale),
                ui_fonts::SnapPixel(element.style.shadowOffsetY * scale));
            drawList->AddText(
                font,
                fontSize,
                ImVec2(pos.x + offset.x, pos.y + offset.y),
                ColorU32(element.style.shadow, element.style.shadowAlpha * element.opacity),
                begin,
                end,
                0.0f,
                &clip);
        }
        if (element.style.outlineEnabled && element.style.outlineSize > 0.0f) {
            const float outline = ui_fonts::RoundFontSize(std::max(1.0f, element.style.outlineSize * scale));
            const ImU32 outlineColor = ColorU32(element.style.outline, element.style.outlineAlpha * element.opacity);
            constexpr std::array<ImVec2, 8> kOffsets = {
                ImVec2(-1.0f, -1.0f), ImVec2(0.0f, -1.0f), ImVec2(1.0f, -1.0f),
                ImVec2(-1.0f, 0.0f),                         ImVec2(1.0f, 0.0f),
                ImVec2(-1.0f, 1.0f),  ImVec2(0.0f, 1.0f),  ImVec2(1.0f, 1.0f),
            };
            for (const ImVec2& offset : kOffsets) {
                drawList->AddText(
                    font,
                    fontSize,
                    ImVec2(pos.x + offset.x * outline, pos.y + offset.y * outline),
                    outlineColor,
                    begin,
                    end,
                    0.0f,
                    &clip);
            }
        }
        drawList->AddText(font, fontSize, pos, textColor, begin, end, 0.0f, &clip);
        (void)rect;
    }

    void DrawTextElement(ImDrawList* drawList, const HudElement& element, const ImRect& rect, float scale) const {
        if (element.style.fillEnabled || element.style.strokeEnabled || element.style.shadowEnabled) {
            DrawShape(drawList, element, rect, scale, false);
        }
        ImFont* font = ImGui::GetFont();
        const float fontSize = ui_fonts::RoundFontSize(std::max(1.0f, static_cast<float>(element.data.fontSize) * scale));
        const float lineHeight = ui_fonts::RoundFontSize(fontSize * 1.18f);
        const ImVec4 clip(rect.Min.x, rect.Min.y, rect.Max.x, rect.Max.y);
        const std::string& text = element.cachedText;
        std::size_t start = 0;
        float y = ui_fonts::SnapPixel(rect.Min.y);
        while (start <= text.size() && y < rect.Max.y) {
            const std::size_t newline = text.find('\n', start);
            const std::size_t end = newline == std::string::npos ? text.size() : newline;
            const char* begin = text.c_str() + start;
            const char* finish = text.c_str() + end;
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, rect.GetWidth(), 0.0f, begin, finish);
            float x = rect.Min.x;
            if (element.data.align == TextAlign::Center) {
                x = rect.Min.x + std::max(0.0f, (rect.GetWidth() - textSize.x) * 0.5f);
            } else if (element.data.align == TextAlign::Right) {
                x = rect.Max.x - textSize.x;
            }
            DrawTextLine(drawList, element, rect, begin, finish, ImVec2(ui_fonts::SnapPixel(x), y), font, fontSize, scale, clip);
            if (newline == std::string::npos) {
                break;
            }
            start = newline + 1;
            y += lineHeight;
        }
    }

    void DrawMarkupElement(HudElement& element, IDirect3DDevice9* device, const ImRect& rect, float scale) {
        if (element.style.fillEnabled || element.style.strokeEnabled || element.style.shadowEnabled) {
            DrawShape(ImGui::GetWindowDrawList(), element, rect, scale, false);
        }

        const ImVec2 previousCursor = ImGui::GetCursorScreenPos();
        const ImVec2 elementSize(std::max(1.0f, rect.GetWidth()), std::max(1.0f, rect.GetHeight()));
        constexpr ImGuiWindowFlags kMarkupWindowFlags =
            ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoInputs
            | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::PushID(element.id.c_str());
        ImGui::SetCursorScreenPos(ui_fonts::SnapPixel(rect.Min));
        ImGui::PushStyleColor(ImGuiCol_Text, WithAlpha(element.style.text, element.style.textAlpha * element.opacity));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::BeginChild("##hud_markup_rect", elementSize, ImGuiChildFlags_None, kMarkupWindowFlags)) {
            const ui_fonts::ScopedFontSize fontScope(std::max(1.0f, static_cast<float>(element.data.fontSize) * scale));
            MarkupRenderer::DrawOptions options{};
            options.wrapText = false;
            options.fontDirectiveScale = scale;
            renderer.DrawText(element.cachedText, device, ImageRootForElement(element), options);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::SetCursorScreenPos(previousCursor);
    }

    void DrawImageElement(ImDrawList* drawList, HudElement& element, IDirect3DDevice9* device, const ImRect& rect) {
        MarkupRenderer::ImageTexture texture{};
        const std::string path = element.cachedImagePath.empty() ? element.data.imagePath : element.cachedImagePath;
        if (!MarkupRenderer::IsSafeRelativeAssetPath(path)
            || !renderer.ResolveImageTexture(path, device, HudImagesDirectory(), texture)) {
            DrawShape(drawList, element, rect, 1.0f, true);
            const char* label = UiSettings::Instance().Text(UiText::HudElementImage);
            drawList->AddText(rect.Min, ColorU32(element.style.text, 0.65f), label);
            return;
        }

        ImVec2 imageMin = rect.Min;
        ImVec2 imageMax = rect.Max;
        ImVec2 uvMin(0.0f, 0.0f);
        ImVec2 uvMax(1.0f, 1.0f);
        const float rectW = std::max(1.0f, rect.GetWidth());
        const float rectH = std::max(1.0f, rect.GetHeight());
        const float texW = std::max(1.0f, static_cast<float>(texture.width));
        const float texH = std::max(1.0f, static_cast<float>(texture.height));
        const float rectRatio = rectW / rectH;
        const float texRatio = texW / texH;
        if (element.data.imageFit == ImageFit::Contain) {
            float w = rectW;
            float h = rectH;
            if (texRatio > rectRatio) {
                h = rectW / texRatio;
            } else {
                w = rectH * texRatio;
            }
            imageMin = ImVec2(rect.Min.x + (rectW - w) * 0.5f, rect.Min.y + (rectH - h) * 0.5f);
            imageMax = ImVec2(imageMin.x + w, imageMin.y + h);
        } else if (element.data.imageFit == ImageFit::Cover) {
            if (texRatio > rectRatio) {
                const float visibleW = texH * rectRatio;
                const float pad = (texW - visibleW) / (texW * 2.0f);
                uvMin.x = pad;
                uvMax.x = 1.0f - pad;
            } else {
                const float visibleH = texW / rectRatio;
                const float pad = (texH - visibleH) / (texH * 2.0f);
                uvMin.y = pad;
                uvMax.y = 1.0f - pad;
            }
        }
        drawList->AddImage(texture.textureId, imageMin, imageMax, uvMin, uvMax, ColorU32(element.style.tint, element.style.tintAlpha * element.opacity));
        if (element.style.strokeEnabled) {
            drawList->AddRect(rect.Min, rect.Max, ColorU32(element.style.stroke, element.style.strokeAlpha * element.opacity), element.style.rounding, 0, element.style.strokeSize);
        }
    }

    void DrawLineElement(ImDrawList* drawList, const HudElement& element, const ImRect& rect, float scale) const {
        drawList->AddLine(
            rect.Min,
            rect.Max,
            ColorU32(element.style.stroke, element.style.strokeAlpha * element.opacity),
            std::max(1.0f, element.style.strokeSize * scale));
    }

    void DrawIconElement(ImDrawList* drawList, HudElement& element, const ImRect& rect, float scale) const {
        if (element.cachedIconName != element.data.icon || element.cachedIconGlyph.empty()) {
            element.cachedIconName = element.data.icon;
            element.cachedIconGlyph = ResolveHudIconGlyph(element.data.icon);
        }
        const std::string& icon = element.cachedIconGlyph;
        ImFont* font = ImGui::GetFont();
        const float fontSize = std::min(rect.GetHeight(), std::max(1.0f, static_cast<float>(element.data.fontSize) * scale));
        const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, icon.c_str());
        ImVec2 pos(rect.Min.x, rect.Min.y + (rect.GetHeight() - size.y) * 0.5f);
        if (element.data.align == TextAlign::Center) {
            pos.x = rect.Min.x + (rect.GetWidth() - size.x) * 0.5f;
        } else if (element.data.align == TextAlign::Right) {
            pos.x = rect.Max.x - size.x;
        }
        const ImVec4 clip(rect.Min.x, rect.Min.y, rect.Max.x, rect.Max.y);
        drawList->AddText(font, fontSize, pos, ColorU32(element.style.text, element.style.textAlpha * element.opacity), icon.c_str(), nullptr, 0.0f, &clip);
    }

    void DrawProgressElement(ImDrawList* drawList, const HudElement& element, const ImRect& rect, float scale) const {
        DrawShape(drawList, element, rect, scale, false);
        const float minValue = element.data.minValue;
        const float maxValue = std::abs(element.data.maxValue - minValue) < 0.0001f ? minValue + 1.0f : element.data.maxValue;
        const float fraction = std::clamp((element.cachedNumber - minValue) / (maxValue - minValue), 0.0f, 1.0f);
        ImRect fillRect = rect;
        fillRect.Max.x = fillRect.Min.x + fillRect.GetWidth() * fraction;
        drawList->AddRectFilled(
            fillRect.Min,
            fillRect.Max,
            ColorU32(element.style.progressFill, element.style.progressFillAlpha * element.opacity),
            element.style.rounding * scale);
    }

    bool DrawElement(HudElement& element, IDirect3DDevice9* device, const ImVec2& origin, float scale, bool editor) {
        if (!ElementVisible(element)) {
            return false;
        }
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect rect = ElementRect(element, origin, scale);
        switch (element.type) {
        case ElementType::Text:
            DrawTextElement(drawList, element, rect, scale);
            break;
        case ElementType::TextMarkup:
            DrawMarkupElement(element, device, rect, scale);
            break;
        case ElementType::Image:
            DrawImageElement(drawList, element, device, rect);
            break;
        case ElementType::Shape:
        case ElementType::Group:
            DrawShape(drawList, element, rect, scale, editor && element.type == ElementType::Group);
            break;
        case ElementType::Line:
            DrawLineElement(drawList, element, rect, scale);
            break;
        case ElementType::Icon:
            DrawIconElement(drawList, element, rect, scale);
            break;
        case ElementType::ProgressBar:
            DrawProgressElement(drawList, element, rect, scale);
            break;
        }
        return true;
    }

    void InvalidateElementOrderCache(HudWidget& widget) const {
        widget.elementsByZAscCache.clear();
        widget.elementsByZDescCache.clear();
        widget.elementsByZAscDirty = true;
        widget.elementsByZDescDirty = true;
    }

    void InvalidateAllElementOrderCaches() {
        for (HudWidget& widget : widgets) {
            InvalidateElementOrderCache(widget);
        }
    }

    const std::vector<HudElement*>& ElementsByZ(HudWidget& widget, bool descending = false) {
        std::vector<HudElement*>* cache = descending ? &widget.elementsByZDescCache : &widget.elementsByZAscCache;
        bool* dirty = descending ? &widget.elementsByZDescDirty : &widget.elementsByZAscDirty;
        if (*dirty || cache->size() != widget.elements.size()) {
            cache->clear();
            cache->reserve(widget.elements.size());
            for (HudElement& element : widget.elements) {
                cache->push_back(&element);
            }
            std::sort(cache->begin(), cache->end(), [&](const HudElement* left, const HudElement* right) {
                return descending ? left->z > right->z : left->z < right->z;
            });
            *dirty = false;
        }

        return *cache;
    }

    std::vector<HudElement*> ElementsByZSnapshot(HudWidget& widget, bool descending = false) {
        const std::vector<HudElement*>& cache = ElementsByZ(widget, descending);
        return std::vector<HudElement*>(cache.begin(), cache.end());
    }

    void DrawCanvas(HudWidget& widget, IDirect3DDevice9* device, const ImVec2& origin, float scale, bool editor) {
        RefreshWidgetCache(widget);
        for (HudElement* element : ElementsByZ(widget)) {
            ++lastOverlayStats.elements;
            if (DrawElement(*element, device, origin, scale, editor)) {
                ++lastOverlayStats.visibleElements;
            }
        }
    }

    void DrawWidgetOverlay(HudWidget& widget, IDirect3DDevice9* device) {
        if (!WidgetVisible(widget)) {
            return;
        }
        ++lastOverlayStats.visibleWidgets;

        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 displaySize = io.DisplaySize;
        if (displaySize.x <= 0.0f || displaySize.y <= 0.0f) {
            return;
        }

        const bool placing = placementMode && placementWidgetId == widget.id && !placementInputBlocked;
        const float scale = CanvasScale(widget, displaySize);
        const ImVec2 canvasSize(widget.canvasWidth * scale, widget.canvasHeight * scale);
        const ImVec2 pos = ScreenPosition(widget, displaySize);
        const ImVec2 pivot = AnchorPivot(widget.position.anchor);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
        ImGui::SetNextWindowSize(canvasSize, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, placing ? ScaleUi(1.0f) : 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.78f, 1.0f, 1.0f));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoNav
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoBackground;
        if (!placing) {
            flags |= ImGuiWindowFlags_NoInputs;
        }

        const std::string windowId = "##hud_widget_" + widget.id;
        if (ImGui::Begin(windowId.c_str(), nullptr, flags)) {
            const ImVec2 origin = ImGui::GetWindowPos();
            DrawCanvas(widget, device, origin, scale, placing);
            if (placing) {
                ImGui::SetCursorScreenPos(origin);
                ImGui::InvisibleButton("##hud_drag_surface", canvasSize);
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                    if (!placementUndoCaptured) {
                        PushUndoSnapshot(lastCommittedSnapshot.empty() ? Snapshot() : lastCommittedSnapshot);
                        placementUndoCaptured = true;
                    }
                    const ImVec2 currentPos = ImGui::GetWindowPos();
                    UpdateOffsetFromWindowPos(widget, displaySize, ImVec2(currentPos.x + io.MouseDelta.x, currentPos.y + io.MouseDelta.y), canvasSize);
                }
                if (ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsMouseReleased(0)) {
                    QueueSave();
                    lastCommittedSnapshot = Snapshot();
                    placementUndoCaptured = false;
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    void DrawOverlay(IDirect3DDevice9* device) {
        EnsureLoaded();
        PrepareConditionSnapshot();
        lastOverlayStats = {};
        lastOverlayStats.widgets = static_cast<int>(widgets.size());
        for (HudWidget& widget : widgets) {
            if (widget.enabled) {
                ++lastOverlayStats.enabledWidgets;
                if (widget.refreshMs <= 0) {
                    ++lastOverlayStats.refreshEveryFrameWidgets;
                }
            }
            DrawWidgetOverlay(widget, device);
        }
    }

    bool WantsOverlayRender() {
        EnsureLoaded();
        return placementMode || std::any_of(widgets.begin(), widgets.end(), [](const HudWidget& widget) {
            return widget.enabled;
        });
    }

    bool WantsInputCapture() const {
        return placementMode && !placementInputBlocked;
    }

    bool OnWindowMessage(UINT message, WPARAM wparam, LPARAM) {
        if (!placementMode || placementInputBlocked) {
            return false;
        }
        if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wparam == VK_ESCAPE) {
            placementMode = false;
            placementWidgetId.clear();
            return true;
        }
        return false;
    }

    bool DrawAnchorCombo(HudWidget& widget) {
        UiSettings& ui = UiSettings::Instance();
        bool changed = false;
        if (ImGui::BeginCombo("##hud_anchor", AnchorLabel(widget.position.anchor, ui))) {
            constexpr Anchor anchors[] = {
                Anchor::TopLeft,
                Anchor::TopCenter,
                Anchor::TopRight,
                Anchor::CenterLeft,
                Anchor::Center,
                Anchor::CenterRight,
                Anchor::BottomLeft,
                Anchor::BottomCenter,
                Anchor::BottomRight,
            };
            for (Anchor anchor : anchors) {
                const bool selected = widget.position.anchor == anchor;
                if (ImGui::Selectable(AnchorLabel(anchor, ui), selected)) {
                    widget.position.anchor = anchor;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    bool DrawScalePolicyCombo(HudWidget& widget) {
        UiSettings& ui = UiSettings::Instance();
        const auto labelFor = [&](ScalePolicy policy) -> const char* {
            switch (policy) {
            case ScalePolicy::ScaleWithWidth: return ui.Text(UiText::HudScalePolicyWidth);
            case ScalePolicy::ScaleWithHeight: return ui.Text(UiText::HudScalePolicyHeight);
            case ScalePolicy::ScaleUniform: return ui.Text(UiText::HudScalePolicyUniform);
            case ScalePolicy::Fixed:
            default:
                return ui.Text(UiText::HudScalePolicyFixed);
            }
        };

        bool changed = false;
        if (ImGui::BeginCombo("##hud_scale_policy", labelFor(widget.scalePolicy))) {
            constexpr ScalePolicy policies[] = {
                ScalePolicy::Fixed,
                ScalePolicy::ScaleWithWidth,
                ScalePolicy::ScaleWithHeight,
                ScalePolicy::ScaleUniform,
            };
            for (ScalePolicy policy : policies) {
                const bool selected = widget.scalePolicy == policy;
                if (ImGui::Selectable(labelFor(policy), selected)) {
                    widget.scalePolicy = policy;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    bool DrawImageFitCombo(HudElement& element) {
        UiSettings& ui = UiSettings::Instance();
        const auto labelFor = [&](ImageFit fit) -> const char* {
            switch (fit) {
            case ImageFit::Cover: return ui.Text(UiText::HudImageFitCover);
            case ImageFit::Stretch: return ui.Text(UiText::HudImageFitStretch);
            case ImageFit::Contain:
            default:
                return ui.Text(UiText::HudImageFitContain);
            }
        };
        bool changed = false;
        if (ImGui::BeginCombo("##hud_image_fit", labelFor(element.data.imageFit))) {
            constexpr ImageFit fits[] = { ImageFit::Contain, ImageFit::Cover, ImageFit::Stretch };
            for (ImageFit fit : fits) {
                const bool selected = element.data.imageFit == fit;
                if (ImGui::Selectable(labelFor(fit), selected)) {
                    element.data.imageFit = fit;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    bool DrawTextAlignCombo(HudElement& element) {
        UiSettings& ui = UiSettings::Instance();
        const auto labelFor = [&](TextAlign align) -> const char* {
            switch (align) {
            case TextAlign::Center: return ui.Text(UiText::HudAlignCenter);
            case TextAlign::Right: return ui.Text(UiText::HudAlignRight);
            case TextAlign::Left:
            default:
                return ui.Text(UiText::HudAlignLeft);
            }
        };
        bool changed = false;
        if (ImGui::BeginCombo("##hud_align", labelFor(element.data.align))) {
            constexpr TextAlign aligns[] = { TextAlign::Left, TextAlign::Center, TextAlign::Right };
            for (TextAlign align : aligns) {
                const bool selected = element.data.align == align;
                if (ImGui::Selectable(labelFor(align), selected)) {
                    element.data.align = align;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    void DrawAddElementMenu(const char* popupId) {
        UiSettings& ui = UiSettings::Instance();
        if (!ImGui::BeginPopup(popupId)) {
            return;
        }
        constexpr ElementType types[] = {
            ElementType::Text,
            ElementType::Image,
            ElementType::Shape,
            ElementType::Line,
            ElementType::Icon,
            ElementType::ProgressBar,
        };
        for (ElementType type : types) {
            if (ImGui::MenuItem(ui.Text(ElementTypeLabelId(type)))) {
                AddElement(type);
            }
        }
        ImGui::EndPopup();
    }

    void DrawPresetMenu(const char* popupId) {
        UiSettings& ui = UiSettings::Instance();
        if (ImGui::BeginPopup(popupId)) {
            if (ImGui::MenuItem(ui.Text(UiText::HudPresetWeapon))) AddWidget(MakeWeaponPreset());
            if (ImGui::MenuItem(ui.Text(UiText::HudPresetFreeText))) AddWidget(MakeFreeTextPreset());
            if (ImGui::MenuItem(ui.Text(UiText::HudPresetPlayerStatus))) AddWidget(MakePlayerStatusPreset());
            if (ImGui::MenuItem(ui.Text(UiText::HudPresetVehicle))) AddWidget(MakeVehiclePreset());
            if (ImGui::MenuItem(ui.Text(UiText::HudPresetNoteCard))) AddWidget(MakeNotePreset());
            if (ImGui::MenuItem(ui.Text(UiText::HudPresetTimer))) AddWidget(MakeTimerPreset());
            if (ImGui::MenuItem(ui.Text(UiText::HudPresetDashboard))) AddWidget(MakeDashboardPreset());
            ImGui::EndPopup();
        }
    }

    void DuplicateToolbarTarget() {
        if (!selectedElementIds.empty()) {
            DuplicateSelectedElements();
            return;
        }
        DuplicateSelectedWidget();
    }

    void DeleteToolbarTarget() {
        if (!selectedElementIds.empty()) {
            DeleteSelectedElements();
            return;
        }
        if (HudWidget* widget = SelectedWidget()) {
            pendingDeleteWidgetId = widget->id;
            deleteWidgetConfirmPending = true;
        }
    }

    void DrawAlignMenuItems() {
        UiSettings& ui = UiSettings::Instance();
        HudWidget* widget = SelectedWidget();
        const std::size_t editable = widget ? EditableSelectionRoots(*widget).size() : 0;
        if (ImGui::MenuItem(ui.Text(UiText::HudArrangeLeft), nullptr, false, editable >= 2)) AlignSelectedElements(HudAlignAction::Left);
        if (ImGui::MenuItem(ui.Text(UiText::HudArrangeHCenter), nullptr, false, editable >= 2)) AlignSelectedElements(HudAlignAction::HorizontalCenter);
        if (ImGui::MenuItem(ui.Text(UiText::HudArrangeRight), nullptr, false, editable >= 2)) AlignSelectedElements(HudAlignAction::Right);
        ImGui::Separator();
        if (ImGui::MenuItem(ui.Text(UiText::HudArrangeTop), nullptr, false, editable >= 2)) AlignSelectedElements(HudAlignAction::Top);
        if (ImGui::MenuItem(ui.Text(UiText::HudArrangeVCenter), nullptr, false, editable >= 2)) AlignSelectedElements(HudAlignAction::VerticalCenter);
        if (ImGui::MenuItem(ui.Text(UiText::HudArrangeBottom), nullptr, false, editable >= 2)) AlignSelectedElements(HudAlignAction::Bottom);
        ImGui::Separator();
        if (ImGui::MenuItem(ui.Text(UiText::HudDistributeHorizontal), nullptr, false, editable >= 3)) AlignSelectedElements(HudAlignAction::DistributeHorizontal);
        if (ImGui::MenuItem(ui.Text(UiText::HudDistributeVertical), nullptr, false, editable >= 3)) AlignSelectedElements(HudAlignAction::DistributeVertical);
    }

    void DrawEditMenu(const char* popupId) {
        UiSettings& ui = UiSettings::Instance();
        if (!ImGui::BeginPopup(popupId)) {
            return;
        }
        if (ImGui::MenuItem(ui.Text(UiText::HudDuplicateWidget))) {
            DuplicateToolbarTarget();
        }
        if (ImGui::MenuItem(ui.Text(UiText::Delete))) {
            DeleteToolbarTarget();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ui.Text(UiText::HudUndo), nullptr, false, !undoStack.empty())) {
            Undo();
        }
        if (ImGui::MenuItem(ui.Text(UiText::HudRedo), nullptr, false, !redoStack.empty())) {
            Redo();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ui.Text(UiText::HudGroup), nullptr, false, selectedElementIds.size() >= 2)) {
            GroupSelectedElements();
        }
        if (ImGui::MenuItem(ui.Text(UiText::HudUngroup), nullptr, false, !selectedElementIds.empty())) {
            UngroupSelectedElements();
        }
        if (ImGui::BeginMenu(ui.Text(UiText::HudAlignMenu), !selectedElementIds.empty())) {
            DrawAlignMenuItems();
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    void DrawFileMenu(const char* popupId) {
        UiSettings& ui = UiSettings::Instance();
        if (!ImGui::BeginPopup(popupId)) {
            return;
        }
        if (ImGui::MenuItem(ui.Text(UiText::HudExport), nullptr, false, SelectedWidget() != nullptr)) {
            ExportSelectedWidget();
        }
        if (ImGui::MenuItem(ui.Text(UiText::HudImport))) {
            ImportWidget();
        }
        ImGui::EndPopup();
    }

    void DrawViewMenu(const char* popupId) {
        UiSettings& ui = UiSettings::Instance();
        if (!ImGui::BeginPopup(popupId)) {
            return;
        }
        if (ImGui::Checkbox(ui.Text(UiText::HudSnap), &snapEnabled)) {
            QueueSave();
        }
        if (ImGui::Checkbox(ui.Text(UiText::HudGuides), &guidesEnabled)) {
            QueueSave();
        }
        ImGui::SetNextItemWidth(ScaleUi(120.0f));
        if (ImGui::DragFloat(ui.Text(UiText::HudGrid), &gridSize, 1.0f, 1.0f, 64.0f, "%.0f")) {
            QueueSave();
        }
        ImGui::Separator();
        if (ImGui::Checkbox(ui.Text(UiText::HudCanvasAutoHeight), &editorCanvasAutoHeight)) {
            QueueSave();
        }
        if (!editorCanvasAutoHeight) {
            ImGui::SetNextItemWidth(ScaleUi(120.0f));
            if (ImGui::DragFloat(ui.Text(UiText::HudCanvasEditorHeight), &editorCanvasHeight, 2.0f, 220.0f, 900.0f, "%.0f")) {
                editorCanvasHeight = std::clamp(editorCanvasHeight, 220.0f, 900.0f);
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                QueueSave();
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ui.Text(UiText::HudZoomFit))) {
            canvasFitZoom = true;
            canvasPan = ImVec2(0.0f, 0.0f);
        }
        if (ImGui::MenuItem(ui.Text(UiText::HudZoom100))) {
            canvasFitZoom = false;
            canvasZoom = 1.0f;
            canvasPan = ImVec2(0.0f, 0.0f);
        }
        if (ImGui::MenuItem(ui.Text(UiText::HudZoomOut))) {
            canvasFitZoom = false;
            canvasZoom = std::max(0.10f, canvasZoom - 0.10f);
        }
        if (ImGui::MenuItem(ui.Text(UiText::HudZoomIn))) {
            canvasFitZoom = false;
            canvasZoom = std::min(4.0f, canvasZoom + 0.10f);
        }
        ImGui::EndPopup();
    }

    void DrawToolbar() {
        UiSettings& ui = UiSettings::Instance();
        HudWidget* widget = SelectedWidget();
        const HudEditorVisualStyle visual = HudEditorStyleTokens();
        const float headerHeight = ImGui::GetFrameHeight() * 2.0f + ScaleUi(22.0f);
        if (!BeginHudPanel("hud_editor_topbar", ImVec2(0.0f, headerHeight), visual)) {
            EndHudPanel();
            return;
        }

        const ImVec2 headerPos = ImGui::GetCursorScreenPos();
        const float headerWidth = ImGui::GetContentRegionAvail().x;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            headerPos,
            ImVec2(headerPos.x + headerWidth, headerPos.y + ImGui::GetFrameHeight()),
            ImGui::GetColorU32(visual.toolbarBg),
            ScaleUi(5.0f));

        ImGui::TextColored(visual.headerText, "%s", ui.Text(UiText::TabHud));
        if (widget) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", widget->enabled ? ui.Text(UiText::HudOn) : ui.Text(UiText::HudOff));
            ImGui::SameLine();
            const std::string widgetName = EllipsizeText(widget->name, std::max(ScaleUi(80.0f), headerWidth * 0.30f));
            ImGui::TextUnformatted(widgetName.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ui.Format(UiText::HudCanvasSizeFormat, widget->canvasWidth, widget->canvasHeight).c_str());
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ui.Text(UiText::HudNoSelection));
        }
        if (!statusMessage.empty()) {
            const std::string status = EllipsizeText(statusMessage, std::max(ScaleUi(80.0f), headerWidth * 0.34f));
            const float statusWidth = ImGui::CalcTextSize(status.c_str()).x;
            const float statusX = headerPos.x + headerWidth - statusWidth - ScaleUi(4.0f);
            if (statusX > ImGui::GetCursorScreenPos().x + ScaleUi(16.0f)) {
                ImGui::SameLine();
                ImGui::SetCursorScreenPos(ImVec2(statusX, ImGui::GetCursorScreenPos().y));
                ImGui::TextDisabled("%s", status.c_str());
            }
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ScaleUi(7.0f));
        const float buttonH = ImGui::GetFrameHeight();
        const float gap = ScaleUi(5.0f);
        const bool compact = ImGui::GetContentRegionAvail().x < ScaleUi(980.0f);
        auto drawButton = [&](const char* icon, UiText labelId, const char* id, bool enabled, bool primary = false) {
            const char* label = ui.Text(labelId);
            const float width = HudTextActionButtonWidth(icon, label);
            const bool clicked = HudTextActionButton(icon, label, id, nullptr, ImVec2(width, buttonH), visual, enabled, primary);
            ImGui::SameLine(0.0f, gap);
            return clicked;
        };

        if (compact) {
            if (drawButton(ui_icons::Plus, UiText::HudToolbarCreate, "##hud_create_top", true, true)) {
                ImGui::OpenPopup("##hud_create_top_popup");
            }
            if (ImGui::BeginPopup("##hud_create_top_popup")) {
                if (ImGui::MenuItem(ui.Text(UiText::HudAddWidget))) AddWidget(MakeDefaultWidget());
                if (ImGui::BeginMenu(ui.Text(UiText::HudAddElement), widget != nullptr)) {
                    constexpr ElementType types[] = { ElementType::Text, ElementType::Image, ElementType::Shape, ElementType::Line, ElementType::Icon, ElementType::ProgressBar };
                    for (ElementType type : types) if (ImGui::MenuItem(ui.Text(ElementTypeLabelId(type)))) AddElement(type);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(ui.Text(UiText::HudPresets))) {
                    if (ImGui::MenuItem(ui.Text(UiText::HudPresetWeapon))) AddWidget(MakeWeaponPreset());
                    if (ImGui::MenuItem(ui.Text(UiText::HudPresetFreeText))) AddWidget(MakeFreeTextPreset());
                    if (ImGui::MenuItem(ui.Text(UiText::HudPresetPlayerStatus))) AddWidget(MakePlayerStatusPreset());
                    if (ImGui::MenuItem(ui.Text(UiText::HudPresetVehicle))) AddWidget(MakeVehiclePreset());
                    if (ImGui::MenuItem(ui.Text(UiText::HudPresetNoteCard))) AddWidget(MakeNotePreset());
                    if (ImGui::MenuItem(ui.Text(UiText::HudPresetTimer))) AddWidget(MakeTimerPreset());
                    if (ImGui::MenuItem(ui.Text(UiText::HudPresetDashboard))) AddWidget(MakeDashboardPreset());
                    ImGui::EndMenu();
                }
                ImGui::EndPopup();
            }
        } else {
            if (drawButton(ui_icons::Plus, UiText::HudAddWidget, "##hud_add_widget_top", true, true)) AddWidget(MakeDefaultWidget());
            if (drawButton(ui_icons::Plus, UiText::HudAddElement, "##hud_add_element_top", widget != nullptr, false)) ImGui::OpenPopup("##hud_add_element_top_popup");
            DrawAddElementMenu("##hud_add_element_top_popup");
            if (drawButton(ui_icons::Sliders, UiText::HudPresets, "##hud_presets_top", true, false)) ImGui::OpenPopup("##hud_presets_top_popup");
            DrawPresetMenu("##hud_presets_top_popup");
        }
        if (drawButton(ui_icons::Edit, UiText::HudToolbarEdit, "##hud_edit_menu_top", widget != nullptr, false)) {
            ImGui::OpenPopup("##hud_edit_menu_top_popup");
        }
        DrawEditMenu("##hud_edit_menu_top_popup");
        const bool wideToolbar = headerWidth >= ScaleUi(1080.0f);
        if (wideToolbar) {
            if (drawButton(nullptr, UiText::HudUndo, "##hud_undo_top", !undoStack.empty(), false)) Undo();
            if (drawButton(nullptr, UiText::HudRedo, "##hud_redo_top", !redoStack.empty(), false)) Redo();
        }
        if (drawButton(ui_icons::FileExport, UiText::HudToolbarFile, "##hud_file_menu_top", true, false)) {
            ImGui::OpenPopup("##hud_file_menu_top_popup");
        }
        DrawFileMenu("##hud_file_menu_top_popup");
        if (drawButton(ui_icons::Sliders, UiText::HudToolbarView, "##hud_view_menu_top", true, false)) {
            ImGui::OpenPopup("##hud_view_menu_top_popup");
        }
        DrawViewMenu("##hud_view_menu_top_popup");

        if (drawButton(nullptr, UiText::HudHelpTitle, "##hud_help_top", true, false)) {
            helpWindowOpen = true;
        }
        if (placementMode && drawButton(nullptr, UiText::Cancel, "##hud_cancel_placement_top", true, false)) {
            placementMode = false;
            placementWidgetId.clear();
            placementUndoCaptured = false;
        }

        if (!compact) {
            const float fitWidth = HudTextActionButtonWidth(nullptr, ui.Text(UiText::HudZoomFit));
            if (HudTextActionButton(nullptr, ui.Text(UiText::HudZoomFit), "##hud_zoom_fit_top", nullptr, ImVec2(fitWidth, buttonH), visual)) {
                canvasFitZoom = true;
                canvasPan = ImVec2(0.0f, 0.0f);
            }
            ImGui::SameLine(0.0f, gap);
            const float zoom100Width = HudTextActionButtonWidth(nullptr, ui.Text(UiText::HudZoom100));
            if (HudTextActionButton(nullptr, ui.Text(UiText::HudZoom100), "##hud_zoom_100_top", nullptr, ImVec2(zoom100Width, buttonH), visual)) {
                canvasFitZoom = false;
                canvasZoom = 1.0f;
                canvasPan = ImVec2(0.0f, 0.0f);
            }
            ImGui::SameLine(0.0f, gap);
            if (HudFlatIconButton(ui_icons::AngleDown, "##hud_zoom_out_top", ui.Text(UiText::HudZoomOut), ImVec2(buttonH, buttonH), visual)) {
                canvasFitZoom = false;
                canvasZoom = std::max(0.10f, canvasZoom - 0.10f);
            }
            ImGui::SameLine(0.0f, gap);
            if (HudFlatIconButton(ui_icons::AngleUp, "##hud_zoom_in_top", ui.Text(UiText::HudZoomIn), ImVec2(buttonH, buttonH), visual)) {
                canvasFitZoom = false;
                canvasZoom = std::min(4.0f, canvasZoom + 0.10f);
            }
        }
        EndHudPanel();
    }

    void DrawWidgetList(float height = 0.0f) {
        UiSettings& ui = UiSettings::Instance();
        const HudEditorVisualStyle visual = HudEditorStyleTokens();
        if (BeginHudPanel("hud_widget_panel", ImVec2(0.0f, height), visual)) {
            const float headerH = ImGui::GetFrameHeight();
            const ImVec2 headerMin = ImGui::GetCursorScreenPos();
            const float headerW = ImGui::GetContentRegionAvail().x;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddText(headerMin, ImGui::GetColorU32(visual.headerText), ui.Text(UiText::HudWidgets));
            const std::string countText = std::to_string(widgets.size());
            const ImVec2 countSize = ImGui::CalcTextSize(countText.c_str());
            const ImRect countRect(
                ImVec2(headerMin.x + headerW - countSize.x - ScaleUi(13.0f), headerMin.y + ScaleUi(3.0f)),
                ImVec2(headerMin.x + headerW, headerMin.y + headerH - ScaleUi(3.0f)));
            drawList->AddRectFilled(countRect.Min, countRect.Max, ImGui::GetColorU32(WithAlpha(visual.buttonHover, 0.34f)), ScaleUi(8.0f));
            drawList->AddText(ImVec2(countRect.Min.x + ScaleUi(6.0f), headerMin.y + ScaleUi(4.0f)), ImGui::GetColorU32(visual.mutedText), countText.c_str());
            ImGui::Dummy(ImVec2(headerW, headerH));

            DrawHudSearchBox("##hud_search", ui.Text(UiText::HudSearchHint), searchQuery, visual);
            ImGui::Spacing();

            if (widgets.empty()) {
                ImGui::TextWrapped("%s", ui.Text(UiText::HudNoWidgets));
                const float buttonH = ImGui::GetFrameHeight();
                const char* label = ui.Text(UiText::HudAddWidget);
                if (HudTextActionButton(ui_icons::Plus, label, "##hud_add_widget_empty", nullptr, ImVec2(HudTextActionButtonWidth(ui_icons::Plus, label), buttonH), visual, true, true)) {
                    AddWidget(MakeDefaultWidget());
                }
            } else if (ImGui::BeginChild("hud_widget_rows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
                drawList = ImGui::GetWindowDrawList();
                const std::string needle = LowerAscii(searchQuery);
                int rowIndex = 0;
                const float rowH = ScaleUi(42.0f);
                const float iconSide = ScaleUi(26.0f);
                const float gap = ScaleUi(7.0f);
                std::string duplicateRequest;
                std::vector<HudWidget*> visibleWidgets;
                visibleWidgets.reserve(widgets.size());
                for (HudWidget& candidate : widgets) {
                    if (needle.empty() || LowerAscii(candidate.name).find(needle) != std::string::npos) visibleWidgets.push_back(&candidate);
                }
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(visibleWidgets.size()), rowH);
                while (clipper.Step()) {
                    for (int visibleIndex = clipper.DisplayStart; visibleIndex < clipper.DisplayEnd; ++visibleIndex) {
                    HudWidget& widget = *visibleWidgets[static_cast<std::size_t>(visibleIndex)];
                    rowIndex = visibleIndex;
                    ImGui::PushID(widget.id.c_str());
                    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                    const float rowW = ImGui::GetContentRegionAvail().x;
                    const ImRect rowRect(rowMin, ImVec2(rowMin.x + rowW, rowMin.y + rowH));
                    const bool selected = widget.id == selectedWidgetId;
                    const bool hovered = ImGui::IsMouseHoveringRect(rowRect.Min, rowRect.Max, true);
                    DrawHudRowBackground(drawList, rowRect, rowIndex, selected, hovered, visual);

                    ImGui::SetCursorScreenPos(ImVec2(rowRect.Min.x + ScaleUi(6.0f), rowRect.Min.y + ScaleUi(8.0f)));
                    const ImVec4 toggleColor = widget.enabled ? visual.accent : visual.faintText;
                    if (HudFlatIconButton(widget.enabled ? ui_icons::ToggleOn : ui_icons::ToggleOff, "##widget_enabled", ui.Text(UiText::Enabled), ImVec2(iconSide, iconSide), visual, toggleColor)) {
                        widget.enabled = !widget.enabled;
                        MarkChanged(false, &widget);
                    }

                    const float textX = rowRect.Min.x + ScaleUi(6.0f) + iconSide + gap;
                    const float actionSide = ScaleUi(26.0f);
                    const float textMaxW = std::max(1.0f, rowRect.Max.x - textX - actionSide - ScaleUi(12.0f));
                    ImGui::SetCursorScreenPos(ImVec2(textX, rowRect.Min.y));
                    if (ImGui::InvisibleButton("##widget_select", ImVec2(textMaxW, rowH))) {
                        selectedWidgetId = widget.id;
                        selectedElementIds.clear();
                        ResetCanvasInteraction(true);
                        QueueSave();
                    }

                    ImGui::SetCursorScreenPos(ImVec2(rowRect.Max.x - actionSide - ScaleUi(5.0f), rowRect.Min.y + ScaleUi(8.0f)));
                    if (HudFlatIconButton(ui_icons::Sliders, "##widget_actions", ui.Text(UiText::HudMoreActions), ImVec2(actionSide, actionSide), visual)) {
                        ImGui::OpenPopup("##widget_actions_popup");
                    }
                    if (ImGui::BeginPopup("##widget_actions_popup")) {
                        if (ImGui::MenuItem(ui.Text(UiText::HudDuplicateWidget))) {
                            duplicateRequest = widget.id;
                        }
                        if (ImGui::MenuItem(ui.Text(UiText::Delete))) {
                            pendingDeleteWidgetId = widget.id;
                            deleteWidgetConfirmPending = true;
                        }
                        ImGui::EndPopup();
                    }

                    const std::string title = EllipsizeText(widget.name, textMaxW);
                    const std::string meta = EllipsizeText(
                        ui.Format(UiText::HudWidgetMetaFormat, widget.canvasWidth, widget.canvasHeight, static_cast<int>(widget.elements.size())),
                        textMaxW);
                    drawList->AddText(
                        ImVec2(textX, rowRect.Min.y + ScaleUi(6.0f)),
                        ImGui::GetColorU32(selected ? visual.headerText : visual.mutedText),
                        title.c_str());
                    drawList->AddText(
                        ImVec2(textX, rowRect.Min.y + ScaleUi(23.0f)),
                        ImGui::GetColorU32(visual.faintText),
                        meta.c_str());
                    ImGui::SetCursorScreenPos(ImVec2(rowRect.Min.x, rowRect.Max.y));
                    ImGui::PopID();
                    ++rowIndex;
                    }
                }
                ImGui::EndChild();
                if (!duplicateRequest.empty()) {
                    selectedWidgetId = duplicateRequest;
                    selectedElementIds.clear();
                    DuplicateSelectedWidget();
                }
            }
        }
        EndHudPanel();
    }

    void DrawStyleProperties(HudElement& element) {
        UiSettings& ui = UiSettings::Instance();
        bool changed = false;
        changed |= ImGui::SliderFloat(ui.Text(UiText::HudOpacity), &element.opacity, 0.0f, 1.0f, "%.2f");
        ImGui::SeparatorText(ui.Text(UiText::HudStyle));
        changed |= ImGui::Checkbox(ui.Text(UiText::HudFill), &element.style.fillEnabled);
        changed |= ImGui::ColorEdit3(ui.Text(UiText::HudFillColor), &element.style.fill.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::SliderFloat(ui.Text(UiText::HudFillAlpha), &element.style.fillAlpha, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::Checkbox(ui.Text(UiText::HudStroke), &element.style.strokeEnabled);
        changed |= ImGui::ColorEdit3(ui.Text(UiText::HudStrokeColor), &element.style.stroke.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::SliderFloat(ui.Text(UiText::HudStrokeAlpha), &element.style.strokeAlpha, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::DragFloat(ui.Text(UiText::HudStrokeSize), &element.style.strokeSize, 0.1f, 0.0f, 32.0f, "%.1f");
        changed |= ImGui::DragFloat(ui.Text(UiText::HudRounding), &element.style.rounding, 0.5f, 0.0f, 80.0f, "%.1f");
        changed |= ImGui::ColorEdit3(ui.Text(UiText::HudTextColor), &element.style.text.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::SliderFloat(ui.Text(UiText::HudTextAlpha), &element.style.textAlpha, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::Checkbox(ui.Text(UiText::HudShadow), &element.style.shadowEnabled);
        changed |= ImGui::ColorEdit3(ui.Text(UiText::HudShadowColor), &element.style.shadow.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::SliderFloat(ui.Text(UiText::HudShadowAlpha), &element.style.shadowAlpha, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::DragFloat(ui.Text(UiText::HudShadowOffsetX), &element.style.shadowOffsetX, 0.5f, -120.0f, 120.0f, "%.1f");
        changed |= ImGui::DragFloat(ui.Text(UiText::HudShadowOffsetY), &element.style.shadowOffsetY, 0.5f, -120.0f, 120.0f, "%.1f");
        changed |= ImGui::Checkbox(ui.Text(UiText::HudOutline), &element.style.outlineEnabled);
        changed |= ImGui::ColorEdit3(ui.Text(UiText::HudOutlineColor), &element.style.outline.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::DragFloat(ui.Text(UiText::HudOutlineSize), &element.style.outlineSize, 0.1f, 0.0f, 12.0f, "%.1f");
        if (element.type == ElementType::Image) {
            changed |= ImGui::ColorEdit3(ui.Text(UiText::HudTint), &element.style.tint.x, ImGuiColorEditFlags_NoInputs);
            changed |= ImGui::SliderFloat(ui.Text(UiText::HudTintAlpha), &element.style.tintAlpha, 0.0f, 1.0f, "%.2f");
        }
        if (element.type == ElementType::ProgressBar) {
            changed |= ImGui::ColorEdit3(ui.Text(UiText::HudProgressFill), &element.style.progressFill.x, ImGuiColorEditFlags_NoInputs);
            changed |= ImGui::SliderFloat(ui.Text(UiText::HudProgressFillAlpha), &element.style.progressFillAlpha, 0.0f, 1.0f, "%.2f");
        }
        if (changed) {
            MarkChanged();
        }
    }

    void ApplyTextMode(HudElement& element, HudTextMode mode) {
        const HudTextMode oldMode = TextModeForElement(element);
        if (oldMode == mode) {
            return;
        }

        switch (mode) {
        case HudTextMode::Plain:
            element.type = ElementType::Text;
            element.data.sourceMode = SourceMode::Inline;
            break;
        case HudTextMode::Markup:
            element.type = ElementType::TextMarkup;
            element.data.sourceMode = SourceMode::Inline;
            break;
        case HudTextMode::Notepad:
            element.type = ElementType::TextMarkup;
            element.data.sourceMode = SourceMode::NotepadNote;
            break;
        }
        element.cachedText.clear();
        element.cachedStaticSource.clear();
        element.cachedCoarseClockUntilMs = 0;
        element.cachedStaticValueReady = false;
        MarkChanged();
    }

    bool DrawTextModeCombo(HudElement& element) {
        UiSettings& ui = UiSettings::Instance();
        HudTextMode mode = TextModeForElement(element);
        bool changed = false;
        if (ImGui::BeginCombo("##hud_text_mode", TextModeLabel(mode, ui))) {
            constexpr HudTextMode modes[] = {
                HudTextMode::Plain,
                HudTextMode::Markup,
                HudTextMode::Notepad,
            };
            for (HudTextMode candidate : modes) {
                const bool selected = mode == candidate;
                if (ImGui::Selectable(TextModeLabel(candidate, ui), selected)) {
                    ApplyTextMode(element, candidate);
                    mode = candidate;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    void InsertIntoString(std::string& value, int& cursor, bool cursorValid, std::string_view text) {
        const std::string insertion(text);
        if (!cursorValid) {
            value += insertion;
            cursor = static_cast<int>(value.size());
            return;
        }
        const int safeCursor = std::clamp(cursor, 0, static_cast<int>(value.size()));
        value.insert(static_cast<std::size_t>(safeCursor), insertion);
        cursor = safeCursor + static_cast<int>(insertion.size());
    }

    bool TryInsertTextTarget(HudElement& element, HudInsertTarget target, std::string_view text) {
        if (target == HudInsertTarget::Text) {
            const bool cursorValid = textInsertCursorValid && textInsertElementId == element.id;
            InsertIntoString(element.data.text, textInsertCursor, cursorValid, text);
            textInsertCursorValid = true;
            textInsertElementId = element.id;
            return true;
        }
        if (target == HudInsertTarget::Markup) {
            const bool cursorValid = markupInsertCursorValid && markupInsertElementId == element.id;
            InsertIntoString(element.data.text, markupInsertCursor, cursorValid, text);
            markupInsertCursorValid = true;
            markupInsertElementId = element.id;
            return true;
        }
        return false;
    }

    void AppendToInsertTarget(HudInsertTarget target, std::string_view text) {
        HudWidget* widget = SelectedWidget();
        if (!widget || target == HudInsertTarget::None) {
            ImGui::SetClipboardText(std::string(text).c_str());
            return;
        }
        HudElement* element = PrimarySelectedElement(*widget);
        if (!element) {
            ImGui::SetClipboardText(std::string(text).c_str());
            return;
        }

        if (TryInsertTextTarget(*element, target, text)) {
            MarkChanged();
            return;
        }

        switch (target) {
        case HudInsertTarget::Text:
        case HudInsertTarget::Markup:
            element->data.text += text;
            break;
        case HudInsertTarget::ImagePath:
            element->data.imagePath += text;
            break;
        case HudInsertTarget::ProgressExpression:
            element->data.expression += text;
            break;
        case HudInsertTarget::None:
        default:
            return;
        }
        MarkChanged();
    }

    void AppendToActiveInsertTarget(std::string_view text) {
        AppendToInsertTarget(activeInsertTarget, text);
    }

    void InsertIntoMarkupTarget(std::string_view text) {
        activeInsertTarget = HudInsertTarget::Markup;
        AppendToInsertTarget(HudInsertTarget::Markup, text);
    }

    void OpenVariablesPopup(HudInsertTarget target) {
        activeInsertTarget = target;
        variablesPopupPending = true;
    }

    void DrawVariablesButton(HudInsertTarget target, float requestedWidth = 0.0f) {
        UiSettings& ui = UiSettings::Instance();
        const HudEditorVisualStyle visual = HudEditorStyleTokens();
        const char* label = ui.Text(UiText::HudVariables);
        const float width = requestedWidth > 0.0f
            ? requestedWidth
            : std::min(HudTextActionButtonWidth(ui_icons::Tags, label), ImGui::GetContentRegionAvail().x);
        if (HudTextActionButton(
                ui_icons::Tags,
                label,
                "##hud_variables_button",
                nullptr,
                ImVec2(width, ImGui::GetFrameHeight()),
                visual)) {
            OpenVariablesPopup(target);
        }
    }

    std::vector<variables_picker::Entry> BuildHudVariableEntries() const {
        if (!tagsModule) {
            return {};
        }
        std::vector<variables_picker::Entry> entries = tagsModule->BuildVariablePickerEntriesForInsert();
        entries.erase(
            std::remove_if(entries.begin(), entries.end(), [](const variables_picker::Entry& entry) {
                return entry.action;
            }),
            entries.end());
        return entries;
    }

    void HandleHudVariablePickerRequest(const variables_picker::Request& request) {
        if (!tagsModule) {
            return;
        }
        switch (request.type) {
        case variables_picker::RequestType::Insert:
            AppendToActiveInsertTarget(request.text);
            break;
        case variables_picker::RequestType::Copy:
            ImGui::SetClipboardText(request.text.c_str());
            break;
        case variables_picker::RequestType::OpenKeyEmulatePicker:
        case variables_picker::RequestType::OpenDialogItemPicker:
        case variables_picker::RequestType::OpenArizonaDialogItemPicker:
        case variables_picker::RequestType::OpenDialogTextPicker:
        case variables_picker::RequestType::OpenArizonaDialogTextPicker:
            tagsModule->HandleVariablePickerUtilityRequest(request);
            break;
        case variables_picker::RequestType::None:
        case variables_picker::RequestType::SaveCustom:
        case variables_picker::RequestType::DeleteCustom:
        default:
            break;
        }
    }

    void DrawVariablesPopup() {
        if (!tagsModule) {
            return;
        }
        const std::string title = std::string(UiSettings::Instance().Text(UiText::HudVariablesTitle)) + "##hud_variables_popup";
        if (variablesPopupPending) {
            variablesPickerState.search.clear();
            variablesPopupEntries = BuildHudVariableEntries();
            debuglog::WriteInfo(
                "[hud][editor] variables picker opened target=%d entries=%zu",
                static_cast<int>(activeInsertTarget),
                variablesPopupEntries.size());
            ImGui::OpenPopup(title.c_str());
            variablesPopupPending = false;
        }

        bool open = true;
        ImGui::SetNextWindowSize(ScaleUi(900.0f, 620.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal(title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
            return;
        }

        bool closeRequested = !open;
        const variables_picker::Request request = variables_picker::Draw(
            variablesPickerState,
            variablesPopupEntries,
            variables_picker::Options{
                variables_picker::Mode::Insert,
                "hud_variables_picker",
                activeInsertTarget != HudInsertTarget::None,
                false,
                true,
                ImGui::GetContentRegionAvail(),
            });
        HandleHudVariablePickerRequest(request);
        closeRequested |= request.closePopupAfterAction;

        tagsModule->DrawVariableHelperPopups([&](std::string_view token) {
            AppendToActiveInsertTarget(token);
            closeRequested = true;
        });

        if (closeRequested) {
            activeInsertTarget = HudInsertTarget::None;
            variablesPopupEntries.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    bool DrawMarkupToolButton(const char* label, std::string_view insertion, float requestedWidth = 0.0f) {
        const HudEditorVisualStyle visual = HudEditorStyleTokens();
        const float width = requestedWidth > 0.0f ? requestedWidth : HudTextActionButtonWidth(nullptr, label);
        if (!HudTextActionButton(nullptr, label, label, nullptr, ImVec2(width, ImGui::GetFrameHeight()), visual)) {
            return false;
        }
        InsertIntoMarkupTarget(insertion);
        return true;
    }

    void DrawMarkupToolbar() {
        UiSettings& ui = UiSettings::Instance();
        const HudEditorVisualStyle visual = HudEditorStyleTokens();
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const int columns = availableWidth >= ScaleUi(1120.0f) ? 8 : availableWidth >= ScaleUi(560.0f) ? 4 : 2;
        const std::string iconPickerPopup = std::string(ui.Text(UiText::IconPickerTitle)) + "##hud_markup_icon_picker";
        if (ImGui::BeginTable("##hud_markup_toolbar", columns,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableNextColumn();
            if (HudTextActionButton(nullptr, ui.Text(UiText::HudMarkupColor), "##hud_markup_color", nullptr,
                    ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()), visual)) {
                InsertIntoMarkupTarget("{FFFFFF}");
            }

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##hud_markup_font", ui.Text(UiText::HudMarkupFont))) {
                constexpr int sizes[] = { 12, 14, 16, 18, 30 };
                for (int size : sizes) {
                    const std::string label = "#font" + std::to_string(size);
                    if (ImGui::Selectable(label.c_str())) InsertIntoMarkupTarget(label);
                }
                ImGui::EndCombo();
            }

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##hud_markup_align", ui.Text(UiText::HudMarkupAlign))) {
                if (ImGui::Selectable("#left")) InsertIntoMarkupTarget("#left ");
                if (ImGui::Selectable("#center")) InsertIntoMarkupTarget("#center ");
                if (ImGui::Selectable("#right")) InsertIntoMarkupTarget("#right ");
                ImGui::EndCombo();
            }

            ImGui::TableNextColumn();
            DrawMarkupToolButton(ui.Text(UiText::HudMarkupLine), "\n#hr\n", ImGui::GetContentRegionAvail().x);
            ImGui::TableNextColumn();
            DrawMarkupToolButton(ui.Text(UiText::HudMarkupBreak), "\n#br\n", ImGui::GetContentRegionAvail().x);
            ImGui::TableNextColumn();
            if (HudTextActionButton(ui_icons::Star, ui.Text(UiText::HudMarkupIcon), "##hud_markup_icon", nullptr,
                    ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()), visual)) {
                icon_picker::OpenPopup(iconPickerPopup.c_str());
            }
            ImGui::TableNextColumn();
            if (HudTextActionButton(nullptr, ui.Text(UiText::HudMarkupImage), "##hud_markup_image", nullptr,
                    ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()), visual)) {
                if (const std::optional<std::string> image = CopyImageIntoHudProfile()) {
                    InsertIntoMarkupTarget("#img(" + *image + ")\n");
                }
            }
            ImGui::TableNextColumn();
            DrawVariablesButton(HudInsertTarget::Markup, ImGui::GetContentRegionAvail().x);
            ImGui::EndTable();
        }

        std::string selectedIconId;
        if (icon_picker::DrawPopup(iconPickerState, icon_picker::Options{ iconPickerPopup.c_str(), ImVec2(560.0f, 460.0f) }, selectedIconId)) {
            InsertIntoMarkupTarget(icon_picker::MarkupToken(selectedIconId) + " ");
        }
    }

    void DrawElementDataProperties(HudElement& element) {
        UiSettings& ui = UiSettings::Instance();
        bool changed = false;
        if (element.type == ElementType::Text) {
            ImGui::TextDisabled("%s", ui.Text(UiText::HudTextMode));
            DrawTextModeCombo(element);
            ImGui::Spacing();
            changed |= InputTextMultilineString(
                "##hud_element_text",
                element.data.text,
                ScaleUi(0.0f, 110.0f),
                0,
                &textInsertCursor,
                &textInsertCursorValid);
            if (ImGui::IsItemActive()) {
                activeInsertTarget = HudInsertTarget::Text;
                textInsertElementId = element.id;
            }
            DrawVariablesButton(HudInsertTarget::Text);
        } else if (element.type == ElementType::TextMarkup) {
            ImGui::TextDisabled("%s", ui.Text(UiText::HudTextMode));
            DrawTextModeCombo(element);
            ImGui::Spacing();
            if (element.data.sourceMode == SourceMode::Inline) {
                DrawMarkupToolbar();
                ImGui::Spacing();
                changed |= InputTextMultilineString(
                    "##hud_markup_text",
                    element.data.text,
                    ScaleUi(0.0f, 120.0f),
                    0,
                    &markupInsertCursor,
                    &markupInsertCursorValid);
                if (ImGui::IsItemActive()) {
                    activeInsertTarget = HudInsertTarget::Markup;
                    markupInsertElementId = element.id;
                }
                if (ContainsHudActionTag(element.data.text)) {
                    ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "%s", ui.Text(UiText::HudActionTagsDisabled));
                }
            } else {
                const std::vector<NotepadModule::NoteSummary> notes = notepadModule ? notepadModule->NoteSummaries() : std::vector<NotepadModule::NoteSummary>{};
                std::string currentLabel = ui.Text(UiText::HudLinkedNoteMissing);
                for (const auto& note : notes) {
                    if (note.id == element.data.noteId) {
                        currentLabel = note.folderPath.empty() ? note.title : note.folderPath + " / " + note.title;
                        break;
                    }
                }
                if (ImGui::BeginCombo("##hud_note_combo", currentLabel.c_str())) {
                    for (const auto& note : notes) {
                        const std::string label = note.folderPath.empty() ? note.title : note.folderPath + " / " + note.title;
                        const bool selected = note.id == element.data.noteId;
                        ImGui::PushID(note.id.c_str());
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            element.data.noteId = note.id;
                            changed = true;
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                if (element.noteMissing) {
                    ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "%s", ui.Text(UiText::HudLinkedNoteMissing));
                }
            }
        } else if (element.type == ElementType::Image) {
            changed |= InputTextString("##hud_image_path", element.data.imagePath, 0, 256);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ui.Text(UiText::HudImagePath));
            if (ImGui::Button((std::string(ui_icons::Image) + " " + ui.Text(UiText::HudInsertImage)).c_str())) {
                if (const std::optional<std::string> image = CopyImageIntoHudProfile()) {
                    element.data.imagePath = *image;
                    changed = true;
                }
            }
            ImGui::SameLine();
            DrawVariablesButton(HudInsertTarget::ImagePath);
            changed |= DrawImageFitCombo(element);
            if (!MarkupRenderer::IsSafeRelativeAssetPath(element.data.imagePath)) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "%s", ui.Text(UiText::HudUnsafeImagePath));
            }
        } else if (element.type == ElementType::Icon) {
            changed |= InputTextString("##hud_icon_name", element.data.icon, 0, 64);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ui.Text(UiText::HudIconName));
            ImGui::SameLine();
            const std::string iconPickerPopup = std::string(ui.Text(UiText::IconPickerTitle)) + "##hud_element_icon_picker";
            if (ImGui::Button((std::string(ResolveHudIconGlyph(element.data.icon)) + " " + ui.Text(UiText::IconPickerSelect)).c_str())) {
                icon_picker::OpenPopup(iconPickerPopup.c_str());
            }
            std::string selectedIconId;
            if (icon_picker::DrawPopup(iconPickerState, icon_picker::Options{ iconPickerPopup.c_str(), ImVec2(560.0f, 460.0f) }, selectedIconId)) {
                element.data.icon = icon_registry::NormalizeIconId(selectedIconId);
                changed = true;
            }
        } else if (element.type == ElementType::ProgressBar) {
            changed |= InputTextString("##hud_expression", element.data.expression, 0, 256);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ui.Text(UiText::HudExpression));
            DrawVariablesButton(HudInsertTarget::ProgressExpression);
            ImGui::SeparatorText(ui.Text(UiText::HudProgressRange));
            if (ImGui::BeginTable("##hud_progress_range", 3,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
                const auto rangeField = [&](int column, UiText label, const char* id, float* value) {
                    ImGui::TableSetColumnIndex(column);
                    ImGui::TextDisabled("%s", ui.Text(label));
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    changed |= ImGui::DragFloat(id, value, 1.0f, -100000.0f, 100000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
                };
                ImGui::TableNextRow();
                rangeField(0, UiText::HudMin, "##hud_progress_min", &element.data.minValue);
                rangeField(1, UiText::HudMax, "##hud_progress_max", &element.data.maxValue);
                rangeField(2, UiText::HudDefaultValue, "##hud_progress_default", &element.data.defaultValue);
                ImGui::EndTable();
            }
        }
        if (element.type == ElementType::Text || element.type == ElementType::TextMarkup || element.type == ElementType::Icon) {
            changed |= ImGui::SliderInt(ui.Text(UiText::HudFontSize), &element.data.fontSize, 8, 96);
            changed |= DrawTextAlignCombo(element);
        }
        if (changed) {
            MarkChanged();
        }
    }

    void DrawWidgetMainInspector(HudWidget& widget) {
        UiSettings& ui = UiSettings::Instance();
        const HudEditorVisualStyle visual = HudEditorStyleTokens();
        bool changed = false;
        ImGui::PushID(widget.id.c_str());
        ImGui::PushID("widget_main");
        ImGui::TextColored(visual.mutedText, "%s", ui.Text(UiText::HudWidgetSection));
        changed |= ImGui::Checkbox(ui.Text(UiText::Enabled), &widget.enabled);
        changed |= InputTextString(ui.Text(UiText::Name), widget.name, 0, 128);
        if (changed) {
            MarkChanged();
        }
        ImGui::PopID();
        ImGui::PopID();
    }

    bool ClampElementToCanvas(const HudWidget& widget, HudElement& element, float minimumHeight = 1.0f) const {
        const float canvasWidth = std::max(1.0f, widget.canvasWidth);
        const float canvasHeight = std::max(minimumHeight, widget.canvasHeight);
        const float width = std::clamp(element.width, 1.0f, canvasWidth);
        const float height = std::clamp(element.height, minimumHeight, canvasHeight);
        const float x = std::clamp(element.x, 0.0f, std::max(0.0f, canvasWidth - width));
        const float y = std::clamp(element.y, 0.0f, std::max(0.0f, canvasHeight - height));
        const bool changed = element.x != x || element.y != y || element.width != width || element.height != height;
        element.x = x;
        element.y = y;
        element.width = width;
        element.height = height;
        return changed;
    }

    bool DrawElementGeometryFields(HudWidget& widget, HudElement& element, const char* tableId, float minimumHeight) {
        bool changed = false;
        minimumHeight = std::max(0.0f, minimumHeight);
        changed |= ClampElementToCanvas(widget, element, minimumHeight);
        if (!ImGui::BeginTable(tableId, 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
            return changed;
        }

        const auto field = [&](int column, UiText label, const char* id, float* value, float minimum, float maximum) {
            ImGui::TableSetColumnIndex(column);
            ImGui::TextDisabled("%s", UiSettings::Instance().Text(label));
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::DragFloat(id, value, 1.0f, minimum, maximum, "%.0f", ImGuiSliderFlags_AlwaysClamp);
        };
        ImGui::TableNextRow();
        field(0, UiText::HudX, "##hud_geometry_x", &element.x, 0.0f, std::max(0.0f, widget.canvasWidth - element.width));
        field(1, UiText::HudY, "##hud_geometry_y", &element.y, 0.0f, std::max(0.0f, widget.canvasHeight - element.height));
        ImGui::TableNextRow();
        field(0, UiText::HudW, "##hud_geometry_w", &element.width, 1.0f, std::max(1.0f, widget.canvasWidth - element.x));
        field(1, UiText::HudH, "##hud_geometry_h", &element.height, minimumHeight, std::max(minimumHeight, widget.canvasHeight - element.y));
        ImGui::EndTable();
        if (changed) ClampElementToCanvas(widget, element, minimumHeight);
        return changed;
    }

    void DrawElementMainInspector(HudWidget& widget) {
        UiSettings& ui = UiSettings::Instance();
        HudElement* element = PrimarySelectedElement(widget);
        if (!element) {
            ImGui::TextWrapped("%s", ui.Text(UiText::HudNoElementSelection));
            return;
        }

        bool changed = false;
        ImGui::PushID(element->id.c_str());
        ImGui::PushID("element_main");
        ImGui::TextColored(HudEditorStyleTokens().mutedText, "%s", ui.Text(UiText::HudElementSection));
        changed |= InputTextString(ui.Text(UiText::Name), element->name, 0, 128);
        const HudTextMode textMode = TextModeForElement(*element);
        if (element->type == ElementType::Text || element->type == ElementType::TextMarkup) {
            ImGui::TextDisabled("%s", TextModeLabel(textMode, ui));
        } else {
            ImGui::TextDisabled("%s", ui.Text(ElementTypeLabelId(element->type)));
        }

        if (changed) {
            MarkChanged();
        }
        ImGui::PopID();
        ImGui::PopID();
    }

    void ReorderElementBefore(HudWidget& widget, std::string_view draggedId, std::string_view targetId) {
        if (draggedId.empty() || targetId.empty() || draggedId == targetId) {
            return;
        }

        std::vector<std::string> order;
        for (HudElement* element : ElementsByZ(widget, true)) {
            if (element->id != draggedId) {
                order.push_back(element->id);
            }
        }
        const auto targetIt = std::find(order.begin(), order.end(), targetId);
        if (targetIt == order.end()) {
            return;
        }
        order.insert(targetIt, std::string(draggedId));
        const int count = static_cast<int>(order.size());
        for (int i = 0; i < count; ++i) {
            if (HudElement* element = FindElement(widget, order[static_cast<std::size_t>(i)])) {
                element->z = count - i - 1;
            }
        }
        MarkChanged(true);
    }

    void RecomputeGroupBounds(HudWidget& widget, std::string_view groupId) {
        HudElement* group = FindElement(widget, groupId);
        if (!group || group->type != ElementType::Group) {
            return;
        }
        bool found = false;
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        for (const HudElement& child : widget.elements) {
            if (child.parentId != group->id) {
                continue;
            }
            if (!found) {
                minX = child.x;
                minY = child.y;
                maxX = child.x + child.width;
                maxY = child.y + child.height;
                found = true;
            } else {
                minX = std::min(minX, child.x);
                minY = std::min(minY, child.y);
                maxX = std::max(maxX, child.x + child.width);
                maxY = std::max(maxY, child.y + child.height);
            }
        }
        if (found) {
            group->x = minX;
            group->y = minY;
            group->width = std::max(1.0f, maxX - minX);
            group->height = std::max(1.0f, maxY - minY);
        }
    }

    void AssignElementToGroup(HudWidget& widget, std::string_view elementId, std::string_view groupId) {
        HudElement* element = FindElement(widget, elementId);
        HudElement* group = FindElement(widget, groupId);
        if (!element || !group || element->id == group->id || element->type == ElementType::Group || group->type != ElementType::Group) {
            return;
        }
        if (element->parentId == group->id) {
            return;
        }
        const std::string oldParentId = element->parentId;
        element->parentId = group->id;
        if (!oldParentId.empty()) {
            RecomputeGroupBounds(widget, oldParentId);
        }
        RecomputeGroupBounds(widget, group->id);
        MarkChanged();
    }

    void RemoveElementFromGroup(HudWidget& widget, std::string_view elementId) {
        HudElement* element = FindElement(widget, elementId);
        if (!element || element->parentId.empty()) {
            return;
        }
        const std::string oldParentId = element->parentId;
        element->parentId.clear();
        RecomputeGroupBounds(widget, oldParentId);
        MarkChanged();
    }

    void RemoveSelectedFromGroups() {
        HudWidget* widget = SelectedWidget();
        if (!widget) {
            return;
        }
        bool changed = false;
        std::unordered_set<std::string> affectedGroups;
        for (const std::string& id : selectedElementIds) {
            if (HudElement* element = FindElement(*widget, id); element && !element->parentId.empty()) {
                affectedGroups.insert(element->parentId);
                element->parentId.clear();
                changed = true;
            }
        }
        if (changed) {
            for (const std::string& groupId : affectedGroups) {
                RecomputeGroupBounds(*widget, groupId);
            }
            MarkChanged();
        }
    }

    void DrawLayers(HudWidget& widget, float height = 0.0f) {
        UiSettings& ui = UiSettings::Instance();
        const HudEditorVisualStyle visual = HudEditorStyleTokens();
        if (BeginHudPanel("hud_layers_panel", ImVec2(0.0f, height), visual)) {
            const float headerH = ImGui::GetFrameHeight();
            const ImVec2 headerMin = ImGui::GetCursorScreenPos();
            const float headerW = ImGui::GetContentRegionAvail().x;
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddText(headerMin, ImGui::GetColorU32(visual.headerText), ui.Text(UiText::HudLayers));

            const float buttonSide = headerH;
            ImGui::SetCursorScreenPos(ImVec2(headerMin.x + headerW - buttonSide * 2.0f - ScaleUi(4.0f), headerMin.y));
            if (HudFlatIconButton(ui_icons::Plus, "##hud_add_element_layers", ui.Text(UiText::HudAddElement), ImVec2(buttonSide, buttonSide), visual, visual.headerText)) {
                ImGui::OpenPopup("##hud_add_element_layers_popup");
            }
            DrawAddElementMenu("##hud_add_element_layers_popup");
            ImGui::SameLine(0.0f, ScaleUi(4.0f));
            if (HudFlatIconButton(ui_icons::Sliders, "##hud_layers_more", ui.Text(UiText::HudMoreActions), ImVec2(buttonSide, buttonSide), visual)) {
                ImGui::OpenPopup("##hud_layers_more_popup");
            }
            if (ImGui::BeginPopup("##hud_layers_more_popup")) {
                if (ImGui::MenuItem(ui.Text(UiText::HudGroup), nullptr, false, selectedElementIds.size() >= 2)) {
                    GroupSelectedElements();
                }
                if (ImGui::MenuItem(ui.Text(UiText::HudUngroup), nullptr, false, !selectedElementIds.empty())) {
                    UngroupSelectedElements();
                }
                if (ImGui::MenuItem(ui.Text(UiText::HudMoveToRoot), nullptr, false, !selectedElementIds.empty())) {
                    RemoveSelectedFromGroups();
                }
                ImGui::EndPopup();
            }
            ImGui::SetCursorScreenPos(ImVec2(headerMin.x, headerMin.y + headerH + ScaleUi(5.0f)));

            ImGui::SetNextItemWidth(-FLT_MIN);
            InputTextWithHintString("##hud_layer_search", ui.Text(UiText::HudLayerSearchHint), layerSearchQuery);
            ImGui::Spacing();

            if (widget.elements.empty()) {
                ImGui::TextWrapped("%s", ui.Text(UiText::HudNoLayers));
            } else if (ImGui::BeginChild("hud_layers_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
                drawList = ImGui::GetWindowDrawList();
                int rowIndex = 0;
                const float rowH = ScaleUi(32.0f);
                const float iconSide = ScaleUi(24.0f);
                const float gap = ScaleUi(5.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::Button(ui.Text(UiText::HudLayerRoot), ImVec2(-FLT_MIN, ScaleUi(24.0f)));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HBO_HUD_ELEMENT")) {
                        const char* draggedId = static_cast<const char*>(payload->Data);
                        RemoveElementFromGroup(widget, draggedId ? draggedId : "");
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopStyleColor();

                std::vector<HudElement*> ordered;
                const std::vector<HudElement*> zOrder = ElementsByZSnapshot(widget, true);
                ordered.reserve(zOrder.size());
                for (HudElement* root : zOrder) {
                    if (!root->parentId.empty()) {
                        continue;
                    }
                    ordered.push_back(root);
                    if (root->type == ElementType::Group) {
                        for (HudElement* child : zOrder) {
                            if (child->parentId == root->id) {
                                ordered.push_back(child);
                            }
                        }
                    }
                }
                const std::string layerNeedle = LowerAscii(TrimAscii(layerSearchQuery));
                std::string duplicateLayerRequest;
                std::string deleteLayerRequest;
                std::vector<HudElement*> visibleLayers;
                visibleLayers.reserve(ordered.size());
                for (HudElement* element : ordered) {
                    if (layerNeedle.empty() || LowerAscii(element->name).find(layerNeedle) != std::string::npos
                        || LowerAscii(ElementTypeToString(element->type)).find(layerNeedle) != std::string::npos) visibleLayers.push_back(element);
                }
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(visibleLayers.size()), rowH);
                while (clipper.Step()) {
                    for (int visibleIndex = clipper.DisplayStart; visibleIndex < clipper.DisplayEnd; ++visibleIndex) {
                    HudElement* element = visibleLayers[static_cast<std::size_t>(visibleIndex)];
                    rowIndex = visibleIndex;
                    ImGui::PushID(element->id.c_str());
                    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                    const float rowW = ImGui::GetContentRegionAvail().x;
                    const ImRect rowRect(rowMin, ImVec2(rowMin.x + rowW, rowMin.y + rowH));
                    const bool selected = IsElementSelected(element->id);
                    const bool hovered = ImGui::IsMouseHoveringRect(rowRect.Min, rowRect.Max, true);
                    DrawHudRowBackground(drawList, rowRect, rowIndex, selected, hovered, visual);

                    float x = rowRect.Min.x + ScaleUi(5.0f);
                    ImGui::SetCursorScreenPos(ImVec2(x, rowRect.Min.y + ScaleUi(4.0f)));
                    const ImVec4 visibilityColor = element->hidden ? visual.faintText : visual.accent;
                    if (HudFlatIconButton(element->hidden ? ui_icons::ToggleOff : ui_icons::ToggleOn, "##layer_visible", ui.Text(UiText::HudHidden), ImVec2(iconSide, iconSide), visual, visibilityColor)) {
                        element->hidden = !element->hidden;
                        MarkChanged();
                    }
                    x += iconSide + gap;

                    ImGui::SetCursorScreenPos(ImVec2(x, rowRect.Min.y + ScaleUi(4.0f)));
                    const ImVec4 lockColor = element->locked ? visual.accent : visual.faintText;
                    if (HudFlatIconButton(ui_icons::Keyboard, "##layer_locked", ui.Text(UiText::HudLocked), ImVec2(iconSide, iconSide), visual, lockColor)) {
                        element->locked = !element->locked;
                        MarkChanged();
                    }
                    x += iconSide + gap;

                    const float typeW = ScaleUi(22.0f);
                    DrawCenteredIconGlyph(
                        drawList,
                        ElementTypeIcon(element->type),
                        ImRect(ImVec2(x, rowRect.Min.y), ImVec2(x + typeW, rowRect.Max.y)),
                        ImGui::GetColorU32(element->hidden ? visual.faintText : visual.mutedText),
                        std::floor(ImGui::GetFontSize() * 0.92f));
                    x += typeW + gap + (element->parentId.empty() ? 0.0f : ScaleUi(12.0f));

                    const float labelW = std::max(1.0f, rowRect.Max.x - x - ScaleUi(7.0f));
                    ImGui::SetCursorScreenPos(ImVec2(x, rowRect.Min.y));
                    if (ImGui::InvisibleButton("##layer_select", ImVec2(labelW, rowH))) {
                        SelectElement(element->id, ImGui::GetIO().KeyCtrl);
                    }
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        ImGui::SetDragDropPayload("HBO_HUD_ELEMENT", element->id.c_str(), element->id.size() + 1);
                        ImGui::TextUnformatted(element->name.c_str());
                        ImGui::EndDragDropSource();
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HBO_HUD_ELEMENT")) {
                            const char* draggedId = static_cast<const char*>(payload->Data);
                            const float relativeY = ImGui::GetIO().MousePos.y - rowRect.Min.y;
                            if (element->type == ElementType::Group && relativeY > rowH * 0.20f && relativeY < rowH * 0.80f) {
                                AssignElementToGroup(widget, draggedId ? draggedId : "", element->id);
                            } else {
                                ReorderElementBefore(widget, draggedId ? draggedId : "", element->id);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (ImGui::BeginPopupContextItem("##hud_layer_context")) {
                        if (ImGui::MenuItem(ui.Text(UiText::HudDuplicateElement))) {
                            duplicateLayerRequest = element->id;
                        }
                        if (ImGui::MenuItem(ui.Text(UiText::Delete))) {
                            deleteLayerRequest = element->id;
                        }
                        ImGui::EndPopup();
                    }

                    const std::string label = EllipsizeText(element->name, labelW);
                    drawList->AddText(
                        ImVec2(x, rowRect.Min.y + std::floor((rowH - ImGui::GetTextLineHeight()) * 0.5f)),
                        ImGui::GetColorU32(element->hidden ? visual.faintText : selected ? visual.headerText : visual.mutedText),
                        label.c_str());
                    ImGui::SetCursorScreenPos(ImVec2(rowRect.Min.x, rowRect.Max.y));
                    ImGui::PopID();
                    ++rowIndex;
                    }
                }
                ImGui::EndChild();
                if (!duplicateLayerRequest.empty()) {
                    selectedElementIds = { duplicateLayerRequest };
                    DuplicateSelectedElements();
                } else if (!deleteLayerRequest.empty()) {
                    selectedElementIds = { deleteLayerRequest };
                    DeleteSelectedElements();
                }
            }
        }
        EndHudPanel();
    }

    ImRect ElementHitRect(const ImRect& rect) const {
        ImRect hit = rect;
        const float minSize = ScaleUi(16.0f);
        if (hit.GetWidth() < minSize) {
            const float center = (hit.Min.x + hit.Max.x) * 0.5f;
            hit.Min.x = center - minSize * 0.5f;
            hit.Max.x = center + minSize * 0.5f;
        }
        if (hit.GetHeight() < minSize) {
            const float center = (hit.Min.y + hit.Max.y) * 0.5f;
            hit.Min.y = center - minSize * 0.5f;
            hit.Max.y = center + minSize * 0.5f;
        }
        return hit;
    }

    float SnapValueWithThreshold(float value, float scale) const {
        if (!snapEnabled || gridSize <= 0.0f) {
            return value;
        }
        const float snapped = std::round(value / gridSize) * gridSize;
        const float thresholdPx = ScaleUi(6.0f);
        return std::abs(snapped - value) * std::max(0.001f, scale) <= thresholdPx ? snapped : value;
    }

    void MarkCanvasGestureChanged() {
        if (!canvasGestureUndoCaptured) {
            PushUndoSnapshot(lastCommittedSnapshot.empty() ? Snapshot() : lastCommittedSnapshot);
            canvasGestureUndoCaptured = true;
            editTransactionActive = true;
            editTransactionChanged = true;
        }
        for (HudWidget& widget : widgets) {
            widget.nextRefreshAtMs = 0;
        }
        canvasGestureSavePending = true;
    }

    ImRect ResizeHandleRect(const ImRect& rect, ResizeHandle handle, float size) const {
        const ImVec2 center = ResizeHandlePos(rect, handle);
        const float half = size * 0.5f;
        return ImRect(ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half));
    }

    ImGuiMouseCursor CursorForHandle(ResizeHandle handle) const {
        switch (handle) {
        case ResizeHandle::TopLeft:
        case ResizeHandle::BottomRight:
            return ImGuiMouseCursor_ResizeNWSE;
        case ResizeHandle::TopRight:
        case ResizeHandle::BottomLeft:
            return ImGuiMouseCursor_ResizeNESW;
        case ResizeHandle::Left:
        case ResizeHandle::Right:
            return ImGuiMouseCursor_ResizeEW;
        case ResizeHandle::Top:
        case ResizeHandle::Bottom:
            return ImGuiMouseCursor_ResizeNS;
        }
        return ImGuiMouseCursor_Arrow;
    }

    CanvasHitResult HitTestResizeHandles(HudWidget& widget, const ImVec2& mouse, const ImVec2& origin, float scale) {
        HudElement* element = PrimarySelectedElement(widget);
        if (!element || !ElementVisible(*element)) {
            return {};
        }
        constexpr ResizeHandle handles[] = {
            ResizeHandle::TopLeft,
            ResizeHandle::Top,
            ResizeHandle::TopRight,
            ResizeHandle::Right,
            ResizeHandle::BottomRight,
            ResizeHandle::Bottom,
            ResizeHandle::BottomLeft,
            ResizeHandle::Left,
        };
        const ImRect rect = ElementRect(*element, origin, scale);
        const float hitSize = ScaleUi(24.0f);
        std::optional<ResizeHandle> bestHandle;
        float bestDistanceSq = FLT_MAX;
        for (ResizeHandle handle : handles) {
            if (ResizeHandleRect(rect, handle, hitSize).Contains(mouse)) {
                const ImVec2 center = ResizeHandlePos(rect, handle);
                const float dx = mouse.x - center.x;
                const float dy = mouse.y - center.y;
                const float distanceSq = dx * dx + dy * dy;
                if (distanceSq < bestDistanceSq) {
                    bestDistanceSq = distanceSq;
                    bestHandle = handle;
                }
            }
        }
        return bestHandle ? CanvasHitResult{ element->id, bestHandle } : CanvasHitResult{};
    }

    CanvasHitResult HitTestElementBody(HudWidget& widget, const ImVec2& mouse, const ImVec2& origin, float scale) {
        for (HudElement* element : ElementsByZ(widget, true)) {
            if (!ElementVisible(*element) || element->type == ElementType::Group) {
                continue;
            }
            if (ElementHitRect(ElementRect(*element, origin, scale)).Contains(mouse)) {
                return CanvasHitResult{ element->id, std::nullopt };
            }
        }
        const float borderHit = ScaleUi(7.0f);
        for (HudElement* element : ElementsByZ(widget, true)) {
            if (!ElementVisible(*element) || element->type != ElementType::Group) {
                continue;
            }
            const ImRect rect = ElementRect(*element, origin, scale);
            const ImRect outer(ImVec2(rect.Min.x - borderHit, rect.Min.y - borderHit), ImVec2(rect.Max.x + borderHit, rect.Max.y + borderHit));
            const ImRect inner(ImVec2(rect.Min.x + borderHit, rect.Min.y + borderHit), ImVec2(rect.Max.x - borderHit, rect.Max.y - borderHit));
            if (outer.Contains(mouse) && (!inner.Contains(mouse) || inner.GetWidth() <= 0.0f || inner.GetHeight() <= 0.0f)) {
                return CanvasHitResult{ element->id, std::nullopt };
            }
        }
        return {};
    }

    CanvasHitResult HitTestCanvas(HudWidget& widget, const ImVec2& mouse, const ImVec2& origin, float scale) {
        if (CanvasHitResult handle = HitTestResizeHandles(widget, mouse, origin, scale); handle.HasHandle()) {
            return handle;
        }
        return HitTestElementBody(widget, mouse, origin, scale);
    }

    CanvasElementSnapshot SnapshotElementRect(const HudElement& element) const {
        return CanvasElementSnapshot{ element.id, element.x, element.y, element.width, element.height };
    }

    std::vector<CanvasElementSnapshot> CaptureDragSnapshots(HudWidget& widget) {
        std::vector<CanvasElementSnapshot> snapshots;
        std::set<std::string> captured;
        auto capture = [&](HudElement& element) {
            if (captured.insert(element.id).second) {
                snapshots.push_back(SnapshotElementRect(element));
            }
        };
        for (const std::string& id : selectedElementIds) {
            HudElement* element = FindElement(widget, id);
            if (!element || element->locked) {
                continue;
            }
            capture(*element);
            if (element->type == ElementType::Group) {
                for (HudElement& child : widget.elements) {
                    if (child.parentId == element->id && !child.locked) {
                        capture(child);
                    }
                }
            }
        }
        return snapshots;
    }

    const CanvasElementSnapshot* FindGestureSnapshot(std::string_view id) const {
        const auto it = std::find_if(canvasGestureSnapshots.begin(), canvasGestureSnapshots.end(), [&](const CanvasElementSnapshot& snapshot) {
            return snapshot.id == id;
        });
        return it == canvasGestureSnapshots.end() ? nullptr : &(*it);
    }

    void BuildGuideCandidates(const HudWidget& widget) {
        const double beginMs = HudPerfNowMs();
        guideCandidatesX = { 0.0f, widget.canvasWidth * 0.5f, widget.canvasWidth };
        guideCandidatesY = { 0.0f, widget.canvasHeight * 0.5f, widget.canvasHeight };
        std::unordered_set<std::string> movingIds;
        movingIds.reserve(canvasGestureSnapshots.size());
        for (const CanvasElementSnapshot& snapshot : canvasGestureSnapshots) {
            movingIds.insert(snapshot.id);
        }
        for (const HudElement& element : widget.elements) {
            if (element.hidden || movingIds.find(element.id) != movingIds.end()) {
                continue;
            }
            guideCandidatesX.push_back(element.x);
            guideCandidatesX.push_back(element.x + element.width * 0.5f);
            guideCandidatesX.push_back(element.x + element.width);
            guideCandidatesY.push_back(element.y);
            guideCandidatesY.push_back(element.y + element.height * 0.5f);
            guideCandidatesY.push_back(element.y + element.height);
        }
        hud_v3::NormalizeGuideCandidates(guideCandidatesX);
        hud_v3::NormalizeGuideCandidates(guideCandidatesY);
        lastGuideMs = HudPerfNowMs() - beginMs;
    }

    bool GestureBounds(ImRect& bounds) const {
        if (canvasGestureSnapshots.empty()) {
            return false;
        }
        const CanvasElementSnapshot& first = canvasGestureSnapshots.front();
        bounds = ImRect(ImVec2(first.x, first.y), ImVec2(first.x + first.width, first.y + first.height));
        for (const CanvasElementSnapshot& snapshot : canvasGestureSnapshots) {
            bounds.Add(ImVec2(snapshot.x, snapshot.y));
            bounds.Add(ImVec2(snapshot.x + snapshot.width, snapshot.y + snapshot.height));
        }
        return true;
    }

    float SnapAxisToGuides(
        const std::vector<float>& candidates,
        float begin,
        float center,
        float end,
        float rawDelta,
        float scale,
        std::optional<float>& activeGuide) const {
        activeGuide.reset();
        if (!guidesEnabled) {
            return rawDelta;
        }
        const float threshold = ScaleUi(6.0f) / std::max(0.001f, scale);
        const hud_v3::GuideSnapResult result = hud_v3::SnapAxisToGuides(candidates, begin, center, end, rawDelta, threshold);
        activeGuide = result.guide;
        return result.delta;
    }

    void BeginCanvasDrag(HudWidget& widget, std::string_view elementId) {
        canvasMode = CanvasInteractionMode::Dragging;
        canvasActiveElementId = std::string(elementId);
        canvasActiveHandle.reset();
        canvasGestureStartScreen = ImGui::GetIO().MousePos;
        canvasGestureSnapshots = CaptureDragSnapshots(widget);
        BuildGuideCandidates(widget);
        canvasGestureMoved = false;
        canvasGestureUndoCaptured = false;
        canvasGestureButton = 0;
    }

    void BeginCanvasResize(HudWidget& widget, std::string_view elementId, ResizeHandle handle) {
        HudElement* element = FindElement(widget, elementId);
        if (!element || element->locked) {
            return;
        }
        canvasMode = CanvasInteractionMode::Resizing;
        canvasActiveElementId = std::string(elementId);
        canvasActiveHandle = handle;
        canvasGestureStartScreen = ImGui::GetIO().MousePos;
        canvasGestureSnapshots = { SnapshotElementRect(*element) };
        BuildGuideCandidates(widget);
        canvasGestureMoved = false;
        canvasGestureUndoCaptured = false;
        canvasGestureButton = 0;
    }

    void BeginCanvasMarquee(bool additive) {
        canvasMode = CanvasInteractionMode::MarqueeSelecting;
        canvasActiveElementId.clear();
        canvasActiveHandle.reset();
        canvasGestureStartScreen = ImGui::GetIO().MousePos;
        canvasMarqueeRect = ImRect(canvasGestureStartScreen, canvasGestureStartScreen);
        canvasGestureMoved = false;
        canvasGestureUndoCaptured = false;
        canvasGestureAdditive = additive;
        canvasGestureButton = 0;
    }

    void BeginCanvasPan(int button) {
        canvasMode = CanvasInteractionMode::Panning;
        canvasActiveElementId.clear();
        canvasActiveHandle.reset();
        canvasGestureStartScreen = ImGui::GetIO().MousePos;
        canvasGestureStartPan = canvasPan;
        canvasGestureMoved = false;
        canvasGestureButton = button;
    }

    void EndCanvasGesture() {
        FlushPendingSaves();
        if (editTransactionActive && editTransactionChanged) {
            lastCommittedSnapshot = Snapshot();
            editTransactionActive = false;
            editTransactionChanged = false;
            textSavePending = false;
        }
        canvasMode = CanvasInteractionMode::Idle;
        canvasActiveElementId.clear();
        canvasActiveHandle.reset();
        canvasGestureSnapshots.clear();
        canvasGestureMoved = false;
        canvasGestureUndoCaptured = false;
        canvasGestureAdditive = false;
        canvasGestureSavePending = false;
        canvasGestureButton = 0;
        canvasMarqueeRect = ImRect();
        activeGuideX.reset();
        activeGuideY.reset();
    }

    void ApplyCanvasDrag(HudWidget& widget, const ImVec2& mouse, float scale) {
        if (canvasGestureSnapshots.empty()) {
            return;
        }
        const ImVec2 rawDelta(
            (mouse.x - canvasGestureStartScreen.x) / std::max(0.001f, scale),
            (mouse.y - canvasGestureStartScreen.y) / std::max(0.001f, scale));
        float effectiveDx = rawDelta.x;
        float effectiveDy = rawDelta.y;
        ImRect gestureBounds;
        if (GestureBounds(gestureBounds)) {
            effectiveDx = SnapAxisToGuides(
                guideCandidatesX,
                gestureBounds.Min.x,
                (gestureBounds.Min.x + gestureBounds.Max.x) * 0.5f,
                gestureBounds.Max.x,
                rawDelta.x,
                scale,
                activeGuideX);
            effectiveDy = SnapAxisToGuides(
                guideCandidatesY,
                gestureBounds.Min.y,
                (gestureBounds.Min.y + gestureBounds.Max.y) * 0.5f,
                gestureBounds.Max.y,
                rawDelta.y,
                scale,
                activeGuideY);
            if (!activeGuideX) {
                effectiveDx = SnapValueWithThreshold(gestureBounds.Min.x + rawDelta.x, scale) - gestureBounds.Min.x;
            }
            if (!activeGuideY) {
                effectiveDy = SnapValueWithThreshold(gestureBounds.Min.y + rawDelta.y, scale) - gestureBounds.Min.y;
            }

            const float minDx = -gestureBounds.Min.x;
            const float maxDx = widget.canvasWidth - gestureBounds.Max.x;
            const float minDy = -gestureBounds.Min.y;
            const float maxDy = widget.canvasHeight - gestureBounds.Max.y;
            const float unclampedDx = effectiveDx;
            const float unclampedDy = effectiveDy;
            if (minDx <= maxDx) effectiveDx = std::clamp(effectiveDx, minDx, maxDx);
            if (minDy <= maxDy) effectiveDy = std::clamp(effectiveDy, minDy, maxDy);
            if (effectiveDx != unclampedDx) activeGuideX.reset();
            if (effectiveDy != unclampedDy) activeGuideY.reset();
        }
        bool changed = false;
        for (const CanvasElementSnapshot& snapshot : canvasGestureSnapshots) {
            HudElement* element = FindElement(widget, snapshot.id);
            if (!element || element->locked) {
                continue;
            }
            const float nextX = snapshot.x + effectiveDx;
            const float nextY = snapshot.y + effectiveDy;
            if (element->x != nextX || element->y != nextY) {
                element->x = nextX;
                element->y = nextY;
                changed = true;
            }
        }
        if (changed) {
            canvasGestureMoved = true;
            MarkCanvasGestureChanged();
        }
    }

    void ApplyCanvasResize(HudWidget& widget, const ImVec2& mouse, float scale) {
        if (!canvasActiveHandle || canvasGestureSnapshots.empty()) {
            return;
        }
        HudElement* element = FindElement(widget, canvasActiveElementId);
        if (!element || element->locked) {
            return;
        }
        const CanvasElementSnapshot& start = canvasGestureSnapshots.front();
        const ImVec2 delta(
            (mouse.x - canvasGestureStartScreen.x) / std::max(0.001f, scale),
            (mouse.y - canvasGestureStartScreen.y) / std::max(0.001f, scale));
        const ResizeHandle handle = *canvasActiveHandle;
        const float fixedLeft = start.x;
        const float fixedTop = start.y;
        const float fixedRight = start.x + start.width;
        const float fixedBottom = start.y + start.height;
        const bool moveLeft = handle == ResizeHandle::TopLeft || handle == ResizeHandle::Left || handle == ResizeHandle::BottomLeft;
        const bool moveRight = handle == ResizeHandle::TopRight || handle == ResizeHandle::Right || handle == ResizeHandle::BottomRight;
        const bool moveTop = handle == ResizeHandle::TopLeft || handle == ResizeHandle::Top || handle == ResizeHandle::TopRight;
        const bool moveBottom = handle == ResizeHandle::BottomLeft || handle == ResizeHandle::Bottom || handle == ResizeHandle::BottomRight;

        float nextLeft = fixedLeft;
        float nextTop = fixedTop;
        float nextRight = fixedRight;
        float nextBottom = fixedBottom;
        if (moveLeft) {
            const float guideDelta = SnapAxisToGuides(guideCandidatesX, fixedLeft, fixedLeft, fixedLeft, delta.x, scale, activeGuideX);
            nextLeft = activeGuideX ? fixedLeft + guideDelta : SnapValueWithThreshold(fixedLeft + delta.x, scale);
        } else if (moveRight) {
            const float guideDelta = SnapAxisToGuides(guideCandidatesX, fixedRight, fixedRight, fixedRight, delta.x, scale, activeGuideX);
            nextRight = activeGuideX ? fixedRight + guideDelta : SnapValueWithThreshold(fixedRight + delta.x, scale);
        } else {
            activeGuideX.reset();
        }
        if (moveTop) {
            const float guideDelta = SnapAxisToGuides(guideCandidatesY, fixedTop, fixedTop, fixedTop, delta.y, scale, activeGuideY);
            nextTop = activeGuideY ? fixedTop + guideDelta : SnapValueWithThreshold(fixedTop + delta.y, scale);
        } else if (moveBottom) {
            const float guideDelta = SnapAxisToGuides(guideCandidatesY, fixedBottom, fixedBottom, fixedBottom, delta.y, scale, activeGuideY);
            nextBottom = activeGuideY ? fixedBottom + guideDelta : SnapValueWithThreshold(fixedBottom + delta.y, scale);
        } else {
            activeGuideY.reset();
        }

        nextLeft = std::clamp(nextLeft, 0.0f, widget.canvasWidth);
        nextRight = std::clamp(nextRight, 0.0f, widget.canvasWidth);
        nextTop = std::clamp(nextTop, 0.0f, widget.canvasHeight);
        nextBottom = std::clamp(nextBottom, 0.0f, widget.canvasHeight);

        if (nextRight - nextLeft < 1.0f) {
            if (moveLeft) {
                nextLeft = fixedRight - 1.0f;
            } else {
                nextRight = fixedLeft + 1.0f;
            }
        }
        if (nextBottom - nextTop < 1.0f) {
            if (moveTop) {
                nextTop = fixedBottom - 1.0f;
            } else {
                nextBottom = fixedTop + 1.0f;
            }
        }
        const float nextW = nextRight - nextLeft;
        const float nextH = nextBottom - nextTop;
        if (element->x != nextLeft || element->y != nextTop || element->width != nextW || element->height != nextH) {
            element->x = nextLeft;
            element->y = nextTop;
            element->width = nextW;
            element->height = nextH;
            canvasGestureMoved = true;
            MarkCanvasGestureChanged();
        }
    }

    void ApplyCanvasMarqueeSelection(HudWidget& widget, const ImRect& marquee, const ImVec2& origin, float scale, bool additive) {
        std::vector<std::string> nextSelection = additive ? selectedElementIds : std::vector<std::string>{};
        auto alreadySelected = [&](std::string_view id) {
            return std::find(nextSelection.begin(), nextSelection.end(), id) != nextSelection.end();
        };
        for (HudElement* element : ElementsByZ(widget)) {
            if (!ElementVisible(*element)) {
                continue;
            }
            if (marquee.Overlaps(ElementRect(*element, origin, scale)) && !alreadySelected(element->id)) {
                nextSelection.push_back(element->id);
            }
        }
        selectedElementIds = std::move(nextSelection);
    }

    ImVec2 ResizeHandlePos(const ImRect& rect, ResizeHandle handle) const {
        const float midX = (rect.Min.x + rect.Max.x) * 0.5f;
        const float midY = (rect.Min.y + rect.Max.y) * 0.5f;
        switch (handle) {
        case ResizeHandle::TopLeft: return rect.Min;
        case ResizeHandle::Top: return ImVec2(midX, rect.Min.y);
        case ResizeHandle::TopRight: return ImVec2(rect.Max.x, rect.Min.y);
        case ResizeHandle::Right: return ImVec2(rect.Max.x, midY);
        case ResizeHandle::BottomRight: return rect.Max;
        case ResizeHandle::Bottom: return ImVec2(midX, rect.Max.y);
        case ResizeHandle::BottomLeft: return ImVec2(rect.Min.x, rect.Max.y);
        case ResizeHandle::Left: return ImVec2(rect.Min.x, midY);
        }
        return rect.Max;
    }

    void DrawEditorGrid(ImDrawList* drawList, const ImRect& rect, float scale, const HudEditorVisualStyle& visual) const {
        const float grid = std::max(1.0f, gridSize * scale);
        const ImU32 color = ImGui::GetColorU32(visual.gridMinor);
        for (float x = rect.Min.x; x <= rect.Max.x; x += grid) {
            drawList->AddLine(ImVec2(x, rect.Min.y), ImVec2(x, rect.Max.y), color);
        }
        for (float y = rect.Min.y; y <= rect.Max.y; y += grid) {
            drawList->AddLine(ImVec2(rect.Min.x, y), ImVec2(rect.Max.x, y), color);
        }
        const ImU32 majorColor = ImGui::GetColorU32(visual.gridMajor);
        drawList->AddLine(ImVec2(rect.Min.x + rect.GetWidth() * 0.5f, rect.Min.y), ImVec2(rect.Min.x + rect.GetWidth() * 0.5f, rect.Max.y), majorColor);
        drawList->AddLine(ImVec2(rect.Min.x, rect.Min.y + rect.GetHeight() * 0.5f), ImVec2(rect.Max.x, rect.Min.y + rect.GetHeight() * 0.5f), majorColor);
    }

    void DrawInlineTextEdit(HudElement& element, const ImRect& rect) {
        if (inlineEditElementId != element.id || element.type != ElementType::Text) {
            return;
        }
        ImGui::SetCursorScreenPos(rect.Min);
        ImGui::SetNextItemWidth(rect.GetWidth());
        if (InputTextMultilineString("##hud_inline_text_edit", element.data.text, rect.GetSize(), ImGuiInputTextFlags_AutoSelectAll)) {
            MarkChanged();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0))) {
            inlineEditElementId.clear();
        }
    }

    void DrawEditorCanvas(HudWidget& widget, IDirect3DDevice9* device) {
        UiSettings& ui = UiSettings::Instance();
        const HudEditorVisualStyle visual = HudEditorStyleTokens();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 areaMin = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            areaMin,
            ImVec2(areaMin.x + std::max(1.0f, avail.x), areaMin.y + std::max(1.0f, avail.y)),
            ImGui::GetColorU32(WithAlpha(visual.canvasBg, 0.36f)),
            ScaleUi(6.0f));
        const float padding = ScaleUi(32.0f);
        const float fitScale = std::max(
            0.10f,
            std::min(
                (avail.x - padding * 2.0f) / std::max(1.0f, widget.canvasWidth),
                (avail.y - padding * 2.0f) / std::max(1.0f, widget.canvasHeight)));
        const float scale = canvasFitZoom ? std::min(4.0f, fitScale) : std::clamp(canvasZoom, 0.10f, 4.0f);
        const ImVec2 canvasSize(widget.canvasWidth * scale, widget.canvasHeight * scale);
        const ImVec2 origin(
            ImGui::GetCursorScreenPos().x + std::max(0.0f, (avail.x - canvasSize.x) * 0.5f) + canvasPan.x,
            ImGui::GetCursorScreenPos().y + std::max(0.0f, (avail.y - canvasSize.y) * 0.5f) + canvasPan.y);
        const ImRect canvasRect(origin, ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y));
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(canvasRect.Min, canvasRect.Max, ImGui::GetColorU32(visual.canvasBg), ScaleUi(2.0f));
        drawList->AddRect(canvasRect.Min, canvasRect.Max, ImGui::GetColorU32(visual.canvasBorder), ScaleUi(2.0f), 0, ScaleUi(1.0f));
        DrawEditorGrid(drawList, canvasRect, scale, visual);

        const float inputPadding = ScaleUi(14.0f);
        const ImRect canvasInputRect(
            ImVec2(canvasRect.Min.x - inputPadding, canvasRect.Min.y - inputPadding),
            ImVec2(canvasRect.Max.x + inputPadding, canvasRect.Max.y + inputPadding));

        ImGui::SetCursorScreenPos(canvasInputRect.Min);
        ImGui::InvisibleButton(
            "##hud_editor_canvas_surface",
            canvasInputRect.GetSize(),
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        ImGuiIO& io = ImGui::GetIO();
        const bool mouseInCanvas = canvasRect.Contains(io.MousePos);
        const bool mouseInCanvasInput = canvasInputRect.Contains(io.MousePos);
        const bool surfaceHovered = ImGui::IsItemHovered() || mouseInCanvasInput;

        CanvasHitResult hover = surfaceHovered ? HitTestCanvas(widget, io.MousePos, origin, scale) : CanvasHitResult{};
        canvasHoverElementId = hover.elementId;
        canvasHoverHandle = hover.handle;
        if (canvasMode == CanvasInteractionMode::Idle
            || canvasMode == CanvasInteractionMode::HoverElement
            || canvasMode == CanvasInteractionMode::HoverHandle) {
            canvasMode = hover.HasHandle()
                ? CanvasInteractionMode::HoverHandle
                : (hover.HasElement() ? CanvasInteractionMode::HoverElement : CanvasInteractionMode::Idle);
        }

        {
            if (canvasMode == CanvasInteractionMode::Dragging) {
                if (ImGui::IsMouseDown(0)) {
                    if (ImGui::IsMouseDragging(0, ScaleUi(2.0f))) {
                        ApplyCanvasDrag(widget, io.MousePos, scale);
                    }
                } else {
                    EndCanvasGesture();
                }
            } else if (canvasMode == CanvasInteractionMode::Resizing) {
                if (ImGui::IsMouseDown(0)) {
                    if (ImGui::IsMouseDragging(0, ScaleUi(2.0f))) {
                        ApplyCanvasResize(widget, io.MousePos, scale);
                    }
                } else {
                    EndCanvasGesture();
                }
            } else if (canvasMode == CanvasInteractionMode::MarqueeSelecting) {
                if (ImGui::IsMouseDown(0)) {
                    canvasGestureMoved = canvasGestureMoved || ImGui::IsMouseDragging(0, ScaleUi(2.0f));
                    canvasMarqueeRect = ImRect(
                        ImVec2(std::min(canvasGestureStartScreen.x, io.MousePos.x), std::min(canvasGestureStartScreen.y, io.MousePos.y)),
                        ImVec2(std::max(canvasGestureStartScreen.x, io.MousePos.x), std::max(canvasGestureStartScreen.y, io.MousePos.y)));
                    if (canvasGestureMoved) {
                        ApplyCanvasMarqueeSelection(widget, canvasMarqueeRect, origin, scale, canvasGestureAdditive);
                    }
                } else {
                    EndCanvasGesture();
                }
            } else if (canvasMode == CanvasInteractionMode::Panning) {
                if (ImGui::IsMouseDown(canvasGestureButton)) {
                    canvasPan = ImVec2(
                        canvasGestureStartPan.x + io.MousePos.x - canvasGestureStartScreen.x,
                        canvasGestureStartPan.y + io.MousePos.y - canvasGestureStartScreen.y);
                    canvasGestureMoved = true;
                } else {
                    EndCanvasGesture();
                }
            } else if (surfaceHovered) {
                if (ImGui::IsMouseClicked(2)) {
                    BeginCanvasPan(2);
                } else if (ImGui::IsMouseClicked(0)) {
                    if (hover.HasHandle()) {
                        if (HudElement* element = FindElement(widget, hover.elementId); element && !element->locked) {
                            BeginCanvasResize(widget, hover.elementId, *hover.handle);
                        }
                    } else if (hover.HasElement()) {
                        HudElement* element = FindElement(widget, hover.elementId);
                        const bool additive = io.KeyCtrl;
                        SelectElement(hover.elementId, additive);
                        if (element && ImGui::IsMouseDoubleClicked(0) && element->type == ElementType::Text) {
                            inlineEditElementId = element->id;
                        }
                        if (!additive && element && !element->locked && IsElementSelected(element->id)) {
                            BeginCanvasDrag(widget, element->id);
                        }
                    } else if (mouseInCanvas) {
                        if (!io.KeyCtrl) {
                            selectedElementIds.clear();
                        }
                        BeginCanvasMarquee(io.KeyCtrl);
                    }
                }
            }

            if (canvasMode == CanvasInteractionMode::Panning) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            } else if (canvasMode == CanvasInteractionMode::Resizing && canvasActiveHandle) {
                ImGui::SetMouseCursor(CursorForHandle(*canvasActiveHandle));
            } else if (hover.HasHandle()) {
                const HudElement* element = FindElement(widget, hover.elementId);
                ImGui::SetMouseCursor(element && element->locked ? ImGuiMouseCursor_NotAllowed : CursorForHandle(*hover.handle));
            } else if (canvasMode == CanvasInteractionMode::Dragging || hover.HasElement()) {
                const HudElement* element = hover.HasElement() ? FindElement(widget, hover.elementId) : FindElement(widget, canvasActiveElementId);
                ImGui::SetMouseCursor(element && element->locked ? ImGuiMouseCursor_NotAllowed : ImGuiMouseCursor_ResizeAll);
            }
        }

        DrawCanvas(widget, device, origin, scale, true);

        const ImU32 guideColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.95f, 0.45f, 0.72f, 0.95f));
        if (activeGuideX) {
            const float x = origin.x + *activeGuideX * scale;
            drawList->AddLine(ImVec2(x, canvasRect.Min.y), ImVec2(x, canvasRect.Max.y), guideColor, ScaleUi(1.0f));
        }
        if (activeGuideY) {
            const float y = origin.y + *activeGuideY * scale;
            drawList->AddLine(ImVec2(canvasRect.Min.x, y), ImVec2(canvasRect.Max.x, y), guideColor, ScaleUi(1.0f));
        }

        if (!hover.HasHandle() && hover.HasElement() && !IsElementSelected(hover.elementId)) {
            if (const HudElement* element = FindElement(widget, hover.elementId)) {
                const ImRect rect = ElementRect(*element, origin, scale);
                drawList->AddRect(rect.Min, rect.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(0.95f, 0.78f, 0.32f, 0.95f)), 0.0f, 0, ScaleUi(1.0f));
            }
        }

        for (HudElement* element : ElementsByZ(widget, true)) {
            if (element->hidden) {
                continue;
            }
            const ImRect rect = ElementRect(*element, origin, scale);
            ImGui::PushID(element->id.c_str());
            const bool selected = IsElementSelected(element->id);
            if (selected) {
                drawList->AddRect(rect.Min, rect.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.78f, 1.0f, 1.0f)), 0.0f, 0, ScaleUi(1.5f));
                const bool primary = !selectedElementIds.empty() && selectedElementIds.back() == element->id;
                if (primary) {
                    constexpr ResizeHandle handles[] = {
                        ResizeHandle::TopLeft,
                        ResizeHandle::Top,
                        ResizeHandle::TopRight,
                        ResizeHandle::Right,
                        ResizeHandle::BottomRight,
                        ResizeHandle::Bottom,
                        ResizeHandle::BottomLeft,
                        ResizeHandle::Left,
                    };
                    const float handleSize = ScaleUi(12.0f);
                    for (ResizeHandle handle : handles) {
                        const ImRect handleRect = ResizeHandleRect(rect, handle, handleSize);
                        const bool hot = canvasHoverElementId == element->id && canvasHoverHandle && *canvasHoverHandle == handle;
                        const ImVec4 color = element->locked
                            ? ImVec4(0.45f, 0.48f, 0.55f, 1.0f)
                            : (hot ? ImVec4(0.95f, 0.78f, 0.32f, 1.0f) : ImVec4(0.35f, 0.78f, 1.0f, 1.0f));
                        drawList->AddRectFilled(handleRect.Min, handleRect.Max, ImGui::ColorConvertFloat4ToU32(color), ScaleUi(2.0f));
                    }
                }
                DrawInlineTextEdit(*element, rect);
            }
            ImGui::PopID();
        }

        if (canvasMode == CanvasInteractionMode::MarqueeSelecting && canvasGestureMoved) {
            drawList->AddRectFilled(canvasMarqueeRect.Min, canvasMarqueeRect.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.78f, 1.0f, 0.12f)));
            drawList->AddRect(canvasMarqueeRect.Min, canvasMarqueeRect.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.78f, 1.0f, 0.85f)), 0.0f, 0, ScaleUi(1.0f));
        }

        const std::string zoomLabel = canvasFitZoom
            ? ui.Text(UiText::HudZoomFit)
            : ui.Format(UiText::HudZoomPercentFormat, static_cast<int>(std::lround(scale * 100.0f)));
        const std::string status = ui.Format(
            UiText::HudCanvasStatusFormat,
            snapEnabled ? ui.Text(UiText::HudOn) : ui.Text(UiText::HudOff),
            guidesEnabled ? ui.Text(UiText::HudOn) : ui.Text(UiText::HudOff),
            gridSize,
            zoomLabel.c_str());
        const float statusY = std::min(canvasRect.Max.y + ScaleUi(8.0f), areaMin.y + avail.y - ImGui::GetTextLineHeight() - ScaleUi(2.0f));
        ImGui::SetCursorScreenPos(ImVec2(areaMin.x + ScaleUi(6.0f), statusY));
        ImGui::TextDisabled("%s", EllipsizeText(status, std::max(1.0f, avail.x - ScaleUi(12.0f))).c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", ui.Text(UiText::HudCanvasInteractionHint));
        }
    }

    void DrawWidgetListMeasured(float height = 0.0f) {
        const double beginMs = HudPerfNowMs();
        DrawWidgetList(height);
        lastEditorStats.widgetListMs += HudPerfNowMs() - beginMs;
    }

    void DrawLayersMeasured(HudWidget& widget, float height = 0.0f) {
        const double beginMs = HudPerfNowMs();
        DrawLayers(widget, height);
        lastEditorStats.layersMs += HudPerfNowMs() - beginMs;
    }

    void DrawEditorCanvasMeasured(HudWidget& widget, IDirect3DDevice9* device) {
        const double beginMs = HudPerfNowMs();
        DrawEditorCanvas(widget, device);
        lastEditorStats.canvasMs += HudPerfNowMs() - beginMs;
    }

    bool DrawBottomPanelTab(HudBottomPanel panel, UiText labelId, float width, const HudEditorVisualStyle& visual) {
        const bool active = bottomPanel == panel;
        if (HudTextActionButton(
                nullptr,
                UiSettings::Instance().Text(labelId),
                ("##hud_bottom_tab_" + std::to_string(static_cast<int>(panel))).c_str(),
                nullptr,
                ImVec2(width, ImGui::GetFrameHeight()),
                visual,
                true,
                active)) {
            bottomPanel = panel;
            bottomPanelCollapsed = false;
            QueueSave();
            return true;
        }
        return false;
    }

    bool PropertyCategoryMatches(HudPropertiesCategory category, std::string_view query) const {
        if (query.empty()) {
            return propertiesCategory == category;
        }
        UiSettings& ui = UiSettings::Instance();
        std::string haystack;
        switch (category) {
        case HudPropertiesCategory::Content:
            haystack = std::string(ui.Text(UiText::HudPropertiesContent)) + " " + ui.Text(UiText::HudText) + " "
                + ui.Text(UiText::Name) + " " + ui.Text(UiText::HudSource) + " " + ui.Text(UiText::HudExpression) + " "
                + ui.Text(UiText::HudProgressRange) + " " + ui.Text(UiText::HudMin) + " " + ui.Text(UiText::HudMax) + " "
                + ui.Text(UiText::HudDefaultValue) + " " + ui.Text(UiText::HudFontSize);
            break;
        case HudPropertiesCategory::Geometry:
            haystack = std::string(ui.Text(UiText::HudPropertiesGeometry)) + " " + ui.Text(UiText::HudPosition) + " " + ui.Text(UiText::HudSize) + " X Y W H";
            break;
        case HudPropertiesCategory::Appearance:
            haystack = std::string(ui.Text(UiText::HudPropertiesAppearance)) + " " + ui.Text(UiText::HudStyle) + " "
                + ui.Text(UiText::HudOpacity) + " " + ui.Text(UiText::HudFillColor) + " " + ui.Text(UiText::HudTextColor)
                + " " + ui.Text(UiText::HudStroke) + " " + ui.Text(UiText::HudShadow) + " " + ui.Text(UiText::HudOutline);
            break;
        case HudPropertiesCategory::Visibility:
            haystack = std::string(ui.Text(UiText::HudInspectorVisibility)) + " " + ui.Text(UiText::HudVisibilityConditions)
                + " " + ui.Text(UiText::HudRefreshMs) + " " + ui.Text(UiText::Enabled) + " " + ui.Text(UiText::HudLocked)
                + " " + ui.Text(UiText::HudHidden);
            break;
        }
        return LowerUtf8(haystack).find(query) != std::string::npos;
    }

    void DrawMultiSelectionCommon(HudWidget& widget, HudPropertiesCategory category) {
        UiSettings& ui = UiSettings::Instance();
        std::vector<HudElement*> elements;
        for (const std::string& id : selectedElementIds) {
            if (HudElement* element = FindElement(widget, id)) {
                elements.push_back(element);
            }
        }
        if (elements.empty()) {
            return;
        }

        const auto drawMixedBool = [&](UiText label, bool HudElement::*member) {
            const bool first = elements.front()->*member;
            const bool mixed = std::any_of(elements.begin() + 1, elements.end(), [&](const HudElement* element) { return element->*member != first; });
            bool value = first;
            if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
            const bool changed = ImGui::Checkbox(ui.Text(label), &value);
            if (mixed) ImGui::PopItemFlag();
            if (changed) {
                for (HudElement* element : elements) element->*member = value;
                MarkChanged();
            }
        };
        if (category == HudPropertiesCategory::Visibility) {
            drawMixedBool(UiText::HudLocked, &HudElement::locked);
            ImGui::SameLine();
            drawMixedBool(UiText::HudHidden, &HudElement::hidden);
        } else if (category == HudPropertiesCategory::Appearance) {
            const float firstOpacity = elements.front()->opacity;
            const bool mixedOpacity = std::any_of(elements.begin() + 1, elements.end(), [&](const HudElement* element) {
                return std::abs(element->opacity - firstOpacity) > 0.0001f;
            });
            float opacity = firstOpacity;
            if (mixedOpacity) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
            const bool opacityChanged = ImGui::SliderFloat(ui.Text(UiText::HudOpacity), &opacity, 0.0f, 1.0f, "%.2f");
            if (mixedOpacity) ImGui::PopItemFlag();
            if (opacityChanged) {
                for (HudElement* element : elements) element->opacity = opacity;
                MarkChanged();
            }
        } else if (category == HudPropertiesCategory::Geometry) {
            const HudEditorVisualStyle visual = HudEditorStyleTokens();
            const float width = std::min(
                HudTextActionButtonWidth(ui_icons::Sliders, ui.Text(UiText::HudAlignMenu)),
                ImGui::GetContentRegionAvail().x);
            if (HudTextActionButton(ui_icons::Sliders, ui.Text(UiText::HudAlignMenu), "##hud_properties_align", nullptr,
                    ImVec2(width, ImGui::GetFrameHeight()), visual)) {
                ImGui::OpenPopup("##hud_properties_align_popup");
            }
            if (ImGui::BeginPopup("##hud_properties_align_popup")) {
                DrawAlignMenuItems();
                ImGui::EndPopup();
            }
        }
    }

    void DrawWidgetGeometryV3(HudWidget& widget) {
        UiSettings& ui = UiSettings::Instance();
        bool changed = false;
        bool canvasSizeChanged = false;
        ImGui::PushID(widget.id.c_str());
        const float placeWidth = HudTextActionButtonWidth(ui_icons::Compass, ui.Text(UiText::HudPlaceOnScreen));
        if (HudTextActionButton(ui_icons::Compass, ui.Text(UiText::HudPlaceOnScreen), "##hud_place_v3", nullptr,
                ImVec2(std::min(placeWidth, ImGui::GetContentRegionAvail().x), ImGui::GetFrameHeight()), HudEditorStyleTokens())) {
            placementMode = true;
            placementWidgetId = widget.id;
            placementUndoCaptured = false;
        }
        if (placementMode && placementWidgetId == widget.id) {
            ImGui::SameLine();
            if (ImGui::Button(ui.Text(UiText::Cancel))) {
                placementMode = false;
                placementWidgetId.clear();
            }
        }
        if (ImGui::BeginTable("##hud_widget_canvas_geometry", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", ui.Text(UiText::HudCanvasWidth));
            ImGui::SetNextItemWidth(-FLT_MIN);
            canvasSizeChanged |= ImGui::DragFloat("##hud_widget_canvas_width", &widget.canvasWidth, 1.0f, 16.0f, 2000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", ui.Text(UiText::HudCanvasHeight));
            ImGui::SetNextItemWidth(-FLT_MIN);
            canvasSizeChanged |= ImGui::DragFloat("##hud_widget_canvas_height", &widget.canvasHeight, 1.0f, 16.0f, 2000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndTable();
        }
        changed |= canvasSizeChanged;
        changed |= DrawScalePolicyCombo(widget);
        changed |= DrawAnchorCombo(widget);
        if (ImGui::BeginTable("##hud_widget_offset_geometry", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", ui.Text(UiText::HudOffsetX));
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::DragFloat("##hud_widget_offset_x", &widget.position.offsetX, 1.0f, -kVirtualWidth, kVirtualWidth, "%.0f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", ui.Text(UiText::HudOffsetY));
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::DragFloat("##hud_widget_offset_y", &widget.position.offsetY, 1.0f, -kVirtualHeight, kVirtualHeight, "%.0f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndTable();
        }
        ImGui::PopID();
        if (canvasSizeChanged) {
            for (HudElement& element : widget.elements) {
                ClampElementToCanvas(widget, element, element.type == ElementType::Line ? 0.0f : 1.0f);
            }
        }
        if (changed) {
            MarkChanged();
        }
    }

    void DrawElementGeometryV3(HudWidget& widget) {
        UiSettings& ui = UiSettings::Instance();
        HudElement* element = PrimarySelectedElement(widget);
        if (!element) return;
        bool changed = false;
        ImGui::PushID(element->id.c_str());
        changed |= DrawElementGeometryFields(
            widget,
            *element,
            "##hud_element_geometry",
            element->type == ElementType::Line ? 0.0f : 1.0f);
        ImGui::PopID();
        if (changed) MarkChanged();
    }

    void DrawVisibilityV3(HudWidget& widget, HudElement* element) {
        UiSettings& ui = UiSettings::Instance();
        bool changed = false;
        if (!element) {
            ImGui::PushID(widget.id.c_str());
            if (ImGui::Button((std::string(ui_icons::Sliders) + " " + ui.Text(UiText::HudVisibilityConditions)).c_str())) conditionsPopupPending = true;
            changed |= DrawConditionFlagsPopup("##hud_widget_conditions_v3", conditionsPopupPending, UiText::HudVisibilityConditions,
                widget.visibility.conditions, &widget.visibility.conditionsCombine);
            if (ImGui::InputInt(ui.Text(UiText::HudRefreshMs), &widget.refreshMs, 50, 100)) {
                widget.refreshMs = std::max(0, widget.refreshMs);
                widget.nextRefreshAtMs = 0;
                changed = true;
            }
            if (widget.refreshMs == 0) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "%s", ui.Text(UiText::HudRefreshZeroWarning));
            }
            ImGui::PopID();
        } else {
            ImGui::PushID(element->id.c_str());
            changed |= ImGui::Checkbox(ui.Text(UiText::HudLocked), &element->locked);
            ImGui::SameLine();
            changed |= ImGui::Checkbox(ui.Text(UiText::HudHidden), &element->hidden);
            ImGui::SeparatorText(ui.Text(UiText::HudVisibilityConditions));
            if (ImGui::Button((std::string(ui_icons::Sliders) + " " + ui.Text(UiText::HudVisibilityConditions)).c_str())) elementConditionsPopupPending = true;
            changed |= DrawConditionFlagsPopup("##hud_element_conditions_v3", elementConditionsPopupPending, UiText::HudVisibilityConditions,
                element->visibility.conditions, &element->visibility.conditionsCombine);
            ImGui::PopID();
        }
        if (changed) MarkChanged();
    }

    void DrawPropertiesV3(HudWidget& widget) {
        UiSettings& ui = UiSettings::Instance();
        const HudEditorVisualStyle visual = HudEditorStyleTokens();
        HudElement* element = PrimarySelectedElement(widget);
        if (!element) propertyScope = HudPropertyScope::Widget;

        const char* widgetScopeLabel = ui.Text(UiText::HudWidgetSection);
        const std::string elementScopeLabel = selectedElementIds.size() > 1
            ? ui.Format(UiText::HudSelectedElementsFormat, static_cast<int>(selectedElementIds.size()))
            : ui.Text(UiText::HudElementSection);
        const float scopeWidgetWidth = std::max(ScaleUi(110.0f), ImGui::CalcTextSize(widgetScopeLabel).x + ScaleUi(24.0f));
        const float scopeElementWidth = std::max(ScaleUi(140.0f), ImGui::CalcTextSize(elementScopeLabel.c_str()).x + ScaleUi(24.0f));
        const bool inlineSearch = ImGui::GetContentRegionAvail().x >= scopeWidgetWidth + (element ? scopeElementWidth : 0.0f) + ScaleUi(260.0f);
        const int scopeColumns = inlineSearch ? (element ? 3 : 2) : (element ? 2 : 1);
        if (ImGui::BeginTable("##hud_properties_scope", scopeColumns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("##widget_scope", ImGuiTableColumnFlags_WidthFixed, scopeWidgetWidth);
            if (element) ImGui::TableSetupColumn("##element_scope", ImGuiTableColumnFlags_WidthFixed, scopeElementWidth);
            if (inlineSearch) ImGui::TableSetupColumn("##property_search", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            if (HudTextActionButton(nullptr, widgetScopeLabel, "##hud_widget_scope", nullptr,
                    ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()), visual, true,
                    propertyScope == HudPropertyScope::Widget)) {
                propertyScope = HudPropertyScope::Widget;
            }
            if (element) {
                ImGui::TableNextColumn();
                if (HudTextActionButton(nullptr, elementScopeLabel.c_str(), "##hud_element_scope", nullptr,
                        ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()), visual, true,
                        propertyScope == HudPropertyScope::Element)) {
                    propertyScope = HudPropertyScope::Element;
                }
            }
            if (inlineSearch) {
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                InputTextWithHintString("##hud_properties_search", ui.Text(UiText::HudPropertiesSearchHint), propertiesSearchQuery);
            }
            ImGui::EndTable();
        }
        if (!inlineSearch) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            InputTextWithHintString("##hud_properties_search", ui.Text(UiText::HudPropertiesSearchHint), propertiesSearchQuery);
        }

        const std::string normalizedSearch = LowerUtf8(TrimAscii(propertiesSearchQuery));
        const bool widgetScope = propertyScope == HudPropertyScope::Widget || !element;
        const bool multiElementScope = !widgetScope && selectedElementIds.size() > 1;
        const auto categoryApplicable = [&](HudPropertiesCategory category) {
            if (widgetScope) return category != HudPropertiesCategory::Appearance;
            if (multiElementScope) return category != HudPropertiesCategory::Content;
            return true;
        };
        if (!categoryApplicable(propertiesCategory)) {
            propertiesCategory = widgetScope ? HudPropertiesCategory::Content : HudPropertiesCategory::Geometry;
        }

        if (normalizedSearch.empty()) {
            HudPropertiesCategory categories[4]{};
            int categoryCount = 0;
            const auto appendCategory = [&](HudPropertiesCategory category) {
                if (categoryApplicable(category)) categories[categoryCount++] = category;
            };
            appendCategory(HudPropertiesCategory::Content);
            appendCategory(HudPropertiesCategory::Geometry);
            appendCategory(HudPropertiesCategory::Appearance);
            appendCategory(HudPropertiesCategory::Visibility);
            const auto categoryLabel = [&](HudPropertiesCategory category) {
                switch (category) {
                case HudPropertiesCategory::Geometry: return UiText::HudPropertiesGeometry;
                case HudPropertiesCategory::Appearance: return UiText::HudPropertiesAppearance;
                case HudPropertiesCategory::Visibility: return UiText::HudInspectorVisibility;
                case HudPropertiesCategory::Content:
                default: return UiText::HudPropertiesContent;
                }
            };
            if (ImGui::BeginTable("##hud_properties_categories", categoryCount,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
                for (int i = 0; i < categoryCount; ++i) {
                    ImGui::TableNextColumn();
                    ImGui::PushID(i);
                    if (HudTextActionButton(nullptr, ui.Text(categoryLabel(categories[i])), "##hud_property_category", nullptr,
                            ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()), visual, true,
                            propertiesCategory == categories[i])) {
                        propertiesCategory = categories[i];
                        QueueSave();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::Separator();

        const float propertyAvailableWidth = ImGui::GetContentRegionAvail().x;
        const float propertyLabelReserve = std::min(ScaleUi(220.0f), propertyAvailableWidth * 0.38f);
        ImGui::PushItemWidth(std::max(ScaleUi(120.0f), propertyAvailableWidth - propertyLabelReserve));

        auto drawCategory = [&](HudPropertiesCategory category) {
            if (!PropertyCategoryMatches(category, normalizedSearch)) return;
            if (!normalizedSearch.empty()) {
                UiText heading = UiText::HudPropertiesContent;
                if (category == HudPropertiesCategory::Geometry) heading = UiText::HudPropertiesGeometry;
                else if (category == HudPropertiesCategory::Appearance) heading = UiText::HudPropertiesAppearance;
                else if (category == HudPropertiesCategory::Visibility) heading = UiText::HudInspectorVisibility;
                ImGui::SeparatorText(ui.Text(heading));
            }
            switch (category) {
            case HudPropertiesCategory::Content:
                if (widgetScope) DrawWidgetMainInspector(widget);
                else {
                    DrawElementMainInspector(widget);
                    DrawElementDataProperties(*element);
                }
                break;
            case HudPropertiesCategory::Geometry:
                if (widgetScope) DrawWidgetGeometryV3(widget);
                else if (multiElementScope) DrawMultiSelectionCommon(widget, category);
                else DrawElementGeometryV3(widget);
                break;
            case HudPropertiesCategory::Appearance:
                if (!widgetScope && element && !multiElementScope) DrawStyleProperties(*element);
                else if (multiElementScope) DrawMultiSelectionCommon(widget, category);
                break;
            case HudPropertiesCategory::Visibility:
                if (multiElementScope) DrawMultiSelectionCommon(widget, category);
                else DrawVisibilityV3(widget, widgetScope ? nullptr : element);
                break;
            }
        };
        const bool anyCategory = (categoryApplicable(HudPropertiesCategory::Content) && PropertyCategoryMatches(HudPropertiesCategory::Content, normalizedSearch))
            || (categoryApplicable(HudPropertiesCategory::Geometry) && PropertyCategoryMatches(HudPropertiesCategory::Geometry, normalizedSearch))
            || (categoryApplicable(HudPropertiesCategory::Appearance) && PropertyCategoryMatches(HudPropertiesCategory::Appearance, normalizedSearch))
            || (categoryApplicable(HudPropertiesCategory::Visibility) && PropertyCategoryMatches(HudPropertiesCategory::Visibility, normalizedSearch));
        if (!anyCategory) {
            ImGui::TextDisabled("%s", ui.Text(UiText::HudPropertiesNoResults));
        } else {
            if (categoryApplicable(HudPropertiesCategory::Content)) drawCategory(HudPropertiesCategory::Content);
            if (categoryApplicable(HudPropertiesCategory::Geometry)) drawCategory(HudPropertiesCategory::Geometry);
            if (categoryApplicable(HudPropertiesCategory::Appearance)) drawCategory(HudPropertiesCategory::Appearance);
            if (categoryApplicable(HudPropertiesCategory::Visibility)) drawCategory(HudPropertiesCategory::Visibility);
        }
        ImGui::PopItemWidth();
    }

    void DrawHudHelpWindow() {
        if (!helpWindowOpen) return;
        UiSettings& ui = UiSettings::Instance();
        const HudEditorVisualStyle visual = HudEditorStyleTokens();
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const ImVec2 maxSize(
            std::max(ScaleUi(520.0f), displaySize.x - ScaleUi(40.0f)),
            std::max(ScaleUi(400.0f), displaySize.y - ScaleUi(40.0f)));
        const ImVec2 minSize(std::min(ScaleUi(680.0f), maxSize.x), std::min(ScaleUi(480.0f), maxSize.y));
        ImGui::SetNextWindowSizeConstraints(minSize, maxSize);
        ImGui::SetNextWindowSize(ScaleUi(920.0f, 640.0f), ImGuiCond_Appearing);
        const std::string title = std::string(ui.Text(UiText::HudHelpTitle)) + "##hud_v3_help";
        if (!ImGui::Begin(title.c_str(), &helpWindowOpen, ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::End();
            return;
        }

        ImGui::TextColored(visual.mutedText, "%s", ui.Text(UiText::HudHelpIntro));
        ImGui::Separator();

        const UiText sections[] = {
            UiText::HudHelpWorkflow,
            UiText::HudHelpCanvas,
            UiText::HudHelpElements,
            UiText::HudHelpData,
            UiText::HudHelpFiles,
        };
        helpSection = std::clamp(helpSection, 0, static_cast<int>(std::size(sections)) - 1);
        const float navigationWidth = std::min(ScaleUi(210.0f), ImGui::GetContentRegionAvail().x * 0.30f);
        if (ImGui::BeginChild("##hud_help_navigation", ImVec2(navigationWidth, 0.0f), ImGuiChildFlags_Borders,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            for (int i = 0; i < static_cast<int>(std::size(sections)); ++i) {
                ImGui::PushID(i);
                if (HudTextActionButton(nullptr, ui.Text(sections[i]), "##hud_help_section", nullptr,
                        ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() + ScaleUi(4.0f)),
                        visual, true, helpSection == i)) {
                    helpSection = i;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();

        if (ImGui::BeginChild("##hud_help_content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
            struct HelpRow {
                UiText title;
                UiText body;
            };
            const auto drawRows = [&](const HelpRow* rows, std::size_t count) {
                if (!ImGui::BeginTable("##hud_help_rows", 2,
                        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH
                            | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings)) {
                    return;
                }
                ImGui::TableSetupColumn("##hud_help_row_title", ImGuiTableColumnFlags_WidthFixed, ScaleUi(205.0f));
                ImGui::TableSetupColumn("##hud_help_row_body", ImGuiTableColumnFlags_WidthStretch);
                for (std::size_t i = 0; i < count; ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(visual.headerText, "%s", ui.Text(rows[i].title));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", ui.Text(rows[i].body));
                }
                ImGui::EndTable();
            };
            const auto heading = [&](UiText text) {
                ImGui::TextColored(visual.accent, "%s", ui.Text(text));
                ImGui::Separator();
            };

            switch (helpSection) {
            case 0: {
                static constexpr HelpRow rows[] = {
                    { UiText::HudHelpStepCreateTitle, UiText::HudHelpStepCreateBody },
                    { UiText::HudHelpStepElementsTitle, UiText::HudHelpStepElementsBody },
                    { UiText::HudHelpStepEditTitle, UiText::HudHelpStepEditBody },
                    { UiText::HudHelpStepPlaceTitle, UiText::HudHelpStepPlaceBody },
                    { UiText::HudHelpAutosaveTitle, UiText::HudHelpAutosaveBody },
                };
                heading(UiText::HudHelpQuickStart);
                drawRows(rows, std::size(rows));
                break;
            }
            case 1: {
                static constexpr HelpRow mouseRows[] = {
                    { UiText::HudHelpMouseLeftTitle, UiText::HudHelpMouseLeftBody },
                    { UiText::HudHelpMouseHandlesTitle, UiText::HudHelpMouseHandlesBody },
                    { UiText::HudHelpMouseEmptyTitle, UiText::HudHelpMouseEmptyBody },
                    { UiText::HudHelpMouseMiddleTitle, UiText::HudHelpMouseMiddleBody },
                };
                static constexpr HelpRow canvasRows[] = {
                    { UiText::HudHelpSnapTitle, UiText::HudHelpSnapBody },
                    { UiText::HudHelpCanvasHeightTitle, UiText::HudHelpCanvasHeightBody },
                };
                heading(UiText::HudHelpMouseTitle);
                drawRows(mouseRows, std::size(mouseRows));
                ImGui::Spacing();
                heading(UiText::HudHelpCanvas);
                ImGui::PushID("canvas_rows");
                drawRows(canvasRows, std::size(canvasRows));
                ImGui::PopID();
                break;
            }
            case 2: {
                static constexpr HelpRow rows[] = {
                    { UiText::HudHelpLayerOrderTitle, UiText::HudHelpLayerOrderBody },
                    { UiText::HudHelpGroupsTitle, UiText::HudHelpGroupsBody },
                    { UiText::HudHelpMultiSelectTitle, UiText::HudHelpMultiSelectBody },
                    { UiText::HudHelpLockHideTitle, UiText::HudHelpLockHideBody },
                };
                heading(UiText::HudHelpElements);
                drawRows(rows, std::size(rows));
                break;
            }
            case 3: {
                static constexpr HelpRow rows[] = {
                    { UiText::HudVariables, UiText::HudHelpVariablesBody },
                    { UiText::HudHelpExpressionsTitle, UiText::HudHelpExpressionsBody },
                    { UiText::HudHelpRefreshTitle, UiText::HudHelpRefreshBody },
                    { UiText::HudHelpConditionsTitle, UiText::HudHelpConditionsBody },
                };
                heading(UiText::HudHelpData);
                drawRows(rows, std::size(rows));
                break;
            }
            case 4:
            default: {
                static constexpr HelpRow rows[] = {
                    { UiText::HudHelpImportTitle, UiText::HudHelpImportBody },
                    { UiText::HudHelpAssetsTitle, UiText::HudHelpAssetsBody },
                    { UiText::HudHelpMigrationTitle, UiText::HudHelpMigrationBody },
                    { UiText::HudHelpFutureSchemaTitle, UiText::HudHelpFutureSchemaBody },
                };
                heading(UiText::HudHelpFiles);
                drawRows(rows, std::size(rows));
                break;
            }
            }
        }
        ImGui::EndChild();
        ImGui::End();
    }

    void DrawDeleteWidgetConfirmation() {
        UiSettings& ui = UiSettings::Instance();
        const std::string popupName = std::string(ui.Text(UiText::HudDeleteWidgetTitle)) + "##hud_delete_widget_confirm";
        if (deleteWidgetConfirmPending) {
            ImGui::OpenPopup(popupName.c_str());
            deleteWidgetConfirmPending = false;
        }
        if (!ImGui::BeginPopupModal(popupName.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        const HudWidget* widget = FindWidget(pendingDeleteWidgetId);
        ImGui::TextWrapped("%s", ui.Format(UiText::HudDeleteWidgetConfirmFormat, widget ? widget->name.c_str() : "").c_str());
        if (ImGui::Button(ui.Text(UiText::Delete), ScaleUi(120.0f, 0.0f))) {
            selectedWidgetId = pendingDeleteWidgetId;
            DeleteSelectedWidget();
            pendingDeleteWidgetId.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::Cancel), ScaleUi(120.0f, 0.0f))) {
            pendingDeleteWidgetId.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void CommitImportedWidget(HudWidget widget, int importSchema, const fs::path& path, int missingAssets) {
        selectedWidgetId = widget.id;
        selectedElementIds.clear();
        widgets.push_back(std::move(widget));
        MarkChanged(true);
        statusMessage = missingAssets > 0
            ? UiSettings::Instance().Format(UiText::HudImportedMissingAssetsFormat, missingAssets)
            : UiSettings::Instance().Text(UiText::HudImported);
        debuglog::WriteInfo("[hud] imported preset schema=%d path=%ls missingAssets=%d", importSchema, path.c_str(), missingAssets);
    }

    void DrawImportMissingAssetsConfirmation() {
        UiSettings& ui = UiSettings::Instance();
        const std::string popupTitle = std::string(ui.Text(UiText::HudImportMissingAssetsTitle)) + "###hud_import_missing_assets";
        if (importMissingAssetsConfirmPending) {
            ImGui::OpenPopup(popupTitle.c_str());
            importMissingAssetsConfirmPending = false;
        }
        if (!ImGui::BeginPopupModal(popupTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        if (!pendingImportWidget) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        ImGui::TextWrapped("%s", ui.Format(
            UiText::HudImportMissingAssetsConfirmFormat,
            static_cast<int>(pendingImportMissingAssets.size())).c_str());
        if (ImGui::BeginChild("##hud_import_missing_assets_list", ScaleUi(520.0f, 150.0f), true)) {
            for (const std::string& asset : pendingImportMissingAssets) {
                ImGui::BulletText("%s", asset.c_str());
            }
        }
        ImGui::EndChild();

        if (ImGui::Button(ui.Text(UiText::HudImportContinue), ScaleUi(160.0f, 0.0f))) {
            HudWidget widget = std::move(*pendingImportWidget);
            const int schema = pendingImportSchema;
            const fs::path path = pendingImportPath;
            const int missingAssets = static_cast<int>(pendingImportMissingAssets.size());
            pendingImportWidget.reset();
            pendingImportMissingAssets.clear();
            pendingImportPath.clear();
            pendingImportSchema = 0;
            CommitImportedWidget(std::move(widget), schema, path, missingAssets);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::Cancel), ScaleUi(120.0f, 0.0f))) {
            pendingImportWidget.reset();
            pendingImportMissingAssets.clear();
            pendingImportPath.clear();
            pendingImportSchema = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void DrawEditorWorkspace(IDirect3DDevice9* device) {
        HudWidget* widget = SelectedWidget();
        UiSettings& ui = UiSettings::Instance();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const HudEditorVisualStyle visual = HudEditorStyleTokens();
        if (!widget && bottomPanel != HudBottomPanel::Widgets) bottomPanel = HudBottomPanel::Widgets;

        const float tabBarHeight = ImGui::GetFrameHeight() + ScaleUi(10.0f);
        const float splitterHeight = bottomPanelCollapsed ? 0.0f : ScaleUi(7.0f);
        const float spacing = ImGui::GetStyle().ItemSpacing.y;
        const float minCanvasHeight = ScaleUi(220.0f);
        const float minPanelHeight = ScaleUi(150.0f);
        float panelContentHeight = 0.0f;
        float canvasHeight = std::max(1.0f, avail.y - tabBarHeight - spacing);
        if (!bottomPanelCollapsed) {
            const float sharedHeight = std::max(1.0f, avail.y - tabBarHeight - splitterHeight - spacing * 2.0f);
            const float effectiveMinPanelHeight = std::min(minPanelHeight, std::max(0.0f, sharedHeight - 1.0f));
            const float maxCanvasHeight = std::max(1.0f, sharedHeight - effectiveMinPanelHeight);
            const float effectiveMinCanvasHeight = std::min(minCanvasHeight, maxCanvasHeight);
            const float autoCanvasHeight = std::clamp(avail.y * 0.42f, ScaleUi(240.0f), ScaleUi(420.0f));
            const float requestedCanvasHeight = editorCanvasAutoHeight ? autoCanvasHeight : ScaleUi(editorCanvasHeight);
            canvasHeight = std::clamp(requestedCanvasHeight, effectiveMinCanvasHeight, maxCanvasHeight);
            panelContentHeight = std::max(0.0f, sharedHeight - canvasHeight);
        }

        if (BeginHudPanel("hud_editor_canvas_v3", ImVec2(0.0f, canvasHeight), visual,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            if (widget) DrawEditorCanvasMeasured(*widget, device);
            else {
                ImGui::TextWrapped("%s", ui.Text(UiText::HudNoWidgets));
                if (ImGui::Button(ui.Text(UiText::HudAddWidget))) AddWidget(MakeDefaultWidget());
            }
        }
        EndHudPanel();

        if (!bottomPanelCollapsed) {
            ImGui::InvisibleButton("##hud_bottom_splitter", ImVec2(-FLT_MIN, splitterHeight));
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ui.Text(UiText::HudCanvasSplitterHint));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                editorCanvasAutoHeight = true;
                QueueSave();
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                const float scale = std::max(0.5f, UiSettings::Instance().CurrentScale());
                editorCanvasAutoHeight = false;
                editorCanvasHeight = std::clamp((canvasHeight + ImGui::GetIO().MouseDelta.y) / scale, 220.0f, 900.0f);
                bottomPanelHeight = std::clamp((panelContentHeight - ImGui::GetIO().MouseDelta.y) / scale, 150.0f, 620.0f);
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) QueueSave();
        }

        const double panelBeginMs = HudPerfNowMs();
        const float panelHeight = tabBarHeight + panelContentHeight;
        if (BeginHudPanel("hud_editor_bottom_v3", ImVec2(0.0f, panelHeight), visual,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            const float collapseWidth = ImGui::GetFrameHeight();
            const float gap = ScaleUi(5.0f);
            const float tabsWidth = std::max(ScaleUi(240.0f), ImGui::GetContentRegionAvail().x - collapseWidth - gap);
            const float tabWidth = std::max(ScaleUi(78.0f), (tabsWidth - gap * 2.0f) / 3.0f);
            DrawBottomPanelTab(HudBottomPanel::Widgets, UiText::HudWidgets, tabWidth, visual);
            ImGui::SameLine(0.0f, gap);
            DrawBottomPanelTab(HudBottomPanel::Layers, UiText::HudLayers, tabWidth, visual);
            ImGui::SameLine(0.0f, gap);
            DrawBottomPanelTab(HudBottomPanel::Properties, UiText::HudProperties, tabWidth, visual);
            ImGui::SameLine(0.0f, gap);
            if (HudFlatIconButton(bottomPanelCollapsed ? ui_icons::AngleUp : ui_icons::AngleDown, "##hud_bottom_collapse",
                    ui.Text(bottomPanelCollapsed ? UiText::HudPanelExpand : UiText::HudPanelCollapse), ImVec2(collapseWidth, collapseWidth), visual)) {
                bottomPanelCollapsed = !bottomPanelCollapsed;
                QueueSave();
            }

            if (!bottomPanelCollapsed) {
                ImGui::Separator();
                const ImGuiWindowFlags contentFlags = bottomPanel == HudBottomPanel::Properties
                    ? ImGuiWindowFlags_None
                    : (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                if (ImGui::BeginChild("##hud_bottom_content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, contentFlags)) {
                    switch (bottomPanel) {
                    case HudBottomPanel::Layers:
                        if (widget) DrawLayersMeasured(*widget);
                        break;
                    case HudBottomPanel::Properties:
                        if (widget) {
                            const double propertiesBeginMs = HudPerfNowMs();
                            DrawPropertiesV3(*widget);
                            lastEditorStats.inspectorMs += HudPerfNowMs() - propertiesBeginMs;
                        }
                        break;
                    case HudBottomPanel::Widgets:
                    default:
                        DrawWidgetListMeasured();
                        break;
                    }
                }
                ImGui::EndChild();
            }
        }
        EndHudPanel();
        lastBottomPanelMs = HudPerfNowMs() - panelBeginMs;
    }

    void DrawMainTab(IDirect3DDevice9* device) {
        const double totalBeginMs = HudPerfNowMs();
        lastEditorStats = {};

        double stageBeginMs = totalBeginMs;
        EnsureLoaded();
        lastEditorStats.loadMs = HudPerfNowMs() - stageBeginMs;
        lastEditorStats.widgets = static_cast<int>(widgets.size());
        for (const HudWidget& widget : widgets) {
            lastEditorStats.elements += static_cast<int>(widget.elements.size());
        }
        lastEditorStats.selectedElements = static_cast<int>(selectedElementIds.size());

        stageBeginMs = HudPerfNowMs();
        BeginEditorFrame();
        PrepareConditionSnapshot();
        lastEditorStats.beginFrameMs = HudPerfNowMs() - stageBeginMs;

        if (configSaveBlocked) {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "%s", statusMessage.c_str());
            ImGui::BeginDisabled();
        }

        stageBeginMs = HudPerfNowMs();
        DrawToolbar();
        lastEditorStats.toolbarMs = HudPerfNowMs() - stageBeginMs;

        ImGui::Spacing();
        stageBeginMs = HudPerfNowMs();
        DrawEditorWorkspace(device);
        lastEditorStats.workspaceMs = HudPerfNowMs() - stageBeginMs;
        lastEditorStats.bottomPanelMs = lastBottomPanelMs;
        lastEditorStats.validationMs = lastValidationMs;
        lastEditorStats.saveTransactionMs = lastSaveTransactionMs;
        lastEditorStats.guidesMs = lastGuideMs;
        lastValidationMs = 0.0;
        lastSaveTransactionMs = 0.0;
        lastGuideMs = 0.0;
        if (configSaveBlocked) {
            ImGui::EndDisabled();
        }

        stageBeginMs = HudPerfNowMs();
        DrawVariablesPopup();
        DrawDeleteWidgetConfirmation();
        DrawImportMissingAssetsConfirmation();
        DrawHudHelpWindow();
        lastEditorStats.variablesPopupMs = HudPerfNowMs() - stageBeginMs;
        lastEditorStats.totalMs = HudPerfNowMs() - totalBeginMs;
    }

    void ExportSelectedWidget() {
        HudWidget* widget = SelectedWidget();
        if (!widget) {
            return;
        }
        EnsureAssetDirectories();
        std::string safeName = widget->name.empty() ? "widget" : widget->name;
        for (char& ch : safeName) {
            if (ch == '\\' || ch == '/' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
                ch = '_';
            }
        }
        const fs::path path = HudExportDirectory() / (MarkupRenderer::Utf8ToWide(safeName) + L".helperhud.json");
        jsonutil::JsonObject root;
        root["schema_version"] = kHudSchemaVersion;
        root["widget"] = SerializeWidget(*widget);
        std::string output;
        jsonutil::WriteJson(jsonutil::JsonValue(std::move(root)), output);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(output.data(), static_cast<std::streamsize>(output.size()));
        file.flush();
        if (!file) {
            statusMessage = UiSettings::Instance().Text(UiText::HudExportFailed);
            debuglog::WriteError("[hud] export failed path=%ls", path.c_str());
            return;
        }
        statusMessage = UiSettings::Instance().Format(UiText::HudExportedFormat, MarkupRenderer::WideToUtf8(path.wstring()).c_str());
        debuglog::WriteInfo("[hud] exported widget path=%ls", path.c_str());
    }

    void ImportWidget() {
        const std::optional<fs::path> selectedPath = OpenFileDialog(
            UiText::HudImport,
            BuildDialogFilter({
                { UiText::HudPresetFilesFilter, L"*.helperhud.json" },
                { UiText::NotepadAllFilesFilter, L"*.*" },
            }));
        if (!selectedPath) {
            return;
        }
        const fs::path path = *selectedPath;
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            statusMessage = UiSettings::Instance().Text(UiText::HudImportInvalid);
            return;
        }
        const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::string error;
        std::optional<jsonutil::JsonValue> parsed = jsonutil::ParseJson(source, error);
        const jsonutil::JsonObject* root = parsed ? parsed->TryObject() : nullptr;
        const jsonutil::JsonObject* widgetObject = root ? jsonutil::JsonObjectOrNull(root, "widget") : nullptr;
        const int importSchema = jsonutil::JsonNumberOr(root, "schema_version", 0);
        if (!widgetObject || importSchema < kPreviousHudSchemaVersion || importSchema > kHudSchemaVersion) {
            statusMessage = UiSettings::Instance().Text(UiText::HudImportInvalid);
            debuglog::WriteError("[hud] import rejected path=%ls schema=%d parse=%s", path.c_str(), importSchema, error.c_str());
            return;
        }
        HudWidget widget = DeserializeWidgetV2(*widgetObject);
        widget.id = GenerateId("hud");
        RemapClonedElementIds(widget.elements);
        std::unordered_set<std::string> validationIds;
        ValidateWidgetModel(widget, validationIds);
        std::vector<std::string> missingAssets;
        std::unordered_set<std::string> missingAssetIds;
        for (const HudElement& element : widget.elements) {
            if (element.type != ElementType::Image
                || !MarkupRenderer::IsSafeRelativeAssetPath(element.data.imagePath)
                || MightContainHudTag(element.data.imagePath)) {
                continue;
            }

            const fs::path assetPath = HudImagesDirectory() / MarkupRenderer::Utf8ToWide(element.data.imagePath);
            std::error_code assetExistsError;
            const bool assetExists = fs::exists(assetPath, assetExistsError);
            if (assetExistsError) {
                debuglog::WriteError("[hud] import asset stat failed path=%ls error=%d", assetPath.c_str(), assetExistsError.value());
            }
            if ((!assetExists || assetExistsError) && missingAssetIds.insert(element.data.imagePath).second) {
                missingAssets.push_back(element.data.imagePath);
            }
        }
        if (missingAssets.empty()) {
            CommitImportedWidget(std::move(widget), importSchema, path, 0);
            return;
        }

        pendingImportWidget = std::move(widget);
        pendingImportMissingAssets = std::move(missingAssets);
        pendingImportPath = path;
        pendingImportSchema = importSchema;
        importMissingAssetsConfirmPending = true;
    }
};

HudModule::HudModule() : impl_(std::make_unique<Impl>()) {
}

HudModule::~HudModule() = default;

HudModule::HudModule(HudModule&&) noexcept = default;

HudModule& HudModule::operator=(HudModule&&) noexcept = default;

void HudModule::OnProcessAttach(HMODULE module) {
    impl_->OnProcessAttach(module);
}

void HudModule::Shutdown() {
    impl_->Shutdown();
}

void HudModule::ReloadConfig() {
    impl_->ReloadConfig();
}

void HudModule::FlushPendingSaves() {
    impl_->FlushPendingSaves();
}

void HudModule::ReleaseDeviceResources() {
    impl_->ReleaseDeviceResources();
}

void HudModule::SetTagsModule(TagsModule* tagsModule) {
    impl_->tagsModule = tagsModule;
}

void HudModule::SetNotepadModule(NotepadModule* notepadModule) {
    impl_->notepadModule = notepadModule;
}

void HudModule::SetSampApi(SampApi* sampApi) {
    impl_->sampApi = sampApi;
}

void HudModule::SetPlacementInputBlocked(bool blocked) {
    impl_->placementInputBlocked = blocked;
}

void HudModule::DrawMainTab(IDirect3DDevice9* device) {
    impl_->DrawMainTab(device);
}

void HudModule::DrawOverlay(IDirect3DDevice9* device) {
    impl_->DrawOverlay(device);
}

HudModule::OverlayStats HudModule::LastOverlayStats() const {
    return impl_->lastOverlayStats;
}

HudModule::EditorStats HudModule::LastEditorStats() const {
    return impl_->lastEditorStats;
}

bool HudModule::WantsOverlayRender() {
    return impl_->WantsOverlayRender();
}

bool HudModule::WantsInputCapture() const {
    return impl_->WantsInputCapture();
}

bool HudModule::OnWindowMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    return impl_->OnWindowMessage(message, wparam, lparam);
}
