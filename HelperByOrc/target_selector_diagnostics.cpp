#include "target_selector_diagnostics.h"

#if HELPERBYORC_TARGET_DIAGNOSTICS

#include "debug_log.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace targetdiag {
namespace {

double CounterFrequency() {
    static const double frequency = [] {
        LARGE_INTEGER value{};
        return QueryPerformanceFrequency(&value) && value.QuadPart > 0
            ? static_cast<double>(value.QuadPart) : 1.0;
    }();
    return frequency;
}

std::uint64_t ToUs(std::uint64_t start, std::uint64_t end) {
    return static_cast<std::uint64_t>(Diagnostics::ElapsedUs(start, end));
}

} // namespace

Diagnostics& Diagnostics::Instance() {
    static Diagnostics instance;
    return instance;
}

std::uint64_t Diagnostics::NowTicks() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}

double Diagnostics::ElapsedUs(std::uint64_t start, std::uint64_t end) {
    return end >= start ? static_cast<double>(end - start) * 1000000.0 / CounterFrequency() : 0.0;
}

void Diagnostics::Samples::Reset() {
    count = sum = maximum = 0;
}

void Diagnostics::Samples::Add(std::uint64_t value) {
    if (count < values.size()) {
        values[static_cast<std::size_t>(count)] = value;
    }
    ++count;
    sum += value;
    maximum = (std::max)(maximum, value);
}

std::size_t Diagnostics::Samples::Stored() const {
    return static_cast<std::size_t>((std::min)(count, static_cast<std::uint64_t>(values.size())));
}

const char* Diagnostics::AlgorithmName(Algorithm algorithm) {
    return algorithm == Algorithm::Raycast ? "B-raycast" : "A-pose";
}

const char* Diagnostics::ModeName(Mode mode) {
    switch (mode) {
    case Mode::Pose: return "A-pose";
    case Mode::Raycast: return "B-raycast";
    case Mode::Shadow: return "shadow-A-primary";
    case Mode::Benchmark: return "exclusive-perf";
    default: return "off";
    }
}

void Diagnostics::Request(Mode mode) {
    pendingCommand_.store(static_cast<int>(mode), std::memory_order_relaxed);
}

bool Diagnostics::HandleKey(UINT message, WPARAM wparam, LPARAM lparam) {
    if (wparam < VK_F6 || wparam > VK_F10) {
        return false;
    }
    if (message == WM_KEYUP || message == WM_SYSKEYUP) {
        return true;
    }
    if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN) {
        return false;
    }
    if ((static_cast<std::uintptr_t>(lparam) & (std::uintptr_t{ 1 } << 30)) == 0) {
        constexpr Mode modes[] = { Mode::Pose, Mode::Raycast, Mode::Shadow, Mode::Benchmark, Mode::Off };
        Request(modes[wparam - VK_F6]);
    }
    return true;
}

bool Diagnostics::ApplyPendingCommand() {
    const int command = pendingCommand_.exchange(-1, std::memory_order_relaxed);
    if (command < 0) {
        return false;
    }
    if (sessionStartedAt_) {
        if (clickActive_) RecordClick("canceled-mode-change", -1, 0, 0, 0);
        Report("mode-change", false);
    }
    mode_ = static_cast<Mode>(command);
    if (mode_ == Mode::Benchmark) {
        benchmarkStartsWithRaycast_ = nextBenchmarkStartsWithRaycast_;
        nextBenchmarkStartsWithRaycast_ = !nextBenchmarkStartsWithRaycast_;
    }
    frameAlgorithm_ = mode_ == Mode::Raycast ? Algorithm::Raycast : Algorithm::Pose;
    ResetSession();
    debuglog::WriteInfo("[target-diag] mode=%s case=%s expectedId=%d", ModeName(mode_), caseLabel_, expectedId_);
    return true;
}

void Diagnostics::ResetSession() {
    sessionStartedAt_ = lastFrameAt_ = frameNumber_ = currentElapsedUs_ = 0;
    comparisons_ = disagreements_ = lastMismatchAt_ = 0;
    pass_ = 0;
    mismatchCount_ = 0;
    frameMeasuring_ = completionPending_ = sessionInvalid_ = false;
    rendererCountersKnown_ = false;
    abortPending_[0] = '\0';
    clickActive_ = false;
    initialScene_ = {};
    for (auto& window : windows_) {
        window.cpu.Reset();
        window.frame.Reset();
        window.completed = window.invalid = false;
        window.cameraMoves = window.cameraTurns = window.cursorMoves = window.candidateFrames = 0;
        window.renderer.fill(0);
    }
}

