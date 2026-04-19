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
    { 0x00064010, 0x000640E0, 0x00067460, 0x00067460, 0x00067BA0, 0x00067BE0, 0x00067BE0, 0x00067650 }, // AddEntry
    { 0x000638A0, 0x00063970, 0x00066CF0, 0x00066CF0, 0x00067430, 0x00067470, 0x00067470, 0x00066EE0 }, // RenderEntry
    { 0x21A0E4, 0x21A0EC, 0x26E8C8, 0x26E8C8, 0x26E9F8, 0x26E9F8, 0x26EB80, 0x2ACA10 }, // pChat
    { 0x00000020, 0x00000020, 0x00000020, 0x00000020, 0x00000020, 0x00000020, 0x00000020, 0x00000020 }, // CHAT_TEXT_OFFSET
    { 0x00000004, 0x00000004, 0x00000004, 0x00000004, 0x00000004, 0x00000004, 0x00000004, 0x00000004 }, // CHAT_PREFIX_TEXT_OFFSET
    { 0x000000F4, 0x000000F4, 0x000000F4, 0x000000F4, 0x000000F4, 0x000000F4, 0x000000F4, 0x000000F4 }, // CHAT_COLOR_OFFSET
    { 0x000000F8, 0x000000F8, 0x000000F8, 0x000000F8, 0x000000F8, 0x000000F8, 0x000000F8, 0x000000F8 }, // CHAT_PREFIX_COLOR_OFFSET
    { 0x000645A0, 0x00064670, 0x000679F0, 0x000679F0, 0x00068130, 0x00068070, 0x00068170, 0x00067BE0 }, // AddChatMessage
    { 0x21A0E8, 0x21A0F0, 0x26E8CC, 0x26E8CC, 0x26E9FC, 0x26E9FC, 0x26EB84, 0x2ACA14 }, // SAMP_CHAT_INPUT_INFO_OFFSET
    { 0x000014E0, 0x000014E0, 0x000014E0, 0x000014E0, 0x000014E0, 0x000014E0, 0x000014E0, 0x000014E0 }, // CInput_Opened (m_bEnabled / opened flag)
    { 0x00064600, 0x000646D0, 0x00067A50, 0x00067A50, 0x00068190, 0x000681D0, 0x000681D0, 0x00067C40 }, // OnResetDevice
    { 0x000657E0, 0x000658B0, 0x00068D10, 0x00068D10, 0x00069440, 0x00069480, 0x00069480, 0x00068EC0 }, // CInput_Open
    { 0x000658E0, 0x000659B0, 0x00068E10, 0x00068E10, 0x00069540, 0x00069580, 0x00069580, 0x00068FC0 }, // CInput_Close
    { 0x0006B9FB, 0x00000000, 0x0006F8FB, 0x0006F8FB, 0x00000000, 0x00000000, 0x00069580, 0x00068FC0 }, // CInput_Close_fix
    { 0x000636D0, 0x000637A0, 0x00066B20, 0x00066B20, 0x00067260, 0x000672A0, 0x000672A0, 0x00066D10 }, // SetPageSize
    { 0x00064A51, 0x00064B21, 0x00067EB1, 0x00067EB1, 0x000685E1, 0x00068621, 0x00068621, 0x00068091 }, // PageSize_MAX
    { 0x000D7AD5, 0x000D7AE5, 0x000E9DB5, 0x000E9DB5, 0x000E9E0D, 0x000E9E0D, 0x000E9E0D, 0x0011BE45 }, // PageSize_StringInfo
    { 0x00065C60, 0x00065D30, 0x00069190, 0x00069190, 0x000698C0, 0x000698C0, 0x00069900, 0x00069340 }, // CInput_Send
    { 0x000057F0, 0x000057E0, 0x00005820, 0x00005820, 0x00005A00, 0x00005A00, 0x00005A10, 0x00005860 }, // CInput_SendSay
    { 0x00065D30, 0x00065E00, 0x00069260, 0x00069260, 0x00069990, 0x000699D0, 0x000699D0, 0x00069410 }, // CInput_ProcessInput
    { 0x0005D850, 0x0005D920, 0x00060BF0, 0x00060BF0, 0x00061320, 0x00061360, 0x00061360, 0x00060DE0 }, // HotkeyDispatcher (SA:MP hotkey switch)
    { 0x0005DA80, 0x0005DB50, 0x00060E20, 0x00060E20, 0x00061550, 0x00061590, 0x00061590, 0x00061010 }, // InputHotkeyHandler (secondary T/F5/Num0/Esc handler)
    { 0x00013CE0, 0x00013DA0, 0x00016F00, 0x00016F00, 0x00017570, 0x000175C0, 0x000175C0, 0x000170D0 }, // GetName
    { 0x00000004, 0x00000000, 0x00002F1C, 0x00002F1C, 0x0000000C, 0x00000004, 0x00000004, 0x00000000 }, // SAMP_SLOCALPLAYERID_OFFSET
    { 0x000003CD, 0x000003C5, 0x000003DE, 0x000003DE, 0x000003DE, 0x000003DE, 0x000003DE, 0x000003DE }, // SAMP_INFO_OFFSET_Pools
    { 0x00000018, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000004, 0x00000004, 0x00000008 }, // SAMP_INFO_OFFSET_Pools_Player
    { 0x0000001C, 0x0000000C, 0x0000000C, 0x0000000C, 0x0000000C, 0x00000000, 0x00000000, 0x0000000C }, // SAMP_INFO_OFFSET_Pools_Veh
    { 0x00216378, 0x00216380, 0x00151578, 0x00151578, 0x001516A0, 0x001516A0, 0x00151828, 0x0018F6C0 }, // SAMP_COLOR_OFFSET
    { 0x00010420, 0x000104C0, 0x00013570, 0x00013570, 0x00013890, 0x000138C0, 0x000138C0, 0x000137C0 }, // ID_Find
    { 0x000010B0, 0x000010B0, 0x000010B0, 0x000010B0, 0x000010B0, 0x000010B0, 0x000010B0, 0x000010B0 }, // CPlayerPool_IsConnected
    { 0x0001B0A0, 0x0001B180, 0x0001E440, 0x0001E440, 0x0001EB40, 0x0001EB90, 0x0001EB90, 0x0001E650 }, // IDcar_Find
    { 0x0000002E, 0x00000026, 0x00000004, 0x00000004, 0x0000002E, 0x00001F8A, 0x00001F8A, 0x00000026 }, // SAMP_PREMOTEPLAYER_OFFSET
    { 0x00000000, 0x0000000C, 0x00000000, 0x00000000, 0x00000010, 0x00000010, 0x00000010, 0x00000008 }, // SAMP_REMOTEPLAYERDATA_OFFSET
    { 0x00000000, 0x0000001C, 0x00000000, 0x00000000, 0x000001DD, 0x000001DD, 0x00000000, 0x00000004 }, // pSAMP_Actor / remoteData->sampActor
    { 0x000002A4, 0x000002A4, 0x000002A4, 0x000002A4, 0x000002A4, 0x000002A4, 0x000002A4, 0x000002A4 }, // pGTA_Ped
    { 0x000010B0, 0x000010B0, 0x000010B0, 0x000010B0, 0x000010B0, 0x000010B0, 0x000010B0, 0x000010B0 }, // IsConnected
    { 0x000001BC, 0x000001BC, 0x000001B0, 0x000001B0, 0x000001B0, 0x000001B0, 0x000001B0, 0x000001B0 }, // SAMP_REMOTEPLAYERDATA_HEALTH_OFFSET
    { 0x000001B8, 0x000001AC, 0x000001AC, 0x000001AC, 0x000001AC, 0x000001AC, 0x000001AC, 0x000001AC }, // SAMP_REMOTEPLAYERDATA_ARMOR_OFFSET
    { 0x00084850, 0x000848F0, 0x00088760, 0x00088760, 0x00088EA0, 0x00088ED0, 0x00088E70, 0x000888F0 }, // CDXUTListBox__GetSelectedIndex
    { 0x00086390, 0x00086430, 0x0008A2B0, 0x0008A2B0, 0x0008A9F0, 0x0008AA20, 0x0008A9C0, 0x0008A440 }, // CDXUTListBox__GetItem
    { 0x000863C0, 0x00086460, 0x0008A2E0, 0x0008A2E0, 0x0008AA20, 0x0008AA50, 0x0008A9F0, 0x0008A470 }, // SAMP_SET_DIALOG_LIST_ITEM_OFFSET
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
}

