const SampApi::SampEntryPoint SampApi::entryPoint[SampApi::entryPointCount] = {
    { 0x2E2BB7, Version::E, "E", false },
    { 0x31DF13, Version::R1, "R1", true },
    { 0x3195DD, Version::R2, "R2", true },
    { 0x0CC490, Version::R3, "R3", true },
    { 0x0CC4D0, Version::R3_1, "R3-1", true },
    { 0x0CBCB0, Version::R4, "R4", true },
    { 0x0CBCD0, Version::R4_2, "R4-2", true },
    { 0x0CBC90, Version::R5_1, "R5-1", true },
    { 0x0FDB60, Version::DL_R1, "DL-R1", true },
};

const SampApi::MainOffsets SampApi::main_offsets = {
    // Order inside each VersionedOffset: { R1, R2, R3, R3-1, R4, R4-2, R5-1, DL-R1 }.
    { 0x21A0F8, 0x21A100, 0x26E8DC, 0x26E8DC, 0x26EA0C, 0x26EA0C, 0x26EB94, 0x2ACA24 }, // SAMP_INFO_OFFSET
    { 0x000003C5, 0x000003C1, 0x000003D5, 0x000003D5, 0x000003D5, 0x000003D5, 0x000003D5, 0x000003D5 }, // SAMP_INFO_OFFSET_Settings
    { 0x000003C9, 0x00000018, 0x0000002C, 0x0000002C, 0x0000002C, 0x00000000, 0x00000000, 0x0000002C }, // rakclient_interface
    { 0x21A0B8, 0x21A0C0, 0x26E898, 0x26E898, 0x26E9C8, 0x26E9C8, 0x26EB50, 0x2AC9E0 }, // SAMP_DIALOG_INFO_OFFSET
    { 0x00000028, 0x00000028, 0x00000028, 0x00000028, 0x00000028, 0x00000028, 0x00000028, 0x00000028 }, // SAMP_DIALOG_ACTIVE_OFFSET
    { 0x00000030, 0x00000030, 0x00000030, 0x00000030, 0x00000030, 0x00000030, 0x00000030, 0x00000030 }, // SAMP_DIALOG_ID_OFFSET
    { 0x00000034, 0x00000034, 0x00000034, 0x00000034, 0x00000034, 0x00000034, 0x00000034, 0x00000034 }, // SAMP_DIALOG_TEXT_OFFSET
    { 0x0006B9C0, 0x0006BA70, 0x0006F8C0, 0x0006F8C0, 0x0006FFE0, 0x00070010, 0x0006FFB0, 0x0006FA50 }, // CDialog_Show
    { 0x0006C040, 0x0006C0F0, 0x0006FF40, 0x0006FF40, 0x00070660, 0x00070690, 0x00070630, 0x000700D0 }, // CDialog_Close
    { 0x00000040, 0x00000040, 0x00000040, 0x00000040, 0x00000040, 0x00000040, 0x00000040, 0x00000040 }, // SAMP_DIALOG_CAPTION_OFFSET
    { 0x00081030, 0x000810D0, 0x00084F40, 0x00084F40, 0x00085680, 0x000856B0, 0x00085650, 0x000850D0 }, // CDXUTEditBox_GetText
    { 0x00080F60, 0x00081000, 0x00084E70, 0x00084E70, 0x000855B0, 0x000855E0, 0x00085580, 0x00085000 }, // CDXUTEditBox_SetText
    { 0x00000024, 0x00000024, 0x00000024, 0x00000024, 0x00000024, 0x00000024, 0x00000024, 0x00000024 }, // pDialogInput_pEditBox
    { 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000008 }, // pChatInput_pEditBox
    { 0x0000001C, 0x0000001C, 0x0000001C, 0x0000001C, 0x0000001C, 0x0000001C, 0x0000001C, 0x0000001C }, // CDXUTDialog
    { 0x0009BD30, 0x0009BDD0, 0x0009FFE0, 0x0009FFE0, 0x000A0720, 0x000A0750, 0x000A06F0, 0x000A0530 }, // SetCursorMode
    { 0x21A10C, 0x21A114, 0x26E8F4, 0x26E8F4, 0x26EA24, 0x26EA24, 0x26EBAC, 0x2ACA3C }, // RefGame
    { 0x00000055, 0x00000055, 0x00000061, 0x00000061, 0x00000061, 0x00000061, 0x00000061, 0x00000055 }, // CGame::m_nCursorMode
    { 0x00064010, 0x000640E0, 0x00067460, 0x00067460, 0x00067BA0, 0x00067BE0, 0x00067BE0, 0x00067650 }, // AddEntry
    { 0x21A0E4, 0x21A0EC, 0x26E8C8, 0x26E8C8, 0x26E9F8, 0x26E9F8, 0x26EB80, 0x2ACA10 }, // pChat
    { 0x00000020, 0x00000020, 0x00000020, 0x00000020, 0x00000020, 0x00000020, 0x00000020, 0x00000020 }, // CHAT_TEXT_OFFSET
    { 0x00000004, 0x00000004, 0x00000004, 0x00000004, 0x00000004, 0x00000004, 0x00000004, 0x00000004 }, // CHAT_PREFIX_TEXT_OFFSET
    { 0x000000F4, 0x000000F4, 0x000000F4, 0x000000F4, 0x000000F4, 0x000000F4, 0x000000F4, 0x000000F4 }, // CHAT_COLOR_OFFSET
    { 0x000000F8, 0x000000F8, 0x000000F8, 0x000000F8, 0x000000F8, 0x000000F8, 0x000000F8, 0x000000F8 }, // CHAT_PREFIX_COLOR_OFFSET
    { 0x00064450, 0x00064520, 0x000678A0, 0x000678A0, 0x00067FE0, 0x00068020, 0x00068020, 0x00067A90 }, // AddChatMessage
    { 0x000645A0, 0x00064670, 0x000679F0, 0x000679F0, 0x00068130, 0x00068170, 0x00068170, 0x00067BE0 }, // AddMessage
    { 0x21A0E8, 0x21A0F0, 0x26E8CC, 0x26E8CC, 0x26E9FC, 0x26E9FC, 0x26EB84, 0x2ACA14 }, // SAMP_CHAT_INPUT_INFO_OFFSET
    { 0x000014E0, 0x000014E0, 0x000014E0, 0x000014E0, 0x000014E0, 0x000014E0, 0x000014E0, 0x000014E0 }, // CInput_Opened (m_bEnabled / opened flag)
    { 0x000657E0, 0x000658B0, 0x00068D10, 0x00068D10, 0x00069440, 0x00069480, 0x00069480, 0x00068EC0 }, // CInput_Open
    { 0x000658E0, 0x000659B0, 0x00068E10, 0x00068E10, 0x00069540, 0x00069580, 0x00069580, 0x00068FC0 }, // CInput_Close
    { 0x000636D0, 0x000637A0, 0x00066B20, 0x00066B20, 0x00067260, 0x000672A0, 0x000672A0, 0x00066D10 }, // SetPageSize
    { 0x00064A51, 0x00064B21, 0x00067EB1, 0x00067EB1, 0x000685E1, 0x00068621, 0x00068621, 0x00068091 }, // PageSize_MAX
    { 0x00065C60, 0x00065D30, 0x00069190, 0x00069190, 0x000698C0, 0x00069900, 0x00069900, 0x00069340 }, // CInput_Send
    { 0x000057F0, 0x000057E0, 0x00005820, 0x00005820, 0x00005A00, 0x00005A10, 0x00005A10, 0x00005860 }, // CInput_SendSay
    { 0x00065D30, 0x00065E00, 0x00069260, 0x00069260, 0x00069990, 0x000699D0, 0x000699D0, 0x00069410 }, // CInput_ProcessInput
    { 0x0005D850, 0x0005D920, 0x00060BF0, 0x00060BF0, 0x00061320, 0x00061360, 0x00061360, 0x00060DE0 }, // HotkeyDispatcher (SA:MP hotkey switch)
    { 0x0005DA80, 0x0005DB50, 0x00060E20, 0x00060E20, 0x00061550, 0x00061590, 0x00061590, 0x00061010 }, // InputHotkeyHandler (secondary T/F5/Num0/Esc handler)
    { 0x00013CE0, 0x00013DA0, 0x00016F00, 0x00016F00, 0x00017570, 0x000175C0, 0x000175C0, 0x000170D0 }, // GetName
    { 0x00000004, 0x00000000, 0x00002F1C, 0x00002F1C, 0x0000000C, 0x00000004, 0x00000004, 0x00000000 }, // SAMP_SLOCALPLAYERID_OFFSET
    { 0x000003CD, 0x000003C5, 0x000003DE, 0x000003DE, 0x000003DE, 0x000003DE, 0x000003DE, 0x000003DE }, // SAMP_INFO_OFFSET_Pools
    { 0x00000018, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000004, 0x00000004, 0x00000008 }, // SAMP_INFO_OFFSET_Pools_Player
    { 0x00000022, 0x0000001E, 0x00002F3A, 0x00002F3A, 0x0000002A, 0x00000026, 0x00000026, 0x0000001E }, // SAMP_LOCAL_PLAYER_OFFSET
    { 0x000001A7, 0x000001CB, 0x000001CB, 0x000001CB, 0x000001CB, 0x000001CB, 0x000001CB, 0x000001CF }, // SAMP_LOCAL_PLAYER_PASSENGER_DRIVE_BY_OFFSET
    { 0x0000001C, 0x0000000C, 0x0000000C, 0x0000000C, 0x0000000C, 0x00000000, 0x00000000, 0x0000000C }, // SAMP_INFO_OFFSET_Pools_Veh
    { 0x00216378, 0x00216380, 0x00151578, 0x00151578, 0x001516A0, 0x001516A0, 0x00151828, 0x0018F6C0 }, // SAMP_COLOR_OFFSET
    { 0x00010420, 0x000104C0, 0x00013570, 0x00013570, 0x00013890, 0x000138C0, 0x000138C0, 0x000137C0 }, // ID_Find
    { 0x00000000, 0x00000022, 0x00000000, 0x00000000, 0x00000000, 0x00002F3A, 0x00002F3A, 0x00000022 }, // CPlayerPool::m_nLargestId
    { 0x00000FDE, 0x00000FD6, 0x00000FB4, 0x00000FB4, 0x00000FDE, 0x0000002A, 0x0000002A, 0x00000FD6 }, // CPlayerPool::m_bNotEmpty
    { 0x0006A1C0, 0x0006A280, 0x0006E110, 0x0006E110, 0x0006E840, 0x0006E880, 0x0006E880, 0x0006E2B0 }, // CPlayerPool_GetPing
    { 0x0006A200, 0x0006A2C0, 0x0006E150, 0x0006E150, 0x0006E880, 0x0006E8C0, 0x0006E8C0, 0x0006E2F0 }, // CPlayerPool_GetLocalPlayerPing
    { 0x0006A190, 0x0006A260, 0x0006E0E0, 0x0006E0E0, 0x0006E810, 0x0006E850, 0x0006E850, 0x0006E290 }, // CPlayerPool_GetScore
    { 0x0006A1F0, 0x0006A2B0, 0x0006E140, 0x0006E140, 0x0006E870, 0x0006E8B0, 0x0006E8B0, 0x0006E2E0 }, // CPlayerPool_GetLocalPlayerScore
    { 0x00010520, 0x000105C0, 0x00013670, 0x00013670, 0x000139A0, 0x000139F0, 0x000139F0, 0x000138C0 }, // CPlayerPool_GetCount
    { 0x0001B0A0, 0x0001B180, 0x0001E440, 0x0001E440, 0x0001EB40, 0x0001EB90, 0x0001EB90, 0x0001E650 }, // IDcar_Find
    { 0x0000002E, 0x00000026, 0x00000004, 0x00000004, 0x0000002E, 0x00001F8A, 0x00001F8A, 0x00000026 }, // CPlayerPool::m_pObject
    { 0x00000000, 0x0000000C, 0x00000000, 0x00000000, 0x00000010, 0x00000010, 0x00000010, 0x00000008 }, // CPlayerInfo::m_pPlayer
    { 0x000000B3, 0x00000008, 0x00000010, 0x00000010, 0x00000004, 0x00000004, 0x00000004, 0x00000010 }, // CRemotePlayer::m_bShowNameTag
    { 0x00000000, 0x0000001C, 0x00000000, 0x00000000, 0x000001DD, 0x000001DD, 0x000001DD, 0x00000004 }, // CRemotePlayer::m_pPed
    { 0x000000AB, 0x00000000, 0x00000008, 0x00000008, 0x000001E5, 0x000001E5, 0x000001E5, 0x00000000 }, // CRemotePlayer::m_nId
    { 0x000002A4, 0x000002A4, 0x000002A4, 0x000002A4, 0x000002A4, 0x000002A4, 0x000002A4, 0x000002A4 }, // CPed::m_pGamePed
    { 0x000001BC, 0x000001BC, 0x000001B0, 0x000001B0, 0x000001B0, 0x000001B0, 0x000001B0, 0x000001B0 }, // CRemotePlayer::m_fHealth
    { 0x000001B8, 0x000001AC, 0x000001AC, 0x000001AC, 0x000001AC, 0x000001AC, 0x000001AC, 0x000001AC }, // CRemotePlayer::m_fArmour
    { 0x00084850, 0x000848F0, 0x00088760, 0x00088760, 0x00088EA0, 0x00088ED0, 0x00088E70, 0x000888F0 }, // CDXUTListBox__GetSelectedIndex
    { 0x00086390, 0x00086430, 0x0008A2B0, 0x0008A2B0, 0x0008A9F0, 0x0008AA20, 0x0008A9C0, 0x0008A440 }, // CDXUTListBox__GetItem
    { 0x000863C0, 0x00086460, 0x0008A2E0, 0x0008A2E0, 0x0008AA20, 0x0008AA50, 0x0008A9F0, 0x0008A470 }, // SAMP_SET_DIALOG_LIST_ITEM_OFFSET
    { 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000008 }, // CChat::m_nMode
    { 0x21A0B4, 0x21A0BC, 0x26E894, 0x26E894, 0x26E9C4, 0x26E9C4, 0x26EB4C, 0x2AC9DC }, // RefScoreboard
    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 }, // CScoreboard::m_bIsEnabled
    { 0x000003BD, 0x000003B9, 0x000003CD, 0x000003CD, 0x000003CD, 0x000003CD, 0x000003CD, 0x000003CD }, // CNetGame::m_nGameState
    { 0x000372F0, 0x000373D0, 0x0003A6A0, 0x0003A6A0, 0x0003AD90, 0x0003ADE0, 0x0003ADE0, 0x0003A8A0 }, // RakNet incoming RPC handler
    { 0x000506B0, 0x00050790, 0x00053A60, 0x00053A60, 0x00054150, 0x000541A0, 0x000541A0, 0x00053C60 }, // RakNet string write encoder
    { 0x000507E0, 0x000508C0, 0x00053B90, 0x00053B90, 0x00054280, 0x000542D0, 0x000542D0, 0x00053D90 }, // RakNet string read decoder
    { 0x0010D894, 0x0010D894, 0x00121914, 0x00121914, 0x00121A3C, 0x00121A3C, 0x00121A3C, 0x0015FA54 }, // RakNet string compressor pointer
};

