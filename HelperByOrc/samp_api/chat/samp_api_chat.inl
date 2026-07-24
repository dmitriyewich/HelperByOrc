bool SampApi::is_chat_opened() {
    std::uint32_t input = 0;
    if (!ResolveChatInput(input) || input == 0) {
        return false;
    }

    std::int32_t opened = 0;
    return SafeRead(input + main_offsets.CInput_Opened.Get(currentVersion_), opened) && opened == 1;
}

bool SampApi::IsChatVisible() {
    return getChatMode() != 0;
}

int SampApi::Local_ID() {
    std::uint32_t pool = 0;
    if (!ResolvePedPool(pool) || pool == 0) {
        return -1;
    }

    std::int16_t localId = -1;
    if (!SafeRead(pool + main_offsets.SAMP_SLOCALPLAYERID_OFFSET.Get(currentVersion_), localId)) {
        return -1;
    }

    return localId;
}

std::pair<bool, int> SampApi::TryResolvePlayerIdByPedFast(const void* ped) {
    if (!ped) {
        return { false, -1 };
    }

    std::uint32_t pool = 0;
    int localId = -1;
    if (!ResolvePlayerPoolState(pool, localId)) {
        return { false, -1 };
    }

    if (ped == FindPlayerPed()) {
        if (localId >= 0 && IsPlayerConnectedInPool(pool, localId, localId)) {
            return { true, localId };
        }
        return { false, -1 };
    }

    // Frame-sensitive callers must not enter getPedID() or repeat the native lookup with a C++ slot scan.
    const auto address = GetAddress(main_offsets.ID_Find);
    if (address == 0) {
        return { false, -1 };
    }

    const auto findId = reinterpret_cast<IdFindFn>(address);
    std::uint16_t rawId = kInvalidSampPlayerId;
    if (!CallIdFind(findId, reinterpret_cast<void*>(pool), ped, rawId)
        || rawId == kInvalidSampPlayerId) {
        return { false, -1 };
    }

    const int id = static_cast<int>(rawId);
    if (!IsPlayerConnectedInPool(pool, localId, id)) {
        return { false, -1 };
    }
    return { true, id };
}

std::pair<bool, int> SampApi::getPedID(const void* ped) {
    if (!ped) {
        return { false, -1 };
    }

    struct GetPedIdPerfStats {
        std::uint64_t windowStartMs = 0;
        std::uint64_t calls = 0;
        std::uint64_t scanFallbacks = 0;
        std::uint64_t scanHits = 0;
        std::uint64_t totalScanMs = 0;
        std::uint64_t maxScanMs = 0;
    };

    static GetPedIdPerfStats perfStats{};
    auto beginGetPedIdPerf = []() {
        constexpr std::uint64_t kGetPedIdPerfWindowMs = 5000;
        const std::uint64_t nowMs = GetTickCount64();
        if (perfStats.windowStartMs == 0 || nowMs < perfStats.windowStartMs) {
            perfStats.windowStartMs = nowMs;
        }

        const std::uint64_t windowMs = nowMs - perfStats.windowStartMs;
        if (windowMs >= kGetPedIdPerfWindowMs) {
            if (perfStats.scanFallbacks > 0) {
                const double avgScanMs =
                    static_cast<double>(perfStats.totalScanMs) / static_cast<double>(perfStats.scanFallbacks);
                debuglog::WriteInfo(
                    "[samp][getPedID][perf] window=%llums calls=%llu scanFallbacks=%llu scanHits=%llu avgScan=%.2fms maxScan=%llums",
                    static_cast<unsigned long long>(windowMs),
                    static_cast<unsigned long long>(perfStats.calls),
                    static_cast<unsigned long long>(perfStats.scanFallbacks),
                    static_cast<unsigned long long>(perfStats.scanHits),
                    avgScanMs,
                    static_cast<unsigned long long>(perfStats.maxScanMs));
            }
            perfStats = GetPedIdPerfStats{};
            perfStats.windowStartMs = nowMs;
        }
        ++perfStats.calls;
    };
    auto recordScanFallbackPerf = [](bool hit, std::uint64_t elapsedMs) {
        ++perfStats.scanFallbacks;
        if (hit) {
            ++perfStats.scanHits;
        }
        perfStats.totalScanMs += elapsedMs;
        perfStats.maxScanMs = std::max(perfStats.maxScanMs, elapsedMs);
    };
    beginGetPedIdPerf();

    const auto pedAddress = reinterpret_cast<std::uintptr_t>(ped);
    auto logPedIdResolve = [&](const char* source, std::uint16_t rawId, int resolvedId, const char* reason) {
        struct LastLogState {
            std::uintptr_t ped = 0;
            std::uint16_t rawId = 0;
            int resolvedId = -2;
            const char* source = nullptr;
            const char* reason = nullptr;
        };

        static LastLogState lastLog{};
        static std::uint64_t lastNoisyTraceMs = 0;
        static std::uint32_t suppressedNoisyTraces = 0;
        if (lastLog.ped == pedAddress && lastLog.rawId == rawId && lastLog.resolvedId == resolvedId
            && lastLog.source == source && lastLog.reason == reason) {
            return;
        }

        lastLog = LastLogState{ pedAddress, rawId, resolvedId, source, reason };
        std::uint32_t suppressedForLog = 0;
        if (std::strcmp(reason, "id-find-not-found") == 0) {
            constexpr std::uint64_t kNoisyTraceIntervalMs = 1000;
            const std::uint64_t nowMs = GetTickCount64();
            if (lastNoisyTraceMs != 0 && nowMs - lastNoisyTraceMs < kNoisyTraceIntervalMs) {
                ++suppressedNoisyTraces;
                return;
            }
            lastNoisyTraceMs = nowMs;
            suppressedForLog = suppressedNoisyTraces;
            suppressedNoisyTraces = 0;
        }

        debuglog::WriteInfo(
            "[samp][getPedID] ped=0x%08X rawId=%u resolvedId=%d source=%s reason=%s suppressed=%u",
            static_cast<unsigned int>(pedAddress),
            static_cast<unsigned int>(rawId),
            resolvedId,
            source,
            reason,
            static_cast<unsigned int>(suppressedForLog));
    };

    const int localId = Local_ID();
    if (ped == FindPlayerPed()) {
        if (localId >= 0 && localId <= kMaxSampPlayerId) {
            return { true, localId };
        }

        logPedIdResolve("local", kInvalidSampPlayerId, localId, "invalid-local-id");
        return { false, -1 };
    }

    std::uint32_t pool = 0;
    const auto address = GetAddress(main_offsets.ID_Find);
    if (!ResolvePedPool(pool) || pool == 0 || address == 0) {
        return { false, -1 };
    }

    auto isUsablePlayerId = [&](int id) {
        return id >= 0 && id <= kMaxSampPlayerId && IsConnected(id);
    };

    const auto findId = reinterpret_cast<IdFindFn>(address);
    std::uint16_t rawId = kInvalidSampPlayerId;

    const bool idFindOk = CallIdFind(findId, reinterpret_cast<void*>(pool), ped, rawId);
    const char* rejectReason = idFindOk ? "id-find-not-found" : "id-find-exception";
    if (idFindOk && rawId != kInvalidSampPlayerId) {
        const int id = static_cast<int>(rawId);
        if (isUsablePlayerId(id)) {
            return { true, id };
        }

        rejectReason = id > kMaxSampPlayerId ? "id-find-out-of-range" : "id-find-disconnected";
    }

    auto matchesTargetPed = [&](std::uint32_t gtaPed) {
        return gtaPed != 0 && static_cast<std::uintptr_t>(gtaPed) == pedAddress;
    };

    auto tryRemoteDataPed = [&](std::uint32_t remoteData) {
        if (remoteData == 0) {
            return false;
        }

        const std::uint32_t actorOffset = main_offsets.pSAMP_Actor.Get(currentVersion_);
        std::uint32_t sampPed = 0;
        if (!SafeRead(remoteData + actorOffset, sampPed) || sampPed == 0) {
            return false;
        }

        return matchesTargetPed(ReadGamePedFromSampPed(sampPed, currentVersion_));
    };

    auto trySampPedField = [&](std::uint32_t owner, std::uint32_t fieldOffset) {
        if (owner == 0) {
            return false;
        }

        std::uint32_t sampPed = 0;
        if (!SafeRead(owner + fieldOffset, sampPed) || sampPed == 0) {
            return false;
        }

        return matchesTargetPed(ReadGamePedFromSampPed(sampPed, currentVersion_));
    };

    auto trySlotRemoteData = [&](int id) {
        std::uint32_t slotPointer = 0;
        if (!SafeRead(
                pool + main_offsets.SAMP_PREMOTEPLAYER_OFFSET.Get(currentVersion_) + (static_cast<std::uint32_t>(id) * 4),
                slotPointer)
            || slotPointer == 0) {
            return false;
        }

        std::uint32_t remoteData = 0;
        return ResolveRemotePlayerData(slotPointer, remoteData) && tryRemoteDataPed(remoteData);
    };

    auto tryRemotePlayer = [&](int id) {
        std::uint32_t remotePlayer = 0;
        if (!ResolveRemotePlayer(id, remotePlayer, false, nullptr) || remotePlayer == 0) {
            return false;
        }

        std::uint32_t remoteData = 0;
        if (ResolveRemotePlayerData(remotePlayer, remoteData) && tryRemoteDataPed(remoteData)) {
            return true;
        }

        const std::uint32_t remotePedOffset = GetRemotePlayerPedOffset(currentVersion_);
        if (trySampPedField(remotePlayer, remotePedOffset)) {
            return true;
        }

        constexpr std::uint32_t kRemotePlayerScanLimit = 0x300;
        for (std::uint32_t fieldOffset = 0; fieldOffset + sizeof(std::uint32_t) <= kRemotePlayerScanLimit; ++fieldOffset) {
            if (fieldOffset == remotePedOffset) {
                continue;
            }

            if (trySampPedField(remotePlayer, fieldOffset)) {
                return true;
            }
        }

        return false;
    };

    const std::uint64_t scanStartedAtMs = GetTickCount64();
    for (int id = 0; id <= kMaxSampPlayerId; ++id) {
        if ((localId >= 0 && id == localId) || !IsConnected(id)) {
            continue;
        }

        if (trySlotRemoteData(id) || tryRemotePlayer(id)) {
            recordScanFallbackPerf(true, GetTickCount64() - scanStartedAtMs);
            logPedIdResolve("scan", rawId, id, rejectReason);
            return { true, id };
        }
    }
    recordScanFallbackPerf(false, GetTickCount64() - scanStartedAtMs);

    if (!idFindOk || rawId != kInvalidSampPlayerId) {
        logPedIdResolve("none", rawId, -1, rejectReason);
    }
    return { false, -1 };
}

