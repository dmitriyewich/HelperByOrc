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

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <shellapi.h>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kHudSectionName = "hud";
constexpr int kHudSchemaVersion = 1;
constexpr wchar_t kHudAssetsFolder[] = L"hud";
constexpr wchar_t kHudImagesFolder[] = L"images";
constexpr int kDefaultRefreshMs = 200;

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

struct HudSource {
    SourceMode mode = SourceMode::Inline;
    std::string text{};
    std::string noteId{};
};

struct HudPosition {
    Anchor anchor = Anchor::TopLeft;
    float offsetX = 40.0f;
    float offsetY = 40.0f;
};

struct HudSize {
    bool autoSize = true;
    float width = 260.0f;
    float height = 96.0f;
    float scale = 1.0f;
};

struct HudStyleConfig {
    ImVec4 background = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);
    float backgroundAlpha = 0.72f;
    float paddingX = 10.0f;
    float paddingY = 8.0f;
    float rounding = 6.0f;
    bool border = false;
    bool shadow = false;
};

struct HudVisibility {
    std::vector<bool> conditions{};
    ConditionCombineMode conditionsCombine = ConditionCombineMode::RequireAny;
    bool hideWhenHelperOpen = true;
    bool hideWhenChatOpen = false;
    bool hideWhenDialogOpen = false;
};

struct HudWidget {
    std::string id{};
    std::string name{};
    bool enabled = true;
    HudSource source{};
    HudPosition position{};
    HudSize size{};
    HudStyleConfig style{};
    HudVisibility visibility{};
    int refreshMs = kDefaultRefreshMs;

    std::string cachedText{};
    std::uint64_t nextRefreshAtMs = 0;
    bool noteMissing = false;
};

struct ImGuiStringUserData {
    std::string* value = nullptr;
};

std::uint64_t UnixTimeNow() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

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

std::string SourceModeToString(SourceMode mode) {
    return mode == SourceMode::NotepadNote ? "notepad_note" : "inline";
}