std::uint32_t SampApi::VersionedOffset::Get(Version version) const {
    switch (version) {
    case Version::R1:
        return R1;
    case Version::R2:
        return R2;
    case Version::R3:
        return R3;
    case Version::R3_1:
        return R3_1;
    case Version::R4:
        return R4;
    case Version::R4_2:
        return R4_2;
    case Version::R5_1:
        return R5_1;
    case Version::DL_R1:
        return DL_R1;
    default:
        return 0;
    }
}

void SampApi::attachModules(TextTransformCallback changeTagsCallback) {
    textTransformCallback_ = std::move(changeTagsCallback);
    debuglog::WriteInfo("SampApi::attachModules callback configured");
}

bool SampApi::hasSampfuncs() const {
    return GetModuleHandleA("SAMPFUNCS.asi") != nullptr;
}

void SampApi::Refresh() {
    const HMODULE currentModule = GetModuleHandleA("samp.dll");
    if (currentModule != sampModule_) {
        debuglog::WriteInfo("SampApi::Refresh samp.dll handle changed old=%p new=%p", sampModule_, currentModule);
        sampModule_ = currentModule;
        versionResolved_ = false;
        currentVersion_ = Version::Unknown;
        currentEntryPoint_ = nullptr;
        entryPointAddress_ = 0;
        supportedVersion_ = false;
        cursorModeValidationDone_ = false;
        cursorModeSignatureValid_ = false;
        cursorModeValidationError_.clear();
        ResetChatAsiInputDiscovery();

        if (sampModule_) {
            debuglog::WriteInfo("samp.dll detected at %p", sampModule_);
        } else {
            debuglog::WriteInfo("samp.dll is not loaded");
            lastError_.clear();
        }
    }

    if (sampModule_ && !versionResolved_) {
        DetectVersion();
    }

}

