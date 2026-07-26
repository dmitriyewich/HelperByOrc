#include "samp_hooks.h"

#include "debug_log.h"
#include "minhook_utils.h"
#include "module_signature_scanner.h"
#include "samp_api.h"
#include "samp_api/chat/samp_local_chat_entry.h"
#include "text_encoding.h"

#include <CPad.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <string>
#include <string_view>

namespace {

constexpr std::uintptr_t kDamageManagerApplyDamageAddress = 0x6C24B0;
constexpr std::uintptr_t kPadUpdateMouseAddress = 0x53F3C0;
constexpr std::string_view kChatAsiAddEntryFrameWrapperSignature =
    "55 8B EC FF 75 18 FF 75 14 FF 75 10 FF 75 0C FF 75 08 E8 ?? ?? ?? ?? 83 C4 14 5D C2 14 00";
constexpr std::string_view kChatAsiAddEntryStackWrapperSignature =
    "FF 74 24 14 FF 74 24 14 FF 74 24 14 FF 74 24 14 FF 74 24 14 E8 ?? ?? ?? ?? 83 C4 14 C2 14 00";
constexpr std::size_t kMaxChatAsiTransferDepth = 4;
constexpr std::size_t kMaxChatAsiSignatureCandidates = 8;

int CaptureNativeCallFailure(
    EXCEPTION_POINTERS* exception,
    SampHooks::NativeCallFailure* failure) noexcept {
    if (failure && exception && exception->ExceptionRecord) {
        failure->code = exception->ExceptionRecord->ExceptionCode;
        failure->address = exception->ExceptionRecord->ExceptionAddress;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

struct ChatAsiWrapperLayout {
    const char* name = "none";
    std::size_t size = 0;
    std::size_t callOffset = 0;
    std::size_t dispatchReturnOffset = 0;
};

constexpr ChatAsiWrapperLayout kChatAsiFrameWrapperLayout{
    "frame",
    30,
    18,
    23,
};
constexpr ChatAsiWrapperLayout kChatAsiStackWrapperLayout{
    "stack",
    31,
    20,
    25,
};

struct ChatAsiAddEntryDiscovery {
    HMODULE module = nullptr;
    std::uintptr_t wrapper = 0;
    std::uintptr_t dispatch = 0;
    std::uintptr_t observedTransferTarget = 0;
    std::size_t transferDepth = 0;
    std::size_t signatureCandidates = 0;
    const char* method = "none";
    const char* wrapperVariant = "none";

    explicit operator bool() const {
        return module != nullptr && wrapper != 0 && dispatch != 0;
    }
};

bool IsExecutableRange(std::uintptr_t address, std::size_t size, HMODULE expectedModule = nullptr) {
    if (address == 0 || size == 0) {
        return false;
    }

    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) != sizeof(region)
        || region.State != MEM_COMMIT
        || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
        || address + size < address
        || address + size > reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize
        || (expectedModule && region.AllocationBase != expectedModule)) {
        return false;
    }

    const DWORD protection = region.Protect & 0xFFu;
    return protection == PAGE_EXECUTE
        || protection == PAGE_EXECUTE_READ
        || protection == PAGE_EXECUTE_READWRITE
        || protection == PAGE_EXECUTE_WRITECOPY;
}

HMODULE FindChatAsiModule() {
    HMODULE chatModule = GetModuleHandleA("_chat.asi");
    if (!chatModule) {
        chatModule = GetModuleHandleA("chat.asi");
    }
    return chatModule;
}

bool MatchesChatAsiFrameWrapper(std::uintptr_t wrapper, HMODULE chatModule) {
    if (!IsExecutableRange(wrapper, kChatAsiFrameWrapperLayout.size, chatModule)) {
        return false;
    }

    constexpr std::uint8_t kWrapperPrefix[] = {
        0x55, 0x8B, 0xEC,
        0xFF, 0x75, 0x18,
        0xFF, 0x75, 0x14,
        0xFF, 0x75, 0x10,
        0xFF, 0x75, 0x0C,
        0xFF, 0x75, 0x08,
        0xE8,
    };
    constexpr std::uint8_t kWrapperSuffix[] = {0x83, 0xC4, 0x14, 0x5D, 0xC2, 0x14, 0x00};
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(wrapper);
    return std::memcmp(bytes, kWrapperPrefix, sizeof(kWrapperPrefix)) == 0
        && std::memcmp(bytes + 23, kWrapperSuffix, sizeof(kWrapperSuffix)) == 0;
}

bool MatchesChatAsiStackWrapper(std::uintptr_t wrapper, HMODULE chatModule) {
    if (!IsExecutableRange(wrapper, kChatAsiStackWrapperLayout.size, chatModule)) {
        return false;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(wrapper);
    constexpr std::uint8_t kPushArgument[] = {0xFF, 0x74, 0x24, 0x14};
    for (std::size_t index = 0; index < 5; ++index) {
        if (std::memcmp(bytes + index * sizeof(kPushArgument), kPushArgument, sizeof(kPushArgument)) != 0) {
            return false;
        }
    }

    constexpr std::uint8_t kSuffix[] = {0x83, 0xC4, 0x14, 0xC2, 0x14, 0x00};
    return bytes[kChatAsiStackWrapperLayout.callOffset] == 0xE8
        && std::memcmp(
            bytes + kChatAsiStackWrapperLayout.dispatchReturnOffset,
            kSuffix,
            sizeof(kSuffix)) == 0;
}

bool ResolveChatAsiAddEntryDispatch(
    std::uintptr_t wrapper,
    HMODULE chatModule,
    ChatAsiWrapperLayout& layout,
    std::uintptr_t& dispatch) {
    layout = {};
    dispatch = 0;
    if (MatchesChatAsiFrameWrapper(wrapper, chatModule)) {
        layout = kChatAsiFrameWrapperLayout;
    } else if (MatchesChatAsiStackWrapper(wrapper, chatModule)) {
        layout = kChatAsiStackWrapperLayout;
    } else {
        return false;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(wrapper);
    std::int32_t displacement = 0;
    std::memcpy(&displacement, bytes + layout.callOffset + 1, sizeof(displacement));
    dispatch = wrapper + layout.dispatchReturnOffset + displacement;
    if (!IsExecutableRange(dispatch, 1, chatModule) || dispatch == wrapper) {
        dispatch = 0;
        return false;
    }

    const std::uint8_t firstOpcode = *reinterpret_cast<const std::uint8_t*>(dispatch);
    if (firstOpcode == 0x00 || firstOpcode == 0xC2 || firstOpcode == 0xC3 || firstOpcode == 0xCC) {
        dispatch = 0;
        return false;
    }
    return true;
}

bool IsWritableRange(std::uintptr_t address, std::size_t size) {
    if (address == 0 || size == 0 || address + size < address) {
        return false;
    }

    const std::uintptr_t finish = address + size;
    while (address < finish) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) != sizeof(region)
            || region.State != MEM_COMMIT
            || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return false;
        }

        const DWORD protection = region.Protect & 0xFF;
        if (protection != PAGE_READWRITE
            && protection != PAGE_WRITECOPY
            && protection != PAGE_EXECUTE_READWRITE
            && protection != PAGE_EXECUTE_WRITECOPY) {
            return false;
        }

        const std::uintptr_t regionEnd =
            reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (regionEnd <= address) {
            return false;
        }
        address = regionEnd;
    }
    return true;
}