bool SampApi::ResolveRemotePlayer(int id, std::uint32_t& remotePlayer, bool trace, const char* traceLabel) {
    remotePlayer = 0;
    const char* const label = traceLabel ? traceLabel : "trace";

    if (trace) {
        debuglog::WriteInfo("[%s] ResolveRemotePlayer begin id=%d", label, id);
    }

    if (id < 0 || id > 1003) {
        if (trace) {
            debuglog::WriteError("[%s] ResolveRemotePlayer invalid id=%d", label, id);
        }
        return false;
    }

    const int localId = Local_ID();
    if (localId >= 0 && id == localId) {
        if (trace) {
            debuglog::WriteInfo("[%s] ResolveRemotePlayer requested local player id=%d", label, id);
        }
        return false;
    }

    if (!IsConnected(id)) {
        if (trace) {
            debuglog::WriteInfo("[%s] ResolveRemotePlayer IsConnected=false id=%d", label, id);
        }
        return false;
    }

    std::uint32_t pool = 0;
    if (!ResolvePedPool(pool) || pool == 0) {
        if (trace) {
            debuglog::WriteError("[%s] ResolveRemotePlayer ResolvePedPool failed", label);
        }
        return false;
    }
    if (trace) {
        debuglog::WriteInfo("[%s] ResolveRemotePlayer pedPool=0x%08X", label, pool);
    }

    std::uint32_t slotPointer = 0;
    if (!SafeRead(pool + main_offsets.SAMP_PREMOTEPLAYER_OFFSET.Get(currentVersion_) + (static_cast<std::uint32_t>(id) * 4), slotPointer)
        || slotPointer == 0) {
        if (trace) {
            debuglog::WriteError("[%s] ResolveRemotePlayer slot read failed id=%d", label, id);
        }
        return false;
    }
    if (trace) {
        debuglog::WriteInfo("[%s] ResolveRemotePlayer slotPointer=0x%08X", label, slotPointer);
    }

    if (LooksLikeRemotePlayerPointer(slotPointer, currentVersion_, id)) {
        remotePlayer = slotPointer;
        if (trace) {
            debuglog::WriteInfo("[%s] ResolveRemotePlayer direct remotePlayer=0x%08X", label, remotePlayer);
        }
        return true;
    }
    if (trace) {
        debuglog::WriteInfo("[%s] ResolveRemotePlayer slot is not direct CRemotePlayer", label);
    }

    const std::uint32_t remotePlayerOffset = GetPlayerInfoRemotePlayerOffset(currentVersion_);
    if (trace) {
        debuglog::WriteInfo("[%s] ResolveRemotePlayer trying indirect offset=0x%X", label, remotePlayerOffset);
    }

    std::uint32_t indirectRemotePlayer = 0;
    if (!SafeRead(slotPointer + remotePlayerOffset, indirectRemotePlayer) || indirectRemotePlayer == 0) {
        if (trace) {
            debuglog::WriteError("[%s] ResolveRemotePlayer indirect read failed", label);
        }
        return false;
    }
    if (trace) {
        debuglog::WriteInfo("[%s] ResolveRemotePlayer indirectRemotePlayer=0x%08X", label, indirectRemotePlayer);
    }

    if (!LooksLikeRemotePlayerPointer(indirectRemotePlayer, currentVersion_, id)) {
        if (trace) {
            debuglog::WriteError("[%s] ResolveRemotePlayer indirect pointer failed id validation", label);
        }
        return false;
    }

    remotePlayer = indirectRemotePlayer;
    if (trace) {
        debuglog::WriteInfo("[%s] ResolveRemotePlayer resolved indirect remotePlayer=0x%08X", label, remotePlayer);
    }
    return true;
}

bool SampApi::ResolveRemotePlayerData(std::uint32_t remotePlayer, std::uint32_t& remoteData) const {
    remoteData = 0;
    if (remotePlayer == 0) {
        return false;
    }

    const std::uint32_t offset = main_offsets.SAMP_REMOTEPLAYERDATA_OFFSET.Get(currentVersion_);
    if (offset == 0) {
        remoteData = remotePlayer;
        return true;
    }

    return SafeRead(remotePlayer + offset, remoteData) && remoteData != 0;
}

