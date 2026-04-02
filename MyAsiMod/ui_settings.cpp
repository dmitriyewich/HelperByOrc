#include "ui_settings.h"

#include "app_config.h"
#include "hotkey_utils.h"
#include "json_utils.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace {

constexpr float kMinScaleMultiplier = 0.75f;
constexpr float kMaxScaleMultiplier = 2.00f;
constexpr float kReferenceWidth = 1920.0f;
constexpr float kReferenceHeight = 1080.0f;
constexpr float kMinAutoScale = 0.85f;
constexpr float kMaxAutoScale = 1.35f;
constexpr std::string_view kUiSectionName = "ui";
constexpr unsigned int kDefaultMenuToggleHotkey[] = { VK_CONTROL, 'Z' };

struct UiTextEntry {
    const char* ru = "";
    const char* en = "";
};

constexpr std::array<UiTextEntry, static_cast<std::size_t>(UiText::Count)> kUiTextEntries = {{
#define APP_UI_TEXT_ENTRY(id, ru, en) { ru, en },
    APP_UI_TEXTS(APP_UI_TEXT_ENTRY)
#undef APP_UI_TEXT_ENTRY
}};

std::string ToLower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

UiLanguage ParseLanguage(std::string_view value) {
    const std::string normalized = ToLower(value);
    return normalized == "en" || normalized == "english" ? UiLanguage::English : UiLanguage::Russian;
}

const char* LanguageId(UiLanguage language) {
    return language == UiLanguage::English ? "en" : "ru";
}

std::vector<unsigned int> DefaultMenuToggleHotkey() {
    return std::vector<unsigned int>(std::begin(kDefaultMenuToggleHotkey), std::end(kDefaultMenuToggleHotkey));
}

bool HasTriggerHotkeyKey(const std::vector<unsigned int>& keys) {
    return hotkeys::HasTriggerKey(keys);
}

std::vector<unsigned int> NormalizeHotkeyCombo(const std::vector<unsigned int>& keys, bool fallbackToDefault) {
    std::vector<unsigned int> normalized = hotkeys::NormalizeCombo(keys, HotkeyMode::ModifierTrigger);
    if (HasTriggerHotkeyKey(normalized)) {
        return normalized;
    }

    return fallbackToDefault ? DefaultMenuToggleHotkey() : std::vector<unsigned int>{};
}

std::vector<unsigned int> DeserializeMenuToggleHotkey(const jsonutil::JsonArray* array) {
    if (!array) {
        return DefaultMenuToggleHotkey();
    }

    std::vector<unsigned int> hotkey;
    hotkey.reserve(array->size());
    for (const jsonutil::JsonValue& value : *array) {
        if (const double* number = value.TryNumber()) {
            hotkey.push_back(static_cast<unsigned int>(*number));
        }
    }

    return NormalizeHotkeyCombo(hotkey, true);
}

jsonutil::JsonArray SerializeMenuToggleHotkey(const std::vector<unsigned int>& keys) {
    jsonutil::JsonArray array;
    for (const unsigned int key : NormalizeHotkeyCombo(keys, true)) {
        array.emplace_back(static_cast<int>(key));
    }
    return array;
}

} // namespace

UiSettings& UiSettings::Instance() {
    static UiSettings instance;
    return instance;
}

void UiSettings::Load() {
    const jsonutil::JsonObject section = AppConfig::Instance().ReadSectionObject(kUiSectionName);
    language_ = ParseLanguage(jsonutil::JsonStringOr(&section, "language", "ru"));
    autoScaleEnabled_ = jsonutil::JsonBoolOr(&section, "auto_scale", true);
    scaleMultiplier_ = std::clamp(
        jsonutil::JsonNumberOr<float>(&section, "scale_multiplier", 1.0f),
        kMinScaleMultiplier,
        kMaxScaleMultiplier);
    blockSampHotkeysInMainWindow_ = jsonutil::JsonBoolOr(&section, "block_samp_hotkeys_in_main_window", true);
    menuToggleHotkey_ = DeserializeMenuToggleHotkey(jsonutil::JsonArrayOrNull(&section, "open_menu_hotkey"));
    currentScale_ = 1.0f;
}

UiLanguage UiSettings::Language() const {
    return language_;
}

void UiSettings::SetLanguage(UiLanguage language) {
    if (language_ == language) {
        return;
    }

    language_ = language;
    QueueSave();
}