bool SampApi::hasSampfuncs() const {
    return GetModuleHandleA("SAMPFUNCS.asi") != nullptr;
}

void SampApi::Refresh() {
    const HMODULE currentModule = GetModuleHandleA("samp.dll");
    if (currentModule != sampModule_) {
        sampModule_ = currentModule;
        versionResolved_ = false;
        currentVersion_ = Version::Unknown;
        currentEntryPoint_ = nullptr;
        entryPointAddress_ = 0;
        supportedVersion_ = false;
        ResetChatAsiInputDiscovery();

        if (sampModule_) {
            debuglog::Write("samp.dll detected at %p", sampModule_);
        } else {
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
        SetError("SAMP version is not supported by the current offsets");
        return false;
    }

    std::uint32_t sampInfo = 0;
    if (!ResolveSampInfo(sampInfo)) {
        SetError("Failed to read SAMP info pointer");
        return false;
    }

    if (sampInfo == 0) {
        SetError("SAMP is not initialized yet");
        return false;
    }

    ClearError();
    return true;
}

std::uintptr_t SampApi::PedPool() {
    std::uint32_t pedPool = 0;
    if (!isSAMPInitilizeLua() || !ResolvePedPool(pedPool)) {
        return 0;
    }
    return pedPool;
}

std::string SampApi::GetNameID(int id) {
    if (id < 0 || id > 1003 || !IsConnected(id)) {
        return "UNKNOWN";
    }

    const std::uintptr_t pool = PedPool();
    const auto address = GetAddress(main_offsets.GetName);
    if (!pool || address == 0) {
        return "UNKNOWN";
    }

    const auto getName = reinterpret_cast<GetNameFn>(address);
    const char* rawName = nullptr;

    if (!CallGetName(getName, reinterpret_cast<void*>(pool), static_cast<unsigned short>(id), rawName) || !rawName) {
        return "UNKNOWN";
    }

    const std::string value = PrepareIncomingText(
        SafeReadCString(reinterpret_cast<std::uintptr_t>(rawName), kDefaultSmallStringLimit));
    return value.empty() ? "UNKNOWN" : value;
}

std::optional<int> SampApi::GetIDByName(std::string_view name) {
    const std::string target = TrimAscii(name);
    if (target.empty()) {
        return std::nullopt;
    }

    for (int id = 0; id <= 1003; ++id) {
        if (IsConnected(id) && GetNameID(id) == target) {
            return id;
        }
    }

    return std::nullopt;
}

bool SampApi::IsConnected(int id) {
    if (id < 0 || id > 1003) {
        return false;
    }

    const int localId = Local_ID();
    if (localId >= 0 && id == localId) {
        return true;
    }

    std::uint32_t pool = 0;
    if (!ResolvePedPool(pool) || pool == 0) {
        return false;
    }

    const auto address = GetAddress(main_offsets.CPlayerPool_IsConnected);
    if (address == 0) {
        return false;
    }

    const auto isConnected = reinterpret_cast<PlayerPoolIsConnectedFn>(address);
    bool connected = false;

    CallPlayerPoolIsConnected(isConnected, reinterpret_cast<void*>(pool), static_cast<unsigned short>(id), connected);
    return connected;
}


bool SampApi::restoreOriginalFunctionGlobals() {
    functionBackendActive_ = BACKEND_STANDARD;
    return true;
}

bool SampApi::applyFunctionBackend(std::string_view mode) {
    functionBackendMode_ = NormalizeBackendMode(mode);

    // Lua backend switching replaced globals in MoonLoader. In this C++ port
    // there is no Lua global environment, so the backend state is preserved as
    // compatibility metadata while calls stay on the native C++ implementation.
    functionBackendActive_ = BACKEND_STANDARD;
    return true;
}

bool SampApi::setFunctionBackendMode(std::string_view mode) {
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

const std::string& SampApi::lastError() const {
    return lastError_;
}

bool SampApi::DetectVersion() {
    versionResolved_ = true;

    if (!sampModule_) {
        SetError("samp.dll is not loaded");
        return false;
    }

    const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(sampModule_);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        SetError("samp.dll has an invalid DOS header");
        return false;
    }

    const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        reinterpret_cast<const std::uint8_t*>(sampModule_) + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        SetError("samp.dll has an invalid NT header");
        return false;
    }

    entryPointAddress_ = ntHeaders->OptionalHeader.AddressOfEntryPoint;

    for (const auto& info : entryPoint) {
        if (info.address == entryPointAddress_) {
            currentVersion_ = info.version;
            currentEntryPoint_ = &info;
            supportedVersion_ = info.supported;
            ClearError();

            debuglog::Write(
                "Detected SAMP version: %s (entry point 0x%X, supported=%s)",
                info.name,
                info.address,
                info.supported ? "yes" : "no");

            return true;
        }
    }

    currentVersion_ = Version::Unknown;
    currentEntryPoint_ = nullptr;
    supportedVersion_ = false;

    debuglog::Write("Unknown SAMP entry point: 0x%X", entryPointAddress_);
    SetError("Unknown SAMP version entry point");
    return false;
}