bool SampApi::isSampLoadedLua() {
    Refresh();

    if (!sampModule_) {
        SetError("samp.dll is not loaded");
        return false;
    }

    if (!versionResolved_) {
        SetError("SAMP version was not resolved");
        return false;
    }

    return true;
}

bool SampApi::isSAMPInitilizeLua() {
    if (!isSampLoadedLua()) {
        return false;
    }

    if (!supportedVersion_) {
        if (lastError_.empty()) {
            SetError("SAMP version is not supported by the current offsets");
        }
        return false;
    }

    std::string readinessReason;
    if (!IsSampReadyByFallback(readinessReason)) {
        SetError(std::move(readinessReason));
        return false;
    }

    ClearError();
    return true;
}

void SampApi::LogReadinessDiagnostics(const char* context) const {
    const char* tag = context ? context : "readiness";
    const std::uintptr_t base = ModuleBase();

    debuglog::WriteInfo(
        "[samp][diag] %s ts=%llums module=%p version=%s supported=%d resolved=%d cursorWrites=%d entry=0x%X path=\"%s\" cursorError=\"%s\"",
        tag,
        static_cast<unsigned long long>(GetTickCount64()),
        sampModule_,
        currentVersionName(),
        supportedVersion_ ? 1 : 0,
        versionResolved_ ? 1 : 0,
        cursorModeSignatureValid_ ? 1 : 0,
        entryPointAddress_,
        ModulePath(sampModule_).c_str(),
        cursorModeValidationError_.c_str());

    if (!sampModule_) {
        debuglog::WriteInfo("[samp][diag] %s samp.dll is not loaded", tag);
        return;
    }

    IMAGE_DOS_HEADER dosHeader{};
    if (SafeRead(base, dosHeader) && dosHeader.e_magic == IMAGE_DOS_SIGNATURE
        && dosHeader.e_lfanew >= static_cast<LONG>(sizeof(IMAGE_DOS_HEADER))
        && dosHeader.e_lfanew <= 0x1000) {
        IMAGE_NT_HEADERS32 ntHeaders{};
        if (SafeRead(base + static_cast<std::uint32_t>(dosHeader.e_lfanew), ntHeaders)
            && ntHeaders.Signature == IMAGE_NT_SIGNATURE) {
            debuglog::WriteInfo(
                "[samp][diag] %s pe imageBase=0x%08X sizeOfImage=0x%X sizeOfHeaders=0x%X timestamp=0x%08X checksum=0x%08X sections=%u",
                tag,
                static_cast<unsigned>(ntHeaders.OptionalHeader.ImageBase),
                static_cast<unsigned>(ntHeaders.OptionalHeader.SizeOfImage),
                static_cast<unsigned>(ntHeaders.OptionalHeader.SizeOfHeaders),
                static_cast<unsigned>(ntHeaders.FileHeader.TimeDateStamp),
                static_cast<unsigned>(ntHeaders.OptionalHeader.CheckSum),
                static_cast<unsigned>(ntHeaders.FileHeader.NumberOfSections));
        } else {
            debuglog::WriteError("[samp][diag] %s invalid NT signature", tag);
        }
    } else {
        debuglog::WriteError("[samp][diag] %s invalid DOS signature", tag);
    }

    if (!CanUseOffsets()) {
        debuglog::WriteInfo(
            "[samp][diag] %s versioned offset diagnostics skipped: exact SAMP variant is not approved",
            tag);
        return;
    }

    auto readGlobal = [&](const char* name, const VersionedOffset& offset) -> std::uint32_t {
        const std::uint32_t relative = offset.Get(currentVersion_);
        const std::uintptr_t address = relative == 0 ? 0 : base + relative;
        std::uint32_t value = 0;
        const bool readableGlobal = address != 0 && IsReadableMemory(address, sizeof(value));
        const bool readOk = readableGlobal && SafeRead(address, value);
        const bool readableValue = value != 0 && IsReadableMemory(value, sizeof(std::uint32_t));

        debuglog::WriteInfo(
            "[samp][diag] %s global %-10s off=0x%X addr=0x%08X read=%d value=0x%08X valueReadable=%d addrRegion=\"%s\" valueRegion=\"%s\"",
            tag,
            name,
            static_cast<unsigned>(relative),
            static_cast<unsigned>(address),
            readOk ? 1 : 0,
            static_cast<unsigned>(value),
            readableValue ? 1 : 0,
            MemoryRegionSummary(address).c_str(),
            MemoryRegionSummary(value).c_str());
        return readOk ? value : 0;
    };

    const std::uint32_t sampInfo = readGlobal("sampInfo", main_offsets.SAMP_INFO_OFFSET);
    const std::uint32_t chat = readGlobal("chat", main_offsets.pChat);
    const std::uint32_t chatInput = readGlobal("chatInput", main_offsets.SAMP_CHAT_INPUT_INFO_OFFSET);
    const std::uint32_t dialog = readGlobal("dialog", main_offsets.SAMP_DIALOG_INFO_OFFSET);
    const std::uint32_t refGame = readGlobal("refGame", main_offsets.RefGame);

    if (sampInfo != 0) {
        std::uint32_t pools = 0;
        std::uint32_t pedPool = 0;
        std::uint32_t vehiclePool = 0;
        std::uint32_t rakClient = 0;
        const bool poolsOk = SafeRead(sampInfo + main_offsets.SAMP_INFO_OFFSET_Pools.Get(currentVersion_), pools);
        const bool pedOk = poolsOk && pools != 0
            && SafeRead(pools + main_offsets.SAMP_INFO_OFFSET_Pools_Player.Get(currentVersion_), pedPool);
        const bool vehOk = poolsOk && pools != 0
            && SafeRead(pools + main_offsets.SAMP_INFO_OFFSET_Pools_Veh.Get(currentVersion_), vehiclePool);
        const bool rakOk = SafeRead(sampInfo + main_offsets.rakclient_interface.Get(currentVersion_), rakClient);
        debuglog::WriteInfo(
            "[samp][diag] %s sampInfo fields pools(+0x%X) ok=%d value=0x%08X pedPool(+0x%X) ok=%d value=0x%08X vehPool(+0x%X) ok=%d value=0x%08X rak(+0x%X) ok=%d value=0x%08X",
            tag,
            static_cast<unsigned>(main_offsets.SAMP_INFO_OFFSET_Pools.Get(currentVersion_)),
            poolsOk ? 1 : 0,
            static_cast<unsigned>(pools),
            static_cast<unsigned>(main_offsets.SAMP_INFO_OFFSET_Pools_Player.Get(currentVersion_)),
            pedOk ? 1 : 0,
            static_cast<unsigned>(pedPool),
            static_cast<unsigned>(main_offsets.SAMP_INFO_OFFSET_Pools_Veh.Get(currentVersion_)),
            vehOk ? 1 : 0,
            static_cast<unsigned>(vehiclePool),
            static_cast<unsigned>(main_offsets.rakclient_interface.Get(currentVersion_)),
            rakOk ? 1 : 0,
            static_cast<unsigned>(rakClient));
    } else if (refGame != 0) {
        debuglog::WriteInfo(
            "[samp][diag] %s refGame is initialized while sampInfo is still null; SA:MP reached GUI/game init but CNetGame is not constructed yet",
            tag);
    }

    if (chatInput != 0) {
        std::uint8_t opened = 0;
        std::uint32_t editBox = 0;
        const bool openedOk = SafeRead(chatInput + main_offsets.CInput_Opened.Get(currentVersion_), opened);
        const bool editOk = SafeRead(chatInput + main_offsets.pChatInput_pEditBox.Get(currentVersion_), editBox);
        debuglog::WriteInfo(
            "[samp][diag] %s chatInput fields opened(+0x%X) ok=%d value=%u editBox(+0x%X) ok=%d value=0x%08X",
            tag,
            static_cast<unsigned>(main_offsets.CInput_Opened.Get(currentVersion_)),
            openedOk ? 1 : 0,
            static_cast<unsigned>(opened),
            static_cast<unsigned>(main_offsets.pChatInput_pEditBox.Get(currentVersion_)),
            editOk ? 1 : 0,
            static_cast<unsigned>(editBox));
    }

    if (dialog != 0) {
        std::uint32_t dxutDialog = 0;
        std::uint32_t editBox = 0;
        std::uint32_t textPtr = 0;
        std::uint32_t id = 0;
        std::uint8_t active = 0;
        const bool dxutOk = SafeRead(dialog + main_offsets.CDXUTDialog.Get(currentVersion_), dxutDialog);
        const bool editOk = SafeRead(dialog + main_offsets.pDialogInput_pEditBox.Get(currentVersion_), editBox);
        const bool textOk = SafeRead(dialog + main_offsets.SAMP_DIALOG_TEXT_OFFSET.Get(currentVersion_), textPtr);
        const bool idOk = SafeRead(dialog + main_offsets.SAMP_DIALOG_ID_OFFSET.Get(currentVersion_), id);
        const bool activeOk = SafeRead(dialog + main_offsets.SAMP_DIALOG_ACTIVE_OFFSET.Get(currentVersion_), active);
        debuglog::WriteInfo(
            "[samp][diag] %s dialog fields active(+0x%X) ok=%d value=%u id(+0x%X) ok=%d value=%u dxut(+0x%X) ok=%d value=0x%08X edit(+0x%X) ok=%d value=0x%08X text(+0x%X) ok=%d value=0x%08X",
            tag,
            static_cast<unsigned>(main_offsets.SAMP_DIALOG_ACTIVE_OFFSET.Get(currentVersion_)),
            activeOk ? 1 : 0,
            static_cast<unsigned>(active),
            static_cast<unsigned>(main_offsets.SAMP_DIALOG_ID_OFFSET.Get(currentVersion_)),
            idOk ? 1 : 0,
            static_cast<unsigned>(id),
            static_cast<unsigned>(main_offsets.CDXUTDialog.Get(currentVersion_)),
            dxutOk ? 1 : 0,
            static_cast<unsigned>(dxutDialog),
            static_cast<unsigned>(main_offsets.pDialogInput_pEditBox.Get(currentVersion_)),
            editOk ? 1 : 0,
            static_cast<unsigned>(editBox),
            static_cast<unsigned>(main_offsets.SAMP_DIALOG_TEXT_OFFSET.Get(currentVersion_)),
            textOk ? 1 : 0,
            static_cast<unsigned>(textPtr));
    }

    auto logCodeBytes = [&](const char* name, const VersionedOffset& offset) {
        const std::uint32_t relative = offset.Get(currentVersion_);
        const std::uintptr_t address = relative == 0 ? 0 : base + relative;
        debuglog::WriteInfo(
            "[samp][diag] %s code %-18s off=0x%X addr=0x%08X bytes=%s region=\"%s\" transfer=\"%s\"",
            tag,
            name,
            static_cast<unsigned>(relative),
            static_cast<unsigned>(address),
            HexBytes(address, 8).c_str(),
            MemoryRegionSummary(address).c_str(),
            DecodeControlTransfer(address).c_str());
    };

    logCodeBytes("CDialog_Show", main_offsets.CDialog_Show);
    logCodeBytes("CDialog_Close", main_offsets.CDialog_Close);
    logCodeBytes("SetCursorMode", main_offsets.SetCursorMode);
    logCodeBytes("CInput_Open", main_offsets.CInput_Open);
    logCodeBytes("CInput_Close", main_offsets.CInput_Close);
    logCodeBytes("CInput_Send", main_offsets.CInput_Send);
    logCodeBytes("HotkeyDispatcher", main_offsets.HotkeyDispatcher);
    logCodeBytes("InputHotkeyHandler", main_offsets.InputHotkeyHandler);
    logCodeBytes("AddEntry", main_offsets.AddEntry);
    logCodeBytes("AddChatMessage", main_offsets.AddChatMessage);
    logCodeBytes("AddMessage", main_offsets.AddMessage);
    logCodeBytes("RakHandleRpc", main_offsets.RakHandleRpc);
}