void Diagnostics::StartSession(std::uint64_t now) {
    ++sessionId_;
    sessionWasBenchmark_ = mode_ == Mode::Benchmark;
    // Warm frequency lookup and measure paired counter overhead outside all measured windows.
    CounterFrequency();
    std::uint64_t overhead = ~std::uint64_t{ 0 };
    for (unsigned int i = 0; i < 32; ++i) {
        const auto start = NowTicks();
        const auto finish = NowTicks();
        overhead = (std::min)(overhead, finish - start);
    }
    debuglog::WriteInfo(
        "[target-diag] begin session=%llu mode=%s case=%s expectedId=%d clock=QPC frequency=%.0f pairMinUs=%.4f "
        "settleUs=%llu measureUs=%llu sampleCapacity=%zu maskHz=30 shadowIsNotPerf=1 order=%s",
        sessionId_, ModeName(mode_), caseLabel_, expectedId_, CounterFrequency(), ElapsedUs(0, overhead),
        kSettleUs, kMeasureUs, kSampleCapacity, benchmarkStartsWithRaycast_ ? "BABA" : "ABAB");
    // Logging/calibration cannot contribute to the first frame interval or settle duration.
    sessionStartedAt_ = NowTicks();
    lastFrameAt_ = 0;
    (void)now;
}

void Diagnostics::BeginFrame() {
    frameMeasuring_ = false;
    if (mode_ == Mode::Off) {
        return;
    }
    if (!sessionStartedAt_) {
        StartSession(NowTicks());
    }
    const auto now = NowTicks();
    const std::uint64_t previousElapsedUs = currentElapsedUs_;
    const unsigned int previousPass = pass_;
    currentElapsedUs_ = ToUs(sessionStartedAt_, now);
    ++frameNumber_;
    if (mode_ == Mode::Benchmark) {
        const Phase phase = LocatePhase(currentElapsedUs_);
        pass_ = phase.pass;
        frameAlgorithm_ = AlgorithmForPass(pass_, benchmarkStartsWithRaycast_) ? Algorithm::Raycast : Algorithm::Pose;
        for (unsigned int i = 0; i < kPassCount; ++i) {
            windows_[i].completed = currentElapsedUs_ >= kPassUs * (i + 1);
        }
        completionPending_ = phase.complete;
        // Do not attribute the interval crossing a phase/settle boundary to either method.
        frameMeasuring_ = phase.measuring && previousPass == pass_
            && LocatePhase(previousElapsedUs).measuring && !phase.complete;
    } else {
        frameAlgorithm_ = mode_ == Mode::Raycast ? Algorithm::Raycast : Algorithm::Pose;
        frameMeasuring_ = mode_ != Mode::Shadow && previousElapsedUs >= kSettleUs;
    }
    if (debuglog::GetLevel() != debuglog::Level::Info) {
        sessionInvalid_ = true;
        std::snprintf(abortPending_, sizeof(abortPending_), "%s", "log-level-not-info");
    }
    if (frameMeasuring_ && lastFrameAt_ && now >= lastFrameAt_) {
        windows_[pass_].frame.Add(now - lastFrameAt_);
    }
    lastFrameAt_ = now;
}

void Diagnostics::EndFrame() {
    if (abortPending_[0]) {
        const bool unavailable = std::strcmp(abortPending_, "raycast-unavailable") == 0;
        Abort(abortPending_);
        if (unavailable) mode_ = Mode::Off;
    } else if (completionPending_) {
        const Algorithm finalAlgorithm = frameAlgorithm_;
        Report("complete", true);
        mode_ = finalAlgorithm == Algorithm::Raycast ? Mode::Raycast : Mode::Pose;
        ResetSession();
    }
}

Algorithm Diagnostics::ActiveAlgorithm() const { return frameAlgorithm_; }
bool Diagnostics::WantsShadow() const { return mode_ == Mode::Shadow; }
bool Diagnostics::IsEnabled() const { return mode_ != Mode::Off; }
bool Diagnostics::ShadowRaycastFirst() const { return (frameNumber_ & 1u) != 0; }

