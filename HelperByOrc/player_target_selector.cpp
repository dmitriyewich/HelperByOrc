#include "player_target_selector.h"

#include "debug_log.h"
#include "notification_manager.h"
#include "player_target_selector_math.h"
#include "samp_api.h"
#include "tags_module.h"
#include "ui_settings.h"

#include <CCamera.h>
#include <CPed.h>
#include <CPlayerPed.h>
#include <CPools.h>
#include <CVehicle.h>
#include <CWorld.h>
#include <RenderWare.h>
#include <common.h>
#include <ePedBones.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace {

constexpr float kSelectionSphereCenterOffset = 1.0f;
constexpr int kMaximumSanePedPoolSize = 4096;
constexpr int kMaximumSaneHAnimNodeCount = 1024;

constexpr std::array<unsigned int, 11> kSelectionPoseBones = {
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

constexpr std::array<targetselectormath::ScreenSegment, 10> kSelectionPoseSegments = {{
    { 0, 1 }, // head -> upper torso
    { 1, 2 }, // upper torso -> pelvis
    { 1, 3 }, // upper torso -> right shoulder
    { 3, 4 }, // right shoulder -> right hand
    { 1, 5 }, // upper torso -> left shoulder
    { 5, 6 }, // left shoulder -> left hand
    { 2, 7 }, // pelvis -> right knee
    { 7, 8 }, // right knee -> right foot
    { 2, 9 }, // pelvis -> left knee
    { 9, 10 }, // left knee -> left foot
}};

struct SelectionCamera {
    const RwMatrix* matrix = nullptr;
    const RwV2d* viewWindow = nullptr;
    targetselectormath::ScreenProjection projection{};
    float width = 0.0f;
    float height = 0.0f;
};

bool IsMouseMessage(UINT message) {
    return message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
}

bool TryResolveSelectionCamera(SelectionCamera& selectionCamera) {
    selectionCamera = {};
    if (RsGlobal.maximumWidth <= 0 || RsGlobal.maximumHeight <= 0) {
        return false;
    }

    RwCamera* camera = TheCamera.m_pRwCamera;
    RwFrame* frame = camera ? RwCameraGetFrame(camera) : nullptr;
    const RwV2d* viewWindow = camera ? RwCameraGetViewWindow(camera) : nullptr;
    const RwMatrix* matrix = frame ? RwFrameGetMatrix(frame) : nullptr;
    if (!matrix || !viewWindow) {
        return false;
    }

    selectionCamera.matrix = matrix;
    selectionCamera.viewWindow = viewWindow;
    selectionCamera.width = static_cast<float>(RsGlobal.maximumWidth);
    selectionCamera.height = static_cast<float>(RsGlobal.maximumHeight);
    return targetselectormath::TryBuildScreenProjection(
        *matrix,
        selectionCamera.width,
        selectionCamera.height,
        viewWindow->x,
        viewWindow->y,
        selectionCamera.projection);
}

bool ScreenToWorldRay(
    float screenX,
    float screenY,
    float selectionDistance,
    const SelectionCamera& selectionCamera,
    CVector& origin,
    CVector& target) {
    if (!selectionCamera.matrix || !selectionCamera.viewWindow) {
        return false;
    }

    targetselectormath::CameraPlanePoint cameraPlane{};
    if (!targetselectormath::TryMapScreenToCameraPlane(
            screenX,
            screenY,
            selectionCamera.width,
            selectionCamera.height,
            selectionCamera.viewWindow->x,
            selectionCamera.viewWindow->y,
            cameraPlane)) {
        return false;
    }

    targetselectormath::WorldRay ray{};
    if (!targetselectormath::TryBuildWorldRay(
            cameraPlane,
            *selectionCamera.matrix,
            selectionDistance,
            ray)) {
        return false;
    }

    origin = CVector(ray.origin.x, ray.origin.y, ray.origin.z);
    target = CVector(ray.target.x, ray.target.y, ray.target.z);
    return true;
}

__declspec(noinline) bool TryResolvePoseScreenScore(
    CPed* ped,
    const SelectionCamera& selectionCamera,
    float cursorX,
    float cursorY,
    float& screenScore) {
    screenScore = 0.0f;
    if (!ped
        || !selectionCamera.matrix
        || !selectionCamera.viewWindow) {
        return false;
    }

    bool resolved = false;
    __try {
        RpClump* clump = ped->m_pRwClump;
        RpHAnimHierarchy* hierarchy = clump
            ? GetAnimHierarchyFromSkinClump(clump)
            : nullptr;
        resolved = hierarchy
            && hierarchy->numNodes > 0
            && hierarchy->numNodes <= kMaximumSaneHAnimNodeCount
            && hierarchy->pMatrixArray
            && hierarchy->pNodeInfo;
        for (const unsigned int boneId : kSelectionPoseBones) {
            if (!resolved) {
                break;
            }
            const int boneIndex = RpHAnimIDGetIndex(
                hierarchy,
                static_cast<int>(boneId));
            resolved = boneIndex >= 0 && boneIndex < hierarchy->numNodes;
        }

        if (resolved) {
            std::array<targetselectormath::ScreenPoint, kSelectionPoseBones.size()> projectedBones{};
            for (std::size_t index = 0; index < kSelectionPoseBones.size(); ++index) {
                RwV3d world{};
                ped->GetBonePosition(world, kSelectionPoseBones[index], false);
                float cameraDepth = 0.0f;
                if (!targetselectormath::TryProjectWorldToScreen(
                        { world.x, world.y, world.z },
                        selectionCamera.projection,
                        projectedBones[index],
                        cameraDepth)) {
                    resolved = false;
                    break;
                }
            }
            if (resolved) {
                resolved = targetselectormath::TryScoreScreenPose(
                    { cursorX, cursorY },
                    projectedBones.data(),
                    projectedBones.size(),
                    kSelectionPoseSegments.data(),
                    kSelectionPoseSegments.size(),
                    screenScore);
            }
        }
    } __except (
        GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER
            : EXCEPTION_CONTINUE_SEARCH) {
        resolved = false;
    }
    return resolved;
}

bool IsSelectablePed(CPed* ped, CPed* localPed) {
    return ped
        && ped != localPed
        && CPools::ms_pPedPool
        && CPools::ms_pPedPool->IsObjectValid(ped)
        && IsPedPointerValid(ped);
}

__declspec(noinline) bool TryResolveNameTagAnchor(CPed* ped, CVector& anchor) {
    if (!ped) {
        return false;
    }

    bool resolved = false;
    __try {
        if (ped->bInVehicle
            && IsVehiclePointerValid(ped->m_pVehicle)
            && ped->m_pVehicle->m_pDriver == ped) {
            anchor = ped->m_pVehicle->GetPosition();
            resolved = true;
        } else {
            RpClump* clump = ped->m_pRwClump;
            RpHAnimHierarchy* hierarchy = clump
                ? GetAnimHierarchyFromSkinClump(clump)
                : nullptr;
            const int headIndex = hierarchy
                ? RpHAnimIDGetIndex(hierarchy, BONE_HEAD)
                : -1;
            if (!hierarchy
                || hierarchy->numNodes <= 0
                || hierarchy->numNodes > kMaximumSaneHAnimNodeCount
                || !hierarchy->pMatrixArray
                || !hierarchy->pNodeInfo
                || headIndex < 0
                || headIndex >= hierarchy->numNodes) {
                return false;
            }
            RwV3d head{};
            ped->GetBonePosition(head, BONE_HEAD, false);
            anchor = CVector(head.x, head.y, head.z);
            resolved = true;
        }
    } __except (
        GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER
            : EXCEPTION_CONTINUE_SEARCH) {
        resolved = false;
    }

    return resolved
        && std::isfinite(anchor.x)
        && std::isfinite(anchor.y)
        && std::isfinite(anchor.z);
}

__declspec(noinline) bool IsNameTagLineOfSightClear(
    const CVector& origin,
    const CVector& target) {
    bool clear = false;
    __try {
        clear = CWorld::GetIsLineOfSightClear(
            origin,
            target,
            true,  // buildings
            false, // vehicles
            false, // peds
            true,  // objects
            false, // dummies
            false, // see-through check
            false); // camera-ignore check
    } __except (
        GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
            ? EXCEPTION_EXECUTE_HANDLER
            : EXCEPTION_CONTINUE_SEARCH) {
        clear = false;
    }
    return clear;
}

bool PassesSelectionNameTagGate(
    SampApi* sampApi,
    CPed* ped,
    int playerId,
    const CVector& rayOrigin) {
    if (!UiSettings::Instance().TargetSelectorRequireVisibleNameTag()) {
        return true;
    }
    if (!sampApi || !ped) {
        return false;
    }

    const std::optional<SampApi::PlayerNameTagRenderState> nameTagRenderState =
        sampApi->GetPlayerNameTagRenderState(playerId);
    if (!nameTagRenderState) {
        return false;
    }

    const CVector& position = ped->GetPosition();
    const float cameraDeltaX = position.x - rayOrigin.x;
    const float cameraDeltaY = position.y - rayOrigin.y;
    const float cameraDeltaZ = position.z - rayOrigin.z;
    const float cameraDistanceSquared =
        cameraDeltaX * cameraDeltaX
        + cameraDeltaY * cameraDeltaY
        + cameraDeltaZ * cameraDeltaZ;
    const float nameTagDrawDistanceSquared =
        nameTagRenderState->drawDistance * nameTagRenderState->drawDistance;
    if (!std::isfinite(cameraDistanceSquared)
        || cameraDistanceSquared > nameTagDrawDistanceSquared) {
        return false;
    }

    if (nameTagRenderState->noNameTagsBehindWalls) {
        CVector nameTagAnchor;
        if (!TryResolveNameTagAnchor(ped, nameTagAnchor)
            || !IsNameTagLineOfSightClear(rayOrigin, nameTagAnchor)) {
            return false;
        }
    }
    return true;
}

} // namespace