bool UiSettings::AutoScaleEnabled() const {
    return autoScaleEnabled_;
}

void UiSettings::SetAutoScaleEnabled(bool enabled) {
    if (autoScaleEnabled_ == enabled) {
        return;
    }

    autoScaleEnabled_ = enabled;
    QueueSave();
}

float UiSettings::ScaleMultiplier() const {
    return scaleMultiplier_;
}

void UiSettings::SetScaleMultiplier(float multiplier) {
    const float clamped = std::clamp(multiplier, kMinScaleMultiplier, kMaxScaleMultiplier);
    if (std::abs(scaleMultiplier_ - clamped) < 0.001f) {
        return;
    }

    scaleMultiplier_ = clamped;
    QueueSave();
}

bool UiSettings::BlockSampHotkeysInMainWindow() const {
    return blockSampHotkeysInMainWindow_;
}

void UiSettings::SetBlockSampHotkeysInMainWindow(bool enabled) {
    if (blockSampHotkeysInMainWindow_ == enabled) {
        return;
    }

    blockSampHotkeysInMainWindow_ = enabled;
    QueueSave();
}

const std::vector<unsigned int>& UiSettings::MenuToggleHotkey() const {
    return menuToggleHotkey_;
}

void UiSettings::SetMenuToggleHotkey(const std::vector<unsigned int>& hotkey) {
    const std::vector<unsigned int> normalized = NormalizeHotkeyCombo(hotkey, true);
    if (menuToggleHotkey_ == normalized) {
        return;
    }

    menuToggleHotkey_ = normalized;
    QueueSave();
}

void UiSettings::ResetToDefaults() {
    language_ = UiLanguage::Russian;
    autoScaleEnabled_ = true;
    scaleMultiplier_ = 1.0f;
    blockSampHotkeysInMainWindow_ = true;
    menuToggleHotkey_ = DefaultMenuToggleHotkey();
    QueueSave();
}

float UiSettings::UpdateScale(const ImVec2& displaySize) {
    const float baseScale = autoScaleEnabled_ ? ComputeAutoScale(displaySize) : 1.0f;
    currentScale_ = std::clamp(baseScale * scaleMultiplier_, kMinScaleMultiplier, kMaxScaleMultiplier);
    return currentScale_;
}

float UiSettings::CurrentScale() const {
    return currentScale_;
}

float UiSettings::Scale(float value) const {
    return value * currentScale_;
}

ImVec2 UiSettings::Scale(const ImVec2& value) const {
    return ImVec2(Scale(value.x), Scale(value.y));
}

const char* UiSettings::Text(UiText id) const {
    const UiTextEntry& entry = kUiTextEntries[static_cast<std::size_t>(id)];
    return language_ == UiLanguage::English ? entry.en : entry.ru;
}

std::string UiSettings::Format(UiText id, ...) const {
    const char* format = Text(id);

    va_list args;
    va_start(args, id);

    va_list copy;
    va_copy(copy, args);
    const int size = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);

    if (size <= 0) {
        va_end(args);
        return std::string(format);
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    std::vsnprintf(result.data(), result.size() + 1, format, args);
    va_end(args);
    return result;
}

const char* UiSettings::LanguageDisplayName(UiLanguage language) const {
    return language == UiLanguage::English ? Text(UiText::LanguageEnglish) : Text(UiText::LanguageRussian);
}

void UiSettings::QueueSave() const {
    jsonutil::JsonObject section;
    section["language"] = LanguageId(language_);
    section["auto_scale"] = autoScaleEnabled_;
    section["scale_multiplier"] = static_cast<double>(scaleMultiplier_);
    section["block_samp_hotkeys_in_main_window"] = blockSampHotkeysInMainWindow_;
    section["open_menu_hotkey"] = SerializeMenuToggleHotkey(menuToggleHotkey_);
    AppConfig::Instance().QueueSectionReplace(std::string(kUiSectionName), jsonutil::JsonValue(std::move(section)));
}

float UiSettings::ComputeAutoScale(const ImVec2& displaySize) const {
    if (displaySize.x <= 0.0f || displaySize.y <= 0.0f) {
        return 1.0f;
    }

    const float widthScale = displaySize.x / kReferenceWidth;
    const float heightScale = displaySize.y / kReferenceHeight;
    return std::clamp(std::min(widthScale, heightScale), kMinAutoScale, kMaxAutoScale);
}
