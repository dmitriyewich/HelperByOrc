#include "notification_manager.h"

#include "app_config.h"
#include "debug_log.h"
#include "json_utils.h"
#include "ui_icons.h"
#include "ui_settings.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace {

constexpr char kNotificationsSectionName[] = "notifications";
constexpr double kFadeInMs = 120.0;
constexpr double kFadeOutMs = 180.0;
constexpr std::uint64_t kSettingsSaveDebounceMs = 250;
constexpr std::uint64_t kStatsLogIntervalMs = 5000;
constexpr std::size_t kMaxRenderedTextBytes = 4096;

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
    case NotificationGroup::BinderSuccess:
        return "binder_success";
    case NotificationGroup::Confirmation:
        return "confirmation";
    case NotificationGroup::BinderEvents:
        return "events";
    case NotificationGroup::TagSuccess:
        return "tag_success";
    case NotificationGroup::ClipboardSuccess:
        return "clipboard_success";
    case NotificationGroup::Validation:
        return "validation";
    case NotificationGroup::UserNotification:
        return "user_notification";
    case NotificationGroup::BinderErrors:
    default:
        return "binder_errors";
    }
}

bool IsLegacySuccessTarget(NotificationGroup group) {
    return group == NotificationGroup::BinderSuccess
        || group == NotificationGroup::TagSuccess
        || group == NotificationGroup::ClipboardSuccess;
}

ImVec4 SeverityAccentColor(NotificationSeverity severity) {
    switch (severity) {
    case NotificationSeverity::Success:
        return ImVec4(0.30f, 0.78f, 0.38f, 1.0f);
    case NotificationSeverity::Warning:
        return ImVec4(0.96f, 0.68f, 0.20f, 1.0f);
    case NotificationSeverity::Error:
        return ImVec4(0.95f, 0.34f, 0.34f, 1.0f);
    case NotificationSeverity::Info:
    default:
        return ImVec4(0.35f, 0.66f, 0.96f, 1.0f);
    }
}

const char* SeverityIcon(NotificationSeverity severity) {
    switch (severity) {
    case NotificationSeverity::Success:
        return ui_icons::Check;
    case NotificationSeverity::Warning:
        return ui_icons::Bolt;
    case NotificationSeverity::Error:
        return ui_icons::Xmark;
    case NotificationSeverity::Info:
    default:
        return ui_icons::Comment;
    }
}

bool IsTopPosition(NotificationPosition position) {
    return position == NotificationPosition::TopLeft
        || position == NotificationPosition::TopCenter
        || position == NotificationPosition::TopRight;
}

bool IsBottomPosition(NotificationPosition position) {
    return position == NotificationPosition::BottomLeft
        || position == NotificationPosition::BottomCenter
        || position == NotificationPosition::BottomRight;
}

float ResolveHorizontalPosition(
    const ImVec2& workMin,
    const ImVec2& workMax,
    float width,
    const NotificationSettings& settings) {
    float x = workMin.x;
    switch (settings.position) {
    case NotificationPosition::TopLeft:
    case NotificationPosition::MiddleLeft:
    case NotificationPosition::BottomLeft:
        x = workMin.x + settings.offsetX;
        break;
    case NotificationPosition::TopCenter:
    case NotificationPosition::MiddleCenter:
    case NotificationPosition::BottomCenter:
        x = (workMin.x + workMax.x - width) * 0.5f + settings.offsetX;
        break;
    case NotificationPosition::TopRight:
    case NotificationPosition::MiddleRight:
    case NotificationPosition::BottomRight:
        x = workMax.x - settings.offsetX - width;
        break;
    default:
        x = workMax.x - settings.offsetX - width;
        break;
    }
    return std::clamp(x, workMin.x, std::max(workMin.x, workMax.x - width));
}

float FadeFactor(const NotificationEntry& entry, double nowMs) {
    const float fadeInProgress =
        static_cast<float>(std::clamp((nowMs - entry.createdAtMs) / kFadeInMs, 0.0, 1.0));
    const float fadeIn = 0.15f + fadeInProgress * 0.85f;
    const float fadeOut = static_cast<float>(std::clamp((entry.expiresAtMs - nowMs) / kFadeOutMs, 0.0, 1.0));
    return std::min(fadeIn, fadeOut);
}