void PlayerTargetSelector::SetSampApi(SampApi* sampApi) {
    sampApi_ = sampApi;
}

void PlayerTargetSelector::SetTagsModule(TagsModule* tagsModule) {
    tagsModule_ = tagsModule;
}

void PlayerTargetSelector::SetNotificationManager(NotificationManager* notificationManager) {
    notificationManager_ = notificationManager;
}

void PlayerTargetSelector::SetHotkeyConflictCallback(HotkeyConflictCallback callback) {
    hotkeyConflictCallback_ = std::move(callback);
}

void PlayerTargetSelector::Tick(bool sampReady, bool gameForeground, bool helperUiBlocked) {
    const bool hotkeyDown = IsHotkeyDown();
    if (!gameForeground) {
        hadForeground_ = false;
        hotkeyWasDown_ = hotkeyDown;
        if (active_) {
            Cancel("focus-lost");
        }
        return;
    }
    if (!hadForeground_) {
        hadForeground_ = true;
        hotkeyWasDown_ = hotkeyDown;
        return;
    }

    if (!UiSettings::Instance().TargetSelectorEnabled()) {
        if (hotkeyCapture_.Active()) {
            CancelHotkeyCapture();
        }
        if (active_) {
            Cancel("disabled");
        }
        hotkeyWasDown_ = hotkeyDown;
        return;
    }

    const bool runtimeBlocked = !sampReady || helperUiBlocked || IsRuntimeInputBlocked();
    if (active_ && runtimeBlocked) {
        Cancel("runtime-blocked");
    }

    if (!hotkeyCapture_.Active() && hotkeyDown && !hotkeyWasDown_) {
        if (active_) {
            Cancel("hotkey");
        } else if (!runtimeBlocked && sampApi_ && tagsModule_) {
            Activate();
        } else if (notificationManager_) {
            notificationManager_->ShowUserPopup(
                UiSettings::Instance().Text(UiText::TargetSelectorUnavailable),
                NotificationSeverity::Warning);
        }
    }
    hotkeyWasDown_ = hotkeyDown;
}

