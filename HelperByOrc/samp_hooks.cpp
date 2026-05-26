#include "samp_hooks.h"

#include "debug_log.h"
#include "minhook_utils.h"
#include "samp_api.h"
#include "text_encoding.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr std::size_t kMaxLogEntries = 64;
constexpr std::uintptr_t kDamageManagerApplyDamageAddress = 0x6C24B0;

struct OutgoingInputTransformScope {
    OutgoingInputTransformScope() {
        SampHooks::PushOutgoingInputTransform();
    }

    ~OutgoingInputTransformScope() {
        SampHooks::PopOutgoingInputTransform();
    }
};

} // namespace

void __fastcall SampHooks::ChatAddEntryDetour(void* chat, void* edx, int type, const char* text, const char* prefix, unsigned long textColor, unsigned long prefixColor) {
    UNREFERENCED_PARAMETER(edx);

    if (self_ && self_->installed_) {
        const std::string textUtf8 = textencoding::GameToUtf8(text ? text : "");
        const std::string prefixUtf8 = textencoding::GameToUtf8(prefix ? prefix : "");
        if (cchatForwardDepth_ == 0) {
            for (const auto& filter : self_->chatMessageFilters_) {
                if (!filter(ChatMessageSource::AddEntry, type, textUtf8, prefixUtf8, textColor, prefixColor)) {
                    return;
                }
            }
        }

        self_->AppendLog(
            "AddEntry type=%d prefix=%s text=%s color=%08lX prefixColor=%08lX",
            type,
            Truncate(prefixUtf8, 48).c_str(),
            Truncate(textUtf8, 96).c_str(),
            textColor,
            prefixColor);
        for (const auto& handler : self_->onChatMessageHandlers_) {
            handler(type, textUtf8, prefixUtf8, textColor, prefixColor);
        }
    }

    if (self_ && self_->chatAddEntryOriginal_) {
        self_->chatAddEntryOriginal_(chat, type, text, prefix, textColor, prefixColor);
    }
}

void __fastcall SampHooks::ChatAddMessageDetour(void* chat, void* edx, unsigned long color, const char* text) {
    UNREFERENCED_PARAMETER(edx);

    if (self_ && self_->installed_) {
        const std::string textUtf8 = textencoding::GameToUtf8(text ? text : "");
        if (cchatForwardDepth_ == 0) {
            for (const auto& filter : self_->chatMessageFilters_) {
                if (!filter(ChatMessageSource::AddMessage, 4, textUtf8, std::string(), color, 0)) {
                    return;
                }
            }
        }
    }

    if (self_ && self_->chatAddMessageOriginal_) {
        ++cchatForwardDepth_;
        self_->chatAddMessageOriginal_(chat, color, text);
        --cchatForwardDepth_;
    }
}

void __fastcall SampHooks::ChatAddChatMessageDetour(void* chat, void* edx, const char* prefix, unsigned long prefixColor, const char* text) {
    UNREFERENCED_PARAMETER(edx);

    if (self_ && self_->installed_) {
        const std::string textUtf8 = textencoding::GameToUtf8(text ? text : "");
        const std::string prefixUtf8 = textencoding::GameToUtf8(prefix ? prefix : "");
        if (cchatForwardDepth_ == 0) {
            for (const auto& filter : self_->chatMessageFilters_) {
                if (!filter(ChatMessageSource::AddChatMessage, 2, textUtf8, prefixUtf8, 0, prefixColor)) {
                    return;
                }
            }
        }
    }

    if (self_ && self_->chatAddChatMessageOriginal_) {
        ++cchatForwardDepth_;
        self_->chatAddChatMessageOriginal_(chat, prefix, prefixColor, text);
        --cchatForwardDepth_;
    }
}

void __fastcall SampHooks::DialogShowDetour(std::uintptr_t self, void* edx, int dialogId, int style, const char* title, const char* text, const char* button1, const char* button2, bool serverside) {
    UNREFERENCED_PARAMETER(edx);

    if (SampHooks::self_ && SampHooks::self_->installed_) {
        SampHooks::self_->AppendLog(
            "CDialog_Show id=%d style=%d title=%s button1=%s button2=%s text=%s serverside=%d",
            dialogId,
            style,
            Truncate(textencoding::GameToUtf8(title ? title : ""), 48).c_str(),
            Truncate(textencoding::GameToUtf8(button1 ? button1 : ""), 24).c_str(),
            Truncate(textencoding::GameToUtf8(button2 ? button2 : ""), 24).c_str(),
            Truncate(textencoding::GameToUtf8(text ? text : ""), 96).c_str(),
            serverside ? 1 : 0);
    }

    if (SampHooks::self_ && SampHooks::self_->dialogShowOriginal_) {
        SampHooks::self_->dialogShowOriginal_(self, dialogId, style, title, text, button1, button2, serverside);
    }
}