SourceMode SourceModeFromString(std::string_view value) {
    return LowerAscii(value) == "notepad_note" ? SourceMode::NotepadNote : SourceMode::Inline;
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

std::string PathToUtf8(const fs::path& path) {
    return MarkupRenderer::WideToUtf8(path.wstring());
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

} // namespace

struct HudModule::Impl {
    HMODULE module = nullptr;
    TagsModule* tagsModule = nullptr;
    NotepadModule* notepadModule = nullptr;
    SampApi* sampApi = nullptr;
    bool configLoaded = false;
    std::vector<HudWidget> widgets;
    std::string selectedWidgetId;
    std::string searchQuery;
    std::string statusMessage;
    MarkupRenderer renderer;
    std::uint64_t idCounter = 0;
    bool placementMode = false;
    std::string placementWidgetId;
    bool conditionsPopupPending = false;

    void OnProcessAttach(HMODULE moduleHandle) {
        module = moduleHandle;
    }

    void Shutdown() {
        ReleaseDeviceResources();
        widgets.clear();
        selectedWidgetId.clear();
        configLoaded = false;
        placementMode = false;
        placementWidgetId.clear();
    }

    void ReloadConfig() {
        ReleaseDeviceResources();
        widgets.clear();
        selectedWidgetId.clear();
        searchQuery.clear();
        statusMessage.clear();
        configLoaded = false;
        placementMode = false;
        placementWidgetId.clear();
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

    void EnsureAssetDirectories() const {
        std::error_code error;
        fs::create_directories(HudImagesDirectory(), error);
        if (error) {
            debuglog::WriteError("[hud] failed to create images directory: %ls error=%d", HudImagesDirectory().c_str(), error.value());
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

    std::string GenerateWidgetId() {
        const std::uint64_t now = TickNow();
        for (;;) {
            char buffer[64]{};
            std::snprintf(buffer, sizeof(buffer), "hud_%llx_%llx",
                static_cast<unsigned long long>(now),
                static_cast<unsigned long long>(++idCounter));
            if (!FindWidget(buffer)) {
                return buffer;
            }
        }
    }

    HudWidget MakeDefaultWidget() {
        HudWidget widget;
        widget.id = GenerateWidgetId();
        widget.name = UiSettings::Instance().Text(UiText::HudDefaultWidgetName);
        widget.source.text = "{time}";
        widget.position.anchor = Anchor::TopLeft;
        widget.position.offsetX = 40.0f;
        widget.position.offsetY = 40.0f;
        NormalizeConditionFlags(widget.visibility.conditions);
        return widget;
    }

    HudWidget MakeWeaponPreset() {
        HudWidget widget = MakeDefaultWidget();
        widget.name = UiSettings::Instance().Text(UiText::HudPresetWeapon);
        widget.source.text =
            "#img(weapons/{myweaponid}.png, size(75,75), pos(0,0))\n"
            "#font18 [ifandor({myweapon}!=Fist?{8AF7FF}{myweapon} - {myweaponclip}:)]";
        widget.position.anchor = Anchor::BottomCenter;
        widget.position.offsetX = 0.0f;
        widget.position.offsetY = -180.0f;
        widget.style.backgroundAlpha = 0.0f;
        return widget;
    }

    HudWidget MakeFreeTextPreset() {
        HudWidget widget = MakeDefaultWidget();
        widget.name = UiSettings::Instance().Text(UiText::HudPresetFreeText);
        widget.source.text = "{8AF7FF}HelperByOrc HUD{FFFFFF}\n#font14{time}";
        widget.position.anchor = Anchor::TopLeft;
        widget.position.offsetX = 40.0f;
        widget.position.offsetY = 160.0f;
        return widget;
    }

    void AddWidget(HudWidget widget) {
        EnsureLoaded();
        selectedWidgetId = widget.id;
        widgets.push_back(std::move(widget));
        QueueSave();
    }

    void DuplicateSelectedWidget() {
        HudWidget* selected = SelectedWidget();
        if (!selected) {
            return;
        }
        HudWidget copy = *selected;
        copy.id = GenerateWidgetId();
        copy.name += " copy";
        copy.position.offsetX += 24.0f;
        copy.position.offsetY += 24.0f;
        copy.cachedText.clear();
        copy.nextRefreshAtMs = 0;
        AddWidget(std::move(copy));
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
        if (placementWidgetId == deletedId) {
            placementMode = false;
            placementWidgetId.clear();
        }
        QueueSave();
    }

    jsonutil::JsonObject SerializeWidget(const HudWidget& widget) const {
        jsonutil::JsonObject root;
        root["id"] = widget.id;
        root["name"] = widget.name;
        root["enabled"] = widget.enabled;

        jsonutil::JsonObject source;
        source["mode"] = SourceModeToString(widget.source.mode);
        source["text"] = widget.source.text;
        source["note_id"] = widget.source.noteId;
        root["source"] = std::move(source);

        jsonutil::JsonObject position;
        position["anchor"] = AnchorToString(widget.position.anchor);
        position["offset_x"] = widget.position.offsetX;
        position["offset_y"] = widget.position.offsetY;
        root["position"] = std::move(position);

        jsonutil::JsonObject size;
        size["auto_size"] = widget.size.autoSize;
        size["width"] = widget.size.width;
        size["height"] = widget.size.height;
        size["scale"] = widget.size.scale;
        root["size"] = std::move(size);

        jsonutil::JsonObject style;
        style["background_color"] = ColorToHex(widget.style.background);
        style["background_alpha"] = widget.style.backgroundAlpha;
        style["padding_x"] = widget.style.paddingX;
        style["padding_y"] = widget.style.paddingY;
        style["rounding"] = widget.style.rounding;
        style["border"] = widget.style.border;
        style["shadow"] = widget.style.shadow;
        root["style"] = std::move(style);

        jsonutil::JsonObject visibility;
        std::vector<bool> conditions = widget.visibility.conditions;
        NormalizeConditionFlags(conditions);
        visibility["conditions"] = SerializeBoolArray(conditions);
        visibility["conditions_combine"] = ConditionCombineModeId(widget.visibility.conditionsCombine);
        visibility["hide_when_helper_open"] = widget.visibility.hideWhenHelperOpen;
        visibility["hide_when_chat_open"] = widget.visibility.hideWhenChatOpen;
        visibility["hide_when_dialog_open"] = widget.visibility.hideWhenDialogOpen;
        root["visibility"] = std::move(visibility);

        root["refresh_ms"] = widget.refreshMs;
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

    HudWidget DeserializeWidget(const jsonutil::JsonObject& object) {
        HudWidget widget = MakeDefaultWidget();
        widget.id = jsonutil::JsonStringOr(&object, "id", widget.id);
        widget.name = jsonutil::JsonStringOr(&object, "name", widget.name);
        widget.enabled = jsonutil::JsonBoolOr(&object, "enabled", widget.enabled);

        if (const jsonutil::JsonObject* source = jsonutil::JsonObjectOrNull(&object, "source")) {
            widget.source.mode = SourceModeFromString(jsonutil::JsonStringOr(source, "mode", SourceModeToString(widget.source.mode)));
            widget.source.text = jsonutil::JsonStringOr(source, "text", widget.source.text);
            widget.source.noteId = jsonutil::JsonStringOr(source, "note_id", widget.source.noteId);
        }

        if (const jsonutil::JsonObject* position = jsonutil::JsonObjectOrNull(&object, "position")) {
            widget.position.anchor = AnchorFromString(jsonutil::JsonStringOr(position, "anchor", AnchorToString(widget.position.anchor)));
            widget.position.offsetX = jsonutil::JsonNumberOr(position, "offset_x", widget.position.offsetX);
            widget.position.offsetY = jsonutil::JsonNumberOr(position, "offset_y", widget.position.offsetY);
        }

        if (const jsonutil::JsonObject* size = jsonutil::JsonObjectOrNull(&object, "size")) {
            widget.size.autoSize = jsonutil::JsonBoolOr(size, "auto_size", widget.size.autoSize);
            widget.size.width = std::max(32.0f, jsonutil::JsonNumberOr(size, "width", widget.size.width));
            widget.size.height = std::max(24.0f, jsonutil::JsonNumberOr(size, "height", widget.size.height));
            widget.size.scale = std::clamp(jsonutil::JsonNumberOr(size, "scale", widget.size.scale), 0.5f, 3.0f);
        }

        if (const jsonutil::JsonObject* style = jsonutil::JsonObjectOrNull(&object, "style")) {
            widget.style.background = HexToColor(jsonutil::JsonStringOr(style, "background_color", ColorToHex(widget.style.background)), widget.style.background);
            widget.style.backgroundAlpha = std::clamp(jsonutil::JsonNumberOr(style, "background_alpha", widget.style.backgroundAlpha), 0.0f, 1.0f);
            widget.style.paddingX = std::clamp(jsonutil::JsonNumberOr(style, "padding_x", widget.style.paddingX), 0.0f, 80.0f);
            widget.style.paddingY = std::clamp(jsonutil::JsonNumberOr(style, "padding_y", widget.style.paddingY), 0.0f, 80.0f);
            widget.style.rounding = std::clamp(jsonutil::JsonNumberOr(style, "rounding", widget.style.rounding), 0.0f, 40.0f);
            widget.style.border = jsonutil::JsonBoolOr(style, "border", widget.style.border);
            widget.style.shadow = jsonutil::JsonBoolOr(style, "shadow", widget.style.shadow);
        }

        if (const jsonutil::JsonObject* visibility = jsonutil::JsonObjectOrNull(&object, "visibility")) {
            widget.visibility.conditions = DeserializeBoolArray(jsonutil::JsonArrayOrNull(visibility, "conditions"));
            NormalizeConditionFlags(widget.visibility.conditions);
            widget.visibility.conditionsCombine =
                NormalizeConditionCombineMode(jsonutil::JsonStringOr(visibility, "conditions_combine", "require_any"));
            widget.visibility.hideWhenHelperOpen = jsonutil::JsonBoolOr(visibility, "hide_when_helper_open", widget.visibility.hideWhenHelperOpen);
            widget.visibility.hideWhenChatOpen = jsonutil::JsonBoolOr(visibility, "hide_when_chat_open", widget.visibility.hideWhenChatOpen);
            widget.visibility.hideWhenDialogOpen = jsonutil::JsonBoolOr(visibility, "hide_when_dialog_open", widget.visibility.hideWhenDialogOpen);
        }

        widget.refreshMs = std::max(0, jsonutil::JsonNumberOr(&object, "refresh_ms", widget.refreshMs));
        widget.cachedText.clear();
        widget.nextRefreshAtMs = 0;
        return widget;
    }

    void LoadConfig() {
        EnsureAssetDirectories();
        widgets.clear();
        selectedWidgetId.clear();

        const jsonutil::JsonObject section = AppConfig::Instance().ReadSectionObject(kHudSectionName);
        selectedWidgetId = jsonutil::JsonStringOr(&section, "selected_widget_id", "");
        if (const jsonutil::JsonArray* array = jsonutil::JsonArrayOrNull(&section, "widgets")) {
            for (const jsonutil::JsonValue& value : *array) {
                const jsonutil::JsonObject* object = value.TryObject();
                if (!object) {
                    continue;
                }
                HudWidget widget = DeserializeWidget(*object);
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
        debuglog::WriteInfo("[hud] config loaded widgets=%zu", widgets.size());
    }

    void QueueSave() const {
        AppConfig::Instance().QueueSectionReplace(std::string(kHudSectionName), SerializeConfig());
    }

    std::string SourceText(HudWidget& widget, fs::path& imageRoot) {
        widget.noteMissing = false;
        imageRoot = HudImagesDirectory();
        if (widget.source.mode == SourceMode::Inline) {
            return widget.source.text;
        }
        if (!notepadModule || widget.source.noteId.empty()) {
            widget.noteMissing = true;
            return {};
        }
        NotepadModule::NoteContent note;
        if (!notepadModule->TryGetNote(widget.source.noteId, note)) {
            widget.noteMissing = true;
            return {};
        }
        imageRoot = notepadModule->ImagesDirectoryPath();
        return note.text;
    }

    std::string ExpandedText(HudWidget& widget) {
        const std::uint64_t now = TickNow();
        if (now < widget.nextRefreshAtMs) {
            return widget.cachedText;
        }
        fs::path imageRoot;
        const std::string source = SourceText(widget, imageRoot);
        widget.cachedText = tagsModule ? tagsModule->ExpandHudText(source) : source;
        widget.nextRefreshAtMs = widget.refreshMs <= 0
            ? 0
            : now + static_cast<std::uint64_t>(widget.refreshMs);
        return widget.cachedText;
    }

    bool WidgetVisible(HudWidget& widget, bool helperWindowOpen) {
        if (!widget.enabled) {
            return false;
        }
        if (helperWindowOpen && widget.visibility.hideWhenHelperOpen && !(placementMode && placementWidgetId == widget.id)) {
            return false;
        }
        if (sampApi && sampApi->sampModule() && sampApi->isSupportedVersion()) {
            if (widget.visibility.hideWhenChatOpen && sampApi->is_chat_opened()) {
                return false;
            }
            if (widget.visibility.hideWhenDialogOpen && sampApi->isDialogActive()) {
                return false;
            }
        }
        NormalizeConditionFlags(widget.visibility.conditions);
        ConditionRuntimeContext conditionContext{};
        conditionContext.helperUiCursorActive = helperWindowOpen;
        if (ConditionsBlocked(widget.visibility.conditions, widget.visibility.conditionsCombine, sampApi, &conditionContext)) {
            return false;
        }
        return true;
    }

    fs::path ImageRootForWidget(HudWidget& widget) {
        fs::path imageRoot = HudImagesDirectory();
        if (widget.source.mode == SourceMode::NotepadNote && notepadModule) {
            NotepadModule::NoteContent note;
            if (notepadModule->TryGetNote(widget.source.noteId, note)) {
                imageRoot = notepadModule->ImagesDirectoryPath();
            }
        }
        return imageRoot;
    }

    ImVec2 ScreenPosition(const HudWidget& widget, const ImVec2& displaySize) const {
        const float xScale = displaySize.x / 1920.0f;
        const float yScale = displaySize.y / 1080.0f;
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
        const float xScale = displaySize.x / 1920.0f;
        const float yScale = displaySize.y / 1080.0f;
        widget.position.offsetX = (pivotPos.x - base.x) / std::max(0.001f, xScale);
        widget.position.offsetY = (pivotPos.y - base.y) / std::max(0.001f, yScale);
    }

    void DrawWidgetOverlay(HudWidget& widget, IDirect3DDevice9* device, bool helperWindowOpen) {
        if (!WidgetVisible(widget, helperWindowOpen)) {
            return;
        }
        const std::string text = ExpandedText(widget);
        if (!MarkupRenderer::HasVisibleContent(text)) {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 displaySize = io.DisplaySize;
        if (displaySize.x <= 0.0f || displaySize.y <= 0.0f) {
            return;
        }

        const bool placing = placementMode && placementWidgetId == widget.id;
        const ImVec2 pos = ScreenPosition(widget, displaySize);
        const ImVec2 pivot = AnchorPivot(widget.position.anchor);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
        if (widget.size.autoSize) {
            ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        } else {
            ImGui::SetNextWindowSize(ScaleUi(widget.size.width * widget.size.scale, widget.size.height * widget.size.scale), ImGuiCond_Always);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ScaleUi(widget.style.paddingX * widget.size.scale, widget.style.paddingY * widget.size.scale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ScaleUi(widget.style.rounding));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, widget.style.border || placing ? ScaleUi(1.0f) : 0.0f);
        ImVec4 bg = widget.style.background;
        bg.w = widget.style.backgroundAlpha;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
        ImGui::PushStyleColor(ImGuiCol_Border, placing ? ImVec4(0.35f, 0.78f, 1.0f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Border));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoNav
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse;
        if (widget.size.autoSize) {
            flags |= ImGuiWindowFlags_AlwaysAutoResize;
        }
        if (!placing) {
            flags |= ImGuiWindowFlags_NoInputs;
        }

        const std::string windowId = "##hud_widget_" + widget.id;
        if (ImGui::Begin(windowId.c_str(), nullptr, flags)) {
            if (widget.style.shadow) {
                const ImVec2 min = ImGui::GetWindowPos();
                const ImVec2 max(min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y);
                ImGui::GetBackgroundDrawList()->AddRectFilled(
                    ImVec2(min.x + ScaleUi(4.0f), min.y + ScaleUi(5.0f)),
                    ImVec2(max.x + ScaleUi(4.0f), max.y + ScaleUi(5.0f)),
                    ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.28f)),
                    ScaleUi(widget.style.rounding));
            }
            const float previousScale = 1.0f;
            ImGui::SetWindowFontScale(widget.size.scale);
            renderer.DrawText(text, device, ImageRootForWidget(widget), MarkupRenderer::DrawOptions{ !widget.size.autoSize });
            ImGui::SetWindowFontScale(previousScale);

            if (placing) {
                const ImVec2 windowSize = ImGui::GetWindowSize();
                ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
                ImGui::InvisibleButton("##hud_drag_surface", windowSize);
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                    const ImVec2 currentPos = ImGui::GetWindowPos();
                    UpdateOffsetFromWindowPos(widget, displaySize, ImVec2(currentPos.x + io.MouseDelta.x, currentPos.y + io.MouseDelta.y), windowSize);
                }
                if (ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsMouseReleased(0)) {
                    QueueSave();
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    void DrawOverlay(IDirect3DDevice9* device, bool helperWindowOpen) {
        EnsureLoaded();
        for (HudWidget& widget : widgets) {
            DrawWidgetOverlay(widget, device, helperWindowOpen);
        }
    }

    bool WantsOverlayRender() {
        EnsureLoaded();
        return placementMode || std::any_of(widgets.begin(), widgets.end(), [](const HudWidget& widget) {
            return widget.enabled;
        });
    }

    bool WantsInputCapture() const {
        return placementMode;
    }

    bool OnWindowMessage(UINT message, WPARAM wparam, LPARAM) {
        if (!placementMode) {
            return false;
        }
        if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wparam == VK_ESCAPE) {
            placementMode = false;
            placementWidgetId.clear();
            return true;
        }
        return false;
    }

    void DrawToolbar() {
        UiSettings& ui = UiSettings::Instance();
        if (ImGui::Button((std::string(ui_icons::Plus) + " " + ui.Text(UiText::HudAddWidget)).c_str())) {
            AddWidget(MakeDefaultWidget());
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
        if (ImGui::Button((std::string(ui_icons::Sliders) + " " + ui.Text(UiText::HudPresets)).c_str())) {
            ImGui::OpenPopup("##hud_presets");
        }
        if (ImGui::BeginPopup("##hud_presets")) {
            if (ImGui::MenuItem(ui.Text(UiText::HudPresetWeapon))) {
                AddWidget(MakeWeaponPreset());
            }
            if (ImGui::MenuItem(ui.Text(UiText::HudPresetFreeText))) {
                AddWidget(MakeFreeTextPreset());
            }
            ImGui::EndPopup();
        }
    }

    void DrawWidgetList() {
        UiSettings& ui = UiSettings::Instance();
        const std::string searchHint = std::string(ui_icons::Search) + " " + ui.Text(UiText::HudSearchHint);
        InputTextWithHintString("##hud_search", searchHint.c_str(), searchQuery, 0, 128);
        ImGui::Spacing();
        if (widgets.empty()) {
            ImGui::TextWrapped("%s", ui.Text(UiText::HudNoWidgets));
            return;
        }

        if (ImGui::BeginChild("hud_widget_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
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
                    QueueSave();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    void DrawSourceEditor(HudWidget& widget, IDirect3DDevice9* device) {
        UiSettings& ui = UiSettings::Instance();
        ImGui::SeparatorText(ui.Text(UiText::HudSource));
        int sourceMode = widget.source.mode == SourceMode::NotepadNote ? 1 : 0;
        const char* sourceItems[] = { ui.Text(UiText::HudSourceInline), ui.Text(UiText::HudSourceNotepad) };
        if (ImGui::Combo("##hud_source_mode", &sourceMode, sourceItems, IM_ARRAYSIZE(sourceItems))) {
            widget.source.mode = sourceMode == 1 ? SourceMode::NotepadNote : SourceMode::Inline;
            widget.cachedText.clear();
            widget.nextRefreshAtMs = 0;
            QueueSave();
        }

        if (widget.source.mode == SourceMode::Inline) {
            ImGui::TextDisabled("%s", ui.Text(UiText::HudText));
            if (InputTextMultilineString("##hud_text", widget.source.text, ScaleUi(0.0f, 170.0f))) {
                widget.cachedText.clear();
                widget.nextRefreshAtMs = 0;
                QueueSave();
            }
        } else {
            const std::vector<NotepadModule::NoteSummary> notes = notepadModule ? notepadModule->NoteSummaries() : std::vector<NotepadModule::NoteSummary>{};
            std::string currentLabel = ui.Text(UiText::HudLinkedNoteMissing);
            for (const auto& note : notes) {
                if (note.id == widget.source.noteId) {
                    currentLabel = note.folderPath.empty() ? note.title : note.folderPath + " / " + note.title;
                    break;
                }
            }
            if (ImGui::BeginCombo("##hud_note_combo", currentLabel.c_str())) {
                for (const auto& note : notes) {
                    const std::string label = note.folderPath.empty() ? note.title : note.folderPath + " / " + note.title;
                    const bool selected = note.id == widget.source.noteId;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        widget.source.noteId = note.id;
                        widget.cachedText.clear();
                        widget.nextRefreshAtMs = 0;
                        QueueSave();
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (widget.noteMissing) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "%s", ui.Text(UiText::HudLinkedNoteMissing));
            }
        }

        fs::path sourceImageRoot;
        const std::string rawSource = SourceText(widget, sourceImageRoot);
        if (ContainsHudActionTag(rawSource)) {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "%s", ui.Text(UiText::HudActionTagsDisabled));
        }

        ImGui::SeparatorText(ui.Text(UiText::HudPreview));
        if (ImGui::BeginChild("hud_preview", ScaleUi(0.0f, 170.0f), ImGuiChildFlags_Borders)) {
            const std::string text = ExpandedText(widget);
            renderer.DrawText(text, device, ImageRootForWidget(widget), MarkupRenderer::DrawOptions{ !widget.size.autoSize });
        }
        ImGui::EndChild();
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

    void DrawLayoutEditor(HudWidget& widget) {
        UiSettings& ui = UiSettings::Instance();
        ImGui::SeparatorText(ui.Text(UiText::HudPosition));
        ImGui::TextDisabled("%s", ui.Text(UiText::HudAnchor));
        if (DrawAnchorCombo(widget)) {
            QueueSave();
        }
        bool changed = false;
        changed |= ImGui::DragFloat(ui.Text(UiText::HudOffsetX), &widget.position.offsetX, 1.0f, -1920.0f, 1920.0f, "%.0f");
        changed |= ImGui::DragFloat(ui.Text(UiText::HudOffsetY), &widget.position.offsetY, 1.0f, -1080.0f, 1080.0f, "%.0f");
        if (ImGui::Button(ui.Text(UiText::HudPlaceOnScreen))) {
            placementMode = true;
            placementWidgetId = widget.id;
        }
        if (placementMode && placementWidgetId == widget.id) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.35f, 0.78f, 1.0f, 1.0f), "%s", ui.Text(UiText::HudPlacementActive));
        }

        ImGui::SeparatorText(ui.Text(UiText::HudSize));
        changed |= ImGui::Checkbox(ui.Text(UiText::HudAutoSize), &widget.size.autoSize);
        changed |= ImGui::DragFloat(ui.Text(UiText::HudWidth), &widget.size.width, 1.0f, 32.0f, 1000.0f, "%.0f");
        changed |= ImGui::DragFloat(ui.Text(UiText::HudHeight), &widget.size.height, 1.0f, 24.0f, 1000.0f, "%.0f");
        changed |= ImGui::SliderFloat(ui.Text(UiText::HudScale), &widget.size.scale, 0.5f, 3.0f, "%.2f");

        ImGui::SeparatorText(ui.Text(UiText::HudStyle));
        changed |= ImGui::ColorEdit3(ui.Text(UiText::HudBackground), &widget.style.background.x, ImGuiColorEditFlags_NoInputs);
        changed |= ImGui::SliderFloat(ui.Text(UiText::HudBackgroundAlpha), &widget.style.backgroundAlpha, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::DragFloat(ui.Text(UiText::HudPaddingX), &widget.style.paddingX, 0.5f, 0.0f, 80.0f, "%.1f");
        changed |= ImGui::DragFloat(ui.Text(UiText::HudPaddingY), &widget.style.paddingY, 0.5f, 0.0f, 80.0f, "%.1f");
        changed |= ImGui::DragFloat(ui.Text(UiText::HudRounding), &widget.style.rounding, 0.5f, 0.0f, 40.0f, "%.1f");
        changed |= ImGui::Checkbox(ui.Text(UiText::HudBorder), &widget.style.border);
        ImGui::SameLine();
        changed |= ImGui::Checkbox(ui.Text(UiText::HudShadow), &widget.style.shadow);

        ImGui::SeparatorText(ui.Text(UiText::HudVisibility));
        NormalizeConditionFlags(widget.visibility.conditions);
        const std::string conditionsButton = std::string(ui_icons::Sliders) + " " + ui.Text(UiText::HudVisibilityConditions);
        if (ImGui::Button(conditionsButton.c_str())) {
            conditionsPopupPending = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", HasSelectedCondition(widget.visibility.conditions) ? ui.Text(UiText::Enabled) : ui.Text(UiText::HotkeyNotSet));
        changed |= DrawConditionFlagsPopup(
            "##hud_conditions_popup",
            conditionsPopupPending,
            UiText::HudVisibilityConditions,
            widget.visibility.conditions,
            &widget.visibility.conditionsCombine);
        changed |= ImGui::Checkbox(ui.Text(UiText::HudHideWhenHelperOpen), &widget.visibility.hideWhenHelperOpen);
        changed |= ImGui::Checkbox(ui.Text(UiText::HudHideWhenChatOpen), &widget.visibility.hideWhenChatOpen);
        changed |= ImGui::Checkbox(ui.Text(UiText::HudHideWhenDialogOpen), &widget.visibility.hideWhenDialogOpen);
        if (ImGui::InputInt(ui.Text(UiText::HudRefreshMs), &widget.refreshMs, 50, 100)) {
            widget.nextRefreshAtMs = 0;
            changed = true;
        }
        widget.refreshMs = std::max(0, widget.refreshMs);
        if (widget.refreshMs == 0) {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "%s", ui.Text(UiText::HudRefreshZeroWarning));
        }

        if (changed) {
            QueueSave();
        }
    }

    void DrawSelectedEditor(IDirect3DDevice9* device) {
        UiSettings& ui = UiSettings::Instance();
        HudWidget* widget = SelectedWidget();
        if (!widget) {
            ImGui::TextWrapped("%s", ui.Text(UiText::HudNoSelection));
            return;
        }

        bool changed = false;
        changed |= ImGui::Checkbox(ui.Text(UiText::Enabled), &widget->enabled);
        changed |= InputTextString(("##" + std::string(ui.Text(UiText::Name))).c_str(), widget->name, 0, 128);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ui.Text(UiText::Name));
        if (changed) {
            QueueSave();
        }

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const bool vertical = avail.x < ScaleUi(760.0f);
        if (vertical) {
            DrawSourceEditor(*widget, device);
            DrawLayoutEditor(*widget);
        } else {
            const float leftWidth = std::max(ScaleUi(330.0f), avail.x * 0.52f);
            if (ImGui::BeginChild("hud_source_column", ImVec2(leftWidth, 0.0f), false)) {
                DrawSourceEditor(*widget, device);
            }
            ImGui::EndChild();
            ImGui::SameLine();
            if (ImGui::BeginChild("hud_layout_column", ImVec2(0.0f, 0.0f), false)) {
                DrawLayoutEditor(*widget);
            }
            ImGui::EndChild();
        }
    }

    void DrawMainTab(IDirect3DDevice9* device) {
        EnsureLoaded();
        UiSettings& ui = UiSettings::Instance();
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
            if (ImGui::BeginChild("hud_list_top", ImVec2(0.0f, ScaleUi(190.0f)), false)) {
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

void HudModule::DrawMainTab(IDirect3DDevice9* device) {
    impl_->DrawMainTab(device);
}

void HudModule::DrawOverlay(IDirect3DDevice9* device, bool helperWindowOpen) {
    impl_->DrawOverlay(device, helperWindowOpen);
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