const void* SampApi::GetPlayerPedPointer(int id, bool trace, const char* traceLabel, bool allowScanFallback) {
    const char* const label = traceLabel ? traceLabel : "trace";

    if (trace) {
        debuglog::WriteInfo("[%s] GetPlayerPedPointer begin id=%d", label, id);
    }

    if (id < 0 || id > 1003) {
        if (trace) {
            debuglog::WriteError("[%s] GetPlayerPedPointer invalid id=%d", label, id);
        }
        return nullptr;
    }

    const int localId = Local_ID();
    if (localId >= 0 && id == localId) {
        CPed* const localPed = FindPlayerPed();
        if (trace) {
            debuglog::WriteInfo("[%s] GetPlayerPedPointer local ped=0x%08X", label, reinterpret_cast<std::uint32_t>(localPed));
        }
        return localPed;
    }

    if (!IsConnected(id)) {
        if (trace) {
            debuglog::WriteInfo("[%s] GetPlayerPedPointer IsConnected=false id=%d", label, id);
        }
        return nullptr;
    }

    auto tryResolveRemoteDataOwner = [&](std::uint32_t owner, bool logAttempt) -> const void* {
        if (owner == 0) {
            return nullptr;
        }

        std::uint32_t remoteData = 0;
        if (!ResolveRemotePlayerData(owner, remoteData)) {
            if (trace && logAttempt) {
                debuglog::WriteError(
                    "[%s] GetPlayerPedPointer remoteData fallback ResolveRemotePlayerData failed owner=0x%08X",
                    label,
                    owner);
            }
            return nullptr;
        }

        const std::uint32_t actorOffset = main_offsets.pSAMP_Actor.Get(currentVersion_);
        std::uint32_t sampPed = 0;
        if (!SafeRead(remoteData + actorOffset, sampPed) || sampPed == 0) {
            if (trace && logAttempt) {
                debuglog::WriteError(
                    "[%s] GetPlayerPedPointer remoteData fallback sampPed read failed remoteData=0x%08X actorOffset=0x%X",
                    label,
                    remoteData,
                    actorOffset);
            }
            return nullptr;
        }

        const std::uint32_t gtaPed = ReadGamePedFromSampPed(sampPed, currentVersion_);
        if (gtaPed == 0) {
            if (trace && logAttempt) {
                debuglog::WriteError(
                    "[%s] GetPlayerPedPointer remoteData fallback ReadGamePedFromSampPed failed sampPed=0x%08X",
                    label,
                    sampPed);
            }
            return nullptr;
        }

        if (trace && logAttempt) {
            debuglog::WriteInfo(
                "[%s] GetPlayerPedPointer remoteData fallback owner=0x%08X remoteData=0x%08X sampPed=0x%08X gtaPed=0x%08X",
                label,
                owner,
                remoteData,
                sampPed,
                gtaPed);
        }

        return reinterpret_cast<const void*>(gtaPed);
    };

    auto tryResolveViaRemoteData = [&](bool requireIdMatch, bool logAttempt) -> const void* {
        std::uint32_t pool = 0;
        if (!ResolvePedPool(pool) || pool == 0) {
            if (trace && logAttempt) {
                debuglog::WriteError("[%s] GetPlayerPedPointer remoteData fallback ResolvePedPool failed", label);
            }
            return nullptr;
        }

        std::uint32_t slotPointer = 0;
        if (!SafeRead(
                pool + main_offsets.SAMP_PREMOTEPLAYER_OFFSET.Get(currentVersion_) + (static_cast<std::uint32_t>(id) * 4),
                slotPointer)
            || slotPointer == 0) {
            if (trace && logAttempt) {
                debuglog::WriteError("[%s] GetPlayerPedPointer remoteData fallback slot read failed", label);
            }
            return nullptr;
        }

        const void* const ped = tryResolveRemoteDataOwner(slotPointer, logAttempt);
        if (!ped) {
            return nullptr;
        }

        if (requireIdMatch) {
            const auto [matched, matchedId] = getPedID(ped);
            if (!matched || matchedId != id) {
                if (trace && logAttempt) {
                    debuglog::WriteError(
                        "[%s] GetPlayerPedPointer remoteData fallback id validation failed matched=%d matchedId=%d expected=%d",
                        label,
                        matched ? 1 : 0,
                        matchedId,
                        id);
                }
                return nullptr;
            }
        }

        return ped;
    };

    const bool supportsRemoteDataFallback = currentVersion_ != Version::Unknown && currentVersion_ != Version::E;

    std::uint32_t remotePlayer = 0;
    if (!ResolveRemotePlayer(id, remotePlayer, trace, traceLabel) || remotePlayer == 0) {
        if (trace) {
            debuglog::WriteError("[%s] GetPlayerPedPointer ResolveRemotePlayer failed", label);
        }

        if (supportsRemoteDataFallback && allowScanFallback) {
            if (trace) {
                debuglog::WriteInfo("[%s] GetPlayerPedPointer trying remoteData fallback after ResolveRemotePlayer fail", label);
            }

            if (const void* ped = tryResolveViaRemoteData(true, true)) {
                if (trace) {
                    debuglog::WriteInfo(
                        "[%s] GetPlayerPedPointer remoteData fallback success ped=0x%08X",
                        label,
                        reinterpret_cast<std::uint32_t>(ped));
                }
                return ped;
            }
        }

        return nullptr;
    }
    if (trace) {
        debuglog::WriteInfo("[%s] GetPlayerPedPointer remotePlayer=0x%08X", label, remotePlayer);
    }

    const std::uint32_t remotePedOffset = GetRemotePlayerPedOffset(currentVersion_);
    if (trace) {
        debuglog::WriteInfo("[%s] GetPlayerPedPointer primary remotePedOffset=0x%X", label, remotePedOffset);
    }

    auto tryResolveGamePed = [&](std::uint32_t pedFieldOffset, bool requireIdMatch, bool logAttempt) -> const void* {
        std::uint32_t sampPed = 0;
        if (!SafeRead(remotePlayer + pedFieldOffset, sampPed) || sampPed == 0) {
            if (trace && logAttempt) {
                debuglog::WriteError("[%s] GetPlayerPedPointer offset=0x%X sampPed read failed", label, pedFieldOffset);
            }
            return nullptr;
        }
        if (trace && logAttempt) {
            debuglog::WriteInfo("[%s] GetPlayerPedPointer offset=0x%X sampPed=0x%08X", label, pedFieldOffset, sampPed);
        }

        const std::uint32_t gtaPed = ReadGamePedFromSampPed(sampPed, currentVersion_);
        if (gtaPed == 0) {
            if (trace && logAttempt) {
                debuglog::WriteError("[%s] GetPlayerPedPointer offset=0x%X ReadGamePedFromSampPed failed", label, pedFieldOffset);
            }
            return nullptr;
        }
        if (trace && logAttempt) {
            debuglog::WriteInfo("[%s] GetPlayerPedPointer offset=0x%X gtaPed=0x%08X", label, pedFieldOffset, gtaPed);
        }

        if (requireIdMatch) {
            const auto [matched, matchedId] = getPedID(reinterpret_cast<const void*>(gtaPed));
            if (!matched || matchedId != id) {
                if (trace && logAttempt) {
                    debuglog::WriteError(
                        "[%s] GetPlayerPedPointer offset=0x%X id validation failed matched=%d matchedId=%d expected=%d",
                        label,
                        pedFieldOffset,
                        matched ? 1 : 0,
                        matchedId,
                        id);
                }
                return nullptr;
            }
            if (trace && logAttempt) {
                debuglog::WriteInfo("[%s] GetPlayerPedPointer offset=0x%X id validation ok", label, pedFieldOffset);
            }
        }

        return reinterpret_cast<const void*>(gtaPed);
    };

    if (const void* ped = tryResolveGamePed(remotePedOffset, false, true)) {
        if (trace) {
            debuglog::WriteInfo("[%s] GetPlayerPedPointer primary success ped=0x%08X", label, reinterpret_cast<std::uint32_t>(ped));
        }
        return ped;
    }
    if (trace) {
        debuglog::WriteInfo("[%s] GetPlayerPedPointer primary path failed; entering scan fallback", label);
    }

    if (supportsRemoteDataFallback) {
        if (const void* ped = tryResolveRemoteDataOwner(remotePlayer, true)) {
            if (trace) {
                debuglog::WriteInfo(
                    "[%s] GetPlayerPedPointer remotePlayerData fallback success ped=0x%08X",
                    label,
                    reinterpret_cast<std::uint32_t>(ped));
            }
            return ped;
        }
    }

    if (supportsRemoteDataFallback && allowScanFallback) {
        if (const void* ped = tryResolveViaRemoteData(true, true)) {
            if (trace) {
                debuglog::WriteInfo(
                    "[%s] GetPlayerPedPointer remoteData fallback success after primary fail ped=0x%08X",
                    label,
                    reinterpret_cast<std::uint32_t>(ped));
            }
            return ped;
        }
    }

    if (!allowScanFallback) {
        return nullptr;
    }

    // R5-era CRemotePlayer layouts differ from the older headers we bundle.
    // Fall back to a narrow packed-struct scan and accept only the candidate
    // whose GTA ped resolves back to the expected SA:MP player id.
    constexpr std::uint32_t kRemotePlayerScanLimit = 0x300;
    for (std::uint32_t pedFieldOffset = 0; pedFieldOffset + sizeof(std::uint32_t) <= kRemotePlayerScanLimit; ++pedFieldOffset) {
        if (pedFieldOffset == remotePedOffset) {
            continue;
        }

        if (const void* ped = tryResolveGamePed(pedFieldOffset, true, false)) {
            if (trace) {
                debuglog::WriteInfo(
                    "[%s] GetPlayerPedPointer scan success offset=0x%X ped=0x%08X",
                    label,
                    pedFieldOffset,
                    reinterpret_cast<std::uint32_t>(ped));
            }
            return ped;
        }
    }

    if (trace) {
        debuglog::WriteError("[%s] GetPlayerPedPointer scan fallback failed", label);
    }
    return nullptr;
}