ChatAsiAddEntryDiscovery FindChatAsiAddEntryDispatch(std::uintptr_t addEntryTarget) {
    ChatAsiAddEntryDiscovery result;
    result.module = FindChatAsiModule();
    if (!result.module) {
        return result;
    }

    std::uintptr_t transfer = addEntryTarget;
    for (std::size_t depth = 1; depth <= kMaxChatAsiTransferDepth; ++depth) {
        if (!IsExecutableRange(transfer, 5)) {
            break;
        }

        const auto* transferBytes = reinterpret_cast<const std::uint8_t*>(transfer);
        if (transferBytes[0] != 0xE9) {
            break;
        }

        std::int32_t displacement = 0;
        std::memcpy(&displacement, transferBytes + 1, sizeof(displacement));
        const std::uintptr_t target = transfer + 5 + displacement;
        if (target == transfer) {
            break;
        }

        result.observedTransferTarget = target;
        result.transferDepth = depth;

        ChatAsiWrapperLayout layout;
        std::uintptr_t dispatch = 0;
        if (ResolveChatAsiAddEntryDispatch(target, result.module, layout, dispatch)) {
            result.wrapper = target;
            result.dispatch = dispatch;
            result.method = depth == 1 ? "AddEntry transfer" : "AddEntry transfer chain";
            result.wrapperVariant = layout.name;
            return result;
        }

        transfer = target;
    }

    constexpr std::array<std::string_view, 2> signatures{
        kChatAsiAddEntryFrameWrapperSignature,
        kChatAsiAddEntryStackWrapperSignature,
    };
    for (const std::string_view signature : signatures) {
        const auto candidates = module_signature_scanner::FindPatterns(
            result.module,
            signature,
            kMaxChatAsiSignatureCandidates,
            module_signature_scanner::ScanRegion::Executable);
        result.signatureCandidates += candidates.size();
        for (const std::uintptr_t wrapper : candidates) {
            ChatAsiWrapperLayout layout;
            std::uintptr_t dispatch = 0;
            if (!ResolveChatAsiAddEntryDispatch(wrapper, result.module, layout, dispatch)) {
                continue;
            }

            result.wrapper = wrapper;
            result.dispatch = dispatch;
            result.method = "module signature";
            result.wrapperVariant = layout.name;
            return result;
        }
    }
    return result;
}

std::string FormatCodeBytes(std::uintptr_t address, std::size_t count, HMODULE expectedModule) {
    if (!IsExecutableRange(address, count, expectedModule)) {
        return "unavailable";
    }

    std::string result;
    result.reserve(count * 3);
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(address);
    char byteText[4]{};
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0) {
            result.push_back(' ');
        }
        std::snprintf(byteText, sizeof(byteText), "%02X", bytes[index]);
        result.append(byteText);
    }
    return result;
}

struct OutgoingInputTransformScope {
    OutgoingInputTransformScope() {
        SampHooks::PushOutgoingInputTransform();
    }

    ~OutgoingInputTransformScope() {
        SampHooks::PopOutgoingInputTransform();
    }
};

bool HasMouseButtonMask(std::uint8_t mask, SampHooks::MouseButtonMask bit) {
    return (mask & static_cast<std::uint8_t>(bit)) != 0;
}

void ClearMouseControllerButtons(CMouseControllerState& state, std::uint8_t mask) {
    if (HasMouseButtonMask(mask, SampHooks::MouseButtonLeft)) {
        state.lmb = 0;
    }
    if (HasMouseButtonMask(mask, SampHooks::MouseButtonRight)) {
        state.rmb = 0;
    }
    if (HasMouseButtonMask(mask, SampHooks::MouseButtonMiddle)) {
        state.mmb = 0;
    }
}

void ClearControllerButtons(CControllerState& state, std::uint8_t mask) {
    if (HasMouseButtonMask(mask, SampHooks::MouseButtonLeft)) {
        state.ButtonCircle = 0;
    }
    if (HasMouseButtonMask(mask, SampHooks::MouseButtonRight)) {
        state.RightShoulder1 = 0;
    }
}

void ClearPadMouseButtons(CPad* pad, std::uint8_t mask) {
    if (mask == 0) {
        return;
    }

    ClearMouseControllerButtons(CPad::PCTempMouseControllerState, mask);
    ClearMouseControllerButtons(CPad::NewMouseControllerState, mask);
    ClearMouseControllerButtons(CPad::OldMouseControllerState, mask);

    const auto clearPad = [mask](CPad* target) {
        if (!target) {
            return;
        }
        ClearControllerButtons(target->NewState, mask);
        ClearControllerButtons(target->OldState, mask);
        ClearControllerButtons(target->PCTempMouseState, mask);
    };

    clearPad(pad);
    CPad* playerPad = CPad::GetPad(0);
    if (playerPad != pad) {
        clearPad(playerPad);
    }
}

} // namespace

