#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <string_view>

enum class NotificationSeverity : int {
    Info = 0,
    Success,
    Warning,
    Error,
};

enum class NotificationGroup : int {
    BinderErrors = 0,
    TagErrors,
    SampDialogErrors,
    Success,
    Confirmation,
    BinderEvents,
    // Internal, always-on popup group for editor/input validation feedback.
    Validation,
};

enum class NotificationChannel : int {
    Popup = 0,
    Log,
};

enum class NotificationPosition : int {
    TopLeft = 0,
    TopCenter,
    TopRight,
    MiddleLeft,
    MiddleCenter,
    MiddleRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

constexpr std::size_t kNotificationGroupCount = 7;

std::size_t NotificationGroupIndex(NotificationGroup group);

struct NotificationSettings {
    bool enabled = true;
    NotificationChannel channel = NotificationChannel::Popup;
    NotificationPosition position = NotificationPosition::TopRight;
    float offsetX = 20.0f;
    float offsetY = 20.0f;
    double durationMs = 2500.0;
    int maxVisible = 5;
    int maxQueue = 32;
    double dedupeWindowMs = 3000.0;
    float width = 320.0f;
    float opacity = 0.95f;
    std::array<bool, kNotificationGroupCount> groups{};

    NotificationSettings();
};

class NotificationManager {
public:
    NotificationManager();

    void LoadConfig();
    void Shutdown();

    const NotificationSettings& Settings() const;
    void ApplySettings(const NotificationSettings& settings);

    void Notify(
        NotificationGroup group,
        NotificationSeverity severity,
        std::string_view text,
        double durationMs = 0.0);
    void ShowUserPopup(
        std::string_view text,
        NotificationSeverity severity = NotificationSeverity::Success,
        double durationMs = 2200.0);

    bool WantsOverlayRender() const;
    void DrawOverlay();

private:
    struct NotificationEntry {
        std::uint64_t id = 0;
        std::string signature{};
        std::string text{};
        NotificationSeverity severity = NotificationSeverity::Info;
        double expiresAtMs = 0.0;
        int repeatCount = 1;
    };

    static NotificationSettings NormalizeSettings(NotificationSettings settings);
    static bool SettingsEqual(const NotificationSettings& lhs, const NotificationSettings& rhs);
    void QueueSave() const;
    void QueuePopup(std::string text, NotificationSeverity severity, double durationMs, std::string signature);
    bool FoldDuplicate(const std::string& signature, double now, double durationMs);
    void PruneExpired();
    void WriteLog(NotificationSeverity severity, std::string_view text) const;

    NotificationSettings settings_{};
    std::deque<NotificationEntry> entries_{};
    std::map<std::string, double> lastEmittedAt_{};
    std::uint64_t nextEntryId_ = 1;
};
