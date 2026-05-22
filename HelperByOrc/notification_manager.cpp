#include "notification_manager.h"

#include "app_config.h"
#include "debug_log.h"
#include "json_utils.h"
#include "ui_settings.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace {

constexpr char kNotificationsSectionName[] = "notifications";

constexpr double kMinDurationMs = 500.0;
constexpr double kMaxDurationMs = 15000.0;
constexpr double kMinDedupeMs = 0.0;
constexpr double kMaxDedupeMs = 10000.0;
constexpr int kMinMaxVisible = 1;
constexpr int kMaxMaxVisible = 10;
constexpr int kMinMaxQueue = 1;
constexpr int kMaxMaxQueue = 64;
constexpr float kMinWidth = 200.0f;
constexpr float kMaxWidth = 560.0f;
constexpr float kMinOpacity = 0.20f;
constexpr float kMaxOpacity = 1.0f;
constexpr float kMinOffset = -500.0f;
constexpr float kMaxOffset = 500.0f;

const char* ChannelId(NotificationChannel channel) {
    switch (channel) {
    case NotificationChannel::Log:
        return "log";
    case NotificationChannel::Popup:
    default:
        return "popup";
    }
}

NotificationChannel ParseChannel(std::string_view value) {
    if (value == "log") {
        return NotificationChannel::Log;
    }
    return NotificationChannel::Popup;
}

const char* PositionId(NotificationPosition position) {
    switch (position) {
    case NotificationPosition::TopLeft:
        return "top_left";
    case NotificationPosition::TopCenter:
        return "top_center";
    case NotificationPosition::MiddleLeft:
        return "middle_left";
    case NotificationPosition::MiddleCenter:
        return "middle_center";
    case NotificationPosition::MiddleRight:
        return "middle_right";
    case NotificationPosition::BottomLeft:
        return "bottom_left";
    case NotificationPosition::BottomCenter:
        return "bottom_center";
    case NotificationPosition::BottomRight:
        return "bottom_right";
    case NotificationPosition::TopRight:
    default:
        return "top_right";
    }
}

NotificationPosition ParsePosition(std::string_view value) {
    if (value == "top_left") {
        return NotificationPosition::TopLeft;
    }
    if (value == "top_center") {
        return NotificationPosition::TopCenter;
    }
    if (value == "middle_left") {
        return NotificationPosition::MiddleLeft;
    }
    if (value == "middle_center") {
        return NotificationPosition::MiddleCenter;
    }
    if (value == "middle_right") {
        return NotificationPosition::MiddleRight;
    }
    if (value == "bottom_left") {
        return NotificationPosition::BottomLeft;
    }
    if (value == "bottom_center") {
        return NotificationPosition::BottomCenter;
    }
    if (value == "bottom_right") {
        return NotificationPosition::BottomRight;
    }
    return NotificationPosition::TopRight;
}

const char* GroupId(NotificationGroup group) {
    switch (group) {
    case NotificationGroup::TagErrors:
        return "tag_errors";
    case NotificationGroup::SampDialogErrors:
        return "samp_dialog_errors";
    case NotificationGroup::Success:
        return "success";
    case NotificationGroup::Confirmation:
        return "confirmation";
    case NotificationGroup::Validation:
        return "validation";
    case NotificationGroup::BinderErrors:
    default:
        return "binder_errors";
    }
}

ImVec4 SeverityColor(NotificationSeverity severity, float opacity) {
    switch (severity) {
    case NotificationSeverity::Success:
        return ImVec4(0.20f, 0.35f, 0.18f, opacity);
    case NotificationSeverity::Warning:
        return ImVec4(0.55f, 0.30f, 0.10f, opacity);
    case NotificationSeverity::Error:
        return ImVec4(0.55f, 0.20f, 0.20f, opacity);
    case NotificationSeverity::Info:
    default:
        return ImVec4(0.12f, 0.16f, 0.22f, opacity);
    }
}

ImVec2 PositionPivot(NotificationPosition position) {
    switch (position) {
    case NotificationPosition::TopLeft:
        return ImVec2(0.0f, 0.0f);
    case NotificationPosition::TopCenter:
        return ImVec2(0.5f, 0.0f);
    case NotificationPosition::MiddleLeft:
        return ImVec2(0.0f, 0.5f);
    case NotificationPosition::MiddleCenter:
        return ImVec2(0.5f, 0.5f);
    case NotificationPosition::MiddleRight:
        return ImVec2(1.0f, 0.5f);
    case NotificationPosition::BottomLeft:
        return ImVec2(0.0f, 1.0f);
    case NotificationPosition::BottomCenter:
        return ImVec2(0.5f, 1.0f);
    case NotificationPosition::BottomRight:
        return ImVec2(1.0f, 1.0f);
    case NotificationPosition::TopRight:
    default:
        return ImVec2(1.0f, 0.0f);
    }
}

