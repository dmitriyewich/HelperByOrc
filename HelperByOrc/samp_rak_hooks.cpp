#include "samp_rak_hooks.h"

#include "debug_log.h"
#include "minhook_utils.h"
#include "samp_api.h"
#include "text_encoding.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kMaxLogEntries = 128;
constexpr std::size_t kMaxQueuedSyntheticIncomingPackets = 16;
constexpr int kDialogTextBufferBytes = 4096;
constexpr std::size_t kMaxEncodedStringBytes =
    static_cast<std::size_t>(kDialogTextBufferBytes - 1);
constexpr std::uint8_t kIdTimestamp = 40;

std::uintptr_t GetVersionedAddress(HMODULE module, const SampApi::VersionedOffset& offset, SampApi::Version version) {
    const std::uint32_t relative = offset.Get(version);
    if (!module || relative == 0) {
        return 0;
    }
    return reinterpret_cast<std::uintptr_t>(module) + relative;
}

bool IsExecutableAddress(std::uintptr_t address, HMODULE expectedModule) {
    MEMORY_BASIC_INFORMATION region{};
    if (address == 0
        || !expectedModule
        || VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) != sizeof(region)
        || region.State != MEM_COMMIT
        || region.AllocationBase != expectedModule
        || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }

    const DWORD protection = region.Protect & 0xFFu;
    return protection == PAGE_EXECUTE
        || protection == PAGE_EXECUTE_READ
        || protection == PAGE_EXECUTE_READWRITE
        || protection == PAGE_EXECUTE_WRITECOPY;
}

bool PayloadMatchesOriginal(
    const std::vector<unsigned char>& originalBytes,
    std::uint32_t originalBitCount,
    const BitStream& payload) {
    if (originalBitCount > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
        || payload.GetNumberOfBitsUsed() != static_cast<int>(originalBitCount)) {
        return false;
    }

    const std::size_t byteCount = static_cast<std::size_t>(BITS_TO_BYTES(originalBitCount));
    return byteCount == 0
        || (originalBytes.size() >= byteCount
            && payload.GetData() != nullptr
            && std::memcmp(originalBytes.data(), payload.GetData(), byteCount) == 0);
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

    Packet* packet = self_->PopQueuedIncomingPacket();
    if (!packet) {
        packet = self_->receivePacketOriginal_(self);
    }
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

        self_->DeallocatePacketInternal(self, packet);
        packet = self_->PopQueuedIncomingPacket();
        if (!packet) {
            packet = self_->receivePacketOriginal_(self);
        }
    }

    return packet;
}

void __fastcall SampRakHooks::DeallocatePacketDetour(void* self, void* edx, Packet* packet) {
    UNREFERENCED_PARAMETER(edx);

    if (self_ && self_->FreeSyntheticPacket(packet)) {
        return;
    }

    if (self_ && self_->deallocatePacketDetourOriginal_) {
        self_->deallocatePacketDetourOriginal_(self, packet);
    }
}

