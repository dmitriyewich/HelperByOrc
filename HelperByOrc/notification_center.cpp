#include "notification_center.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <utility>

namespace {

constexpr double kMinDurationMs = 500.0;
constexpr double kMaxDurationMs = 15000.0;
constexpr double kMinDedupeMs = 0.0;
constexpr double kMaxDedupeMs = 10000.0;
constexpr int kMinMaxVisible = 1;
constexpr int kMaxMaxVisible = 10;
constexpr int kMinMaxQueue = 1;
constexpr int kMaxMaxQueue = static_cast<int>(NotificationCenter::kVisibleCapacity);
constexpr float kMinWidth = 200.0f;
constexpr float kMaxWidth = 560.0f;
constexpr float kMinOpacity = 0.20f;
constexpr float kMaxOpacity = 1.0f;
constexpr float kMinOffset = -500.0f;
constexpr float kMaxOffset = 500.0f;
constexpr double kMinHistoryCleanupIntervalMs = 250.0;
constexpr double kMaxHistoryCleanupIntervalMs = 1000.0;
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

template <typename Entry, std::size_t Capacity>
std::size_t RingIndex(
    const std::array<Entry, Capacity>&,
    std::size_t start,
    std::size_t offset) {
    return (start + offset) % Capacity;
}

void HashByte(std::uint64_t& hash, std::uint8_t value) {
    hash ^= value;
    hash *= kFnvPrime;
}

void HashInt(std::uint64_t& hash, int value) {
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    HashByte(hash, static_cast<std::uint8_t>(bits & 0xFFu));
    HashByte(hash, static_cast<std::uint8_t>((bits >> 8u) & 0xFFu));
    HashByte(hash, static_cast<std::uint8_t>((bits >> 16u) & 0xFFu));
    HashByte(hash, static_cast<std::uint8_t>((bits >> 24u) & 0xFFu));
}

} // namespace

std::size_t NotificationGroupIndex(NotificationGroup group) {
    const int index = static_cast<int>(group);
    if (index < 0 || index >= static_cast<int>(kNotificationGroupCount)) {
        return 0;
    }
    return static_cast<std::size_t>(index);
}

bool IsPersistedNotificationGroup(NotificationGroup group) {
    switch (group) {
    case NotificationGroup::BinderErrors:
    case NotificationGroup::TagErrors:
    case NotificationGroup::SampDialogErrors:
    case NotificationGroup::BinderSuccess:
    case NotificationGroup::Confirmation:
    case NotificationGroup::BinderEvents:
    case NotificationGroup::TagSuccess:
    case NotificationGroup::ClipboardSuccess:
        return true;
    case NotificationGroup::Validation:
    case NotificationGroup::UserNotification:
    default:
        return false;
    }
}

NotificationSettings::NotificationSettings() {
    groups.fill(true);
}

NotificationSettings NormalizeNotificationSettings(NotificationSettings settings) {
    if (settings.channel != NotificationChannel::Popup && settings.channel != NotificationChannel::Log) {
        settings.channel = NotificationChannel::Popup;
    }
    const int position = static_cast<int>(settings.position);
    if (position < static_cast<int>(NotificationPosition::TopLeft)
        || position > static_cast<int>(NotificationPosition::BottomRight)) {
        settings.position = NotificationPosition::TopRight;
    }
    settings.offsetX = std::clamp(settings.offsetX, kMinOffset, kMaxOffset);
    settings.offsetY = std::clamp(settings.offsetY, kMinOffset, kMaxOffset);
    settings.durationMs = std::clamp(settings.durationMs, kMinDurationMs, kMaxDurationMs);
    settings.maxVisible = std::clamp(settings.maxVisible, kMinMaxVisible, kMaxMaxVisible);
    settings.maxQueue = std::clamp(settings.maxQueue, kMinMaxQueue, kMaxMaxQueue);
    settings.dedupeWindowMs = std::clamp(settings.dedupeWindowMs, kMinDedupeMs, kMaxDedupeMs);
    settings.width = std::clamp(settings.width, kMinWidth, kMaxWidth);
    settings.opacity = std::clamp(settings.opacity, kMinOpacity, kMaxOpacity);
    settings.groups[NotificationGroupIndex(NotificationGroup::Validation)] = true;
    settings.groups[NotificationGroupIndex(NotificationGroup::UserNotification)] = true;
    return settings;
}