void SampApi::SetError(std::string message) {
    lastError_ = std::move(message);
}

void SampApi::ClearError() {
    lastError_.clear();
}

std::uintptr_t SampApi::ModuleBase() const {
    return reinterpret_cast<std::uintptr_t>(sampModule_);
}

std::uintptr_t SampApi::GetAddress(const VersionedOffset& offset) const {
    if (!sampModule_) {
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
    return sampModule_ && versionResolved_
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

bool SampApi::ResolveChat(std::uint32_t& chat) const {
    chat = 0;
    return sampModule_ && versionResolved_ && SafeRead(ModuleBase() + main_offsets.pChat.Get(currentVersion_), chat)
        && chat != 0;
}

bool SampApi::ResolveChatInput(std::uint32_t& chatInput) const {
    chatInput = 0;
    return sampModule_ && versionResolved_
        && SafeRead(ModuleBase() + main_offsets.SAMP_CHAT_INPUT_INFO_OFFSET.Get(currentVersion_), chatInput)
        && chatInput != 0;
}

bool SampApi::ResolveDialog(std::uint32_t& dialog) const {
    dialog = 0;
    return sampModule_ && versionResolved_
        && SafeRead(ModuleBase() + main_offsets.SAMP_DIALOG_INFO_OFFSET.Get(currentVersion_), dialog) && dialog != 0;
}
