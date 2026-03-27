#include "ui_settings.h"

#include "app_config.h"
#include "json_utils.h"

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

void UiSettings::ResetToDefaults() {
    language_ = UiLanguage::Russian;
    autoScaleEnabled_ = true;
    scaleMultiplier_ = 1.0f;
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