bool SampHooks::CallChatAddEntryOriginal(
    ChatAddEntryFn original,
    void* chat,
    int type,
    const char* text,
    std::size_t textLength,
    const char* prefix,
    unsigned long textColor,
    unsigned long prefixColor) {
    bool succeeded = false;
    ++cchatForwardDepth_;
    __try {
        __try {
            original(chat, type, text, prefix, textColor, prefixColor);
            succeeded = true;
        }
        __except (CaptureChatAddEntryException(GetExceptionInformation())) {
        }
    }
    __finally {
        --cchatForwardDepth_;
    }
    if (succeeded
        && type == samp_local_chat::kLocalMessageType
        && textLength > samp_local_chat::kNativeEntryTextBytes) {
        succeeded = ExtendLatestChatEntryText(
            chat,
            text,
            textLength,
            chatAddEntryFailureSink_);
    }
    return succeeded;
}

int SampHooks::CaptureChatAddEntryException(EXCEPTION_POINTERS* exception) noexcept {
    if (!chatAddEntryFailureSink_) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return CaptureNativeCallFailure(exception, chatAddEntryFailureSink_);
}

bool SampHooks::ExtendLatestChatEntryText(
    void* chat,
    const char* text,
    std::size_t textLength,
    NativeCallFailure* failure) noexcept {
    if (!chat
        || !text
        || textLength <= samp_local_chat::kNativeEntryTextBytes
        || textLength > samp_local_chat::kMaxEntryTextBytes) {
        return textLength <= samp_local_chat::kNativeEntryTextBytes;
    }

    const std::uintptr_t destination =
        reinterpret_cast<std::uintptr_t>(chat)
        + samp_local_chat::kLatestEntryTextOffset;
    const std::size_t bytesToWrite = textLength + 1;
    if (!IsWritableRange(destination, bytesToWrite)) {
        if (failure) {
            failure->code = ERROR_WRITE_FAULT;
            failure->address = reinterpret_cast<const void*>(destination);
        }
        return false;
    }

    NativeCallFailure localFailure{};
    __try {
        std::memcpy(reinterpret_cast<void*>(destination), text, textLength);
        *reinterpret_cast<char*>(destination + textLength) = '\0';
        return true;
    }
    __except (CaptureNativeCallFailure(
        GetExceptionInformation(),
        failure ? failure : &localFailure)) {
        return false;
    }
}

void SampHooks::CallChatAddMessageOriginal(
    ChatAddMessageFn original,
    void* chat,
    unsigned long color,
    const char* text) {
    ++cchatForwardDepth_;
    __try {
        original(chat, color, text);
    }
    __finally {
        --cchatForwardDepth_;
    }
}

void SampHooks::CallChatAddChatMessageOriginal(
    ChatAddChatMessageFn original,
    void* chat,
    const char* prefix,
    unsigned long prefixColor,
    const char* text) {
    ++cchatForwardDepth_;
    __try {
        original(chat, prefix, prefixColor, text);
    }
    __finally {
        --cchatForwardDepth_;
    }
}

void __fastcall SampHooks::ChatAddEntryDetour(void* chat, void* edx, int type, const char* text, const char* prefix, unsigned long textColor, unsigned long prefixColor) {
    UNREFERENCED_PARAMETER(edx);

    const void* returnAddress = _ReturnAddress();
    const char* forwardedText = text;
    std::string sourceTextUtf8;
    std::string textUtf8;
    std::string textGame;

    if (self_ && self_->installed_) {
        sourceTextUtf8 = textencoding::GameToUtf8(text ? text : "");
        textUtf8 = sourceTextUtf8;
        const std::string prefixUtf8 = textencoding::GameToUtf8(prefix ? prefix : "");
        if (cchatForwardDepth_ == 0) {
            const SampCallContext context = ResolveSampCallContext(
                returnAddress,
                self_->sampApi_ ? self_->sampApi_->sampModule() : nullptr,
                self_->ownerModule_);
            for (const auto& transform : self_->chatMessageTransforms_) {
                if (!transform(ChatMessageSource::AddEntry, type, textUtf8, prefixUtf8, textColor, prefixColor, context)) {
                    return;
                }
            }
            for (const auto& filter : self_->chatMessageFilters_) {
                if (!filter(ChatMessageSource::AddEntry, type, textUtf8, prefixUtf8, textColor, prefixColor)) {
                    return;
                }
            }
        }

        for (const auto& handler : self_->onChatMessageHandlers_) {
            handler(type, textUtf8, prefixUtf8, textColor, prefixColor);
        }
        if (textUtf8 != sourceTextUtf8) {
            textGame = textencoding::Utf8ToGame(textUtf8);
            forwardedText = textGame.c_str();
        }
    }

    if (!self_ || !self_->chatAddEntryOriginal_) {
        return;
    }

    const std::string_view gameText(forwardedText ? forwardedText : "");
    std::string truncatedText;
    std::size_t forwardedLength = gameText.size();
    if (type == samp_local_chat::kLocalMessageType) {
        forwardedLength = samp_local_chat::SafeTruncationLength(gameText);
        if (forwardedLength < gameText.size()) {
            truncatedText.assign(gameText.data(), forwardedLength);
            forwardedText = truncatedText.c_str();
            debuglog::WriteInfo(
                "SampHooks::ChatAddEntryDetour truncated bytes=%llu to=%llu capacity=%llu",
                static_cast<unsigned long long>(gameText.size()),
                static_cast<unsigned long long>(forwardedLength),
                static_cast<unsigned long long>(samp_local_chat::kMaxEntryTextBytes));
        } else if (forwardedLength > samp_local_chat::kNativeEntryTextBytes) {
            debuglog::WriteInfo(
                "SampHooks::ChatAddEntryDetour extended bytes=%llu capacity=%llu",
                static_cast<unsigned long long>(forwardedLength),
                static_cast<unsigned long long>(samp_local_chat::kMaxEntryTextBytes));
        }
    }

    if (!CallChatAddEntryOriginal(
            self_->chatAddEntryOriginal_,
            chat,
            type,
            forwardedText,
            forwardedLength,
            prefix,
            textColor,
            prefixColor)) {
        debuglog::WriteError(
            "SampHooks::ChatAddEntryDetour forward failed type=%d bytes=%llu",
            type,
            static_cast<unsigned long long>(forwardedLength));
    }
}