void Diagnostics::RecordResolve(Algorithm algorithm, std::uint64_t start, std::uint64_t end, bool click) {
    if (click || !frameMeasuring_ || completionPending_ || algorithm != frameAlgorithm_ || end < start) {
        return;
    }
    windows_[pass_].cpu.Add(end - start);
}

void Diagnostics::RecordRendererDelta(const std::array<std::uint64_t, 8>& delta) {
    if (!frameMeasuring_ || completionPending_) {
        return;
    }
    for (std::size_t i = 0; i < delta.size(); ++i) {
        windows_[pass_].renderer[i] += delta[i];
    }
    if (sessionWasBenchmark_ && (delta[2] || delta[3])) windows_[pass_].invalid = true;
}

void Diagnostics::RecordRendererCounters(const std::array<std::uint64_t, 8>& counters) {
    if (mode_ == Mode::Off || !sessionStartedAt_) {
        rendererCountersKnown_ = false;
        return;
    }
    if (rendererCountersKnown_) {
        std::array<std::uint64_t, 8> delta{};
        for (std::size_t i = 0; i < counters.size(); ++i) {
            delta[i] = counters[i] - lastRendererCounters_[i];
        }
        // Covers the preceding native ped render and this UI callback. Settle frames
        // update the baseline too; RecordRendererDelta excludes phase boundaries.
        RecordRendererDelta(delta);
    }
    lastRendererCounters_ = counters;
    rendererCountersKnown_ = true;
}

void Diagnostics::RecordScene(float camX, float camY, float camZ, float forwardX, float forwardY, float forwardZ,
    int pedPoolSize, int width, int height,
    float selectionDistance, bool requireNameTag) {
    const Scene next{ true, camX, camY, camZ, forwardX, forwardY, forwardZ,
        pedPoolSize, width, height, selectionDistance, requireNameTag };
    if (sessionStartedAt_) {
        if (!initialScene_.valid) {
            initialScene_ = next;
        } else if (next.width != initialScene_.width || next.height != initialScene_.height
            || next.distance != initialScene_.distance || next.nameTag != initialScene_.nameTag) {
            sessionInvalid_ = true;
            std::snprintf(abortPending_, sizeof(abortPending_), "%s", "scene-config-changed");
        }
        if (scene_.valid) {
            const float dx = next.x - scene_.x, dy = next.y - scene_.y, dz = next.z - scene_.z;
            if (frameMeasuring_ && dx * dx + dy * dy + dz * dz > 0.0001f) {
                ++windows_[pass_].cameraMoves;
            }
            const float fx = next.fx - scene_.fx, fy = next.fy - scene_.fy, fz = next.fz - scene_.fz;
            if (frameMeasuring_ && fx * fx + fy * fy + fz * fz > 0.000001f)
                ++windows_[pass_].cameraTurns;
        }
    }
    scene_ = next;
}

void Diagnostics::RecordCandidate(int id, float cursorX, float cursorY) {
    if (frameMeasuring_) {
        if (id >= 0) ++windows_[pass_].candidateFrames;
        if (cursorX != previousCursorX_ || cursorY != previousCursorY_) ++windows_[pass_].cursorMoves;
    }
    previousCursorX_ = cursorX;
    previousCursorY_ = cursorY;
}

void Diagnostics::RecordComparison(int x, int y, int poseId, int rayId,
    std::uintptr_t posePed, std::uintptr_t rayPed) {
    ++comparisons_;
    lastPoseId_ = poseId;
    lastRayId_ = rayId;
    if (poseId == rayId && posePed == rayPed) {
        return;
    }
    ++disagreements_;
    if (mismatchCount_ >= mismatches_.size()) {
        return;
    }
    if (mismatchCount_ > 0) {
        const auto& last = mismatches_[mismatchCount_ - 1];
        if (currentElapsedUs_ - lastMismatchAt_ < 200000
            || (last.x == x && last.y == y && last.poseId == poseId && last.rayId == rayId
                && last.posePed == posePed && last.rayPed == rayPed)) {
            return;
        }
    }
    mismatches_[mismatchCount_++] = {
        currentElapsedUs_, x, y, poseId, rayId, posePed, rayPed, ShadowRaycastFirst(), scene_, nativeRay_,
    };
    lastMismatchAt_ = currentElapsedUs_;
}