bool SampApi::IsSampReadyByFallback(std::string& reason) const {
    reason = "SAMP is not initialized yet";

    if (!CanUseOffsets()) {
        reason = "SAMP readiness probe failed: exact variant is not approved";
        return false;
    }

    const std::uintptr_t base = ModuleBase();
    const auto version = currentVersion_;

    bool hasSampInfo = false;
    bool hasChatInput = false;
    bool hasRefGame = false;
    bool hasDialog = false;

    std::uint32_t sampInfo = 0;
    if (ResolveSampInfo(sampInfo) && sampInfo != 0 && IsReadableMemory(sampInfo, sizeof(std::uint32_t))) {
        hasSampInfo = true;
    }

    std::uint32_t chatInput = 0;
    if (ResolveChatInput(chatInput) && chatInput != 0) {
        const std::uintptr_t openedAddress = static_cast<std::uintptr_t>(chatInput) + main_offsets.CInput_Opened.Get(version);
        if (IsReadableMemory(static_cast<std::uintptr_t>(chatInput), sizeof(std::uint32_t))
            && IsReadableMemory(openedAddress, sizeof(std::uint8_t))) {
            std::uint8_t openedFlag = 0;
            hasChatInput = SafeRead(openedAddress, openedFlag);
        }
    }

    const std::uintptr_t refGameAddress = base + main_offsets.RefGame.Get(version);
    if (main_offsets.RefGame.Get(version) != 0 && IsReadableMemory(refGameAddress, sizeof(std::uint32_t))) {
        std::uint32_t refGame = 0;
        hasRefGame = SafeRead(refGameAddress, refGame) && refGame != 0 && IsReadableMemory(refGame, sizeof(std::uint32_t));
    }

    std::uint32_t dialog = 0;
    if (ResolveDialog(dialog) && dialog != 0) {
        const std::uintptr_t activeAddress = static_cast<std::uintptr_t>(dialog) + main_offsets.SAMP_DIALOG_ACTIVE_OFFSET.Get(version);
        if (IsReadableMemory(static_cast<std::uintptr_t>(dialog), sizeof(std::uint32_t))
            && IsReadableMemory(activeAddress, sizeof(std::uint8_t))) {
            std::uint8_t activeValue = 0;
            hasDialog = SafeRead(activeAddress, activeValue);
        }
    }

    if (hasSampInfo || (hasChatInput && hasRefGame) || (hasChatInput && hasDialog)) {
        return true;
    }

    reason = "SAMP is not initialized yet (probe: sampInfo=" + std::to_string(hasSampInfo ? 1 : 0)
        + " chatInput=" + std::to_string(hasChatInput ? 1 : 0)
        + " refGame=" + std::to_string(hasRefGame ? 1 : 0)
        + " dialog=" + std::to_string(hasDialog ? 1 : 0) + ")";
    return false;
}

