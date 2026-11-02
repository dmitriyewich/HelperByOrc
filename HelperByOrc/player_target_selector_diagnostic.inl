// Included only by the diagnostic build, after the selector implementation.
bool PlayerTargetSelector::ResolveDiagnosticCandidate(
    float cursorX, float cursorY, CPed*& ped, int& playerId,
    const std::vector<CPed*>* excludedPeds) const {
    auto& diag = targetdiag::Diagnostics::Instance();
    if (!diag.IsEnabled() || !std::isfinite(cursorX) || !std::isfinite(cursorY)
        || cursorX < 0 || cursorY < 0 || cursorX >= RsGlobal.maximumWidth || cursorY >= RsGlobal.maximumHeight) {
        return ResolveCursorCandidate(cursorX, cursorY, ped, playerId, excludedPeds);
    }
    const bool click = excludedPeds != nullptr;
    CPed* posePed = nullptr;
    CPed* rayPed = nullptr;
    int poseId = -1;
    int rayId = -1;
    const auto runPose = [&] {
        const auto start = targetdiag::Diagnostics::NowTicks();
        ResolveCursorCandidate(cursorX, cursorY, posePed, poseId, excludedPeds);
        const auto end = targetdiag::Diagnostics::NowTicks();
        diag.RecordResolve(targetdiag::Algorithm::Pose, start, end, click);
    };
    const auto runRay = [&] {
        const auto start = targetdiag::Diagnostics::NowTicks();
        ResolveRaycastCandidate(cursorX, cursorY, rayPed, rayId, excludedPeds);
        const auto end = targetdiag::Diagnostics::NowTicks();
        diag.RecordResolve(targetdiag::Algorithm::Raycast, start, end, click);
    };
    if (diag.WantsShadow() || click) {
        if (diag.ShadowRaycastFirst()) { runRay(); runPose(); }
        else { runPose(); runRay(); }
        diag.RecordComparison(static_cast<int>(cursorX), static_cast<int>(cursorY), poseId, rayId,
            reinterpret_cast<std::uintptr_t>(posePed), reinterpret_cast<std::uintptr_t>(rayPed));
        if (click) {
            diag.RecordClick("raw-A", poseId, static_cast<int>(cursorX), static_cast<int>(cursorY), excludedPeds->size());
            diag.RecordClick("raw-B", rayId, static_cast<int>(cursorX), static_cast<int>(cursorY), excludedPeds->size());
        }
    } else if (diag.ActiveAlgorithm() == targetdiag::Algorithm::Raycast) {
        runRay();
    } else {
        runPose();
    }
    const bool useRay = diag.ActiveAlgorithm() == targetdiag::Algorithm::Raycast;
    ped = useRay ? rayPed : posePed;
    playerId = useRay ? rayId : poseId;
    return ped != nullptr;
}

bool PlayerTargetSelector::ResolveRaycastCandidate(
    float cursorX, float cursorY, CPed*& ped, int& playerId,
    const std::vector<CPed*>* excludedPeds) const {
    ped = nullptr;
    playerId = -1;
    if (!sampApi_ || IsRuntimeInputBlocked()
        || !std::isfinite(cursorX) || !std::isfinite(cursorY)
        || cursorX < 0 || cursorY < 0
        || cursorX >= static_cast<float>(RsGlobal.maximumWidth)
        || cursorY >= static_cast<float>(RsGlobal.maximumHeight)) {
        return false;
    }
    SelectionCamera camera{};
    CVector origin;
    CVector target;
    if (!TryResolveSelectionCamera(camera)
        || !ScreenToWorldRay(cursorX, cursorY, UiSettings::Instance().TargetSelectorDistance(),
            camera, origin, target)) {
        return false;
    }
    auto* pool = CPools::ms_pPedPool;
    if (!pool || !pool->m_pObjects || !pool->m_byteMap
        || pool->m_nSize <= 0 || pool->m_nSize > kMaximumSanePedPoolSize) return false;
    auto& diagnostics = targetdiag::Diagnostics::Instance();
    const bool trace = diagnostics.WantsShadow() || excludedPeds != nullptr;
    CPlayerPed* local = FindPlayerPed();
    CEntity* hit = nullptr;
    const char* failure = nullptr;
    if (!targetdiag::NativePedRaycast(origin, target, local, hit, failure)) {
        if (failure) targetdiag::Diagnostics::Instance().SetRaycastAvailability(false, failure);
        if (trace) diagnostics.RecordNativeRay(origin.x, origin.y, origin.z,
            target.x, target.y, target.z, reinterpret_cast<std::uintptr_t>(hit), -1,
            failure ? failure : "no-collision");
        return false;
    }
    // Never dereference a native entity as a ped until the ped pool validates its address.
    CPed* candidate = reinterpret_cast<CPed*>(hit);
    const auto address = reinterpret_cast<std::uintptr_t>(hit);
    const auto poolBegin = reinterpret_cast<std::uintptr_t>(pool->m_pObjects);
    const bool inPool = address >= poolBegin
        && address - poolBegin < static_cast<std::size_t>(pool->m_nSize) * sizeof(*pool->m_pObjects)
        && (address - poolBegin) % sizeof(*pool->m_pObjects) == 0;
    const bool valid = inPool && IsSelectablePed(candidate, local);
    const char* outcome = "selected";
    if (!valid) outcome = "invalid-or-local-ped";
    else if (excludedPeds && std::find(excludedPeds->begin(), excludedPeds->end(), candidate) != excludedPeds->end())
        outcome = "first-ped-already-rejected";
    else {
        const auto [resolved, id] = sampApi_->TryResolvePlayerIdByPedFast(candidate);
        if (!resolved || id < 0 || id > 1003 || id == sampApi_->Local_ID() || !sampApi_->IsConnected(id))
            outcome = "first-ped-not-remote-player";
        else if (!PassesSelectionNameTagGate(sampApi_, candidate, id, origin))
            outcome = "first-ped-name-tag-gate";
        else {
            ped = candidate;
            playerId = id;
        }
    }
    if (trace) diagnostics.RecordNativeRay(origin.x, origin.y, origin.z,
        target.x, target.y, target.z, reinterpret_cast<std::uintptr_t>(hit),
        valid ? candidate->m_nModelIndex : -1, outcome);
    return ped != nullptr;
}
