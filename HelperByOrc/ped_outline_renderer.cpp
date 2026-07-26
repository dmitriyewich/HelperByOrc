#include "ped_outline_renderer.h"

#include "debug_log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <CPed.h>
#include <CSprite.h>
#include <common.h>
#include <ePedBones.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace {

constexpr std::uintptr_t kPreferredImageBase = 0x00400000;
constexpr std::uintptr_t kRenderEntityAddress = 0x00534310;
constexpr std::uintptr_t kPedRenderCall1Address = 0x005E77FC;
constexpr std::uintptr_t kPedRenderCall2Address = 0x005E780A;
constexpr std::uintptr_t kRwFlushStateCacheAddress = 0x007FC200;
constexpr std::uintptr_t kRwGetRenderStateAddress = 0x007FC320;
constexpr float kOutlineThickness = 3.0f;
constexpr float kOutlineDiagonal = 2.0f;
constexpr std::uint64_t kPerformanceLogIntervalMs = 5000;
constexpr float kFallbackPedHalfWidth = 1.35f;
constexpr float kFallbackPedBottomOffset = -0.50f;
constexpr float kFallbackPedTopOffset = 3.20f;

constexpr std::array<unsigned int, 11> kOutlineBoundsBones = {
    BONE_HEAD,
    BONE_UPPERTORSO,
    BONE_PELVIS,
    BONE_RIGHTSHOULDER,
    BONE_RIGHTHAND,
    BONE_LEFTSHOULDER,
    BONE_LEFTHAND,
    BONE_RIGHTKNEE,
    BONE_RIGHTFOOT,
    BONE_LEFTKNEE,
    BONE_LEFTFOOT,
};

constexpr std::array<D3DRENDERSTATETYPE, 6> kCaptureRenderStates = {
    D3DRS_ZENABLE,
    D3DRS_ZWRITEENABLE,
    D3DRS_ZFUNC,
    D3DRS_STENCILENABLE,
    D3DRS_SCISSORTESTENABLE,
    D3DRS_COLORWRITEENABLE,
};

// fxc 10.1 /O3 /T ps_2_0: s0=visible, c0=offsets, c1=white.
// Formula matches the visible branch of outline.lua:
// (1 - centerAlpha) * max(eight neighborAlpha).
constexpr DWORD kOutlineShader[] = {
    0xFFFF0200, 0x05000051, 0xA00F0002, 0x80000000, 0xBF800000, 0x3F800000, 0x00000000, 0x0200001F,
    0x80000000, 0xB0030000, 0x0200001F, 0x90000000, 0xA00F0800, 0x02000001, 0x80010000, 0xA1000000,
    0x02000001, 0x80020000, 0xA0000002, 0x03000002, 0x80030000, 0x80E40000, 0xB0E40000, 0x03000002,
    0x80010001, 0xB0000000, 0xA0000000, 0x02000001, 0x80020001, 0xB0550000, 0x02000001, 0x80010002,
    0xB0000000, 0x03000002, 0x80020002, 0xB0550000, 0xA0550000, 0x02000001, 0x80010003, 0xA0000002,
    0x02000001, 0x80020003, 0xA1550000, 0x03000002, 0x80030003, 0x80E40003, 0xB0E40000, 0x03000002,
    0x80010004, 0xB0000000, 0xA0AA0000, 0x03000002, 0x80020004, 0xB0550000, 0xA0FF0000, 0x03000002,
    0x80010005, 0xB0000000, 0xA1AA0000, 0x03000002, 0x80020005, 0xB0550000, 0xA1FF0000, 0x03000002,
    0x80010006, 0xB0000000, 0xA0AA0000, 0x03000002, 0x80020006, 0xB0550000, 0xA1FF0000, 0x02000001,
    0x800C0000, 0xA0E40000, 0x04000004, 0x80010007, 0x80AA0000, 0xA0550002, 0xB0000000, 0x04000004,
    0x80020007, 0x80FF0000, 0xA0AA0002, 0xB0550000, 0x03000042, 0x800F0000, 0x80E40000, 0xA0E40800,
    0x03000042, 0x800F0001, 0x80E40001, 0xA0E40800, 0x03000042, 0x800F0002, 0x80E40002, 0xA0E40800,
    0x03000042, 0x800F0003, 0x80E40003, 0xA0E40800, 0x03000042, 0x800F0004, 0x80E40004, 0xA0E40800,
    0x03000042, 0x800F0005, 0x80E40005, 0xA0E40800, 0x03000042, 0x800F0006, 0x80E40006, 0xA0E40800,
    0x03000042, 0x800F0007, 0x80E40007, 0xA0E40800, 0x03000042, 0x800F0008, 0xB0E40000, 0xA0E40800,
    0x0300000B, 0x80010002, 0x80FF0001, 0x80FF0000, 0x0300000B, 0x80010000, 0x80000002, 0x80FF0002,
    0x0300000B, 0x80010001, 0x80000000, 0x80FF0003, 0x0300000B, 0x80010000, 0x80000001, 0x80FF0004,
    0x0300000B, 0x80010001, 0x80000000, 0x80FF0005, 0x0300000B, 0x80010000, 0x80000001, 0x80FF0006,
    0x0300000B, 0x80010001, 0x80000000, 0x80FF0007, 0x03000002, 0x80010000, 0x81FF0008, 0xA0AA0002,
    0x03000005, 0x80010000, 0x80000001, 0x80000000, 0x03000005, 0x800F0000, 0x80000000, 0xA0E40001,
    0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF,
};

// fxc 10.1 /O3 /T ps_2_0. One point-filtered alpha sample followed by
// texkill. Used inside an asynchronous D3D9 occlusion query; no readback.
constexpr DWORD kCursorHitShader[] = {
    0xFFFF0200,
    0x05000051, 0xA00F0000, 0xBB808081, 0x00000000, 0x00000000, 0x00000000,
    0x0200001F, 0x80000000, 0xB0030000,
    0x0200001F, 0x90000000, 0xA00F0800,
    0x03000042, 0x800F0000, 0xB0E40000, 0xA0E40800,
    0x03000002, 0x800F0000, 0x80FF0000, 0xA0000000,
    0x01000041, 0x800F0000,
    0x02000001, 0x800F0000, 0xA0550000,
    0x02000001, 0x800F0800, 0x80E40000,
    0x0000FFFF,
};

struct OutlineVertex {
    float x;
    float y;
    float z;
    float rhw;
    float u;
    float v;
};

template <typename T>
void ReleaseCom(T*& object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
}

std::uintptr_t RebasedAddress(std::uintptr_t preferredAddress) {
    const auto imageBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    return imageBase + (preferredAddress - kPreferredImageBase);
}