std::uintptr_t SampApi::PedPool() {
    std::uint32_t pedPool = 0;
    if (!isSAMPInitilizeLua() || !ResolvePedPool(pedPool)) {
        return 0;
    }
    return pedPool;
}

std::string SampApi::GetNameID(int id) {
    if (id < 0 || id > kMaxSampPlayerId) {
        return "UNKNOWN";
    }

    std::uint32_t playerPool = 0;
    int localId = -1;
    if (!ResolvePlayerPoolState(playerPool, localId)
        || !IsPlayerConnectedInPool(playerPool, localId, id)) {
        return "UNKNOWN";
    }

    return GetPlayerNameInPool(playerPool, id, GetAddress(main_offsets.GetName));
}

std::optional<int> SampApi::GetIDByName(std::string_view name) {
    const std::string target = TrimAscii(name);
    if (target.empty()) {
        return std::nullopt;
    }

    std::uint32_t playerPool = 0;
    int localId = -1;
    if (!ResolvePlayerPoolState(playerPool, localId)) {
        return std::nullopt;
    }

    std::int32_t largestId = -1;
    const std::uintptr_t largestIdAddress = static_cast<std::uintptr_t>(playerPool)
        + main_offsets.CPlayerPool_LargestId.Get(currentVersion_);
    if (!SafeRead(largestIdAddress, largestId) || largestId < 0 || largestId > kMaxSampPlayerId) {
        return std::nullopt;
    }

    const std::uintptr_t getNameAddress = GetAddress(main_offsets.GetName);
    if (getNameAddress == 0) {
        return std::nullopt;
    }

    const int lastCandidateId = std::max(largestId, localId);
    for (int id = 0; id <= lastCandidateId; ++id) {
        if (IsPlayerConnectedInPool(playerPool, localId, id)
            && GetPlayerNameInPool(playerPool, id, getNameAddress) == target) {
            return id;
        }
    }

    return std::nullopt;
}

bool SampApi::IsConnected(int id) {
    if (id < 0 || id > kMaxSampPlayerId) {
        return false;
    }

    std::uint32_t playerPool = 0;
    int localId = -1;
    if (!ResolvePlayerPoolState(playerPool, localId)) {
        return false;
    }

    return IsPlayerConnectedInPool(playerPool, localId, id);
}

std::optional<int> SampApi::GetPlayerPing(int id) {
    if (id < 0 || id > kMaxSampPlayerId || !isSupportedVersion()) {
        return std::nullopt;
    }

    std::uint32_t playerPool = 0;
    int localId = -1;
    if (!ResolvePlayerPoolState(playerPool, localId)) {
        return std::nullopt;
    }

    int ping = 0;
    if (localId >= 0 && id == localId) {
        const auto address = GetAddress(main_offsets.CPlayerPool_GetLocalPlayerPing);
        if (address == 0) {
            return std::nullopt;
        }

        const auto getLocalPing = reinterpret_cast<PlayerPoolGetLocalPingFn>(address);
        if (!CallPlayerPoolGetLocalPing(getLocalPing, reinterpret_cast<void*>(playerPool), ping)) {
            return std::nullopt;
        }
        if (ping < 0) {
            return std::nullopt;
        }
        return ping;
    }

    if (!IsPlayerConnectedInPool(playerPool, localId, id)) {
        return std::nullopt;
    }

    const auto address = GetAddress(main_offsets.CPlayerPool_GetPing);
    if (address == 0) {
        return std::nullopt;
    }

    const auto getPing = reinterpret_cast<PlayerPoolGetPingFn>(address);
    if (!CallPlayerPoolGetPing(
            getPing,
            reinterpret_cast<void*>(playerPool),
            static_cast<unsigned short>(id),
            ping)) {
        return std::nullopt;
    }

    if (ping < 0) {
        return std::nullopt;
    }
    return ping;
}