ImVec2 ResolveWindowPosition(const ImVec2& displaySize, const NotificationSettings& settings) {
    switch (settings.position) {
    case NotificationPosition::TopLeft:
        return ImVec2(settings.offsetX, settings.offsetY);
    case NotificationPosition::TopCenter:
        return ImVec2(displaySize.x * 0.5f + settings.offsetX, settings.offsetY);
    case NotificationPosition::TopRight:
        return ImVec2(displaySize.x - settings.offsetX, settings.offsetY);
    case NotificationPosition::MiddleLeft:
        return ImVec2(settings.offsetX, displaySize.y * 0.5f + settings.offsetY);
    case NotificationPosition::MiddleCenter:
        return ImVec2(displaySize.x * 0.5f + settings.offsetX, displaySize.y * 0.5f + settings.offsetY);
    case NotificationPosition::MiddleRight:
        return ImVec2(displaySize.x - settings.offsetX, displaySize.y * 0.5f + settings.offsetY);
    case NotificationPosition::BottomLeft:
        return ImVec2(settings.offsetX, displaySize.y - settings.offsetY);
    case NotificationPosition::BottomCenter:
        return ImVec2(displaySize.x * 0.5f + settings.offsetX, displaySize.y - settings.offsetY);
    case NotificationPosition::BottomRight:
        return ImVec2(displaySize.x - settings.offsetX, displaySize.y - settings.offsetY);
    default:
        return ImVec2(displaySize.x - settings.offsetX, settings.offsetY);
    }
}

std::string MakeSignature(NotificationGroup group, NotificationSeverity severity, std::string_view text) {
    return std::to_string(static_cast<int>(group)) + ":" + std::to_string(static_cast<int>(severity)) + ":" + std::string(text);
}

std::string MakeUserSignature(NotificationSeverity severity, std::string_view text) {
    return std::string("user:") + std::to_string(static_cast<int>(severity)) + ":" + std::string(text);
}

} // namespace

std::size_t NotificationGroupIndex(NotificationGroup group) {
    const int index = static_cast<int>(group);
    if (index < 0 || index >= static_cast<int>(kNotificationGroupCount)) {
        return 0;
    }
    return static_cast<std::size_t>(index);
}

NotificationSettings::NotificationSettings() {
    groups.fill(true);
}

NotificationManager::NotificationManager() = default;

void NotificationManager::LoadConfig() {
    NotificationSettings loaded{};
    const jsonutil::JsonValue rawSection = AppConfig::Instance().ReadSection(kNotificationsSectionName);
    const jsonutil::JsonObject* section = rawSection.TryObject();
    bool removeLegacyValidationGroup = false;

    if (section) {
        loaded.enabled = jsonutil::JsonBoolOr(section, "enabled", loaded.enabled);
        loaded.channel = ParseChannel(jsonutil::JsonStringOr(section, "channel", ChannelId(loaded.channel)));
        loaded.position = ParsePosition(jsonutil::JsonStringOr(section, "position", PositionId(loaded.position)));
        loaded.offsetX = jsonutil::JsonNumberOr<float>(section, "offset_x", loaded.offsetX);
        loaded.offsetY = jsonutil::JsonNumberOr<float>(section, "offset_y", loaded.offsetY);
        loaded.durationMs = jsonutil::JsonNumberOr<double>(section, "duration_ms", loaded.durationMs);
        loaded.maxVisible = jsonutil::JsonNumberOr<int>(section, "max_visible", loaded.maxVisible);
        loaded.maxQueue = jsonutil::JsonNumberOr<int>(section, "max_queue", loaded.maxQueue);
        loaded.dedupeWindowMs = jsonutil::JsonNumberOr<double>(section, "dedupe_window_ms", loaded.dedupeWindowMs);
        loaded.width = jsonutil::JsonNumberOr<float>(section, "width", loaded.width);
        loaded.opacity = jsonutil::JsonNumberOr<float>(section, "opacity", loaded.opacity);

        if (const jsonutil::JsonObject* groups = jsonutil::JsonObjectOrNull(section, "groups")) {
            for (int index = 0; index < static_cast<int>(kNotificationGroupCount); ++index) {
                const auto group = static_cast<NotificationGroup>(index);
                if (group == NotificationGroup::Validation) {
                    removeLegacyValidationGroup = groups->find(GroupId(group)) != groups->end();
                    continue;
                }
                loaded.groups[NotificationGroupIndex(group)] = jsonutil::JsonBoolOr(
                    groups,
                    GroupId(group),
                    loaded.groups[NotificationGroupIndex(group)]);
            }
        }
    }

    settings_ = NormalizeSettings(loaded);
    entries_.clear();
    lastEmittedAt_.clear();

    if (!section || removeLegacyValidationGroup || !SettingsEqual(loaded, settings_)) {
        QueueSave();
    }

    debuglog::WriteInfo(
        "NotificationManager loaded enabled=%d channel=%s position=%s max_visible=%d max_queue=%d",
        settings_.enabled ? 1 : 0,
        ChannelId(settings_.channel),
        PositionId(settings_.position),
        settings_.maxVisible,
        settings_.maxQueue);
}

