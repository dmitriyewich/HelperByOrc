#include "samp_api.h"

#include "debug_log.h"
#include "feature_flags.h"
#include "file_hash_utils.h"
#include "minhook_utils.h"
#include "module_signature_scanner.h"
#include "samp_api/chat/samp_local_chat_entry.h"
#include "samp_hooks.h"
#include "text_encoding.h"

#include <memory>
#include <memwrapper.h>

#include <common.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t SAMP_SERVER_SETTINGS_NAME_TAG_DISTANCE_OFFSET = 0x27;
constexpr std::uint32_t SAMP_SERVER_SETTINGS_NO_NAME_TAGS_BEHIND_WALLS_OFFSET = 0x2F;
constexpr std::uint32_t SAMP_SERVER_SETTINGS_SHOW_NAME_TAGS_OFFSET = 0x38;
constexpr float kMaximumSaneNameTagDrawDistance = 10000.0f;

using GetNameFn = const char*(__thiscall*)(void*, unsigned short);
using GetEditboxTextFn = char*(__thiscall*)(void*);
using SetEditboxTextFn = void(__thiscall*)(void*, char*, int);
using SetPageSizeFn = void(__thiscall*)(void*, int);
using IdFindFn = std::uint16_t(__thiscall*)(void*, const void*);
using PlayerPoolGetPingFn = int(__thiscall*)(void*, unsigned short);
using PlayerPoolGetLocalPingFn = int(__thiscall*)(void*);
using PlayerPoolGetScoreFn = int(__thiscall*)(void*, unsigned short);
using PlayerPoolGetLocalScoreFn = int(__thiscall*)(void*);
using PlayerPoolGetCountFn = int(__thiscall*)(void*, BOOL);
using ListBoxGetSelectedIndexFn = int(__thiscall*)(void*, int);
using ListBoxGetItemFn = const char*(__thiscall*)(void*, int);
using SetCursorModeFn = char*(__thiscall*)(void*, int, bool);
using DialogCloseFn = void(__thiscall*)(void*, int);
using InputOpenCloseFn = void(__thiscall*)(void*);
using SendInputFn = void(__thiscall*)(void*, const char*);
using ProcessInputFn = void(__thiscall*)(void*);
using SetDialogListItemFn = void(__thiscall*)(void*, int);
using ChatAsiInputWriterFn = void(__cdecl*)(const char*, std::size_t, unsigned char);
using ChatAsiInputSubmitFn = void(__cdecl*)(unsigned int);
using ChatAsiInputCallbackFn = int(__cdecl*)(void*);

struct ModuleSectionRange {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    DWORD characteristics = 0;

    bool executable() const {
        return (characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    }

    bool writable() const {
        return (characteristics & IMAGE_SCN_MEM_WRITE) != 0;
    }

    bool contains(std::uintptr_t address) const {
        return address >= begin && address < end;
    }
};

constexpr std::size_t kChatAsiWriterScanWindow = 0x80;
constexpr std::size_t kChatAsiCallbackScanWindow = 0x200;
constexpr std::size_t kChatAsiSubmitScanWindow = 0x500;
constexpr std::size_t kChatAsiSubmitBacktrackWindow = 0x400;
constexpr std::size_t kDefaultSmallStringLimit = 256;
constexpr std::size_t kDefaultTextLimit = 8192;
constexpr std::uint32_t kChatAsiInputTextCallbackAlways = 0x100;
constexpr std::size_t kChatAsiCallbackDataSize = 0x30;
constexpr std::uintptr_t kChatAsiCallbackOffsetEventFlag = 0x00;
constexpr std::uintptr_t kChatAsiCallbackOffsetBuf = 0x14;
constexpr std::uintptr_t kChatAsiCallbackOffsetBufTextLen = 0x18;
constexpr std::uintptr_t kChatAsiCallbackOffsetBufSize = 0x1C;
constexpr std::uintptr_t kChatAsiCallbackOffsetCursorPos = 0x24;
constexpr std::uintptr_t kChatAsiCallbackOffsetSelectionStart = 0x28;
constexpr std::uintptr_t kChatAsiCallbackOffsetSelectionEnd = 0x2C;
constexpr int kMaxSampPlayerId = 1003;
constexpr std::uint16_t kInvalidSampPlayerId = 0xFFFF;
constexpr int kMaxSampVehicleId = 1999;
constexpr std::uint16_t kInvalidSampVehicleId = 0xFFFF;

ChatAsiInputCallbackFn g_chatAsiInputCallbackOriginal = nullptr;
SampApi* g_chatAsiInputCallbackOwner = nullptr;

constexpr const char* kSampGlobalNames[] = {
    "sampAddChatMessage",
    "sampGetChatInputText",
    "sampGetChatString",
    "sampIsChatInputActive",
    "sampIsDialogActive",
    "sampProcessChatInput",
    "sampSendChat",
    "sampSendDialogResponse",
    "sampSetChatInputEnabled",
    "sampSetChatInputText",
};

constexpr std::uint32_t kPlaceablePositionOffset = 0x4;
constexpr std::uint32_t kPlaceableMatrixOffset = 0x14;
constexpr std::uint32_t kMatrixPositionOffset = 0x30;
constexpr std::size_t kRakClientVtblSendBitStream = 6;
constexpr std::size_t kRakClientVtblRpcBitStream = 25;

std::optional<int> ConnectedGameStateValue(SampApi::Version version) {
    switch (version) {
    case SampApi::Version::R1:
    case SampApi::Version::R2:
        return 0x0E;
    case SampApi::Version::R3:
    case SampApi::Version::R3_1:
    case SampApi::Version::R4:
    case SampApi::Version::R4_2:
    case SampApi::Version::R5_1:
    case SampApi::Version::DL_R1:
        return 0x05;
    default:
        return std::nullopt;
    }
}

bool IsReadableMemory(std::uintptr_t address, std::size_t size) {
    if (address == 0 || size == 0) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    std::uintptr_t current = address;
    const std::uintptr_t finish = address + size - 1;

    while (current <= finish) {
        if (VirtualQuery(reinterpret_cast<const void*>(current), &mbi, sizeof(mbi)) == 0) {
            return false;
        }

        if (mbi.State != MEM_COMMIT) {
            return false;
        }

        if ((mbi.Protect & PAGE_GUARD) != 0 || (mbi.Protect & PAGE_NOACCESS) != 0) {
            return false;
        }

        const DWORD readable = mbi.Protect & 0xFF;
        if (readable != PAGE_READONLY && readable != PAGE_READWRITE && readable != PAGE_WRITECOPY
            && readable != PAGE_EXECUTE_READ && readable != PAGE_EXECUTE_READWRITE
            && readable != PAGE_EXECUTE_WRITECOPY) {
            return false;
        }

        const auto regionBase = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        if (regionBase == 0 || mbi.RegionSize == 0) {
            return false;
        }

        const auto regionEnd = regionBase + mbi.RegionSize;
        if (regionEnd <= current) {
            return false;
        }

        current = regionEnd;
    }

    return true;
}

bool IsWritableMemory(std::uintptr_t address, std::size_t size) {
    if (address == 0 || size == 0) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    std::uintptr_t current = address;
    const std::uintptr_t finish = address + size - 1;

    while (current <= finish) {
        if (VirtualQuery(reinterpret_cast<const void*>(current), &mbi, sizeof(mbi)) == 0) {
            return false;
        }

        if (mbi.State != MEM_COMMIT) {
            return false;
        }

        if ((mbi.Protect & PAGE_GUARD) != 0 || (mbi.Protect & PAGE_NOACCESS) != 0) {
            return false;
        }

        const DWORD writable = mbi.Protect & 0xFF;
        if (writable != PAGE_READWRITE && writable != PAGE_WRITECOPY
            && writable != PAGE_EXECUTE_READWRITE && writable != PAGE_EXECUTE_WRITECOPY) {
            return false;
        }

        const auto regionBase = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        if (regionBase == 0 || mbi.RegionSize == 0) {
            return false;
        }

        const auto regionEnd = regionBase + mbi.RegionSize;
        if (regionEnd <= current) {
            return false;
        }

        current = regionEnd;
    }

    return true;
}

template <typename T>
bool SafeRead(std::uintptr_t address, T& value) {
    __try {
        value = *reinterpret_cast<const T*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        value = T{};
        return false;
    }
}

template <typename T>
bool SafeWrite(std::uintptr_t address, const T& value) {
    __try {
        *reinterpret_cast<T*>(address) = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

const char* MemoryStateName(DWORD state) {
    switch (state) {
    case MEM_COMMIT:
        return "commit";
    case MEM_RESERVE:
        return "reserve";
    case MEM_FREE:
        return "free";
    default:
        return "unknown";
    }
}

const char* MemoryTypeName(DWORD type) {
    switch (type) {
    case MEM_IMAGE:
        return "image";
    case MEM_MAPPED:
        return "mapped";
    case MEM_PRIVATE:
        return "private";
    default:
        return "unknown";
    }
}

std::string MemoryProtectName(DWORD protect) {
    if ((protect & PAGE_GUARD) != 0) {
        return "guard";
    }
    if ((protect & PAGE_NOACCESS) != 0) {
        return "noaccess";
    }

    switch (protect & 0xFF) {
    case PAGE_READONLY:
        return "r";
    case PAGE_READWRITE:
        return "rw";
    case PAGE_WRITECOPY:
        return "wc";
    case PAGE_EXECUTE:
        return "x";
    case PAGE_EXECUTE_READ:
        return "xr";
    case PAGE_EXECUTE_READWRITE:
        return "xrw";
    case PAGE_EXECUTE_WRITECOPY:
        return "xwc";
    default:
        return "unknown";
    }
}

std::string MemoryRegionSummary(std::uintptr_t address) {
    if (address == 0) {
        return "null";
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)) == 0) {
        return "VirtualQuery=0";
    }

    char buffer[256]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "base=0x%08X size=0x%X state=%s protect=%s type=%s",
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(mbi.BaseAddress)),
        static_cast<unsigned>(mbi.RegionSize),
        MemoryStateName(mbi.State),
        MemoryProtectName(mbi.Protect).c_str(),
        MemoryTypeName(mbi.Type));
    return buffer;
}