bool __fastcall SampRakHooks::SendRpcDetour(void* self, void* edx, int* id, BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel, bool shiftTimestamp) {
    UNREFERENCED_PARAMETER(edx);

    const void* returnAddress = _ReturnAddress();
    if (self_ && self_->installed_ && id && bitStream) {
        const SampCallContext context = ResolveSampCallContext(
            returnAddress,
            self_->sampApi_ ? self_->sampApi_->sampModule() : nullptr,
            self_->ownerModule_);
        RakNetBitStreamView view(bitStream);
        const auto rpcId = static_cast<std::uint8_t>(*id);
        self_->RecordSendRpc(rpcId);
        if (!self_->HandleOutgoingRpc(rpcId, view, context)) {
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
    debuglog::WriteInfo("SampRakHooks::SetSampApi assigned=%d", sampApi_ ? 1 : 0);
}

void SampRakHooks::SetOwnerModule(HMODULE module) {
    ownerModule_ = module;
    debuglog::WriteInfo("SampRakHooks::SetOwnerModule assigned=%d", ownerModule_ ? 1 : 0);
}

void SampRakHooks::Refresh() {
    if (!sampApi_) {
        statusText_ = "SampApi is not assigned";
        debuglog::WriteError("SampRakHooks::Refresh skipped: SampApi is not assigned");
        return;
    }

    sampApi_->Refresh();
    if (installed_) {
        return;
    }

    if (!sampApi_->isSAMPInitilizeLua()) {
        statusText_ = sampApi_->lastError();
        debuglog::WriteInfo("SampRakHooks::Refresh waiting for SA:MP: %s", statusText_.c_str());
        return;
    }

    if (!sampApi_->isSupportedVersion()) {
        statusText_ = "SAMP version is not supported by current RakNet offsets";
        debuglog::WriteError("SampRakHooks::Refresh unsupported SAMP version: %s", sampApi_->currentVersionName());
        return;
    }

    debuglog::WriteInfo("SampRakHooks::Refresh install requested for SAMP %s", sampApi_->currentVersionName());
    Install();
}

void SampRakHooks::Shutdown() {
    debuglog::WriteInfo("SampRakHooks::Shutdown begin");
    CleanupHooks();
    installed_ = false;
    rakClientInterface_ = 0;
    deallocatePacketOriginal_ = nullptr;
    {
        std::lock_guard lock(syntheticPacketsMutex_);
        queuedIncomingPackets_.clear();
        for (Packet* packet : syntheticPackets_) {
            if (packet) {
                delete[] packet->data;
                delete packet;
            }
        }
        syntheticPackets_.clear();
    }
    statusText_ = "RakNet hooks disabled";
    debuglog::WriteInfo("SampRakHooks::Shutdown done");
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

void SampRakHooks::AddServerMessageFilter(ServerMessageFilter handler) {
    if (handler) {
        serverMessageFilters_.push_back(std::move(handler));
    }
}

void SampRakHooks::AddPlayerChatFilter(PlayerChatFilter handler) {
    if (handler) {
        playerChatFilters_.push_back(std::move(handler));
    }
}

void SampRakHooks::AddChatBubbleFilter(ChatBubbleFilter handler) {
    if (handler) {
        chatBubbleFilters_.push_back(std::move(handler));
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

bool SampRakHooks::EmulateIncomingPacket(std::uint8_t packetId, BitStream& payload) {
    if (!installed_) {
        debuglog::WriteError("SampRakHooks::EmulateIncomingPacket skipped: hooks are not installed");
        return false;
    }

    const int payloadBytesUsed = payload.GetNumberOfBytesUsed();
    if (payloadBytesUsed < 0) {
        return false;
    }

    std::vector<unsigned char> bytes;
    bytes.reserve(static_cast<std::size_t>(payloadBytesUsed) + 1);
    bytes.push_back(packetId);
    if (payloadBytesUsed > 0 && payload.GetData()) {
        const unsigned char* const data = payload.GetData();
        bytes.insert(bytes.end(), data, data + payloadBytesUsed);
    }

    {
        std::lock_guard lock(syntheticPacketsMutex_);
        if (queuedIncomingPackets_.size() >= kMaxQueuedSyntheticIncomingPackets) {
            debuglog::WriteError(
                "SampRakHooks::EmulateIncomingPacket rejected packet=%u: synthetic queue is full (%llu)",
                packetId,
                static_cast<unsigned long long>(queuedIncomingPackets_.size()));
            return false;
        }
        queuedIncomingPackets_.push_back(std::move(bytes));
    }

    debuglog::WriteInfo("SampRakHooks::EmulateIncomingPacket queued packet=%u payloadBytes=%d", packetId, payloadBytesUsed);
    return true;
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
    if (!sampApi_ || !sampApi_->sampModule() || !sampApi_->isSupportedVersion()) {
        statusText_ = "exact SAMP variant is not approved for RakNet hooks";
        debuglog::WriteError("SampRakHooks install failed: exact SAMP variant is not approved");
        return false;
    }

    std::uintptr_t rakClientInterface = 0;
    if (!TryGetRakClientInterface(rakClientInterface)) {
        statusText_ = "RakClientInterface is not available yet";
        debuglog::WriteInfo("SampRakHooks install delayed: RakClientInterface is not available yet");
        return false;
    }

    if (!sampApi_ || !sampApi_->sampModule()) {
        statusText_ = "samp.dll is not available";
        debuglog::WriteError("SampRakHooks install failed: samp.dll is not available");
        return false;
    }

    const auto version = sampApi_->currentVersion();
    const std::uintptr_t incomingRpcTarget = GetVersionedAddress(sampApi_->sampModule(), SampApi::main_offsets.RakHandleRpc, version);
    if (incomingRpcTarget == 0) {
        statusText_ = "Incoming RPC handler offset is missing";
        debuglog::WriteError("SampRakHooks install failed: incoming RPC handler offset is missing");
        return false;
    }

    std::uint32_t vtable = 0;
    if (!SafeReadUInt32(rakClientInterface, vtable) || vtable == 0) {
        statusText_ = "RakClientInterface vtable is unavailable";
        debuglog::WriteError("SampRakHooks install failed: RakClientInterface vtable is unavailable");
        return false;
    }

    std::uint32_t sendPacketTarget = 0;
    std::uint32_t receivePacketTarget = 0;
    std::uint32_t deallocatePacketTarget = 0;
    std::uint32_t sendRpcTarget = 0;
    if (!SafeReadUInt32(vtable + 6 * sizeof(std::uint32_t), sendPacketTarget)
        || !SafeReadUInt32(vtable + 8 * sizeof(std::uint32_t), receivePacketTarget)
        || !SafeReadUInt32(vtable + 9 * sizeof(std::uint32_t), deallocatePacketTarget)
        || !SafeReadUInt32(vtable + 25 * sizeof(std::uint32_t), sendRpcTarget)) {
        statusText_ = "RakClientInterface hook slots are unreadable";
        debuglog::WriteError("SampRakHooks install failed: RakClientInterface hook slots are unreadable");
        return false;
    }

    const std::array<std::pair<const char*, std::uintptr_t>, 5> targets{ {
        { "IncomingRpcHandler", incomingRpcTarget },
        { "RakClientInterface::Send", sendPacketTarget },
        { "RakClientInterface::Receive", receivePacketTarget },
        { "RakClientInterface::DeallocatePacket", deallocatePacketTarget },
        { "RakClientInterface::RPC", sendRpcTarget },
    } };
    for (const auto& [name, target] : targets) {
        if (!IsExecutableAddress(target, sampApi_->sampModule())) {
            statusText_ = std::string("RakNet hook target is not executable: ") + name;
            debuglog::WriteError(
                "SampRakHooks install failed: %s target=0x%08X is outside executable samp.dll memory",
                name,
                static_cast<unsigned>(target));
            return false;
        }
    }
    deallocatePacketOriginal_ = reinterpret_cast<DeallocatePacketFn>(deallocatePacketTarget);

    const std::uintptr_t sampBase = reinterpret_cast<std::uintptr_t>(sampApi_->sampModule());
    debuglog::WriteInfo("SampRakHooks: sampBase=0x%08X", static_cast<unsigned>(sampBase));
    debuglog::WriteInfo("SampRakHooks: incomingRpcTarget=0x%08X (offset 0x%X)", static_cast<unsigned>(incomingRpcTarget), static_cast<unsigned>(incomingRpcTarget - sampBase));
    debuglog::WriteInfo("SampRakHooks: sendPacketTarget=0x%08X", static_cast<unsigned>(sendPacketTarget));
    debuglog::WriteInfo("SampRakHooks: receivePacketTarget=0x%08X", static_cast<unsigned>(receivePacketTarget));
    debuglog::WriteInfo("SampRakHooks: deallocatePacketTarget=0x%08X", static_cast<unsigned>(deallocatePacketTarget));
    debuglog::WriteInfo("SampRakHooks: sendRpcTarget=0x%08X", static_cast<unsigned>(sendRpcTarget));

    incomingRpcTarget_ = reinterpret_cast<void*>(incomingRpcTarget);
    sendPacketTarget_ = reinterpret_cast<void*>(sendPacketTarget);
    receivePacketTarget_ = reinterpret_cast<void*>(receivePacketTarget);
    deallocatePacketTarget_ = reinterpret_cast<void*>(deallocatePacketTarget);
    sendRpcTarget_ = reinterpret_cast<void*>(sendRpcTarget);

    const auto failInstall = [this](const char* statusText, const char* logMessage) {
        statusText_ = statusText;
        debuglog::WriteError("%s", logMessage);
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

    if (!minhook::CreateAndEnableHook(deallocatePacketTarget_, reinterpret_cast<void*>(&DeallocatePacketDetour), &deallocatePacketDetourOriginal_, "SampRakHooks::DeallocatePacket")) {
        return failInstall("MinHook install failed for RakClientInterface::DeallocatePacket", "SampRakHooks: MinHook install failed for DeallocatePacket");
    }

    if (!minhook::CreateAndEnableHook(sendRpcTarget_, reinterpret_cast<void*>(&SendRpcDetour), &sendRpcOriginal_, "SampRakHooks::SendRpc")) {
        return failInstall("MinHook install failed for RakClientInterface::RPC(BitStream)", "SampRakHooks: MinHook install failed for SendRpc");
    }

    rakClientInterface_ = rakClientInterface;
    installed_ = true;
    statusText_ = "RakNet hooks installed";

    debuglog::WriteInfo("SampRakHooks: installed for SAMP version %s", sampApi_->currentVersionName());
    AppendLog("RakNet hooks installed for SAMP %s", sampApi_->currentVersionName());
    return true;
}

void SampRakHooks::CleanupHooks() {
    debuglog::WriteInfo("SampRakHooks::CleanupHooks begin");
    minhook::DisableAndRemoveHook(incomingRpcTarget_, "SampRakHooks::IncomingRpcHandler");
    minhook::DisableAndRemoveHook(sendPacketTarget_, "SampRakHooks::SendPacket");
    minhook::DisableAndRemoveHook(receivePacketTarget_, "SampRakHooks::ReceivePacket");
    minhook::DisableAndRemoveHook(deallocatePacketTarget_, "SampRakHooks::DeallocatePacket");
    minhook::DisableAndRemoveHook(sendRpcTarget_, "SampRakHooks::SendRpc");

    incomingRpcOriginal_ = nullptr;
    sendPacketOriginal_ = nullptr;
    receivePacketOriginal_ = nullptr;
    deallocatePacketDetourOriginal_ = nullptr;
    sendRpcOriginal_ = nullptr;
    {
        std::lock_guard lock(syntheticPacketsMutex_);
        queuedIncomingPackets_.clear();
    }
    deallocatePacketTarget_ = nullptr;
    debuglog::WriteInfo("SampRakHooks::CleanupHooks done");
}

Packet* SampRakHooks::PopQueuedIncomingPacket() {
    std::vector<unsigned char> bytes;
    {
        std::lock_guard lock(syntheticPacketsMutex_);
        if (queuedIncomingPackets_.empty()) {
            return nullptr;
        }
        bytes = std::move(queuedIncomingPackets_.front());
        queuedIncomingPackets_.pop_front();
    }

    if (bytes.empty()) {
        return nullptr;
    }

    auto* data = new (std::nothrow) unsigned char[bytes.size()];
    if (!data) {
        debuglog::WriteError("SampRakHooks::PopQueuedIncomingPacket failed: data allocation failed");
        return nullptr;
    }

    auto* packet = new (std::nothrow) Packet;
    if (!packet) {
        delete[] data;
        debuglog::WriteError("SampRakHooks::PopQueuedIncomingPacket failed: packet allocation failed");
        return nullptr;
    }

    std::memcpy(data, bytes.data(), bytes.size());
    packet->playerIndex = static_cast<PlayerIndex>(-1);
    packet->playerId = {};
    packet->length = static_cast<unsigned int>(bytes.size());
    packet->bitSize = static_cast<unsigned int>(BYTES_TO_BITS(bytes.size()));
    packet->data = data;
    packet->deleteData = true;

    {
        std::lock_guard lock(syntheticPacketsMutex_);
        syntheticPackets_.insert(packet);
    }

    return packet;
}

bool SampRakHooks::FreeSyntheticPacket(Packet* packet) {
    if (!packet) {
        return false;
    }

    {
        std::lock_guard lock(syntheticPacketsMutex_);
        const auto it = syntheticPackets_.find(packet);
        if (it == syntheticPackets_.end()) {
            return false;
        }
        syntheticPackets_.erase(it);
    }

    delete[] packet->data;
    delete packet;
    return true;
}

void SampRakHooks::DeallocatePacketInternal(void* self, Packet* packet) {
    if (FreeSyntheticPacket(packet)) {
        return;
    }

    if (deallocatePacketDetourOriginal_) {
        deallocatePacketDetourOriginal_(self, packet);
        return;
    }

    if (deallocatePacketOriginal_) {
        deallocatePacketOriginal_(self, packet);
    }
}

bool SampRakHooks::TryGetRakClientInterface(std::uintptr_t& rakClientInterface) const {
    rakClientInterface = 0;

    if (!sampApi_ || !sampApi_->sampModule() || !sampApi_->isSupportedVersion()) {
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

bool SampRakHooks::HandleOutgoingRpc(
    std::uint8_t rpcId,
    RakNetBitStreamView& view,
    const SampCallContext& context) {
    for (const auto& handler : onSendRpcHandlers_) {
        view.ResetReadPointer();
        if (!handler(rpcId, view)) {
            return false;
        }
    }

    view.ResetReadPointer();
    switch (rpcId) {
    case SampRpcIds::ServerCommand:
        return DispatchSendCommand(view, context);
    case SampRpcIds::Chat:
        return DispatchSendChat(view, context);
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
    case SampRpcIds::ChatBubble:
        return DispatchChatBubble(view);
    case SampRpcIds::ClientMessage:
        return DispatchServerMessage(view);
    case SampRpcIds::Chat:
        return DispatchPlayerChat(view);
    case SampRpcIds::ShowDialog:
        return DispatchShowDialog(view);
    default:
        return true;
    }
}

bool SampRakHooks::DispatchSendCommand(RakNetBitStreamView& view, const SampCallContext& context) {
    std::string textCp1251;
    if (!ReadString32(view, textCp1251)) {
        return true;
    }

    std::string textUtf8 = textencoding::GameToUtf8(textCp1251);
    for (const auto& handler : onSendCommandHandlers_) {
        if (!handler(textUtf8, context)) {
            return false;
        }
    }

    view.Reset();
    WriteString32(view, textencoding::Utf8ToGame(textUtf8));
    AppendLog("onSendCommand %s", Truncate(textUtf8, 96).c_str());
    return true;
}

bool SampRakHooks::DispatchSendChat(RakNetBitStreamView& view, const SampCallContext& context) {
    std::string textCp1251;
    if (!ReadString8(view, textCp1251)) {
        return true;
    }

    std::string textUtf8 = textencoding::GameToUtf8(textCp1251);
    for (const auto& handler : onSendChatHandlers_) {
        if (!handler(textUtf8, context)) {
            return false;
        }
    }

    view.Reset();
    WriteString8(view, textencoding::Utf8ToGame(textUtf8));
    AppendLog("onSendChat %s", Truncate(textUtf8, 96).c_str());
    return true;
}

bool SampRakHooks::DispatchSendDialogResponse(RakNetBitStreamView& view) {
    constexpr int kFixedPayloadBits = 16 + 8 + 16;
    if (view.GetNumberOfUnreadBits() < kFixedPayloadBits) {
        return true;
    }

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
    if (view.GetNumberOfUnreadBits() < 32) {
        return true;
    }

    std::int32_t color = view.ReadInt32();
    std::string textCp1251;
    if (!ReadString32(view, textCp1251)) {
        return true;
    }

    std::string textUtf8 = textencoding::GameToUtf8(textCp1251);
    for (const auto& filter : serverMessageFilters_) {
        if (!filter(color, textUtf8)) {
            return false;
        }
    }

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

bool SampRakHooks::DispatchPlayerChat(RakNetBitStreamView& view) {
    if (view.GetNumberOfUnreadBits() < 16) {
        return true;
    }

    const std::uint16_t playerId = view.ReadUInt16();
    std::string textCp1251;
    if (!ReadString8(view, textCp1251)) {
        return true;
    }

    const std::string textUtf8 = textencoding::GameToUtf8(textCp1251);
    const std::string playerName = sampApi_ ? sampApi_->GetNameID(playerId) : std::string("UNKNOWN");
    for (const auto& filter : playerChatFilters_) {
        if (!filter(playerId, playerName, textUtf8)) {
            return false;
        }
    }

    AppendLog("onPlayerChat id=%u name=%s text=%s", playerId, Truncate(playerName, 48).c_str(), Truncate(textUtf8, 96).c_str());
    return true;
}

bool SampRakHooks::DispatchChatBubble(RakNetBitStreamView& view) {
    constexpr int kFixedPayloadBits = 16 + 32 + 32 + 32;
    if (view.GetNumberOfUnreadBits() < kFixedPayloadBits) {
        return true;
    }

    const std::uint16_t playerId = view.ReadUInt16();
    const std::uint32_t color = view.ReadUInt32();
    const float drawDistance = view.ReadFloat();
    const std::uint32_t durationMs = view.ReadUInt32();

    std::string textCp1251;
    if (!ReadString8(view, textCp1251)) {
        return true;
    }

    const std::string textUtf8 = textencoding::GameToUtf8(textCp1251);
    const std::string playerName = sampApi_ ? sampApi_->GetNameID(playerId) : std::string("UNKNOWN");
    for (const auto& filter : chatBubbleFilters_) {
        if (!filter(playerId, playerName, color, drawDistance, durationMs, textUtf8)) {
            return false;
        }
    }

    AppendLog(
        "onChatBubble id=%u name=%s color=%08X text=%s",
        playerId,
        Truncate(playerName, 48).c_str(),
        color,
        Truncate(textUtf8, 96).c_str());
    return true;
}

bool SampRakHooks::DispatchShowDialog(RakNetBitStreamView& view) {
    if (onShowDialogHandlers_.empty()) {
        return true;
    }

    if (!sampApi_ || !sampApi_->sampModule()) {
        return true;
    }

    const auto version = sampApi_->currentVersion();
    const std::uintptr_t reader = GetVersionedAddress(sampApi_->sampModule(), SampApi::main_offsets.RakStringReadDecoder, version);
    const std::uintptr_t compressorPtrAddress = GetVersionedAddress(sampApi_->sampModule(), SampApi::main_offsets.RakCompressorPtr, version);

    std::uint32_t compressor = 0;
    if (reader == 0 || compressorPtrAddress == 0 || !SafeReadUInt32(compressorPtrAddress, compressor) || compressor == 0) {
        return true;
    }

    if (view.GetNumberOfUnreadBits() < 24) {
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
    if (!ReadEncodedString(view, reader, compressor, kDialogTextBufferBytes, textCp1251)) {
        return true;
    }

    std::string titleUtf8 = textencoding::GameToUtf8(titleCp1251);
    std::string button1Utf8 = textencoding::GameToUtf8(button1Cp1251);
    std::string button2Utf8 = textencoding::GameToUtf8(button2Cp1251);
    std::string textUtf8 = textencoding::GameToUtf8(textCp1251);
    const std::uint16_t originalDialogId = dialogId;
    const std::uint8_t originalStyle = style;
    const std::string originalTitleUtf8 = titleUtf8;
    const std::string originalButton1Utf8 = button1Utf8;
    const std::string originalButton2Utf8 = button2Utf8;
    const std::string originalTextUtf8 = textUtf8;

    for (const auto& handler : onShowDialogHandlers_) {
        if (!handler(dialogId, style, titleUtf8, button1Utf8, button2Utf8, textUtf8)) {
            return false;
        }
    }

    const bool changed = dialogId != originalDialogId
        || style != originalStyle
        || titleUtf8 != originalTitleUtf8
        || button1Utf8 != originalButton1Utf8
        || button2Utf8 != originalButton2Utf8
        || textUtf8 != originalTextUtf8;
    if (!changed) {
        return true;
    }

    const std::uintptr_t writer = GetVersionedAddress(sampApi_->sampModule(), SampApi::main_offsets.RakStringWriteEncoder, version);
    if (writer == 0) {
        debuglog::WriteError("SampRakHooks::DispatchShowDialog changed payload but RakStringWriteEncoder is unavailable");
        return true;
    }

    const std::string titleGame = textencoding::Utf8ToGame(titleUtf8);
    const std::string button1Game = textencoding::Utf8ToGame(button1Utf8);
    const std::string button2Game = textencoding::Utf8ToGame(button2Utf8);
    const std::string textGame = textencoding::Utf8ToGame(textUtf8);
    const bool textHasNul = textGame.find('\0') != std::string::npos;
    if (textGame.size() > kMaxEncodedStringBytes || textHasNul) {
        debuglog::WriteError(
            "SampRakHooks::DispatchShowDialog changed payload but encoded text is invalid size=%llu hasNul=%d",
            static_cast<unsigned long long>(textGame.size()),
            textHasNul ? 1 : 0);
        return true;
    }

    view.Reset();
    view.WriteUInt16(dialogId);
    view.WriteUInt8(style);
    WriteString8(view, titleGame);
    WriteString8(view, button1Game);
    WriteString8(view, button2Game);
    if (!WriteEncodedString(view, writer, compressor, textGame)) {
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
    if (view.GetNumberOfUnreadBits() < 8) {
        value.clear();
        return false;
    }

    const auto length = view.ReadUInt8();
    if (view.GetNumberOfUnreadBits() < static_cast<int>(length) * 8) {
        value.clear();
        return false;
    }

    value = length > 0 ? view.ReadString(length) : std::string{};
    return true;
}

bool SampRakHooks::ReadString32(RakNetBitStreamView& view, std::string& value) {
    if (view.GetNumberOfUnreadBits() < 32) {
        value.clear();
        return false;
    }

    const auto length = view.ReadInt32();
    if (length < 0 || length > 1 << 20) {
        value.clear();
        return false;
    }

    if (view.GetNumberOfUnreadBits() < length * 8) {
        value.clear();
        return false;
    }

    value = length > 0 ? view.ReadString(length) : std::string{};
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
    if (!writer
        || !compressor
        || value.size() > kMaxEncodedStringBytes
        || value.find('\0') != std::string_view::npos) {
        return false;
    }

    // StringCompressor writes up to 16 Huffman bits per byte plus up to
    // 17 bits for the compressed uint16_t payload length.
    const int reserveBits = static_cast<int>(value.size() * 16 + 17);
    const int usedBits = view.GetNumberOfBitsUsed();
    if (usedBits < 0
        || usedBits > std::numeric_limits<int>::max() / 2 - reserveBits) {
        return false;
    }

    std::string nullTerminated(value);
    view.raw()->AddBitsAndReallocate(reserveBits);
    const auto encode = reinterpret_cast<void(__thiscall*)(std::uintptr_t, const char*, int, BitStream*, int)>(writer);
    encode(compressor, nullTerminated.c_str(), static_cast<int>(nullTerminated.size() + 1), view.raw(), 0);
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
        constexpr int kTimestampHeaderBits = 8 * (sizeof(RakNetTime) + sizeof(unsigned char));
        if (wrapper.GetNumberOfUnreadBits() < kTimestampHeaderBits) {
            return incomingRpcOriginal_(self, data, length, playerId);
        }
        wrapper.IgnoreBits(kTimestampHeaderBits);
    }

    if (wrapper.GetNumberOfUnreadBits() < 8) {
        return incomingRpcOriginal_(self, data, length, playerId);
    }

    const int rpcHeaderOffset = wrapper.GetReadOffset();
    std::uint8_t rpcId = 0;
    if (!wrapper.Read(rpcId)) {
        return incomingRpcOriginal_(self, data, length, playerId);
    }

    std::uint32_t bitsData = 0;
    if (!wrapper.ReadCompressed(bitsData)) {
        return incomingRpcOriginal_(self, data, length, playerId);
    }
    if (bitsData > static_cast<std::uint32_t>(wrapper.GetNumberOfUnreadBits())) {
        return incomingRpcOriginal_(self, data, length, playerId);
    }

    std::vector<unsigned char> payloadBytes(static_cast<std::size_t>(BITS_TO_BYTES(bitsData)));
    if (bitsData > 0 && !wrapper.ReadBits(payloadBytes.data(), static_cast<int>(bitsData), false)) {
        return false;
    }

    BitStream payloadBitStream(payloadBytes.empty() ? nullptr : payloadBytes.data(), static_cast<unsigned int>(payloadBytes.size()), true);
    payloadBitStream.SetWriteOffset(static_cast<int>(bitsData));
    RakNetBitStreamView payloadView(&payloadBitStream);

    RecordReceiveRpc(rpcId);
    if (!HandleIncomingRpcPayload(rpcId, payloadView)) {
        return false;
    }

    if (PayloadMatchesOriginal(payloadBytes, bitsData, payloadBitStream)) {
        return incomingRpcOriginal_(self, data, length, playerId);
    }

    const int payloadBitsUsed = payloadBitStream.GetNumberOfBitsUsed();
    constexpr int kMaxSafeRepackedPayloadBits = std::numeric_limits<int>::max() / 2 - 64;
    if (payloadBitsUsed < 0
        || payloadBitsUsed > kMaxSafeRepackedPayloadBits
        || (payloadBitsUsed > 0 && payloadBitStream.GetData() == nullptr)) {
        debuglog::WriteError(
            "SampRakHooks::HandleIncomingRpc rejected invalid modified payload rpc=%u bits=%d",
            static_cast<unsigned int>(rpcId),
            payloadBitsUsed);
        return incomingRpcOriginal_(self, data, length, playerId);
    }

    wrapper.SetWriteOffset(rpcHeaderOffset);
    wrapper.Write(rpcId);

    bitsData = static_cast<std::uint32_t>(payloadBitsUsed);
    wrapper.WriteCompressed(bitsData);
    if (bitsData > 0) {
        wrapper.WriteBits(payloadBitStream.GetData(), static_cast<int>(bitsData));
    }

    return incomingRpcOriginal_(self, wrapper.GetData(), wrapper.GetNumberOfBytesUsed(), playerId);
}