using RwFlushStateCacheFn = void(__cdecl*)();
using RwGetRenderStateFn = void(__cdecl*)(DWORD, DWORD*);

__declspec(noinline) bool HasValidAnimHierarchy(RpClump* clump) {
    if (!clump) {
        return false;
    }

    bool valid = false;
    __try {
        valid = GetAnimHierarchyFromSkinClump(clump) != nullptr;
    } __except (
        GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER
            : EXCEPTION_CONTINUE_SEARCH) {
    }
    return valid;
}

__declspec(noinline) bool TryProjectPedBones(
    CPed* ped,
    outlineperf::ProjectedPoint* projectedPoints,
    std::size_t pointCount) {
    if (!ped
        || !projectedPoints
        || pointCount < kOutlineBoundsBones.size()
        || !HasValidAnimHierarchy(ped->m_pRwClump)) {
        return false;
    }

    bool projected = true;
    __try {
        for (std::size_t index = 0; index < kOutlineBoundsBones.size(); ++index) {
            RwV3d world{};
            ped->GetBonePosition(world, kOutlineBoundsBones[index], false);
            RwV3d screen{};
            float screenScaleX = 0.0f;
            float screenScaleY = 0.0f;
            if (!std::isfinite(world.x)
                || !std::isfinite(world.y)
                || !std::isfinite(world.z)
                || !CSprite::CalcScreenCoors(
                    world,
                    &screen,
                    &screenScaleX,
                    &screenScaleY,
                    false,
                    false)
                || !std::isfinite(screen.x)
                || !std::isfinite(screen.y)
                || !std::isfinite(screen.z)
                || screen.z <= 0.0f) {
                projected = false;
                break;
            }
            projectedPoints[index] = { screen.x, screen.y };
        }
    } __except (
        GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER
            : EXCEPTION_CONTINUE_SEARCH) {
        projected = false;
    }
    return projected;
}

__declspec(noinline) bool RenderEntityGuarded(
    PedOutlineRenderer::RenderEntityFn renderEntity,
    CPed* ped) {
    if (!renderEntity || !ped) {
        return false;
    }

    bool rendered = false;
    __try {
        renderEntity(ped);
        rendered = true;
    } __except (
        GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER
            : EXCEPTION_CONTINUE_SEARCH) {
    }
    return rendered;
}

bool ReadCallTarget(std::uintptr_t callAddress, PedOutlineRenderer::RenderEntityFn& target) {
    const auto* instruction = reinterpret_cast<const std::uint8_t*>(callAddress);
    if (!instruction || instruction[0] != 0xE8) {
        return false;
    }

    std::int32_t relative = 0;
    std::memcpy(&relative, instruction + 1, sizeof(relative));
    target = reinterpret_cast<PedOutlineRenderer::RenderEntityFn>(callAddress + 5 + relative);
    return target != nullptr;
}

bool WriteCallTarget(std::uintptr_t callAddress, const void* target) {
    const auto* instruction = reinterpret_cast<const std::uint8_t*>(callAddress);
    if (!instruction || instruction[0] != 0xE8 || !target) {
        return false;
    }

    const std::intptr_t delta = reinterpret_cast<std::intptr_t>(target)
        - static_cast<std::intptr_t>(callAddress + 5);
    if (delta < std::numeric_limits<std::int32_t>::min() || delta > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }

    DWORD oldProtection = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(callAddress), 5, PAGE_EXECUTE_READWRITE, &oldProtection)) {
        return false;
    }

    const std::int32_t relative = static_cast<std::int32_t>(delta);
    std::memcpy(reinterpret_cast<void*>(callAddress + 1), &relative, sizeof(relative));
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(callAddress), 5);

    DWORD ignoredProtection = 0;
    const bool restored = VirtualProtect(
        reinterpret_cast<void*>(callAddress),
        5,
        oldProtection,
        &ignoredProtection)
        != FALSE;
    return restored;
}

} // namespace

PedOutlineRenderer* PedOutlineRenderer::self_ = nullptr;

PedOutlineRenderer::~PedOutlineRenderer() {
    Shutdown();
}

void PedOutlineRenderer::SetTarget(CPed* ped) {
    CPed* next = disabled_ ? nullptr : ped;
    CPed* previous = targetPed_.exchange(next, std::memory_order_acq_rel);
    if (previous == next) {
        return;
    }

    maskValid_ = false;
    capturedPed_ = nullptr;
    lastCaptureAtMs_ = 0;
    cursorSampleValid_ = false;
    cursorSamplePed_ = nullptr;
    rejectedPed_ = nullptr;
    rejectedClump_ = nullptr;
    if (!next) {
        LogPerformanceIfDue(true);
    }
}

void PedOutlineRenderer::Deactivate() {
    const std::uint64_t retiringSession = cursorQuerySession_.Current();
    cursorQuerySession_.Retire();
    for (const CursorHitQuerySlot& slot : cursorHitQueries_) {
        if (slot.pending && slot.session == retiringSession) {
            ++perfCursorQueryRetiredCount_;
        }
    }
    SetTarget(nullptr);
    LogPerformanceIfDue(true);
    perfWindowStartedAtMs_ = 0;
    cursorSampleValid_ = false;
    cursorSamplePed_ = nullptr;
    RestoreRenderHooks();
}