bool NotificationSettingsEqual(const NotificationSettings& lhs, const NotificationSettings& rhs) {
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

void NotificationCenter::Reset(const NotificationSettings& settings) {
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingGeneration_.fetch_add(1, std::memory_order_acq_rel);
        for (PendingEvent& event : pending_) {
            event = {};
        }
        pendingStart_ = 0;
        pendingCount_ = 0;
    }
    for (PendingEvent& event : drain_) {
        event = {};
    }
    drainCount_ = 0;

    settings_ = NormalizeNotificationSettings(settings);
    ClearActive();
    ClearHistory();
    ClearLog();
    lastDispatchPreparationAtMs_ = -1.0;
    nextEntryId_ = 1;
    accepted_ = 0;
    filtered_ = 0;
    folded_ = 0;
    byGroup_.fill({});
    droppedPending_.store(0, std::memory_order_relaxed);
    droppedVisible_ = 0;
    droppedLog_ = 0;
}

const NotificationSettings& NotificationCenter::Settings() const {
    return settings_;
}

void NotificationCenter::ApplySettings(const NotificationSettings& settings) {
    settings_ = NormalizeNotificationSettings(settings);
    if (!settings_.enabled || settings_.channel == NotificationChannel::Log) {
        ClearActive();
    } else {
        TrimActiveToSettings();
    }
    if (settings_.dedupeWindowMs <= 0.0) {
        ClearHistory();
    } else {
        nextHistoryCleanupAtMs_ = 0.0;
    }
}

bool NotificationCenter::EnqueueConfigured(
    NotificationGroup group,
    NotificationSeverity severity,
    std::string_view text,
    double durationMs) {
    if (text.empty()) {
        return false;
    }
    const std::uint64_t generation = pendingGeneration_.load(std::memory_order_acquire);
    return Enqueue(PendingEvent{
        group,
        severity,
        std::string(text),
        durationMs,
        false,
        generation,
    });
}

bool NotificationCenter::EnqueueUserPopup(
    NotificationSeverity severity,
    std::string_view text,
    double durationMs) {
    if (text.empty()) {
        return false;
    }
    const std::uint64_t generation = pendingGeneration_.load(std::memory_order_acquire);
    return Enqueue(PendingEvent{
        NotificationGroup::UserNotification,
        severity,
        std::string(text),
        durationMs,
        true,
        generation,
    });
}

void NotificationCenter::DispatchConfigured(
    NotificationGroup group,
    NotificationSeverity severity,
    std::string_view text,
    double durationMs,
    double nowMs) {
    if (text.empty()) {
        return;
    }
    const bool forceOverlay =
        group == NotificationGroup::Validation || group == NotificationGroup::UserNotification;
    if ((!settings_.enabled && !forceOverlay)
        || (!forceOverlay && !settings_.groups[NotificationGroupIndex(group)])) {
        IncrementSaturated(filtered_);
        IncrementSaturated(byGroup_[NotificationGroupIndex(group)].filtered);
        return;
    }
    ProcessEvent(PendingEvent{group, severity, std::string(text), durationMs, false}, nowMs);
}

void NotificationCenter::DispatchUserPopup(
    NotificationSeverity severity,
    std::string_view text,
    double durationMs,
    double nowMs) {
    if (text.empty()) {
        return;
    }
    ProcessEvent(PendingEvent{
        NotificationGroup::UserNotification,
        severity,
        std::string(text),
        durationMs,
        true,
    }, nowMs);
}

bool NotificationCenter::Enqueue(PendingEvent event) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    if (pendingCount_ >= kPendingCapacity) {
        pending_[pendingStart_] = std::move(event);
        pendingStart_ = (pendingStart_ + 1) % kPendingCapacity;
        IncrementSaturated(droppedPending_);
        return true;
    }

    const std::size_t end = RingIndex(pending_, pendingStart_, pendingCount_);
    pending_[end] = std::move(event);
    ++pendingCount_;
    return true;
}

