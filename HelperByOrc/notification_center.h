#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
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
    BinderSuccess,
    Confirmation,
    BinderEvents,
    TagSuccess,
    ClipboardSuccess,
    // Internal, always-on OSD group for editor/input validation feedback.
    Validation,
    // Internal, always-on OSD group for explicit user-authored notifications.
    UserNotification,
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

constexpr std::size_t kNotificationGroupCount = 10;

std::size_t NotificationGroupIndex(NotificationGroup group);
bool IsPersistedNotificationGroup(NotificationGroup group);

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

NotificationSettings NormalizeNotificationSettings(NotificationSettings settings);
bool NotificationSettingsEqual(const NotificationSettings& lhs, const NotificationSettings& rhs);

struct NotificationEntry {
    std::uint64_t id = 0;
    std::string text{};
    NotificationGroup group = NotificationGroup::BinderErrors;
    NotificationSeverity severity = NotificationSeverity::Info;
    double createdAtMs = 0.0;
    double expiresAtMs = 0.0;
    std::uint32_t repeatCount = 1;

private:
    friend class NotificationCenter;

    bool userPopup = false;
    std::uint64_t signatureHash = 0;
};

struct NotificationLogEntry {
    NotificationSeverity severity = NotificationSeverity::Info;
    std::string text{};
};

struct NotificationGroupStats {
    std::uint64_t accepted = 0;
    std::uint64_t filtered = 0;
    std::uint64_t folded = 0;
};

struct NotificationCenterStats {
    std::uint64_t accepted = 0;
    std::uint64_t filtered = 0;
    std::uint64_t folded = 0;
    std::uint64_t droppedPending = 0;
    std::uint64_t droppedVisible = 0;
    std::uint64_t droppedLog = 0;
    std::size_t active = 0;
    std::size_t pending = 0;
    std::size_t history = 0;
    std::size_t logPending = 0;
    std::array<NotificationGroupStats, kNotificationGroupCount> byGroup{};
};

class NotificationCenter {
public:
    static constexpr std::size_t kVisibleCapacity = 64;
    static constexpr std::size_t kPendingCapacity = 256;
    static constexpr std::size_t kHistoryCapacity = 256;
    static constexpr std::size_t kLogCapacity = kPendingCapacity;

    NotificationCenter() = default;

    void Reset(const NotificationSettings& settings);
    const NotificationSettings& Settings() const;
    void ApplySettings(const NotificationSettings& settings);

    bool EnqueueConfigured(
        NotificationGroup group,
        NotificationSeverity severity,
        std::string_view text,
        double durationMs = 0.0);
    bool EnqueueUserPopup(
        NotificationSeverity severity,
        std::string_view text,
        double durationMs = 2200.0);

    // Owner-thread fast path after Tick(). Cross-thread producers must use Enqueue*().
    void DispatchConfigured(
        NotificationGroup group,
        NotificationSeverity severity,
        std::string_view text,
        double durationMs,
        double nowMs);
    void DispatchUserPopup(
        NotificationSeverity severity,
        std::string_view text,
        double durationMs,
        double nowMs);

    void Tick(double nowMs);
    bool PopLog(NotificationLogEntry& entry);

    std::size_t ActiveCount() const;
    const NotificationEntry& ActiveFromNewest(std::size_t offset) const;
    std::size_t PendingCount() const;
    std::size_t HistoryCount() const;
    NotificationCenterStats Stats() const;

private:
    struct PendingEvent {
        NotificationGroup group = NotificationGroup::BinderErrors;
        NotificationSeverity severity = NotificationSeverity::Info;
        std::string text{};
        double durationMs = 0.0;
        bool userPopup = false;
        std::uint64_t generation = 0;
    };

    struct HistoryEntry {
        NotificationGroup group = NotificationGroup::BinderErrors;
        NotificationSeverity severity = NotificationSeverity::Info;
        std::string text{};
        double lastEmittedAtMs = 0.0;
        bool userPopup = false;
        std::uint64_t signatureHash = 0;
    };

    bool Enqueue(PendingEvent event);
    void PrepareForDispatch(double nowMs);
    void DrainPending();
    void ProcessEvent(PendingEvent event, double nowMs);
    bool FoldOrRecordDuplicate(
        const PendingEvent& event,
        std::uint64_t signatureHash,
        double nowMs,
        double durationMs);
    void TouchHistory(const PendingEvent& event, std::uint64_t signatureHash, double nowMs);
    void AppendHistory(const PendingEvent& event, std::uint64_t signatureHash, double nowMs);
    void QueueVisible(PendingEvent event, std::uint64_t signatureHash, double nowMs, double durationMs);
    void QueueLog(PendingEvent event);
    void PruneExpired(double nowMs);
    void PruneHistory(double nowMs);
    void ClearActive();
    void ClearHistory();
    void ClearLog();
    void TrimActiveToSettings();

    static std::uint64_t SignatureHash(const PendingEvent& event);
    static bool SameSignature(
        const PendingEvent& event,
        std::uint64_t signatureHash,
        const NotificationEntry& entry);
    static bool SameSignature(
        const PendingEvent& event,
        std::uint64_t signatureHash,
        const HistoryEntry& entry);
    static void IncrementSaturated(std::uint64_t& value);
    static void IncrementSaturated(std::atomic<std::uint64_t>& value);

    NotificationSettings settings_{};

    std::array<NotificationEntry, kVisibleCapacity> active_{};
    std::size_t activeStart_ = 0;
    std::size_t activeCount_ = 0;

    std::array<HistoryEntry, kHistoryCapacity> history_{};
    std::size_t historyStart_ = 0;
    std::size_t historyCount_ = 0;
    double nextHistoryCleanupAtMs_ = 0.0;
    double lastDispatchPreparationAtMs_ = -1.0;

    std::array<NotificationLogEntry, kLogCapacity> log_{};
    std::size_t logStart_ = 0;
    std::size_t logCount_ = 0;

    mutable std::mutex pendingMutex_{};
    std::atomic<std::uint64_t> pendingGeneration_{1};
    std::array<PendingEvent, kPendingCapacity> pending_{};
    std::size_t pendingStart_ = 0;
    std::size_t pendingCount_ = 0;
    std::array<PendingEvent, kPendingCapacity> drain_{};
    std::size_t drainCount_ = 0;

    std::uint64_t nextEntryId_ = 1;
    std::uint64_t accepted_ = 0;
    std::uint64_t filtered_ = 0;
    std::uint64_t folded_ = 0;
    std::array<NotificationGroupStats, kNotificationGroupCount> byGroup_{};
    std::atomic<std::uint64_t> droppedPending_{0};
    std::uint64_t droppedVisible_ = 0;
    std::uint64_t droppedLog_ = 0;
};