bool PedOutlineRenderer::Render(
    IDirect3DDevice9* device,
    float cursorX,
    float cursorY,
    CursorHitStatus& cursorHit) {
    cursorHit = CursorHitStatus::Pending;
    if (disabled_ || !targetPed_.load(std::memory_order_acquire) || !device) {
        return !disabled_;
    }

    if (!InstallRenderHooks() || !EnsureResources(device)) {
        return false;
    }

    CPed* target = targetPed_.load(std::memory_order_acquire);
    if (!std::isfinite(cursorX)
        || !std::isfinite(cursorY)
        || cursorX < 0.0f
        || cursorY < 0.0f
        || cursorX >= static_cast<float>(targetDesc_.Width)
        || cursorY >= static_cast<float>(targetDesc_.Height)) {
        cursorHit = CursorHitStatus::Miss;
        return true;
    }

    const int cursorPixelX = static_cast<int>(std::floor(cursorX));
    const int cursorPixelY = static_cast<int>(std::floor(cursorY));
    const bool cursorPixelStable = cursorPixelStability_.Observe(
        cursorPixelX,
        cursorPixelY);
    if (!PollCursorHitQuery(target, cursorPixelX, cursorPixelY, cursorHit)) {
        return false;
    }

    if (rejectedPed_ == target && rejectedClump_ == target->m_pRwClump) {
        cursorHit = CursorHitStatus::Miss;
        return true;
    }
    if (!maskValid_ || !capturedPed_ || capturedPed_ != target) {
        return true;
    }

    if (!outlineperf::ContainsPixel(capturedRoi_.capture, cursorPixelX, cursorPixelY)) {
        cursorHit = CursorHitStatus::Miss;
        return true;
    }

    const bool sampleMatches = cursorSampleValid_
        && cursorSamplePed_ == target
        && cursorSampleX_ == cursorPixelX
        && cursorSampleY_ == cursorPixelY;
    if (sampleMatches) {
        cursorHit = cursorSampleHit_ ? CursorHitStatus::Hit : CursorHitStatus::Miss;
    }

    const bool queryDesired = !sampleMatches
        || (cursorHit == CursorHitStatus::Hit
            && cursorSampleGeneration_ != maskGeneration_);
    bool matchingQueryPending = false;
    bool freeQuerySlotAvailable = false;
    for (const CursorHitQuerySlot& slot : cursorHitQueries_) {
        matchingQueryPending = matchingQueryPending
            || (slot.pending
                && cursorQuerySession_.IsCurrent(slot.session)
                && slot.ped == target
                && slot.x == cursorPixelX
                && slot.y == cursorPixelY);
        freeQuerySlotAvailable = freeQuerySlotAvailable || !slot.pending;
    }

    const bool queryNeeded = queryDesired
        && cursorPixelStable
        && !matchingQueryPending
        && freeQuerySlotAvailable;
    if (queryDesired && !cursorPixelStable) {
        ++perfCursorMotionDeferredCount_;
    } else if (queryDesired && !matchingQueryPending && !freeQuerySlotAvailable) {
        ++perfCursorQueryPoolBusyCount_;
    }
    const bool compositeNeeded = cursorHit == CursorHitStatus::Hit;
    if (queryNeeded || compositeNeeded) {
        FrameContext context{};
        if (!AcquireFrameContext(context)) {
            Disable("outline cursor/composite frame context failed");
            return false;
        }

        const char* renderFailure = nullptr;
        if (queryNeeded && !IssueCursorHitQuery(target, cursorPixelX, cursorPixelY)) {
            renderFailure = "outline cursor hit query failed";
        }
        if (!renderFailure && compositeNeeded && !CompositePendingOutline()) {
            renderFailure = "outline composite failed";
        }
        RestoreFrameContext(context);
        if (renderFailure) {
            Disable(renderFailure);
            return false;
        }
    }

    if (compositeNeeded) {
        ++perfCompositeCount_;
        perfCompositePixels_ += outlineperf::Area(capturedRoi_.composite);
    }
    LogPerformanceIfDue(false);

    if (compositeNeeded && !activeLogged_) {
        activeLogged_ = true;
        debuglog::WriteInfo(
            "[target-selector][outline] active ped=%p color=white depth=visible-only "
            "hit=pixel-mask-async thickness=%.0f",
            target,
            kOutlineThickness);
    }
    return true;
}

bool PedOutlineRenderer::HasPendingCursorQueries() const {
    return std::any_of(
        cursorHitQueries_.begin(),
        cursorHitQueries_.end(),
        [](const CursorHitQuerySlot& slot) { return slot.pending; });
}

bool PedOutlineRenderer::PollPendingCursorQueries() {
    CursorHitStatus ignored = CursorHitStatus::Pending;
    const bool ok = PollCursorHitQuery(nullptr, 0, 0, ignored);
    LogPerformanceIfDue(false);
    return ok;
}

void PedOutlineRenderer::OnDeviceLost() {
    maskValid_ = false;
    capturedPed_ = nullptr;
    device_ = nullptr;
    ReleaseResources();
    debuglog::WriteInfo("[target-selector][outline] D3D device lost: resources released");
}

void PedOutlineRenderer::OnDeviceReset() {
    readyLogged_ = false;
    activeLogged_ = false;
    debuglog::WriteInfo("[target-selector][outline] D3D device reset: resources will be recreated lazily");
}

void PedOutlineRenderer::Shutdown() {
    LogPerformanceIfDue(true);
    targetPed_.store(nullptr, std::memory_order_release);
    maskValid_ = false;
    capturedPed_ = nullptr;
    RestoreRenderHooks();
    ReleaseResources();
    device_ = nullptr;
}

bool PedOutlineRenderer::IsDisabled() const {
    return disabled_;
}

bool PedOutlineRenderer::ConsumeFailure(std::string& reason) {
    if (!failurePending_) {
        return false;
    }
    failurePending_ = false;
    reason = failureReason_;
    return true;
}

void __fastcall PedOutlineRenderer::PedRenderCall1(CPed* ped, void*) {
    PedOutlineRenderer* self = self_;
    if (!self) {
        return;
    }
    self->CaptureIfTarget(ped);
    if (self->previousRender1_) {
        self->previousRender1_(ped);
    }
}

void __fastcall PedOutlineRenderer::PedRenderCall2(CPed* ped, void*) {
    PedOutlineRenderer* self = self_;
    if (!self) {
        return;
    }
    self->CaptureIfTarget(ped);
    if (self->previousRender2_) {
        self->previousRender2_(ped);
    }
}

bool PedOutlineRenderer::InstallRenderHooks() {
    if (renderCall1Active_ && renderCall2Active_) {
        return true;
    }

    renderCall1_ = RebasedAddress(kPedRenderCall1Address);
    renderCall2_ = RebasedAddress(kPedRenderCall2Address);
    self_ = this;
    if (!renderCall1Active_) {
        if (!ReadCallTarget(renderCall1_, previousRender1_)) {
            Disable("expected CALL instruction is missing at ped render callsite 0x005E77FC");
            return false;
        }
        if (!WriteCallTarget(renderCall1_, reinterpret_cast<const void*>(&PedRenderCall1))) {
            previousRender1_ = nullptr;
            Disable("failed to patch ped render callsite 0x005E77FC");
            return false;
        }
        renderCall1Active_ = true;
    }
    if (!renderCall2Active_) {
        if (!ReadCallTarget(renderCall2_, previousRender2_)) {
            Disable("expected CALL instruction is missing at ped render callsite 0x005E780A");
            return false;
        }
        if (!WriteCallTarget(renderCall2_, reinterpret_cast<const void*>(&PedRenderCall2))) {
            previousRender2_ = nullptr;
            Disable("failed to patch ped render callsite 0x005E780A");
            return false;
        }
        renderCall2Active_ = true;
    }

    debuglog::WriteInfo(
        "[target-selector][outline] chained callsite hooks installed calls=(0x%08X,0x%08X) previous=(%p,%p)",
        static_cast<unsigned int>(renderCall1_),
        static_cast<unsigned int>(renderCall2_),
        previousRender1_,
        previousRender2_);
    return true;
}