void NotificationCenter::Tick(double nowMs) {
    PrepareForDispatch(nowMs);

    DrainPending();
    const std::uint64_t generation = pendingGeneration_.load(std::memory_order_acquire);
    for (std::size_t index = 0; index < drainCount_; ++index) {
        if (drain_[index].generation == generation) {
            ProcessEvent(std::move(drain_[index]), nowMs);
        } else {
            IncrementSaturated(filtered_);
            IncrementSaturated(
                byGroup_[NotificationGroupIndex(drain_[index].group)].filtered);
        }
        drain_[index] = {};
    }
    drainCount_ = 0;
}

void NotificationCenter::PrepareForDispatch(double nowMs) {
    if (lastDispatchPreparationAtMs_ == nowMs) {
        return;
    }
    lastDispatchPreparationAtMs_ = nowMs;
    PruneExpired(nowMs);
    if (settings_.dedupeWindowMs <= 0.0) {
        ClearHistory();
    } else if (historyCount_ > 0 && nowMs >= nextHistoryCleanupAtMs_) {
        PruneHistory(nowMs);
    }
}

void NotificationCenter::DrainPending() {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    drainCount_ = pendingCount_;
    for (std::size_t offset = 0; offset < pendingCount_; ++offset) {
        const std::size_t index = RingIndex(pending_, pendingStart_, offset);
        drain_[offset] = std::move(pending_[index]);
        pending_[index] = {};
    }
    pendingStart_ = 0;
    pendingCount_ = 0;
}

void NotificationCenter::ProcessEvent(PendingEvent event, double nowMs) {
    const std::size_t groupIndex = NotificationGroupIndex(event.group);
    const bool forceOverlay = event.userPopup
        || event.group == NotificationGroup::Validation
        || event.group == NotificationGroup::UserNotification;
    const bool groupEnabled =
        forceOverlay || settings_.groups[groupIndex];
    if ((!settings_.enabled && !forceOverlay) || !groupEnabled || event.text.empty()) {
        IncrementSaturated(filtered_);
        IncrementSaturated(byGroup_[groupIndex].filtered);
        return;
    }

    const double durationMs = event.durationMs > 0.0 ? event.durationMs : settings_.durationMs;
    const std::uint64_t signatureHash = SignatureHash(event);
    if (settings_.dedupeWindowMs > 0.0
        && FoldOrRecordDuplicate(event, signatureHash, nowMs, durationMs)) {
        IncrementSaturated(folded_);
        IncrementSaturated(byGroup_[groupIndex].folded);
        return;
    }

    if (!forceOverlay && settings_.channel == NotificationChannel::Log) {
        QueueLog(std::move(event));
    } else {
        QueueVisible(std::move(event), signatureHash, nowMs, durationMs);
    }
    IncrementSaturated(accepted_);
    IncrementSaturated(byGroup_[groupIndex].accepted);
}

bool NotificationCenter::FoldOrRecordDuplicate(
    const PendingEvent& event,
    std::uint64_t signatureHash,
    double nowMs,
    double durationMs) {
    for (std::size_t reverseOffset = 0; reverseOffset < activeCount_; ++reverseOffset) {
        const std::size_t offset = activeCount_ - 1 - reverseOffset;
        NotificationEntry& entry = active_[RingIndex(active_, activeStart_, offset)];
        if (!SameSignature(event, signatureHash, entry)) {
            continue;
        }
        if (entry.repeatCount < std::numeric_limits<std::uint32_t>::max()) {
            ++entry.repeatCount;
        }
        entry.expiresAtMs = nowMs + durationMs;
        TouchHistory(event, signatureHash, nowMs);
        return true;
    }

    for (std::size_t reverseOffset = 0; reverseOffset < historyCount_; ++reverseOffset) {
        const std::size_t offset = historyCount_ - 1 - reverseOffset;
        HistoryEntry& entry = history_[RingIndex(history_, historyStart_, offset)];
        if (!SameSignature(event, signatureHash, entry)) {
            continue;
        }
        const bool duplicate = nowMs - entry.lastEmittedAtMs < settings_.dedupeWindowMs;
        entry.lastEmittedAtMs = nowMs;
        return duplicate;
    }

    AppendHistory(event, signatureHash, nowMs);
    return false;
}

void NotificationCenter::TouchHistory(
    const PendingEvent& event,
    std::uint64_t signatureHash,
    double nowMs) {
    for (std::size_t reverseOffset = 0; reverseOffset < historyCount_; ++reverseOffset) {
        const std::size_t offset = historyCount_ - 1 - reverseOffset;
        HistoryEntry& entry = history_[RingIndex(history_, historyStart_, offset)];
        if (SameSignature(event, signatureHash, entry)) {
            entry.lastEmittedAtMs = nowMs;
            return;
        }
    }
    AppendHistory(event, signatureHash, nowMs);
}