void Diagnostics::RecordClick(const char* event, int playerId, int x, int y, std::size_t retries) {
    if (!IsEnabled()) {
        return;
    }
    if (std::strcmp(event, "queued") == 0) {
        if (clickActive_) RecordClick("superseded", -1, x, y, retries);
        if (mode_ == Mode::Benchmark) {
            sessionInvalid_ = true;
            Abort("click-during-benchmark");
            StartSession(NowTicks());
        }
        clickAlgorithm_ = frameAlgorithm_;
        frozenExpectedId_ = expectedId_;
        std::snprintf(frozenCaseLabel_, sizeof(frozenCaseLabel_), "%s", caseLabel_);
        clickStartedAt_ = NowTicks();
        ++clickId_;
        clickSessionId_ = sessionId_;
        clickActive_ = true;
    }
    debuglog::WriteInfo(
        "[target-diag][click] session=%llu click=%llu event=%s algorithm=%s expectedId=%d id=%d x=%d y=%d retries=%zu "
        "elapsedUs=%.1f case=%s",
        clickSessionId_, clickId_, event, AlgorithmName(clickAlgorithm_), frozenExpectedId_, playerId, x, y, retries,
        clickStartedAt_ ? ElapsedUs(clickStartedAt_, NowTicks()) : 0.0, frozenCaseLabel_);
    if (std::strcmp(event, "raw-B") == 0) {
        debuglog::WriteInfo("[target-diag][click-native] session=%llu click=%llu firstPed=%p model=%d outcome=%s "
            "ray=%.5f,%.5f,%.5f->%.5f,%.5f,%.5f", clickSessionId_, clickId_,
            reinterpret_cast<void*>(nativeRay_.firstPed), nativeRay_.model, nativeRay_.outcome,
            nativeRay_.points[0], nativeRay_.points[1], nativeRay_.points[2],
            nativeRay_.points[3], nativeRay_.points[4], nativeRay_.points[5]);
    }
    if (std::strcmp(event, "selected") == 0 || std::strcmp(event, "expired") == 0
        || std::strcmp(event, "validation-failed") == 0 || std::strcmp(event, "no-candidate") == 0
        || std::strcmp(event, "request-rejected") == 0) clickActive_ = false;
}

void Diagnostics::RecordNativeRay(float ox, float oy, float oz, float tx, float ty, float tz,
    std::uintptr_t firstPed, int model, const char* outcome) {
    nativeRay_ = { { ox, oy, oz, tx, ty, tz }, firstPed, model, outcome };
}

void Diagnostics::SetRaycastAvailability(bool available, const char* reason) {
    if (!availabilityKnown_ || raycastAvailable_ != available) {
        debuglog::WriteInfo("[target-diag] raycastAvailable=%d reason=%s", available ? 1 : 0, reason ? reason : "-");
    }
    availabilityKnown_ = true;
    raycastAvailable_ = available;
    std::snprintf(unavailableReason_, sizeof(unavailableReason_), "%s", reason ? reason : "-");
    if (!available && sessionStartedAt_ && mode_ != Mode::Pose && mode_ != Mode::Off) {
        sessionInvalid_ = true;
        std::snprintf(abortPending_, sizeof(abortPending_), "%s", "raycast-unavailable");
    }
}

void Diagnostics::Abort(const char* reason) {
    // Only complete ABAB cycles can support a comparison. Interrupted sessions are evidence, not a pass.
    if (mode_ == Mode::Benchmark) sessionInvalid_ = true;
    if (clickActive_) RecordClick("canceled", -1, 0, 0, 0);
    if (sessionStartedAt_) {
        Report(reason, false);
    }
    if (mode_ == Mode::Benchmark) {
        mode_ = frameAlgorithm_ == Algorithm::Raycast ? Mode::Raycast : Mode::Pose;
    }
    ResetSession();
}

void Diagnostics::ReportSamples(const char* metric, unsigned int pass, Samples& samples) {
    const std::size_t stored = samples.Stored();
    std::sort(samples.values.begin(), samples.values.begin() + stored);
    const auto quantile = [&](unsigned int p) {
        return stored ? ElapsedUs(0, samples.values[QuantileIndex(stored, p)]) : 0.0;
    };
    debuglog::WriteInfo(
        "[target-diag][distribution] session=%llu pass=%u metric=%s unit=us n=%llu stored=%zu overflow=%d "
        "mean=%.4f median=%.4f p95=%.4f p99=%.4f max=%.4f",
        sessionId_, pass + 1, metric, samples.count, stored, samples.count > stored ? 1 : 0,
        samples.count ? ElapsedUs(0, samples.sum) / static_cast<double>(samples.count) : 0.0,
        quantile(50), quantile(95), quantile(99), ElapsedUs(0, samples.maximum));
}

