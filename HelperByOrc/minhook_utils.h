#pragma once

#include <MinHook.h>

#include "debug_log.h"

namespace minhook {

inline bool Initialize() {
    const MH_STATUS status = MH_Initialize();
    if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) {
        return true;
    }

    debuglog::Write("MH_Initialize failed: %s (%d)", MH_StatusToString(status), static_cast<int>(status));
    return false;
}

inline void Uninitialize() {
    const MH_STATUS status = MH_Uninitialize();
    if (status == MH_OK || status == MH_ERROR_NOT_INITIALIZED) {
        return;
    }

    debuglog::Write("MH_Uninitialize failed: %s (%d)", MH_StatusToString(status), static_cast<int>(status));
}

inline bool CreateHook(void* target, void* detour, void** original, const char* hookName) {
    if (!target || !detour || !original) {
        debuglog::Write("MH_CreateHook skipped for %s: invalid arguments", hookName ? hookName : "<unnamed>");
        return false;
    }

    const MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status == MH_OK) {
        return true;
    }

    debuglog::Write(
        "MH_CreateHook failed for %s: %s (%d)",
        hookName ? hookName : "<unnamed>",
        MH_StatusToString(status),
        static_cast<int>(status));
    return false;
}

inline bool EnableHook(void* target, const char* hookName) {
    if (!target) {
        debuglog::Write("MH_EnableHook skipped for %s: target is null", hookName ? hookName : "<unnamed>");
        return false;
    }

    const MH_STATUS status = MH_EnableHook(target);
    if (status == MH_OK) {
        return true;
    }

    debuglog::Write(
        "MH_EnableHook failed for %s: %s (%d)",
        hookName ? hookName : "<unnamed>",
        MH_StatusToString(status),
        static_cast<int>(status));
    return false;
}

template <typename TOriginal>
inline bool CreateAndEnableHook(void* target, void* detour, TOriginal* original, const char* hookName) {
    if (!CreateHook(target, detour, reinterpret_cast<void**>(original), hookName)) {
        return false;
    }

    if (EnableHook(target, hookName)) {
        return true;
    }

    const MH_STATUS removeStatus = MH_RemoveHook(target);
    if (removeStatus != MH_OK && removeStatus != MH_ERROR_NOT_CREATED) {
        debuglog::Write(
            "MH_RemoveHook rollback failed for %s: %s (%d)",
            hookName ? hookName : "<unnamed>",
            MH_StatusToString(removeStatus),
            static_cast<int>(removeStatus));
    }
    return false;
}

inline void DisableAndRemoveHook(void*& target, const char* hookName) {
    if (!target) {
        return;
    }

    const MH_STATUS disableStatus = MH_DisableHook(target);
    if (disableStatus != MH_OK
        && disableStatus != MH_ERROR_DISABLED
        && disableStatus != MH_ERROR_NOT_CREATED) {
        debuglog::Write(
            "MH_DisableHook failed for %s: %s (%d)",
            hookName ? hookName : "<unnamed>",
            MH_StatusToString(disableStatus),
            static_cast<int>(disableStatus));
    }

    const MH_STATUS removeStatus = MH_RemoveHook(target);
    if (removeStatus != MH_OK && removeStatus != MH_ERROR_NOT_CREATED) {
        debuglog::Write(
            "MH_RemoveHook failed for %s: %s (%d)",
            hookName ? hookName : "<unnamed>",
            MH_StatusToString(removeStatus),
            static_cast<int>(removeStatus));
    }

    target = nullptr;
}

} // namespace minhook