bool PlayerTargetSelector::OnWindowMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (!UiSettings::Instance().TargetSelectorEnabled()) {
        return false;
    }
    if (HandleHotkeyCaptureMessage(message, wparam)) {
        return true;
    }
    if (!active_) {
        return false;
    }

    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wparam == VK_ESCAPE) {
        Cancel("escape");
        return true;
    }
    if (message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK) {
        Cancel("right-click");
        return true;
    }
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) {
        const int cursorX = static_cast<short>(LOWORD(lparam));
        const int cursorY = static_cast<short>(HIWORD(lparam));
        pendingClick_.queued = true;
        pendingClick_.cursorX = cursorX;
        pendingClick_.cursorY = cursorY;
        return true;
    }
    return IsMouseMessage(message);
}

void PlayerTargetSelector::DrawOverlay(IDirect3DDevice9* device) {
    if (!active_) {
        if (outline_.HasPendingCursorQueries()
            && !outline_.PollPendingCursorQueries()) {
            std::string failure;
            outline_.ConsumeFailure(failure);
            debuglog::WriteError(
                "[target-selector] retired outline query drain failed: %s",
                failure.empty() ? "unknown" : failure.c_str());
        }
        return;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (pendingClick_.queued) {
        const int cursorX = pendingClick_.cursorX;
        const int cursorY = pendingClick_.cursorY;
        pendingClick_.Reset();
        pendingClickStartedAtMs_ = GetTickCount64();
        outline_.CancelCursorHitRequest();
        pendingSelection_.Reset();
        clickRejectedCandidates_.clear();
        BeginPendingSelection(cursorX, cursorY);
    }

    if (pendingSelection_.active
        && !ValidatePlayer(pendingSelection_.ped, pendingSelection_.playerId)) {
        ProcessPendingSelection(PedOutlineRenderer::CursorHitStatus::Miss);
    }
    if (pendingSelection_.active && outline_.CursorHitRequestNeedsTarget()) {
        const std::uint64_t now = GetTickCount64();
        if (now >= pendingClickStartedAtMs_
            && now - pendingClickStartedAtMs_ >= outlineperf::kCursorHitQueryTimeoutMs) {
            ProcessPendingSelection(PedOutlineRenderer::CursorHitStatus::Expired);
        }
    }

    CPed* candidatePed = nullptr;
    int candidateId = -1;
    ResolveCursorCandidate(mouse.x, mouse.y, candidatePed, candidateId);
    UpdateCandidate(candidatePed, candidateId);

    CPed* renderPed = candidatePed;
    if (pendingSelection_.active
        && (outline_.CursorHitRequestNeedsTarget() || !renderPed)) {
        renderPed = pendingSelection_.ped;
    }
    outline_.SetTarget(renderPed);

    PedOutlineRenderer::CursorHitStatus cursorHit =
        PedOutlineRenderer::CursorHitStatus::Pending;
    const bool outlineOk = renderPed
        ? outline_.Render(device, mouse.x, mouse.y, cursorHit)
        : outline_.PollPendingCursorQueries();
    if (!outlineOk) {
        std::string failure;
        outline_.ConsumeFailure(failure);
        if (notificationManager_) {
            notificationManager_->ShowUserPopup(
                UiSettings::Instance().Text(UiText::TargetSelectorOutlineFailed),
                NotificationSeverity::Error,
                3500.0);
        }
        debuglog::WriteError(
            "[target-selector] canceled after outline failure: %s",
            failure.empty() ? "unknown" : failure.c_str());
        Cancel("outline-failed");
        return;
    }

    if (pendingSelection_.active) {
        ProcessPendingSelection(cursorHit);
        if (!active_) {
            return;
        }
        if (!outline_.CursorHitRequestNeedsTarget() && candidatePed) {
            outline_.SetTarget(candidatePed);
        }
    }

    DrawFullscreenHitSurface();
    DrawInstructionOverlay();
}

void PlayerTargetSelector::OnDeviceLost() {
    outline_.OnDeviceLost();
}

void PlayerTargetSelector::OnDeviceReset() {
    outline_.OnDeviceReset();
}

void PlayerTargetSelector::Shutdown() {
    CancelHotkeyCapture();
    Cancel("shutdown");
    outline_.Shutdown();
    sampApi_ = nullptr;
    tagsModule_ = nullptr;
    notificationManager_ = nullptr;
}

void PlayerTargetSelector::OnProfileChanged() {
    CancelHotkeyCapture();
    Cancel("profile-changed");
    hotkeyWasDown_ = IsHotkeyDown();
}

bool PlayerTargetSelector::IsActive() const {
    return active_;
}

bool PlayerTargetSelector::WantsOverlayRender() const {
    return active_ || outline_.HasPendingCursorQueries();
}

bool PlayerTargetSelector::WantsInputCapture() const {
    return active_;
}

std::string PlayerTargetSelector::HotkeyText() const {
    return hotkeys::ToString(UiSettings::Instance().TargetSelectorHotkey());
}

void PlayerTargetSelector::BeginHotkeyCapture() {
    if (!UiSettings::Instance().TargetSelectorEnabled()) {
        return;
    }
    hotkeyCapture_.Start(hotkeys::NormalizeCombo(
        UiSettings::Instance().TargetSelectorHotkey(),
        HotkeyMode::ModifierTrigger));
    hotkeys::OpenCapturePopupCenteredOnCurrentWindow(hotkeyCapturePopup_);
    hotkeyWasDown_ = IsHotkeyDown();
}

void PlayerTargetSelector::CancelHotkeyCapture() {
    hotkeyCapture_.Stop();
    hotkeys::ResetCapturePopupState(hotkeyCapturePopup_);
    hotkeyWasDown_ = IsHotkeyDown();
}

void PlayerTargetSelector::DrawHotkeyCapturePopup() {
    std::string conflict;
    const bool canSave = CanApplyHotkey(hotkeyCapture_.Draft(), &conflict);
    hotkeys::DrawCapturePopupModal(
        "##target_selector_hotkey_capture_popup",
        hotkeyCapturePopup_,
        hotkeyCapture_,
        [this](const std::vector<UINT>& keys) { return ApplyHotkey(keys); },
        canSave,
        HotkeyMode::ModifierTrigger,
        [&](const std::vector<UINT>&) {
            if (conflict.empty()) {
                return;
            }
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.35f, 0.35f, 1.00f));
            ImGui::TextWrapped(
                "%s",
                UiSettings::Instance().Format(UiText::HotkeyConflictFormat, conflict.c_str()).c_str());
            ImGui::PopStyleColor();
        },
        [this]() { CancelHotkeyCapture(); });
}