bool SampApi::GetPlayerPosition(int id, float& x, float& y, float& z) {
    x = 0.0f;
    y = 0.0f;
    z = 0.0f;

    if (id < 0 || id > 1003) {
        return false;
    }

    const int localId = Local_ID();
    if (localId >= 0 && id == localId) {
        if (auto* localPed = FindPlayerPed()) {
            return TryReadPedPosition(reinterpret_cast<std::uint32_t>(localPed), x, y, z);
        }
        return false;
    }

    const void* gtaPed = GetPlayerPedPointer(id);
    if (!gtaPed) {
        return false;
    }

    return TryReadPedPosition(reinterpret_cast<std::uint32_t>(gtaPed), x, y, z);
}

int SampApi::getChatMode() {
    std::uint32_t chat = 0;
    if (!ResolveChat(chat) || chat == 0) {
        return 0;
    }

    std::int32_t mode = 0;
    if (!SafeRead(chat + main_offsets.CChatMode.Get(currentVersion_), mode)) {
        return 0;
    }

    return mode;
}

bool SampApi::SetPageSize(int pageSize) {
    constexpr int minPageSize = 10;
    int maxPageSize = 20;

    const auto maxOffset = main_offsets.PageSize_MAX.Get(currentVersion_);
    if (maxOffset != 0) {
        std::int8_t maxValue = 0;
        if (SafeRead(ModuleBase() + maxOffset, maxValue) && maxValue > 0) {
            maxPageSize = maxValue;
        }
    }

    if (pageSize < minPageSize || pageSize > maxPageSize) {
        SetError("Page size is out of supported range");
        return false;
    }

    std::uint32_t chat = 0;
    const auto address = GetAddress(main_offsets.SetPageSize);
    if (!ResolveChat(chat) || chat == 0 || address == 0) {
        SetError("Chat page-size controller is not available");
        return false;
    }

    const auto setPageSize = reinterpret_cast<SetPageSizeFn>(address);
    if (!CallSetPageSize(setPageSize, reinterpret_cast<void*>(chat), pageSize)) {
        SetError("Failed to set chat page size");
        return false;
    }

    ClearError();
    return true;
}

SampApi::HealthAndArmour SampApi::GetHealthAndArmour(int id) {
    HealthAndArmour result;
    const int localId = Local_ID();

    if (id == localId) {
        if (auto* player = FindPlayerPed()) {
            result.health = player->m_fHealth;
            result.armour = player->m_fArmour;
            result.valid = true;
        }
        return result;
    }

    std::uint32_t remotePlayer = 0;
    if (!ResolveRemotePlayer(id, remotePlayer, false, nullptr) || remotePlayer == 0) {
        return result;
    }

    std::uint32_t remoteData = 0;
    if (!ResolveRemotePlayerData(remotePlayer, remoteData)) {
        return result;
    }

    float health = result.health;
    float armour = result.armour;
    if (!SafeRead(remoteData + main_offsets.SAMP_REMOTEPLAYERDATA_HEALTH_OFFSET.Get(currentVersion_), health)
        || !SafeRead(remoteData + main_offsets.SAMP_REMOTEPLAYERDATA_ARMOR_OFFSET.Get(currentVersion_), armour)) {
        return result;
    }

    result.health = health;
    result.armour = armour;
    result.valid = true;
    return result;
}

bool SampApi::send_chat_internal(std::string_view text, bool alreadyDecoded) {
    const auto previewText = [](std::string_view value) {
        constexpr std::size_t kMaxPreviewLength = 96;
        if (value.size() <= kMaxPreviewLength) {
            return std::string(value);
        }
        return std::string(value.substr(0, kMaxPreviewLength - 3)) + "...";
    };
    const std::string rawPreview = previewText(text);
    debuglog::WriteInfo(
        "SampApi::send_chat begin decoded=%d len=%llu text=%s",
        alreadyDecoded ? 1 : 0,
        static_cast<unsigned long long>(text.size()),
        rawPreview.c_str());

    if (text.empty()) {
        debuglog::WriteError("SampApi::send_chat failed: input text is empty");
        SetError("Chat text is empty");
        return false;
    }

    if (!isSAMPInitilizeLua()) {
        debuglog::WriteError("SampApi::send_chat failed during init: %s", lastError().c_str());
        return false;
    }

    std::string gameText = PrepareOutgoingText(text, alreadyDecoded, false);
    const std::string gamePreview = previewText(gameText);
    if (gameText.empty()) {
        debuglog::WriteError("SampApi::send_chat failed: prepared text is empty");
        SetError("Chat text is empty after conversion");
        return false;
    }

    const auto sendCommandAddress = GetAddress(main_offsets.CInput_Send);
    const auto sendSayAddress = GetAddress(main_offsets.CInput_SendSay);
    if (sendCommandAddress == 0 || sendSayAddress == 0) {
        debuglog::WriteError(
            "SampApi::send_chat failed: send routines unavailable send=0x%08X sendSay=0x%08X",
            static_cast<unsigned>(sendCommandAddress),
            static_cast<unsigned>(sendSayAddress));
        SetError("SAMP chat send routines are not available");
        return false;
    }

    if (!gameText.empty() && gameText.front() == '/') {
        debuglog::WriteInfo(
            "SampApi::send_chat branch=command send=0x%08X len=%llu text=%s",
            static_cast<unsigned>(sendCommandAddress),
            static_cast<unsigned long long>(gameText.size()),
            gamePreview.c_str());
        std::uint32_t input = 0;
        if (!ResolveChatInput(input) || input == 0) {
            debuglog::WriteError("SampApi::send_chat failed: command input pointer is null");
            SetError("SAMP chat input pointer is null");
            return false;
        }

        debuglog::WriteInfo("SampApi::send_chat command input=0x%08X", input);
        const auto sendCommand = reinterpret_cast<SendInputFn>(sendCommandAddress);
        if (!CallSendInput(sendCommand, reinterpret_cast<void*>(input), gameText.c_str())) {
            debuglog::WriteError(
                "SampApi::send_chat failed: CallSendInput(command) raised SEH input=0x%08X send=0x%08X text=%s",
                input,
                static_cast<unsigned>(sendCommandAddress),
                gamePreview.c_str());
            SetError("Failed to send SAMP command");
            return false;
        }
    } else {
        auto* playerPed = FindPlayerPed();
        debuglog::WriteInfo(
            "SampApi::send_chat branch=chat sendSay=0x%08X ped=%p len=%llu text=%s",
            static_cast<unsigned>(sendSayAddress),
            playerPed,
            static_cast<unsigned long long>(gameText.size()),
            gamePreview.c_str());
        if (!playerPed) {
            debuglog::WriteError("SampApi::send_chat failed: player ped was not found");
            SetError("Player ped was not found");
            return false;
        }

        const auto sendSay = reinterpret_cast<SendInputFn>(sendSayAddress);
        if (!CallSendInput(sendSay, playerPed, gameText.c_str())) {
            debuglog::WriteError(
                "SampApi::send_chat failed: CallSendInput(chat) raised SEH ped=%p sendSay=0x%08X text=%s",
                playerPed,
                static_cast<unsigned>(sendSayAddress),
                gamePreview.c_str());
            SetError("Failed to send SAMP chat message");
            return false;
        }
    }

    debuglog::WriteInfo("SampApi::send_chat ok");
    ClearError();
    return true;
}

