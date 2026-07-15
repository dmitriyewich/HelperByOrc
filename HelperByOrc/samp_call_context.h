#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>

enum class SampCallOrigin : std::uint8_t {
    Samp,
    HelperByOrc,
    External,
    Unknown,
};

struct SampCallContext {
    SampCallOrigin origin = SampCallOrigin::Unknown;
    std::uintptr_t returnAddress = 0;
    std::uintptr_t moduleBase = 0;

    bool IsExternal() const {
        return origin == SampCallOrigin::External;
    }
};

inline SampCallContext ResolveSampCallContext(
    const void* returnAddress,
    HMODULE sampModule,
    HMODULE helperModule) {
    SampCallContext context;
    context.returnAddress = reinterpret_cast<std::uintptr_t>(returnAddress);
    if (!returnAddress) {
        return context;
    }

    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(returnAddress, &region, sizeof(region)) != sizeof(region) || !region.AllocationBase) {
        return context;
    }

    const auto module = static_cast<HMODULE>(region.AllocationBase);
    context.moduleBase = reinterpret_cast<std::uintptr_t>(module);
    if (module == sampModule) {
        context.origin = SampCallOrigin::Samp;
    } else if (module == helperModule) {
        context.origin = SampCallOrigin::HelperByOrc;
    } else if (module == GetModuleHandleW(nullptr)) {
        context.origin = SampCallOrigin::Unknown;
    } else {
        context.origin = SampCallOrigin::External;
    }
    return context;
}