std::optional<int> SampApi::GetPlayerScore(int id) {
    if (id < 0 || id > kMaxSampPlayerId || !isSupportedVersion() || !IsServerConnected()) {
        return std::nullopt;
    }

    std::uint32_t playerPool = 0;
    int localId = -1;
    if (!ResolvePlayerPoolState(playerPool, localId)) {
        return std::nullopt;
    }

    int score = 0;
    if (localId >= 0 && id == localId) {
        const auto address = GetAddress(main_offsets.CPlayerPool_GetLocalPlayerScore);
        if (address == 0) {
            return std::nullopt;
        }

        const auto getLocalScore = reinterpret_cast<PlayerPoolGetLocalScoreFn>(address);
        if (!CallPlayerPoolGetLocalScore(getLocalScore, reinterpret_cast<void*>(playerPool), score)) {
            return std::nullopt;
        }
        return score;
    }

    if (!IsPlayerConnectedInPool(playerPool, localId, id)) {
        return std::nullopt;
    }

    const auto address = GetAddress(main_offsets.CPlayerPool_GetScore);
    if (address == 0) {
        return std::nullopt;
    }

    const auto getScore = reinterpret_cast<PlayerPoolGetScoreFn>(address);
    if (!CallPlayerPoolGetScore(
            getScore,
            reinterpret_cast<void*>(playerPool),
            static_cast<unsigned short>(id),
            score)) {
        return std::nullopt;
    }
    return score;
}

std::optional<int> SampApi::GetPlayerCount(bool includeNpc) {
    if (!isSupportedVersion() || !IsServerConnected()) {
        return std::nullopt;
    }

    std::uint32_t playerPool = 0;
    int localId = -1;
    if (!ResolvePlayerPoolState(playerPool, localId)) {
        return std::nullopt;
    }

    const auto address = GetAddress(main_offsets.CPlayerPool_GetCount);
    if (address == 0) {
        return std::nullopt;
    }

    int count = 0;
    const auto getCount = reinterpret_cast<PlayerPoolGetCountFn>(address);
    if (!CallPlayerPoolGetCount(
            getCount,
            reinterpret_cast<void*>(playerPool),
            includeNpc ? TRUE : FALSE,
            count)
        || count < 0 || count > kMaxSampPlayerId + 1) {
        return std::nullopt;
    }
    return count;
}

std::optional<int> SampApi::FindVehicleIdByPointer(const void* vehicle) {
    if (!vehicle || !isSupportedVersion() || !IsServerConnected()) {
        return std::nullopt;
    }

    std::uint32_t vehiclePool = 0;
    if (!ResolveVehiclePool(vehiclePool)) {
        return std::nullopt;
    }

    const auto address = GetAddress(main_offsets.IDcar_Find);
    if (address == 0) {
        return std::nullopt;
    }

    std::uint16_t vehicleId = kInvalidSampVehicleId;
    const auto find = reinterpret_cast<IdFindFn>(address);
    if (!CallIdFind(find, reinterpret_cast<void*>(vehiclePool), vehicle, vehicleId)
        || vehicleId == kInvalidSampVehicleId || vehicleId > kMaxSampVehicleId) {
        return std::nullopt;
    }
    return static_cast<int>(vehicleId);
}

std::optional<bool> SampApi::GetLocalPassengerDriveByState() const {
    if (!isSupportedVersion()) {
        return std::nullopt;
    }

    std::uint32_t localPlayer = 0;
    if (!ResolveLocalPlayer(localPlayer)) {
        return std::nullopt;
    }

    std::int32_t rawState = 0;
    const std::uint32_t stateOffset = main_offsets.SAMP_LOCAL_PLAYER_PASSENGER_DRIVE_BY_OFFSET.Get(currentVersion_);
    if (!SafeRead(localPlayer + stateOffset, rawState) || (rawState != 0 && rawState != 1)) {
        return std::nullopt;
    }

    return rawState == 1;
}

std::optional<std::uint32_t> SampApi::GetPlayerColor(int id) {
    if (id < 0 || id > 1003 || !isSupportedVersion() || !IsConnected(id)) {
        return std::nullopt;
    }

    const std::uint32_t colorsOffset = main_offsets.SAMP_COLOR_OFFSET.Get(currentVersion_);
    const std::uintptr_t colorsBase = ModuleBase() + colorsOffset;
    if (colorsOffset == 0 || colorsBase == 0) {
        return std::nullopt;
    }

    std::uint32_t color = 0;
    if (!SafeRead(colorsBase + static_cast<std::uintptr_t>(id) * sizeof(std::uint32_t), color)) {
        return std::nullopt;
    }
    return color;
}


bool SampApi::restoreOriginalFunctionGlobals() {
    functionBackendActive_ = BACKEND_STANDARD;
    debuglog::WriteInfo("SampApi backend restored to standard");
    return true;
}

bool SampApi::applyFunctionBackend(std::string_view mode) {
    functionBackendMode_ = NormalizeBackendMode(mode);

    // Lua backend switching replaced globals in MoonLoader. In this C++ port
    // there is no Lua global environment, so the backend state is preserved as
    // compatibility metadata while calls stay on the native C++ implementation.
    functionBackendActive_ = BACKEND_STANDARD;
    debuglog::WriteInfo(
        "SampApi backend apply desired=%s active=%s",
        functionBackendMode_.c_str(),
        functionBackendActive_.c_str());
    return true;
}

bool SampApi::setFunctionBackendMode(std::string_view mode) {
    debuglog::WriteInfo("SampApi::setFunctionBackendMode requested=%.*s", static_cast<int>(mode.size()), mode.data());
    return applyFunctionBackend(mode);
}

std::string SampApi::getFunctionBackendMode() const {
    return functionBackendMode_;
}

SampApi::FunctionBackendStatus SampApi::getFunctionBackendStatus() const {
    FunctionBackendStatus status;
    status.desired = functionBackendMode_;
    status.active = functionBackendActive_;
    status.hasSampfuncs = hasSampfuncs();

    for (const char* name : kSampGlobalNames) {
        status.globals.push_back({ name, "custom" });
    }

    return status;
}

bool SampApi::installSampfuncsCompat(std::string_view mode) {
    return applyFunctionBackend(mode);
}

void SampApi::onTerminate() {
    RemoveChatAsiInputCallbackHook();
    restoreOriginalFunctionGlobals();
}

HMODULE SampApi::sampModule() const {
    return sampModule_;
}

SampApi::Version SampApi::currentVersion() const {
    return currentVersion_;
}

const char* SampApi::currentVersionName() const {
    return currentEntryPoint_ ? currentEntryPoint_->name : "UNKNOWN";
}

bool SampApi::isSupportedVersion() const {
    return supportedVersion_;
}

bool SampApi::IsCursorModeWriteSupported() const {
    return cursorModeValidationDone_ && cursorModeSignatureValid_;
}

const std::string& SampApi::cursorModeValidationError() const {
    return cursorModeValidationError_;
}

const std::string& SampApi::lastError() const {
    return lastError_;
}