bool PlayerTargetSelector::IsHotkeyDown() const {
    const auto& hotkey = UiSettings::Instance().TargetSelectorHotkey();
    if (hotkey.empty()) {
        return false;
    }

    for (const unsigned int key : hotkey) {
        if ((GetAsyncKeyState(static_cast<int>(hotkeys::NormalizeKey(key))) & 0x8000) == 0) {
            return false;
        }
    }

    return hotkeys::ComboMatch(
        hotkeys::CollectPressedKeys(),
        hotkey,
        HotkeyMode::ModifierTrigger);
}

bool PlayerTargetSelector::IsRuntimeInputBlocked() const {
    return !sampApi_
        || !sampApi_->sampModule()
        || !sampApi_->isSupportedVersion()
        || sampApi_->is_chat_opened()
        || sampApi_->isDialogActive()
        || sampApi_->IsScoreboardOpen();
}

bool PlayerTargetSelector::CanApplyHotkey(
    const std::vector<unsigned int>& keys,
    std::string* description) const {
    if (description) {
        description->clear();
    }

    const std::vector<unsigned int> normalized = hotkeys::NormalizeCombo(keys, HotkeyMode::ModifierTrigger);
    if (!hotkeys::HasTriggerKey(normalized)) {
        return false;
    }

    const auto& menuHotkey = UiSettings::Instance().MenuToggleHotkey();
    if (hotkeys::CombosConflict(
            normalized,
            HotkeyMode::ModifierTrigger,
            menuHotkey,
            HotkeyMode::ModifierTrigger)) {
        if (description) {
            *description = UiSettings::Instance().Text(UiText::SettingsMainWindowHotkey);
        }
        return false;
    }

    std::string conflict;
    if (hotkeyConflictCallback_ && hotkeyConflictCallback_(normalized, conflict)) {
        if (description) {
            *description = std::move(conflict);
        }
        return false;
    }
    return true;
}