ImU32 ColorWithAlpha(ImVec4 color, float alpha) {
    color.w = std::clamp(color.w * alpha, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(color);
}

std::size_t RenderedTextLength(std::string_view text) {
    std::size_t length = std::min(text.size(), kMaxRenderedTextBytes);
    while (length > 0
        && length < text.size()
        && (static_cast<unsigned char>(text[length]) & 0xC0u) == 0x80u) {
        --length;
    }
    return length;
}

bool StatsCountersEqual(
    const NotificationCenterStats& lhs,
    const NotificationCenterStats& rhs) {
    if (lhs.accepted != rhs.accepted
        || lhs.filtered != rhs.filtered
        || lhs.folded != rhs.folded
        || lhs.droppedPending != rhs.droppedPending
        || lhs.droppedVisible != rhs.droppedVisible
        || lhs.droppedLog != rhs.droppedLog) {
        return false;
    }

    for (std::size_t index = 0; index < kNotificationGroupCount; ++index) {
        const NotificationGroupStats& left = lhs.byGroup[index];
        const NotificationGroupStats& right = rhs.byGroup[index];
        if (left.accepted != right.accepted
            || left.filtered != right.filtered
            || left.folded != right.folded) {
            return false;
        }
    }
    return true;
}

std::string BuildGroupStatsText(const NotificationCenterStats& stats) {
    std::string text;
    text.reserve(256);
    char counters[96]{};
    for (std::size_t index = 0; index < kNotificationGroupCount; ++index) {
        const NotificationGroupStats& groupStats = stats.byGroup[index];
        if (groupStats.accepted == 0 && groupStats.filtered == 0 && groupStats.folded == 0) {
            continue;
        }

        const auto group = static_cast<NotificationGroup>(index);
        const int written = std::snprintf(
            counters,
            sizeof(counters),
            "%s%s:%llu/%llu/%llu",
            text.empty() ? "" : ",",
            GroupId(group),
            static_cast<unsigned long long>(groupStats.accepted),
            static_cast<unsigned long long>(groupStats.filtered),
            static_cast<unsigned long long>(groupStats.folded));
        if (written > 0) {
            text.append(counters, static_cast<std::size_t>(std::min<int>(written, sizeof(counters) - 1)));
        }
    }
    return text.empty() ? "-" : text;
}

} // namespace

NotificationManager::NotificationManager() = default;

void NotificationManager::LoadConfig() {
    NotificationSettings loaded{};
    const jsonutil::JsonValue rawSection = AppConfig::Instance().ReadSection(kNotificationsSectionName);
    const jsonutil::JsonObject* section = rawSection.TryObject();
    bool removeLegacyInternalGroup = false;
    bool removeLegacySuccessGroup = false;
    bool missingPersistedGroup = false;

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
            removeLegacySuccessGroup = groups->find("success") != groups->end();
            for (int index = 0; index < static_cast<int>(kNotificationGroupCount); ++index) {
                const auto group = static_cast<NotificationGroup>(index);
                if (!IsPersistedNotificationGroup(group)) {
                    removeLegacyInternalGroup =
                        removeLegacyInternalGroup || groups->find(GroupId(group)) != groups->end();
                    continue;
                }

                const char* groupId = GroupId(group);
                if (groups->find(groupId) != groups->end()) {
                    loaded.groups[NotificationGroupIndex(group)] = jsonutil::JsonBoolOr(
                        groups,
                        groupId,
                        loaded.groups[NotificationGroupIndex(group)]);
                    continue;
                }
                if (removeLegacySuccessGroup && IsLegacySuccessTarget(group)) {
                    loaded.groups[NotificationGroupIndex(group)] = jsonutil::JsonBoolOr(
                        groups,
                        "success",
                        loaded.groups[NotificationGroupIndex(group)]);
                    continue;
                }
                missingPersistedGroup = true;
            }
        } else {
            missingPersistedGroup = true;
        }
    }

    const NotificationSettings normalized = NormalizeNotificationSettings(loaded);
    center_.Reset(normalized);
    textLayoutCache_.fill({});
    settingsSaveDirty_ = false;
    settingsSaveDueAtMs_ = 0;
    lastLoggedStats_ = {};
    nextStatsLogAtMs_ = GetTickCount64() + kStatsLogIntervalMs;

    if (!section || removeLegacyInternalGroup || removeLegacySuccessGroup || missingPersistedGroup
        || !NotificationSettingsEqual(loaded, normalized)) {
        QueueSave();
    }

    debuglog::WriteInfo(
        "NotificationManager loaded enabled=%d channel=%s position=%s max_visible=%d max_queue=%d",
        normalized.enabled ? 1 : 0,
        ChannelId(normalized.channel),
        PositionId(normalized.position),
        normalized.maxVisible,
        normalized.maxQueue);
}