std::string ModulePath(HMODULE module) {
    char path[MAX_PATH]{};
    if (!module || !GetModuleFileNameA(module, path, MAX_PATH)) {
        return {};
    }
    return path;
}

std::wstring ModulePathWide(HMODULE module) {
    wchar_t path[MAX_PATH]{};
    if (!module || !GetModuleFileNameW(module, path, MAX_PATH)) {
        return {};
    }
    return path;
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

std::string FileTimeText(const FILETIME& fileTime) {
    SYSTEMTIME utc{};
    SYSTEMTIME local{};
    if (!FileTimeToSystemTime(&fileTime, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
        return "unknown";
    }

    char buffer[64]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%04u-%02u-%02u %02u:%02u:%02u",
        static_cast<unsigned>(local.wYear),
        static_cast<unsigned>(local.wMonth),
        static_cast<unsigned>(local.wDay),
        static_cast<unsigned>(local.wHour),
        static_cast<unsigned>(local.wMinute),
        static_cast<unsigned>(local.wSecond));
    return buffer;
}

struct ModuleFingerprint {
    std::wstring path;
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    std::uint64_t size = 0;
    file_hash::Result hashes{};
};

struct KnownSampVariant {
    SampApi::Version version = SampApi::Version::Unknown;
    std::uint64_t fileSize = 0;
    std::uint32_t timestamp = 0;
    std::uint32_t sizeOfImage = 0;
    const char* sha256 = "";
    const char* name = "";
};

constexpr std::array<KnownSampVariant, 9> kKnownSampVariants{ {
    { SampApi::Version::R1, 2199552, 0x5542F47A, 0x330000, "7E30F3C9CD99D5E2932410F486E8139AFFA2DAD19BD65AD9C328F6A4071943F7", "R1" },
    { SampApi::Version::R2, 2220032, 0x59C30C94, 0x335000, "DE07A850590A43D83A40F9251741C07D3D0D74A217D5A09CB498A32982E8315B", "R2" },
    { SampApi::Version::R3, 1204224, 0x5C08E5BC, 0x27E000, "81D39AF30EAFE6176DE82C57EF9D2A9EAA92268B18D7B17096F67919A9248040", "R3" },
    { SampApi::Version::R3_1, 1204224, 0x5C0B4243, 0x27E000, "9C9B2CC31A4CED6967420B1880C096B5C4E7630E227AA379BE4019C21B6FDDC1", "R3-1" },
    { SampApi::Version::R3_1, 1226984, 0x5C0B4243, 0x27E000, "087ED1D0C29B9A9F24443DACDDBD3E0DDBC0EADC5673E83E8A9280E72E93DFE5", "Arizona R3-1" },
    { SampApi::Version::R4, 1204224, 0x5DD606CD, 0x27E000, "15DB80C5C9E02E011F16509D081D1CE7C8526238200814EBC16BA1F4F9FF12AB", "R4" },
    { SampApi::Version::R4_2, 1204224, 0x6094ACAB, 0x27E000, "0B0E409C2FCAF52B74131C05B3B28FCB614C78F79578A691F1B44FCDAB229088", "R4-2" },
    { SampApi::Version::R5_1, 1204224, 0x6372C39E, 0x27E000, "B72B5DBE725F81864CA3F78BC7063BDA56CC05FC7188AF822FA7A754432553A2", "R5-1" },
    { SampApi::Version::DL_R1, 1466368, 0x5A6A3130, 0x2BE000, "BCCDB297464BD382625635BE25585DF07A8FA6668BC0015650708E3EB4FFCD4B", "DL-R1" },
} };

bool ReadModuleFingerprint(HMODULE module, ModuleFingerprint& fingerprint, std::string& error) {
    fingerprint = {};
    error.clear();
    fingerprint.path = ModulePathWide(module);
    if (fingerprint.path.empty()) {
        error = "samp.dll path is unavailable";
        return false;
    }

    if (!GetFileAttributesExW(
            fingerprint.path.c_str(),
            GetFileExInfoStandard,
            &fingerprint.attributes)) {
        char buffer[128]{};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "samp.dll stat failed (gle=%lu)",
            static_cast<unsigned long>(GetLastError()));
        error = buffer;
        return false;
    }

    fingerprint.size =
        (static_cast<std::uint64_t>(fingerprint.attributes.nFileSizeHigh) << 32u)
        | fingerprint.attributes.nFileSizeLow;
    fingerprint.hashes = file_hash::Compute(fingerprint.path, true);
    if (!fingerprint.hashes.fnvOk || !fingerprint.hashes.sha256Ok) {
        char buffer[160]{};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "samp.dll fingerprint failed (fnvError=%lu shaError=%lu)",
            fingerprint.hashes.fnvError,
            fingerprint.hashes.sha256Error);
        error = buffer;
        return false;
    }
    return true;
}

