#pragma once

#include "ped_outline_performance.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

#include <d3d9.h>

class CPed;

class PedOutlineRenderer {
public:
    using RenderEntityFn = void(__thiscall*)(CPed*);

    enum class CursorHitStatus : std::uint8_t {
        Pending,
        Miss,
        Hit,
    };

    PedOutlineRenderer() = default;
    ~PedOutlineRenderer();

    PedOutlineRenderer(const PedOutlineRenderer&) = delete;
    PedOutlineRenderer& operator=(const PedOutlineRenderer&) = delete;

    void SetTarget(CPed* ped);
    void Deactivate();
    bool Render(
        IDirect3DDevice9* device,
        float cursorX,
        float cursorY,
        CursorHitStatus& cursorHit);
    bool HasPendingCursorQueries() const;
    bool PollPendingCursorQueries();
    void OnDeviceLost();
    void OnDeviceReset();
    void Shutdown();

    bool IsDisabled() const;
    bool ConsumeFailure(std::string& reason);

private:
    enum class RoiResolveStatus : std::uint8_t {
        Success,
        SuccessFixedBounds,
        InvalidInput,
        BoneProjectionFailed,
        InvalidProjectedBounds,
    };

    struct FrameContext {
        IDirect3DSurface9* renderTarget = nullptr;
        IDirect3DSurface9* depthStencil = nullptr;
        IDirect3DVertexBuffer9* streamSource = nullptr;
        D3DVIEWPORT9 viewport{};
        UINT streamOffset = 0;
        UINT streamStride = 0;
    };

    struct CursorHitQuerySlot {
        IDirect3DQuery9* query = nullptr;
        CPed* ped = nullptr;
        int x = 0;
        int y = 0;
        bool pending = false;
        std::uint64_t generation = 0;
        std::uint64_t session = 0;
        std::uint64_t issuedAtMs = 0;
    };

    static void __fastcall PedRenderCall1(CPed* ped, void*);
    static void __fastcall PedRenderCall2(CPed* ped, void*);

    bool InstallRenderHooks();
    void RestoreRenderHooks();
    bool EnsureResources(IDirect3DDevice9* device);
    void ReleaseResources();
    bool AcquireFrameContext(FrameContext& context);
    void RestoreFrameContext(FrameContext& context);
    RoiResolveStatus ResolveScreenRoi(CPed* ped, outlineperf::ScreenRoi& roi);
    bool CaptureVisibleMask(
        CPed* ped,
        const outlineperf::ScreenRoi& roi,
        bool& renderRejected);
    bool ResolveVisibleMask(const outlineperf::ScreenRoi& roi);
    bool CompositePendingOutline();
    bool PollCursorHitQuery(
        CPed* target,
        int cursorX,
        int cursorY,
        CursorHitStatus& cursorHit);
    bool IssueCursorHitQuery(
        CPed* target,
        int cursorX,
        int cursorY);
    void ResetCursorHitState();
    void CaptureIfTarget(CPed* ped);
    void LogPerformanceIfDue(bool force);
    void Disable(std::string reason);

    static PedOutlineRenderer* self_;

    std::atomic<CPed*> targetPed_{ nullptr };
    CPed* capturedPed_ = nullptr;
    IDirect3DDevice9* device_ = nullptr;
    IDirect3DTexture9* visibleTexture_ = nullptr;
    IDirect3DSurface9* visibleSurface_ = nullptr;
    IDirect3DSurface9* multisampleCaptureSurface_ = nullptr;
    IDirect3DPixelShader9* outlineShader_ = nullptr;
    IDirect3DPixelShader9* cursorHitShader_ = nullptr;
    std::array<CursorHitQuerySlot, outlineperf::kCursorHitQuerySlotCount>
        cursorHitQueries_{};
    IDirect3DStateBlock9* stateBlock_ = nullptr;
    IDirect3DStateBlock9* maskStateBlock_ = nullptr;
    IDirect3DStateBlock9* compositeStateBlock_ = nullptr;
    IDirect3DStateBlock9* cursorHitStateBlock_ = nullptr;
    D3DSURFACE_DESC targetDesc_{};
    outlineperf::ScreenRoi capturedRoi_{};
    RenderEntityFn previousRender1_ = nullptr;
    RenderEntityFn previousRender2_ = nullptr;
    std::uintptr_t renderCall1_ = 0;
    std::uintptr_t renderCall2_ = 0;
    bool renderCall1Active_ = false;
    bool renderCall2Active_ = false;
    bool rendering_ = false;
    bool maskValid_ = false;
    bool disabled_ = false;
    bool failurePending_ = false;
    bool readyLogged_ = false;
    bool activeLogged_ = false;
    bool cursorSampleValid_ = false;
    bool cursorSampleHit_ = false;
    int partialResolveSupported_ = -1;
    int cursorSampleX_ = 0;
    int cursorSampleY_ = 0;
    CPed* cursorSamplePed_ = nullptr;
    std::uint64_t maskGeneration_ = 0;
    std::uint64_t cursorSampleGeneration_ = 0;
    outlineperf::CursorQuerySession cursorQuerySession_{};
    outlineperf::CursorPixelStability cursorPixelStability_{};
    std::uint64_t lastCaptureAtMs_ = 0;
    std::uint64_t perfWindowStartedAtMs_ = 0;
    std::uint64_t perfCaptureCount_ = 0;
    std::uint64_t perfCaptureReuseCount_ = 0;
    std::uint64_t perfCompositeCount_ = 0;
    std::uint64_t perfPartialResolveCount_ = 0;
    std::uint64_t perfFullResolveCount_ = 0;
    std::uint64_t perfBoneBoundsCount_ = 0;
    std::uint64_t perfFixedBoundsCount_ = 0;
    std::uint64_t perfRoiSkippedCount_ = 0;
    std::uint64_t perfUnsafePedSkipCount_ = 0;
    std::uint64_t perfRoiInvalidInputCount_ = 0;
    std::uint64_t perfRoiBoneProjectionFailedCount_ = 0;
    std::uint64_t perfRoiInvalidProjectedBoundsCount_ = 0;
    std::uint64_t perfCompositePixels_ = 0;
    std::uint64_t perfResolvedPixels_ = 0;
    std::uint64_t perfCursorQueryIssuedCount_ = 0;
    std::uint64_t perfCursorQueryResolvedCount_ = 0;
    std::uint64_t perfCursorHitCount_ = 0;
    std::uint64_t perfCursorMissCount_ = 0;
    std::uint64_t perfCursorStaleResultCount_ = 0;
    std::uint64_t perfCursorQueryRetiredCount_ = 0;
    std::uint64_t perfCursorMotionDeferredCount_ = 0;
    std::uint64_t perfCursorQueryPoolBusyCount_ = 0;
    std::uint64_t perfCursorQueryLatencySampleCount_ = 0;
    std::uint64_t perfCursorQueryLatencyTotalMs_ = 0;
    std::uint64_t perfCursorQueryLatencyMaxMs_ = 0;
    CPed* rejectedPed_ = nullptr;
    void* rejectedClump_ = nullptr;
    std::string failureReason_{};
};
