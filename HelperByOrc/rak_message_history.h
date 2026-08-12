#pragma once

#include <cstdint>
#include <string>

enum class IncomingMessageHistorySource : std::uint8_t {
    ServerMessage = 0,
    PlayerChat,
    ChatBubble,
};

struct IncomingMessageHistoryEntry {
    std::uint64_t sequence = 0;
    IncomingMessageHistorySource source = IncomingMessageHistorySource::ServerMessage;
    int playerId = -1;
    std::string playerName;
    std::string text;
};