void LogModuleFingerprint(const ModuleFingerprint& fingerprint, const char* label) {
    debuglog::WriteInfo(
        "[samp][file] %s size=%llu mtime=\"%s\" fnv64=%016llX fnvOk=1 fnvError=0 sha256=%s shaOk=1 shaError=0 path=\"%s\"",
        label ? label : "module",
        static_cast<unsigned long long>(fingerprint.size),
        FileTimeText(fingerprint.attributes.ftLastWriteTime).c_str(),
        static_cast<unsigned long long>(fingerprint.hashes.fnv1a64),
        fingerprint.hashes.sha256.c_str(),
        WideToUtf8(fingerprint.path).c_str());
}

bool ValidateKnownSampVariant(
    HMODULE module,
    SampApi::Version version,
    const IMAGE_NT_HEADERS32& ntHeaders,
    std::string& error) {
    ModuleFingerprint fingerprint;
    if (!ReadModuleFingerprint(module, fingerprint, error)) {
        debuglog::WriteError("[samp][variant] validation failed: %s", error.c_str());
        return false;
    }

    LogModuleFingerprint(fingerprint, "samp.dll");
    for (const KnownSampVariant& known : kKnownSampVariants) {
        if (known.version == version
            && known.fileSize == fingerprint.size
            && known.timestamp == ntHeaders.FileHeader.TimeDateStamp
            && known.sizeOfImage == ntHeaders.OptionalHeader.SizeOfImage
            && fingerprint.hashes.sha256 == known.sha256) {
            debuglog::WriteInfo(
                "[samp][variant] validation ok variant=\"%s\" timestamp=0x%08X sizeOfImage=0x%X",
                known.name,
                known.timestamp,
                known.sizeOfImage);
            error.clear();
            return true;
        }
    }

    char buffer[384]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "samp.dll exact variant is not approved (version=%d size=%llu timestamp=0x%08X sizeOfImage=0x%X sha256=%s)",
        static_cast<int>(version),
        static_cast<unsigned long long>(fingerprint.size),
        static_cast<unsigned>(ntHeaders.FileHeader.TimeDateStamp),
        static_cast<unsigned>(ntHeaders.OptionalHeader.SizeOfImage),
        fingerprint.hashes.sha256.c_str());
    error = buffer;
    debuglog::WriteError("[samp][variant] validation failed: %s", error.c_str());
    return false;
}

template <std::size_t N>
bool MatchesSignatureFragment(
    std::uintptr_t address,
    std::size_t offset,
    const std::array<std::uint8_t, N>& expected) {
    for (std::size_t i = 0; i < expected.size(); ++i) {
        std::uint8_t actual = 0;
        if (!SafeRead(address + offset + i, actual) || actual != expected[i]) {
            return false;
        }
    }
    return true;
}

bool ValidateCriticalSampSignatures(
    HMODULE module,
    SampApi::Version version,
    std::string& error) {
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(module);
    const std::uint32_t inputSendRva = SampApi::main_offsets.CInput_Send.Get(version);
    const std::uint32_t hotkeyDispatcherRva =
        SampApi::main_offsets.HotkeyDispatcher.Get(version);
    const std::uint32_t rakHandleRpcRva =
        SampApi::main_offsets.RakHandleRpc.Get(version);
    if (base == 0 || inputSendRva == 0 || hotkeyDispatcherRva == 0 || rakHandleRpcRva == 0) {
        error = "critical SAMP signature target is unavailable";
        return false;
    }

    // Fragments begin after the first five bytes so ordinary rel32 detours do
    // not invalidate the exact loaded-code check.
    constexpr std::array<std::uint8_t, 2> kInputSendFragment1{ 0x6A, 0xFF };
    constexpr std::array<std::uint8_t, 13> kInputSendFragment2{
        0x64, 0x89, 0x25, 0x00, 0x00, 0x00, 0x00,
        0x81, 0xEC, 0x18, 0x01, 0x00, 0x00,
    };
    constexpr std::array<std::uint8_t, 14> kHotkeyDispatcherFragment{
        0xC0, 0xF3, 0x83, 0xF8, 0x6B, 0x0F, 0x87,
        0x80, 0x01, 0x00, 0x00, 0x0F, 0xB6, 0x80,
    };
    constexpr std::array<std::uint8_t, 3> kRakHandleRpcFragment1{ 0x6A, 0xFF, 0x68 };
    constexpr std::array<std::uint8_t, 20> kRakHandleRpcFragment2{
        0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50,
        0x64, 0x89, 0x25, 0x00, 0x00, 0x00, 0x00,
        0x81, 0xEC, 0x2C, 0x01, 0x00, 0x00,
    };

    const std::uintptr_t inputSend = base + inputSendRva;
    if (!MatchesSignatureFragment(inputSend, 6, kInputSendFragment1)
        || !MatchesSignatureFragment(inputSend, 19, kInputSendFragment2)) {
        error = "CInput_Send loaded-code signature mismatch";
        return false;
    }

    const std::uintptr_t hotkeyDispatcher = base + hotkeyDispatcherRva;
    if (!MatchesSignatureFragment(
            hotkeyDispatcher,
            5,
            kHotkeyDispatcherFragment)) {
        error = "HotkeyDispatcher loaded-code signature mismatch";
        return false;
    }

    const std::uintptr_t rakHandleRpc = base + rakHandleRpcRva;
    if (!MatchesSignatureFragment(rakHandleRpc, 5, kRakHandleRpcFragment1)
        || !MatchesSignatureFragment(rakHandleRpc, 12, kRakHandleRpcFragment2)) {
        error = "RakHandleRpc loaded-code signature mismatch";
        return false;
    }

    debuglog::WriteInfo(
        "[samp][variant] loaded-code signatures ok targets=CInput_Send,HotkeyDispatcher,RakHandleRpc");
    error.clear();
    return true;
}

std::string ModuleOwnerSummary(std::uintptr_t address) {
    if (address == 0) {
        return "target=null";
    }

    HMODULE module = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(address),
            &module)
        || !module) {
        char buffer[256]{};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "target=0x%08X module=<unknown> region=\"%s\"",
            static_cast<unsigned>(address),
            MemoryRegionSummary(address).c_str());
        return buffer;
    }

    const std::uintptr_t moduleBase = reinterpret_cast<std::uintptr_t>(module);
    char buffer[512]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "target=0x%08X module='%s' rva=0x%X region='%s'",
        static_cast<unsigned>(address),
        ModulePath(module).c_str(),
        static_cast<unsigned>(address - moduleBase),
        MemoryRegionSummary(address).c_str());
    return buffer;
}