bool SampApi::send_chat(std::string_view text, bool alreadyDecoded) {
    return send_chat_internal(text, alreadyDecoded);
}

bool SampApi::process_chat_input(std::string_view text, bool alreadyDecoded) {
    if (text.empty()) {
        SetError("Chat text is empty");
        return false;
    }

    if (!isSAMPInitilizeLua()) {
        return false;
    }

    std::string utf8Text = alreadyDecoded ? textencoding::GameToUtf8(text) : std::string(text);
    std::string gameText = textencoding::Utf8ToGame(utf8Text);
    if (gameText.empty()) {
        SetError("Chat text is empty after conversion");
        return false;
    }

    if (TryProcessChatInputViaChatAsi(utf8Text)) {
        ClearError();
        return true;
    }

    const auto processInputAddr = GetAddress(main_offsets.CInput_ProcessInput);
    if (processInputAddr == 0) {
        SetError("CInput::ProcessInput is not available");
        return false;
    }

    const auto setTextAddr = GetAddress(main_offsets.CDXUTEditBox_SetText);
    if (setTextAddr == 0) {
        SetError("CDXUTEditBox::SetText is not available");
        return false;
    }

    const std::uintptr_t editBox = SAMP_CHAT_INPUT_INFO_OFFSET_func_test();
    if (editBox == 0) {
        SetError("SAMP chat edit box is not available");
        return false;
    }

    const auto setText = reinterpret_cast<SetEditboxTextFn>(setTextAddr);
    if (!CallSetEditboxText(setText, reinterpret_cast<void*>(editBox), gameText.data())) {
        SetError("Failed to set chat input text");
        return false;
    }

    std::uint32_t input = 0;
    if (!ResolveChatInput(input) || input == 0) {
        SetError("SAMP chat input pointer is null");
        return false;
    }

    const auto processInput = reinterpret_cast<ProcessInputFn>(processInputAddr);
    if (!CallProcessInput(processInput, reinterpret_cast<void*>(input))) {
        SetError("Failed to call CInput::ProcessInput");
        return false;
    }

    ClearError();
    return true;
}

bool SampApi::memoryAddMessageSamp(std::string_view text, std::uint32_t color, bool alreadyDecoded) {
    if (!isSAMPInitilizeLua()) {
        return false;
    }

    const std::string sourceText = text.empty() ? std::string("nil") : std::string(text);
    std::string gameText = PrepareOutgoingText(sourceText, alreadyDecoded, true);
    if (gameText.empty()) {
        gameText = "nil";
    }

    std::uint32_t chat = 0;
    const auto address = GetAddress(main_offsets.AddEntry);
    if (!ResolveChat(chat) || chat == 0 || address == 0) {
        SetError("SAMP chat pointer is null");
        return false;
    }

    const bool hookActive = SampHooks::IsChatAddEntryHookActive();
    if (!hookActive && gameText.size() > samp_local_chat::kMaxEntryTextBytes) {
        const std::size_t originalSize = gameText.size();
        gameText.resize(samp_local_chat::SafeTruncationLength(gameText));
        debuglog::WriteInfo(
            "SampApi::memoryAddMessageSamp truncated bytes=%llu to=%llu capacity=%llu",
            static_cast<unsigned long long>(originalSize),
            static_cast<unsigned long long>(gameText.size()),
            static_cast<unsigned long long>(samp_local_chat::kMaxEntryTextBytes));
    } else if (!hookActive && gameText.size() > samp_local_chat::kNativeEntryTextBytes) {
        debuglog::WriteInfo(
            "SampApi::memoryAddMessageSamp route=extended-add-entry bytes=%llu capacity=%llu",
            static_cast<unsigned long long>(gameText.size()),
            static_cast<unsigned long long>(samp_local_chat::kMaxEntryTextBytes));
    }

    SampHooks::NativeCallFailure failure{};
    if (!SampHooks::CallChatAddEntry(
            address,
            reinterpret_cast<void*>(chat),
            samp_local_chat::kLocalMessageType,
            gameText.c_str(),
            gameText.size(),
            static_cast<unsigned long>(samp_local_chat::RgbaToArgb(color)),
            failure)) {
        debuglog::WriteError(
            "SampApi::memoryAddMessageSamp AddEntry failed bytes=%llu code=0x%08lX address=%p",
            static_cast<unsigned long long>(gameText.size()),
            static_cast<unsigned long>(failure.code),
            failure.address);
        SetError("Failed to add chat message to SAMP");
        return false;
    }

    ClearError();
    return true;
}

bool SampApi::ClearChatLocal(int lines, std::uint32_t color) {
    if (lines <= 0) {
        return true;
    }

    if (!isSAMPInitilizeLua()) {
        return false;
    }

    bool ok = true;
    for (int i = 0; i < lines; ++i) {
        ok = memoryAddMessageSamp(" ", color, true) && ok;
    }
    return ok;
}

SampApi::ChatEntry SampApi::pGetChatString(int index) {
    ChatEntry result;
    if (index < 0
        || static_cast<std::size_t>(index) >= samp_local_chat::kChatEntryCount
        || currentVersion_ == Version::Unknown) {
        return result;
    }

    const auto textOffset = main_offsets.CHAT_TEXT_OFFSET.Get(currentVersion_);
    const auto prefixOffset = main_offsets.CHAT_PREFIX_TEXT_OFFSET.Get(currentVersion_);
    const auto colorOffset = main_offsets.CHAT_COLOR_OFFSET.Get(currentVersion_);
    const auto prefixColorOffset = main_offsets.CHAT_PREFIX_COLOR_OFFSET.Get(currentVersion_);
    if (textOffset == 0 || colorOffset <= textOffset || textOffset <= prefixOffset) {
        return result;
    }

    std::uint32_t chat = 0;
    if (!ResolveChat(chat) || chat == 0) {
        return result;
    }

    const auto entry =
        chat
        + samp_local_chat::kChatEntryBaseOffset
        + (static_cast<std::uintptr_t>(index) * samp_local_chat::kChatEntrySize);
    if (!IsReadableMemory(entry, samp_local_chat::kChatEntrySize)) {
        return result;
    }

    const auto prefixSize = static_cast<std::size_t>(textOffset - prefixOffset);
    const auto textSize = static_cast<std::size_t>(colorOffset - textOffset);
    std::uint32_t colorValue = 0;
    SafeRead(entry + colorOffset, colorValue);

    std::uint8_t prefixColorFlag = 0;
    SafeRead(entry + prefixColorOffset, prefixColorFlag);

    result.text = PrepareIncomingText(SafeReadCString(entry + textOffset, textSize));
    result.prefix = PrepareIncomingText(SafeReadCString(entry + prefixOffset, prefixSize));
    result.color = colorValue;

    std::uint32_t prefixColor = 0;
    if (prefixColorFlag > 0) {
        SafeRead(entry + prefixColorOffset, prefixColor);
    }
    result.prefixColor = prefixColor;
    result.valid = true;
    return result;
}

