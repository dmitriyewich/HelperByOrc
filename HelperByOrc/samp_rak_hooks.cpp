#include "samp_rak_hooks.h"

#include "debug_log.h"
#include "minhook_utils.h"
#include "samp_api.h"
#include "text_encoding.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kMaxLogEntries = 128;
constexpr std::uint8_t kIdTimestamp = 40;

struct RakOffsets {
    SampApi::VersionedOffset handleRpc;
    SampApi::VersionedOffset stringWriteEncoder;
    SampApi::VersionedOffset stringReadDecoder;
    SampApi::VersionedOffset compressorPtr;
};

const RakOffsets kRakOffsets = {
    { 0x372F0, 0x373D0, 0x3A6A0, 0x3A6A0, 0x3AD90, 0x3ADE0, 0x3ADE0, 0x3A8A0 },
    { 0x506B0, 0x50790, 0x53A60, 0x53A60, 0x54150, 0x541A0, 0x541A0, 0x53C60 },
    { 0x507E0, 0x508C0, 0x53B90, 0x53B90, 0x54280, 0x542D0, 0x542D0, 0x53D90 },
    { 0x10D894, 0x10D894, 0x121914, 0x121914, 0x121A3C, 0x121A3C, 0x121A3C, 0x15FA54 },
};

std::uintptr_t GetVersionedAddress(HMODULE module, const SampApi::VersionedOffset& offset, SampApi::Version version) {
    const std::uint32_t relative = offset.Get(version);
    if (!module || relative == 0) {
        return 0;
    }
    return reinterpret_cast<std::uintptr_t>(module) + relative;
}

} // namespace

bool __fastcall SampRakHooks::IncomingRpcHandlerDetour(void* self, void* edx, unsigned char* data, int length, PlayerID playerId) {
    UNREFERENCED_PARAMETER(edx);

    if (self_ && self_->installed_) {
        return self_->HandleIncomingRpc(self, data, length, playerId);
    }

    return self_ && self_->incomingRpcOriginal_
        ? self_->incomingRpcOriginal_(self, data, length, playerId)
        : false;
}

bool __fastcall SampRakHooks::SendPacketDetour(void* self, void* edx, BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel) {
    UNREFERENCED_PARAMETER(edx);

    if (self_ && self_->installed_ && bitStream) {
        RakNetBitStreamView view(bitStream);
        if (view.GetNumberOfUnreadBits() >= 8) {
            const auto packetId = view.ReadUInt8();
            self_->RecordSendPacket(packetId);
            if (!self_->HandleOutgoingPacket(packetId, view)) {
                return false;
            }
            view.ResetReadPointer();
        }
    }

    return self_ && self_->sendPacketOriginal_
        ? self_->sendPacketOriginal_(self, bitStream, priority, reliability, orderingChannel)
        : false;
}

Packet* __fastcall SampRakHooks::ReceivePacketDetour(void* self, void* edx) {
    UNREFERENCED_PARAMETER(edx);

    if (!self_ || !self_->installed_) {
        return self_ && self_->receivePacketOriginal_
            ? self_->receivePacketOriginal_(self)
            : nullptr;
    }

    if (!self_->receivePacketOriginal_) {
        return nullptr;
    }

    Packet* packet = self_->receivePacketOriginal_(self);
    while (packet && packet->data && packet->bitSize > 0) {
        RakNetBitStreamView view(packet->data, packet->bitSize, false);
        if (view.GetNumberOfUnreadBits() < 8) {
            break;
        }

        const auto packetId = view.ReadUInt8();
        self_->RecordReceivePacket(packetId);
        if (self_->HandleIncomingPacket(packetId, view)) {
            packet->bitSize = static_cast<unsigned int>(view.GetNumberOfBitsUsed());
            packet->length = static_cast<unsigned int>(view.GetNumberOfBytesUsed());
            return packet;
        }

        if (!self_->deallocatePacket_) {
            return nullptr;
        }

        self_->deallocatePacket_(self, packet);
        packet = self_->receivePacketOriginal_(self);
    }

    return packet;
}

