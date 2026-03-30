#include "samp_api.h"

#include "debug_log.h"
#include "text_encoding.h"

#include <memory>
#include <memwrapper.h>

#include <common.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>
#include <thread>
#include <utility>

namespace {

using GetNameFn = const char*(__thiscall*)(void*, unsigned short);
using GetEditboxTextFn = char*(__thiscall*)(void*);
using SetEditboxTextFn = void(__thiscall*)(void*, char*, int);
using SetPageSizeFn = void(__thiscall*)(void*, int);
using IdFindFn = int(__thiscall*)(void*, const void*);
using PlayerPoolIsConnectedFn = bool(__thiscall*)(void*, unsigned short);
using ListBoxGetSelectedIndexFn = int(__thiscall*)(void*, int);
using ListBoxGetItemFn = const char*(__thiscall*)(void*, int);
using SetCursorModeFn = char*(__thiscall*)(void*, int, bool);
using DialogCloseFn = void(__thiscall*)(void*, int);
using InputOpenCloseFn = void(__thiscall*)(void*);
using SendInputFn = void(__thiscall*)(void*, const char*);
using ProcessInputFn = void(__thiscall*)(void*);
using ArizonaProcessInputFn = void(__cdecl*)(std::uint32_t);
using AddChatMessageFn = void(__thiscall*)(void*, unsigned long, const char*);
using SetDialogListItemFn = void(__thiscall*)(void*, int);

constexpr std::uintptr_t kChatEntryBaseOffset = 0x132;
constexpr std::size_t kChatEntrySize = 0xFC;
constexpr int kChatEntryCount = 100;
constexpr std::size_t kDefaultSmallStringLimit = 256;
constexpr std::size_t kDefaultTextLimit = 8192;
constexpr std::size_t kArizonaChatSmallStringCapacity = 15;
constexpr std::size_t kArizonaChatMaxTextLength = 4096;
constexpr std::size_t kArizonaChatMaxCapacity = 65536;

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

constexpr const char* kArizonaNativePriorityNames[] = {
    "sampGetChatInputText",
    "sampGetChatString",
    "sampSetChatInputText",
};

enum class ArizonaChatScanState : int {
    Idle = 0,
    Scanning = 1,
    Ready = 2,
    Failed = 3,
};

struct ModuleImageInfo {
    std::uintptr_t base = 0;
    std::size_t size = 0;
};

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

bool SafeWriteBuffer(std::uintptr_t address, const void* data, std::size_t size) {
    if (address == 0 || (!data && size != 0)) {
        return false;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        if (!SafeWrite(address + i, bytes[i])) {
            return false;
        }
    }
    return true;
}

bool SafeReadBuffer(std::uintptr_t address, void* data, std::size_t size) {
    if (address == 0 || (!data && size != 0)) {
        return false;
    }

    auto* bytes = static_cast<std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        if (!SafeRead(address + i, bytes[i])) {
            return false;
        }
    }
    return true;
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

bool SafeReadStringExact(std::uintptr_t address, std::size_t size, std::string& out) {
    out.clear();
    if (address == 0) {
        return false;
    }
    if (size == 0) {
        return true;
    }

    out.resize(size);
    for (std::size_t i = 0; i < size; ++i) {
        char ch = '\0';
        if (!SafeRead(address + i, ch)) {
            out.clear();
            return false;
        }
        out[i] = ch;
    }
    return true;
}

bool ReadModuleImageInfo(HMODULE module, ModuleImageInfo& info) {
    info = {};
    if (!module) {
        return false;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(module);
    IMAGE_DOS_HEADER dosHeader{};
    if (!SafeRead(base, dosHeader) || dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    IMAGE_NT_HEADERS32 ntHeaders{};
    if (!SafeRead(base + static_cast<std::uintptr_t>(dosHeader.e_lfanew), ntHeaders) || ntHeaders.Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    if (ntHeaders.OptionalHeader.SizeOfImage == 0) {
        return false;
    }

    info.base = base;
    info.size = ntHeaders.OptionalHeader.SizeOfImage;
    return true;
}

std::uintptr_t FindPatternMasked(
    std::uintptr_t base,
    std::size_t size,
    const std::uint8_t* pattern,
    const char* mask,
    std::size_t patternSize) {
    if (base == 0 || size < patternSize || patternSize == 0 || !pattern || !mask) {
        return 0;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(base);
    const std::size_t last = size - patternSize;
    for (std::size_t offset = 0; offset <= last; ++offset) {
        bool matched = true;
        for (std::size_t i = 0; i < patternSize; ++i) {
            if (mask[i] == 'x' && bytes[offset + i] != pattern[i]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return base + offset;
        }
    }

    return 0;
}

bool MatchPatternMasked(std::uintptr_t address, const std::uint8_t* pattern, const char* mask, std::size_t patternSize) {
    if (address == 0 || patternSize == 0 || !pattern || !mask || !IsReadableMemory(address, patternSize)) {
        return false;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(address);
    for (std::size_t i = 0; i < patternSize; ++i) {
        if (mask[i] == 'x' && bytes[i] != pattern[i]) {
            return false;
        }
    }

    return true;
}

std::uintptr_t ResolveRelativeCallTarget(std::uintptr_t instruction) {
    std::int32_t displacement = 0;
    if (instruction == 0 || !SafeRead(instruction + 1, displacement)) {
        return 0;
    }

    const auto next = static_cast<std::intptr_t>(instruction + 5);
    return static_cast<std::uintptr_t>(next + displacement);
}

bool IsAddressInImage(std::uintptr_t address, const ModuleImageInfo& image, std::size_t size = 1) {
    return address >= image.base && size <= image.size && address - image.base <= image.size - size;
}

bool IsValidArizonaChatProcessInputCandidate(
    std::uintptr_t candidate,
    const ModuleImageInfo& image,
    std::uintptr_t stringObject) {
    if (!IsAddressInImage(candidate, image, 51)) {
        return false;
    }

    std::array<std::uint8_t, 51> validation = {
        0x6A, 0x1C, 0xB8, 0x00, 0x00, 0x00, 0x00,
        0xE8, 0x00, 0x00, 0x00, 0x00,
        0x33, 0xDB, 0x39, 0x1D, 0x00, 0x00, 0x00, 0x00,
        0x0F, 0x84, 0x00, 0x00, 0x00, 0x00,
        0xE8, 0x00, 0x00, 0x00, 0x00,
        0xE8, 0x00, 0x00, 0x00, 0x00,
        0xBE, 0x00, 0x00, 0x00, 0x00,
        0x38, 0x5D, 0x08, 0x74, 0x09, 0xE8,
        0x00, 0x00, 0x00, 0x00
    };
    const auto stringObject32 = static_cast<std::uint32_t>(stringObject);
    std::memcpy(validation.data() + 37, &stringObject32, sizeof(stringObject32));

    constexpr char kValidationMask[] = "xxx????x????xxxx????xx????x????x????xxxxxxxxxxx????";
    return MatchPatternMasked(candidate, validation.data(), kValidationMask, validation.size());
}

std::uintptr_t ScanArizonaChatStringObject(std::uintptr_t moduleBase, std::uintptr_t* callbackOut = nullptr) {
    if (callbackOut) {
        *callbackOut = 0;
    }

    ModuleImageInfo image{};
    if (!ReadModuleImageInfo(reinterpret_cast<HMODULE>(moduleBase), image)) {
        return 0;
    }

    constexpr std::array<std::uint8_t, 9> kAnchor = {
        '#', '#', '#', 'i', 'n', 'p', 'u', 't', '\0'
    };
    constexpr char kAnchorMask[] = "xxxxxxxxx";
    const std::uintptr_t anchor = FindPatternMasked(image.base, image.size, kAnchor.data(), kAnchorMask, kAnchor.size());
    if (anchor == 0) {
        return 0;
    }

    std::array<std::uint8_t, 27> signature = {
        0x6A, 0x00, 0x68, 0x00, 0x00, 0x00, 0x00,
        0x68, 0x80, 0x03, 0x08, 0x00,
        0x68, 0x00, 0x00, 0x00, 0x00,
        0x68, 0x00, 0x00, 0x00, 0x00,
        0xE8, 0x00, 0x00, 0x00, 0x00
    };
    const auto anchor32 = static_cast<std::uint32_t>(anchor);
    std::memcpy(signature.data() + 18, &anchor32, sizeof(anchor32));

    constexpr char kSignatureMask[] = "xxx????xxxxxx????xxxxxx????";
    const std::uintptr_t callsite =
        FindPatternMasked(image.base, image.size, signature.data(), kSignatureMask, signature.size());
    if (callsite == 0) {
        return 0;
    }

    std::uint32_t stringObject = 0;
    if (!SafeRead(callsite + 13, stringObject) || stringObject == 0) {
        return 0;
    }

    if (callbackOut) {
        std::uint32_t callbackFunction = 0;
        if (SafeRead(callsite + 3, callbackFunction)) {
            *callbackOut = callbackFunction;
        }
    }

    std::uint32_t length = 0;
    std::uint32_t capacity = 0;
    if (!SafeRead(stringObject + 0x10, length) || !SafeRead(stringObject + 0x14, capacity)) {
        return 0;
    }
    if (length > kArizonaChatMaxTextLength || capacity > kArizonaChatMaxCapacity || length > capacity) {
        return 0;
    }

    return stringObject;
}

std::uintptr_t ScanArizonaChatDirtyFlag(std::uintptr_t callbackFunction, std::uintptr_t stringObject) {
    if (callbackFunction == 0 || stringObject == 0) {
        return 0;
    }

    constexpr std::size_t kScanWindow = 128;
    std::array<std::uint8_t, kScanWindow> code{};
    if (!SafeReadBuffer(callbackFunction, code.data(), code.size())) {
        return 0;
    }

    const auto read32 = [&](std::size_t offset) -> std::uint32_t {
        std::uint32_t value = 0;
        std::memcpy(&value, code.data() + offset, sizeof(value));
        return value;
    };

    for (std::size_t i = 0; i + 7 < code.size(); ++i) {
        if (code[i] != 0x80 || code[i + 1] != 0x3D || code[i + 6] != 0x00) {
            continue;
        }

        const std::uint32_t dirtyFlag = read32(i + 2);
        if (dirtyFlag == 0) {
            continue;
        }

        for (std::size_t j = i + 7; j + 18 < code.size() && j < i + 48; ++j) {
            if (code[j] != 0x68 || read32(j + 1) != static_cast<std::uint32_t>(stringObject)) {
                continue;
            }
            if (code[j + 5] != 0x56 || code[j + 6] != 0xE8) {
                continue;
            }

            for (std::size_t k = j + 11; k + 6 < code.size() && k < j + 32; ++k) {
                if (code[k] != 0xC6 || code[k + 1] != 0x05 || code[k + 6] != 0x00) {
                    continue;
                }
                if (read32(k + 2) == dirtyFlag) {
                    return dirtyFlag;
                }
            }
        }
    }

    return 0;
}

std::uintptr_t ScanArizonaChatProcessInput(std::uintptr_t moduleBase, std::uintptr_t stringObject) {
    if (stringObject == 0) {
        return 0;
    }

    ModuleImageInfo image{};
    if (!ReadModuleImageInfo(reinterpret_cast<HMODULE>(moduleBase), image)) {
        return 0;
    }

    constexpr std::array<std::uint8_t, 40> kEnterCallsite = {
        0x83, 0x7D, 0x10, 0x0D, 0x75, 0x00,
        0xA1, 0x00, 0x00, 0x00, 0x00,
        0x83, 0xB8, 0xE0, 0x14, 0x00, 0x00, 0x00,
        0x74, 0x00,
        0x81, 0xFE, 0x01, 0x01, 0x00, 0x00, 0x75, 0x08, 0x6A, 0x01,
        0xE8, 0x00, 0x00, 0x00, 0x00,
        0x59, 0x32, 0xC0, 0xEB, 0x00
    };
    constexpr char kEnterCallsiteMask[] = "xxxxx?x????xxxxxxxx?xxxxxxxxxxx????xxxx?";
    const auto enterCallsite =
        FindPatternMasked(image.base, image.size, kEnterCallsite.data(), kEnterCallsiteMask, kEnterCallsite.size());
    if (enterCallsite != 0) {
        const auto candidate = ResolveRelativeCallTarget(enterCallsite + 30);
        if (IsValidArizonaChatProcessInputCandidate(candidate, image, stringObject)) {
            return candidate;
        }
    }

    const auto fallbackCandidate = moduleBase + 0x000176D2;
    return IsValidArizonaChatProcessInputCandidate(fallbackCandidate, image, stringObject) ? fallbackCandidate : 0;
}

bool ReadArizonaChatStdString(std::uintptr_t stringObject, std::string& out) {
    out.clear();
    if (stringObject == 0) {
        return false;
    }

    std::uint32_t length = 0;
    std::uint32_t capacity = 0;
    if (!SafeRead(stringObject + 0x10, length) || !SafeRead(stringObject + 0x14, capacity)) {
        return false;
    }
    if (length > kArizonaChatMaxTextLength || capacity > kArizonaChatMaxCapacity || length > capacity) {
        return false;
    }

    std::uintptr_t dataAddress = stringObject;
    if (capacity > kArizonaChatSmallStringCapacity) {
        std::uint32_t heapPtr = 0;
        if (!SafeRead(stringObject, heapPtr) || heapPtr == 0) {
            return false;
        }
        dataAddress = heapPtr;
    }

    return SafeReadStringExact(dataAddress, length, out);
}

bool WriteArizonaChatStdString(std::uintptr_t stringObject, std::string_view text) {
    if (stringObject == 0) {
        return false;
    }

    std::uint32_t capacity = 0;
    if (!SafeRead(stringObject + 0x14, capacity)) {
        return false;
    }
    if (capacity > kArizonaChatMaxCapacity) {
        return false;
    }
    if (text.size() > capacity) {
        return false;
    }

    std::uintptr_t dataAddress = stringObject;
    if (capacity > kArizonaChatSmallStringCapacity) {
        std::uint32_t heapPtr = 0;
        if (!SafeRead(stringObject, heapPtr) || heapPtr == 0) {
            return false;
        }
        dataAddress = heapPtr;
    }

    if (!SafeWriteBuffer(dataAddress, text.data(), text.size())) {
        return false;
    }

    const char terminator = '\0';
    if (!SafeWrite(dataAddress + text.size(), terminator)) {
        return false;
    }

    const auto length = static_cast<std::uint32_t>(text.size());
    return SafeWrite(stringObject + 0x10, length);
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
    if (normalized == SampApi::BACKEND_ARIZONA) {
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

bool CallPlayerPoolIsConnected(PlayerPoolIsConnectedFn fn, void* pool, unsigned short id, bool& out) {
    __try {
        out = fn(pool, id);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = false;
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
        out = client->RPC(rpcId, bitStream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
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
        out = client->Send(bitStream, priority, reliability, orderingChannel);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = false;
        return false;
    }
}

bool CallIdFind(IdFindFn fn, void* pool, const void* ped, int& out) {
    __try {
        out = fn(pool, ped);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out = 65535;
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

bool CallAddChatMessage(AddChatMessageFn fn, void* chat, unsigned long color, const char* text) {
    __try {
        fn(chat, color, text);
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

bool CallArizonaProcessInput(ArizonaProcessInputFn fn, std::uint32_t mode) {
    __try {
        fn(mode);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

void SampApi::RefreshArizonaChatModule() {
    const auto currentModule = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("_chat.asi"));
    const auto previousModule = arizonaChatModuleBase_.load();
    if (currentModule != previousModule) {
        arizonaChatModuleBase_.store(currentModule);
        arizonaChatStringObject_.store(0);
        arizonaChatDirtyFlag_.store(0);
        arizonaChatProcessInput_.store(0);
        arizonaChatScanTicket_.fetch_add(1);
        arizonaChatScanState_.store(static_cast<int>(ArizonaChatScanState::Idle));

        if (currentModule != 0) {
            debuglog::Write("_chat.asi detected at 0x%p", reinterpret_cast<void*>(currentModule));
        }
    }

    if (currentModule != 0 && arizonaChatScanState_.load() == static_cast<int>(ArizonaChatScanState::Idle)) {
        StartArizonaChatScan(currentModule);
    }
}

void SampApi::StartArizonaChatScan(std::uintptr_t moduleBase) {
    int expected = static_cast<int>(ArizonaChatScanState::Idle);
    if (!arizonaChatScanState_.compare_exchange_strong(expected, static_cast<int>(ArizonaChatScanState::Scanning))) {
        return;
    }

    const auto ticket = arizonaChatScanTicket_.load();
    std::thread([this, moduleBase, ticket]() {
        std::uintptr_t callbackFunction = 0;
        const auto stringObject = ScanArizonaChatStringObject(moduleBase, &callbackFunction);
        std::uintptr_t dirtyFlag = 0;
        std::uintptr_t processInput = 0;
        if (stringObject != 0 && callbackFunction != 0) {
            dirtyFlag = ScanArizonaChatDirtyFlag(callbackFunction, stringObject);
        }
        if (stringObject != 0) {
            processInput = ScanArizonaChatProcessInput(moduleBase, stringObject);
        }
        if (arizonaChatScanTicket_.load() != ticket || arizonaChatModuleBase_.load() != moduleBase) {
            return;
        }

        arizonaChatStringObject_.store(stringObject);
        arizonaChatDirtyFlag_.store(dirtyFlag);
        arizonaChatProcessInput_.store(processInput);
        arizonaChatScanState_.store(static_cast<int>(
            stringObject != 0 ? ArizonaChatScanState::Ready : ArizonaChatScanState::Failed));

        if (stringObject != 0) {
            debuglog::Write("_chat.asi input buffer located at 0x%p", reinterpret_cast<void*>(stringObject));
            if (dirtyFlag != 0) {
                debuglog::Write("_chat.asi input dirty flag located at 0x%p", reinterpret_cast<void*>(dirtyFlag));
            } else {
                debuglog::Write("_chat.asi input dirty flag was not found");
            }
            if (processInput != 0) {
                debuglog::Write("_chat.asi process input routine located at 0x%p", reinterpret_cast<void*>(processInput));
            } else {
                debuglog::Write("_chat.asi process input routine was not found");
            }
        } else {
            debuglog::Write("_chat.asi input buffer signature was not found");
        }
    }).detach();
}

bool SampApi::ReadArizonaChatInputText(std::string& outText) const {
    outText.clear();
    if (arizonaChatModuleBase_.load() == 0 || arizonaChatScanState_.load() != static_cast<int>(ArizonaChatScanState::Ready)) {
        return false;
    }

    return ReadArizonaChatStdString(arizonaChatStringObject_.load(), outText);
}

bool SampApi::WriteArizonaChatInputText(std::string_view gameText) {
    RefreshArizonaChatModule();
    if (arizonaChatModuleBase_.load() == 0 || arizonaChatScanState_.load() != static_cast<int>(ArizonaChatScanState::Ready)) {
        return false;
    }

    if (!WriteArizonaChatStdString(arizonaChatStringObject_.load(), gameText)) {
        return false;
    }

    SetArizonaChatInputDirty(true);
    return true;
}

bool SampApi::SetArizonaChatInputDirty(bool dirty) {
    const auto dirtyFlag = arizonaChatDirtyFlag_.load();
    if (dirtyFlag == 0) {
        return false;
    }

    const std::uint8_t value = dirty ? 1 : 0;
    return SafeWrite(dirtyFlag, value);
}

#include "samp_api/core/samp_api_core.inl"
#include "samp_api/dialog/samp_api_dialog.inl"
#include "samp_api/raknet/samp_api_raknet.inl"
#include "samp_api/chat/samp_api_chat.inl"