void SampApi::ResetChatAsiInputDiscovery(HMODULE module) {
    RemoveChatAsiInputCallbackHook();
    chatAsiInputDiscovery_ = {};
    chatAsiInputDiscovery_.module = module;
    std::lock_guard<std::mutex> lock(chatAsiCursorMutex_);
    chatAsiPendingCursor_ = {};
}

bool SampApi::EnsureChatAsiInputDiscovery() {
#if !HELPERBYORC_ENABLE_CHAT_ASI_INTEGRATION
    if (!chatAsiInputDiscovery_.attempted) {
        debuglog::WriteInfo("SampApi::_chat.asi input discovery skipped: Arizona integration disabled in this build");
    }
    chatAsiInputDiscovery_ = {};
    chatAsiInputDiscovery_.attempted = true;
    return false;
#else

    const HMODULE module = GetModuleHandleA("_chat.asi");
    if (!module) {
        ResetChatAsiInputDiscovery();
        return false;
    }

    if (chatAsiInputDiscovery_.module != module) {
        ResetChatAsiInputDiscovery(module);
    }

    if (chatAsiInputDiscovery_.attempted) {
        return chatAsiInputDiscovery_.inputWriter != 0 && chatAsiInputDiscovery_.inputBuffer != 0;
    }

    chatAsiInputDiscovery_.attempted = true;
    const std::uint64_t scanStartedAt = GetTickCount64();

    std::uintptr_t imageBase = 0;
    std::uintptr_t imageEnd = 0;
    std::vector<ModuleSectionRange> sections;
    if (!TryGetModuleSections(module, imageBase, imageEnd, sections)) {
        debuglog::WriteError(
            "SampApi::_chat.asi input discovery failed: invalid module layout module=%p elapsed=%llums",
            module,
            static_cast<unsigned long long>(GetTickCount64() - scanStartedAt));
        return false;
    }

    const std::uintptr_t inputLabel = FindRuntimeAsciiStringLiteral(module, "###input");

    if (inputLabel == 0) {
        debuglog::WriteError(
            "SampApi::_chat.asi input discovery failed: ###input was not found module=%p imageSize=0x%X elapsed=%llums",
            module,
            static_cast<unsigned>(imageEnd - imageBase),
            static_cast<unsigned long long>(GetTickCount64() - scanStartedAt));
        return false;
    }

    const auto refs = FindPushImmediateRefs(sections, inputLabel);
    std::uintptr_t fallbackRef = 0;
    std::uintptr_t fallbackWrapper = 0;
    std::uintptr_t fallbackCallback = 0;
    std::uintptr_t fallbackBuffer = 0;
    std::uintptr_t fallbackWriter = 0;
    std::uintptr_t fallbackWriterDirty = 0;

    for (const auto ref : refs) {
        const std::uintptr_t inputWrapper = FindNearbyWrapperCall(ref, imageBase, imageEnd);
        const std::uintptr_t inputCallback = FindExecutablePushBefore(ref, sections, imageBase, imageEnd);
        const std::uintptr_t inputBuffer = FindWritablePushBefore(ref, sections);
        if (inputWrapper == 0 || inputCallback == 0 || inputBuffer == 0) {
            continue;
        }

        std::uintptr_t inputWriter = 0;
        std::uintptr_t writerDirtyFlag = 0;
        if (!FindChatAsiWriterForBuffer(sections, imageBase, imageEnd, inputBuffer, inputWriter, writerDirtyFlag)) {
            continue;
        }

        if (!ValidateChatAsiInputCallback(sections, inputCallback, writerDirtyFlag)) {
            debuglog::WriteError(
                "SampApi::_chat.asi input discovery rejected callback=0x%08X ref=0x%08X writer_dirty=0x%08X",
                static_cast<unsigned>(inputCallback),
                static_cast<unsigned>(ref),
                static_cast<unsigned>(writerDirtyFlag));
            continue;
        }

        if (fallbackWriter == 0) {
            fallbackRef = ref;
            fallbackWrapper = inputWrapper;
            fallbackCallback = inputCallback;
            fallbackBuffer = inputBuffer;
            fallbackWriter = inputWriter;
            fallbackWriterDirty = writerDirtyFlag;
        }

        std::uintptr_t inputSubmit = 0;
        std::uintptr_t submitDirtyFlag = 0;
        if (!FindChatAsiSubmitForBuffer(sections, imageBase, imageEnd, inputBuffer, inputSubmit, submitDirtyFlag)) {
            continue;
        }

        chatAsiInputDiscovery_.inputLabel = inputLabel;
        chatAsiInputDiscovery_.inputWrapper = inputWrapper;
        chatAsiInputDiscovery_.inputCallback = inputCallback;
        chatAsiInputDiscovery_.inputBuffer = inputBuffer;
        chatAsiInputDiscovery_.inputWriter = inputWriter;
        chatAsiInputDiscovery_.inputSubmit = inputSubmit;

        debuglog::WriteInfo(
            "SampApi::_chat.asi input discovery ok module=%p imageSize=0x%X elapsed=%llums refs=%llu labelRva=0x%X refRva=0x%X wrapperRva=0x%X callbackRva=0x%X bufferRva=0x%X writerRva=0x%X submitRva=0x%X writerDirtyRva=0x%X submitDirtyRva=0x%X",
            module,
            static_cast<unsigned>(imageEnd - imageBase),
            static_cast<unsigned long long>(GetTickCount64() - scanStartedAt),
            static_cast<unsigned long long>(refs.size()),
            static_cast<unsigned>(inputLabel - imageBase),
            static_cast<unsigned>(ref - imageBase),
            static_cast<unsigned>(inputWrapper - imageBase),
            static_cast<unsigned>(inputCallback - imageBase),
            static_cast<unsigned>(inputBuffer - imageBase),
            static_cast<unsigned>(inputWriter - imageBase),
            static_cast<unsigned>(inputSubmit - imageBase),
            static_cast<unsigned>(writerDirtyFlag ? writerDirtyFlag - imageBase : 0),
            static_cast<unsigned>(submitDirtyFlag ? submitDirtyFlag - imageBase : 0));
        debuglog::WriteInfo(
            "SampApi::_chat.asi input validation bytes callback=[%s] writer=[%s] submit=[%s]",
            FormatModuleCodeBytes(sections, inputCallback, 16).c_str(),
            FormatModuleCodeBytes(sections, inputWriter, 16).c_str(),
            FormatModuleCodeBytes(sections, inputSubmit, 16).c_str());
        return true;
    }

    if (fallbackWriter != 0) {
        chatAsiInputDiscovery_.inputLabel = inputLabel;
        chatAsiInputDiscovery_.inputWrapper = fallbackWrapper;
        chatAsiInputDiscovery_.inputCallback = fallbackCallback;
        chatAsiInputDiscovery_.inputBuffer = fallbackBuffer;
        chatAsiInputDiscovery_.inputWriter = fallbackWriter;

        debuglog::WriteInfo(
            "SampApi::_chat.asi input discovery partial module=%p imageSize=0x%X elapsed=%llums refs=%llu labelRva=0x%X refRva=0x%X wrapperRva=0x%X callbackRva=0x%X bufferRva=0x%X writerRva=0x%X writerDirtyRva=0x%X submit=not_found",
            module,
            static_cast<unsigned>(imageEnd - imageBase),
            static_cast<unsigned long long>(GetTickCount64() - scanStartedAt),
            static_cast<unsigned long long>(refs.size()),
            static_cast<unsigned>(inputLabel - imageBase),
            static_cast<unsigned>(fallbackRef - imageBase),
            static_cast<unsigned>(fallbackWrapper - imageBase),
            static_cast<unsigned>(fallbackCallback - imageBase),
            static_cast<unsigned>(fallbackBuffer - imageBase),
            static_cast<unsigned>(fallbackWriter - imageBase),
            static_cast<unsigned>(fallbackWriterDirty ? fallbackWriterDirty - imageBase : 0));
        debuglog::WriteInfo(
            "SampApi::_chat.asi input validation bytes callback=[%s] writer=[%s] submit=[not_found]",
            FormatModuleCodeBytes(sections, fallbackCallback, 16).c_str(),
            FormatModuleCodeBytes(sections, fallbackWriter, 16).c_str());
        return true;
    }

    debuglog::WriteError(
        "SampApi::_chat.asi input discovery failed: no valid ###input -> wrapper -> buffer -> writer chain was found module=%p imageSize=0x%X refs=%llu labelRva=0x%X elapsed=%llums",
        module,
        static_cast<unsigned>(imageEnd - imageBase),
        static_cast<unsigned long long>(refs.size()),
        static_cast<unsigned>(inputLabel - imageBase),
        static_cast<unsigned long long>(GetTickCount64() - scanStartedAt));
    return false;
#endif
}

