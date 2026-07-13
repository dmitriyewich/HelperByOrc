#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

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
    using ChatMessageFilter = std::function<bool(ChatMessageSource, int, const std::string&, const std::string&, std::uint32_t, std::uint32_t)>;
    using SendCommandHandler = std::function<bool(std::string&)>;
    using SendChatHandler = std::function<bool(std::string&)>;
    using HotkeyBlockCallback = std::function<bool()>;
    enum MouseButtonMask : std::uint8_t {
        MouseButtonLeft = 1u << 0,
        MouseButtonRight = 1u << 1,
        MouseButtonMiddle = 1u << 2,
    };
    using MouseButtonBlockCallback = std::function<std::uint8_t()>;

    void SetSampApi(SampApi* sampApi);
    void SetHotkeyBlockCallback(HotkeyBlockCallback callback);
    void SetMouseButtonBlockCallback(MouseButtonBlockCallback callback);
    void Refresh();
    void Shutdown();

    bool IsInstalled() const;
    static bool IsOutgoingInputTransformActive();
    static void PushOutgoingInputTransform();
    static void PopOutgoingInputTransform();
    const std::string& statusText() const;
    std::vector<std::string> GetRecentLog() const;
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
    using CDialogShowFn = void(__thiscall*)(std::uintptr_t, int, int, const char*, const char*, const char*, const char*, bool);
    using CDialogCloseFn = void(__thiscall*)(std::uintptr_t, char);
    using CInputSendFn = void(__thiscall*)(std::uintptr_t, const char*);
    using CInputSendSayFn = void(__thiscall*)(std::uintptr_t, const char*);
    using HotkeyDispatcherFn = int(__cdecl*)(int);
    using InputHotkeyHandlerFn = int(__cdecl*)(int);
    using ApplyDamageFn = bool(__thiscall*)(std::uintptr_t, std::uintptr_t, int, float, float);
    using CPadUpdateMouseFn = void(__thiscall*)(CPad*);

    bool Install();
    void CleanupHooks();
    void AppendLog(const char* format, ...);
    static std::string Truncate(std::string text, std::size_t maxLength);
    static void __fastcall ChatAddEntryDetour(void* chat, void* edx, int type, const char* text, const char* prefix, unsigned long textColor, unsigned long prefixColor);
    static void __cdecl ChatAsiAddEntryDispatchDetour(int type, const char* text, const char* prefix, unsigned long textColor, unsigned long prefixColor);
    static void __fastcall ChatAddMessageDetour(void* chat, void* edx, unsigned long color, const char* text);
    static void __fastcall ChatAddChatMessageDetour(void* chat, void* edx, const char* prefix, unsigned long prefixColor, const char* text);
    static void __fastcall DialogShowDetour(std::uintptr_t self, void* edx, int dialogId, int style, const char* title, const char* text, const char* button1, const char* button2, bool serverside);
    static void __fastcall DialogCloseDetour(std::uintptr_t self, void* edx, char button);
    static void __fastcall InputSendDetour(std::uintptr_t self, void* edx, const char* text);
    static void __fastcall InputSendSayDetour(std::uintptr_t self, void* edx, const char* text);
    static int __cdecl HotkeyDispatcherDetour(int key);
    static int __cdecl InputHotkeyHandlerDetour(int key);
    static bool __fastcall ApplyDamageDetour(std::uintptr_t self, void* edx, std::uintptr_t car, int component, float intensity, float arg3);
    static void __fastcall PadUpdateMouseDetour(CPad* pad, void* edx);

    static inline SampHooks* self_ = nullptr;
    static inline thread_local int outgoingInputTransformDepth_ = 0;
    static inline thread_local int cchatForwardDepth_ = 0;

    SampApi* sampApi_ = nullptr;
    bool installed_ = false;
    std::string statusText_ = "waiting for samp.dll";
    mutable std::mutex logMutex_;
    std::vector<std::string> recentLog_;
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
    void* dialogShowTarget_ = nullptr;
    void* dialogCloseTarget_ = nullptr;
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
    CDialogShowFn dialogShowOriginal_ = nullptr;
    CDialogCloseFn dialogCloseOriginal_ = nullptr;
    CInputSendFn inputSendOriginal_ = nullptr;
    CInputSendSayFn inputSendSayOriginal_ = nullptr;
    HotkeyDispatcherFn hotkeyDispatcherOriginal_ = nullptr;
    InputHotkeyHandlerFn inputHotkeyHandlerOriginal_ = nullptr;
    ApplyDamageFn applyDamageOriginal_ = nullptr;
    CPadUpdateMouseFn padUpdateMouseOriginal_ = nullptr;
};
