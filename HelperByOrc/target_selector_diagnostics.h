#pragma once

#include "feature_flags.h"

#if HELPERBYORC_TARGET_DIAGNOSTICS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "target_selector_diagnostic_math.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace targetdiag {

enum class Algorithm { Pose, Raycast };
enum class Mode { Off, Pose, Raycast, Shadow, Benchmark };

class Diagnostics {
public:
    static Diagnostics& Instance();
    static std::uint64_t NowTicks();
    static double ElapsedUs(std::uint64_t start, std::uint64_t end);

    bool HandleKey(UINT message, WPARAM wparam, LPARAM lparam);
    void DrawSettings();
    void DrawOverlay() const;
    bool ApplyPendingCommand();
    void BeginFrame();
    void EndFrame();
    Algorithm ActiveAlgorithm() const;
    bool WantsShadow() const;
    bool IsEnabled() const;
    bool ShadowRaycastFirst() const;
    void RecordResolve(Algorithm algorithm, std::uint64_t start, std::uint64_t end, bool click);
    void RecordComparison(int x, int y, int poseId, int rayId, std::uintptr_t posePed, std::uintptr_t rayPed);
    void RecordClick(const char* event, int playerId, int x, int y, std::size_t retries);
    void Abort(const char* reason);
    void SetRaycastAvailability(bool available, const char* reason);
    void RecordRendererDelta(const std::array<std::uint64_t, 8>& delta);
    void RecordRendererCounters(const std::array<std::uint64_t, 8>& counters);
    void RecordScene(float camX, float camY, float camZ, float forwardX, float forwardY, float forwardZ,
        int pedPoolSize, int width, int height,
        float selectionDistance, bool requireNameTag);
    void RecordCandidate(int id, float cursorX, float cursorY);
    void RecordNativeRay(float ox, float oy, float oz, float tx, float ty, float tz,
        std::uintptr_t firstPed, int model, const char* outcome);

private:
    static constexpr std::size_t kSampleCapacity = 8192;
    static constexpr std::size_t kMismatchCapacity = 128;

    struct Samples {
        std::array<std::uint64_t, kSampleCapacity> values{};
        std::uint64_t count = 0;
        std::uint64_t sum = 0;
        std::uint64_t maximum = 0;

        void Reset();
        void Add(std::uint64_t value);
        std::size_t Stored() const;
    };
    struct Window {
        Samples cpu{};
        Samples frame{};
        bool completed = false;
        bool invalid = false;
        std::uint64_t cameraMoves = 0;
        std::uint64_t cameraTurns = 0;
        std::uint64_t cursorMoves = 0;
        std::uint64_t candidateFrames = 0;
        std::array<std::uint64_t, 8> renderer{};
    };
    struct Scene {
        bool valid = false;
        float x = 0, y = 0, z = 0;
        float fx = 0, fy = 0, fz = 0;
        int pool = 0, width = 0, height = 0;
        float distance = 0;
        bool nameTag = false;
    };
    struct NativeRay {
        std::array<float, 6> points{};
        std::uintptr_t firstPed = 0;
        int model = -1;
        const char* outcome = "not-run";
    };
    struct Mismatch {
        std::uint64_t elapsedUs = 0;
        int x = 0, y = 0, poseId = -1, rayId = -1;
        std::uintptr_t posePed = 0, rayPed = 0;
        bool rayFirst = false;
        Scene scene{};
        NativeRay ray{};
    };

    void ResetSession();
    void StartSession(std::uint64_t now);
    void Report(const char* reason, bool complete);
    void ReportSamples(const char* metric, unsigned int pass, Samples& samples);
    void Request(Mode mode);
    static const char* AlgorithmName(Algorithm algorithm);
    static const char* ModeName(Mode mode);

    std::atomic<int> pendingCommand_{ -1 };
    Mode mode_ = Mode::Off;
    Algorithm frameAlgorithm_ = Algorithm::Pose;
    bool frameMeasuring_ = false;
    bool completionPending_ = false;
    bool raycastAvailable_ = false;
    bool availabilityKnown_ = false;
    bool sessionWasBenchmark_ = false;
    bool sessionInvalid_ = false;
    bool benchmarkStartsWithRaycast_ = false;
    bool nextBenchmarkStartsWithRaycast_ = false;
    char unavailableReason_[192] = "native gate not checked";
    char abortPending_[96]{};
    char caseLabel_[96]{};
    char frozenCaseLabel_[96]{};
    int expectedId_ = -2;
    int frozenExpectedId_ = -2;
    Algorithm clickAlgorithm_ = Algorithm::Pose;
    std::uint64_t clickStartedAt_ = 0;
    std::uint64_t clickId_ = 0;
    std::uint64_t clickSessionId_ = 0;
    std::uint64_t sessionId_ = 0;
    bool clickActive_ = false;
    std::uint64_t sessionStartedAt_ = 0;
    std::uint64_t lastFrameAt_ = 0;
    std::uint64_t frameNumber_ = 0;
    std::uint64_t currentElapsedUs_ = 0;
    std::uint64_t comparisons_ = 0;
    std::uint64_t disagreements_ = 0;
    std::uint64_t lastMismatchAt_ = 0;
    unsigned int pass_ = 0;
    std::size_t mismatchCount_ = 0;
    Scene scene_{};
    Scene initialScene_{};
    float previousCursorX_ = -1, previousCursorY_ = -1;
    int lastPoseId_ = -1, lastRayId_ = -1;
    NativeRay nativeRay_{};
    std::array<Window, kPassCount> windows_{};
    std::array<Mismatch, kMismatchCapacity> mismatches_{};
    bool rendererCountersKnown_ = false;
    std::array<std::uint64_t, 8> lastRendererCounters_{};
};

} // namespace targetdiag

#endif