void PedOutlineRenderer::RestoreRenderHooks() {
    if (!renderCall1Active_ && !renderCall2Active_) {
        if (self_ == this) {
            self_ = nullptr;
        }
        return;
    }

    const auto restoreCallsite = [&](std::uintptr_t callsite,
                                     RenderEntityFn wrapper,
                                     RenderEntityFn& previous,
                                     bool& active) {
        if (!active) {
            previous = nullptr;
            return;
        }

        RenderEntityFn current = nullptr;
        if (!ReadCallTarget(callsite, current)) {
            debuglog::WriteError(
                "[target-selector][outline] failed to read callsite 0x%08X during restore",
                static_cast<unsigned int>(callsite));
            return;
        }
        if (current != wrapper) {
            debuglog::WriteInfo(
                "[target-selector][outline] callsite 0x%08X changed by another module; "
                "wrapper retained for chain safety",
                static_cast<unsigned int>(callsite));
            return;
        }
        if (!WriteCallTarget(callsite, reinterpret_cast<const void*>(previous))) {
            debuglog::WriteError(
                "[target-selector][outline] failed to restore callsite 0x%08X",
                static_cast<unsigned int>(callsite));
            return;
        }
        active = false;
        previous = nullptr;
    };

    restoreCallsite(
        renderCall1_,
        reinterpret_cast<RenderEntityFn>(&PedRenderCall1),
        previousRender1_,
        renderCall1Active_);
    restoreCallsite(
        renderCall2_,
        reinterpret_cast<RenderEntityFn>(&PedRenderCall2),
        previousRender2_,
        renderCall2Active_);
    if (!renderCall1Active_ && !renderCall2Active_ && self_ == this) {
        self_ = nullptr;
    }
}

bool PedOutlineRenderer::EnsureResources(IDirect3DDevice9* device) {
    const bool cursorQueriesReady = std::all_of(
        cursorHitQueries_.begin(),
        cursorHitQueries_.end(),
        [](const CursorHitQuerySlot& slot) { return slot.query != nullptr; });
    if (device_ == device
        && visibleTexture_
        && visibleSurface_
        && outlineShader_
        && cursorHitShader_
        && cursorQueriesReady
        && stateBlock_
        && maskStateBlock_
        && compositeStateBlock_
        && cursorHitStateBlock_) {
        return true;
    }

    IDirect3DSurface9* renderTarget = nullptr;
    if (FAILED(device->GetRenderTarget(0, &renderTarget)) || !renderTarget) {
        Disable("GetRenderTarget failed");
        return false;
    }

    D3DSURFACE_DESC desc{};
    const HRESULT descResult = renderTarget->GetDesc(&desc);
    renderTarget->Release();
    if (FAILED(descResult) || desc.Width == 0 || desc.Height == 0) {
        Disable("GetDesc for primary render target failed");
        return false;
    }

    ReleaseResources();
    device_ = device;
    targetDesc_ = desc;

    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE
        && FAILED(device_->CreateRenderTarget(
            desc.Width,
            desc.Height,
            D3DFMT_A8R8G8B8,
            desc.MultiSampleType,
            desc.MultiSampleQuality,
            FALSE,
            &multisampleCaptureSurface_,
            nullptr))) {
        Disable("CreateRenderTarget for MSAA mask failed");
        return false;
    }

    if (FAILED(device_->CreateTexture(
            desc.Width,
            desc.Height,
            1,
            D3DUSAGE_RENDERTARGET,
            D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT,
            &visibleTexture_,
            nullptr))
        || !visibleTexture_
        || FAILED(visibleTexture_->GetSurfaceLevel(0, &visibleSurface_))
        || !visibleSurface_
        || FAILED(device_->CreatePixelShader(kOutlineShader, &outlineShader_))
        || !outlineShader_
        || FAILED(device_->CreatePixelShader(kCursorHitShader, &cursorHitShader_))
        || !cursorHitShader_
        || FAILED(device_->CreateStateBlock(D3DSBT_ALL, &stateBlock_))
        || !stateBlock_) {
        Disable("D3D outline resource creation failed");
        return false;
    }
    for (CursorHitQuerySlot& slot : cursorHitQueries_) {
        if (FAILED(device_->CreateQuery(D3DQUERYTYPE_OCCLUSION, &slot.query))
            || !slot.query) {
            Disable("D3D outline cursor query pool creation failed");
            return false;
        }
    }

    const auto recordMaskState = [&]() {
        if (FAILED(device_->BeginStateBlock())) {
            return false;
        }
        device_->SetRenderState(D3DRS_ZENABLE, TRUE);
        device_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        device_->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        device_->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        device_->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
        device_->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA);
        return SUCCEEDED(device_->EndStateBlock(&maskStateBlock_)) && maskStateBlock_;
    };
    const auto recordCompositeState = [&]() {
        if (FAILED(device_->BeginStateBlock())) {
            return false;
        }
        device_->SetVertexShader(nullptr);
        device_->SetPixelShader(outlineShader_);
        device_->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
        device_->SetTexture(0, visibleTexture_);
        device_->SetRenderState(D3DRS_ZENABLE, FALSE);
        device_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        device_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        device_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device_->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
        device_->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device_->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        device_->SetRenderState(D3DRS_LIGHTING, FALSE);
        device_->SetRenderState(D3DRS_FOGENABLE, FALSE);
        device_->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
        device_->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);
        device_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        device_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        device_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        device_->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        device_->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        device_->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
        return SUCCEEDED(device_->EndStateBlock(&compositeStateBlock_)) && compositeStateBlock_;
    };
    const auto recordCursorHitState = [&]() {
        if (FAILED(device_->BeginStateBlock())) {
            return false;
        }
        device_->SetVertexShader(nullptr);
        device_->SetPixelShader(cursorHitShader_);
        device_->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
        device_->SetTexture(0, visibleTexture_);
        device_->SetRenderState(D3DRS_ZENABLE, FALSE);
        device_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        device_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        device_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device_->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        device_->SetRenderState(D3DRS_LIGHTING, FALSE);
        device_->SetRenderState(D3DRS_FOGENABLE, FALSE);
        device_->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
        device_->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
        device_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        device_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        device_->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        device_->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        device_->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
        device_->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
        return SUCCEEDED(device_->EndStateBlock(&cursorHitStateBlock_)) && cursorHitStateBlock_;
    };
    if (!recordMaskState() || !recordCompositeState() || !recordCursorHitState()) {
        Disable("D3D outline state block creation failed");
        return false;
    }

    if (!readyLogged_) {
        readyLogged_ = true;
        debuglog::WriteInfo(
            "[target-selector][outline] resources ready %ux%u msaa=%d quality=%lu "
            "shader=visible-only-ps_2_0 samples=9 cursorHit=async-occlusion-query "
            "querySlots=%zu stableFrames=1",
            desc.Width,
            desc.Height,
            static_cast<int>(desc.MultiSampleType),
            static_cast<unsigned long>(desc.MultiSampleQuality),
            cursorHitQueries_.size());
    }
    return true;
}