void __cdecl SampHooks::ChatAsiAddEntryDispatchDetour(
    int type,
    const char* text,
    const char* prefix,
    unsigned long textColor,
    unsigned long prefixColor) {
    const void* returnAddress = _ReturnAddress();
    const char* forwardedText = text;
    std::string sourceTextUtf8;
    std::string textUtf8;
    std::string textGame;

    if (self_ && self_->installed_ && cchatForwardDepth_ == 0
        && (!self_->chatMessageTransforms_.empty()
            || self_->chatAsiCompatibilityEnabled_.load(std::memory_order_relaxed))) {
        sourceTextUtf8 = textencoding::GameToUtf8(text ? text : "");
        textUtf8 = sourceTextUtf8;
        const std::string prefixUtf8 = textencoding::GameToUtf8(prefix ? prefix : "");
        const SampCallContext context = ResolveSampCallContext(
            returnAddress,
            self_->sampApi_ ? self_->sampApi_->sampModule() : nullptr,
            self_->ownerModule_);
        for (const auto& transform : self_->chatMessageTransforms_) {
            if (!transform(ChatMessageSource::AddEntry, type, textUtf8, prefixUtf8, textColor, prefixColor, context)) {
                return;
            }
        }
        if (self_->chatAsiCompatibilityEnabled_.load(std::memory_order_relaxed)) {
            for (const auto& filter : self_->chatMessageFilters_) {
                if (!filter(ChatMessageSource::AddEntry, type, textUtf8, prefixUtf8, textColor, prefixColor)) {
                    return;
                }
            }
        }
        for (const auto& handler : self_->onChatMessageHandlers_) {
            handler(type, textUtf8, prefixUtf8, textColor, prefixColor);
        }
        if (textUtf8 != sourceTextUtf8) {
            textGame = textencoding::Utf8ToGame(textUtf8);
            forwardedText = textGame.c_str();
        }
    }

    if (self_ && self_->chatAsiAddEntryDispatchOriginal_) {
        self_->chatAsiAddEntryDispatchOriginal_(type, forwardedText, prefix, textColor, prefixColor);
    }
}

void __fastcall SampHooks::ChatAddMessageDetour(void* chat, void* edx, unsigned long color, const char* text) {
    UNREFERENCED_PARAMETER(edx);

    const void* returnAddress = _ReturnAddress();
    const char* forwardedText = text;
    std::string sourceTextUtf8;
    std::string textUtf8;
    std::string textGame;

    if (self_ && self_->installed_) {
        sourceTextUtf8 = textencoding::GameToUtf8(text ? text : "");
        textUtf8 = sourceTextUtf8;
        if (cchatForwardDepth_ == 0) {
            const SampCallContext context = ResolveSampCallContext(
                returnAddress,
                self_->sampApi_ ? self_->sampApi_->sampModule() : nullptr,
                self_->ownerModule_);
            for (const auto& transform : self_->chatMessageTransforms_) {
                if (!transform(ChatMessageSource::AddMessage, 4, textUtf8, std::string(), color, 0, context)) {
                    return;
                }
            }
            for (const auto& filter : self_->chatMessageFilters_) {
                if (!filter(ChatMessageSource::AddMessage, 4, textUtf8, std::string(), color, 0)) {
                    return;
                }
            }
        }
        if (textUtf8 != sourceTextUtf8) {
            textGame = textencoding::Utf8ToGame(textUtf8);
            forwardedText = textGame.c_str();
        }
    }

    if (self_ && self_->chatAddMessageOriginal_) {
        CallChatAddMessageOriginal(self_->chatAddMessageOriginal_, chat, color, forwardedText);
    }
}

void __fastcall SampHooks::ChatAddChatMessageDetour(void* chat, void* edx, const char* prefix, unsigned long prefixColor, const char* text) {
    UNREFERENCED_PARAMETER(edx);

    const void* returnAddress = _ReturnAddress();
    const char* forwardedText = text;
    std::string sourceTextUtf8;
    std::string textUtf8;
    std::string textGame;

    if (self_ && self_->installed_) {
        sourceTextUtf8 = textencoding::GameToUtf8(text ? text : "");
        textUtf8 = sourceTextUtf8;
        const std::string prefixUtf8 = textencoding::GameToUtf8(prefix ? prefix : "");
        if (cchatForwardDepth_ == 0) {
            const SampCallContext context = ResolveSampCallContext(
                returnAddress,
                self_->sampApi_ ? self_->sampApi_->sampModule() : nullptr,
                self_->ownerModule_);
            for (const auto& transform : self_->chatMessageTransforms_) {
                if (!transform(ChatMessageSource::AddChatMessage, 2, textUtf8, prefixUtf8, 0, prefixColor, context)) {
                    return;
                }
            }
            for (const auto& filter : self_->chatMessageFilters_) {
                if (!filter(ChatMessageSource::AddChatMessage, 2, textUtf8, prefixUtf8, 0, prefixColor)) {
                    return;
                }
            }
        }
        if (textUtf8 != sourceTextUtf8) {
            textGame = textencoding::Utf8ToGame(textUtf8);
            forwardedText = textGame.c_str();
        }
    }

    if (self_ && self_->chatAddChatMessageOriginal_) {
        CallChatAddChatMessageOriginal(
            self_->chatAddChatMessageOriginal_,
            chat,
            prefix,
            prefixColor,
            forwardedText);
    }
}

