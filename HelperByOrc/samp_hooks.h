#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "samp_call_context.h"

class SampApi;
class CPad;

class SampHooks {
public:
    enum class ChatMessageSource {
        AddEntry,
        AddMessage,
        AddChatMessage,
    };

    using ChatMessageHandler = std::function<void(int, const std::string&, const std::string&, std::uint32_t, std::uint32_t)>;
    using ChatMessageTransform = std::function<bool(
        ChatMessageSource,
        int,
        std::string&,
        const std::string&,
        std::uint32_t,
        std::uint32_t,
        const SampCallContext&)>;
    using ChatMessageFilter = std::function<bool(ChatMessageSource, int, const std::string&, const std::string&, std::uint32_t, std::uint32_t)>;
    using SendCommandHandler = std::function<bool(std::string&, const SampCallContext&)>;
    using SendChatHandler = std::function<bool(std::string&, const SampCallContext&)>;
    using HotkeyBlockCallback = std::function<bool()>;
    enum MouseButtonMask : std::uint8_t {
        MouseButtonLeft = 1u << 0,
        MouseButtonRight = 1u << 1,
        MouseButtonMiddle = 1u << 2,
    };
    using MouseButtonBlockCallback = std::function<std::uint8_t()>;

    struct NativeCallFailure {
        std::uint32_t code = 0;
        const void* address = nullptr;
    };

    void SetSampApi(SampApi* sampApi);
    void SetOwnerModule(HMODULE module);
    void SetHotkeyBlockCallback(HotkeyBlockCallback callback);
    void SetMouseButtonBlockCallback(MouseButtonBlockCallback callback);
    void Refresh();
    void Shutdown();

    bool IsInstalled() const;
    static bool IsChatAddEntryHookActive();
    static bool CallChatAddEntry(
        std::uintptr_t target,
        void* chat,
        int type,
        const char* text,
        std::size_t textLength,
        unsigned long textColor,
        NativeCallFailure& failure);
    static bool IsOutgoingInputTransformActive();
    static void PushOutgoingInputTransform();
    static void PopOutgoingInputTransform();
    const std::string& statusText() const;
    void AddChatMessageTransform(ChatMessageTransform handler);
    void AddChatMessageFilter(ChatMessageFilter handler);
    void AddOnChatMessageHandler(ChatMessageHandler handler);
    void AddOnSendCommandHandler(SendCommandHandler handler);
    void AddOnSendChatHandler(SendChatHandler handler);
    void SetApplyDamageProtectionEnabled(bool enabled);
    void SetChatAsiCompatibilityEnabled(bool enabled);
    void onChatMessage(ChatMessageHandler handler) { AddOnChatMessageHandler(std::move(handler)); }
    void onSendCommand(SendCommandHandler handler) { AddOnSendCommandHandler(std::move(handler)); }
    void onSendChat(SendChatHandler handler) { AddOnSendChatHandler(std::move(handler)); }

private:
    using ChatAddEntryFn = void(__thiscall*)(void*, int, const char*, const char*, unsigned long, unsigned long);
    using ChatAsiAddEntryDispatchFn = void(__cdecl*)(int, const char*, const char*, unsigned long, unsigned long);
    using ChatAddMessageFn = void(__thiscall*)(void*, unsigned long, const char*);
    using ChatAddChatMessageFn = void(__thiscall*)(void*, const char*, unsigned long, const char*);
    using CInputSendFn = void(__thiscall*)(std::uintptr_t, const char*);
    using CInputSendSayFn = void(__thiscall*)(std::uintptr_t, const char*);
    using HotkeyDispatcherFn = int(__cdecl*)(int);
    using InputHotkeyHandlerFn = int(__cdecl*)(int);
    using ApplyDamageFn = bool(__thiscall*)(std::uintptr_t, std::uintptr_t, int, float, float);
    using CPadUpdateMouseFn = void(__thiscall*)(CPad*);