void PedOutlineRenderer::ReleaseResources() {
    ReleaseCom(cursorHitStateBlock_);
    ReleaseCom(compositeStateBlock_);
    ReleaseCom(maskStateBlock_);
    ReleaseCom(stateBlock_);
    for (CursorHitQuerySlot& slot : cursorHitQueries_) {
        ReleaseCom(slot.query);
    }
    ReleaseCom(cursorHitShader_);
    ReleaseCom(outlineShader_);
    ReleaseCom(multisampleCaptureSurface_);
    ReleaseCom(visibleSurface_);
    ReleaseCom(visibleTexture_);
    targetDesc_ = {};
    capturedRoi_ = {};
    maskValid_ = false;
    capturedPed_ = nullptr;
    partialResolveSupported_ = -1;
    lastCaptureAtMs_ = 0;
    maskGeneration_ = 0;
    ResetCursorHitState();
}

bool PedOutlineRenderer::AcquireFrameContext(FrameContext& context) {
    if (!device_ || !stateBlock_) {
        return false;
    }

    const auto fail = [&]() {
        ReleaseCom(context.streamSource);
        ReleaseCom(context.depthStencil);
        ReleaseCom(context.renderTarget);
        return false;
    };

    const auto rwFlush = reinterpret_cast<RwFlushStateCacheFn>(RebasedAddress(kRwFlushStateCacheAddress));
    rwFlush();
    if (FAILED(stateBlock_->Capture())
        || FAILED(device_->GetRenderTarget(0, &context.renderTarget))
        || !context.renderTarget
        || FAILED(device_->GetDepthStencilSurface(&context.depthStencil))
        || !context.depthStencil
        || FAILED(device_->GetViewport(&context.viewport))
        || FAILED(device_->GetStreamSource(0, &context.streamSource, &context.streamOffset, &context.streamStride))) {
        return fail();
    }
    return true;
}

void PedOutlineRenderer::RestoreFrameContext(FrameContext& context) {
    if (device_) {
        if (stateBlock_) {
            stateBlock_->Apply();
        }
        if (context.renderTarget) {
            device_->SetRenderTarget(0, context.renderTarget);
        }
        if (context.depthStencil) {
            device_->SetDepthStencilSurface(context.depthStencil);
        }
        device_->SetViewport(&context.viewport);
        device_->SetStreamSource(0, context.streamSource, context.streamOffset, context.streamStride);
    }

    ReleaseCom(context.streamSource);
    ReleaseCom(context.depthStencil);
    ReleaseCom(context.renderTarget);
}

PedOutlineRenderer::RoiResolveStatus PedOutlineRenderer::ResolveScreenRoi(
    CPed* ped,
    outlineperf::ScreenRoi& roi) {
    roi = {};
    if (!ped || !ped->m_pRwClump || targetDesc_.Width == 0 || targetDesc_.Height == 0) {
        return RoiResolveStatus::InvalidInput;
    }

    RoiResolveStatus primaryFailure = RoiResolveStatus::BoneProjectionFailed;
    std::array<outlineperf::ProjectedPoint, kOutlineBoundsBones.size()> projectedBones{};
    if (TryProjectPedBones(ped, projectedBones.data(), projectedBones.size())) {
        double minX = projectedBones[0].x;
        double minY = projectedBones[0].y;
        double maxX = projectedBones[0].x;
        double maxY = projectedBones[0].y;
        for (std::size_t index = 1; index < projectedBones.size(); ++index) {
            minX = std::min(minX, static_cast<double>(projectedBones[index].x));
            minY = std::min(minY, static_cast<double>(projectedBones[index].y));
            maxX = std::max(maxX, static_cast<double>(projectedBones[index].x));
            maxY = std::max(maxY, static_cast<double>(projectedBones[index].y));
        }
        primaryFailure = RoiResolveStatus::InvalidProjectedBounds;
        if (outlineperf::TryBuildExpandedBoneRoi(
                minX,
                minY,
                maxX,
                maxY,
                targetDesc_.Width,
                targetDesc_.Height,
                static_cast<int>(kOutlineThickness),
                roi)) {
            return RoiResolveStatus::Success;
        }
    }

    const CVector& position = ped->GetPosition();
    if (!std::isfinite(position.x)
        || !std::isfinite(position.y)
        || !std::isfinite(position.z)) {
        return primaryFailure;
    }

    std::array<outlineperf::ProjectedPoint, 8> projectedCorners{};
    std::size_t cornerIndex = 0;
    for (float xOffset : { -kFallbackPedHalfWidth, kFallbackPedHalfWidth }) {
        for (float yOffset : { -kFallbackPedHalfWidth, kFallbackPedHalfWidth }) {
            for (float zOffset : { kFallbackPedBottomOffset, kFallbackPedTopOffset }) {
                const RwV3d world{
                    position.x + xOffset,
                    position.y + yOffset,
                    position.z + zOffset,
                };
                RwV3d screen{};
                float screenScaleX = 0.0f;
                float screenScaleY = 0.0f;
                if (!CSprite::CalcScreenCoors(
                        world,
                        &screen,
                        &screenScaleX,
                        &screenScaleY,
                        false,
                        false)
                    || !std::isfinite(screen.x)
                    || !std::isfinite(screen.y)
                    || !std::isfinite(screen.z)
                    || screen.z <= 0.0f) {
                    return primaryFailure;
                }
                projectedCorners[cornerIndex++] = { screen.x, screen.y };
            }
        }
    }

    if (outlineperf::TryBuildScreenRoi(
            projectedCorners.data(),
            projectedCorners.size(),
            targetDesc_.Width,
            targetDesc_.Height,
            static_cast<int>(kOutlineThickness),
            roi)) {
        return RoiResolveStatus::SuccessFixedBounds;
    }
    return primaryFailure;
}