bool SampApi::DetectVersion() {
    versionResolved_ = true;
    currentVersion_ = Version::Unknown;
    currentEntryPoint_ = nullptr;
    entryPointAddress_ = 0;
    supportedVersion_ = false;
    cursorModeValidationDone_ = false;
    cursorModeSignatureValid_ = false;
    cursorModeValidationError_.clear();

    if (!sampModule_) {
        SetError("samp.dll is not loaded");
        return false;
    }

    const std::uintptr_t moduleBase = ModuleBase();
    IMAGE_DOS_HEADER dosHeader{};
    if (!SafeRead(moduleBase, dosHeader) || dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
        SetError("samp.dll has an invalid DOS header");
        return false;
    }

    if (dosHeader.e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER))
        || dosHeader.e_lfanew > 0x1000) {
        SetError("samp.dll has an invalid PE header offset");
        return false;
    }

    IMAGE_NT_HEADERS32 ntHeaders{};
    if (!SafeRead(moduleBase + static_cast<std::uint32_t>(dosHeader.e_lfanew), ntHeaders)
        || ntHeaders.Signature != IMAGE_NT_SIGNATURE
        || ntHeaders.FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || ntHeaders.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC
        || ntHeaders.OptionalHeader.SizeOfImage == 0
        || ntHeaders.OptionalHeader.AddressOfEntryPoint >= ntHeaders.OptionalHeader.SizeOfImage) {
        SetError("samp.dll has an invalid NT header");
        return false;
    }

    entryPointAddress_ = ntHeaders.OptionalHeader.AddressOfEntryPoint;

    for (const auto& info : entryPoint) {
        if (info.address == entryPointAddress_) {
            currentVersion_ = info.version;
            currentEntryPoint_ = &info;
            if (!info.supported) {
                SetError("SAMP version is recognized but not supported");
                debuglog::WriteError(
                    "Detected unsupported SAMP version: %s (entry point 0x%X)",
                    info.name,
                    info.address);
                return false;
            }

            std::string variantError;
            if (!ValidateKnownSampVariant(sampModule_, info.version, ntHeaders, variantError)) {
                SetError(std::move(variantError));
                debuglog::WriteError(
                    "Rejected SAMP version %s (entry point 0x%X): exact variant validation failed",
                    info.name,
                    info.address);
                return false;
            }

            if (!ValidateCriticalSampSignatures(sampModule_, info.version, variantError)) {
                SetError(std::move(variantError));
                debuglog::WriteError(
                    "Rejected SAMP version %s (entry point 0x%X): loaded-code signature validation failed",
                    info.name,
                    info.address);
                return false;
            }

            supportedVersion_ = true;
            ClearError();
            debuglog::WriteInfo(
                "Detected exact SAMP version: %s (entry point 0x%X, supported=yes)",
                info.name,
                info.address);
            ValidateCursorModeFunction();
            return true;
        }
    }

    debuglog::WriteError("Unknown SAMP entry point: 0x%X", entryPointAddress_);
    SetError("Unknown SAMP version entry point");
    return false;
}

bool SampApi::ValidateCursorModeFunction() {
    if (cursorModeValidationDone_) {
        return cursorModeSignatureValid_;
    }

    cursorModeValidationDone_ = true;
    cursorModeSignatureValid_ = false;
    cursorModeValidationError_.clear();

    if (!sampModule_ || !versionResolved_ || !supportedVersion_) {
        cursorModeValidationError_ = "samp.dll is not ready for cursor validation";
        debuglog::WriteError("[samp][cursor] validation skipped: %s", cursorModeValidationError_.c_str());
        return false;
    }

    const std::uint32_t relative = main_offsets.SetCursorMode.Get(currentVersion_);
    const std::uintptr_t address = GetAddress(main_offsets.SetCursorMode);
    if (relative == 0 || address == 0) {
        cursorModeValidationError_ = "SetCursorMode offset is not available";
        debuglog::WriteError(
            "[samp][cursor] validation failed version=%s: %s",
            currentVersionName(),
            cursorModeValidationError_.c_str());
        return false;
    }

    static constexpr std::uint8_t kExpectedPrefix[] = {
        0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x83, 0xF8,
        0x02, 0x56, 0x57, 0x8B, 0xF1, 0x75, 0x6F, 0x6A, 0x05,
    };

    bool matches = true;
    for (std::size_t i = 0; i < sizeof(kExpectedPrefix); ++i) {
        std::uint8_t value = 0;
        if (!SafeRead(address + i, value) || value != kExpectedPrefix[i]) {
            matches = false;
            break;
        }
    }

    const std::string bytes = HexBytes(address, 16);
    debuglog::WriteInfo(
        "[samp][cursor] validate version=%s entry=0x%X setCursorModeRva=0x%X addr=0x%08X bytes16=\"%s\" transfer=\"%s\"",
        currentVersionName(),
        entryPointAddress_,
        relative,
        static_cast<unsigned>(address),
        bytes.c_str(),
        DecodeControlTransfer(address).c_str());

    if (!matches) {
        cursorModeValidationError_ = "SetCursorMode signature mismatch; cursor writes disabled for this samp.dll";
        debuglog::WriteError(
            "[samp][cursor] validation failed version=%s: %s",
            currentVersionName(),
            cursorModeValidationError_.c_str());
        return false;
    }

    cursorModeSignatureValid_ = true;
    debuglog::WriteInfo("[samp][cursor] validation ok version=%s", currentVersionName());
    return true;
}

void SampApi::SetError(std::string message) {
    if (lastError_ != message) {
        debuglog::WriteError("SampApi error: %s", message.c_str());
    }
    lastError_ = std::move(message);
}

void SampApi::ClearError() {
    if (!lastError_.empty()) {
        debuglog::WriteInfo("SampApi error cleared: %s", lastError_.c_str());
    }
    lastError_.clear();
}

bool SampApi::CanUseOffsets() const {
    return sampModule_
        && versionResolved_
        && supportedVersion_
        && currentVersion_ != Version::Unknown
        && currentVersion_ != Version::E;
}

std::uintptr_t SampApi::ModuleBase() const {
    return reinterpret_cast<std::uintptr_t>(sampModule_);
}

std::uintptr_t SampApi::GetAddress(const VersionedOffset& offset) const {
    if (!CanUseOffsets()) {
        return 0;
    }

    const auto relative = offset.Get(currentVersion_);
    return relative == 0 ? 0 : ModuleBase() + relative;
}

std::string SampApi::PrepareOutgoingText(std::string_view text, bool alreadyDecoded, bool applyTransform) const {
    std::string utf8Text = alreadyDecoded ? textencoding::GameToUtf8(text) : std::string(text);
    if (applyTransform) {
        utf8Text = ApplyTextTransform(utf8Text);
    }
    return textencoding::Utf8ToGame(utf8Text);
}

std::string SampApi::PrepareIncomingText(std::string_view text) const {
    return textencoding::GameToUtf8(text);
}

std::string SampApi::ApplyTextTransform(std::string_view utf8Text) const {
    if (!textTransformCallback_) {
        return std::string(utf8Text);
    }

    try {
        return textTransformCallback_(utf8Text);
    } catch (...) {
        return std::string(utf8Text);
    }
}

