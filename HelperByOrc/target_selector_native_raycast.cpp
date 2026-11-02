#include "target_selector_native_raycast.h"

#if HELPERBYORC_TARGET_DIAGNOSTICS
#include "debug_log.h"
#include "file_hash_utils.h"

#include <CWorld.h>

#include <array>
#include <cstdint>

namespace targetdiag {
namespace {

DWORD renderThreadId = 0;
bool checked = false;
bool available = false;
const char* gateReason = "native gate not checked";

struct CodeRange { std::uintptr_t address; std::size_t size; std::uint64_t hash; };
// Full IDA-confirmed function ranges, including the on-disk relocated instruction island.
// Exact builds and derivation: local diagnostic raycast-binary.md / native-fingerprints.json.
constexpr std::array<CodeRange, 6> kCodeRanges{{
    { 0x56BA00, 5079, 0xF663E133964D32B9ull },
    { 0x56B5E0, 418, 0x529D6EEE37DF9A2Full },
    { 0x401855, 11, 0xB94D46416549136Aull },
    { 0x566EE0, 1531, 0xF5F511636E01FE10ull },
    { 0x4C6F70, 503, 0x3222E8C08FB93095ull },
    { 0x563F40, 73, 0xE1C3F9513904E184ull },
}};

bool CheckLiveCode() {
    __try {
        for (const auto& range : kCodeRanges) {
            const auto* bytes = reinterpret_cast<const unsigned char*>(range.address);
            std::uint64_t hash = 14695981039346656037ull;
            for (std::size_t i = 0; i < range.size; ++i) {
                hash = (hash ^ bytes[i]) * 1099511628211ull;
            }
            if (hash != range.hash) {
                debuglog::WriteError("[target-diag][native] live code mismatch VA=%p", bytes);
                return false;
            }
        }
        MEMORY_BASIC_INFORMATION region{};
        return VirtualQuery(reinterpret_cast<const void*>(0xB7CD60), &region, sizeof(region))
            && region.AllocationBase == GetModuleHandleW(nullptr)
            && region.State == MEM_COMMIT && !(region.Protect & (PAGE_GUARD | PAGE_NOACCESS))
            && (region.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
            && reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize > 0xB7CD79;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

bool CallNative(const CVector& origin, const CVector& target, CPed* localPed, CEntity*& hit) {
    // Native scan advancement and animated collision scratch deliberately remain native.
    // Rolling the scan code back would disagree with the entity stamps already written.
    CEntity* savedIgnore = CWorld::pIgnoreEntity;
    const bool savedDead = CWorld::bIncludeDeadPeds;
    const bool savedBikers = CWorld::bIncludeBikers;
    const bool savedTyres = CWorld::bIncludeCarTyres;
    const float savedSpread = CWorld::fWeaponSpreadRate;
    const int savedCrossings = CWorld::ms_iProcessLineNumCrossings;
    bool hitSomething = false;
    __try {
        CWorld::pIgnoreEntity = localPed;
        CWorld::bIncludeDeadPeds = true;
        CWorld::bIncludeBikers = true;
        CWorld::bIncludeCarTyres = false;
        CWorld::fWeaponSpreadRate = 0.0f;
        CColPoint point{};
        hitSomething = CWorld::ProcessLineOfSight(origin, target, point, hit,
            false, false, true, false, false, false, false, false);
    } __finally {
        CWorld::pIgnoreEntity = savedIgnore;
        CWorld::bIncludeDeadPeds = savedDead;
        CWorld::bIncludeBikers = savedBikers;
        CWorld::bIncludeCarTyres = savedTyres;
        CWorld::fWeaponSpreadRate = savedSpread;
        CWorld::ms_iProcessLineNumCrossings = savedCrossings;
    }
    return hitSomething;
}

} // namespace

bool PrepareNativeRaycast(IDirect3DDevice9* device, const char*& reason) {
    if (!checked) {
        checked = true;
        wchar_t path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x400000)
            || length == 0 || length >= MAX_PATH) {
            gateReason = "unsupported image base/path";
        } else {
            const auto hash = file_hash::Compute(path, true);
            debuglog::WriteInfo("[target-diag][native] exe sha256=%s", hash.sha256.c_str());
            if (!hash.sha256Ok || (hash.sha256 != "DD4FC723481481D3FA688868C2E4D08C294C8CF3524C1F974E19D860A14AD0F6"
                && hash.sha256 != "F01A00CE950FA40CA1ED59DF0E789848C6EDCF6405456274965885D0929343AC")) {
                gateReason = "exe fingerprint not verified in IDA";
            } else if (!CheckLiveCode()) {
                gateReason = "native code/globals modified or unavailable";
            } else {
                available = true;
                gateReason = "verified exe and live native ranges";
            }
        }
    }
    if (available) {
        D3DDEVICE_CREATION_PARAMETERS parameters{};
        DWORD windowPid = 0;
        const DWORD windowThread = device && SUCCEEDED(device->GetCreationParameters(&parameters))
            ? GetWindowThreadProcessId(parameters.hFocusWindow, &windowPid) : 0;
        if (windowPid != GetCurrentProcessId() || windowThread != GetCurrentThreadId()
            || (renderThreadId && renderThreadId != windowThread)) {
            available = false;
            gateReason = "render callback is not the game window thread";
        } else {
            renderThreadId = windowThread;
        }
    }
    reason = gateReason;
    return available;
}

bool NativePedRaycast(const CVector& origin, const CVector& target,
    CPed* localPed, CEntity*& hit, const char*& failure) {
    hit = nullptr;
    failure = nullptr;
    if (!available || GetCurrentThreadId() != renderThreadId) {
        failure = available ? "native call thread changed" : gateReason;
        return false;
    }
    bool hitSomething = false;
    __try {
        hitSomething = CallNative(origin, target, localPed, hit);
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        available = false;
        gateReason = "access violation in native raycast; disabled";
        failure = gateReason;
        hit = nullptr;
        debuglog::WriteError("[target-diag][native] %s", failure);
        return false;
    }
    return hitSomething && hit;
}

} // namespace targetdiag
#endif