void NotificationManager::Shutdown() {
    FlushPendingSettings();
    center_.Reset(NotificationSettings{});
    textLayoutCache_.fill({});
    ownerThreadId_.store(0, std::memory_order_release);
}

void NotificationManager::Tick() {
    const std::uint64_t now = GetTickCount64();
    const std::uint32_t currentThreadId = GetCurrentThreadId();
    const std::uint32_t previousThreadId =
        ownerThreadId_.exchange(currentThreadId, std::memory_order_acq_rel);
    if (previousThreadId != 0 && previousThreadId != currentThreadId) {
        debuglog::WriteInfo(
            "[notifications] owner thread changed old=%lu new=%lu",
            static_cast<unsigned long>(previousThreadId),
            static_cast<unsigned long>(currentThreadId));
    }
    if (settingsSaveDirty_ && now >= settingsSaveDueAtMs_) {
        FlushPendingSettings();
    }

    center_.Tick(static_cast<double>(now));
    DrainLogEntries();

    if (now < nextStatsLogAtMs_) {
        return;
    }
    nextStatsLogAtMs_ = now + kStatsLogIntervalMs;
    const NotificationCenterStats stats = center_.Stats();
    if (StatsCountersEqual(stats, lastLoggedStats_)) {
        return;
    }
    const std::string groupStats = BuildGroupStatsText(stats);
    debuglog::WriteInfo(
        "[notifications][stats] accepted=%llu filtered=%llu folded=%llu dropped_pending=%llu "
        "dropped_visible=%llu dropped_log=%llu active=%llu pending=%llu history=%llu "
        "groups_a_f_f=\"%s\"",
        static_cast<unsigned long long>(stats.accepted),
        static_cast<unsigned long long>(stats.filtered),
        static_cast<unsigned long long>(stats.folded),
        static_cast<unsigned long long>(stats.droppedPending),
        static_cast<unsigned long long>(stats.droppedVisible),
        static_cast<unsigned long long>(stats.droppedLog),
        static_cast<unsigned long long>(stats.active),
        static_cast<unsigned long long>(stats.pending),
        static_cast<unsigned long long>(stats.history),
        groupStats.c_str());
    lastLoggedStats_ = stats;
}

void NotificationManager::FlushPendingSettings() {
    if (!settingsSaveDirty_) {
        return;
    }
    QueueSave();
    settingsSaveDirty_ = false;
    settingsSaveDueAtMs_ = 0;
    const NotificationSettings& settings = center_.Settings();
    debuglog::WriteInfo(
        "Notification settings changed enabled=%d channel=%s position=%s",
        settings.enabled ? 1 : 0,
        ChannelId(settings.channel),
        PositionId(settings.position));
}

const NotificationSettings& NotificationManager::Settings() const {
    return center_.Settings();
}

void NotificationManager::ApplySettings(const NotificationSettings& settings) {
    const NotificationSettings normalized = NormalizeNotificationSettings(settings);
    if (NotificationSettingsEqual(center_.Settings(), normalized)) {
        return;
    }

    center_.ApplySettings(normalized);
    settingsSaveDirty_ = true;
    settingsSaveDueAtMs_ = GetTickCount64() + kSettingsSaveDebounceMs;
}

void NotificationManager::Notify(
    NotificationGroup group,
    NotificationSeverity severity,
    std::string_view text,
    double durationMs) {
    if (ownerThreadId_.load(std::memory_order_acquire) == GetCurrentThreadId()) {
        center_.DispatchConfigured(
            group,
            severity,
            text,
            durationMs,
            static_cast<double>(GetTickCount64()));
        DrainLogEntries();
        return;
    }
    center_.EnqueueConfigured(group, severity, text, durationMs);
}