void __fastcall SampHooks::InputSendDetour(std::uintptr_t self, void* edx, const char* text) {
    UNREFERENCED_PARAMETER(edx);

    const void* returnAddress = _ReturnAddress();
    const char* forwardedText = text;
    std::string textUtf8;
    std::string textGame;

    if (SampHooks::self_ && SampHooks::self_->installed_) {
        const SampCallContext context = ResolveSampCallContext(
            returnAddress,
            SampHooks::self_->sampApi_ ? SampHooks::self_->sampApi_->sampModule() : nullptr,
            SampHooks::self_->ownerModule_);
        textUtf8 = textencoding::GameToUtf8(text ? text : "");
        for (const auto& handler : SampHooks::self_->onSendCommandHandlers_) {
            if (!handler(textUtf8, context)) {
                return;
            }
        }
        textGame = textencoding::Utf8ToGame(textUtf8);
        forwardedText = textGame.c_str();
    }

    if (SampHooks::self_ && SampHooks::self_->inputSendOriginal_) {
        const OutgoingInputTransformScope scope;
        SampHooks::self_->inputSendOriginal_(self, forwardedText);
    }
}

void __fastcall SampHooks::InputSendSayDetour(std::uintptr_t self, void* edx, const char* text) {
    UNREFERENCED_PARAMETER(edx);

    const void* returnAddress = _ReturnAddress();
    const char* forwardedText = text;
    std::string textUtf8;
    std::string textGame;

    if (SampHooks::self_ && SampHooks::self_->installed_) {
        const SampCallContext context = ResolveSampCallContext(
            returnAddress,
            SampHooks::self_->sampApi_ ? SampHooks::self_->sampApi_->sampModule() : nullptr,
            SampHooks::self_->ownerModule_);
        textUtf8 = textencoding::GameToUtf8(text ? text : "");
        for (const auto& handler : SampHooks::self_->onSendChatHandlers_) {
            if (!handler(textUtf8, context)) {
                return;
            }
        }
        textGame = textencoding::Utf8ToGame(textUtf8);
        forwardedText = textGame.c_str();
    }

    if (SampHooks::self_ && SampHooks::self_->inputSendSayOriginal_) {
        const OutgoingInputTransformScope scope;
        SampHooks::self_->inputSendSayOriginal_(self, forwardedText);
    }
}

int __cdecl SampHooks::HotkeyDispatcherDetour(int key) {
    if (SampHooks::self_ && SampHooks::self_->installed_ && SampHooks::self_->hotkeyBlockCallback_) {
        if (SampHooks::self_->hotkeyBlockCallback_()) {
            return 0;
        }
    }

    return SampHooks::self_ && SampHooks::self_->hotkeyDispatcherOriginal_
        ? SampHooks::self_->hotkeyDispatcherOriginal_(key)
        : 0;
}

int __cdecl SampHooks::InputHotkeyHandlerDetour(int key) {
    if (SampHooks::self_ && SampHooks::self_->installed_ && SampHooks::self_->hotkeyBlockCallback_) {
        if (SampHooks::self_->hotkeyBlockCallback_()) {
            return 0;
        }
    }

    return SampHooks::self_ && SampHooks::self_->inputHotkeyHandlerOriginal_
        ? SampHooks::self_->inputHotkeyHandlerOriginal_(key)
        : 0;
}

bool __fastcall SampHooks::ApplyDamageDetour(std::uintptr_t self, void* edx, std::uintptr_t car, int component, float intensity, float arg3) {
    UNREFERENCED_PARAMETER(edx);

    if (SampHooks::self_ && SampHooks::self_->installed_
        && SampHooks::self_->applyDamageProtectionEnabled_
        && (component < 1 || component > 4)) {
        return false;
    }

    return SampHooks::self_ && SampHooks::self_->applyDamageOriginal_
        ? SampHooks::self_->applyDamageOriginal_(self, car, component, intensity, arg3)
        : false;
}

void __fastcall SampHooks::PadUpdateMouseDetour(CPad* pad, void* edx) {
    UNREFERENCED_PARAMETER(edx);

    if (SampHooks::self_ && SampHooks::self_->padUpdateMouseOriginal_) {
        SampHooks::self_->padUpdateMouseOriginal_(pad);
    }

    if (!SampHooks::self_ || !SampHooks::self_->installed_ || !SampHooks::self_->mouseButtonBlockCallback_) {
        return;
    }

    const std::uint8_t mask = SampHooks::self_->mouseButtonBlockCallback_();
    ClearPadMouseButtons(pad, mask);
}

void SampHooks::SetSampApi(SampApi* sampApi) {
    sampApi_ = sampApi;
    self_ = this;
    debuglog::WriteInfo("SampHooks::SetSampApi assigned=%d", sampApi_ ? 1 : 0);
}

void SampHooks::SetOwnerModule(HMODULE module) {
    ownerModule_ = module;
    debuglog::WriteInfo("SampHooks::SetOwnerModule assigned=%d", ownerModule_ ? 1 : 0);
}

void SampHooks::SetHotkeyBlockCallback(HotkeyBlockCallback callback) {
    hotkeyBlockCallback_ = std::move(callback);
    debuglog::WriteInfo("SampHooks::SetHotkeyBlockCallback assigned=%d", hotkeyBlockCallback_ ? 1 : 0);
}

void SampHooks::SetMouseButtonBlockCallback(MouseButtonBlockCallback callback) {
    mouseButtonBlockCallback_ = std::move(callback);
    debuglog::WriteInfo("SampHooks::SetMouseButtonBlockCallback assigned=%d", mouseButtonBlockCallback_ ? 1 : 0);
}

void SampHooks::SetApplyDamageProtectionEnabled(bool enabled) {
    if (applyDamageProtectionEnabled_ == enabled) {
        return;
    }

    applyDamageProtectionEnabled_ = enabled;
    debuglog::WriteInfo(
        "SampHooks::SetApplyDamageProtectionEnabled enabled=%d",
        applyDamageProtectionEnabled_ ? 1 : 0);
}

void SampHooks::SetChatAsiCompatibilityEnabled(bool enabled) {
    const bool previous = chatAsiCompatibilityEnabled_.exchange(enabled, std::memory_order_relaxed);
    if (previous == enabled) {
        return;
    }

    debuglog::WriteInfo(
        "SampHooks::_chat compatibility enabled=%d standardAddEntryFallback=1",
        enabled ? 1 : 0);
}