std::string DecodeControlTransfer(std::uintptr_t address) {
    if (address == 0 || !IsReadableMemory(address, 8)) {
        return "unreadable";
    }

    std::uint8_t op0 = 0;
    if (!SafeRead(address, op0)) {
        return "read-failed";
    }

    if (op0 == 0xE9 || op0 == 0xE8) {
        std::int32_t rel = 0;
        if (!SafeRead(address + 1, rel)) {
            return "rel32-read-failed";
        }

        const std::uintptr_t target = address + 5 + rel;
        return std::string(op0 == 0xE9 ? "rel32-jmp " : "rel32-call ") + ModuleOwnerSummary(target);
    }

    if (op0 == 0xEB) {
        std::int8_t rel = 0;
        if (!SafeRead(address + 1, rel)) {
            return "rel8-read-failed";
        }

        const std::uintptr_t target = address + 2 + rel;
        return "rel8-jmp " + ModuleOwnerSummary(target);
    }

    std::uint8_t op1 = 0;
    SafeRead(address + 1, op1);
    if (op0 == 0xFF && (op1 == 0x25 || op1 == 0x15)) {
        std::uint32_t pointerAddress = 0;
        if (!SafeRead(address + 2, pointerAddress)) {
            return "abs-indirect-read-failed";
        }

        std::uint32_t target = 0;
        const bool targetOk = IsReadableMemory(pointerAddress, sizeof(target)) && SafeRead(pointerAddress, target);
        char buffer[128]{};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%s ptr=0x%08X read=%d ",
            op1 == 0x25 ? "abs-indirect-jmp" : "abs-indirect-call",
            static_cast<unsigned>(pointerAddress),
            targetOk ? 1 : 0);
        return std::string(buffer) + ModuleOwnerSummary(target);
    }

    if (op0 == 0x68) {
        std::uint32_t target = 0;
        std::uint8_t retOp = 0;
        if (SafeRead(address + 1, target) && SafeRead(address + 5, retOp) && (retOp == 0xC3 || retOp == 0xCB)) {
            return "push-ret " + ModuleOwnerSummary(target);
        }
    }

    if (op0 == 0xB8) {
        std::uint32_t target = 0;
        std::uint8_t op5 = 0;
        std::uint8_t op6 = 0;
        if (SafeRead(address + 1, target) && SafeRead(address + 5, op5) && SafeRead(address + 6, op6)
            && op5 == 0xFF && (op6 == 0xE0 || op6 == 0xD0)) {
            return std::string(op6 == 0xE0 ? "mov-eax-jmp " : "mov-eax-call ") + ModuleOwnerSummary(target);
        }
    }

    return "none";
}

std::string HexBytes(std::uintptr_t address, std::size_t count) {
    if (address == 0 || count == 0 || count > 16 || !IsReadableMemory(address, count)) {
        return "unreadable";
    }

    std::ostringstream out;
    out.setf(std::ios::hex, std::ios::basefield);
    out.fill('0');
    for (std::size_t i = 0; i < count; ++i) {
        std::uint8_t value = 0;
        if (!SafeRead(address + i, value)) {
            return "read-failed";
        }
        char byteText[4]{};
        std::snprintf(byteText, sizeof(byteText), "%02X", static_cast<unsigned>(value));
        if (i != 0) {
            out << ' ';
        }
        out << byteText;
    }
    return out.str();
}

bool LooksLikeReadablePedPointer(std::uint32_t gtaPed) {
    if (gtaPed < 0x10000) {
        return false;
    }

    std::uint32_t matrix = 0;
    if (!SafeRead(gtaPed + kPlaceableMatrixOffset, matrix)) {
        return false;
    }

    struct PositionProbe {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    } probe;

    if (matrix != 0) {
        return SafeRead(matrix + kMatrixPositionOffset, probe);
    }

    return SafeRead(gtaPed + kPlaceablePositionOffset, probe);
}

bool TryReadPedPosition(std::uint32_t gtaPed, float& x, float& y, float& z) {
    if (gtaPed < 0x10000) {
        return false;
    }

    struct PositionProbe {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    } probe;

    std::uint32_t matrix = 0;
    if (SafeRead(gtaPed + kPlaceableMatrixOffset, matrix) && matrix != 0
        && SafeRead(matrix + kMatrixPositionOffset, probe)) {
        x = probe.x;
        y = probe.y;
        z = probe.z;
        return true;
    }

    if (!SafeRead(gtaPed + kPlaceablePositionOffset, probe)) {
        return false;
    }

    x = probe.x;
    y = probe.y;
    z = probe.z;
    return true;
}

bool TryGetModuleSections(
    HMODULE module,
    std::uintptr_t& imageBase,
    std::uintptr_t& imageEnd,
    std::vector<ModuleSectionRange>& sections) {
    imageBase = 0;
    imageEnd = 0;
    sections.clear();

    if (!module) {
        return false;
    }

    if (!module_signature_scanner::GetLoadedModuleImageRange(module, imageBase, imageEnd)) {
        return false;
    }

    const auto base = imageBase;
    const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE || dosHeader->e_lfanew <= 0) {
        return false;
    }

    const auto ntOffset = static_cast<std::uintptr_t>(dosHeader->e_lfanew);
    if (ntOffset > imageEnd - imageBase || sizeof(IMAGE_NT_HEADERS32) > imageEnd - imageBase - ntOffset) {
        return false;
    }

    const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + ntOffset);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE
        || ntHeaders->FileHeader.NumberOfSections == 0
        || ntHeaders->FileHeader.NumberOfSections > 96) {
        return false;
    }

    const IMAGE_SECTION_HEADER* sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);
    const std::uintptr_t sectionHeadersBegin = reinterpret_cast<std::uintptr_t>(sectionHeader);
    const std::size_t sectionHeadersSize =
        static_cast<std::size_t>(ntHeaders->FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
    if (sectionHeadersBegin < imageBase
        || sectionHeadersBegin >= imageEnd
        || sectionHeadersSize > imageEnd - sectionHeadersBegin) {
        return false;
    }

    for (unsigned short index = 0; index < ntHeaders->FileHeader.NumberOfSections; ++index, ++sectionHeader) {
        if (sectionHeader->VirtualAddress > imageEnd - imageBase) {
            continue;
        }
        const std::uintptr_t sectionBase = imageBase + sectionHeader->VirtualAddress;
        const std::size_t sectionSize =
            std::max<std::size_t>(sectionHeader->Misc.VirtualSize, sectionHeader->SizeOfRawData);
        if (sectionSize == 0 || sectionBase >= imageEnd) {
            continue;
        }

        const std::uintptr_t sectionEnd = sectionSize > imageEnd - sectionBase
            ? imageEnd
            : sectionBase + sectionSize;
        sections.push_back({ sectionBase, sectionEnd, sectionHeader->Characteristics });
    }

    return !sections.empty();
}

const ModuleSectionRange* FindSectionForAddress(
    const std::vector<ModuleSectionRange>& sections,
    std::uintptr_t address,
    bool executableOnly = false) {
    for (const auto& section : sections) {
        if (executableOnly && !section.executable()) {
            continue;
        }
        if (section.contains(address)) {
            return &section;
        }
    }

    return nullptr;
}

std::string FormatModuleCodeBytes(
    const std::vector<ModuleSectionRange>& sections,
    std::uintptr_t address,
    std::size_t count) {
    const ModuleSectionRange* section = FindSectionForAddress(sections, address, true);
    if (!section || count == 0 || address + count < address || address + count > section->end) {
        return "unavailable";
    }

    std::string result;
    result.reserve(count * 3);
    char byteText[4]{};
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(address);
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0) {
            result.push_back(' ');
        }
        std::snprintf(byteText, sizeof(byteText), "%02X", bytes[index]);
        result.append(byteText);
    }
    return result;
}

bool IsAddressInModule(std::uintptr_t address, std::uintptr_t imageBase, std::uintptr_t imageEnd) {
    return address >= imageBase && address < imageEnd;
}