bool SampApi::TrySetChatInputTextViaChatAsi(std::string_view utf8Text) {
#if !HELPERBYORC_ENABLE_CHAT_ASI_INTEGRATION
    (void)utf8Text;
    return false;
#else
    if (!EnsureChatAsiInputDiscovery()) {
        return false;
    }

    if (chatAsiInputDiscovery_.inputWriter == 0) {
        return false;
    }

    const auto writer = reinterpret_cast<ChatAsiInputWriterFn>(chatAsiInputDiscovery_.inputWriter);
    if (!CallChatAsiInputWriter(writer, utf8Text.data(), utf8Text.size(), 0)) {
        debuglog::WriteError(
            "SampApi::_chat.asi input writer SEH fail writer=0x%08X len=%llu",
            static_cast<unsigned>(chatAsiInputDiscovery_.inputWriter),
            static_cast<unsigned long long>(utf8Text.size()));
        return false;
    }

    const std::string bufferSummary = ChatAsiInputBufferSummary(chatAsiInputDiscovery_.inputBuffer);
    debuglog::WriteInfo(
        "SampApi::_chat.asi input writer ok writer=0x%08X len=%llu %s",
        static_cast<unsigned>(chatAsiInputDiscovery_.inputWriter),
        static_cast<unsigned long long>(utf8Text.size()),
        bufferSummary.c_str());
    return true;
#endif
}

bool SampApi::EnsureChatAsiInputCallbackHook() {
#if !HELPERBYORC_ENABLE_CHAT_ASI_INTEGRATION
    return false;
#else
    if (chatAsiInputDiscovery_.inputCallback == 0) {
        SetError("_chat.asi input callback was not discovered");
        return false;
    }

    void* const target = reinterpret_cast<void*>(chatAsiInputDiscovery_.inputCallback);
    if (chatAsiInputCallbackHookTarget_ == target) {
        return true;
    }

    RemoveChatAsiInputCallbackHook();

    g_chatAsiInputCallbackOwner = this;
    g_chatAsiInputCallbackOriginal = nullptr;
    if (!minhook::CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&SampApi::ChatAsiInputCallbackDetour),
            &g_chatAsiInputCallbackOriginal,
            "SampApi::_chat.asi InputText callback")) {
        g_chatAsiInputCallbackOwner = nullptr;
        g_chatAsiInputCallbackOriginal = nullptr;
        SetError("_chat.asi input callback hook install failed");
        return false;
    }

    chatAsiInputCallbackHookTarget_ = target;
    debuglog::WriteInfo(
        "SampApi::_chat.asi input callback hook installed callback=0x%08X",
        static_cast<unsigned>(chatAsiInputDiscovery_.inputCallback));
    return true;
#endif
}

void SampApi::RemoveChatAsiInputCallbackHook() {
#if HELPERBYORC_ENABLE_CHAT_ASI_INTEGRATION
    if (chatAsiInputCallbackHookTarget_) {
        minhook::DisableAndRemoveHook(chatAsiInputCallbackHookTarget_, "SampApi::_chat.asi InputText callback");
        debuglog::WriteInfo("SampApi::_chat.asi input callback hook removed");
    }
    if (g_chatAsiInputCallbackOwner == this) {
        g_chatAsiInputCallbackOwner = nullptr;
        g_chatAsiInputCallbackOriginal = nullptr;
    }
#endif
    chatAsiInputCallbackHookTarget_ = nullptr;
}

int __cdecl SampApi::ChatAsiInputCallbackDetour(void* callbackData) {
    int result = 0;
    if (g_chatAsiInputCallbackOriginal) {
        result = g_chatAsiInputCallbackOriginal(callbackData);
    }

    if (SampApi* owner = g_chatAsiInputCallbackOwner) {
        owner->ApplyChatAsiPendingCursor(callbackData);
    }
    return result;
}