void SampHooks::Refresh() {
    if (!sampApi_) {
        statusText_ = "SampApi is not assigned";
        debuglog::WriteError("SampHooks::Refresh skipped: SampApi is not assigned");
        return;
    }

    sampApi_->Refresh();

    if (installed_) {
        return;
    }

    if (!sampApi_->isSAMPInitilizeLua()) {
        statusText_ = sampApi_->lastError();
        debuglog::WriteInfo("SampHooks::Refresh waiting for SA:MP: %s", statusText_.c_str());
        return;
    }

    if (!sampApi_->isSupportedVersion()) {
        statusText_ = "SAMP version is loaded but not supported by hook offsets";
        debuglog::WriteError("SampHooks::Refresh unsupported SAMP version: %s", sampApi_->currentVersionName());
        return;
    }

    debuglog::WriteInfo("SampHooks::Refresh install requested for SAMP %s", sampApi_->currentVersionName());
    Install();
}

void SampHooks::Shutdown() {
    debuglog::WriteInfo("SampHooks::Shutdown begin");
    CleanupHooks();
    installed_ = false;
    statusText_ = "hooks disabled";
    debuglog::WriteInfo("SampHooks::Shutdown done");
}

void SampHooks::AddOnChatMessageHandler(ChatMessageHandler handler) {
    if (handler) {
        onChatMessageHandlers_.push_back(std::move(handler));
    }
}

void SampHooks::AddChatMessageTransform(ChatMessageTransform handler) {
    if (handler) {
        chatMessageTransforms_.push_back(std::move(handler));
    }
}

void SampHooks::AddChatMessageFilter(ChatMessageFilter handler) {
    if (handler) {
        chatMessageFilters_.push_back(std::move(handler));
    }
}

void SampHooks::AddOnSendCommandHandler(SendCommandHandler handler) {
    if (handler) {
        onSendCommandHandlers_.push_back(std::move(handler));
    }
}

void SampHooks::AddOnSendChatHandler(SendChatHandler handler) {
    if (handler) {
        onSendChatHandlers_.push_back(std::move(handler));
    }
}

bool SampHooks::IsInstalled() const {
    return installed_;
}

bool SampHooks::IsChatAddEntryHookActive() {
    return self_
        && self_->installed_
        && self_->chatAddEntryOriginal_ != nullptr;
}

bool SampHooks::CallChatAddEntry(
    std::uintptr_t target,
    void* chat,
    int type,
    const char* text,
    std::size_t textLength,
    unsigned long textColor,
    NativeCallFailure& failure) {
    failure = {};

    const auto addEntry = reinterpret_cast<ChatAddEntryFn>(target);
    if (!IsChatAddEntryHookActive()) {
        __try {
            addEntry(chat, type, text, nullptr, textColor, 0);
            if (type == samp_local_chat::kLocalMessageType
                && textLength > samp_local_chat::kNativeEntryTextBytes) {
                return ExtendLatestChatEntryText(
                    chat,
                    text,
                    textLength,
                    &failure);
            }
            return true;
        }
        __except (CaptureNativeCallFailure(GetExceptionInformation(), &failure)) {
            return false;
        }
    }

    NativeCallFailure* const previousSink = chatAddEntryFailureSink_;
    chatAddEntryFailureSink_ = &failure;
    __try {
        addEntry(chat, type, text, nullptr, textColor, 0);
    }
    __finally {
        chatAddEntryFailureSink_ = previousSink;
    }
    return failure.code == 0;
}

bool SampHooks::IsOutgoingInputTransformActive() {
    return outgoingInputTransformDepth_ > 0;
}

void SampHooks::PushOutgoingInputTransform() {
    ++outgoingInputTransformDepth_;
}

void SampHooks::PopOutgoingInputTransform() {
    --outgoingInputTransformDepth_;
}

const std::string& SampHooks::statusText() const {
    return statusText_;
}