bool PlayerTargetSelector::ApplyHotkey(const std::vector<unsigned int>& keys) {
    if (!CanApplyHotkey(keys)) {
        return false;
    }
    UiSettings::Instance().SetTargetSelectorHotkey(
        hotkeys::NormalizeCombo(keys, HotkeyMode::ModifierTrigger));
    hotkeys::ResetCapturePopupState(hotkeyCapturePopup_);
    hotkeyWasDown_ = IsHotkeyDown();
    return true;
}

bool PlayerTargetSelector::HandleHotkeyCaptureMessage(UINT message, WPARAM wparam) {
    bool canceled = false;
    bool saved = false;
    std::vector<UINT> capturedKeys;
    if (!hotkeyCapture_.Active()
        || !hotkeyCapture_.OnWindowMessage(message, wparam, canceled, saved, capturedKeys)) {
        return false;
    }

    if (saved) {
        if (!ApplyHotkey(capturedKeys)) {
            hotkeyCapture_.Start(capturedKeys);
        }
    } else if (canceled) {
        CancelHotkeyCapture();
    }
    return true;
}

bool PlayerTargetSelector::ResolveCursorCandidate(
    float cursorX,
    float cursorY,
    CPed*& ped,
    int& playerId,
    const std::vector<CPed*>* excludedPeds) const {
    ped = nullptr;
    playerId = -1;
    if (!sampApi_ || IsRuntimeInputBlocked()) {
        return false;
    }

    if (!std::isfinite(cursorX) || !std::isfinite(cursorY)
        || cursorX < 0.0f || cursorY < 0.0f
        || cursorX >= static_cast<float>(RsGlobal.maximumWidth)
        || cursorY >= static_cast<float>(RsGlobal.maximumHeight)) {
        return false;
    }

    SelectionCamera selectionCamera{};
    if (!TryResolveSelectionCamera(selectionCamera)) {
        return false;
    }

    CVector rayOrigin;
    CVector rayTarget;
    const UiSettings& settings = UiSettings::Instance();
    if (!ScreenToWorldRay(
            cursorX,
            cursorY,
            settings.TargetSelectorDistance(),
            selectionCamera,
            rayOrigin,
            rayTarget)) {
        return false;
    }

    CPlayerPed* localPed = FindPlayerPed();
    const int localPlayerId = sampApi_->Local_ID();
    auto* pedPool = CPools::ms_pPedPool;
    if (!pedPool
        || !pedPool->m_pObjects
        || !pedPool->m_byteMap
        || pedPool->m_nSize <= 0
        || pedPool->m_nSize > kMaximumSanePedPoolSize) {
        return false;
    }

    targetselectormath::PreparedWorldRay ray{};
    if (!targetselectormath::TryPrepareWorldRay(
            {
                { rayOrigin.x, rayOrigin.y, rayOrigin.z },
                { rayTarget.x, rayTarget.y, rayTarget.z },
            },
            ray)) {
        return false;
    }
    CPed* nearestPed = nullptr;
    int nearestPlayerId = -1;
    float nearestDistance = std::numeric_limits<float>::max();
    float nearestScreenScore = std::numeric_limits<float>::max();
    std::uintptr_t nearestAddress = std::numeric_limits<std::uintptr_t>::max();
    for (int index = 0; index < pedPool->m_nSize; ++index) {
        CPed* candidate = pedPool->GetAt(index);
        if (!IsSelectablePed(candidate, localPed)) {
            continue;
        }
        if (excludedPeds
            && std::find(excludedPeds->begin(), excludedPeds->end(), candidate)
                != excludedPeds->end()) {
            continue;
        }

        const CVector& position = candidate->GetPosition();
        targetselectormath::RaySphereHit candidateHit{};
        if (!targetselectormath::TryIntersectSelectionBounds(
                ray,
                {
                    position.x,
                    position.y,
                    position.z,
                },
                candidateHit)) {
            continue;
        }

        const float candidateDistance = candidateHit.entryDistance;
        float candidateScreenScore = 0.0f;
        if (!TryResolvePoseScreenScore(
                candidate,
                selectionCamera,
                cursorX,
                cursorY,
                candidateScreenScore)) {
            targetselectormath::ScreenPoint projectedCenter{};
            float cameraDepth = 0.0f;
            if (!targetselectormath::TryProjectWorldToScreen(
                    {
                        position.x,
                        position.y,
                        position.z + kSelectionSphereCenterOffset,
                    },
                    selectionCamera.projection,
                    projectedCenter,
                    cameraDepth)) {
                continue;
            }
            const float deltaX = cursorX - projectedCenter.x;
            const float deltaY = cursorY - projectedCenter.y;
            candidateScreenScore = deltaX * deltaX + deltaY * deltaY;
            if (!std::isfinite(candidateScreenScore)) {
                continue;
            }
        }

        const std::uintptr_t candidateAddress = reinterpret_cast<std::uintptr_t>(candidate);
        const bool candidateIsBetter = candidateScreenScore < nearestScreenScore
            || (candidateScreenScore == nearestScreenScore
                && (candidateDistance < nearestDistance
                    || (candidateDistance == nearestDistance
                        && candidateAddress < nearestAddress)));
        if (!candidateIsBetter) {
            continue;
        }

        const auto [resolved, candidatePlayerId] =
            sampApi_->TryResolvePlayerIdByPedFast(candidate);
        if (!resolved
            || candidatePlayerId < 0
            || candidatePlayerId > 1003
            || candidatePlayerId == localPlayerId
            || !sampApi_->IsConnected(candidatePlayerId)) {
            continue;
        }

        if (!PassesSelectionNameTagGate(sampApi_, candidate, candidatePlayerId, rayOrigin)) {
            continue;
        }

        nearestPed = candidate;
        nearestPlayerId = candidatePlayerId;
        nearestDistance = candidateDistance;
        nearestScreenScore = candidateScreenScore;
        nearestAddress = candidateAddress;
    }

    if (!nearestPed) {
        return false;
    }

    ped = nearestPed;
    playerId = nearestPlayerId;
    return true;
}