bool __fastcall SampRakHooks::SendRpcDetour(void* self, void* edx, int* id, BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel, bool shiftTimestamp) {
    UNREFERENCED_PARAMETER(edx);

    if (self_ && self_->installed_ && id && bitStream) {
        RakNetBitStreamView view(bitStream);
        const auto rpcId = static_cast<std::uint8_t>(*id);
        self_->RecordSendRpc(rpcId);
        if (!self_->HandleOutgoingRpc(rpcId, view)) {
            return false;
        }
        view.ResetReadPointer();
    }

    return self_ && self_->sendRpcOriginal_
        ? self_->sendRpcOriginal_(self, id, bitStream, priority, reliability, orderingChannel, shiftTimestamp)
        : false;
}

void SampRakHooks::SetSampApi(SampApi* sampApi) {
    sampApi_ = sampApi;
    self_ = this;
}

void SampRakHooks::Refresh() {
    if (!sampApi_) {
        statusText_ = "SampApi is not assigned";
        return;
    }

    sampApi_->Refresh();
    if (installed_) {
        return;
    }

    if (!sampApi_->isSAMPInitilizeLua()) {
        statusText_ = sampApi_->lastError();
        return;
    }

    if (!sampApi_->isSupportedVersion()) {
        statusText_ = "SAMP version is not supported by current RakNet offsets";
        return;
    }

    Install();
}

void SampRakHooks::Shutdown() {
    CleanupHooks();
    installed_ = false;
    rakClientInterface_ = 0;
    deallocatePacket_ = nullptr;
    statusText_ = "RakNet hooks disabled";
}

void SampRakHooks::AddOnSendRpcHandler(RawRpcHandler handler) {
    if (handler) {
        onSendRpcHandlers_.push_back(std::move(handler));
    }
}

void SampRakHooks::AddOnSendPacketHandler(RawPacketHandler handler) {
    if (handler) {
        onSendPacketHandlers_.push_back(std::move(handler));
    }
}

void SampRakHooks::AddOnReceiveRpcHandler(RawRpcHandler handler) {
    if (handler) {
        onReceiveRpcHandlers_.push_back(std::move(handler));
    }
}

void SampRakHooks::AddOnReceivePacketHandler(RawPacketHandler handler) {
    if (handler) {
        onReceivePacketHandlers_.push_back(std::move(handler));
    }
}

void SampRakHooks::AddOnServerMessageHandler(ServerMessageHandler handler) {
    if (handler) {
        onServerMessageHandlers_.push_back(std::move(handler));
    }
}

void SampRakHooks::AddOnShowDialogHandler(ShowDialogHandler handler) {
    if (handler) {
        onShowDialogHandlers_.push_back(std::move(handler));
    }
}

void SampRakHooks::AddOnSendCommandHandler(SendCommandHandler handler) {
    if (handler) {
        onSendCommandHandlers_.push_back(std::move(handler));
    }
}

void SampRakHooks::AddOnSendChatHandler(SendChatHandler handler) {
    if (handler) {
        onSendChatHandlers_.push_back(std::move(handler));
    }
}

void SampRakHooks::AddOnSendDialogResponseHandler(SendDialogResponseHandler handler) {
    if (handler) {
        onSendDialogResponseHandlers_.push_back(std::move(handler));
    }
}

bool SampRakHooks::IsInstalled() const {
    return installed_;
}

const std::string& SampRakHooks::statusText() const {
    return statusText_;
}

SampRakHooks::Stats SampRakHooks::stats() const {
    std::lock_guard lock(statsMutex_);
    return stats_;
}

std::vector<std::string> SampRakHooks::GetRecentLog() const {
    std::lock_guard lock(logMutex_);
    return recentLog_;
}