bool SampHooks::Install() {
    if (!sampApi_ || !sampApi_->sampModule() || !sampApi_->isSupportedVersion()) {
        statusText_ = "exact SAMP variant is not approved for hooks";
        debuglog::WriteError("SampHooks install failed: exact SAMP variant is not approved");
        return false;
    }

    const std::uintptr_t sampBase = reinterpret_cast<std::uintptr_t>(sampApi_->sampModule());
    const auto version = sampApi_->currentVersion();

    const std::uintptr_t addEntryTarget = sampBase + SampApi::main_offsets.AddEntry.Get(version);
    const std::uintptr_t addMessageTarget = sampBase + SampApi::main_offsets.AddMessage.Get(version);
    const std::uintptr_t addChatMessageTarget = sampBase + SampApi::main_offsets.AddChatMessage.Get(version);
    const std::uintptr_t inputSendTarget = sampBase + SampApi::main_offsets.CInput_Send.Get(version);
    const std::uintptr_t inputSendSayTarget = sampBase + SampApi::main_offsets.CInput_SendSay.Get(version);
    const std::uintptr_t hotkeyDispatcherTarget = sampBase + SampApi::main_offsets.HotkeyDispatcher.Get(version);
    const std::uintptr_t inputHotkeyHandlerTarget = sampBase + SampApi::main_offsets.InputHotkeyHandler.Get(version);
    const std::uintptr_t damageTarget = kDamageManagerApplyDamageAddress;
    const std::uintptr_t padUpdateMouseTarget = kPadUpdateMouseAddress;
    const ChatAsiAddEntryDiscovery chatAsiDiscovery = FindChatAsiAddEntryDispatch(addEntryTarget);
    const std::uintptr_t chatAsiAddEntryDispatchTarget = chatAsiDiscovery.dispatch;

    debuglog::WriteInfo("SampHooks: sampBase=0x%08X", static_cast<unsigned>(sampBase));
    debuglog::WriteInfo("SampHooks: addEntryTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(addEntryTarget), static_cast<unsigned>(addEntryTarget - sampBase));
    debuglog::WriteInfo("SampHooks: addMessageTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(addMessageTarget), static_cast<unsigned>(addMessageTarget - sampBase));
    debuglog::WriteInfo("SampHooks: addChatMessageTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(addChatMessageTarget), static_cast<unsigned>(addChatMessageTarget - sampBase));
    debuglog::WriteInfo("SampHooks: inputSendTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(inputSendTarget), static_cast<unsigned>(inputSendTarget - sampBase));
    debuglog::WriteInfo("SampHooks: inputSendSayTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(inputSendSayTarget), static_cast<unsigned>(inputSendSayTarget - sampBase));
    debuglog::WriteInfo("SampHooks: hotkeyDispatcherTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(hotkeyDispatcherTarget), static_cast<unsigned>(hotkeyDispatcherTarget - sampBase));
    debuglog::WriteInfo("SampHooks: inputHotkeyHandlerTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(inputHotkeyHandlerTarget), static_cast<unsigned>(inputHotkeyHandlerTarget - sampBase));
    debuglog::WriteInfo("SampHooks: damageTarget=0x%08X", static_cast<unsigned>(damageTarget));
    debuglog::WriteInfo("SampHooks: padUpdateMouseTarget=0x%08X", static_cast<unsigned>(padUpdateMouseTarget));
    if (chatAsiDiscovery) {
        const auto chatModule = reinterpret_cast<std::uintptr_t>(chatAsiDiscovery.module);
        const std::string wrapperBytes = FormatCodeBytes(chatAsiDiscovery.wrapper, 32, chatAsiDiscovery.module);
        const std::string dispatchBytes = FormatCodeBytes(chatAsiDiscovery.dispatch, 16, chatAsiDiscovery.module);
        debuglog::WriteInfo(
            "SampHooks: _chat discovery method=%s variant=%s transferDepth=%llu signatureCandidates=%llu wrapper=0x%08X wrapperRva=0x%X dispatch=0x%08X dispatchRva=0x%X wrapperBytes=[%s] dispatchBytes=[%s]",
            chatAsiDiscovery.method,
            chatAsiDiscovery.wrapperVariant,
            static_cast<unsigned long long>(chatAsiDiscovery.transferDepth),
            static_cast<unsigned long long>(chatAsiDiscovery.signatureCandidates),
            static_cast<unsigned>(chatAsiDiscovery.wrapper),
            static_cast<unsigned>(chatAsiDiscovery.wrapper - chatModule),
            static_cast<unsigned>(chatAsiDiscovery.dispatch),
            static_cast<unsigned>(chatAsiDiscovery.dispatch - chatModule),
            wrapperBytes.c_str(),
            dispatchBytes.c_str());
    } else if (chatAsiDiscovery.module) {
        const std::string transferBytes = FormatCodeBytes(
            chatAsiDiscovery.observedTransferTarget,
            32,
            chatAsiDiscovery.module);
        debuglog::WriteInfo(
            "SampHooks: _chat AddEntry dispatch not recognized transferDepth=%llu transferTarget=0x%08X transferBytes=[%s] signatureCandidates=%llu; standard AddEntry fallback remains active",
            static_cast<unsigned long long>(chatAsiDiscovery.transferDepth),
            static_cast<unsigned>(chatAsiDiscovery.observedTransferTarget),
            transferBytes.c_str(),
            static_cast<unsigned long long>(chatAsiDiscovery.signatureCandidates));
    } else {
        debuglog::WriteInfo("SampHooks: _chat.asi is not loaded; standard AddEntry fallback remains active");
    }

    if (addEntryTarget == sampBase || addMessageTarget == sampBase || addChatMessageTarget == sampBase
        || inputSendTarget == sampBase || inputSendSayTarget == sampBase
        || hotkeyDispatcherTarget == sampBase || inputHotkeyHandlerTarget == sampBase) {
        statusText_ = "some hook offsets are missing for the detected SAMP version";
        debuglog::WriteError("SampHooks install failed: some hook offsets are missing");
        return false;
    }

    const std::array<std::pair<const char*, std::uintptr_t>, 7> sampTargets{ {
        { "AddEntry", addEntryTarget },
        { "AddMessage", addMessageTarget },
        { "AddChatMessage", addChatMessageTarget },
        { "CInput_Send", inputSendTarget },
        { "CInput_SendSay", inputSendSayTarget },
        { "HotkeyDispatcher", hotkeyDispatcherTarget },
        { "InputHotkeyHandler", inputHotkeyHandlerTarget },
    } };
    for (const auto& [name, target] : sampTargets) {
        if (!IsExecutableRange(target, 1, sampApi_->sampModule())) {
            statusText_ = std::string("SAMP hook target is not executable: ") + name;
            debuglog::WriteError(
                "SampHooks install failed: %s target=0x%08X is outside executable samp.dll memory",
                name,
                static_cast<unsigned>(target));
            return false;
        }
    }

    chatAddEntryTarget_ = reinterpret_cast<void*>(addEntryTarget);
    chatAsiAddEntryDispatchTarget_ = reinterpret_cast<void*>(chatAsiAddEntryDispatchTarget);
    chatAddMessageTarget_ = reinterpret_cast<void*>(addMessageTarget);
    chatAddChatMessageTarget_ = reinterpret_cast<void*>(addChatMessageTarget);
    inputSendTarget_ = reinterpret_cast<void*>(inputSendTarget);
    inputSendSayTarget_ = reinterpret_cast<void*>(inputSendSayTarget);
    hotkeyDispatcherTarget_ = reinterpret_cast<void*>(hotkeyDispatcherTarget);
    inputHotkeyHandlerTarget_ = reinterpret_cast<void*>(inputHotkeyHandlerTarget);
    applyDamageTarget_ = reinterpret_cast<void*>(damageTarget);
    padUpdateMouseTarget_ = reinterpret_cast<void*>(padUpdateMouseTarget);

    const auto failInstall = [this](const char* statusText, const char* logMessage) {
        statusText_ = statusText;
        debuglog::WriteError("%s", logMessage);
        CleanupHooks();
        return false;
    };

    if (chatAsiAddEntryDispatchTarget_
        && !minhook::CreateAndEnableHook(
            chatAsiAddEntryDispatchTarget_,
            reinterpret_cast<void*>(&ChatAsiAddEntryDispatchDetour),
            &chatAsiAddEntryDispatchOriginal_,
            "SampHooks::_chat AddEntry dispatch")) {
        debuglog::WriteError(
            "SampHooks: secondary _chat AddEntry filter unavailable; standard AddEntry fallback remains active");
        chatAsiAddEntryDispatchTarget_ = nullptr;
        chatAsiAddEntryDispatchOriginal_ = nullptr;
    }

    if (!minhook::CreateAndEnableHook(chatAddEntryTarget_, reinterpret_cast<void*>(&ChatAddEntryDetour), &chatAddEntryOriginal_, "SampHooks::AddEntry")) {
        return failInstall("MinHook install failed for AddEntry", "SampHooks: MinHook install failed for AddEntry");
    }

    if (!minhook::CreateAndEnableHook(chatAddMessageTarget_, reinterpret_cast<void*>(&ChatAddMessageDetour), &chatAddMessageOriginal_, "SampHooks::AddMessage")) {
        return failInstall("MinHook install failed for AddMessage", "SampHooks: MinHook install failed for AddMessage");
    }

    if (!minhook::CreateAndEnableHook(chatAddChatMessageTarget_, reinterpret_cast<void*>(&ChatAddChatMessageDetour), &chatAddChatMessageOriginal_, "SampHooks::AddChatMessage")) {
        return failInstall("MinHook install failed for AddChatMessage", "SampHooks: MinHook install failed for AddChatMessage");
    }

    if (!minhook::CreateAndEnableHook(inputSendTarget_, reinterpret_cast<void*>(&InputSendDetour), &inputSendOriginal_, "SampHooks::CInput_Send")) {
        return failInstall("MinHook install failed for CInput_Send", "SampHooks: MinHook install failed for CInput_Send");
    }

    if (!minhook::CreateAndEnableHook(inputSendSayTarget_, reinterpret_cast<void*>(&InputSendSayDetour), &inputSendSayOriginal_, "SampHooks::CInput_SendSay")) {
        return failInstall("MinHook install failed for CInput_SendSay", "SampHooks: MinHook install failed for CInput_SendSay");
    }

    if (!minhook::CreateAndEnableHook(hotkeyDispatcherTarget_, reinterpret_cast<void*>(&HotkeyDispatcherDetour), &hotkeyDispatcherOriginal_, "SampHooks::HotkeyDispatcher")) {
        return failInstall("MinHook install failed for HotkeyDispatcher", "SampHooks: MinHook install failed for HotkeyDispatcher");
    }

    if (!minhook::CreateAndEnableHook(inputHotkeyHandlerTarget_, reinterpret_cast<void*>(&InputHotkeyHandlerDetour), &inputHotkeyHandlerOriginal_, "SampHooks::InputHotkeyHandler")) {
        return failInstall("MinHook install failed for InputHotkeyHandler", "SampHooks: MinHook install failed for InputHotkeyHandler");
    }

    if (!minhook::CreateAndEnableHook(applyDamageTarget_, reinterpret_cast<void*>(&ApplyDamageDetour), &applyDamageOriginal_, "SampHooks::CDamageManager_ApplyDamage")) {
        return failInstall("MinHook install failed for CDamageManager_ApplyDamage", "SampHooks: MinHook install failed for CDamageManager_ApplyDamage");
    }

    if (!minhook::CreateAndEnableHook(padUpdateMouseTarget_, reinterpret_cast<void*>(&PadUpdateMouseDetour), &padUpdateMouseOriginal_, "SampHooks::CPad_UpdateMouse")) {
        debuglog::WriteError("SampHooks: quick menu mouse suppression unavailable: MinHook install failed for CPad::UpdateMouse");
        padUpdateMouseTarget_ = nullptr;
        padUpdateMouseOriginal_ = nullptr;
    }

    installed_ = true;
    statusText_ = "hooks installed";
    debuglog::WriteInfo("SampHooks: installed for SAMP version %s", sampApi_->currentVersionName());
    return true;
}

void SampHooks::CleanupHooks() {
    debuglog::WriteInfo("SampHooks::CleanupHooks begin");
    installed_ = false;
    minhook::DisableAndRemoveHook(chatAddEntryTarget_, "SampHooks::AddEntry");
    minhook::DisableAndRemoveHook(chatAsiAddEntryDispatchTarget_, "SampHooks::_chat AddEntry dispatch");
    minhook::DisableAndRemoveHook(chatAddMessageTarget_, "SampHooks::AddMessage");
    minhook::DisableAndRemoveHook(chatAddChatMessageTarget_, "SampHooks::AddChatMessage");
    minhook::DisableAndRemoveHook(inputSendTarget_, "SampHooks::CInput_Send");
    minhook::DisableAndRemoveHook(inputSendSayTarget_, "SampHooks::CInput_SendSay");
    minhook::DisableAndRemoveHook(hotkeyDispatcherTarget_, "SampHooks::HotkeyDispatcher");
    minhook::DisableAndRemoveHook(inputHotkeyHandlerTarget_, "SampHooks::InputHotkeyHandler");
    minhook::DisableAndRemoveHook(applyDamageTarget_, "SampHooks::CDamageManager_ApplyDamage");
    minhook::DisableAndRemoveHook(padUpdateMouseTarget_, "SampHooks::CPad_UpdateMouse");

    chatAddEntryOriginal_ = nullptr;
    chatAsiAddEntryDispatchOriginal_ = nullptr;
    chatAddMessageOriginal_ = nullptr;
    chatAddChatMessageOriginal_ = nullptr;
    inputSendOriginal_ = nullptr;
    inputSendSayOriginal_ = nullptr;
    hotkeyDispatcherOriginal_ = nullptr;
    inputHotkeyHandlerOriginal_ = nullptr;
    applyDamageOriginal_ = nullptr;
    padUpdateMouseOriginal_ = nullptr;
    debuglog::WriteInfo("SampHooks::CleanupHooks done");
}
