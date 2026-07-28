#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "external/raknet/RakClient.h"
#include "raknet_bitstream_view.h"
#include "samp_call_context.h"

class SampApi;

namespace SampRpcIds {

inline constexpr std::uint8_t ShowDialog = 61;
inline constexpr std::uint8_t DialogResponse = 62;
inline constexpr std::uint8_t ClientMessage = 93;
inline constexpr std::uint8_t ServerCommand = 50;
inline constexpr std::uint8_t Chat = 101;
inline constexpr std::uint8_t ChatBubble = 59;
inline constexpr std::uint8_t GiveTakeDamage = 115;

} // namespace SampRpcIds

class SampRakHooks {
public:
    using RawRpcHandler = std::function<bool(std::uint8_t, RakNetBitStreamView&)>;
    using RawPacketHandler = std::function<bool(std::uint8_t, RakNetBitStreamView&)>;
    using ServerMessageFilter = std::function<bool(std::int32_t, const std::string&)>;
    using PlayerChatFilter = std::function<bool(std::uint16_t, const std::string&, const std::string&)>;
    using ChatBubbleFilter = std::function<bool(std::uint16_t, const std::string&, std::uint32_t, float, std::uint32_t, const std::string&)>;
    using ServerMessageHandler = std::function<bool(std::int32_t&, std::string&)>;
    using ShowDialogHandler = std::function<bool(std::uint16_t&, std::uint8_t&, std::string&, std::string&, std::string&, std::string&)>;
    using SendCommandHandler = std::function<bool(std::string&, const SampCallContext&)>;
    using SendChatHandler = std::function<bool(std::string&, const SampCallContext&)>;
    using SendDialogResponseHandler = std::function<bool(std::uint16_t&, std::uint8_t&, std::uint16_t&, std::string&)>;

    struct Stats {
        std::uint64_t onSendRpcCount = 0;
        std::uint64_t onSendPacketCount = 0;
        std::uint64_t onReceiveRpcCount = 0;
        std::uint64_t onReceivePacketCount = 0;
        std::uint8_t lastSendRpcId = 0;
        std::uint8_t lastSendPacketId = 0;
        std::uint8_t lastReceiveRpcId = 0;
        std::uint8_t lastReceivePacketId = 0;
    };

    void SetSampApi(SampApi* sampApi);
    void SetOwnerModule(HMODULE module);
    void Refresh();
    void Shutdown();

    void AddOnSendRpcHandler(RawRpcHandler handler);
    void AddOnSendPacketHandler(RawPacketHandler handler);
    void AddOnReceiveRpcHandler(RawRpcHandler handler);
    void AddOnReceivePacketHandler(RawPacketHandler handler);
    void AddServerMessageFilter(ServerMessageFilter handler);
    void AddPlayerChatFilter(PlayerChatFilter handler);
    void AddChatBubbleFilter(ChatBubbleFilter handler);
    void AddOnServerMessageHandler(ServerMessageHandler handler);
    void AddOnShowDialogHandler(ShowDialogHandler handler);
    void AddOnSendCommandHandler(SendCommandHandler handler);
    void AddOnSendChatHandler(SendChatHandler handler);
    void AddOnSendDialogResponseHandler(SendDialogResponseHandler handler);

    void onSendRpc(RawRpcHandler handler) { AddOnSendRpcHandler(std::move(handler)); }
    void onSendPacket(RawPacketHandler handler) { AddOnSendPacketHandler(std::move(handler)); }
    void onReceiveRpc(RawRpcHandler handler) { AddOnReceiveRpcHandler(std::move(handler)); }
    void onReceivePacket(RawPacketHandler handler) { AddOnReceivePacketHandler(std::move(handler)); }
    void onServerMessage(ServerMessageHandler handler) { AddOnServerMessageHandler(std::move(handler)); }
    void onShowDialog(ShowDialogHandler handler) { AddOnShowDialogHandler(std::move(handler)); }
    void onSendCommand(SendCommandHandler handler) { AddOnSendCommandHandler(std::move(handler)); }
    void onSendChat(SendChatHandler handler) { AddOnSendChatHandler(std::move(handler)); }
    void onSendDialogResponse(SendDialogResponseHandler handler) { AddOnSendDialogResponseHandler(std::move(handler)); }

    bool IsInstalled() const;
    bool EmulateIncomingPacket(std::uint8_t packetId, BitStream& payload);
    const std::string& statusText() const;
    Stats stats() const;
    std::vector<std::string> GetRecentLog() const;

private:
    using IncomingRpcHandlerFn = bool(__thiscall*)(void*, unsigned char*, int, PlayerID);
    using SendPacketFn = bool(__thiscall*)(void*, BitStream*, PacketPriority, PacketReliability, char);
    using ReceivePacketFn = Packet*(__thiscall*)(void*);
    using DeallocatePacketFn = void(__thiscall*)(void*, Packet*);
    using SendRpcFn = bool(__thiscall*)(void*, int*, BitStream*, PacketPriority, PacketReliability, char, bool);

