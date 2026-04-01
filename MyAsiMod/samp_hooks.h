#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class SampApi;

class SampHooks {
public:
    using ChatMessageHandler = std::function<void(int, const std::string&, const std::string&, std::uint32_t, std::uint32_t)>;
    using SendCommandHandler = std::function<bool(std::string&)>;
    using SendChatHandler = std::function<bool(std::string&)>;
    using HotkeyBlockCallback = std::function<bool()>;

    void SetSampApi(SampApi* sampApi);
    void SetHotkeyBlockCallback(HotkeyBlockCallback callback);
    void Refresh();
    void Shutdown();

    bool IsInstalled() const;
    static bool IsOutgoingInputTransformActive();
    static void PushOutgoingInputTransform();
    static void PopOutgoingInputTransform();
    const std::string& statusText() const;
    std::vector<std::string> GetRecentLog() const;
    void AddOnChatMessageHandler(ChatMessageHandler handler);
    void AddOnSendCommandHandler(SendCommandHandler handler);
    void AddOnSendChatHandler(SendChatHandler handler);
    void onChatMessage(ChatMessageHandler handler) { AddOnChatMessageHandler(std::move(handler)); }
    void onSendCommand(SendCommandHandler handler) { AddOnSendCommandHandler(std::move(handler)); }
    void onSendChat(SendChatHandler handler) { AddOnSendChatHandler(std::move(handler)); }

private:
    using ChatAddEntryFn = void(__thiscall*)(void*, int, const char*, const char*, unsigned long, unsigned long);
    using CDialogShowFn = void(__thiscall*)(std::uintptr_t, int, int, const char*, const char*, const char*, const char*, bool);
    using CDialogCloseFn = void(__thiscall*)(std::uintptr_t, char);
    using CInputSendFn = void(__thiscall*)(std::uintptr_t, const char*);
    using CInputSendSayFn = void(__thiscall*)(std::uintptr_t, const char*);
    using HotkeyDispatcherFn = int(__cdecl*)(int);
    using ApplyDamageFn = bool(__thiscall*)(std::uintptr_t, std::uintptr_t, int, float, float);

    bool Install();
    void CleanupHooks();
    void AppendLog(const char* format, ...);
    static std::string Truncate(std::string text, std::size_t maxLength);
    static void __fastcall ChatAddEntryDetour(void* chat, void* edx, int type, const char* text, const char* prefix, unsigned long textColor, unsigned long prefixColor);
    static void __fastcall DialogShowDetour(std::uintptr_t self, void* edx, int dialogId, int style, const char* title, const char* text, const char* button1, const char* button2, bool serverside);
    static void __fastcall DialogCloseDetour(std::uintptr_t self, void* edx, char button);
    static void __fastcall InputSendDetour(std::uintptr_t self, void* edx, const char* text);
    static void __fastcall InputSendSayDetour(std::uintptr_t self, void* edx, const char* text);
    static int __cdecl HotkeyDispatcherDetour(int key);
    static bool __fastcall ApplyDamageDetour(std::uintptr_t self, void* edx, std::uintptr_t car, int component, float intensity, float arg3);

    static inline SampHooks* self_ = nullptr;
    static inline thread_local int outgoingInputTransformDepth_ = 0;

    SampApi* sampApi_ = nullptr;
    bool installed_ = false;
    std::string statusText_ = "waiting for samp.dll";
    mutable std::mutex logMutex_;
    std::vector<std::string> recentLog_;
    std::vector<ChatMessageHandler> onChatMessageHandlers_;
    std::vector<SendCommandHandler> onSendCommandHandlers_;
    std::vector<SendChatHandler> onSendChatHandlers_;
    HotkeyBlockCallback hotkeyBlockCallback_;

    void* chatAddEntryTarget_ = nullptr;
    void* dialogShowTarget_ = nullptr;
    void* dialogCloseTarget_ = nullptr;
    void* inputSendTarget_ = nullptr;
    void* inputSendSayTarget_ = nullptr;
    void* hotkeyDispatcherTarget_ = nullptr;
    void* applyDamageTarget_ = nullptr;
    ChatAddEntryFn chatAddEntryOriginal_ = nullptr;
    CDialogShowFn dialogShowOriginal_ = nullptr;
    CDialogCloseFn dialogCloseOriginal_ = nullptr;
    CInputSendFn inputSendOriginal_ = nullptr;
    CInputSendSayFn inputSendSayOriginal_ = nullptr;
    HotkeyDispatcherFn hotkeyDispatcherOriginal_ = nullptr;
    ApplyDamageFn applyDamageOriginal_ = nullptr;
};
