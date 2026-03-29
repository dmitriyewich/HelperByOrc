#include "samp_api.h"

#include "debug_log.h"
#include "text_encoding.h"

#include <memory>
#include <memwrapper.h>

#include <common.h>

#include <algorithm>
#include <cctype>
#include <string>
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
using AddChatMessageFn = void(__thiscall*)(void*, unsigned long, const char*);
using SetDialogListItemFn = void(__thiscall*)(void*, int);

constexpr std::uintptr_t kChatEntryBaseOffset = 0x132;
constexpr std::size_t kChatEntrySize = 0xFC;
constexpr int kChatEntryCount = 100;
constexpr std::size_t kDefaultSmallStringLimit = 256;
constexpr std::size_t kDefaultTextLimit = 8192;

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

} // namespace

#include "samp_api/core/samp_api_core.inl"
#include "samp_api/dialog/samp_api_dialog.inl"
#include "samp_api/raknet/samp_api_raknet.inl"
#include "samp_api/chat/samp_api_chat.inl"