bool PedOutlineRenderer::CaptureVisibleMask(
    CPed* ped,
    const outlineperf::ScreenRoi& roi,
    bool& renderRejected) {
    renderRejected = false;
    FrameContext context{};
    if (!AcquireFrameContext(context)) {
        return false;
    }

    const RECT captureRect = {
        roi.capture.left,
        roi.capture.top,
        roi.capture.right,
        roi.capture.bottom,
    };
    const D3DRECT captureClearRect = {
        roi.capture.left,
        roi.capture.top,
        roi.capture.right,
        roi.capture.bottom,
    };
    IDirect3DSurface9* captureSurface = multisampleCaptureSurface_ ? multisampleCaptureSurface_ : visibleSurface_;
    bool ok = captureSurface
        && SUCCEEDED(device_->SetRenderTarget(0, captureSurface))
        && SUCCEEDED(device_->SetDepthStencilSurface(context.depthStencil))
        && SUCCEEDED(device_->SetViewport(&context.viewport))
        && SUCCEEDED(maskStateBlock_->Apply())
        && SUCCEEDED(device_->SetScissorRect(&captureRect));
    if (ok) {
        ok = SUCCEEDED(device_->Clear(1, &captureClearRect, D3DCLEAR_TARGET, 0x00000000, 1.0f, 0));
    }

    if (ok) {
        const auto renderEntity = reinterpret_cast<RenderEntityFn>(RebasedAddress(kRenderEntityAddress));
        ok = RenderEntityGuarded(renderEntity, ped);
        if (!ok) {
            renderRejected = true;
            debuglog::WriteError(
                "[target-selector][outline] protected extra render failed ped=%p clump=%p",
                ped,
                ped->m_pRwClump);
        }
    }

    if (ok) {
        device_->SetRenderTarget(0, context.renderTarget);
        device_->SetDepthStencilSurface(context.depthStencil);
        device_->SetViewport(&context.viewport);

        const auto rwFlush = reinterpret_cast<RwFlushStateCacheFn>(RebasedAddress(kRwFlushStateCacheAddress));
        const auto rwGet = reinterpret_cast<RwGetRenderStateFn>(RebasedAddress(kRwGetRenderStateAddress));
        rwFlush();
        for (D3DRENDERSTATETYPE state : kCaptureRenderStates) {
            DWORD cachedValue = 0;
            rwGet(static_cast<DWORD>(state), &cachedValue);
            device_->SetRenderState(state, cachedValue);
        }

        ReleaseCom(context.streamSource);
        ok = SUCCEEDED(device_->GetStreamSource(0, &context.streamSource, &context.streamOffset, &context.streamStride))
            && SUCCEEDED(stateBlock_->Capture());
    }

    if (ok) {
        ok = ResolveVisibleMask(roi);
    }

    RestoreFrameContext(context);
    return ok;
}

bool PedOutlineRenderer::ResolveVisibleMask(const outlineperf::ScreenRoi& roi) {
    if (!multisampleCaptureSurface_) {
        return true;
    }

    const RECT captureRect = {
        roi.capture.left,
        roi.capture.top,
        roi.capture.right,
        roi.capture.bottom,
    };
    if (partialResolveSupported_ != 0) {
        const HRESULT partialResult = device_->StretchRect(
            multisampleCaptureSurface_,
            &captureRect,
            visibleSurface_,
            &captureRect,
            D3DTEXF_NONE);
        if (SUCCEEDED(partialResult)) {
            if (partialResolveSupported_ < 0) {
                debuglog::WriteInfo("[target-selector][outline] partial MSAA resolve supported");
            }
            partialResolveSupported_ = 1;
            ++perfPartialResolveCount_;
            perfResolvedPixels_ += outlineperf::Area(roi.capture);
            return true;
        }
        if (partialResult == D3DERR_INVALIDCALL) {
            if (partialResolveSupported_ < 0) {
                debuglog::WriteInfo(
                    "[target-selector][outline] partial MSAA resolve unsupported; full resolve fallback active");
            }
            partialResolveSupported_ = 0;
        }
    }

    const HRESULT fullResult = device_->StretchRect(
        multisampleCaptureSurface_,
        nullptr,
        visibleSurface_,
        nullptr,
        D3DTEXF_NONE);
    if (SUCCEEDED(fullResult)) {
        ++perfFullResolveCount_;
        perfResolvedPixels_ += static_cast<std::uint64_t>(targetDesc_.Width) * targetDesc_.Height;
        return true;
    }
    return false;
}

bool PedOutlineRenderer::CompositePendingOutline() {
    const float constants[8] = {
        kOutlineThickness / static_cast<float>(targetDesc_.Width),
        kOutlineThickness / static_cast<float>(targetDesc_.Height),
        kOutlineDiagonal / static_cast<float>(targetDesc_.Width),
        kOutlineDiagonal / static_cast<float>(targetDesc_.Height),
        1.0f,
        1.0f,
        1.0f,
        1.0f,
    };
    const float width = static_cast<float>(targetDesc_.Width);
    const float height = static_cast<float>(targetDesc_.Height);
    const OutlineVertex quad[4] = {
        { -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f },
        { width - 0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f },
        { -0.5f, height - 0.5f, 0.0f, 1.0f, 0.0f, 1.0f },
        { width - 0.5f, height - 0.5f, 0.0f, 1.0f, 1.0f, 1.0f },
    };
    const RECT compositeRect = {
        capturedRoi_.composite.left,
        capturedRoi_.composite.top,
        capturedRoi_.composite.right,
        capturedRoi_.composite.bottom,
    };

    bool ok = SUCCEEDED(compositeStateBlock_->Apply())
        && SUCCEEDED(device_->SetPixelShaderConstantF(0, constants, 2))
        && SUCCEEDED(device_->SetScissorRect(&compositeRect));
    if (ok) {
        ok = SUCCEEDED(device_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(OutlineVertex)));
    }
    return ok;
}