void PlayerTargetSelector::UpdateCandidate(CPed* ped, int playerId) {
    if (candidatePed_ == ped && candidatePlayerId_ == playerId) {
        return;
    }

    candidatePed_ = ped;
    candidatePlayerId_ = playerId;
    candidatePlayerName_.clear();
    if (!ped || playerId < 0 || !sampApi_) {
        return;
    }

    candidatePlayerName_ = sampApi_->GetNameID(playerId);
    if (candidatePlayerName_.empty() || candidatePlayerName_ == "UNKNOWN") {
        candidatePlayerName_ = "ID " + std::to_string(playerId);
    }
}

bool PlayerTargetSelector::ValidatePlayer(CPed* ped, int playerId) const {
    return sampApi_
        && ped
        && playerId >= 0
        && playerId <= 1003
        && sampApi_->IsConnected(playerId)
        && sampApi_->GetPlayerPedPointer(playerId) == ped;
}

bool PlayerTargetSelector::ValidateSelectionCandidate(
    CPed* ped,
    int playerId,
    int cursorX,
    int cursorY) const {
    if (!ValidatePlayer(ped, playerId)
        || ped == FindPlayerPed()
        || cursorX < 0
        || cursorY < 0
        || cursorX >= RsGlobal.maximumWidth
        || cursorY >= RsGlobal.maximumHeight) {
        return false;
    }

    SelectionCamera selectionCamera{};
    CVector rayOrigin;
    CVector rayTarget;
    if (!TryResolveSelectionCamera(selectionCamera)
        || !ScreenToWorldRay(
            static_cast<float>(cursorX),
            static_cast<float>(cursorY),
            UiSettings::Instance().TargetSelectorDistance(),
            selectionCamera,
            rayOrigin,
            rayTarget)) {
        return false;
    }

    const CVector& position = ped->GetPosition();
    targetselectormath::PreparedWorldRay ray{};
    targetselectormath::RaySphereHit hit{};
    if (!targetselectormath::TryPrepareWorldRay(
            {
                { rayOrigin.x, rayOrigin.y, rayOrigin.z },
                { rayTarget.x, rayTarget.y, rayTarget.z },
            },
            ray)
        || !targetselectormath::TryIntersectSelectionBounds(
            ray,
            { position.x, position.y, position.z },
            hit)) {
        return false;
    }

    return PassesSelectionNameTagGate(sampApi_, ped, playerId, rayOrigin);
}