void NotificationManager::Shutdown() {
    entries_.clear();
    lastEmittedAt_.clear();
}

const NotificationSettings& NotificationManager::Settings() const {
    return settings_;
}

void NotificationManager::ApplySettings(const NotificationSettings& settings) {
    const NotificationSettings normalized = NormalizeSettings(settings);
    if (SettingsEqual(settings_, normalized)) {
        return;
    }

    settings_ = normalized;
    if (!settings_.enabled || settings_.channel == NotificationChannel::Log) {
        entries_.clear();
    } else {
        while (static_cast<int>(entries_.size()) > settings_.maxQueue) {
            entries_.pop_front();
        }
    }
    QueueSave();
    debuglog::WriteInfo(
        "Notification settings changed enabled=%d channel=%s position=%s",
        settings_.enabled ? 1 : 0,
        ChannelId(settings_.channel),
        PositionId(settings_.position));
}

void NotificationManager::Notify(
    NotificationGroup group,
    NotificationSeverity severity,
    std::string_view text,
    double durationMs) {
    const bool forcePopup = group == NotificationGroup::Validation;
    const bool groupEnabled = forcePopup || settings_.groups[NotificationGroupIndex(group)];
    if (text.empty() || (!settings_.enabled && !forcePopup) || !groupEnabled) {
        return;
    }

    const double effectiveDuration = durationMs > 0.0 ? durationMs : settings_.durationMs;
    const double now = static_cast<double>(GetTickCount64());
    const std::string signature = MakeSignature(group, severity, text);
    if (FoldDuplicate(signature, now, effectiveDuration)) {
        return;
    }

    if (!forcePopup && settings_.channel == NotificationChannel::Log) {
        WriteLog(severity, text);
        return;
    }

    QueuePopup(std::string(text), severity, effectiveDuration, signature);
}

void NotificationManager::ShowUserPopup(
    std::string_view text,
    NotificationSeverity severity,
    double durationMs) {
    if (text.empty()) {
        return;
    }

    const double effectiveDuration = durationMs > 0.0 ? durationMs : settings_.durationMs;
    const double now = static_cast<double>(GetTickCount64());
    const std::string signature = MakeUserSignature(severity, text);
    if (FoldDuplicate(signature, now, effectiveDuration)) {
        return;
    }

    QueuePopup(std::string(text), severity, effectiveDuration, signature);
}

bool NotificationManager::WantsOverlayRender() const {
    return !entries_.empty();
}