bool PedOutlineRenderer::PollCursorHitQuery(
    CPed* target,
    int cursorX,
    int cursorY,
    CursorHitStatus& cursorHit) {
    const std::uint64_t now = GetTickCount64();
    for (CursorHitQuerySlot& slot : cursorHitQueries_) {
        if (!slot.pending || !slot.query) {
            continue;
        }

        DWORD visiblePixels = 0;
        const HRESULT result = slot.query->GetData(&visiblePixels, sizeof(visiblePixels), 0);
        if (result == S_OK) {
            slot.pending = false;
            const bool currentSession = cursorQuerySession_.IsCurrent(slot.session);
            if (currentSession) {
                ++perfCursorQueryResolvedCount_;
                const bool hit = visiblePixels != 0;
                if (hit) {
                    ++perfCursorHitCount_;
                } else {
                    ++perfCursorMissCount_;
                }
                const std::uint64_t latencyMs = now >= slot.issuedAtMs
                    ? now - slot.issuedAtMs
                    : 0;
                ++perfCursorQueryLatencySampleCount_;
                perfCursorQueryLatencyTotalMs_ += latencyMs;
                perfCursorQueryLatencyMaxMs_ = std::max(
                    perfCursorQueryLatencyMaxMs_,
                    latencyMs);

                const bool currentPixel = slot.ped == target
                    && slot.x == cursorX
                    && slot.y == cursorY;
                if (currentPixel
                    && (!cursorSampleValid_
                        || cursorSamplePed_ != target
                        || cursorSampleX_ != cursorX
                        || cursorSampleY_ != cursorY
                        || slot.generation >= cursorSampleGeneration_)) {
                    cursorSampleValid_ = true;
                    cursorSampleHit_ = hit;
                    cursorSamplePed_ = slot.ped;
                    cursorSampleX_ = slot.x;
                    cursorSampleY_ = slot.y;
                    cursorSampleGeneration_ = slot.generation;
                } else if (!currentPixel) {
                    ++perfCursorStaleResultCount_;
                }
            }

            slot.ped = nullptr;
            slot.x = 0;
            slot.y = 0;
            slot.generation = 0;
            slot.session = 0;
            slot.issuedAtMs = 0;
        } else if (result != S_FALSE) {
            Disable("outline cursor hit query polling failed");
            return false;
        }
    }

    if (cursorSampleValid_
        && cursorSamplePed_ == target
        && cursorSampleX_ == cursorX
        && cursorSampleY_ == cursorY) {
        cursorHit = cursorSampleHit_ ? CursorHitStatus::Hit : CursorHitStatus::Miss;
    }
    return true;
}

bool PedOutlineRenderer::IssueCursorHitQuery(
    CPed* target,
    int cursorX,
    int cursorY) {
    CursorHitQuerySlot* freeSlot = nullptr;
    for (CursorHitQuerySlot& slot : cursorHitQueries_) {
        if (!slot.pending) {
            freeSlot = &slot;
            break;
        }
    }
    if (!freeSlot
        || !freeSlot->query
        || !cursorHitStateBlock_
        || targetDesc_.Width == 0
        || targetDesc_.Height == 0) {
        return false;
    }

    const float left = static_cast<float>(cursorX) - 0.5f;
    const float top = static_cast<float>(cursorY) - 0.5f;
    const float right = left + 1.0f;
    const float bottom = top + 1.0f;
    const float u = (static_cast<float>(cursorX) + 0.5f)
        / static_cast<float>(targetDesc_.Width);
    const float v = (static_cast<float>(cursorY) + 0.5f)
        / static_cast<float>(targetDesc_.Height);
    const OutlineVertex quad[4] = {
        { left, top, 0.0f, 1.0f, u, v },
        { right, top, 0.0f, 1.0f, u, v },
        { left, bottom, 0.0f, 1.0f, u, v },
        { right, bottom, 0.0f, 1.0f, u, v },
    };
    const RECT cursorRect = { cursorX, cursorY, cursorX + 1, cursorY + 1 };

    if (FAILED(cursorHitStateBlock_->Apply())
        || FAILED(device_->SetScissorRect(&cursorRect))
        || FAILED(freeSlot->query->Issue(D3DISSUE_BEGIN))) {
        return false;
    }

    const HRESULT drawResult =
        device_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(OutlineVertex));
    const HRESULT endResult = freeSlot->query->Issue(D3DISSUE_END);
    if (FAILED(drawResult) || FAILED(endResult)) {
        return false;
    }

    freeSlot->pending = true;
    freeSlot->ped = target;
    freeSlot->x = cursorX;
    freeSlot->y = cursorY;
    freeSlot->generation = maskGeneration_;
    freeSlot->session = cursorQuerySession_.Current();
    freeSlot->issuedAtMs = GetTickCount64();
    ++perfCursorQueryIssuedCount_;
    return true;
}

void PedOutlineRenderer::ResetCursorHitState() {
    for (CursorHitQuerySlot& slot : cursorHitQueries_) {
        slot.pending = false;
        slot.ped = nullptr;
        slot.x = 0;
        slot.y = 0;
        slot.generation = 0;
        slot.session = 0;
        slot.issuedAtMs = 0;
    }
    cursorSampleValid_ = false;
    cursorSampleHit_ = false;
    cursorSamplePed_ = nullptr;
    cursorSampleX_ = 0;
    cursorSampleY_ = 0;
    cursorSampleGeneration_ = 0;
    cursorPixelStability_.Reset();
}

void PedOutlineRenderer::CaptureIfTarget(CPed* ped) {
    CPed* target = targetPed_.load(std::memory_order_acquire);
    if (disabled_ || rendering_ || !ped || ped != target
        || !device_ || !visibleSurface_ || !stateBlock_) {
        return;
    }

    const std::uint64_t now = GetTickCount64();
    const bool sameTarget = maskValid_ && capturedPed_ == ped;
    if (!outlineperf::ShouldRefreshMask(
            now,
            lastCaptureAtMs_,
            maskValid_,
            sameTarget,
            outlineperf::kMaskRefreshIntervalMs)) {
        ++perfCaptureReuseCount_;
        return;
    }

    void* clump = ped->m_pRwClump;
    if (rejectedPed_ == ped && rejectedClump_ == clump) {
        ++perfUnsafePedSkipCount_;
        return;
    }

    outlineperf::ScreenRoi roi{};
    const RoiResolveStatus roiStatus = ResolveScreenRoi(ped, roi);
    if (roiStatus == RoiResolveStatus::Success) {
        ++perfBoneBoundsCount_;
    } else if (roiStatus == RoiResolveStatus::SuccessFixedBounds) {
        ++perfFixedBoundsCount_;
    } else {
        ++perfRoiSkippedCount_;
        switch (roiStatus) {
        case RoiResolveStatus::InvalidInput:
            ++perfRoiInvalidInputCount_;
            break;
        case RoiResolveStatus::BoneProjectionFailed:
            ++perfRoiBoneProjectionFailedCount_;
            break;
        case RoiResolveStatus::InvalidProjectedBounds:
            ++perfRoiInvalidProjectedBoundsCount_;
            break;
        case RoiResolveStatus::Success:
        case RoiResolveStatus::SuccessFixedBounds:
            break;
        }
        maskValid_ = false;
        capturedPed_ = nullptr;
        return;
    }

    maskValid_ = false;
    capturedPed_ = nullptr;
    rendering_ = true;
    bool renderRejected = false;
    const bool captured = CaptureVisibleMask(ped, roi, renderRejected);
    rendering_ = false;
    if (!captured) {
        if (renderRejected) {
            rejectedPed_ = ped;
            rejectedClump_ = clump;
            ++perfUnsafePedSkipCount_;
            return;
        }
        Disable("visible ped mask capture failed");
        return;
    }

    rejectedPed_ = nullptr;
    rejectedClump_ = nullptr;
    capturedPed_ = ped;
    capturedRoi_ = roi;
    maskValid_ = true;
    ++maskGeneration_;
    lastCaptureAtMs_ = now;
    ++perfCaptureCount_;
    if (perfWindowStartedAtMs_ == 0) {
        perfWindowStartedAtMs_ = now;
    }
}