void SampApi::ApplyChatAsiPendingCursor(void* callbackData) {
#if !HELPERBYORC_ENABLE_CHAT_ASI_INTEGRATION
    (void)callbackData;
#else
    ChatAsiPendingCursor pending{};
    {
        std::lock_guard<std::mutex> lock(chatAsiCursorMutex_);
        if (!chatAsiPendingCursor_.valid) {
            return;
        }
        pending = chatAsiPendingCursor_;
    }

    const auto data = reinterpret_cast<std::uintptr_t>(callbackData);
    if (!IsReadableMemory(data, kChatAsiCallbackDataSize) || !IsWritableMemory(data, kChatAsiCallbackDataSize)) {
        debuglog::WriteError(
            "SampApi::_chat.asi cursor callback apply failed: callback data unavailable data=0x%08X region=%s seq=%llu",
            static_cast<unsigned>(data),
            MemoryRegionSummary(data).c_str(),
            static_cast<unsigned long long>(pending.sequence));
        std::lock_guard<std::mutex> lock(chatAsiCursorMutex_);
        if (chatAsiPendingCursor_.sequence == pending.sequence) {
            chatAsiPendingCursor_ = {};
        }
        return;
    }

    std::uint32_t eventFlag = 0;
    if (!SafeRead(data + kChatAsiCallbackOffsetEventFlag, eventFlag)) {
        return;
    }

    if (eventFlag != kChatAsiInputTextCallbackAlways) {
        return;
    }

    std::uintptr_t textBuffer = 0;
    std::int32_t textLength = 0;
    std::int32_t bufferSize = 0;
    const bool bufferOk = SafeRead(data + kChatAsiCallbackOffsetBuf, textBuffer);
    const bool lengthOk = SafeRead(data + kChatAsiCallbackOffsetBufTextLen, textLength);
    const bool sizeOk = SafeRead(data + kChatAsiCallbackOffsetBufSize, bufferSize);
    if (!bufferOk || !lengthOk || !sizeOk || textLength < 0 || bufferSize <= 0
        || textLength >= bufferSize || textLength > static_cast<std::int32_t>(kDefaultTextLimit)
        || !IsReadableMemory(textBuffer, static_cast<std::size_t>(textLength) + 1)) {
        debuglog::WriteError(
            "SampApi::_chat.asi cursor callback apply failed: invalid callback data data=0x%08X event=0x%X buf_ok=%d buf=0x%08X len_ok=%d len=%d size_ok=%d size=%d seq=%llu data_region=%s buf_region=%s",
            static_cast<unsigned>(data),
            static_cast<unsigned>(eventFlag),
            bufferOk ? 1 : 0,
            static_cast<unsigned>(textBuffer),
            lengthOk ? 1 : 0,
            textLength,
            sizeOk ? 1 : 0,
            bufferSize,
            static_cast<unsigned long long>(pending.sequence),
            MemoryRegionSummary(data).c_str(),
            MemoryRegionSummary(textBuffer).c_str());
        std::lock_guard<std::mutex> lock(chatAsiCursorMutex_);
        if (chatAsiPendingCursor_.sequence == pending.sequence) {
            chatAsiPendingCursor_ = {};
        }
        return;
    }

    int start = std::clamp(pending.start, 0, textLength);
    int finish = std::clamp(pending.finish, 0, textLength);
    if (finish < start) {
        std::swap(start, finish);
    }

    const bool cursorOk = SafeWrite(data + kChatAsiCallbackOffsetCursorPos, finish);
    const bool selectionStartOk = SafeWrite(data + kChatAsiCallbackOffsetSelectionStart, start);
    const bool selectionEndOk = SafeWrite(data + kChatAsiCallbackOffsetSelectionEnd, finish);
    if (!cursorOk || !selectionStartOk || !selectionEndOk) {
        debuglog::WriteError(
            "SampApi::_chat.asi cursor callback apply failed: write failed data=0x%08X cursor_ok=%d sel_start_ok=%d sel_end_ok=%d start=%d finish=%d len=%d seq=%llu",
            static_cast<unsigned>(data),
            cursorOk ? 1 : 0,
            selectionStartOk ? 1 : 0,
            selectionEndOk ? 1 : 0,
            start,
            finish,
            textLength,
            static_cast<unsigned long long>(pending.sequence));
        std::lock_guard<std::mutex> lock(chatAsiCursorMutex_);
        if (chatAsiPendingCursor_.sequence == pending.sequence) {
            chatAsiPendingCursor_ = {};
        }
        return;
    }

    debuglog::WriteInfo(
        "SampApi::_chat.asi cursor callback apply ok callback=0x%08X data=0x%08X start=%d finish=%d len=%d buf=0x%08X seq=%llu",
        static_cast<unsigned>(chatAsiInputDiscovery_.inputCallback),
        static_cast<unsigned>(data),
        start,
        finish,
        textLength,
        static_cast<unsigned>(textBuffer),
        static_cast<unsigned long long>(pending.sequence));

    std::lock_guard<std::mutex> lock(chatAsiCursorMutex_);
    if (chatAsiPendingCursor_.sequence == pending.sequence) {
        chatAsiPendingCursor_ = {};
    }
#endif
}

bool SampApi::SetChatAsiInputCursor(int start, int finish) {
#if !HELPERBYORC_ENABLE_CHAT_ASI_INTEGRATION
    (void)start;
    (void)finish;
    SetError("_chat.asi cursor callback is disabled in this build");
    return false;
#else
    if (!EnsureChatAsiInputDiscovery()) {
        SetError("_chat.asi input discovery is unavailable");
        return false;
    }

    const int maxPosition = ClampChatAsiCursorRange(chatAsiInputDiscovery_.inputBuffer, start, finish);

    if (!EnsureChatAsiInputCallbackHook()) {
        debuglog::WriteError(
            "SampApi::_chat.asi cursor schedule failed: callback hook unavailable module=%p callback=0x%08X buffer=0x%08X writer=0x%08X submit=0x%08X start=%d finish=%d max=%d error=%s",
            chatAsiInputDiscovery_.module,
            static_cast<unsigned>(chatAsiInputDiscovery_.inputCallback),
            static_cast<unsigned>(chatAsiInputDiscovery_.inputBuffer),
            static_cast<unsigned>(chatAsiInputDiscovery_.inputWriter),
            static_cast<unsigned>(chatAsiInputDiscovery_.inputSubmit),
            start,
            finish,
            maxPosition,
            lastError_.c_str());
        return false;
    }

    std::uint64_t sequence = 0;
    {
        std::lock_guard<std::mutex> lock(chatAsiCursorMutex_);
        sequence = ++chatAsiCursorSequence_;
        chatAsiPendingCursor_.valid = true;
        chatAsiPendingCursor_.start = start;
        chatAsiPendingCursor_.finish = finish;
        chatAsiPendingCursor_.maxPosition = maxPosition;
        chatAsiPendingCursor_.sequence = sequence;
    }

    const std::string bufferSummary = ChatAsiInputBufferSummary(chatAsiInputDiscovery_.inputBuffer);
    debuglog::WriteInfo(
        "SampApi::_chat.asi cursor scheduled callback=0x%08X start=%d finish=%d max=%d seq=%llu %s",
        static_cast<unsigned>(chatAsiInputDiscovery_.inputCallback),
        start,
        finish,
        maxPosition,
        static_cast<unsigned long long>(sequence),
        bufferSummary.c_str());
    ClearError();
    return true;
#endif
}

bool SampApi::TryProcessChatInputViaChatAsi(std::string_view utf8Text) {
#if !HELPERBYORC_ENABLE_CHAT_ASI_INTEGRATION
    (void)utf8Text;
    return false;
#else
    if (!EnsureChatAsiInputDiscovery()) {
        return false;
    }

    if (chatAsiInputDiscovery_.inputSubmit == 0) {
        if (TrySetChatInputTextViaChatAsi(utf8Text)) {
            debuglog::WriteInfo(
                "SampApi::_chat.asi input prefilled before SAMP ProcessInput fallback len=%llu",
                static_cast<unsigned long long>(utf8Text.size()));
        }
        return false;
    }

    if (!TrySetChatInputTextViaChatAsi(utf8Text)) {
        return false;
    }

    const auto submit = reinterpret_cast<ChatAsiInputSubmitFn>(chatAsiInputDiscovery_.inputSubmit);
    if (!CallChatAsiInputSubmit(submit, 1)) {
        debuglog::WriteError(
            "SampApi::_chat.asi input submit SEH fail submit=0x%08X len=%llu",
            static_cast<unsigned>(chatAsiInputDiscovery_.inputSubmit),
            static_cast<unsigned long long>(utf8Text.size()));
        return false;
    }

    return true;
#endif
}

bool SampApi::Set_ChatInputText(std::string_view text, bool openInput, bool alreadyDecoded) {
    std::string utf8Text = alreadyDecoded ? textencoding::GameToUtf8(text) : std::string(text);
    utf8Text = ApplyTextTransform(utf8Text);
    std::string gameText = textencoding::Utf8ToGame(utf8Text);

    if (openInput && !pCInput_Open_Close(true)) {
        return false;
    }

    if (TrySetChatInputTextViaChatAsi(utf8Text)) {
        ClearError();
        return true;
    }

    const auto address = GetAddress(main_offsets.CDXUTEditBox_SetText);
    const std::uintptr_t editBox = SAMP_CHAT_INPUT_INFO_OFFSET_func_test();
    if (address == 0 || editBox == 0) {
        SetError("Failed to set chat input text");
        return false;
    }

    const auto setText = reinterpret_cast<SetEditboxTextFn>(address);
    if (!CallSetEditboxText(setText, reinterpret_cast<void*>(editBox), gameText.data())) {
        SetError("Failed to set chat input text");
        return false;
    }

    ClearError();
    return true;
}