bool SampRakHooks::Install() {
    std::uintptr_t rakClientInterface = 0;
    if (!TryGetRakClientInterface(rakClientInterface)) {
        statusText_ = "RakClientInterface is not available yet";
        return false;
    }

    if (!sampApi_ || !sampApi_->sampModule()) {
        statusText_ = "samp.dll is not available";
        return false;
    }

    const auto version = sampApi_->currentVersion();
    const std::uintptr_t incomingRpcTarget = GetVersionedAddress(sampApi_->sampModule(), kRakOffsets.handleRpc, version);
    if (incomingRpcTarget == 0) {
        statusText_ = "Incoming RPC handler offset is missing";
        return false;
    }

    auto** vtable = *reinterpret_cast<void***>(rakClientInterface);
    if (!vtable) {
        statusText_ = "RakClientInterface vtable is null";
        return false;
    }

    const std::uintptr_t sendPacketTarget = reinterpret_cast<std::uintptr_t>(vtable[6]);
    const std::uintptr_t receivePacketTarget = reinterpret_cast<std::uintptr_t>(vtable[8]);
    const std::uintptr_t sendRpcTarget = reinterpret_cast<std::uintptr_t>(vtable[25]);
    deallocatePacket_ = reinterpret_cast<DeallocatePacketFn>(vtable[9]);

    const std::uintptr_t sampBase = reinterpret_cast<std::uintptr_t>(sampApi_->sampModule());
    debuglog::Write("SampRakHooks: sampBase=0x%08X", static_cast<unsigned>(sampBase));
    debuglog::Write("SampRakHooks: incomingRpcTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(incomingRpcTarget), static_cast<unsigned>(incomingRpcTarget - sampBase));
    debuglog::Write("SampRakHooks: sendPacketTarget=0x%08X", static_cast<unsigned>(sendPacketTarget));
    debuglog::Write("SampRakHooks: receivePacketTarget=0x%08X", static_cast<unsigned>(receivePacketTarget));
    debuglog::Write("SampRakHooks: sendRpcTarget=0x%08X", static_cast<unsigned>(sendRpcTarget));

    incomingRpcTarget_ = reinterpret_cast<void*>(incomingRpcTarget);
    sendPacketTarget_ = reinterpret_cast<void*>(sendPacketTarget);
    receivePacketTarget_ = reinterpret_cast<void*>(receivePacketTarget);
    sendRpcTarget_ = reinterpret_cast<void*>(sendRpcTarget);

    const auto failInstall = [this](const char* statusText, const char* logMessage) {
        statusText_ = statusText;
        debuglog::Write("%s", logMessage);
        CleanupHooks();
        return false;
    };

    if (!minhook::CreateAndEnableHook(incomingRpcTarget_, reinterpret_cast<void*>(&IncomingRpcHandlerDetour), &incomingRpcOriginal_, "SampRakHooks::IncomingRpcHandler")) {
        return failInstall("MinHook install failed for IncomingRpcHandler", "SampRakHooks: MinHook install failed for IncomingRpcHandler");
    }

    if (!minhook::CreateAndEnableHook(sendPacketTarget_, reinterpret_cast<void*>(&SendPacketDetour), &sendPacketOriginal_, "SampRakHooks::SendPacket")) {
        return failInstall("MinHook install failed for RakClientInterface::Send(BitStream)", "SampRakHooks: MinHook install failed for SendPacket");
    }

    if (!minhook::CreateAndEnableHook(receivePacketTarget_, reinterpret_cast<void*>(&ReceivePacketDetour), &receivePacketOriginal_, "SampRakHooks::ReceivePacket")) {
        return failInstall("MinHook install failed for RakClientInterface::Receive", "SampRakHooks: MinHook install failed for ReceivePacket");
    }

    if (!minhook::CreateAndEnableHook(sendRpcTarget_, reinterpret_cast<void*>(&SendRpcDetour), &sendRpcOriginal_, "SampRakHooks::SendRpc")) {
        return failInstall("MinHook install failed for RakClientInterface::RPC(BitStream)", "SampRakHooks: MinHook install failed for SendRpc");
    }

    rakClientInterface_ = rakClientInterface;
    installed_ = true;
    statusText_ = "RakNet hooks installed";

    debuglog::Write("SampRakHooks: installed for SAMP version %s", sampApi_->currentVersionName());
    AppendLog("RakNet hooks installed for SAMP %s", sampApi_->currentVersionName());
    return true;
}

void SampRakHooks::CleanupHooks() {
    minhook::DisableAndRemoveHook(incomingRpcTarget_, "SampRakHooks::IncomingRpcHandler");
    minhook::DisableAndRemoveHook(sendPacketTarget_, "SampRakHooks::SendPacket");
    minhook::DisableAndRemoveHook(receivePacketTarget_, "SampRakHooks::ReceivePacket");
    minhook::DisableAndRemoveHook(sendRpcTarget_, "SampRakHooks::SendRpc");

    incomingRpcOriginal_ = nullptr;
    sendPacketOriginal_ = nullptr;
    receivePacketOriginal_ = nullptr;
    sendRpcOriginal_ = nullptr;
}

bool SampRakHooks::TryGetRakClientInterface(std::uintptr_t& rakClientInterface) const {
    rakClientInterface = 0;

    if (!sampApi_ || !sampApi_->sampModule()) {
        return false;
    }

    const auto version = sampApi_->currentVersion();
    const std::uintptr_t sampBase = reinterpret_cast<std::uintptr_t>(sampApi_->sampModule());
    const std::uint32_t cNetGamePtrAddress = static_cast<std::uint32_t>(
        sampBase + SampApi::main_offsets.SAMP_INFO_OFFSET.Get(version));

    std::uint32_t cNetGame = 0;
    if (!SafeReadUInt32(cNetGamePtrAddress, cNetGame) || cNetGame == 0) {
        return false;
    }

    const std::uint32_t rakClientAddress = cNetGame + SampApi::main_offsets.rakclient_interface.Get(version);
    std::uint32_t rakClient = 0;
    if (!SafeReadUInt32(rakClientAddress, rakClient) || rakClient == 0) {
        return false;
    }

    rakClientInterface = rakClient;
    return true;
}

bool SampRakHooks::HandleOutgoingPacket(std::uint8_t packetId, RakNetBitStreamView& view) const {
    for (const auto& handler : onSendPacketHandlers_) {
        view.ResetReadPointer();
        if (!handler(packetId, view)) {
            return false;
        }
    }
    return true;
}

bool SampRakHooks::HandleIncomingPacket(std::uint8_t packetId, RakNetBitStreamView& view) const {
    for (const auto& handler : onReceivePacketHandlers_) {
        view.ResetReadPointer();
        if (!handler(packetId, view)) {
            return false;
        }
    }
    return true;
}

bool SampRakHooks::HandleOutgoingRpc(std::uint8_t rpcId, RakNetBitStreamView& view) {
    for (const auto& handler : onSendRpcHandlers_) {
        view.ResetReadPointer();
        if (!handler(rpcId, view)) {
            return false;
        }
    }

    view.ResetReadPointer();
    switch (rpcId) {
    case SampRpcIds::ServerCommand:
        return DispatchSendCommand(view);
    case SampRpcIds::Chat:
        return DispatchSendChat(view);
    case SampRpcIds::DialogResponse:
        return DispatchSendDialogResponse(view);
    default:
        return true;
    }
}

bool SampRakHooks::HandleIncomingRpcPayload(std::uint8_t rpcId, RakNetBitStreamView& view) {
    for (const auto& handler : onReceiveRpcHandlers_) {
        view.ResetReadPointer();
        if (!handler(rpcId, view)) {
            return false;
        }
    }

    view.ResetReadPointer();
    switch (rpcId) {
    case SampRpcIds::ClientMessage:
        return DispatchServerMessage(view);
    case SampRpcIds::ShowDialog:
        return DispatchShowDialog(view);
    default:
        return true;
    }
}

bool SampRakHooks::DispatchSendCommand(RakNetBitStreamView& view) {
    std::string textCp1251;
    if (!ReadString32(view, textCp1251)) {
        return true;
    }

    std::string textUtf8 = textencoding::GameToUtf8(textCp1251);
    for (const auto& handler : onSendCommandHandlers_) {
        if (!handler(textUtf8)) {
            return false;
        }
    }

    view.Reset();
    WriteString32(view, textencoding::Utf8ToGame(textUtf8));
    AppendLog("onSendCommand %s", Truncate(textUtf8, 96).c_str());
    return true;
}

bool SampRakHooks::DispatchSendChat(RakNetBitStreamView& view) {
    std::string textCp1251;
    if (!ReadString8(view, textCp1251)) {
        return true;
    }

    std::string textUtf8 = textencoding::GameToUtf8(textCp1251);
    for (const auto& handler : onSendChatHandlers_) {
        if (!handler(textUtf8)) {
            return false;
        }
    }

    view.Reset();
    WriteString8(view, textencoding::Utf8ToGame(textUtf8));
    AppendLog("onSendChat %s", Truncate(textUtf8, 96).c_str());
    return true;
}

bool SampRakHooks::DispatchSendDialogResponse(RakNetBitStreamView& view) {
    const std::uint16_t dialogIdRead = view.ReadUInt16();
    const std::uint8_t buttonRead = view.ReadUInt8();
    const std::uint16_t listboxIdRead = view.ReadUInt16();

    std::string inputCp1251;
    if (!ReadString8(view, inputCp1251)) {
        inputCp1251.clear();
    }

    std::uint16_t dialogId = dialogIdRead;
    std::uint8_t button = buttonRead;
    std::uint16_t listboxId = listboxIdRead;
    std::string inputUtf8 = textencoding::GameToUtf8(inputCp1251);

    for (const auto& handler : onSendDialogResponseHandlers_) {
        if (!handler(dialogId, button, listboxId, inputUtf8)) {
            return false;
        }
    }

    view.Reset();
    view.WriteUInt16(dialogId);
    view.WriteUInt8(button);
    view.WriteUInt16(listboxId);
    WriteString8(view, textencoding::Utf8ToGame(inputUtf8));
    AppendLog(
        "onSendDialogResponse id=%u button=%u list=%u input=%s",
        dialogId,
        button,
        listboxId,
        Truncate(inputUtf8, 96).c_str());
    return true;
}

bool SampRakHooks::DispatchServerMessage(RakNetBitStreamView& view) {
    std::int32_t color = view.ReadInt32();
    std::string textCp1251;
    if (!ReadString32(view, textCp1251)) {
        return true;
    }

    std::string textUtf8 = textencoding::GameToUtf8(textCp1251);
    for (const auto& handler : onServerMessageHandlers_) {
        if (!handler(color, textUtf8)) {
            return false;
        }
    }

    view.Reset();
    view.WriteInt32(color);
    WriteString32(view, textencoding::Utf8ToGame(textUtf8));
    AppendLog("onServerMessage color=%08X text=%s", static_cast<std::uint32_t>(color), Truncate(textUtf8, 96).c_str());
    return true;
}

bool SampRakHooks::DispatchShowDialog(RakNetBitStreamView& view) {
    if (!sampApi_ || !sampApi_->sampModule()) {
        return true;
    }

    const auto version = sampApi_->currentVersion();
    const std::uintptr_t reader = GetVersionedAddress(sampApi_->sampModule(), kRakOffsets.stringReadDecoder, version);
    const std::uintptr_t compressorPtrAddress = GetVersionedAddress(sampApi_->sampModule(), kRakOffsets.compressorPtr, version);

    std::uint32_t compressor = 0;
    if (reader == 0 || compressorPtrAddress == 0 || !SafeReadUInt32(compressorPtrAddress, compressor) || compressor == 0) {
        return true;
    }

    std::uint16_t dialogId = view.ReadUInt16();
    std::uint8_t style = view.ReadUInt8();

    std::string titleCp1251;
    std::string button1Cp1251;
    std::string button2Cp1251;
    std::string textCp1251;
    if (!ReadString8(view, titleCp1251) || !ReadString8(view, button1Cp1251) || !ReadString8(view, button2Cp1251)) {
        return true;
    }
    if (!ReadEncodedString(view, reader, compressor, 4096, textCp1251)) {
        return true;
    }

    std::string titleUtf8 = textencoding::GameToUtf8(titleCp1251);
    std::string button1Utf8 = textencoding::GameToUtf8(button1Cp1251);
    std::string button2Utf8 = textencoding::GameToUtf8(button2Cp1251);
    std::string textUtf8 = textencoding::GameToUtf8(textCp1251);

    for (const auto& handler : onShowDialogHandlers_) {
        if (!handler(dialogId, style, titleUtf8, button1Utf8, button2Utf8, textUtf8)) {
            return false;
        }
    }

    const std::uintptr_t writer = GetVersionedAddress(sampApi_->sampModule(), kRakOffsets.stringWriteEncoder, version);
    if (writer == 0) {
        return true;
    }

    view.Reset();
    view.WriteUInt16(dialogId);
    view.WriteUInt8(style);
    WriteString8(view, textencoding::Utf8ToGame(titleUtf8));
    WriteString8(view, textencoding::Utf8ToGame(button1Utf8));
    WriteString8(view, textencoding::Utf8ToGame(button2Utf8));
    if (!WriteEncodedString(view, writer, compressor, textencoding::Utf8ToGame(textUtf8))) {
        return true;
    }

    AppendLog(
        "onShowDialog id=%u style=%u title=%s text=%s",
        dialogId,
        style,
        Truncate(titleUtf8, 48).c_str(),
        Truncate(textUtf8, 96).c_str());
    return true;
}

void SampRakHooks::AppendLog(const char* format, ...) {
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

void SampRakHooks::RecordSendRpc(std::uint8_t id) {
    std::lock_guard lock(statsMutex_);
    ++stats_.onSendRpcCount;
    stats_.lastSendRpcId = id;
}

void SampRakHooks::RecordSendPacket(std::uint8_t id) {
    std::lock_guard lock(statsMutex_);
    ++stats_.onSendPacketCount;
    stats_.lastSendPacketId = id;
}

void SampRakHooks::RecordReceiveRpc(std::uint8_t id) {
    std::lock_guard lock(statsMutex_);
    ++stats_.onReceiveRpcCount;
    stats_.lastReceiveRpcId = id;
}

void SampRakHooks::RecordReceivePacket(std::uint8_t id) {
    std::lock_guard lock(statsMutex_);
    ++stats_.onReceivePacketCount;
    stats_.lastReceivePacketId = id;
}

std::string SampRakHooks::Truncate(std::string text, std::size_t maxLength) {
    if (text.size() <= maxLength) {
        return text;
    }

    text.resize(maxLength);
    text += "...";
    return text;
}

bool SampRakHooks::SafeReadUInt32(std::uintptr_t address, std::uint32_t& value) {
    __try {
        value = *reinterpret_cast<const std::uint32_t*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        value = 0;
        return false;
    }
}

bool SampRakHooks::ReadString8(RakNetBitStreamView& view, std::string& value) {
    const auto length = view.ReadUInt8();
    value = length > 0 ? view.ReadString(length) : std::string();
    return true;
}

bool SampRakHooks::ReadString32(RakNetBitStreamView& view, std::string& value) {
    const auto length = view.ReadInt32();
    if (length < 0 || length > 1 << 20) {
        value.clear();
        return false;
    }

    value = length > 0 ? view.ReadString(length) : std::string();
    return true;
}

bool SampRakHooks::ReadEncodedString(
    RakNetBitStreamView& view,
    std::uintptr_t reader,
    std::uintptr_t compressor,
    int maxLength,
    std::string& value) {
    if (!reader || !compressor || maxLength <= 0) {
        value.clear();
        return false;
    }

    std::string buffer(static_cast<std::size_t>(maxLength), '\0');
    const auto decode = reinterpret_cast<bool(__thiscall*)(std::uintptr_t, char*, int, BitStream*, int)>(reader);
    if (!decode(compressor, buffer.data(), maxLength, view.raw(), 0)) {
        value.clear();
        return false;
    }

    buffer.resize(std::strlen(buffer.c_str()));
    value = std::move(buffer);
    return true;
}

void SampRakHooks::WriteString8(RakNetBitStreamView& view, std::string_view value) {
    const std::size_t clampedSize = std::min<std::size_t>(value.size(), 0xFF);
    view.WriteUInt8(static_cast<std::uint8_t>(clampedSize));
    view.WriteString(value.substr(0, clampedSize));
}

void SampRakHooks::WriteString32(RakNetBitStreamView& view, std::string_view value) {
    view.WriteInt32(static_cast<std::int32_t>(value.size()));
    view.WriteString(value);
}

bool SampRakHooks::WriteEncodedString(
    RakNetBitStreamView& view,
    std::uintptr_t writer,
    std::uintptr_t compressor,
    std::string_view value) {
    if (!writer || !compressor) {
        return false;
    }

    view.raw()->AddBitsAndReallocate(static_cast<int>(value.size() * 16 + 16));
    const auto encode = reinterpret_cast<void(__thiscall*)(std::uintptr_t, const char*, int, BitStream*, int)>(writer);
    encode(compressor, value.data(), static_cast<int>(value.size()) + 1, view.raw(), 0);
    return true;
}

bool SampRakHooks::HandleIncomingRpc(void* self, unsigned char* data, int length, PlayerID playerId) {
    if (!incomingRpcOriginal_) {
        return false;
    }

    if (!data || length <= 0) {
        return incomingRpcOriginal_(self, data, length, playerId);
    }

    BitStream wrapper(data, length, true);
    wrapper.IgnoreBits(8);
    if (data[0] == kIdTimestamp) {
        wrapper.IgnoreBits(8 * (sizeof(RakNetTime) + sizeof(unsigned char)));
    }

    const int rpcHeaderOffset = wrapper.GetReadOffset();
    std::uint8_t rpcId = 0;
    wrapper.Read(rpcId);

    std::uint32_t bitsData = 0;
    if (!wrapper.ReadCompressed(bitsData)) {
        return false;
    }

    std::vector<unsigned char> payloadBytes(static_cast<std::size_t>(BITS_TO_BYTES(bitsData)));
    if (bitsData > 0 && !wrapper.ReadBits(payloadBytes.data(), static_cast<int>(bitsData), false)) {
        return false;
    }

    BitStream payloadBitStream(payloadBytes.empty() ? nullptr : payloadBytes.data(), static_cast<unsigned int>(payloadBytes.size()), true);
    RakNetBitStreamView payloadView(&payloadBitStream);

    RecordReceiveRpc(rpcId);
    if (!HandleIncomingRpcPayload(rpcId, payloadView)) {
        return false;
    }

    wrapper.SetWriteOffset(rpcHeaderOffset);
    wrapper.Write(rpcId);

    bitsData = static_cast<std::uint32_t>(payloadBitStream.GetNumberOfBitsUsed());
    wrapper.WriteCompressed(bitsData);
    if (bitsData > 0) {
        wrapper.WriteBits(payloadBitStream.GetData(), static_cast<int>(bitsData));
    }

    return incomingRpcOriginal_(self, wrapper.GetData(), wrapper.GetNumberOfBytesUsed(), playerId);
}