bool IsAddressInWritableSection(const std::vector<ModuleSectionRange>& sections, std::uintptr_t address) {
    for (const auto& section : sections) {
        if (section.writable() && section.contains(address)) {
            return true;
        }
    }

    return false;
}

std::uintptr_t FindRuntimeAsciiStringLiteral(HMODULE module, std::string_view value) {
    if (module == nullptr || value.empty()) {
        return 0;
    }

    std::vector<module_signature_scanner::PatternByte> pattern;
    pattern.reserve(value.size() + 1);
    for (const unsigned char ch : value) {
        pattern.push_back(module_signature_scanner::PatternByte{ ch, false });
    }
    pattern.push_back(module_signature_scanner::PatternByte{ 0, false });

    const auto matches = module_signature_scanner::FindPatterns(
        module,
        pattern,
        1,
        module_signature_scanner::ScanRegion::NonExecutable);
    return matches.empty() ? 0 : matches.front();
}

std::vector<std::uintptr_t> FindPushImmediateRefs(
    const std::vector<ModuleSectionRange>& sections,
    std::uintptr_t target) {
    std::vector<std::uintptr_t> refs;

    for (const auto& section : sections) {
        if (!section.executable()) {
            continue;
        }

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(section.begin);
        const std::size_t sectionSize = section.end - section.begin;
        for (std::size_t offset = 0; offset + 5 <= sectionSize; ++offset) {
            if (bytes[offset] != 0x68) {
                continue;
            }

            std::uint32_t immediate = 0;
            std::memcpy(&immediate, bytes + offset + 1, sizeof(immediate));
            if (immediate == target) {
                refs.push_back(section.begin + offset);
            }
        }
    }

    return refs;
}

std::uintptr_t ResolveRelativeTarget(std::uintptr_t instructionAddress) {
    std::int32_t displacement = 0;
    std::memcpy(&displacement, reinterpret_cast<const void*>(instructionAddress + 1), sizeof(displacement));
    return instructionAddress + 5 + displacement;
}

bool ReadAbsoluteEcxLoad(
    const std::uint8_t* bytes,
    std::size_t size,
    std::size_t offset,
    std::uint32_t& value) {
    value = 0;
    if (offset + 5 <= size && bytes[offset] == 0xB9) {
        std::memcpy(&value, bytes + offset + 1, sizeof(value));
        return true;
    }
    if (offset + 6 <= size && bytes[offset] == 0x8D && bytes[offset + 1] == 0x0D) {
        std::memcpy(&value, bytes + offset + 2, sizeof(value));
        return true;
    }
    return false;
}

std::uintptr_t FindNearbyWrapperCall(
    std::uintptr_t stringRef,
    std::uintptr_t imageBase,
    std::uintptr_t imageEnd) {
    constexpr std::size_t kMaxForwardScan = 16;

    for (std::size_t offset = 5; offset <= kMaxForwardScan; ++offset) {
        const auto address = stringRef + offset;
        std::uint8_t opcode = 0;
        if (!SafeRead(address, opcode)) {
            break;
        }

        if (opcode != 0xE8) {
            continue;
        }

        const std::uintptr_t target = ResolveRelativeTarget(address);
        if (IsAddressInModule(target, imageBase, imageEnd)) {
            return target;
        }
    }

    return 0;
}

std::uintptr_t FindWritablePushBefore(
    std::uintptr_t stringRef,
    const std::vector<ModuleSectionRange>& sections) {
    constexpr std::size_t kMaxBacktrack = 32;

    for (std::size_t distance = 1; distance <= kMaxBacktrack; ++distance) {
        const auto address = stringRef - distance;
        std::uint8_t opcode = 0;
        if (!SafeRead(address, opcode)) {
            continue;
        }

        if (opcode != 0x68) {
            continue;
        }

        std::uint32_t immediate = 0;
        if (!SafeRead(address + 1, immediate)) {
            continue;
        }

        if (IsAddressInWritableSection(sections, immediate)) {
            return immediate;
        }
    }

    return 0;
}

std::uintptr_t FindExecutablePushBefore(
    std::uintptr_t stringRef,
    const std::vector<ModuleSectionRange>& sections,
    std::uintptr_t imageBase,
    std::uintptr_t imageEnd) {
    constexpr std::size_t kMaxBacktrack = 48;

    for (std::size_t distance = 1; distance <= kMaxBacktrack; ++distance) {
        const auto address = stringRef - distance;
        std::uint8_t opcode = 0;
        if (!SafeRead(address, opcode)) {
            continue;
        }

        if (opcode != 0x68) {
            continue;
        }

        std::uint32_t immediate = 0;
        if (!SafeRead(address + 1, immediate)) {
            continue;
        }

        if (!IsAddressInModule(immediate, imageBase, imageEnd)) {
            continue;
        }

        const auto* section = FindSectionForAddress(sections, immediate, true);
        if (section != nullptr) {
            return immediate;
        }
    }

    return 0;
}