void NotificationManager::ShowUserPopup(
    std::string_view text,
    NotificationSeverity severity,
    double durationMs) {
    if (ownerThreadId_.load(std::memory_order_acquire) == GetCurrentThreadId()) {
        center_.DispatchUserPopup(
            severity,
            text,
            durationMs,
            static_cast<double>(GetTickCount64()));
        return;
    }
    center_.EnqueueUserPopup(severity, text, durationMs);
}

void NotificationManager::TestCurrentChannel(std::string_view text, double durationMs) {
    if (text.empty()) {
        return;
    }
    if (center_.Settings().channel == NotificationChannel::Log) {
        WriteLog(NotificationSeverity::Success, text);
        return;
    }
    ShowUserPopup(text, NotificationSeverity::Success, durationMs);
}

void NotificationManager::DrainLogEntries() {
    NotificationLogEntry entry;
    while (center_.PopLog(entry)) {
        WriteLog(entry.severity, entry.text);
    }
}

bool NotificationManager::WantsOverlayRender() const {
    return center_.ActiveCount() > 0;
}

void NotificationManager::DrawOverlay() {
    const std::size_t activeCount = center_.ActiveCount();
    if (activeCount == 0) {
        return;
    }

    const NotificationSettings& settings = center_.Settings();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImFont* font = ImGui::GetFont();
    if (!viewport || !font || viewport->WorkSize.x <= 1.0f || viewport->WorkSize.y <= 1.0f) {
        return;
    }

    UiSettings& ui = UiSettings::Instance();
    const ImVec2 workMin = viewport->WorkPos;
    const ImVec2 workMax(
        viewport->WorkPos.x + viewport->WorkSize.x,
        viewport->WorkPos.y + viewport->WorkSize.y);
    const float width = std::min(ui.Scale(settings.width), viewport->WorkSize.x);
    if (width <= 1.0f) {
        return;
    }

    const float fontSize = ImGui::GetFontSize();
    const float accentWidth = std::max(2.0f, ui.Scale(3.0f));
    const float paddingX = ui.Scale(12.0f);
    const float paddingY = ui.Scale(9.0f);
    const float iconGap = ui.Scale(8.0f);
    const float badgePadX = ui.Scale(6.0f);
    const float badgePadY = ui.Scale(2.0f);
    const float badgeGap = ui.Scale(8.0f);
    const float cardGap = ui.Scale(8.0f);
    const float rounding = ui.Scale(7.0f);
    const float minCardHeight = ui.Scale(40.0f);
    const double nowMs = static_cast<double>(GetTickCount64());

    struct CardLayout {
        const NotificationEntry* entry = nullptr;
        const char* icon = nullptr;
        ImVec2 iconSize{};
        ImVec2 textSize{};
        ImVec2 badgeSize{};
        float wrapWidth = 0.0f;
        float naturalHeight = 0.0f;
        const char* textEnd = nullptr;
        bool sourceTruncated = false;
        char badge[32]{};
    };

    const std::size_t candidateCount = std::min<std::size_t>(
        activeCount,
        static_cast<std::size_t>(std::clamp(settings.maxVisible, 1, 10)));
    const auto buildLayout = [&](std::size_t offset) {
        CardLayout layout{};
        layout.entry = &center_.ActiveFromNewest(offset);
        layout.icon = SeverityIcon(layout.entry->severity);
        layout.iconSize = font->CalcTextSizeA(fontSize, width, 0.0f, layout.icon);

        if (layout.entry->repeatCount > 1) {
            std::snprintf(
                layout.badge,
                sizeof(layout.badge),
                "x%u",
                static_cast<unsigned>(layout.entry->repeatCount));
            const ImVec2 labelSize = font->CalcTextSizeA(fontSize, width, 0.0f, layout.badge);
            layout.badgeSize = ImVec2(
                labelSize.x + badgePadX * 2.0f,
                labelSize.y + badgePadY * 2.0f);
        }

        const float fixedWidth = accentWidth + paddingX * 2.0f
            + layout.iconSize.x + iconGap
            + (layout.badge[0] != '\0' ? layout.badgeSize.x + badgeGap : 0.0f);
        layout.wrapWidth = std::max(fontSize, width - fixedWidth);
        const std::size_t renderedTextLength = RenderedTextLength(layout.entry->text);
        layout.textEnd = layout.entry->text.data() + renderedTextLength;
        layout.sourceTruncated = renderedTextLength < layout.entry->text.size();

        TextLayoutCache& cache =
            textLayoutCache_[layout.entry->id % textLayoutCache_.size()];
        if (cache.entryId != layout.entry->id
            || cache.font != font
            || cache.textLength != renderedTextLength
            || std::abs(cache.fontSize - fontSize) > 0.001f
            || std::abs(cache.wrapWidth - layout.wrapWidth) > 0.001f) {
            const char* begin = layout.entry->text.data();
            const ImVec2 measured =
                font->CalcTextSizeA(fontSize, width, layout.wrapWidth, begin, layout.textEnd);
            cache.entryId = layout.entry->id;
            cache.font = font;
            cache.textLength = renderedTextLength;
            cache.fontSize = fontSize;
            cache.wrapWidth = layout.wrapWidth;
            cache.width = measured.x;
            cache.height = measured.y;
        }
        layout.textSize = ImVec2(cache.width, cache.height);
        layout.naturalHeight = std::max(
            minCardHeight,
            std::max({layout.iconSize.y, layout.textSize.y, layout.badgeSize.y}) + paddingY * 2.0f);
        return layout;
    };

    const float x = ResolveHorizontalPosition(workMin, workMax, width, settings);
    const CardLayout firstLayout = buildLayout(0);
    const float firstHeight = std::min(firstLayout.naturalHeight, viewport->WorkSize.y);
    bool drawDown = IsTopPosition(settings.position);
    float cursorY = workMin.y;
    if (IsTopPosition(settings.position)) {
        cursorY = std::clamp(
            workMin.y + settings.offsetY,
            workMin.y,
            std::max(workMin.y, workMax.y - firstHeight));
    } else if (IsBottomPosition(settings.position)) {
        drawDown = false;
        cursorY = std::clamp(
            workMax.y - settings.offsetY,
            std::min(workMax.y, workMin.y + firstHeight),
            workMax.y);
    } else {
        const float centerY = std::clamp(
            (workMin.y + workMax.y) * 0.5f + settings.offsetY,
            workMin.y,
            workMax.y);
        const float newestTop = std::clamp(
            centerY - firstHeight * 0.5f,
            workMin.y,
            std::max(workMin.y, workMax.y - firstHeight));
        const float spaceAbove = newestTop - workMin.y;
        const float spaceBelow = workMax.y - (newestTop + firstHeight);
        drawDown = spaceBelow >= spaceAbove;
        cursorY = drawDown ? newestTop : newestTop + firstHeight;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 background = style.Colors[ImGuiCol_PopupBg];
    background.w = settings.opacity;
    const ImVec4 textColor = style.Colors[ImGuiCol_Text];
    const ImVec4 borderColor = style.Colors[ImGuiCol_Border];

    drawList->PushClipRect(workMin, workMax, true);
    std::size_t drawn = 0;
    for (std::size_t index = 0; index < candidateCount; ++index) {
        if (drawn > 0) {
            cursorY += drawDown ? cardGap : -cardGap;
        }

        const float availableHeight =
            drawDown ? workMax.y - cursorY : cursorY - workMin.y;
        if (availableHeight <= 1.0f) {
            break;
        }

        const CardLayout layout = index == 0 ? firstLayout : buildLayout(index);
        float cardHeight = layout.naturalHeight;
        bool clipped = false;
        if (cardHeight > availableHeight) {
            if (drawn > 0) {
                break;
            }
            cardHeight = availableHeight;
            clipped = true;
        }

        const float top = drawDown ? cursorY : cursorY - cardHeight;
        const ImVec2 cardMin(x, top);
        const ImVec2 cardMax(x + width, top + cardHeight);
        const float fade = FadeFactor(*layout.entry, nowMs);
        const ImVec4 accent = SeverityAccentColor(layout.entry->severity);

        drawList->AddRectFilled(
            cardMin,
            cardMax,
            ColorWithAlpha(background, fade),
            rounding);
        drawList->AddRectFilled(
            ImVec2(cardMin.x, cardMin.y + rounding * 0.5f),
            ImVec2(cardMin.x + accentWidth, cardMax.y - rounding * 0.5f),
            ColorWithAlpha(accent, fade));
        drawList->AddRect(
            cardMin,
            cardMax,
            ColorWithAlpha(borderColor, fade),
            rounding,
            0,
            std::max(1.0f, ui.Scale(1.0f)));

        const ImVec2 iconPos(
            cardMin.x + accentWidth + paddingX,
            cardMin.y + paddingY);
        const float textX = iconPos.x + layout.iconSize.x + iconGap;
        const ImVec2 textPos(textX, cardMin.y + paddingY);

        drawList->PushClipRect(
            ImVec2(cardMin.x + accentWidth, cardMin.y),
            cardMax,
            true);
        drawList->AddText(
            font,
            fontSize,
            iconPos,
            ColorWithAlpha(accent, fade),
            layout.icon);
        drawList->AddText(
            font,
            fontSize,
            textPos,
            ColorWithAlpha(textColor, fade),
            layout.entry->text.data(),
            layout.textEnd,
            layout.wrapWidth);

        if (layout.badge[0] != '\0') {
            const ImVec2 badgeMin(
                cardMax.x - paddingX - layout.badgeSize.x,
                cardMin.y + paddingY);
            const ImVec2 badgeMax(
                badgeMin.x + layout.badgeSize.x,
                badgeMin.y + layout.badgeSize.y);
            ImVec4 badgeBackground = accent;
            badgeBackground.w = 0.22f;
            drawList->AddRectFilled(
                badgeMin,
                badgeMax,
                ColorWithAlpha(badgeBackground, fade),
                layout.badgeSize.y * 0.5f);
            const ImVec2 badgeLabelSize =
                font->CalcTextSizeA(fontSize, layout.badgeSize.x, 0.0f, layout.badge);
            drawList->AddText(
                font,
                fontSize,
                ImVec2(
                    badgeMin.x + (layout.badgeSize.x - badgeLabelSize.x) * 0.5f,
                    badgeMin.y + (layout.badgeSize.y - badgeLabelSize.y) * 0.5f),
                ColorWithAlpha(accent, fade),
                layout.badge);
        }

        if (clipped || layout.sourceTruncated) {
            const char* ellipsis = "...";
            const ImVec2 ellipsisSize =
                font->CalcTextSizeA(fontSize, width, 0.0f, ellipsis);
            drawList->AddText(
                font,
                fontSize,
                ImVec2(
                    cardMax.x - paddingX - ellipsisSize.x,
                    cardMax.y - paddingY - ellipsisSize.y),
                ColorWithAlpha(textColor, fade),
                ellipsis);
        }
        drawList->PopClipRect();

        cursorY += drawDown ? cardHeight : -cardHeight;
        ++drawn;
    }
    drawList->PopClipRect();
}

void NotificationManager::QueueSave() const {
    const NotificationSettings& settings = center_.Settings();
    jsonutil::JsonObject section;
    section["enabled"] = settings.enabled;
    section["channel"] = ChannelId(settings.channel);
    section["position"] = PositionId(settings.position);
    section["offset_x"] = static_cast<double>(settings.offsetX);
    section["offset_y"] = static_cast<double>(settings.offsetY);
    section["duration_ms"] = settings.durationMs;
    section["max_visible"] = settings.maxVisible;
    section["max_queue"] = settings.maxQueue;
    section["dedupe_window_ms"] = settings.dedupeWindowMs;
    section["width"] = static_cast<double>(settings.width);
    section["opacity"] = static_cast<double>(settings.opacity);

    jsonutil::JsonObject groups;
    for (int index = 0; index < static_cast<int>(kNotificationGroupCount); ++index) {
        const auto group = static_cast<NotificationGroup>(index);
        if (!IsPersistedNotificationGroup(group)) {
            continue;
        }
        groups[GroupId(group)] = settings.groups[NotificationGroupIndex(group)];
    }
    section["groups"] = jsonutil::JsonValue(std::move(groups));

    AppConfig::Instance().QueueSectionReplace(kNotificationsSectionName, jsonutil::JsonValue(std::move(section)));
}

void NotificationManager::WriteLog(NotificationSeverity severity, std::string_view text) const {
    if (text.empty()) {
        return;
    }

    const debuglog::Level level =
        severity == NotificationSeverity::Error ? debuglog::Level::Error : debuglog::Level::Info;
    debuglog::WriteAlways(level, "[notification] %.*s", static_cast<int>(text.size()), text.data());
}
