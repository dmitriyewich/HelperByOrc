#pragma once

#include <MinHook.h>

#include "debug_log.h"

namespace minhook {

inline bool Initialize() {
    const MH_STATUS status = MH_Initialize();
    if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) {
        return true;
    }

    debuglog::WriteError("MH_Initialize failed: %s (%d)", MH_StatusToString(status), static_cast<int>(status));
    return false;
}

inline void Uninitialize() {
    const MH_STATUS status = MH_Uninitialize();
    if (status == MH_OK || status == MH_ERROR_NOT_INITIALIZED) {
        return;
    }

    debuglog::WriteError("MH_Uninitialize failed: %s (%d)", MH_StatusToString(status), static_cast<int>(status));
}

inline bool CreateHook(void* target, void* detour, void** original, const char* hookName) {
    if (!target || !detour || !original) {
        debuglog::WriteError("MH_CreateHook skipped for %s: invalid arguments", hookName ? hookName : "<unnamed>");
        return false;
    }

    const MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status == MH_OK) {
        return true;
    }

    debuglog::WriteError(
        "MH_CreateHook failed for %s: %s (%d)",
        hookName ? hookName : "<unnamed>",
        MH_StatusToString(status),
        static_cast<int>(status));
    return false;
}

inline bool EnableHook(void* target, const char* hookName) {
    if (!target) {
        debuglog::WriteError("MH_EnableHook skipped for %s: target is null", hookName ? hookName : "<unnamed>");
        return false;
    }

    const MH_STATUS status = MH_EnableHook(target);
    if (status == MH_OK) {
        return true;
    }

    debuglog::WriteError(
        "MH_EnableHook failed for %s: %s (%d)",
        hookName ? hookName : "<unnamed>",
        MH_StatusToString(status),
        static_cast<int>(status));
    return false;
}

inline bool DisableHook(void* target, const char* hookName) {
    if (!target) {
        return true;
    }

    const MH_STATUS status = MH_DisableHook(target);
    if (status == MH_OK || status == MH_ERROR_DISABLED || status == MH_ERROR_NOT_CREATED) {
        return true;
    }

    debuglog::WriteError(
        "MH_DisableHook failed for %s: %s (%d)",
        hookName ? hookName : "<unnamed>",
        MH_StatusToString(status),
        static_cast<int>(status));
    return false;
}

inline void RemoveHook(void*& target, const char* hookName) {
    if (!target) {
        return;
    }

    const MH_STATUS status = MH_RemoveHook(target);
    if (status != MH_OK && status != MH_ERROR_NOT_CREATED) {
        debuglog::WriteError(
            "MH_RemoveHook failed for %s: %s (%d)",
            hookName ? hookName : "<unnamed>",
            MH_StatusToString(status),
            static_cast<int>(status));
    }
    target = nullptr;
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
        debuglog::WriteError(
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

    DisableHook(target, hookName);
    RemoveHook(target, hookName);
}

} // namespace minhook
