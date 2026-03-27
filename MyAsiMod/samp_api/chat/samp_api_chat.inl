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

    std::string gameText = PrepareOutgoingText(text, alreadyDecoded, true);
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
    const auto address = GetAddress(main_offsets.CDXUTEditBox_SetText);
    const std::uintptr_t editBox = SAMP_CHAT_INPUT_INFO_OFFSET_func_test();
    if (address == 0 || editBox == 0) {
        SetError("Chat input edit box is not available");
        return false;
    }

    std::string gameText = PrepareOutgoingText(text, alreadyDecoded, true);
    const auto setText = reinterpret_cast<SetEditboxTextFn>(address);

    if (!CallSetEditboxText(setText, reinterpret_cast<void*>(editBox), gameText.data())) {
        SetError("Failed to set chat input text");
        return false;
    }

    if (openInput && !pCInput_Open_Close(true)) {
        return false;
    }

    ClearError();
    return true;
}

