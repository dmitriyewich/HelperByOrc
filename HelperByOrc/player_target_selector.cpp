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
#include <cmath>
#include <limits>
#include <utility>

namespace {

constexpr float kSelectionSphereCenterOffset = 1.0f;
constexpr float kSelectionSphereRadius = 1.5f;
constexpr int kMaximumSanePedPoolSize = 4096;

bool IsMouseMessage(UINT message) {
    return message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
}

bool ScreenToWorldRay(
    float screenX,
    float screenY,
    float selectionDistance,
    CVector& origin,
    CVector& target) {
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

    const float width = static_cast<float>(RsGlobal.maximumWidth);
    const float height = static_cast<float>(RsGlobal.maximumHeight);
    targetselectormath::CameraPlanePoint cameraPlane{};
    if (!targetselectormath::TryMapScreenToCameraPlane(
            screenX,
            screenY,
            width,
            height,
            viewWindow->x,
            viewWindow->y,
            cameraPlane)) {
        return false;
    }

    targetselectormath::WorldRay ray{};
    if (!targetselectormath::TryBuildWorldRay(
            cameraPlane,
            *matrix,
            selectionDistance,
            ray)) {
        return false;
    }

    origin = CVector(ray.origin.x, ray.origin.y, ray.origin.z);
    target = CVector(ray.target.x, ray.target.y, ray.target.z);
    return true;
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
        } else if (ped->m_pRwClump
            && GetAnimHierarchyFromSkinClump(ped->m_pRwClump)) {
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
        if (cursorX == hoveredCursorX_
            && cursorY == hoveredCursorY_
            && ValidateHoveredPlayer()) {
            SelectHoveredPlayer();
        }
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
    ResetRejectedCandidatesForCursor(mouse.x, mouse.y);

    CPed* candidatePed = nullptr;
    int candidateId = -1;
    ResolveCursorCandidate(candidatePed, candidateId);
    outline_.SetTarget(candidatePed);

    PedOutlineRenderer::CursorHitStatus cursorHit =
        PedOutlineRenderer::CursorHitStatus::Pending;
    const bool outlineOk = candidatePed
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

    hoveredPed_ = nullptr;
    hoveredPlayerId_ = -1;
    hoveredCursorX_ = -1;
    hoveredCursorY_ = -1;
    hoveredPlayerName_.clear();
    if (cursorHit == PedOutlineRenderer::CursorHitStatus::Hit) {
        hoveredPed_ = candidatePed;
        hoveredPlayerId_ = candidateId;
        hoveredCursorX_ = static_cast<int>(std::floor(mouse.x));
        hoveredCursorY_ = static_cast<int>(std::floor(mouse.y));
        hoveredPlayerName_ = sampApi_->GetNameID(candidateId);
        if (hoveredPlayerName_.empty() || hoveredPlayerName_ == "UNKNOWN") {
            hoveredPlayerName_ = "ID " + std::to_string(candidateId);
        }
    } else if (cursorHit == PedOutlineRenderer::CursorHitStatus::Miss && candidatePed) {
        if (std::find(rejectedCandidates_.begin(), rejectedCandidates_.end(), candidatePed)
            == rejectedCandidates_.end()) {
            rejectedCandidates_.push_back(candidatePed);
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
    return hotkeys::ComboMatch(
        hotkeys::CollectPressedKeys(),
        UiSettings::Instance().TargetSelectorHotkey(),
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
    CPed*& ped,
    int& playerId) const {
    ped = nullptr;
    playerId = -1;
    if (!sampApi_ || IsRuntimeInputBlocked()) {
        return false;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (!std::isfinite(mouse.x) || !std::isfinite(mouse.y)
        || mouse.x < 0.0f || mouse.y < 0.0f
        || mouse.x >= static_cast<float>(RsGlobal.maximumWidth)
        || mouse.y >= static_cast<float>(RsGlobal.maximumHeight)) {
        return false;
    }

    CVector rayOrigin;
    CVector rayTarget;
    const UiSettings& settings = UiSettings::Instance();
    if (!ScreenToWorldRay(
            mouse.x,
            mouse.y,
            settings.TargetSelectorDistance(),
            rayOrigin,
            rayTarget)) {
        return false;
    }

    CPlayerPed* localPed = FindPlayerPed();
    auto* pedPool = CPools::ms_pPedPool;
    if (!pedPool
        || !pedPool->m_pObjects
        || !pedPool->m_byteMap
        || pedPool->m_nSize <= 0
        || pedPool->m_nSize > kMaximumSanePedPoolSize) {
        return false;
    }

    const targetselectormath::WorldRay ray{
        { rayOrigin.x, rayOrigin.y, rayOrigin.z },
        { rayTarget.x, rayTarget.y, rayTarget.z },
    };
    CPed* nearestPed = nullptr;
    int nearestPlayerId = -1;
    float nearestDistance = std::numeric_limits<float>::max();
    for (int index = 0; index < pedPool->m_nSize; ++index) {
        CPed* candidate = pedPool->GetAt(index);
        if (!IsSelectablePed(candidate, localPed)) {
            continue;
        }
        if (std::find(rejectedCandidates_.begin(), rejectedCandidates_.end(), candidate)
            != rejectedCandidates_.end()) {
            continue;
        }

        const CVector& position = candidate->GetPosition();
        float candidateDistance = 0.0f;
        if (!targetselectormath::TryIntersectRaySphere(
                ray,
                {
                    position.x,
                    position.y,
                    position.z + kSelectionSphereCenterOffset,
                },
                kSelectionSphereRadius,
                candidateDistance)
            || candidateDistance >= nearestDistance) {
            continue;
        }

        const auto [resolved, candidatePlayerId] =
            sampApi_->TryResolvePlayerIdByPedFast(candidate);
        if (!resolved
            || candidatePlayerId < 0
            || candidatePlayerId > 1003
            || candidatePlayerId == sampApi_->Local_ID()
            || !sampApi_->IsConnected(candidatePlayerId)) {
            continue;
        }

        nearestPed = candidate;
        nearestPlayerId = candidatePlayerId;
        nearestDistance = candidateDistance;
    }

    if (!nearestPed) {
        return false;
    }

    if (settings.TargetSelectorRequireVisibleNameTag()) {
        const std::optional<SampApi::PlayerNameTagRenderState> nameTagRenderState =
            sampApi_->GetPlayerNameTagRenderState(nearestPlayerId);
        if (!nameTagRenderState) {
            return false;
        }

        const CVector& position = nearestPed->GetPosition();
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
            if (!TryResolveNameTagAnchor(nearestPed, nameTagAnchor)
                || !IsNameTagLineOfSightClear(rayOrigin, nameTagAnchor)) {
                return false;
            }
        }
    }

    ped = nearestPed;
    playerId = nearestPlayerId;
    return true;
}

void PlayerTargetSelector::ResetRejectedCandidatesForCursor(float cursorX, float cursorY) {
    if (!std::isfinite(cursorX) || !std::isfinite(cursorY)) {
        rejectedCursorX_ = -1;
        rejectedCursorY_ = -1;
        rejectedCandidates_.clear();
        return;
    }

    const int pixelX = static_cast<int>(std::floor(cursorX));
    const int pixelY = static_cast<int>(std::floor(cursorY));
    if (pixelX == rejectedCursorX_ && pixelY == rejectedCursorY_) {
        return;
    }

    rejectedCursorX_ = pixelX;
    rejectedCursorY_ = pixelY;
    rejectedCandidates_.clear();
}

bool PlayerTargetSelector::ValidateHoveredPlayer() const {
    return sampApi_
        && hoveredPed_
        && hoveredPlayerId_ >= 0
        && hoveredPlayerId_ <= 1003
        && sampApi_->IsConnected(hoveredPlayerId_)
        && sampApi_->GetPlayerPedPointer(hoveredPlayerId_) == hoveredPed_;
}

void PlayerTargetSelector::Activate() {
    active_ = true;
    hoveredPed_ = nullptr;
    hoveredPlayerId_ = -1;
    hoveredCursorX_ = -1;
    hoveredCursorY_ = -1;
    hoveredPlayerName_.clear();
    rejectedCandidates_.clear();
    rejectedCursorX_ = -1;
    rejectedCursorY_ = -1;
    outline_.SetTarget(nullptr);
    debuglog::WriteInfo(
        "[target-selector] activated hotkey=%s distance=%.1f requireVisibleNameTag=%d",
        HotkeyText().c_str(),
        UiSettings::Instance().TargetSelectorDistance(),
        UiSettings::Instance().TargetSelectorRequireVisibleNameTag() ? 1 : 0);
}

void PlayerTargetSelector::Cancel(const char* reason) {
    if (!active_) {
        return;
    }

    active_ = false;
    hoveredPed_ = nullptr;
    hoveredPlayerId_ = -1;
    hoveredCursorX_ = -1;
    hoveredCursorY_ = -1;
    hoveredPlayerName_.clear();
    rejectedCandidates_.clear();
    rejectedCursorX_ = -1;
    rejectedCursorY_ = -1;
    outline_.Deactivate();
    debuglog::WriteInfo("[target-selector] canceled reason=%s", reason ? reason : "unknown");
}

void PlayerTargetSelector::SelectHoveredPlayer() {
    if (!ValidateHoveredPlayer() || !tagsModule_) {
        return;
    }

    CPlayerPed* localPed = FindPlayerPed();
    const void* currentAimPed = localPed ? localPed->m_pPlayerTargettedPed : nullptr;
    tagsModule_->SetManualTargetId(hoveredPlayerId_, currentAimPed);
    debuglog::WriteInfo(
        "[target-selector] selected id=%d ped=%p name=%s",
        hoveredPlayerId_,
        hoveredPed_,
        hoveredPlayerName_.c_str());

    if (notificationManager_) {
        notificationManager_->ShowUserPopup(
            UiSettings::Instance().Format(
                UiText::TargetSelectorSelectedFormat,
                hoveredPlayerName_.c_str(),
                hoveredPlayerId_),
            NotificationSeverity::Success);
    }
    Cancel("selected");
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
        if (hoveredPed_) {
            ImGui::Text(
                "%s",
                ui.Format(
                    UiText::TargetSelectorHoverFormat,
                    hoveredPlayerName_.c_str(),
                    hoveredPlayerId_)
                    .c_str());
        } else {
            ImGui::TextDisabled("%s", ui.Text(UiText::TargetSelectorNoTarget));
        }
    }
    ImGui::End();
}