void PedOutlineRenderer::LogPerformanceIfDue(bool force) {
    const std::uint64_t eventCount = perfCaptureCount_
        + perfCaptureReuseCount_
        + perfCompositeCount_
        + perfRoiSkippedCount_
        + perfUnsafePedSkipCount_
        + perfCursorQueryIssuedCount_
        + perfCursorQueryResolvedCount_
        + perfCursorQueryRetiredCount_
        + perfCursorMotionDeferredCount_
        + perfCursorQueryPoolBusyCount_;
    if (eventCount == 0) {
        return;
    }

    const std::uint64_t now = GetTickCount64();
    if (perfWindowStartedAtMs_ == 0 || now < perfWindowStartedAtMs_) {
        perfWindowStartedAtMs_ = now;
    }
    const std::uint64_t elapsedMs = std::max<std::uint64_t>(1, now - perfWindowStartedAtMs_);
    if (!force && elapsedMs < kPerformanceLogIntervalMs) {
        return;
    }

    const std::uint64_t fullPixels = std::max<std::uint64_t>(
        1,
        static_cast<std::uint64_t>(targetDesc_.Width) * targetDesc_.Height);
    const double captureHz = static_cast<double>(perfCaptureCount_) * 1000.0
        / static_cast<double>(elapsedMs);
    const double averageCompositePercent = perfCompositeCount_ == 0
        ? 0.0
        : static_cast<double>(perfCompositePixels_) * 100.0
            / static_cast<double>(perfCompositeCount_ * fullPixels);
    const double averageResolvePercent = perfCaptureCount_ == 0
        ? 0.0
        : static_cast<double>(perfResolvedPixels_) * 100.0
            / static_cast<double>(perfCaptureCount_ * fullPixels);
    const double averageCursorQueryLatencyMs = perfCursorQueryLatencySampleCount_ == 0
        ? 0.0
        : static_cast<double>(perfCursorQueryLatencyTotalMs_)
            / static_cast<double>(perfCursorQueryLatencySampleCount_);

    debuglog::WriteInfo(
        "[target-selector][outline][perf] window=%llums captures=%llu reused=%llu composites=%llu "
        "captureHz=%.1f roiAvg=%.2f%% resolvedAvg=%.2f%% partial=%llu full=%llu "
        "boneBounds=%llu fixedBounds=%llu roiSkipped=%llu unsafePedSkipped=%llu "
        "cursorQuery(issued=%llu resolved=%llu hit=%llu miss=%llu stale=%llu retired=%llu "
        "moveDeferred=%llu poolBusy=%llu latencySamples=%llu latencyAvg=%.1fms latencyMax=%llums) "
        "roiFail(input=%llu bone=%llu bounds=%llu) "
        "partialResolve=%d",
        static_cast<unsigned long long>(elapsedMs),
        static_cast<unsigned long long>(perfCaptureCount_),
        static_cast<unsigned long long>(perfCaptureReuseCount_),
        static_cast<unsigned long long>(perfCompositeCount_),
        captureHz,
        averageCompositePercent,
        averageResolvePercent,
        static_cast<unsigned long long>(perfPartialResolveCount_),
        static_cast<unsigned long long>(perfFullResolveCount_),
        static_cast<unsigned long long>(perfBoneBoundsCount_),
        static_cast<unsigned long long>(perfFixedBoundsCount_),
        static_cast<unsigned long long>(perfRoiSkippedCount_),
        static_cast<unsigned long long>(perfUnsafePedSkipCount_),
        static_cast<unsigned long long>(perfCursorQueryIssuedCount_),
        static_cast<unsigned long long>(perfCursorQueryResolvedCount_),
        static_cast<unsigned long long>(perfCursorHitCount_),
        static_cast<unsigned long long>(perfCursorMissCount_),
        static_cast<unsigned long long>(perfCursorStaleResultCount_),
        static_cast<unsigned long long>(perfCursorQueryRetiredCount_),
        static_cast<unsigned long long>(perfCursorMotionDeferredCount_),
        static_cast<unsigned long long>(perfCursorQueryPoolBusyCount_),
        static_cast<unsigned long long>(perfCursorQueryLatencySampleCount_),
        averageCursorQueryLatencyMs,
        static_cast<unsigned long long>(perfCursorQueryLatencyMaxMs_),
        static_cast<unsigned long long>(perfRoiInvalidInputCount_),
        static_cast<unsigned long long>(perfRoiBoneProjectionFailedCount_),
        static_cast<unsigned long long>(perfRoiInvalidProjectedBoundsCount_),
        partialResolveSupported_);

    perfWindowStartedAtMs_ = now;
    perfCaptureCount_ = 0;
    perfCaptureReuseCount_ = 0;
    perfCompositeCount_ = 0;
    perfPartialResolveCount_ = 0;
    perfFullResolveCount_ = 0;
    perfBoneBoundsCount_ = 0;
    perfFixedBoundsCount_ = 0;
    perfRoiSkippedCount_ = 0;
    perfUnsafePedSkipCount_ = 0;
    perfRoiInvalidInputCount_ = 0;
    perfRoiBoneProjectionFailedCount_ = 0;
    perfRoiInvalidProjectedBoundsCount_ = 0;
    perfCompositePixels_ = 0;
    perfResolvedPixels_ = 0;
    perfCursorQueryIssuedCount_ = 0;
    perfCursorQueryResolvedCount_ = 0;
    perfCursorHitCount_ = 0;
    perfCursorMissCount_ = 0;
    perfCursorStaleResultCount_ = 0;
    perfCursorQueryRetiredCount_ = 0;
    perfCursorMotionDeferredCount_ = 0;
    perfCursorQueryPoolBusyCount_ = 0;
    perfCursorQueryLatencySampleCount_ = 0;
    perfCursorQueryLatencyTotalMs_ = 0;
    perfCursorQueryLatencyMaxMs_ = 0;
}

void PedOutlineRenderer::Disable(std::string reason) {
    if (disabled_) {
        return;
    }

    disabled_ = true;
    failurePending_ = true;
    failureReason_ = std::move(reason);
    targetPed_.store(nullptr, std::memory_order_release);
    maskValid_ = false;
    capturedPed_ = nullptr;
    RestoreRenderHooks();
    ReleaseResources();
    debuglog::WriteError("[target-selector][outline] disabled: %s", failureReason_.c_str());
}