bool ValidateChatAsiInputCallback(
    const std::vector<ModuleSectionRange>& sections,
    std::uintptr_t callback,
    std::uintptr_t dirtyFlag) {
    const auto* section = FindSectionForAddress(sections, callback, true);
    if (!section) {
        return false;
    }

    const std::size_t callbackOffset = static_cast<std::size_t>(callback - section->begin);
    const std::size_t sectionSize = section->end - section->begin;
    if (callbackOffset + 3 > sectionSize) {
        return false;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(section->begin);
    if (bytes[callbackOffset] != 0x55 || bytes[callbackOffset + 1] != 0x8B || bytes[callbackOffset + 2] != 0xEC) {
        return false;
    }

    const std::size_t window = std::min<std::size_t>(kChatAsiCallbackScanWindow, sectionSize - callbackOffset);
    bool sawCallbackDataArg = false;
    bool sawAlwaysEventCompare = false;
    bool sawDirtyCheck = dirtyFlag == 0;
    bool sawBufTextLenPush = false;
    bool sawBufPointerUse = false;
    int internalCallCount = 0;
    bool sawDirtyClear = dirtyFlag == 0;

    for (std::size_t index = 0; index < window; ++index) {
        const std::size_t absoluteIndex = callbackOffset + index;

        if (index + 3 <= window && bytes[absoluteIndex] == 0x8B && bytes[absoluteIndex + 1] == 0x75
            && bytes[absoluteIndex + 2] == 0x08) {
            sawCallbackDataArg = true;
        }

        if (index + 5 <= window && bytes[absoluteIndex] == 0x3D) {
            std::uint32_t immediate = 0;
            std::memcpy(&immediate, bytes + absoluteIndex + 1, sizeof(immediate));
            if (immediate == kChatAsiInputTextCallbackAlways) {
                sawAlwaysEventCompare = true;
            }
        }

        if (dirtyFlag != 0 && index + 7 <= window && bytes[absoluteIndex] == 0x80
            && bytes[absoluteIndex + 1] == 0x3D && bytes[absoluteIndex + 6] == 0x00) {
            std::uint32_t absolute = 0;
            std::memcpy(&absolute, bytes + absoluteIndex + 2, sizeof(absolute));
            if (absolute == dirtyFlag) {
                sawDirtyCheck = true;
            }
        }

        if (index + 3 <= window && bytes[absoluteIndex] == 0xFF && bytes[absoluteIndex + 1] == 0x76
            && bytes[absoluteIndex + 2] == kChatAsiCallbackOffsetBufTextLen) {
            sawBufTextLenPush = true;
        }

        if (index + 3 <= window && bytes[absoluteIndex] == 0xFF && bytes[absoluteIndex + 1] == 0x76
            && bytes[absoluteIndex + 2] == kChatAsiCallbackOffsetBuf) {
            sawBufPointerUse = true;
        }

        if (index + 5 <= window && bytes[absoluteIndex] == 0xE8) {
            const std::uintptr_t callAddress = section->begin + absoluteIndex;
            const std::uintptr_t target = ResolveRelativeTarget(callAddress);
            if (FindSectionForAddress(sections, target, true) != nullptr) {
                ++internalCallCount;
            }
        }

        if (dirtyFlag != 0 && index + 7 <= window && bytes[absoluteIndex] == 0xC6
            && bytes[absoluteIndex + 1] == 0x05 && bytes[absoluteIndex + 6] == 0x00) {
            std::uint32_t absolute = 0;
            std::memcpy(&absolute, bytes + absoluteIndex + 2, sizeof(absolute));
            if (absolute == dirtyFlag) {
                sawDirtyClear = true;
            }
        }
    }

    return sawCallbackDataArg && sawAlwaysEventCompare && sawDirtyCheck
        && sawBufTextLenPush && sawBufPointerUse && internalCallCount >= 2 && sawDirtyClear;
}

std::uintptr_t RecoverFunctionStart(
    const ModuleSectionRange& section,
    std::uintptr_t address,
    std::size_t maxBacktrack) {
    if (!section.contains(address)) {
        return 0;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(section.begin);
    const std::ptrdiff_t offset = static_cast<std::ptrdiff_t>(address - section.begin);
    const std::ptrdiff_t minOffset =
        std::max<std::ptrdiff_t>(0, offset - static_cast<std::ptrdiff_t>(maxBacktrack));

    for (std::ptrdiff_t start = offset - 2; start >= minOffset; --start) {
        if (bytes[start] == 0x55 && bytes[start + 1] == 0x8B && bytes[start + 2] == 0xEC) {
            return section.begin + static_cast<std::uintptr_t>(start);
        }
    }

    return 0;
}

bool FindChatAsiWriterForBuffer(
    const std::vector<ModuleSectionRange>& sections,
    std::uintptr_t imageBase,
    std::uintptr_t imageEnd,
    std::uintptr_t inputBuffer,
    std::uintptr_t& writer,
    std::uintptr_t& dirtyFlag) {
    writer = 0;
    dirtyFlag = 0;

    for (const auto& section : sections) {
        if (!section.executable()) {
            continue;
        }

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(section.begin);
        const std::size_t sectionSize = section.end - section.begin;

        for (std::size_t offset = 0; offset + 5 <= sectionSize; ++offset) {
            std::uint32_t immediate = 0;
            if (!ReadAbsoluteEcxLoad(bytes, sectionSize, offset, immediate)) {
                continue;
            }
            if (immediate != inputBuffer) {
                continue;
            }

            const std::uintptr_t candidate =
                RecoverFunctionStart(section, section.begin + offset, kChatAsiWriterScanWindow);
            if (candidate == 0) {
                continue;
            }

            const std::size_t candidateOffset = static_cast<std::size_t>(candidate - section.begin);
            const std::size_t window =
                std::min<std::size_t>(kChatAsiWriterScanWindow, sectionSize - candidateOffset);
            bool sawBufferLoad = false;
            bool sawCall = false;
            bool sawDirtyStore = false;
            bool sawReturn = false;
            std::uintptr_t candidateDirtyFlag = 0;

            for (std::size_t index = 0; index < window; ++index) {
                const std::uintptr_t address = section.begin + candidateOffset + index;
                const std::uint8_t opcode = bytes[candidateOffset + index];

                std::uint32_t bufferLoad = 0;
                if (ReadAbsoluteEcxLoad(
                        bytes + candidateOffset,
                        window,
                        index,
                        bufferLoad)
                    && bufferLoad == inputBuffer) {
                    sawBufferLoad = true;
                }

                if (index + 7 <= window && opcode == 0xC6 && bytes[candidateOffset + index + 1] == 0x05
                    && bytes[candidateOffset + index + 6] == 0x01) {
                    std::uint32_t value = 0;
                    std::memcpy(&value, bytes + candidateOffset + index + 2, sizeof(value));
                    if (IsAddressInWritableSection(sections, value)) {
                        sawDirtyStore = true;
                        candidateDirtyFlag = value;
                    }
                }

                if (index + 5 <= window && opcode == 0xE8) {
                    const auto target = ResolveRelativeTarget(address);
                    if (IsAddressInModule(target, imageBase, imageEnd)) {
                        sawCall = true;
                    }
                }

                if (opcode == 0xC3 || opcode == 0xC2) {
                    sawReturn = true;
                    break;
                }
            }

            if (sawBufferLoad && sawCall && sawDirtyStore && sawReturn) {
                writer = candidate;
                dirtyFlag = candidateDirtyFlag;
                return true;
            }
        }
    }

    return false;
}

bool MatchChatAsiSubmitCandidate(
    const std::vector<ModuleSectionRange>& sections,
    std::uintptr_t imageBase,
    std::uintptr_t imageEnd,
    std::uintptr_t inputBuffer,
    std::uintptr_t candidate,
    std::uintptr_t& dirtyFlag) {
    dirtyFlag = 0;

    const auto* candidateSection = FindSectionForAddress(sections, candidate, true);
    if (!candidateSection) {
        return false;
    }

    const std::uint32_t inputBuffer32 = static_cast<std::uint32_t>(inputBuffer);
    const std::uint32_t inputLength32 = static_cast<std::uint32_t>(inputBuffer + 0x10);
    const std::uint32_t inputCapacity32 = static_cast<std::uint32_t>(inputBuffer + 0x14);
    const std::size_t candidateOffset = static_cast<std::size_t>(candidate - candidateSection->begin);
    const auto* candidateBytes = reinterpret_cast<const std::uint8_t*>(candidateSection->begin);
    const std::size_t window = std::min<std::size_t>(kChatAsiSubmitScanWindow, candidateSection->end - candidate);
    bool sawBufferLoad = false;
    bool sawLengthCmp = false;
    bool sawLengthPush = false;
    bool sawCapacityCmp = false;
    bool sawLengthClear = false;
    bool sawZeroTerminator = false;
    bool sawDirtyStore = false;
    bool sawReturn = false;
    int internalCallCount = 0;
    std::uintptr_t candidateDirtyFlag = 0;

    for (std::size_t index = 0; index < window; ++index) {
        const std::uintptr_t address = candidate + index;
        const std::uint8_t opcode = candidateBytes[candidateOffset + index];

        if (index + 5 <= window
            && ((opcode >= 0xB8 && opcode <= 0xBF) || opcode == 0x68 || opcode == 0xA1)) {
            std::uint32_t immediate = 0;
            std::memcpy(&immediate, candidateBytes + candidateOffset + index + 1, sizeof(immediate));
            if (immediate == inputBuffer32) {
                sawBufferLoad = true;
            }
        }

        if (index + 6 <= window && opcode == 0xFF && candidateBytes[candidateOffset + index + 1] == 0x35) {
            std::uint32_t immediate = 0;
            std::memcpy(&immediate, candidateBytes + candidateOffset + index + 2, sizeof(immediate));
            if (immediate == inputLength32) {
                sawLengthPush = true;
            }
        }

        if (index + 7 <= window && opcode == 0x83 && candidateBytes[candidateOffset + index + 1] == 0x3D) {
            std::uint32_t absolute = 0;
            std::memcpy(&absolute, candidateBytes + candidateOffset + index + 2, sizeof(absolute));
            const std::uint8_t immediate = candidateBytes[candidateOffset + index + 6];
            if (absolute == inputLength32 && immediate == 0x00) {
                sawLengthCmp = true;
            }
            if (absolute == inputCapacity32 && immediate == 0x0F) {
                sawCapacityCmp = true;
            }
        }

        if (index + 10 <= window && opcode == 0xC7 && candidateBytes[candidateOffset + index + 1] == 0x05) {
            std::uint32_t absolute = 0;
            std::uint32_t immediate = 0;
            std::memcpy(&absolute, candidateBytes + candidateOffset + index + 2, sizeof(absolute));
            std::memcpy(&immediate, candidateBytes + candidateOffset + index + 6, sizeof(immediate));
            if (absolute == inputLength32 && immediate == 0) {
                sawLengthClear = true;
            }
        }

        if (index + 3 <= window && opcode == 0xC6
            && (candidateBytes[candidateOffset + index + 1] == 0x06
                || candidateBytes[candidateOffset + index + 1] == 0x00)
            && candidateBytes[candidateOffset + index + 2] == 0x00) {
            sawZeroTerminator = true;
        }

        if (index + 7 <= window && opcode == 0xC6 && candidateBytes[candidateOffset + index + 1] == 0x05
            && candidateBytes[candidateOffset + index + 6] == 0x01) {
            std::uint32_t absolute = 0;
            std::memcpy(&absolute, candidateBytes + candidateOffset + index + 2, sizeof(absolute));
            if (IsAddressInWritableSection(sections, absolute)) {
                sawDirtyStore = true;
                candidateDirtyFlag = absolute;
            }
        }

        if (index + 5 <= window && opcode == 0xE8) {
            const auto target = ResolveRelativeTarget(address);
            if (IsAddressInModule(target, imageBase, imageEnd)) {
                ++internalCallCount;
            }
        }

        if (opcode == 0xC3 || opcode == 0xC2) {
            sawReturn = true;
            if (sawBufferLoad && sawLengthCmp && sawLengthClear && internalCallCount >= 3
                && (sawLengthPush || sawCapacityCmp) && (sawZeroTerminator || sawDirtyStore)) {
                break;
            }
        }
    }

    if (!sawBufferLoad || !sawLengthCmp || !sawLengthClear || !sawReturn || internalCallCount < 3
        || (!sawLengthPush && !sawCapacityCmp) || (!sawZeroTerminator && !sawDirtyStore)) {
        return false;
    }

    dirtyFlag = candidateDirtyFlag;
    return true;
}

bool FindChatAsiSubmitForBuffer(
    const std::vector<ModuleSectionRange>& sections,
    std::uintptr_t imageBase,
    std::uintptr_t imageEnd,
    std::uintptr_t inputBuffer,
    std::uintptr_t& submit,
    std::uintptr_t& dirtyFlag) {
    submit = 0;
    dirtyFlag = 0;

    const std::uint32_t inputLength32 = static_cast<std::uint32_t>(inputBuffer + 0x10);

    for (const std::uint32_t expectedArg : { 1u, 0u }) {
        for (const auto& section : sections) {
            if (!section.executable()) {
                continue;
            }

            const auto* bytes = reinterpret_cast<const std::uint8_t*>(section.begin);
            const std::size_t sectionSize = section.end - section.begin;

            for (std::size_t offset = 0; offset + 7 <= sectionSize; ++offset) {
                std::uintptr_t callAddress = 0;
                if (bytes[offset] == 0x6A && bytes[offset + 1] == expectedArg && bytes[offset + 2] == 0xE8) {
                    callAddress = section.begin + offset + 2;
                } else if (offset + 10 <= sectionSize && bytes[offset] == 0x68 && bytes[offset + 5] == 0xE8) {
                    std::uint32_t immediate = 0;
                    std::memcpy(&immediate, bytes + offset + 1, sizeof(immediate));
                    if (immediate != expectedArg) {
                        continue;
                    }
                    callAddress = section.begin + offset + 5;
                } else {
                    continue;
                }

                const std::uintptr_t candidate = ResolveRelativeTarget(callAddress);
                if (!IsAddressInModule(candidate, imageBase, imageEnd)) {
                    continue;
                }

                std::uintptr_t candidateDirtyFlag = 0;
                if (MatchChatAsiSubmitCandidate(
                        sections,
                        imageBase,
                        imageEnd,
                        inputBuffer,
                        candidate,
                        candidateDirtyFlag)) {
                    submit = candidate;
                    dirtyFlag = candidateDirtyFlag;
                    return true;
                }
            }
        }
    }

    std::vector<std::uintptr_t> checkedCandidates;
    for (const auto& section : sections) {
        if (!section.executable()) {
            continue;
        }

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(section.begin);
        const std::size_t sectionSize = section.end - section.begin;

        for (std::size_t offset = 0; offset + 10 <= sectionSize; ++offset) {
            bool referencesInputLength = false;

            if (offset + 7 <= sectionSize && bytes[offset] == 0x83 && bytes[offset + 1] == 0x3D
                && bytes[offset + 6] == 0x00) {
                std::uint32_t absolute = 0;
                std::memcpy(&absolute, bytes + offset + 2, sizeof(absolute));
                referencesInputLength = absolute == inputLength32;
            }

            if (!referencesInputLength && bytes[offset] == 0xC7 && bytes[offset + 1] == 0x05) {
                std::uint32_t absolute = 0;
                std::memcpy(&absolute, bytes + offset + 2, sizeof(absolute));
                referencesInputLength = absolute == inputLength32;
            }

            if (!referencesInputLength) {
                continue;
            }

            const std::uintptr_t candidate =
                RecoverFunctionStart(section, section.begin + offset, kChatAsiSubmitBacktrackWindow);
            if (candidate == 0
                || std::find(checkedCandidates.begin(), checkedCandidates.end(), candidate) != checkedCandidates.end()) {
                continue;
            }

            checkedCandidates.push_back(candidate);

            std::uintptr_t candidateDirtyFlag = 0;
            if (MatchChatAsiSubmitCandidate(
                    sections,
                    imageBase,
                    imageEnd,
                    inputBuffer,
                    candidate,
                    candidateDirtyFlag)) {
                submit = candidate;
                dirtyFlag = candidateDirtyFlag;
                return true;
            }
        }
    }

    return false;
}

std::uint32_t ReadGamePedFromSampPed(std::uint32_t sampPed, SampApi::Version version) {
    const std::uint32_t gamePedOffset = SampApi::main_offsets.SampPed_GamePed.Get(version);
    if (sampPed < 0x10000 || gamePedOffset == 0) {
        return 0;
    }

    std::uint32_t gtaPed = 0;
    if (!SafeRead(sampPed + gamePedOffset, gtaPed) || gtaPed == 0) {
        return 0;
    }

    return LooksLikeReadablePedPointer(gtaPed) ? gtaPed : 0;
}

std::string SafeReadCString(std::uintptr_t address, std::size_t maxSize) {
    if (address == 0 || maxSize == 0) {
        return {};
    }

    std::string value;
    value.reserve(std::min<std::size_t>(maxSize, 128));

    for (std::size_t i = 0; i < maxSize; ++i) {
        char ch = '\0';
        if (!SafeRead(address + i, ch) || ch == '\0') {
            break;
        }
        value.push_back(ch);
    }

    return value;
}

std::string TrimAscii(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();

    while (begin < end && static_cast<unsigned char>(text[begin]) <= 0x20) {
        ++begin;
    }
    while (end > begin && static_cast<unsigned char>(text[end - 1]) <= 0x20) {
        --end;
    }

    return std::string(text.substr(begin, end - begin));
}

std::string NormalizeBackendMode(std::string_view mode) {
    std::string normalized;
    normalized.reserve(mode.size());
    for (const unsigned char ch : mode) {
        normalized.push_back(static_cast<char>(std::tolower(ch)));
    }

    if (normalized == SampApi::BACKEND_SAMPFUNCS) {
        return normalized;
    }
    return SampApi::BACKEND_STANDARD;
}

bool CallGetName(GetNameFn fn, void* pool, unsigned short id, const char*& out) {
    __try {
        out = fn(pool, id);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = nullptr;
        return false;
    }
}

bool CallSetEditboxText(SetEditboxTextFn fn, void* editBox, char* text) {
    __try {
        fn(editBox, text, 0);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallGetEditboxText(GetEditboxTextFn fn, void* editBox, char*& out) {
    __try {
        out = fn(editBox);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = nullptr;
        return false;
    }
}

bool CallPlayerPoolGetPing(PlayerPoolGetPingFn fn, void* pool, unsigned short id, int& out) {
    __try {
        out = fn(pool, id);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = 0;
        return false;
    }
}

bool CallPlayerPoolGetLocalPing(PlayerPoolGetLocalPingFn fn, void* pool, int& out) {
    __try {
        out = fn(pool);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = 0;
        return false;
    }
}

bool CallPlayerPoolGetScore(PlayerPoolGetScoreFn fn, void* pool, unsigned short id, int& out) {
    __try {
        out = fn(pool, id);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = 0;
        return false;
    }
}

bool CallPlayerPoolGetLocalScore(PlayerPoolGetLocalScoreFn fn, void* pool, int& out) {
    __try {
        out = fn(pool);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = 0;
        return false;
    }
}

bool CallPlayerPoolGetCount(PlayerPoolGetCountFn fn, void* pool, BOOL includeNpc, int& out) {
    __try {
        out = fn(pool, includeNpc);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = 0;
        return false;
    }
}

bool CallSetCursorMode(SetCursorModeFn fn, void* game, int mode, bool enabled) {
    __try {
        fn(game, mode, enabled);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallDialogClose(DialogCloseFn fn, void* dialog, int button) {
    __try {
        fn(dialog, button);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallInputOpenClose(InputOpenCloseFn fn, void* input) {
    __try {
        fn(input);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallSetDialogListItem(SetDialogListItemFn fn, void* listBox, int index) {
    __try {
        fn(listBox, index);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallListBoxGetSelectedIndex(ListBoxGetSelectedIndexFn fn, void* listBox, int arg, int& out) {
    __try {
        out = fn(listBox, arg);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = -1;
        return false;
    }
}

bool CallListBoxGetItem(ListBoxGetItemFn fn, void* listBox, int index, const char*& out) {
    __try {
        out = fn(listBox, index);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = nullptr;
        return false;
    }
}

bool CallRakRpc(RakClientInterface* client, int* rpcId, BitStream* bitStream, bool& out) {
    __try {
        out = SampApi::callVirtualMethod<bool>(
            reinterpret_cast<std::uintptr_t>(client),
            kRakClientVtblRpcBitStream,
            rpcId,
            bitStream,
            HIGH_PRIORITY,
            RELIABLE_ORDERED,
            0,
            false);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = false;
        return false;
    }
}

bool CallRakSend(
    RakClientInterface* client,
    BitStream* bitStream,
    PacketPriority priority,
    PacketReliability reliability,
    char orderingChannel,
    bool& out) {
    __try {
        out = SampApi::callVirtualMethod<bool>(
            reinterpret_cast<std::uintptr_t>(client),
            kRakClientVtblSendBitStream,
            bitStream,
            priority,
            reliability,
            orderingChannel);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = false;
        return false;
    }
}

bool CallIdFind(IdFindFn fn, void* pool, const void* ped, std::uint16_t& out) {
    __try {
        out = fn(pool, ped);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = kInvalidSampPlayerId;
        return false;
    }
}

bool CallSetPageSize(SetPageSizeFn fn, void* chat, int pageSize) {
    __try {
        fn(chat, pageSize);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallSendInput(SendInputFn fn, void* target, const char* text) {
    __try {
        fn(target, text);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallProcessInput(ProcessInputFn fn, void* input) {
    __try {
        fn(input);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallChatAsiInputWriter(ChatAsiInputWriterFn fn, const char* text, std::size_t length, unsigned char mode) {
    __try {
        fn(text, length, mode);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallChatAsiInputSubmit(ChatAsiInputSubmitFn fn, unsigned int mode) {
    __try {
        fn(mode);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int ClampSampEditboxCursorRange(std::string_view utf8Text, int& start, int& finish) {
    const int maxPosition = static_cast<int>(std::min<std::size_t>(textencoding::Utf8ToGame(utf8Text).size(), 255));
    const auto clampPosition = [maxPosition](int value) {
        return std::clamp(value, 0, maxPosition);
    };

    start = clampPosition(start);
    finish = clampPosition(finish);
    if (finish < start) {
        std::swap(start, finish);
    }
    return maxPosition;
}

int ClampChatAsiCursorRange(std::uintptr_t inputBuffer, int& start, int& finish) {
    if (finish < start) {
        std::swap(start, finish);
    }

    start = std::max(0, start);
    finish = std::max(0, finish);

    std::uint32_t inputLength = 0;
    int maxPosition = std::max(start, finish);
    if (inputBuffer != 0 && SafeRead(inputBuffer + 0x10, inputLength)
        && inputLength <= static_cast<std::uint32_t>(kDefaultTextLimit)) {
        maxPosition = static_cast<int>(inputLength);
    }

    start = std::clamp(start, 0, maxPosition);
    finish = std::clamp(finish, 0, maxPosition);
    if (finish < start) {
        std::swap(start, finish);
    }
    return maxPosition;
}

std::string ChatAsiInputBufferSummary(std::uintptr_t inputBuffer) {
    if (inputBuffer == 0) {
        return "input_buffer=null";
    }

    std::uintptr_t dataPointer = 0;
    std::uint32_t inputLength = 0;
    std::uint32_t inputCapacity = 0;
    const bool dataPointerOk = SafeRead(inputBuffer, dataPointer);
    const bool lengthOk = SafeRead(inputBuffer + 0x10, inputLength);
    const bool capacityOk = SafeRead(inputBuffer + 0x14, inputCapacity);

    char buffer[256]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "input_buffer=0x%08X data_ok=%d data=0x%08X len_ok=%d len=%u cap_ok=%d cap=%u",
        static_cast<unsigned>(inputBuffer),
        dataPointerOk ? 1 : 0,
        static_cast<unsigned>(dataPointer),
        lengthOk ? 1 : 0,
        static_cast<unsigned>(inputLength),
        capacityOk ? 1 : 0,
        static_cast<unsigned>(inputCapacity));
    return buffer;
}

} // namespace

#include "samp_api/core/samp_api_core.inl"
#include "samp_api/dialog/samp_api_dialog.inl"
#include "samp_api/raknet/samp_api_raknet.inl"
#include "samp_api/chat/samp_api_chat.inl"