void Diagnostics::Report(const char* reason, bool complete) {
    frameMeasuring_ = false;
    if (sessionWasBenchmark_) {
        for (auto& window : windows_) {
            const double seconds = ElapsedUs(0, window.frame.sum) / 1000000.0;
            window.invalid = window.invalid || !window.completed || window.cpu.count == 0
                || window.cpu.count != window.frame.count || window.cpu.count > kSampleCapacity
                || window.frame.count > kSampleCapacity || seconds < 11.4 || seconds > 12.6;
            sessionInvalid_ = sessionInvalid_ || window.invalid;
        }
    }
    debuglog::WriteInfo(
        "[target-diag][end] session=%llu mode=%s reason=%s complete=%d invalid=%d elapsedUs=%llu "
        "case=%s expectedId=%d compares=%llu disagreements=%llu storedMismatch=%zu "
        "viewport=%dx%d distance=%.1f requireNameTag=%d pedPoolCapacity=%d cam=%.5f,%.5f,%.5f",
        sessionId_, sessionWasBenchmark_ ? (benchmarkStartsWithRaycast_ ? "exclusive-BABA" : "exclusive-ABAB")
            : ModeName(mode_), reason ? reason : "-", complete ? 1 : 0,
        sessionInvalid_ ? 1 : 0, currentElapsedUs_, caseLabel_, expectedId_, comparisons_, disagreements_,
        mismatchCount_, initialScene_.width, initialScene_.height, initialScene_.distance,
        initialScene_.nameTag ? 1 : 0, initialScene_.pool, initialScene_.x, initialScene_.y, initialScene_.z);
    if (mode_ != Mode::Shadow) {
        const unsigned int windowCount = sessionWasBenchmark_ ? kPassCount : 1;
        for (unsigned int i = 0; i < windowCount; ++i) {
            auto& window = windows_[i];
            if (!window.cpu.count && !window.frame.count) {
                continue;
            }
            const Algorithm algorithm = sessionWasBenchmark_
                ? (AlgorithmForPass(i, benchmarkStartsWithRaycast_) ? Algorithm::Raycast : Algorithm::Pose) : frameAlgorithm_;
            const bool valid = !sessionInvalid_ && !window.invalid && window.cpu.count == window.frame.count
                && window.cpu.count <= kSampleCapacity && window.frame.count <= kSampleCapacity
                && (sessionWasBenchmark_ ? window.completed : false);
            const double frameSeconds = ElapsedUs(0, window.frame.sum) / 1000000.0;
            const double pixels = static_cast<double>(initialScene_.width) * initialScene_.height;
            const auto& render = window.renderer;
            debuglog::WriteInfo(
                "[target-diag][window] session=%llu pass=%u algorithm=%s valid=%d completed=%d sampledSeconds=%.6f "
                "callbackHz=%.3f cameraMoveFrames=%llu cameraTurnFrames=%llu cursorMoveFrames=%llu "
                "candidateFrames=%llu captures=%llu captureHz=%.3f composites=%llu "
                "queriesIssued=%llu queriesResolved=%llu partialResolves=%llu fullResolves=%llu "
                "resolvedPixels=%llu compositePixels=%llu resolveAreaPct=%.4f compositeAreaPct=%.4f",
                sessionId_, i + 1, AlgorithmName(algorithm), valid ? 1 : 0, window.completed ? 1 : 0,
                frameSeconds, frameSeconds > 0 ? window.frame.count / frameSeconds : 0.0,
                window.cameraMoves, window.cameraTurns, window.cursorMoves, window.candidateFrames,
                render[0], frameSeconds > 0 ? render[0] / frameSeconds : 0.0,
                render[1], render[2], render[3], render[4], render[5], render[6], render[7],
                pixels > 0 && render[0] ? 100.0 * render[6] / (pixels * render[0]) : 0.0,
                pixels > 0 && render[1] ? 100.0 * render[7] / (pixels * render[1]) : 0.0);
            ReportSamples("resolve-cpu", i, window.cpu);
            ReportSamples("render-callback-interval", i, window.frame);
        }
    }
    for (std::size_t i = 0; i < mismatchCount_; ++i) {
        const auto& mismatch = mismatches_[i];
        debuglog::WriteInfo(
            "[target-diag][mismatch] session=%llu sample=%zu elapsedUs=%llu x=%d y=%d A=%d B=%d "
            "pedA=%p pedB=%p order=%s cam=%.5f,%.5f,%.5f pool=%d "
            "ray=%.5f,%.5f,%.5f->%.5f,%.5f,%.5f firstPed=%p model=%d outcome=%s",
            sessionId_, i + 1, mismatch.elapsedUs, mismatch.x, mismatch.y, mismatch.poseId, mismatch.rayId,
            reinterpret_cast<void*>(mismatch.posePed), reinterpret_cast<void*>(mismatch.rayPed),
            mismatch.rayFirst ? "BA" : "AB", mismatch.scene.x, mismatch.scene.y, mismatch.scene.z, mismatch.scene.pool,
            mismatch.ray.points[0], mismatch.ray.points[1], mismatch.ray.points[2],
            mismatch.ray.points[3], mismatch.ray.points[4], mismatch.ray.points[5],
            reinterpret_cast<void*>(mismatch.ray.firstPed), mismatch.ray.model, mismatch.ray.outcome);
    }
}