bool PlayerTargetSelector::BeginPendingSelection(int cursorX, int cursorY) {
    CPed* ped = nullptr;
    int playerId = -1;
    if (!ResolveCursorCandidate(
            static_cast<float>(cursorX),
            static_cast<float>(cursorY),
            ped,
            playerId,
            &clickRejectedCandidates_)) {
        pendingSelection_.Reset();
        outline_.CancelCursorHitRequest();
        outline_.SetTarget(nullptr);
        return false;
    }

    pendingSelection_.active = true;
    pendingSelection_.ped = ped;
    pendingSelection_.playerId = playerId;
    pendingSelection_.cursorX = cursorX;
    pendingSelection_.cursorY = cursorY;
    outline_.SetTarget(ped);
    if (!outline_.RequestCursorHit(ped, cursorX, cursorY)) {
        debuglog::WriteError(
            "[target-selector] exact click request rejected id=%d ped=%p pixel=(%d,%d)",
            playerId,
            ped,
            cursorX,
            cursorY);
        pendingSelection_.Reset();
        outline_.SetTarget(nullptr);
        return false;
    }
    return true;
}

void PlayerTargetSelector::ProcessPendingSelection(
    PedOutlineRenderer::CursorHitStatus cursorHit) {
    if (!pendingSelection_.active) {
        return;
    }
    // Poll/accept a ready hit before applying the whole-click deadline.
    if (cursorHit == PedOutlineRenderer::CursorHitStatus::Hit) {
        SelectPendingPlayer();
        return;
    }
    const std::uint64_t now = GetTickCount64();
    if (cursorHit == PedOutlineRenderer::CursorHitStatus::Expired
        || (now >= pendingClickStartedAtMs_
            && now - pendingClickStartedAtMs_ >= outlineperf::kCursorHitQueryTimeoutMs)) {
        debuglog::WriteInfo("[target-selector] click attempt expired; selection unchanged");
        outline_.CancelCursorHitRequest();
        pendingSelection_.Reset();
        clickRejectedCandidates_.clear();
        return;
    }
    if (cursorHit == PedOutlineRenderer::CursorHitStatus::Pending) {
        return;
    }

    const int cursorX = pendingSelection_.cursorX;
    const int cursorY = pendingSelection_.cursorY;
    if (pendingSelection_.ped
        && std::find(
               clickRejectedCandidates_.begin(),
               clickRejectedCandidates_.end(),
               pendingSelection_.ped)
            == clickRejectedCandidates_.end()) {
        clickRejectedCandidates_.push_back(pendingSelection_.ped);
    }
    outline_.CancelCursorHitRequest();
    pendingSelection_.Reset();
    if (!BeginPendingSelection(cursorX, cursorY)) {
        clickRejectedCandidates_.clear();
    }
}