void NotificationCenter::AppendHistory(
    const PendingEvent& event,
    std::uint64_t signatureHash,
    double nowMs) {
    HistoryEntry value{
        event.group,
        event.severity,
        event.text,
        nowMs,
        event.userPopup,
        signatureHash,
    };
    if (historyCount_ >= kHistoryCapacity) {
        history_[historyStart_] = std::move(value);
        historyStart_ = (historyStart_ + 1) % kHistoryCapacity;
    } else {
        const std::size_t end = RingIndex(history_, historyStart_, historyCount_);
        history_[end] = std::move(value);
        ++historyCount_;
    }

    const double cleanupInterval =
        std::clamp(settings_.dedupeWindowMs, kMinHistoryCleanupIntervalMs, kMaxHistoryCleanupIntervalMs);
    if (nextHistoryCleanupAtMs_ <= 0.0) {
        nextHistoryCleanupAtMs_ = nowMs + cleanupInterval;
    }
}

void NotificationCenter::QueueVisible(
    PendingEvent event,
    std::uint64_t signatureHash,
    double nowMs,
    double durationMs) {
    const std::size_t limit = static_cast<std::size_t>(settings_.maxQueue);
    if (activeCount_ >= limit) {
        active_[activeStart_] = {};
        activeStart_ = (activeStart_ + 1) % kVisibleCapacity;
        --activeCount_;
        IncrementSaturated(droppedVisible_);
    }

    const std::size_t end = RingIndex(active_, activeStart_, activeCount_);
    NotificationEntry& entry = active_[end];
    entry.id = nextEntryId_++;
    entry.text = std::move(event.text);
    entry.group = event.group;
    entry.severity = event.severity;
    entry.createdAtMs = nowMs;
    entry.expiresAtMs = nowMs + durationMs;
    entry.repeatCount = 1;
    entry.userPopup = event.userPopup;
    entry.signatureHash = signatureHash;
    ++activeCount_;
}

void NotificationCenter::QueueLog(PendingEvent event) {
    NotificationLogEntry value{event.severity, std::move(event.text)};
    if (logCount_ >= kLogCapacity) {
        log_[logStart_] = std::move(value);
        logStart_ = (logStart_ + 1) % kLogCapacity;
        IncrementSaturated(droppedLog_);
        return;
    }
    const std::size_t end = RingIndex(log_, logStart_, logCount_);
    log_[end] = std::move(value);
    ++logCount_;
}

bool NotificationCenter::PopLog(NotificationLogEntry& entry) {
    if (logCount_ == 0) {
        return false;
    }
    entry = std::move(log_[logStart_]);
    log_[logStart_] = {};
    logStart_ = (logStart_ + 1) % kLogCapacity;
    --logCount_;
    return true;
}

void NotificationCenter::PruneExpired(double nowMs) {
    if (activeCount_ == 0) {
        return;
    }

    std::size_t kept = 0;
    for (std::size_t offset = 0; offset < activeCount_; ++offset) {
        const std::size_t index = RingIndex(active_, activeStart_, offset);
        if (active_[index].expiresAtMs <= nowMs) {
            active_[index] = {};
            continue;
        }
        if (kept != offset) {
            const std::size_t target = RingIndex(active_, activeStart_, kept);
            active_[target] = std::move(active_[index]);
            active_[index] = {};
        }
        ++kept;
    }
    activeCount_ = kept;
    if (activeCount_ == 0) {
        activeStart_ = 0;
    }
}