    bool Install();
    void CleanupHooks();
    static void __fastcall ChatAddEntryDetour(void* chat, void* edx, int type, const char* text, const char* prefix, unsigned long textColor, unsigned long prefixColor);
    static void __cdecl ChatAsiAddEntryDispatchDetour(int type, const char* text, const char* prefix, unsigned long textColor, unsigned long prefixColor);
    static void __fastcall ChatAddMessageDetour(void* chat, void* edx, unsigned long color, const char* text);
    static void __fastcall ChatAddChatMessageDetour(void* chat, void* edx, const char* prefix, unsigned long prefixColor, const char* text);
    static bool CallChatAddEntryOriginal(
        ChatAddEntryFn original,
        void* chat,
        int type,
        const char* text,
        std::size_t textLength,
        const char* prefix,
        unsigned long textColor,
        unsigned long prefixColor);
    static bool ExtendLatestChatEntryText(
        void* chat,
        const char* text,
        std::size_t textLength,
        NativeCallFailure* failure) noexcept;
    static int CaptureChatAddEntryException(EXCEPTION_POINTERS* exception) noexcept;
    static void CallChatAddMessageOriginal(ChatAddMessageFn original, void* chat, unsigned long color, const char* text);
    static void CallChatAddChatMessageOriginal(
        ChatAddChatMessageFn original,
        void* chat,
        const char* prefix,
        unsigned long prefixColor,
        const char* text);
    static void __fastcall InputSendDetour(std::uintptr_t self, void* edx, const char* text);
    static void __fastcall InputSendSayDetour(std::uintptr_t self, void* edx, const char* text);
    static int __cdecl HotkeyDispatcherDetour(int key);
    static int __cdecl InputHotkeyHandlerDetour(int key);
    static bool __fastcall ApplyDamageDetour(std::uintptr_t self, void* edx, std::uintptr_t car, int component, float intensity, float arg3);
    static void __fastcall PadUpdateMouseDetour(CPad* pad, void* edx);

    static inline SampHooks* self_ = nullptr;
    static inline thread_local int outgoingInputTransformDepth_ = 0;
    static inline thread_local int cchatForwardDepth_ = 0;
    static inline thread_local NativeCallFailure* chatAddEntryFailureSink_ = nullptr;

    SampApi* sampApi_ = nullptr;
    HMODULE ownerModule_ = nullptr;
    bool installed_ = false;
    std::string statusText_ = "waiting for samp.dll";
    std::vector<ChatMessageTransform> chatMessageTransforms_;
    std::vector<ChatMessageFilter> chatMessageFilters_;
    std::vector<ChatMessageHandler> onChatMessageHandlers_;
    std::vector<SendCommandHandler> onSendCommandHandlers_;
    std::vector<SendChatHandler> onSendChatHandlers_;
    HotkeyBlockCallback hotkeyBlockCallback_;
    MouseButtonBlockCallback mouseButtonBlockCallback_;
    bool applyDamageProtectionEnabled_ = true;
    std::atomic_bool chatAsiCompatibilityEnabled_{true};

    void* chatAddEntryTarget_ = nullptr;
    void* chatAsiAddEntryDispatchTarget_ = nullptr;
    void* chatAddMessageTarget_ = nullptr;
    void* chatAddChatMessageTarget_ = nullptr;
    void* inputSendTarget_ = nullptr;
    void* inputSendSayTarget_ = nullptr;
    void* hotkeyDispatcherTarget_ = nullptr;
    void* inputHotkeyHandlerTarget_ = nullptr;
    void* applyDamageTarget_ = nullptr;
    void* padUpdateMouseTarget_ = nullptr;
    ChatAddEntryFn chatAddEntryOriginal_ = nullptr;
    ChatAsiAddEntryDispatchFn chatAsiAddEntryDispatchOriginal_ = nullptr;
    ChatAddMessageFn chatAddMessageOriginal_ = nullptr;
    ChatAddChatMessageFn chatAddChatMessageOriginal_ = nullptr;
    CInputSendFn inputSendOriginal_ = nullptr;
    CInputSendSayFn inputSendSayOriginal_ = nullptr;
    HotkeyDispatcherFn hotkeyDispatcherOriginal_ = nullptr;
    InputHotkeyHandlerFn inputHotkeyHandlerOriginal_ = nullptr;
    ApplyDamageFn applyDamageOriginal_ = nullptr;
    CPadUpdateMouseFn padUpdateMouseOriginal_ = nullptr;
};
