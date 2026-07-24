#pragma once

#include "notification_center.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

class NotificationManager {
public:
    NotificationManager();

    void LoadConfig();
    void Shutdown();
    void Tick();
    void FlushPendingSettings();

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
    void TestCurrentChannel(std::string_view text, double durationMs = 2200.0);

    bool WantsOverlayRender() const;
    void DrawOverlay();

private:
    struct TextLayoutCache {
        std::uint64_t entryId = 0;
        const void* font = nullptr;
        std::size_t textLength = 0;
        float fontSize = 0.0f;
        float wrapWidth = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    void QueueSave() const;
    void DrainLogEntries();
    void WriteLog(NotificationSeverity severity, std::string_view text) const;

    NotificationCenter center_{};
    std::array<TextLayoutCache, NotificationCenter::kVisibleCapacity> textLayoutCache_{};
    bool settingsSaveDirty_ = false;
    std::uint64_t settingsSaveDueAtMs_ = 0;
    NotificationCenterStats lastLoggedStats_{};
    std::uint64_t nextStatsLogAtMs_ = 0;
    std::atomic<std::uint32_t> ownerThreadId_{0};
};