std::uintptr_t SampApi::GetCurrentDialogListBoxPointer() {
    if (!isDialogActive() || !isDialogListStyle(GetCurrentDialogStyle())) {
        return 0;
    }

    std::uint32_t listBox = 0;
    if (!SafeRead(pDialog_func() + 0x20, listBox)) {
        return 0;
    }

    return listBox;
}

int SampApi::GetCurrentDialogSelectedIndex() {
    const std::uintptr_t listBox = GetCurrentDialogListBoxPointer();
    const auto address = GetAddress(main_offsets.CDXUTListBox__GetSelectedIndex);
    if (listBox == 0 || address == 0) {
        return -1;
    }

    const auto getSelectedIndex = reinterpret_cast<ListBoxGetSelectedIndexFn>(address);
    int selectedIndex = -1;
    CallListBoxGetSelectedIndex(getSelectedIndex, reinterpret_cast<void*>(listBox), -1, selectedIndex);
    return selectedIndex;
}

bool SampApi::ResolveSampInfo(std::uint32_t& sampInfo) const {
    sampInfo = 0;
    return CanUseOffsets()
        && SafeRead(ModuleBase() + main_offsets.SAMP_INFO_OFFSET.Get(currentVersion_), sampInfo);
}

bool SampApi::ResolvePedPool(std::uint32_t& pedPool) const {
    pedPool = 0;

    std::uint32_t sampInfo = 0;
    if (!ResolveSampInfo(sampInfo) || sampInfo == 0) {
        return false;
    }

    std::uint32_t pools = 0;
    if (!SafeRead(sampInfo + main_offsets.SAMP_INFO_OFFSET_Pools.Get(currentVersion_), pools) || pools == 0) {
        return false;
    }

    return SafeRead(pools + main_offsets.SAMP_INFO_OFFSET_Pools_Player.Get(currentVersion_), pedPool) && pedPool != 0;
}

bool SampApi::ResolveVehiclePool(std::uint32_t& vehiclePool) const {
    vehiclePool = 0;

    std::uint32_t sampInfo = 0;
    if (!ResolveSampInfo(sampInfo) || sampInfo == 0) {
        return false;
    }

    std::uint32_t pools = 0;
    if (!SafeRead(sampInfo + main_offsets.SAMP_INFO_OFFSET_Pools.Get(currentVersion_), pools) || pools == 0) {
        return false;
    }

    return SafeRead(pools + main_offsets.SAMP_INFO_OFFSET_Pools_Veh.Get(currentVersion_), vehiclePool)
        && vehiclePool != 0;
}

bool SampApi::ResolvePlayerPoolState(std::uint32_t& playerPool, int& localId) const {
    playerPool = 0;
    localId = -1;
    if (!ResolvePedPool(playerPool) || playerPool == 0) {
        return false;
    }

    std::int16_t rawLocalId = -1;
    if (SafeRead(
            static_cast<std::uintptr_t>(playerPool)
                + main_offsets.SAMP_SLOCALPLAYERID_OFFSET.Get(currentVersion_),
            rawLocalId)
        && rawLocalId >= 0 && rawLocalId <= kMaxSampPlayerId) {
        localId = rawLocalId;
    }
    return true;
}

bool SampApi::IsPlayerConnectedInPool(std::uint32_t playerPool, int localId, int id) const {
    if (playerPool == 0 || id < 0 || id > kMaxSampPlayerId) {
        return false;
    }
    if (localId >= 0 && id == localId) {
        return true;
    }

    std::int32_t connected = 0;
    const std::uintptr_t connectedAddress = static_cast<std::uintptr_t>(playerPool)
        + main_offsets.CPlayerPool_ConnectedFlags.Get(currentVersion_)
        + static_cast<std::uintptr_t>(id) * sizeof(connected);
    return SafeRead(connectedAddress, connected) && connected != 0;
}

std::string SampApi::GetPlayerNameInPool(
    std::uint32_t playerPool,
    int id,
    std::uintptr_t getNameAddress) const {
    if (playerPool == 0 || id < 0 || id > kMaxSampPlayerId || getNameAddress == 0) {
        return "UNKNOWN";
    }

    const auto getName = reinterpret_cast<GetNameFn>(getNameAddress);
    const char* rawName = nullptr;
    if (!CallGetName(
            getName,
            reinterpret_cast<void*>(playerPool),
            static_cast<unsigned short>(id),
            rawName)
        || !rawName) {
        return "UNKNOWN";
    }

    const std::string value = PrepareIncomingText(
        SafeReadCString(reinterpret_cast<std::uintptr_t>(rawName), kDefaultSmallStringLimit));
    return value.empty() ? "UNKNOWN" : value;
}

bool SampApi::ResolveLocalPlayer(std::uint32_t& localPlayer) const {
    localPlayer = 0;

    std::uint32_t playerPool = 0;
    if (!ResolvePedPool(playerPool)) {
        return false;
    }

    return SafeRead(playerPool + main_offsets.SAMP_LOCAL_PLAYER_OFFSET.Get(currentVersion_), localPlayer)
        && localPlayer != 0;
}

bool SampApi::ResolveChat(std::uint32_t& chat) const {
    chat = 0;
    return CanUseOffsets() && SafeRead(ModuleBase() + main_offsets.pChat.Get(currentVersion_), chat)
        && chat != 0;
}

bool SampApi::ResolveChatInput(std::uint32_t& chatInput) const {
    chatInput = 0;
    return CanUseOffsets()
        && SafeRead(ModuleBase() + main_offsets.SAMP_CHAT_INPUT_INFO_OFFSET.Get(currentVersion_), chatInput)
        && chatInput != 0;
}

bool SampApi::ResolveDialog(std::uint32_t& dialog) const {
    dialog = 0;
    return CanUseOffsets()
        && SafeRead(ModuleBase() + main_offsets.SAMP_DIALOG_INFO_OFFSET.Get(currentVersion_), dialog) && dialog != 0;
}

bool SampApi::ResolveScoreboard(std::uint32_t& scoreboard) const {
    scoreboard = 0;
    return CanUseOffsets()
        && SafeRead(ModuleBase() + main_offsets.RefScoreboard.Get(currentVersion_), scoreboard) && scoreboard != 0;
}

std::optional<int> SampApi::GetNetGameState() const {
    std::uint32_t sampInfo = 0;
    if (!ResolveSampInfo(sampInfo) || sampInfo == 0) {
        return std::nullopt;
    }

    std::int32_t state = 0;
    if (!SafeRead(sampInfo + main_offsets.CNetGameState.Get(currentVersion_), state)) {
        return std::nullopt;
    }

    return state;
}

bool SampApi::IsScoreboardOpen() {
    std::uint32_t scoreboard = 0;
    if (!ResolveScoreboard(scoreboard) || scoreboard == 0) {
        return false;
    }

    std::int32_t enabled = 0;
    return SafeRead(scoreboard + main_offsets.CScoreboardEnabled.Get(currentVersion_), enabled) && enabled != 0;
}

bool SampApi::IsServerConnected() {
    const std::optional<int> state = GetNetGameState();
    const std::optional<int> connectedState = ConnectedGameStateValue(currentVersion_);
    return state.has_value() && connectedState.has_value() && *state == *connectedState;
}