void NotificationManager::DrawOverlay() {
    PruneExpired();
    if (entries_.empty()) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 position = ResolveWindowPosition(io.DisplaySize, settings_);
    const ImVec2 pivot = PositionPivot(settings_.position);
    ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(0.0f);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMouseInputs;

    if (ImGui::Begin("##helperbyorc_notifications", nullptr, flags)) {
        const std::size_t visibleCount = std::min<std::size_t>(
            entries_.size(),
            static_cast<std::size_t>(std::max(settings_.maxVisible, 1)));
        const std::size_t first = entries_.size() - visibleCount;
        const float width = UiSettings::Instance().Scale(settings_.width);
        for (std::size_t index = first; index < entries_.size(); ++index) {
            const NotificationEntry& entry = entries_[index];
            ImGui::PushID(static_cast<int>(entry.id));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, SeverityColor(entry.severity, settings_.opacity));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiSettings::Instance().Scale(6.0f));
            if (ImGui::BeginChild(
                    "notification",
                    ImVec2(width, 0.0f),
                    ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                if (entry.repeatCount > 1) {
                    ImGui::TextWrapped("%s x%d", entry.text.c_str(), entry.repeatCount);
                } else {
                    ImGui::TextWrapped("%s", entry.text.c_str());
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImGui::PopID();
            if (index + 1 < entries_.size()) {
                ImGui::Spacing();
            }
        }
    }
    ImGui::End();
}

NotificationSettings NotificationManager::NormalizeSettings(NotificationSettings settings) {
    settings.offsetX = std::clamp(settings.offsetX, kMinOffset, kMaxOffset);
    settings.offsetY = std::clamp(settings.offsetY, kMinOffset, kMaxOffset);
    settings.durationMs = std::clamp(settings.durationMs, kMinDurationMs, kMaxDurationMs);
    settings.maxVisible = std::clamp(settings.maxVisible, kMinMaxVisible, kMaxMaxVisible);
    settings.maxQueue = std::clamp(settings.maxQueue, kMinMaxQueue, kMaxMaxQueue);
    settings.dedupeWindowMs = std::clamp(settings.dedupeWindowMs, kMinDedupeMs, kMaxDedupeMs);
    settings.width = std::clamp(settings.width, kMinWidth, kMaxWidth);
    settings.opacity = std::clamp(settings.opacity, kMinOpacity, kMaxOpacity);
    settings.groups[NotificationGroupIndex(NotificationGroup::Validation)] = true;
    return settings;
}

bool NotificationManager::SettingsEqual(const NotificationSettings& lhs, const NotificationSettings& rhs) {
    return lhs.enabled == rhs.enabled
        && lhs.channel == rhs.channel
        && lhs.position == rhs.position
        && std::abs(lhs.offsetX - rhs.offsetX) < 0.001f
        && std::abs(lhs.offsetY - rhs.offsetY) < 0.001f
        && std::abs(lhs.durationMs - rhs.durationMs) < 0.001
        && lhs.maxVisible == rhs.maxVisible
        && lhs.maxQueue == rhs.maxQueue
        && std::abs(lhs.dedupeWindowMs - rhs.dedupeWindowMs) < 0.001
        && std::abs(lhs.width - rhs.width) < 0.001f
        && std::abs(lhs.opacity - rhs.opacity) < 0.001f
        && lhs.groups == rhs.groups;
}

void NotificationManager::QueueSave() const {
    jsonutil::JsonObject section;
    section["enabled"] = settings_.enabled;
    section["channel"] = ChannelId(settings_.channel);
    section["position"] = PositionId(settings_.position);
    section["offset_x"] = static_cast<double>(settings_.offsetX);
    section["offset_y"] = static_cast<double>(settings_.offsetY);
    section["duration_ms"] = settings_.durationMs;
    section["max_visible"] = settings_.maxVisible;
    section["max_queue"] = settings_.maxQueue;
    section["dedupe_window_ms"] = settings_.dedupeWindowMs;
    section["width"] = static_cast<double>(settings_.width);
    section["opacity"] = static_cast<double>(settings_.opacity);

    jsonutil::JsonObject groups;
    for (int index = 0; index < static_cast<int>(kNotificationGroupCount); ++index) {
        const auto group = static_cast<NotificationGroup>(index);
        if (group == NotificationGroup::Validation) {
            continue;
        }
        groups[GroupId(group)] = settings_.groups[NotificationGroupIndex(group)];
    }
    section["groups"] = jsonutil::JsonValue(std::move(groups));

    AppConfig::Instance().QueueSectionReplace(kNotificationsSectionName, jsonutil::JsonValue(std::move(section)));
}

void NotificationManager::QueuePopup(
    std::string text,
    NotificationSeverity severity,
    double durationMs,
    std::string signature) {
    const double now = static_cast<double>(GetTickCount64());
    entries_.push_back(NotificationEntry{
        nextEntryId_++,
        std::move(signature),
        std::move(text),
        severity,
        now + durationMs,
        1,
    });

    while (static_cast<int>(entries_.size()) > settings_.maxQueue) {
        entries_.pop_front();
    }
}

bool NotificationManager::FoldDuplicate(const std::string& signature, double now, double durationMs) {
    if (settings_.dedupeWindowMs <= 0.0) {
        lastEmittedAt_[signature] = now;
        return false;
    }

    for (NotificationEntry& entry : entries_) {
        if (entry.signature == signature) {
            ++entry.repeatCount;
            entry.expiresAtMs = now + durationMs;
            lastEmittedAt_[signature] = now;
            return true;
        }
    }

    const auto it = lastEmittedAt_.find(signature);
    if (it != lastEmittedAt_.end() && now - it->second < settings_.dedupeWindowMs) {
        it->second = now;
        return true;
    }

    lastEmittedAt_[signature] = now;
    return false;
}

void NotificationManager::PruneExpired() {
    const double now = static_cast<double>(GetTickCount64());
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(), [&](const NotificationEntry& entry) {
            return entry.expiresAtMs <= now;
        }),
        entries_.end());

    const double keepWindowMs = std::max(settings_.dedupeWindowMs * 4.0, settings_.durationMs * 2.0);
    for (auto it = lastEmittedAt_.begin(); it != lastEmittedAt_.end();) {
        if (now - it->second > keepWindowMs) {
            it = lastEmittedAt_.erase(it);
        } else {
            ++it;
        }
    }
}

void NotificationManager::WriteLog(NotificationSeverity severity, std::string_view text) const {
    if (text.empty()) {
        return;
    }

    const debuglog::Level level =
        severity == NotificationSeverity::Error ? debuglog::Level::Error : debuglog::Level::Info;
    debuglog::WriteAlways(level, "[notification] %.*s", static_cast<int>(text.size()), text.data());
}
