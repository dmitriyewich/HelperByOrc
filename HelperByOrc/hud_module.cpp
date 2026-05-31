#include "hud_module.h"

#include "app_config.h"
#include "conditions_module.h"
#include "debug_log.h"
#include "json_utils.h"
#include "markup_renderer.h"
#include "notepad_module.h"
#include "samp_api.h"
#include "tags_module.h"
#include "ui_icons.h"
#include "ui_settings.h"

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
#include <optional>
#include <set>
#include <shellapi.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kHudSectionName = "hud";
constexpr int kHudSchemaVersion = 2;
constexpr int kLegacyHudSchemaVersion = 1;
constexpr wchar_t kHudAssetsFolder[] = L"hud";
constexpr wchar_t kHudImagesFolder[] = L"images";
constexpr wchar_t kHudExportFolder[] = L"export";
constexpr wchar_t kHudImportFile[] = L"import.helperhud.json";
constexpr int kDefaultRefreshMs = 200;
constexpr int kUndoLimit = 50;
constexpr float kVirtualWidth = 1920.0f;
constexpr float kVirtualHeight = 1080.0f;

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
    float cachedNumber = 0.0f;
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
};

struct ImGuiStringUserData {
    std::string* value = nullptr;
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

int ImGuiStringResizeCallback(ImGuiInputTextCallbackData* data) {
    auto* userData = static_cast<ImGuiStringUserData*>(data->UserData);
    if (!userData || !userData->value) {
        return 0;
    }
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        userData->value->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = userData->value->data();
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
    ImGuiInputTextFlags flags = 0) {
    if (value.capacity() < 4096) {
        value.reserve(4096);
    }
    ImGuiStringUserData userData{ &value };
    return ImGui::InputTextMultiline(
        label,
        value.data(),
        value.capacity() + 1,
        size,
        flags | ImGuiInputTextFlags_CallbackResize,
        ImGuiStringResizeCallback,
        &userData);
}

ImVec4 WithAlpha(ImVec4 color, float alpha) {
    color.w *= std::clamp(alpha, 0.0f, 1.0f);
    return color;
}

ImU32 ColorU32(ImVec4 color, float alphaMultiplier = 1.0f) {
    color.w *= std::clamp(alphaMultiplier, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(color);
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
    data.icon = jsonutil::JsonStringOr(object, "icon", data.icon);
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

std::string ResolveIconGlyph(std::string_view name) {
    const std::string normalized = LowerAscii(name);
    if (normalized == "bars") return ui_icons::Bars;
    if (normalized == "book" || normalized == "note" || normalized == "file") return ui_icons::Book;
    if (normalized == "car") return ui_icons::Cubes;
    if (normalized == "check") return ui_icons::Check;
    if (normalized == "compass") return ui_icons::Compass;
    if (normalized == "copy") return ui_icons::Copy;
    if (normalized == "folder") return ui_icons::Folder;
    if (normalized == "gun" || normalized == "weapon") return ui_icons::Bolt;
    if (normalized == "image") return ui_icons::Image;
    if (normalized == "save") return ui_icons::SaveDisk;
    if (normalized == "star") return ui_icons::Star;
    if (normalized == "user") return ui_icons::Tags;
    if (normalized == "wrench") return ui_icons::Sliders;
    if (normalized == "terminal") return ui_icons::Terminal;
    return ui_icons::Star;
}

} // namespace

struct HudModule::Impl {
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
    std::uint64_t idCounter = 0;
    bool placementMode = false;
    std::string placementWidgetId;
    bool placementInputBlocked = false;
    bool editorOpen = false;
    bool conditionsPopupPending = false;
    bool elementConditionsPopupPending = false;
    bool deprecatedHelperVisibilityMigrated = false;
    bool configMigratedToV2 = false;
    bool snapEnabled = true;
    float gridSize = 8.0f;
    std::vector<std::string> undoStack;
    std::vector<std::string> redoStack;
    std::string frameSnapshot;
    bool frameUndoUsed = false;
    bool dragUndoCaptured = false;
    std::string activeDragElementId;
    std::string inlineEditElementId;

    void OnProcessAttach(HMODULE moduleHandle) {
        module = moduleHandle;
    }

    void Shutdown() {
        ReleaseDeviceResources();
        widgets.clear();
        selectedWidgetId.clear();
        selectedElementIds.clear();
        configLoaded = false;
        placementMode = false;
        placementWidgetId.clear();
        editorOpen = false;
        undoStack.clear();
        redoStack.clear();
        deprecatedHelperVisibilityMigrated = false;
        configMigratedToV2 = false;
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
        inlineEditElementId.clear();
        undoStack.clear();
        redoStack.clear();
        deprecatedHelperVisibilityMigrated = false;
        configMigratedToV2 = false;
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

    fs::path HudImportPath() const {
        return HudDirectory() / kHudImportFile;
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
        MarkChanged();
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
        for (HudElement& element : copy.elements) {
            element.id = GenerateId("hud_el");
            element.cachedText.clear();
            element.cachedImagePath.clear();
        }
        selectedWidgetId = copy.id;
        selectedElementIds.clear();
        widgets.push_back(std::move(copy));
        MarkChanged();
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
        MarkChanged();
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
        MarkChanged();
    }

    void DuplicateSelectedElements() {
        HudWidget* widget = SelectedWidget();
        if (!widget || selectedElementIds.empty()) {
            return;
        }
        std::vector<HudElement> copies;
        std::vector<std::string> newSelection;
        for (const std::string& id : selectedElementIds) {
            if (const HudElement* element = FindElement(*widget, id)) {
                HudElement copy = *element;
                copy.id = GenerateId("hud_el");
                copy.name += " copy";
                copy.parentId.clear();
                copy.x += 16.0f;
                copy.y += 16.0f;
                copy.z = NextZ(*widget) + static_cast<int>(copies.size());
                copy.cachedText.clear();
                copy.cachedImagePath.clear();
                newSelection.push_back(copy.id);
                copies.push_back(std::move(copy));
            }
        }
        if (copies.empty()) {
            return;
        }
        widget->elements.insert(widget->elements.end(), copies.begin(), copies.end());
        selectedElementIds = std::move(newSelection);
        MarkChanged();
    }

    void DeleteSelectedElements() {
        HudWidget* widget = SelectedWidget();
        if (!widget || selectedElementIds.empty()) {
            return;
        }
        std::set<std::string> ids(selectedElementIds.begin(), selectedElementIds.end());
        for (const HudElement& element : widget->elements) {
            if (!element.parentId.empty() && ids.find(element.parentId) != ids.end()) {
                ids.insert(element.id);
            }
        }
        const auto it = std::remove_if(widget->elements.begin(), widget->elements.end(), [&](const HudElement& element) {
            return ids.find(element.id) != ids.end();
        });
        if (it == widget->elements.end()) {
            return;
        }
        widget->elements.erase(it, widget->elements.end());
        selectedElementIds.clear();
        MarkChanged();
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
        for (const std::string& id : selectedElementIds) {
            if (HudElement* element = FindElement(*widget, id)) {
                if (element->type != ElementType::Group) {
                    element->parentId = groupId;
                }
            }
        }
        widget->elements.push_back(std::move(group));
        selectedElementIds = { groupId };
        MarkChanged();
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
        deprecatedHelperVisibilityMigrated = false;
        configMigratedToV2 = false;

        selectedWidgetId = jsonutil::JsonStringOr(&section, "selected_widget_id", "");
        const int schema = jsonutil::JsonNumberOr(&section, "schema_version", kLegacyHudSchemaVersion);
        if (const jsonutil::JsonArray* array = jsonutil::JsonArrayOrNull(&section, "widgets")) {
            for (const jsonutil::JsonValue& value : *array) {
                const jsonutil::JsonObject* object = value.TryObject();
                if (!object) {
                    continue;
                }
                HudWidget widget = schema >= kHudSchemaVersion
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
        if (saveMigrations && (configMigratedToV2 || deprecatedHelperVisibilityMigrated)) {
            debuglog::WriteInfo("[hud] migrated config to schema v2 widgets=%zu", widgets.size());
            QueueSave();
        }
    }

    void LoadConfig() {
        EnsureAssetDirectories();
        const jsonutil::JsonObject section = AppConfig::Instance().ReadSectionObject(kHudSectionName);
        LoadFromSection(section, true);
        debuglog::WriteInfo("[hud] config loaded schema=%d widgets=%zu", kHudSchemaVersion, widgets.size());
    }

    void QueueSave() const {
        AppConfig::Instance().QueueSectionReplace(std::string(kHudSectionName), SerializeConfig());
    }

    std::string Snapshot() const {
        std::string output;
        jsonutil::WriteJson(jsonutil::JsonValue(SerializeConfig()), output);
        return output;
    }

    void PushUndoSnapshot(const std::string& snapshot) {
        if (snapshot.empty()) {
            return;
        }
        if (!undoStack.empty() && undoStack.back() == snapshot) {
            return;
        }
        undoStack.push_back(snapshot);
        if (undoStack.size() > kUndoLimit) {
            undoStack.erase(undoStack.begin());
        }
        redoStack.clear();
    }

    void BeginEditorFrame() {
        frameSnapshot = Snapshot();
        frameUndoUsed = false;
    }

    void MarkChanged() {
        EnsureLoaded();
        if (!frameUndoUsed) {
            PushUndoSnapshot(frameSnapshot.empty() ? Snapshot() : frameSnapshot);
            frameUndoUsed = true;
        }
        for (HudWidget& widget : widgets) {
            widget.nextRefreshAtMs = 0;
        }
        QueueSave();
    }

    bool RestoreSnapshot(const std::string& snapshot) {
        std::string error;
        std::optional<jsonutil::JsonValue> parsed = jsonutil::ParseJson(snapshot, error);
        if (!parsed || !parsed->TryObject()) {
            debuglog::WriteError("[hud] failed to restore editor snapshot: %s", error.c_str());
            return false;
        }
        LoadFromSection(*parsed->TryObject(), false);
        QueueSave();
        return true;
    }

    void Undo() {
        if (undoStack.empty()) {
            return;
        }
        const std::string current = Snapshot();
        const std::string previous = undoStack.back();
        undoStack.pop_back();
        redoStack.push_back(current);
        RestoreSnapshot(previous);
    }

    void Redo() {
        if (redoStack.empty()) {
            return;
        }
        const std::string current = Snapshot();
        const std::string next = redoStack.back();
        redoStack.pop_back();
        undoStack.push_back(current);
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

    void RefreshWidgetCache(HudWidget& widget) {
        const std::uint64_t now = TickNow();
        if (now < widget.nextRefreshAtMs) {
            return;
        }

        for (HudElement& element : widget.elements) {
            if (element.type == ElementType::Text) {
                element.cachedText = ExpandText(element.data.text);
            } else if (element.type == ElementType::TextMarkup) {
                fs::path imageRoot;
                element.cachedText = ExpandText(SourceText(element, imageRoot));
            } else if (element.type == ElementType::Image) {
                element.cachedImagePath = ExpandText(element.data.imagePath);
            } else if (element.type == ElementType::ProgressBar) {
                const std::string expression = ExpandText(element.data.expression);
                element.cachedText = expression;
                element.cachedNumber = EvaluateNumberExpression(expression, element.data.defaultValue);
            }
        }

        widget.nextRefreshAtMs = widget.refreshMs <= 0
            ? 0
            : now + static_cast<std::uint64_t>(widget.refreshMs);
    }

    bool VisibilityBlocked(const HudVisibility& visibility) const {
        std::vector<bool> conditions = visibility.conditions;
        NormalizeConditionFlags(conditions);
        ConditionRuntimeContext conditionContext{};
        return ConditionsBlocked(conditions, visibility.conditionsCombine, sampApi, &conditionContext);
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
        const ImVec4& clip) const {
        const ImU32 textColor = ColorU32(element.style.text, element.style.textAlpha * element.opacity);
        if (element.style.shadowEnabled) {
            const ImVec2 offset(element.style.shadowOffsetX, element.style.shadowOffsetY);
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
            const float outline = std::max(1.0f, element.style.outlineSize);
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
        const float fontSize = std::max(1.0f, static_cast<float>(element.data.fontSize) * scale);
        const float lineHeight = fontSize * 1.18f;
        const ImVec4 clip(rect.Min.x, rect.Min.y, rect.Max.x, rect.Max.y);
        std::string text = element.cachedText;
        std::size_t start = 0;
        float y = rect.Min.y;
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
            DrawTextLine(drawList, element, rect, begin, finish, ImVec2(x, y), font, fontSize, clip);
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

        ImGui::PushClipRect(rect.Min, rect.Max, true);
        ImGui::SetCursorScreenPos(rect.Min);
        ImGui::PushStyleColor(ImGuiCol_Text, WithAlpha(element.style.text, element.style.textAlpha * element.opacity));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const float previousScale = 1.0f;
        ImGui::SetWindowFontScale(std::max(0.1f, static_cast<float>(element.data.fontSize) * scale / 16.0f));
        renderer.DrawText(element.cachedText, device, ImageRootForElement(element), MarkupRenderer::DrawOptions{ false });
        ImGui::SetWindowFontScale(previousScale);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::PopClipRect();
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

    void DrawIconElement(ImDrawList* drawList, const HudElement& element, const ImRect& rect, float scale) const {
        const std::string icon = ResolveIconGlyph(element.data.icon);
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

    void DrawElement(HudElement& element, IDirect3DDevice9* device, const ImVec2& origin, float scale, bool editor) {
        if (!ElementVisible(element)) {
            return;
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
    }

    std::vector<HudElement*> ElementsByZ(HudWidget& widget, bool descending = false) {
        std::vector<HudElement*> result;
        result.reserve(widget.elements.size());
        for (HudElement& element : widget.elements) {
            result.push_back(&element);
        }
        std::sort(result.begin(), result.end(), [&](const HudElement* left, const HudElement* right) {
            return descending ? left->z > right->z : left->z < right->z;
        });
        return result;
    }

    void DrawCanvas(HudWidget& widget, IDirect3DDevice9* device, const ImVec2& origin, float scale, bool editor) {
        RefreshWidgetCache(widget);
        for (HudElement* element : ElementsByZ(widget)) {
            DrawElement(*element, device, origin, scale, editor);
        }
    }

    void DrawWidgetOverlay(HudWidget& widget, IDirect3DDevice9* device) {
        if (!WidgetVisible(widget)) {
            return;
        }

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
                    const ImVec2 currentPos = ImGui::GetWindowPos();
                    UpdateOffsetFromWindowPos(widget, displaySize, ImVec2(currentPos.x + io.MouseDelta.x, currentPos.y + io.MouseDelta.y), canvasSize);
                }
                if (ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsMouseReleased(0)) {
                    QueueSave();
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    void DrawOverlay(IDirect3DDevice9* device) {
        EnsureLoaded();
        for (HudWidget& widget : widgets) {
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

    bool DrawOpenCanvasEditorButton(bool fullWidth) {
        UiSettings& ui = UiSettings::Instance();
        const std::string label = std::string(ui_icons::Edit) + " " + ui.Text(UiText::HudOpenCanvasEditor);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.36f, 0.58f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.47f, 0.72f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.30f, 0.50f, 1.0f));
        const bool clicked = ImGui::Button(label.c_str(), fullWidth ? ImVec2(ImGui::GetContentRegionAvail().x, 0.0f) : ImVec2(0.0f, 0.0f));
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s", ui.Text(UiText::HudOpenCanvasEditorHint));
        }
        if (clicked) {
            editorOpen = true;
        }
        return clicked;
    }

    void DrawToolbar() {
        UiSettings& ui = UiSettings::Instance();
        if (ImGui::Button((std::string(ui_icons::Plus) + " " + ui.Text(UiText::HudAddWidget)).c_str())) {
            AddWidget(MakeDefaultWidget());
        }
        ImGui::SameLine();
        DrawOpenCanvasEditorButton(false);
        ImGui::SameLine();
        if (ImGui::Button((std::string(ui_icons::Sliders) + " " + ui.Text(UiText::HudPresets)).c_str())) {
            ImGui::OpenPopup("##hud_presets");
        }
        if (ImGui::BeginPopup("##hud_presets")) {
            if (ImGui::MenuItem(ui.Text(UiText::HudPresetWeapon))) AddWidget(MakeWeaponPreset());
            if (ImGui::MenuItem(ui.Text(UiText::HudPresetFreeText))) AddWidget(MakeFreeTextPreset());
            if (ImGui::MenuItem(ui.Text(UiText::HudPresetPlayerStatus))) AddWidget(MakePlayerStatusPreset());
            if (ImGui::MenuItem(ui.Text(UiText::HudPresetVehicle))) AddWidget(MakeVehiclePreset());
            if (ImGui::MenuItem(ui.Text(UiText::HudPresetNoteCard))) AddWidget(MakeNotePreset());
            if (ImGui::MenuItem(ui.Text(UiText::HudPresetTimer))) AddWidget(MakeTimerPreset());
            if (ImGui::MenuItem(ui.Text(UiText::HudPresetDashboard))) AddWidget(MakeDashboardPreset());
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(ui_icons::Clone) + " " + ui.Text(UiText::HudDuplicateWidget)).c_str())) {
            DuplicateSelectedWidget();
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(ui_icons::Delete) + " " + ui.Text(UiText::Delete)).c_str())) {
            DeleteSelectedWidget();
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(ui_icons::FileExport) + " " + ui.Text(UiText::HudExport)).c_str())) {
            ExportSelectedWidget();
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(ui_icons::FileImport) + " " + ui.Text(UiText::HudImport)).c_str())) {
            ImportWidget();
        }
    }

    void DrawWidgetList(float height = 0.0f) {
        UiSettings& ui = UiSettings::Instance();
        if (ImGui::BeginChild("hud_widget_panel", ImVec2(0.0f, height), ImGuiChildFlags_None)) {
            const std::string searchHint = std::string(ui_icons::Search) + " " + ui.Text(UiText::HudSearchHint);
            InputTextWithHintString("##hud_search", searchHint.c_str(), searchQuery, 0, 128);
            ImGui::Spacing();

            if (ImGui::BeginChild("hud_widget_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
                if (widgets.empty()) {
                    ImGui::TextWrapped("%s", ui.Text(UiText::HudNoWidgets));
                } else {
                    const std::string needle = LowerAscii(searchQuery);
                    for (HudWidget& widget : widgets) {
                        if (!needle.empty() && LowerAscii(widget.name).find(needle) == std::string::npos) {
                            continue;
                        }
                        ImGui::PushID(widget.id.c_str());
                        const bool selected = widget.id == selectedWidgetId;
                        const std::string label = std::string(widget.enabled ? ui_icons::ToggleOn : ui_icons::ToggleOff)
                            + " " + widget.name;
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            selectedWidgetId = widget.id;
                            selectedElementIds.clear();
                            MarkChanged();
                        }
                        ImGui::PopID();
                    }
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
    }

    void DrawWidgetProperties(HudWidget& widget) {
        UiSettings& ui = UiSettings::Instance();
        ImGui::PushID(widget.id.c_str());
        ImGui::PushID("widget_properties");
        bool changed = false;
        changed |= ImGui::Checkbox(ui.Text(UiText::Enabled), &widget.enabled);
        changed |= InputTextString("##widget_name", widget.name, 0, 128);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ui.Text(UiText::Name));
        ImGui::SeparatorText(ui.Text(UiText::HudCanvas));
        changed |= ImGui::DragFloat(ui.Text(UiText::HudCanvasWidth), &widget.canvasWidth, 1.0f, 16.0f, 2000.0f, "%.0f");
        changed |= ImGui::DragFloat(ui.Text(UiText::HudCanvasHeight), &widget.canvasHeight, 1.0f, 16.0f, 2000.0f, "%.0f");
        ImGui::TextDisabled("%s", ui.Text(UiText::HudScalePolicy));
        changed |= DrawScalePolicyCombo(widget);

        ImGui::SeparatorText(ui.Text(UiText::HudPosition));
        ImGui::TextDisabled("%s", ui.Text(UiText::HudAnchor));
        changed |= DrawAnchorCombo(widget);
        changed |= ImGui::DragFloat(ui.Text(UiText::HudOffsetX), &widget.position.offsetX, 1.0f, -kVirtualWidth, kVirtualWidth, "%.0f");
        changed |= ImGui::DragFloat(ui.Text(UiText::HudOffsetY), &widget.position.offsetY, 1.0f, -kVirtualHeight, kVirtualHeight, "%.0f");
        if (ImGui::Button(ui.Text(UiText::HudPlaceOnScreen))) {
            placementMode = true;
            placementWidgetId = widget.id;
        }
        if (placementMode && placementWidgetId == widget.id) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.35f, 0.78f, 1.0f, 1.0f), "%s", ui.Text(UiText::HudPlacementActive));
        }

        ImGui::SeparatorText(ui.Text(UiText::HudVisibility));
        NormalizeConditionFlags(widget.visibility.conditions);
        const std::string conditionsButton = std::string(ui_icons::Sliders) + " " + ui.Text(UiText::HudVisibilityConditions);
        if (ImGui::Button(conditionsButton.c_str())) {
            conditionsPopupPending = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", HasSelectedCondition(widget.visibility.conditions) ? ui.Text(UiText::Enabled) : ui.Text(UiText::HotkeyNotSet));
        changed |= DrawConditionFlagsPopup(
            "##hud_widget_conditions_popup",
            conditionsPopupPending,
            UiText::HudVisibilityConditions,
            widget.visibility.conditions,
            &widget.visibility.conditionsCombine);
        if (ImGui::InputInt(ui.Text(UiText::HudRefreshMs), &widget.refreshMs, 50, 100)) {
            widget.nextRefreshAtMs = 0;
            changed = true;
        }
        widget.refreshMs = std::max(0, widget.refreshMs);
        if (widget.refreshMs == 0) {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "%s", ui.Text(UiText::HudRefreshZeroWarning));
        }
        if (changed) {
            MarkChanged();
        }
        ImGui::PopID();
        ImGui::PopID();
    }

    void DrawStyleProperties(HudElement& element) {
        UiSettings& ui = UiSettings::Instance();
        bool changed = false;
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

    void DrawElementDataProperties(HudElement& element) {
        UiSettings& ui = UiSettings::Instance();
        bool changed = false;
        ImGui::SeparatorText(ui.Text(UiText::HudSource));
        if (element.type == ElementType::Text) {
            changed |= InputTextMultilineString("##hud_element_text", element.data.text, ScaleUi(0.0f, 110.0f));
            if (ImGui::Button(ui.Text(UiText::HudInsertTagTime))) {
                element.data.text += "{time}";
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(ui.Text(UiText::HudInsertTagHp))) {
                element.data.text += "{health}";
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(ui.Text(UiText::HudInsertTagWeapon))) {
                element.data.text += "{myweapon}";
                changed = true;
            }
        } else if (element.type == ElementType::TextMarkup) {
            int sourceMode = element.data.sourceMode == SourceMode::NotepadNote ? 1 : 0;
            const char* sourceItems[] = { ui.Text(UiText::HudSourceInline), ui.Text(UiText::HudSourceNotepad) };
            if (ImGui::Combo("##hud_source_mode", &sourceMode, sourceItems, IM_ARRAYSIZE(sourceItems))) {
                element.data.sourceMode = sourceMode == 1 ? SourceMode::NotepadNote : SourceMode::Inline;
                changed = true;
            }
            if (element.data.sourceMode == SourceMode::Inline) {
                changed |= InputTextMultilineString("##hud_markup_text", element.data.text, ScaleUi(0.0f, 120.0f));
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
            changed |= DrawImageFitCombo(element);
            if (!MarkupRenderer::IsSafeRelativeAssetPath(element.data.imagePath)) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "%s", ui.Text(UiText::HudUnsafeImagePath));
            }
        } else if (element.type == ElementType::Icon) {
            changed |= InputTextString("##hud_icon_name", element.data.icon, 0, 64);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ui.Text(UiText::HudIconName));
        } else if (element.type == ElementType::ProgressBar) {
            changed |= InputTextString("##hud_expression", element.data.expression, 0, 256);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ui.Text(UiText::HudExpression));
            changed |= ImGui::DragFloat(ui.Text(UiText::HudMin), &element.data.minValue, 1.0f, -100000.0f, 100000.0f, "%.1f");
            changed |= ImGui::DragFloat(ui.Text(UiText::HudMax), &element.data.maxValue, 1.0f, -100000.0f, 100000.0f, "%.1f");
            changed |= ImGui::DragFloat(ui.Text(UiText::HudDefaultValue), &element.data.defaultValue, 1.0f, -100000.0f, 100000.0f, "%.1f");
        }
        if (element.type == ElementType::Text || element.type == ElementType::TextMarkup || element.type == ElementType::Icon) {
            changed |= ImGui::SliderInt(ui.Text(UiText::HudFontSize), &element.data.fontSize, 8, 96);
            changed |= DrawTextAlignCombo(element);
        }
        if (changed) {
            MarkChanged();
        }
    }

    void DrawElementProperties(HudWidget& widget) {
        UiSettings& ui = UiSettings::Instance();
        HudElement* element = PrimarySelectedElement(widget);
        if (!element) {
            ImGui::TextWrapped("%s", ui.Text(UiText::HudNoElementSelection));
            return;
        }

        bool changed = false;
        ImGui::PushID(element->id.c_str());
        ImGui::PushID("element_properties");
        changed |= InputTextString("##element_name", element->name, 0, 128);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ui.Text(UiText::Name));
        ImGui::TextDisabled("%s", ui.Text(ElementTypeLabelId(element->type)));
        changed |= ImGui::Checkbox(ui.Text(UiText::HudLocked), &element->locked);
        ImGui::SameLine();
        changed |= ImGui::Checkbox(ui.Text(UiText::HudHidden), &element->hidden);
        changed |= ImGui::SliderFloat(ui.Text(UiText::HudOpacity), &element->opacity, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::DragFloat(ui.Text(UiText::HudX), &element->x, 1.0f, -4000.0f, 4000.0f, "%.0f");
        changed |= ImGui::DragFloat(ui.Text(UiText::HudY), &element->y, 1.0f, -4000.0f, 4000.0f, "%.0f");
        changed |= ImGui::DragFloat(ui.Text(UiText::HudW), &element->width, 1.0f, 1.0f, 4000.0f, "%.0f");
        changed |= ImGui::DragFloat(ui.Text(UiText::HudH), &element->height, 1.0f, 0.0f, 4000.0f, "%.0f");
        changed |= ImGui::InputInt(ui.Text(UiText::HudZOrder), &element->z);

        if (changed) {
            MarkChanged();
        }

        ImGui::SeparatorText(ui.Text(UiText::HudVisibility));
        NormalizeConditionFlags(element->visibility.conditions);
        if (ImGui::Button((std::string(ui_icons::Sliders) + " " + ui.Text(UiText::HudVisibilityConditions)).c_str())) {
            elementConditionsPopupPending = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", HasSelectedCondition(element->visibility.conditions) ? ui.Text(UiText::Enabled) : ui.Text(UiText::HotkeyNotSet));
        if (DrawConditionFlagsPopup(
            "##hud_element_conditions_popup",
            elementConditionsPopupPending,
            UiText::HudVisibilityConditions,
            element->visibility.conditions,
            &element->visibility.conditionsCombine)) {
            MarkChanged();
        }

        DrawElementDataProperties(*element);
        DrawStyleProperties(*element);
        ImGui::PopID();
        ImGui::PopID();
    }

    void DrawLayers(HudWidget& widget, float height = 0.0f) {
        UiSettings& ui = UiSettings::Instance();
        if (ImGui::BeginChild("hud_layers_panel", ImVec2(0.0f, height), ImGuiChildFlags_None)) {
            ImGui::SeparatorText(ui.Text(UiText::HudLayers));
            if (ImGui::Button((std::string(ui_icons::Plus) + " " + ui.Text(UiText::HudAddElement)).c_str())) {
                ImGui::OpenPopup("##hud_add_element");
            }
            if (ImGui::BeginPopup("##hud_add_element")) {
                constexpr ElementType types[] = {
                    ElementType::Text,
                    ElementType::TextMarkup,
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
            ImGui::SameLine();
            if (ImGui::Button(ui.Text(UiText::HudGroup))) {
                GroupSelectedElements();
            }
            ImGui::SameLine();
            if (ImGui::Button(ui.Text(UiText::HudUngroup))) {
                UngroupSelectedElements();
            }

            if (ImGui::BeginChild("hud_layers_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
                for (HudElement* element : ElementsByZ(widget, true)) {
                    ImGui::PushID(element->id.c_str());
                    const bool selected = IsElementSelected(element->id);
                    const std::string prefix = std::string(element->hidden ? ui_icons::ToggleOff : ui_icons::ToggleOn)
                        + (element->locked ? std::string(" ") + ui_icons::Keyboard : "");
                    const std::string parentHint = element->parentId.empty() ? "" : "  ";
                    const std::string label = prefix + " " + parentHint + element->name + "##layer";
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        SelectElement(element->id, ImGui::GetIO().KeyCtrl);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
    }

    float SnapValue(float value) const {
        if (!snapEnabled || ImGui::GetIO().KeyAlt || gridSize <= 0.0f) {
            return value;
        }
        return std::round(value / gridSize) * gridSize;
    }

    void MoveElementAndChildren(HudWidget& widget, HudElement& element, float dx, float dy) {
        element.x = SnapValue(element.x + dx);
        element.y = SnapValue(element.y + dy);
        if (element.type != ElementType::Group) {
            return;
        }
        for (HudElement& child : widget.elements) {
            if (child.parentId == element.id) {
                child.x = SnapValue(child.x + dx);
                child.y = SnapValue(child.y + dy);
            }
        }
    }

    void DrawEditorGrid(ImDrawList* drawList, const ImRect& rect, float scale) const {
        const float grid = std::max(1.0f, gridSize * scale);
        const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.38f, 0.45f, 0.20f));
        for (float x = rect.Min.x; x <= rect.Max.x; x += grid) {
            drawList->AddLine(ImVec2(x, rect.Min.y), ImVec2(x, rect.Max.y), color);
        }
        for (float y = rect.Min.y; y <= rect.Max.y; y += grid) {
            drawList->AddLine(ImVec2(rect.Min.x, y), ImVec2(rect.Max.x, y), color);
        }
        drawList->AddLine(ImVec2(rect.Min.x + rect.GetWidth() * 0.5f, rect.Min.y), ImVec2(rect.Min.x + rect.GetWidth() * 0.5f, rect.Max.y), ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.78f, 1.0f, 0.26f)));
        drawList->AddLine(ImVec2(rect.Min.x, rect.Min.y + rect.GetHeight() * 0.5f), ImVec2(rect.Max.x, rect.Min.y + rect.GetHeight() * 0.5f), ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.78f, 1.0f, 0.26f)));
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
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float padding = ScaleUi(32.0f);
        const float fitScale = std::max(
            0.10f,
            std::min(
                (avail.x - padding * 2.0f) / std::max(1.0f, widget.canvasWidth),
                (avail.y - padding * 2.0f) / std::max(1.0f, widget.canvasHeight)));
        const float scale = std::min(4.0f, fitScale);
        const ImVec2 canvasSize(widget.canvasWidth * scale, widget.canvasHeight * scale);
        const ImVec2 origin(
            ImGui::GetCursorScreenPos().x + std::max(0.0f, (avail.x - canvasSize.x) * 0.5f),
            ImGui::GetCursorScreenPos().y + std::max(0.0f, (avail.y - canvasSize.y) * 0.5f));
        const ImRect canvasRect(origin, ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y));
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(canvasRect.Min, canvasRect.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(0.04f, 0.045f, 0.055f, 0.92f)), ScaleUi(2.0f));
        DrawEditorGrid(drawList, canvasRect, scale);

        ImGui::SetCursorScreenPos(canvasRect.Min);
        ImGui::InvisibleButton("##hud_editor_canvas_background", canvasRect.GetSize());
        if (ImGui::IsItemClicked() && !ImGui::GetIO().KeyCtrl) {
            selectedElementIds.clear();
        }

        DrawCanvas(widget, device, origin, scale, true);

        for (HudElement* element : ElementsByZ(widget, true)) {
            if (element->hidden) {
                continue;
            }
            const ImRect rect = ElementRect(*element, origin, scale);
            ImGui::PushID(element->id.c_str());
            ImGui::SetCursorScreenPos(rect.Min);
            ImGui::InvisibleButton("##hud_element_hit", rect.GetSize());
            if (ImGui::IsItemClicked()) {
                SelectElement(element->id, ImGui::GetIO().KeyCtrl);
                if (ImGui::IsMouseDoubleClicked(0) && element->type == ElementType::Text) {
                    inlineEditElementId = element->id;
                }
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0) && !element->locked) {
                if (!dragUndoCaptured || activeDragElementId != element->id) {
                    PushUndoSnapshot(Snapshot());
                    dragUndoCaptured = true;
                    activeDragElementId = element->id;
                }
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                MoveElementAndChildren(widget, *element, delta.x / scale, delta.y / scale);
                QueueSave();
            }
            if (ImGui::IsItemDeactivated()) {
                dragUndoCaptured = false;
                activeDragElementId.clear();
            }

            const bool selected = IsElementSelected(element->id);
            if (selected) {
                drawList->AddRect(rect.Min, rect.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.78f, 1.0f, 1.0f)), 0.0f, 0, ScaleUi(1.5f));
                const float handle = ScaleUi(9.0f);
                const ImRect handleRect(ImVec2(rect.Max.x - handle, rect.Max.y - handle), rect.Max);
                drawList->AddRectFilled(handleRect.Min, handleRect.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.78f, 1.0f, 1.0f)), ScaleUi(2.0f));
                ImGui::SetCursorScreenPos(handleRect.Min);
                ImGui::InvisibleButton("##hud_resize_handle", handleRect.GetSize());
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0) && !element->locked) {
                    if (!dragUndoCaptured || activeDragElementId != element->id + "_resize") {
                        PushUndoSnapshot(Snapshot());
                        dragUndoCaptured = true;
                        activeDragElementId = element->id + "_resize";
                    }
                    const ImVec2 delta = ImGui::GetIO().MouseDelta;
                    element->width = std::max(1.0f, SnapValue(element->width + delta.x / scale));
                    element->height = std::max(0.0f, SnapValue(element->height + delta.y / scale));
                    QueueSave();
                }
                DrawInlineTextEdit(*element, rect);
            }
            ImGui::PopID();
        }

        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, canvasRect.Max.y + ScaleUi(8.0f)));
        ImGui::TextDisabled("%s", snapEnabled ? ui.Text(UiText::HudSnapHint) : ui.Text(UiText::HudSnapOffHint));
    }

    void DrawCanvasEditorWindow(IDirect3DDevice9* device) {
        if (!editorOpen) {
            return;
        }
        HudWidget* widget = SelectedWidget();
        if (!widget) {
            return;
        }
        UiSettings& ui = UiSettings::Instance();
        ImGui::SetNextWindowSize(ScaleUi(1180.0f, 720.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(ui.Text(UiText::HudCanvasEditor), &editorOpen, ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::End();
            return;
        }

        BeginEditorFrame();
        if (ImGui::Button(ui.Text(UiText::HudUndo))) {
            Undo();
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::HudRedo))) {
            Redo();
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::HudDuplicateElement))) {
            DuplicateSelectedElements();
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::Delete))) {
            DeleteSelectedElements();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox(ui.Text(UiText::HudSnap), &snapEnabled)) {
            MarkChanged();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ScaleUi(92.0f));
        if (ImGui::DragFloat(ui.Text(UiText::HudGrid), &gridSize, 1.0f, 1.0f, 64.0f, "%.0f")) {
            MarkChanged();
        }

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float gapX = style.ItemSpacing.x;
        const float minLeftWidth = ScaleUi(220.0f);
        const float maxLeftWidth = ScaleUi(320.0f);
        const float minRightWidth = ScaleUi(300.0f);
        const float maxRightWidth = ScaleUi(390.0f);
        const float minCenterWidth = ScaleUi(360.0f);
        float leftWidth = std::clamp(avail.x * 0.18f, minLeftWidth, maxLeftWidth);
        float rightWidth = std::clamp(avail.x * 0.24f, minRightWidth, maxRightWidth);
        float centerWidth = avail.x - leftWidth - rightWidth - gapX * 2.0f;
        if (centerWidth < minCenterWidth) {
            float deficit = minCenterWidth - centerWidth;
            const float shrinkRight = std::min(std::max(0.0f, rightWidth - minRightWidth), deficit);
            rightWidth -= shrinkRight;
            deficit -= shrinkRight;
            const float shrinkLeft = std::min(std::max(0.0f, leftWidth - minLeftWidth), deficit);
            leftWidth -= shrinkLeft;
            centerWidth = std::max(ScaleUi(240.0f), avail.x - leftWidth - rightWidth - gapX * 2.0f);
        }

        if (ImGui::BeginChild("hud_editor_left", ImVec2(leftWidth, avail.y), false)) {
            const float leftHeight = ImGui::GetContentRegionAvail().y;
            const float minWidgetHeight = ScaleUi(130.0f);
            const float minLayerHeight = ScaleUi(150.0f);
            float widgetPanelHeight = leftHeight * 0.58f;
            if (leftHeight > minWidgetHeight + minLayerHeight + style.ItemSpacing.y) {
                widgetPanelHeight = std::clamp(
                    widgetPanelHeight,
                    minWidgetHeight,
                    leftHeight - minLayerHeight - style.ItemSpacing.y);
            } else {
                widgetPanelHeight = std::max(ScaleUi(80.0f), leftHeight * 0.52f);
            }
            DrawWidgetList(widgetPanelHeight);
            DrawLayers(*widget);
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("hud_editor_center", ImVec2(centerWidth, avail.y), true)) {
            DrawEditorCanvas(*widget, device);
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("hud_editor_right", ImVec2(rightWidth, avail.y), false)) {
            ImGui::SeparatorText(ui.Text(UiText::HudProperties));
            DrawWidgetProperties(*widget);
            ImGui::Separator();
            DrawElementProperties(*widget);
        }
        ImGui::EndChild();
        ImGui::End();
    }

    void DrawSelectedEditor(IDirect3DDevice9* device) {
        HudWidget* widget = SelectedWidget();
        UiSettings& ui = UiSettings::Instance();
        if (!widget) {
            ImGui::TextWrapped("%s", ui.Text(UiText::HudNoSelection));
            return;
        }

        BeginEditorFrame();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const bool vertical = avail.x < ScaleUi(760.0f);
        if (vertical) {
            DrawOpenCanvasEditorButton(true);
            ImGui::Spacing();
            DrawWidgetProperties(*widget);
            ImGui::SeparatorText(ui.Text(UiText::HudPreview));
            const float previewHeight = std::clamp(
                ImGui::GetContentRegionAvail().y * 0.48f,
                ScaleUi(160.0f),
                ScaleUi(360.0f));
            if (ImGui::BeginChild("hud_small_preview", ImVec2(0.0f, previewHeight), ImGuiChildFlags_Borders)) {
                DrawEditorCanvas(*widget, device);
            }
            ImGui::EndChild();
        } else {
            const float leftWidth = std::max(ScaleUi(330.0f), avail.x * 0.45f);
            if (ImGui::BeginChild("hud_widget_properties", ImVec2(leftWidth, 0.0f), false)) {
                DrawOpenCanvasEditorButton(true);
                ImGui::Spacing();
                DrawWidgetProperties(*widget);
            }
            ImGui::EndChild();
            ImGui::SameLine();
            if (ImGui::BeginChild("hud_preview_column", ImVec2(0.0f, 0.0f), false)) {
                ImGui::SeparatorText(ui.Text(UiText::HudPreview));
                DrawEditorCanvas(*widget, device);
            }
            ImGui::EndChild();
        }
    }

    void DrawMainTab(IDirect3DDevice9* device) {
        EnsureLoaded();
        UiSettings& ui = UiSettings::Instance();
        BeginEditorFrame();
        ImGui::SeparatorText(ui.Text(UiText::TabHud));
        ImGui::TextWrapped("%s", ui.Text(UiText::HudIntro));
        DrawToolbar();
        if (!statusMessage.empty()) {
            ImGui::TextDisabled("%s", statusMessage.c_str());
        }
        ImGui::Spacing();

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const bool vertical = avail.x < ScaleUi(820.0f);
        if (vertical) {
            float listHeight = std::clamp(avail.y * 0.32f, ScaleUi(150.0f), ScaleUi(300.0f));
            if (avail.y < ScaleUi(420.0f)) {
                listHeight = std::max(ScaleUi(110.0f), avail.y * 0.42f);
            }
            if (ImGui::BeginChild("hud_list_top", ImVec2(0.0f, listHeight), false)) {
                DrawWidgetList();
            }
            ImGui::EndChild();
            ImGui::Separator();
            if (ImGui::BeginChild("hud_editor_bottom", ImVec2(0.0f, 0.0f), false)) {
                DrawSelectedEditor(device);
            }
            ImGui::EndChild();
        } else {
            const float listWidth = ScaleUi(280.0f);
            if (ImGui::BeginChild("hud_list_left", ImVec2(listWidth, 0.0f), false)) {
                DrawWidgetList();
            }
            ImGui::EndChild();
            ImGui::SameLine();
            if (ImGui::BeginChild("hud_editor_right", ImVec2(0.0f, 0.0f), false)) {
                DrawSelectedEditor(device);
            }
            ImGui::EndChild();
        }
        DrawCanvasEditorWindow(device);
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
        statusMessage = UiSettings::Instance().Format(UiText::HudExportedFormat, MarkupRenderer::WideToUtf8(path.wstring()).c_str());
        debuglog::WriteInfo("[hud] exported widget path=%ls", path.c_str());
    }

    void ImportWidget() {
        const fs::path path = HudImportPath();
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            statusMessage = UiSettings::Instance().Format(UiText::HudImportMissingFormat, MarkupRenderer::WideToUtf8(path.wstring()).c_str());
            return;
        }
        const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::string error;
        std::optional<jsonutil::JsonValue> parsed = jsonutil::ParseJson(source, error);
        const jsonutil::JsonObject* root = parsed ? parsed->TryObject() : nullptr;
        const jsonutil::JsonObject* widgetObject = root ? jsonutil::JsonObjectOrNull(root, "widget") : nullptr;
        if (!widgetObject || jsonutil::JsonNumberOr(root, "schema_version", 0) != kHudSchemaVersion) {
            statusMessage = UiSettings::Instance().Text(UiText::HudImportInvalid);
            return;
        }
        HudWidget widget = DeserializeWidgetV2(*widgetObject);
        widget.id = GenerateId("hud");
        for (HudElement& element : widget.elements) {
            element.id = GenerateId("hud_el");
            element.parentId.clear();
        }
        selectedWidgetId = widget.id;
        selectedElementIds.clear();
        widgets.push_back(std::move(widget));
        MarkChanged();
        statusMessage = UiSettings::Instance().Text(UiText::HudImported);
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

bool HudModule::WantsOverlayRender() {
    return impl_->WantsOverlayRender();
}

bool HudModule::WantsInputCapture() const {
    return impl_->WantsInputCapture();
}

bool HudModule::OnWindowMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    return impl_->OnWindowMessage(message, wparam, lparam);
}