void PlayerTargetSelector::SelectPendingPlayer() {
    if (!ValidateSelectionCandidate(
            pendingSelection_.ped,
            pendingSelection_.playerId,
            pendingSelection_.cursorX,
            pendingSelection_.cursorY)
        || !tagsModule_) {
        debuglog::WriteError(
            "[target-selector] exact hit rejected after frozen candidate validation "
            "id=%d ped=%p",
            pendingSelection_.playerId,
            pendingSelection_.ped);
        outline_.CancelCursorHitRequest();
        pendingSelection_.Reset();
        clickRejectedCandidates_.clear();
        return;
    }

    std::string playerName = sampApi_->GetNameID(pendingSelection_.playerId);
    if (playerName.empty() || playerName == "UNKNOWN") {
        playerName = "ID " + std::to_string(pendingSelection_.playerId);
    }

    CPlayerPed* localPed = FindPlayerPed();
    const void* currentAimPed = localPed ? localPed->m_pPlayerTargettedPed : nullptr;
    tagsModule_->SetManualTargetId(pendingSelection_.playerId, currentAimPed);
    debuglog::WriteInfo(
        "[target-selector] selected id=%d ped=%p name=%s",
        pendingSelection_.playerId,
        pendingSelection_.ped,
        playerName.c_str());

    if (notificationManager_) {
        notificationManager_->ShowUserPopup(
            UiSettings::Instance().Format(
                UiText::TargetSelectorSelectedFormat,
                playerName.c_str(),
                pendingSelection_.playerId),
            NotificationSeverity::Success);
    }
    Cancel("selected");
}

void PlayerTargetSelector::Activate() {
    active_ = true;
    pendingClick_.Reset();
    pendingSelection_.Reset();
    candidatePed_ = nullptr;
    candidatePlayerId_ = -1;
    candidatePlayerName_.clear();
    clickRejectedCandidates_.clear();
    outline_.CancelCursorHitRequest();
    outline_.SetTarget(nullptr);
    debuglog::WriteInfo(
        "[target-selector] activated hotkey=%s distance=%.1f requireVisibleNameTag=%d "
        "candidateRank=screen-pose-v1",
        HotkeyText().c_str(),
        UiSettings::Instance().TargetSelectorDistance(),
        UiSettings::Instance().TargetSelectorRequireVisibleNameTag() ? 1 : 0);
}

void PlayerTargetSelector::Cancel(const char* reason) {
    if (!active_) {
        return;
    }

    active_ = false;
    pendingClick_.Reset();
    pendingSelection_.Reset();
    candidatePed_ = nullptr;
    candidatePlayerId_ = -1;
    candidatePlayerName_.clear();
    clickRejectedCandidates_.clear();
    outline_.Deactivate();
    debuglog::WriteInfo("[target-selector] canceled reason=%s", reason ? reason : "unknown");
}

void PlayerTargetSelector::DrawFullscreenHitSurface() const {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoBackground;
    if (ImGui::Begin("##target_selector_hit_surface", nullptr, flags)) {
        ImGui::InvisibleButton(
            "##target_selector_fullscreen_button",
            ImGui::GetContentRegionAvail(),
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    }
    ImGui::End();
}

void PlayerTargetSelector::DrawInstructionOverlay() const {
    UiSettings& ui = UiSettings::Instance();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + ui.Scale(24.0f)),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.88f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoInputs
        | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##target_selector_instruction", nullptr, flags)) {
        ImGui::TextUnformatted(ui.Text(UiText::TargetSelectorInstruction));
        ImGui::Separator();
        if (candidatePed_) {
            ImGui::Text(
                "%s",
                ui.Format(
                    UiText::TargetSelectorCandidateFormat,
                    candidatePlayerName_.c_str(),
                    candidatePlayerId_)
                    .c_str());
        } else {
            ImGui::TextDisabled("%s", ui.Text(UiText::TargetSelectorNoCandidate));
        }
    }
    ImGui::End();
}