    bool Install();
    void CleanupHooks();
    bool InstallRakClientVtableHooks(
        std::uintptr_t vtable,
        const std::array<std::uint32_t, 4>& targets);
    bool RestoreRakClientVtableHooks();
    void ClearRakClientOriginals();
    bool TryGetRakClientInterface(std::uintptr_t& rakClientInterface) const;
    bool HandleIncomingRpc(void* self, unsigned char* data, int length, PlayerID playerId);
    Packet* PopQueuedIncomingPacket();
    bool FreeSyntheticPacket(Packet* packet);
    void DeallocatePacketInternal(void* self, Packet* packet);
    bool HandleOutgoingRpc(std::uint8_t rpcId, RakNetBitStreamView& view, const SampCallContext& context);
    bool HandleIncomingRpcPayload(std::uint8_t rpcId, RakNetBitStreamView& view);
    bool HandleOutgoingPacket(std::uint8_t packetId, RakNetBitStreamView& view) const;
    bool HandleIncomingPacket(std::uint8_t packetId, RakNetBitStreamView& view) const;

    bool DispatchSendCommand(RakNetBitStreamView& view, const SampCallContext& context);
    bool DispatchSendChat(RakNetBitStreamView& view, const SampCallContext& context);
    bool DispatchSendDialogResponse(RakNetBitStreamView& view);
    bool DispatchServerMessage(RakNetBitStreamView& view);
    bool DispatchPlayerChat(RakNetBitStreamView& view);
    bool DispatchChatBubble(RakNetBitStreamView& view);
    bool DispatchShowDialog(RakNetBitStreamView& view);

    void AppendLog(const char* format, ...);
    void RecordSendRpc(std::uint8_t id);
    void RecordSendPacket(std::uint8_t id);
    void RecordReceiveRpc(std::uint8_t id);
    void RecordReceivePacket(std::uint8_t id);

    static std::string Truncate(std::string text, std::size_t maxLength);

    static bool SafeReadUInt32(std::uintptr_t address, std::uint32_t& value);
    static bool ReadString8(RakNetBitStreamView& view, std::string& value);
    static bool ReadString32(RakNetBitStreamView& view, std::string& value);
    static bool ReadEncodedString(RakNetBitStreamView& view, std::uintptr_t reader, std::uintptr_t compressor, int maxLength, std::string& value);
    static void WriteString8(RakNetBitStreamView& view, std::string_view value);
    static void WriteString32(RakNetBitStreamView& view, std::string_view value);
    static bool WriteEncodedString(RakNetBitStreamView& view, std::uintptr_t writer, std::uintptr_t compressor, std::string_view value);
    static bool __fastcall IncomingRpcHandlerDetour(void* self, void* edx, unsigned char* data, int length, PlayerID playerId);
    static bool __fastcall SendPacketDetour(void* self, void* edx, BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel);
    static Packet* __fastcall ReceivePacketDetour(void* self, void* edx);
    static void __fastcall DeallocatePacketDetour(void* self, void* edx, Packet* packet);
    static bool __fastcall SendRpcDetour(void* self, void* edx, int* id, BitStream* bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel, bool shiftTimestamp);

    static inline SampRakHooks* self_ = nullptr;

    SampApi* sampApi_ = nullptr;
    HMODULE ownerModule_ = nullptr;
    bool installed_ = false;
    std::string statusText_ = "waiting for SAMP RakNet";
    mutable std::mutex logMutex_;
    std::vector<std::string> recentLog_;
    mutable std::mutex statsMutex_;
    Stats stats_;

    std::vector<RawRpcHandler> onSendRpcHandlers_;
    std::vector<RawPacketHandler> onSendPacketHandlers_;
    std::vector<RawRpcHandler> onReceiveRpcHandlers_;
    std::vector<RawPacketHandler> onReceivePacketHandlers_;
    std::vector<ServerMessageFilter> serverMessageFilters_;
    std::vector<PlayerChatFilter> playerChatFilters_;
    std::vector<ChatBubbleFilter> chatBubbleFilters_;
    std::vector<ServerMessageHandler> onServerMessageHandlers_;
    std::vector<ShowDialogHandler> onShowDialogHandlers_;
    std::vector<SendCommandHandler> onSendCommandHandlers_;
    std::vector<SendChatHandler> onSendChatHandlers_;
    std::vector<SendDialogResponseHandler> onSendDialogResponseHandlers_;

    std::array<std::uintptr_t, 4> rakClientVtableSlots_{};
    // Once published, a detour can already be loaded by another thread even
    // after its slot is restored. Keep its predecessor valid until process exit.
    bool rakClientDetourExposed_ = false;
    bool retainedRakClientOwnerLogged_ = false;
    mutable std::mutex syntheticPacketsMutex_;
    std::deque<std::vector<unsigned char>> queuedIncomingPackets_;
    std::unordered_set<Packet*> syntheticPackets_;

    void* incomingRpcTarget_ = nullptr;
    IncomingRpcHandlerFn incomingRpcOriginal_ = nullptr;
    SendPacketFn sendPacketOriginal_ = nullptr;
    ReceivePacketFn receivePacketOriginal_ = nullptr;
    DeallocatePacketFn deallocatePacketDetourOriginal_ = nullptr;
    SendRpcFn sendRpcOriginal_ = nullptr;
};