void NotificationCenter::PruneHistory(double nowMs) {
    if (historyCount_ == 0) {
        nextHistoryCleanupAtMs_ = 0.0;
        return;
    }

    std::size_t kept = 0;
    for (std::size_t offset = 0; offset < historyCount_; ++offset) {
        const std::size_t index = RingIndex(history_, historyStart_, offset);
        if (nowMs - history_[index].lastEmittedAtMs > settings_.dedupeWindowMs) {
            history_[index] = {};
            continue;
        }
        if (kept != offset) {
            const std::size_t target = RingIndex(history_, historyStart_, kept);
            history_[target] = std::move(history_[index]);
            history_[index] = {};
        }
        ++kept;
    }
    historyCount_ = kept;
    if (historyCount_ == 0) {
        historyStart_ = 0;
        nextHistoryCleanupAtMs_ = 0.0;
        return;
    }

    const double cleanupInterval =
        std::clamp(settings_.dedupeWindowMs, kMinHistoryCleanupIntervalMs, kMaxHistoryCleanupIntervalMs);
    nextHistoryCleanupAtMs_ = nowMs + cleanupInterval;
}

void NotificationCenter::ClearActive() {
    for (NotificationEntry& entry : active_) {
        entry = {};
    }
    activeStart_ = 0;
    activeCount_ = 0;
}

void NotificationCenter::ClearHistory() {
    for (HistoryEntry& entry : history_) {
        entry = {};
    }
    historyStart_ = 0;
    historyCount_ = 0;
    nextHistoryCleanupAtMs_ = 0.0;
}

void NotificationCenter::ClearLog() {
    for (NotificationLogEntry& entry : log_) {
        entry = {};
    }
    logStart_ = 0;
    logCount_ = 0;
}

void NotificationCenter::TrimActiveToSettings() {
    const std::size_t limit = static_cast<std::size_t>(settings_.maxQueue);
    while (activeCount_ > limit) {
        active_[activeStart_] = {};
        activeStart_ = (activeStart_ + 1) % kVisibleCapacity;
        --activeCount_;
        IncrementSaturated(droppedVisible_);
    }
    if (activeCount_ == 0) {
        activeStart_ = 0;
    }
}

std::size_t NotificationCenter::ActiveCount() const {
    return activeCount_;
}

const NotificationEntry& NotificationCenter::ActiveFromNewest(std::size_t offset) const {
    assert(offset < activeCount_);
    const std::size_t newest = activeCount_ - 1 - offset;
    return active_[RingIndex(active_, activeStart_, newest)];
}

std::size_t NotificationCenter::PendingCount() const {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    return pendingCount_;
}

std::size_t NotificationCenter::HistoryCount() const {
    return historyCount_;
}

NotificationCenterStats NotificationCenter::Stats() const {
    NotificationCenterStats stats{};
    stats.accepted = accepted_;
    stats.filtered = filtered_;
    stats.folded = folded_;
    stats.droppedPending = droppedPending_.load(std::memory_order_relaxed);
    stats.droppedVisible = droppedVisible_;
    stats.droppedLog = droppedLog_;
    stats.active = activeCount_;
    stats.pending = PendingCount();
    stats.history = historyCount_;
    stats.logPending = logCount_;
    stats.byGroup = byGroup_;
    return stats;
}

std::uint64_t NotificationCenter::SignatureHash(const PendingEvent& event) {
    std::uint64_t hash = kFnvOffsetBasis;
    HashByte(hash, event.userPopup ? 1u : 0u);
    HashInt(hash, static_cast<int>(event.group));
    HashInt(hash, static_cast<int>(event.severity));
    for (const unsigned char byte : event.text) {
        HashByte(hash, byte);
    }
    return hash;
}

bool NotificationCenter::SameSignature(
    const PendingEvent& event,
    std::uint64_t signatureHash,
    const NotificationEntry& entry) {
    return entry.signatureHash == signatureHash
        && entry.userPopup == event.userPopup
        && entry.group == event.group
        && entry.severity == event.severity
        && entry.text == event.text;
}

bool NotificationCenter::SameSignature(
    const PendingEvent& event,
    std::uint64_t signatureHash,
    const HistoryEntry& entry) {
    return entry.signatureHash == signatureHash
        && entry.userPopup == event.userPopup
        && entry.group == event.group
        && entry.severity == event.severity
        && entry.text == event.text;
}

void NotificationCenter::IncrementSaturated(std::uint64_t& value) {
    if (value < std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

void NotificationCenter::IncrementSaturated(std::atomic<std::uint64_t>& value) {
    std::uint64_t current = value.load(std::memory_order_relaxed);
    while (current < std::numeric_limits<std::uint64_t>::max()
        && !value.compare_exchange_weak(
            current,
            current + 1,
            std::memory_order_relaxed,
            std::memory_order_relaxed)) {
    }
}