void __fastcall SampHooks::DialogCloseDetour(std::uintptr_t self, void* edx, char button) {
    UNREFERENCED_PARAMETER(edx);

    if (SampHooks::self_ && SampHooks::self_->installed_) {
        SampHooks::self_->AppendLog("CDialog_Close button=%d", static_cast<int>(button));
    }

    if (SampHooks::self_ && SampHooks::self_->dialogCloseOriginal_) {
        SampHooks::self_->dialogCloseOriginal_(self, button);
    }
}

void __fastcall SampHooks::InputSendDetour(std::uintptr_t self, void* edx, const char* text) {
    UNREFERENCED_PARAMETER(edx);

    const char* forwardedText = text;
    std::string textUtf8;
    std::string textGame;

    if (SampHooks::self_ && SampHooks::self_->installed_) {
        textUtf8 = textencoding::GameToUtf8(text ? text : "");
        SampHooks::self_->AppendLog("CInput_Send text=%s", Truncate(textUtf8, 96).c_str());
        for (const auto& handler : SampHooks::self_->onSendCommandHandlers_) {
            if (!handler(textUtf8)) {
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

    const char* forwardedText = text;
    std::string textUtf8;
    std::string textGame;

    if (SampHooks::self_ && SampHooks::self_->installed_) {
        textUtf8 = textencoding::GameToUtf8(text ? text : "");
        SampHooks::self_->AppendLog("CInput_SendSay text=%s", Truncate(textUtf8, 96).c_str());
        for (const auto& handler : SampHooks::self_->onSendChatHandlers_) {
            if (!handler(textUtf8)) {
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

    if (SampHooks::self_ && SampHooks::self_->installed_) {
        SampHooks::self_->AppendLog(
            "CDamageManager_ApplyDamage component=%d intensity=%.2f arg3=%.2f",
            component,
            intensity,
            arg3);
    }

    if (SampHooks::self_ && SampHooks::self_->installed_
        && SampHooks::self_->applyDamageProtectionEnabled_
        && (component < 1 || component > 4)) {
        if (SampHooks::self_ && SampHooks::self_->installed_) {
            SampHooks::self_->AppendLog("CDamageManager_ApplyDamage blocked for component=%d", component);
        }
        return false;
    }

    return SampHooks::self_ && SampHooks::self_->applyDamageOriginal_
        ? SampHooks::self_->applyDamageOriginal_(self, car, component, intensity, arg3)
        : false;
}

void SampHooks::SetSampApi(SampApi* sampApi) {
    sampApi_ = sampApi;
    self_ = this;
    debuglog::WriteInfo("SampHooks::SetSampApi assigned=%d", sampApi_ ? 1 : 0);
}

void SampHooks::SetHotkeyBlockCallback(HotkeyBlockCallback callback) {
    hotkeyBlockCallback_ = std::move(callback);
    debuglog::WriteInfo("SampHooks::SetHotkeyBlockCallback assigned=%d", hotkeyBlockCallback_ ? 1 : 0);
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

std::vector<std::string> SampHooks::GetRecentLog() const {
    std::lock_guard lock(logMutex_);
    return recentLog_;
}

bool SampHooks::Install() {
    const std::uintptr_t sampBase = reinterpret_cast<std::uintptr_t>(sampApi_->sampModule());
    const auto version = sampApi_->currentVersion();

    const std::uintptr_t addEntryTarget = sampBase + SampApi::main_offsets.AddEntry.Get(version);
    const std::uintptr_t addMessageTarget = sampBase + SampApi::main_offsets.AddMessage.Get(version);
    const std::uintptr_t addChatMessageTarget = sampBase + SampApi::main_offsets.AddChatMessage.Get(version);
    const std::uintptr_t dialogShowTarget = sampBase + SampApi::main_offsets.CDialog_Show.Get(version);
    const std::uintptr_t dialogCloseTarget = sampBase + SampApi::main_offsets.CDialog_Close.Get(version);
    const std::uintptr_t inputSendTarget = sampBase + SampApi::main_offsets.CInput_Send.Get(version);
    const std::uintptr_t inputSendSayTarget = sampBase + SampApi::main_offsets.CInput_SendSay.Get(version);
    const std::uintptr_t hotkeyDispatcherTarget = sampBase + SampApi::main_offsets.HotkeyDispatcher.Get(version);
    const std::uintptr_t inputHotkeyHandlerTarget = sampBase + SampApi::main_offsets.InputHotkeyHandler.Get(version);
    const std::uintptr_t damageTarget = kDamageManagerApplyDamageAddress;

    debuglog::WriteInfo("SampHooks: sampBase=0x%08X", static_cast<unsigned>(sampBase));
    debuglog::WriteInfo("SampHooks: addEntryTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(addEntryTarget), static_cast<unsigned>(addEntryTarget - sampBase));
    debuglog::WriteInfo("SampHooks: addMessageTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(addMessageTarget), static_cast<unsigned>(addMessageTarget - sampBase));
    debuglog::WriteInfo("SampHooks: addChatMessageTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(addChatMessageTarget), static_cast<unsigned>(addChatMessageTarget - sampBase));
    debuglog::WriteInfo("SampHooks: dialogShowTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(dialogShowTarget), static_cast<unsigned>(dialogShowTarget - sampBase));
    debuglog::WriteInfo("SampHooks: dialogCloseTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(dialogCloseTarget), static_cast<unsigned>(dialogCloseTarget - sampBase));
    debuglog::WriteInfo("SampHooks: inputSendTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(inputSendTarget), static_cast<unsigned>(inputSendTarget - sampBase));
    debuglog::WriteInfo("SampHooks: inputSendSayTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(inputSendSayTarget), static_cast<unsigned>(inputSendSayTarget - sampBase));
    debuglog::WriteInfo("SampHooks: hotkeyDispatcherTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(hotkeyDispatcherTarget), static_cast<unsigned>(hotkeyDispatcherTarget - sampBase));
    debuglog::WriteInfo("SampHooks: inputHotkeyHandlerTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(inputHotkeyHandlerTarget), static_cast<unsigned>(inputHotkeyHandlerTarget - sampBase));
    debuglog::WriteInfo("SampHooks: damageTarget=0x%08X", static_cast<unsigned>(damageTarget));

    if (addEntryTarget == sampBase || addMessageTarget == sampBase || addChatMessageTarget == sampBase
        || dialogShowTarget == sampBase || dialogCloseTarget == sampBase
        || inputSendTarget == sampBase || inputSendSayTarget == sampBase
        || hotkeyDispatcherTarget == sampBase || inputHotkeyHandlerTarget == sampBase) {
        statusText_ = "some hook offsets are missing for the detected SAMP version";
        debuglog::WriteError("SampHooks install failed: some hook offsets are missing");
        return false;
    }

    chatAddEntryTarget_ = reinterpret_cast<void*>(addEntryTarget);
    chatAddMessageTarget_ = reinterpret_cast<void*>(addMessageTarget);
    chatAddChatMessageTarget_ = reinterpret_cast<void*>(addChatMessageTarget);
    dialogShowTarget_ = reinterpret_cast<void*>(dialogShowTarget);
    dialogCloseTarget_ = reinterpret_cast<void*>(dialogCloseTarget);
    inputSendTarget_ = reinterpret_cast<void*>(inputSendTarget);
    inputSendSayTarget_ = reinterpret_cast<void*>(inputSendSayTarget);
    hotkeyDispatcherTarget_ = reinterpret_cast<void*>(hotkeyDispatcherTarget);
    inputHotkeyHandlerTarget_ = reinterpret_cast<void*>(inputHotkeyHandlerTarget);
    applyDamageTarget_ = reinterpret_cast<void*>(damageTarget);

    const auto failInstall = [this](const char* statusText, const char* logMessage) {
        statusText_ = statusText;
        debuglog::WriteError("%s", logMessage);
        CleanupHooks();
        return false;
    };

    if (!minhook::CreateAndEnableHook(chatAddEntryTarget_, reinterpret_cast<void*>(&ChatAddEntryDetour), &chatAddEntryOriginal_, "SampHooks::AddEntry")) {
        return failInstall("MinHook install failed for AddEntry", "SampHooks: MinHook install failed for AddEntry");
    }

    if (!minhook::CreateAndEnableHook(chatAddMessageTarget_, reinterpret_cast<void*>(&ChatAddMessageDetour), &chatAddMessageOriginal_, "SampHooks::AddMessage")) {
        return failInstall("MinHook install failed for AddMessage", "SampHooks: MinHook install failed for AddMessage");
    }

    if (!minhook::CreateAndEnableHook(chatAddChatMessageTarget_, reinterpret_cast<void*>(&ChatAddChatMessageDetour), &chatAddChatMessageOriginal_, "SampHooks::AddChatMessage")) {
        return failInstall("MinHook install failed for AddChatMessage", "SampHooks: MinHook install failed for AddChatMessage");
    }

    if (!minhook::CreateAndEnableHook(dialogShowTarget_, reinterpret_cast<void*>(&DialogShowDetour), &dialogShowOriginal_, "SampHooks::CDialog_Show")) {
        return failInstall("MinHook install failed for CDialog_Show", "SampHooks: MinHook install failed for CDialog_Show");
    }

    if (!minhook::CreateAndEnableHook(dialogCloseTarget_, reinterpret_cast<void*>(&DialogCloseDetour), &dialogCloseOriginal_, "SampHooks::CDialog_Close")) {
        return failInstall("MinHook install failed for CDialog_Close", "SampHooks: MinHook install failed for CDialog_Close");
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

    installed_ = true;
    statusText_ = "hooks installed";
    debuglog::WriteInfo("SampHooks: installed for SAMP version %s", sampApi_->currentVersionName());
    AppendLog("hooks installed for SAMP %s", sampApi_->currentVersionName());
    return true;
}

void SampHooks::CleanupHooks() {
    debuglog::WriteInfo("SampHooks::CleanupHooks begin");
    minhook::DisableAndRemoveHook(chatAddEntryTarget_, "SampHooks::AddEntry");
    minhook::DisableAndRemoveHook(chatAddMessageTarget_, "SampHooks::AddMessage");
    minhook::DisableAndRemoveHook(chatAddChatMessageTarget_, "SampHooks::AddChatMessage");
    minhook::DisableAndRemoveHook(dialogShowTarget_, "SampHooks::CDialog_Show");
    minhook::DisableAndRemoveHook(dialogCloseTarget_, "SampHooks::CDialog_Close");
    minhook::DisableAndRemoveHook(inputSendTarget_, "SampHooks::CInput_Send");
    minhook::DisableAndRemoveHook(inputSendSayTarget_, "SampHooks::CInput_SendSay");
    minhook::DisableAndRemoveHook(hotkeyDispatcherTarget_, "SampHooks::HotkeyDispatcher");
    minhook::DisableAndRemoveHook(inputHotkeyHandlerTarget_, "SampHooks::InputHotkeyHandler");
    minhook::DisableAndRemoveHook(applyDamageTarget_, "SampHooks::CDamageManager_ApplyDamage");

    chatAddEntryOriginal_ = nullptr;
    chatAddMessageOriginal_ = nullptr;
    chatAddChatMessageOriginal_ = nullptr;
    dialogShowOriginal_ = nullptr;
    dialogCloseOriginal_ = nullptr;
    inputSendOriginal_ = nullptr;
    inputSendSayOriginal_ = nullptr;
    hotkeyDispatcherOriginal_ = nullptr;
    inputHotkeyHandlerOriginal_ = nullptr;
    applyDamageOriginal_ = nullptr;
    debuglog::WriteInfo("SampHooks::CleanupHooks done");
}

void SampHooks::AppendLog(const char* format, ...) {
    char buffer[1024]{};

    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    std::lock_guard lock(logMutex_);
    if (recentLog_.size() >= kMaxLogEntries) {
        recentLog_.erase(recentLog_.begin());
    }
    recentLog_.emplace_back(buffer);
}

std::string SampHooks::Truncate(std::string text, std::size_t maxLength) {
    if (text.size() <= maxLength) {
        return text;
    }

    text.resize(maxLength);
    text += "...";
    return text;
}
