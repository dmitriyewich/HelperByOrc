bool SampApi::is_chat_opened() {
    std::uint32_t input = 0;
    if (!ResolveChatInput(input) || input == 0) {
        return false;
    }

    std::int32_t opened = 0;
    return SafeRead(input + main_offsets.CInput_Opened.Get(currentVersion_), opened) && opened == 1;
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

std::pair<bool, int> SampApi::getPedID(const void* ped) {
    if (!ped) {
        return { false, -1 };
    }

    if (ped == FindPlayerPed()) {
        return { true, Local_ID() };
    }

    std::uint32_t pool = 0;
    const auto address = GetAddress(main_offsets.ID_Find);
    if (!ResolvePedPool(pool) || pool == 0 || address == 0) {
        return { false, -1 };
    }

    const auto findId = reinterpret_cast<IdFindFn>(address);
    int id = 65535;

    CallIdFind(findId, reinterpret_cast<void*>(pool), ped, id);

    return id != 65535 ? std::make_pair(true, id) : std::make_pair(false, -1);
}

bool SampApi::ResolveRemotePlayer(int id, std::uint32_t& remotePlayer, bool trace, const char* traceLabel) {
    remotePlayer = 0;
    const char* const label = traceLabel ? traceLabel : "trace";

    if (trace) {
        debuglog::Write("[%s] ResolveRemotePlayer begin id=%d", label, id);
    }

    if (id < 0 || id > 1003) {
        if (trace) {
            debuglog::Write("[%s] ResolveRemotePlayer invalid id=%d", label, id);
        }
        return false;
    }

    const int localId = Local_ID();
    if (localId >= 0 && id == localId) {
        if (trace) {
            debuglog::Write("[%s] ResolveRemotePlayer requested local player id=%d", label, id);
        }
        return false;
    }

    if (!IsConnected(id)) {
        if (trace) {
            debuglog::Write("[%s] ResolveRemotePlayer IsConnected=false id=%d", label, id);
        }
        return false;
    }

    std::uint32_t pool = 0;
    if (!ResolvePedPool(pool) || pool == 0) {
        if (trace) {
            debuglog::Write("[%s] ResolveRemotePlayer ResolvePedPool failed", label);
        }
        return false;
    }
    if (trace) {
        debuglog::Write("[%s] ResolveRemotePlayer pedPool=0x%08X", label, pool);
    }

    std::uint32_t slotPointer = 0;
    if (!SafeRead(pool + main_offsets.SAMP_PREMOTEPLAYER_OFFSET.Get(currentVersion_) + (static_cast<std::uint32_t>(id) * 4), slotPointer)
        || slotPointer == 0) {
        if (trace) {
            debuglog::Write("[%s] ResolveRemotePlayer slot read failed id=%d", label, id);
        }
        return false;
    }
    if (trace) {
        debuglog::Write("[%s] ResolveRemotePlayer slotPointer=0x%08X", label, slotPointer);
    }

    if (LooksLikeRemotePlayerPointer(slotPointer, currentVersion_, id)) {
        remotePlayer = slotPointer;
        if (trace) {
            debuglog::Write("[%s] ResolveRemotePlayer direct remotePlayer=0x%08X", label, remotePlayer);
        }
        return true;
    }
    if (trace) {
        debuglog::Write("[%s] ResolveRemotePlayer slot is not direct CRemotePlayer", label);
    }

    const std::uint32_t remotePlayerOffset = GetPlayerInfoRemotePlayerOffset(currentVersion_);
    if (trace) {
        debuglog::Write("[%s] ResolveRemotePlayer trying indirect offset=0x%X", label, remotePlayerOffset);
    }

    std::uint32_t indirectRemotePlayer = 0;
    if (!SafeRead(slotPointer + remotePlayerOffset, indirectRemotePlayer) || indirectRemotePlayer == 0) {
        if (trace) {
            debuglog::Write("[%s] ResolveRemotePlayer indirect read failed", label);
        }
        return false;
    }
    if (trace) {
        debuglog::Write("[%s] ResolveRemotePlayer indirectRemotePlayer=0x%08X", label, indirectRemotePlayer);
    }

    if (!LooksLikeRemotePlayerPointer(indirectRemotePlayer, currentVersion_, id)) {
        if (trace) {
            debuglog::Write("[%s] ResolveRemotePlayer indirect pointer failed id validation", label);
        }
        return false;
    }

    remotePlayer = indirectRemotePlayer;
    if (trace) {
        debuglog::Write("[%s] ResolveRemotePlayer resolved indirect remotePlayer=0x%08X", label, remotePlayer);
    }
    return true;
}

const void* SampApi::GetPlayerPedPointer(int id, bool trace, const char* traceLabel) {
    const char* const label = traceLabel ? traceLabel : "trace";

    if (trace) {
        debuglog::Write("[%s] GetPlayerPedPointer begin id=%d", label, id);
    }

    if (id < 0 || id > 1003) {
        if (trace) {
            debuglog::Write("[%s] GetPlayerPedPointer invalid id=%d", label, id);
        }
        return nullptr;
    }

    const int localId = Local_ID();
    if (localId >= 0 && id == localId) {
        CPed* const localPed = FindPlayerPed();
        if (trace) {
            debuglog::Write("[%s] GetPlayerPedPointer local ped=0x%08X", label, reinterpret_cast<std::uint32_t>(localPed));
        }
        return localPed;
    }

    if (!IsConnected(id)) {
        if (trace) {
            debuglog::Write("[%s] GetPlayerPedPointer IsConnected=false id=%d", label, id);
        }
        return nullptr;
    }

    auto tryResolveViaRemoteData = [&](bool requireIdMatch, bool logAttempt) -> const void* {
        std::uint32_t pool = 0;
        if (!ResolvePedPool(pool) || pool == 0) {
            if (trace && logAttempt) {
                debuglog::Write("[%s] GetPlayerPedPointer remoteData fallback ResolvePedPool failed", label);
            }
            return nullptr;
        }

        std::uint32_t slotPointer = 0;
        if (!SafeRead(
                pool + main_offsets.SAMP_PREMOTEPLAYER_OFFSET.Get(currentVersion_) + (static_cast<std::uint32_t>(id) * 4),
                slotPointer)
            || slotPointer == 0) {
            if (trace && logAttempt) {
                debuglog::Write("[%s] GetPlayerPedPointer remoteData fallback slot read failed", label);
            }
            return nullptr;
        }

        const std::uint32_t remoteDataOffset = main_offsets.SAMP_REMOTEPLAYERDATA_OFFSET.Get(currentVersion_);
        std::uint32_t remoteData = 0;
        if (!SafeRead(slotPointer + remoteDataOffset, remoteData) || remoteData == 0) {
            if (trace && logAttempt) {
                debuglog::Write(
                    "[%s] GetPlayerPedPointer remoteData fallback remoteData read failed slot=0x%08X offset=0x%X",
                    label,
                    slotPointer,
                    remoteDataOffset);
            }
            return nullptr;
        }

        const std::uint32_t actorOffset = main_offsets.pSAMP_Actor.Get(currentVersion_);
        std::uint32_t sampPed = 0;
        if (!SafeRead(remoteData + actorOffset, sampPed) || sampPed == 0) {
            if (trace && logAttempt) {
                debuglog::Write(
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
                debuglog::Write(
                    "[%s] GetPlayerPedPointer remoteData fallback ReadGamePedFromSampPed failed sampPed=0x%08X",
                    label,
                    sampPed);
            }
            return nullptr;
        }

        if (trace && logAttempt) {
            debuglog::Write(
                "[%s] GetPlayerPedPointer remoteData fallback slot=0x%08X remoteData=0x%08X sampPed=0x%08X gtaPed=0x%08X",
                label,
                slotPointer,
                remoteData,
                sampPed,
                gtaPed);
        }

        if (requireIdMatch) {
            const auto [matched, matchedId] = getPedID(reinterpret_cast<const void*>(gtaPed));
            if (!matched || matchedId != id) {
                if (trace && logAttempt) {
                    debuglog::Write(
                        "[%s] GetPlayerPedPointer remoteData fallback id validation failed matched=%d matchedId=%d expected=%d",
                        label,
                        matched ? 1 : 0,
                        matchedId,
                        id);
                }
                return nullptr;
            }
        }

        return reinterpret_cast<const void*>(gtaPed);
    };

    std::uint32_t remotePlayer = 0;
    if (!ResolveRemotePlayer(id, remotePlayer, trace, traceLabel) || remotePlayer == 0) {
        if (trace) {
            debuglog::Write("[%s] GetPlayerPedPointer ResolveRemotePlayer failed", label);
        }

        if (currentVersion_ == Version::DL_R1) {
            if (trace) {
                debuglog::Write("[%s] GetPlayerPedPointer trying DL remoteData fallback", label);
            }

            if (const void* ped = tryResolveViaRemoteData(true, true)) {
                if (trace) {
                    debuglog::Write(
                        "[%s] GetPlayerPedPointer DL remoteData fallback success ped=0x%08X",
                        label,
                        reinterpret_cast<std::uint32_t>(ped));
                }
                return ped;
            }
        }

        return nullptr;
    }
    if (trace) {
        debuglog::Write("[%s] GetPlayerPedPointer remotePlayer=0x%08X", label, remotePlayer);
    }

    const std::uint32_t remotePedOffset = GetRemotePlayerPedOffset(currentVersion_);
    if (trace) {
        debuglog::Write("[%s] GetPlayerPedPointer primary remotePedOffset=0x%X", label, remotePedOffset);
    }

    auto tryResolveGamePed = [&](std::uint32_t pedFieldOffset, bool requireIdMatch, bool logAttempt) -> const void* {
        std::uint32_t sampPed = 0;
        if (!SafeRead(remotePlayer + pedFieldOffset, sampPed) || sampPed == 0) {
            if (trace && logAttempt) {
                debuglog::Write("[%s] GetPlayerPedPointer offset=0x%X sampPed read failed", label, pedFieldOffset);
            }
            return nullptr;
        }
        if (trace && logAttempt) {
            debuglog::Write("[%s] GetPlayerPedPointer offset=0x%X sampPed=0x%08X", label, pedFieldOffset, sampPed);
        }

        const std::uint32_t gtaPed = ReadGamePedFromSampPed(sampPed, currentVersion_);
        if (gtaPed == 0) {
            if (trace && logAttempt) {
                debuglog::Write("[%s] GetPlayerPedPointer offset=0x%X ReadGamePedFromSampPed failed", label, pedFieldOffset);
            }
            return nullptr;
        }
        if (trace && logAttempt) {
            debuglog::Write("[%s] GetPlayerPedPointer offset=0x%X gtaPed=0x%08X", label, pedFieldOffset, gtaPed);
        }

        if (requireIdMatch) {
            const auto [matched, matchedId] = getPedID(reinterpret_cast<const void*>(gtaPed));
            if (!matched || matchedId != id) {
                if (trace && logAttempt) {
                    debuglog::Write(
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
                debuglog::Write("[%s] GetPlayerPedPointer offset=0x%X id validation ok", label, pedFieldOffset);
            }
        }

        return reinterpret_cast<const void*>(gtaPed);
    };

    if (const void* ped = tryResolveGamePed(remotePedOffset, false, true)) {
        if (trace) {
            debuglog::Write("[%s] GetPlayerPedPointer primary success ped=0x%08X", label, reinterpret_cast<std::uint32_t>(ped));
        }
        return ped;
    }
    if (trace) {
        debuglog::Write("[%s] GetPlayerPedPointer primary path failed; entering scan fallback", label);
    }

    if (currentVersion_ == Version::DL_R1) {
        if (const void* ped = tryResolveViaRemoteData(true, true)) {
            if (trace) {
                debuglog::Write(
                    "[%s] GetPlayerPedPointer DL remoteData fallback success after primary fail ped=0x%08X",
                    label,
                    reinterpret_cast<std::uint32_t>(ped));
            }
            return ped;
        }
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
                debuglog::Write(
                    "[%s] GetPlayerPedPointer scan success offset=0x%X ped=0x%08X",
                    label,
                    pedFieldOffset,
                    reinterpret_cast<std::uint32_t>(ped));
            }
            return ped;
        }
    }

    if (trace) {
        debuglog::Write("[%s] GetPlayerPedPointer scan fallback failed", label);
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

    std::int8_t mode = 0;
    if (!SafeRead(chat + 0x8, mode)) {
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

    std::uint32_t pool = 0;
    if (!ResolvePedPool(pool) || pool == 0 || id < 0) {
        return result;
    }

    std::uint32_t remotePlayer = 0;
    if (!SafeRead(pool + main_offsets.SAMP_PREMOTEPLAYER_OFFSET.Get(currentVersion_) + (id * 4), remotePlayer)
        || remotePlayer == 0) {
        return result;
    }

    std::uint32_t remoteData = 0;
    if (!SafeRead(remotePlayer + main_offsets.SAMP_REMOTEPLAYERDATA_OFFSET.Get(currentVersion_), remoteData)
        || remoteData == 0) {
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
    if (text.empty()) {
        SetError("Chat text is empty");
        return false;
    }

    if (!isSAMPInitilizeLua()) {
        return false;
    }

    std::string gameText = PrepareOutgoingText(text, alreadyDecoded, false);
    if (gameText.empty()) {
        SetError("Chat text is empty after conversion");
        return false;
    }

    const auto sendCommandAddress = GetAddress(main_offsets.CInput_Send);
    const auto sendSayAddress = GetAddress(main_offsets.CInput_SendSay);
    if (sendCommandAddress == 0 || sendSayAddress == 0) {
        SetError("SAMP chat send routines are not available");
        return false;
    }

    if (!gameText.empty() && gameText.front() == '/') {
        std::uint32_t input = 0;
        if (!ResolveChatInput(input) || input == 0) {
            SetError("SAMP chat input pointer is null");
            return false;
        }

        const auto sendCommand = reinterpret_cast<SendInputFn>(sendCommandAddress);
        if (!CallSendInput(sendCommand, reinterpret_cast<void*>(input), gameText.c_str())) {
            SetError("Failed to send SAMP command");
            return false;
        }
    } else {
        auto* playerPed = FindPlayerPed();
        if (!playerPed) {
            SetError("Player ped was not found");
            return false;
        }

        const auto sendSay = reinterpret_cast<SendInputFn>(sendSayAddress);
        if (!CallSendInput(sendSay, playerPed, gameText.c_str())) {
            SetError("Failed to send SAMP chat message");
            return false;
        }
    }

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

    std::string gameText = PrepareOutgoingText(text, alreadyDecoded, false);
    if (gameText.empty()) {
        SetError("Chat text is empty after conversion");
        return false;
    }

    RefreshArizonaChatModule();
    if (arizonaChatModuleBase_.load() != 0) {
        const auto scanState = static_cast<ArizonaChatScanState>(arizonaChatScanState_.load());
        if (scanState == ArizonaChatScanState::Idle || scanState == ArizonaChatScanState::Scanning) {
            SetError("Arizona chat signature scan is still in progress");
            return false;
        }
        if (scanState == ArizonaChatScanState::Failed) {
            SetError("Arizona chat input buffer was not found");
            return false;
        }

        if (!WriteArizonaChatInputText(gameText)) {
            SetError("Failed to set Arizona chat input text");
            return false;
        }

        const auto arizonaProcessInputAddr = arizonaChatProcessInput_.load();
        if (arizonaProcessInputAddr == 0) {
            SetError("Arizona chat process input routine is not available");
            return false;
        }

        const auto arizonaProcessInput = reinterpret_cast<ArizonaProcessInputFn>(arizonaProcessInputAddr);
        if (!CallArizonaProcessInput(arizonaProcessInput, 1)) {
            SetError("Failed to call Arizona chat process input");
            return false;
        }

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
    const auto address = GetAddress(main_offsets.AddChatMessage);
    if (!ResolveChat(chat) || chat == 0 || address == 0) {
        SetError("SAMP chat pointer is null");
        return false;
    }

    const auto addMessage = reinterpret_cast<AddChatMessageFn>(address);
    if (!CallAddChatMessage(addMessage, reinterpret_cast<void*>(chat), color, gameText.c_str())) {
        SetError("Failed to add chat message to SAMP");
        return false;
    }

    ClearError();
    return true;
}

SampApi::ChatEntry SampApi::pGetChatString(int index) {
    ChatEntry result;
    if (index < 0 || index >= kChatEntryCount || currentVersion_ == Version::Unknown) {
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

    const auto entry = chat + kChatEntryBaseOffset + (static_cast<std::uintptr_t>(index) * kChatEntrySize);
    if (!IsReadableMemory(entry, kChatEntrySize)) {
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

bool SampApi::Set_ChatInputText(std::string_view text, bool openInput, bool alreadyDecoded) {
    RefreshArizonaChatModule();
    std::string gameText = PrepareOutgoingText(text, alreadyDecoded, true);

    // Arizona's custom chat keeps its own ImGui input state. Opening the chat first
    // matches the Lua implementation and gives the standard SAMP setter a chance to
    // propagate through Arizona's hook/sync path before we fall back to raw buffer writes.
    if (openInput && !pCInput_Open_Close(true)) {
        return false;
    }

    bool sampEditboxUpdated = false;
    const auto address = GetAddress(main_offsets.CDXUTEditBox_SetText);
    if (address != 0) {
        const std::uintptr_t editBox = SAMP_CHAT_INPUT_INFO_OFFSET_func_test();
        if (editBox != 0) {
            const auto setText = reinterpret_cast<SetEditboxTextFn>(address);
            sampEditboxUpdated = CallSetEditboxText(setText, reinterpret_cast<void*>(editBox), gameText.data());
        }
    }

    const bool hasArizonaChat = arizonaChatModuleBase_.load() != 0;
    const bool arizonaChatUpdated = hasArizonaChat && WriteArizonaChatInputText(gameText);

    if (!sampEditboxUpdated && !arizonaChatUpdated) {
        if (hasArizonaChat && arizonaChatScanState_.load() == static_cast<int>(ArizonaChatScanState::Scanning)) {
            SetError("Arizona chat signature scan is still in progress");
        } else if (hasArizonaChat && arizonaChatScanState_.load() == static_cast<int>(ArizonaChatScanState::Failed)) {
            SetError("Arizona chat input buffer was not found");
        } else {
            SetError("Failed to set chat input text");
        }
        return false;
    }

    ClearError();
    return true;
}