void Diagnostics::DrawSettings() {
    ImGui::SeparatorText("Target selector: диагностическая сборка");
    ImGui::TextWrapped("A — bounds + pose; B — GTA ped raycast; Shadow — сравнение ID при одном курсоре. "
        "Скорость сравнивает только A/B/A/B: 2 с прогрев + 12 с замер на каждый проход.");
    if (debuglog::GetLevel() != debuglog::Level::Info) {
        ImGui::TextColored(ImVec4(1, 0.55f, 0.25f, 1), "Для сохранения результатов включи уровень лога Info.");
    }
    ImGui::Text("Режим: %s; raycast: %s", ModeName(mode_), raycastAvailable_ ? "готов" : unavailableReason_);
    ImGui::InputText("Метка сценария", caseLabel_, sizeof(caseLabel_));
    ImGui::InputInt("Ожидаемый ID (-2: не задан, -1: фон)", &expectedId_);
    expectedId_ = (std::max)(-2, (std::min)(1003, expectedId_));
    if (ImGui::Button("A / F6")) Request(Mode::Pose);
    ImGui::SameLine();
    if (ImGui::Button("B / F7")) Request(Mode::Raycast);
    ImGui::SameLine();
    if (ImGui::Button("Shadow / F8")) Request(Mode::Shadow);
    if (ImGui::Button("A/B/A/B (повтор: B/A/B/A) / F9")) Request(Mode::Benchmark);
    ImGui::SameLine();
    if (ImGui::Button("Стоп / F10")) Request(Mode::Off);
    ImGui::TextWrapped("Закрой меню и включи выбор цели. F6–F10 работают только при активном выборе. "
        "В замере не кликай: это прервёт цикл. Сохраняй одинаковые сцену, курсор, камеру и FPS limit. "
        "Shadow нагружает оба алгоритма и не даёт сравнения FPS. Результаты: [target-diag] в HelperByOrc.log.");
}

void Diagnostics::DrawOverlay() const {
    ImGui::Separator();
    ImGui::Text("DIAG %s | %s | F6 A  F7 B  F8 Shadow  F9 ABAB  F10 Stop", ModeName(mode_), AlgorithmName(frameAlgorithm_));
    if (mode_ == Mode::Benchmark) {
        const auto phase = LocatePhase(currentElapsedUs_);
        ImGui::Text("Проход %u/4: %s %.1f / 14 с", phase.pass + 1,
            phase.measuring ? "замер" : "прогрев", (currentElapsedUs_ % kPassUs) / 1000000.0);
    } else if (mode_ == Mode::Shadow) {
        ImGui::Text("A: %d  B: %d | сравнений: %llu; расхождений: %llu", lastPoseId_, lastRayId_, comparisons_, disagreements_);
    }
    if (!raycastAvailable_) ImGui::TextDisabled("B: %s", unavailableReason_);
}

} // namespace targetdiag

#endif
