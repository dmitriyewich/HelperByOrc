#include "incoming_message_router.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "samp_hooks.h"
#include "samp_rak_hooks.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t kIncomingMessageMergeWindowMs = 80;
constexpr std::uint64_t kIncomingMessageRecentDedupMs = 1500;
constexpr std::size_t kMaxPendingMessages = 128;
constexpr std::size_t kMaxRecentMessages = 256;

std::string TrimCopy(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return std::string(text.substr(start, end - start));
}

std::string NormalizeMessageKey(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '\r') {
            normalized.push_back('\n');
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            continue;
        }
        normalized.push_back(ch);
    }

    return TrimCopy(normalized);
}

std::vector<std::string> BuildMessageKeys(std::string_view text, std::string_view prefix) {
    std::vector<std::string> keys;

    const std::string normalizedText = NormalizeMessageKey(text);
    if (!normalizedText.empty()) {
        keys.push_back(normalizedText);
    }

    const std::string normalizedPrefix = NormalizeMessageKey(prefix);
    if (!normalizedPrefix.empty()) {
        const std::string normalizedPrefixedText = NormalizeMessageKey(normalizedPrefix + " " + std::string(text));
        if (!normalizedPrefixedText.empty()
            && std::find(keys.begin(), keys.end(), normalizedPrefixedText) == keys.end()) {
            keys.push_back(normalizedPrefixedText);
        }
    }

    return keys;
}

bool KeysIntersect(const std::vector<std::string>& lhs, const std::vector<std::string>& rhs) {
    for (const std::string& leftKey : lhs) {
        if (std::find(rhs.begin(), rhs.end(), leftKey) != rhs.end()) {
            return true;
        }
    }
    return false;
}

} // namespace

struct IncomingMessageRouter::Impl {
    struct PendingMessage {
        IncomingMessageEvent event;
        std::vector<std::string> keys;
        std::uint64_t firstSeenAtMs = 0;
        std::uint64_t lastSeenAtMs = 0;
    };

    struct ReadyMessage {
        IncomingMessageEvent event;
        std::vector<std::string> keys;
        std::uint64_t readyAtMs = 0;
    };

    struct RecentMessage {
        std::vector<std::string> keys;
        bool fromAddEntry = false;
        bool fromRakNet = false;
        std::uint64_t dispatchedAtMs = 0;
    };

    SampHooks* sampHooks = nullptr;
    SampRakHooks* sampRakHooks = nullptr;
    bool chatHookBound = false;
    bool rakHookBound = false;
    bool active = true;
    std::mutex mutex{};
    std::vector<MessageHandler> handlers{};
    std::deque<PendingMessage> pendingMessages{};
    std::deque<ReadyMessage> readyMessages{};
    std::deque<RecentMessage> recentMessages{};

    void ConnectHooks() {
        if (!chatHookBound && sampHooks) {
            sampHooks->AddOnChatMessageHandler([this](
                                                  int type,
                                                  const std::string& text,
                                                  const std::string& prefix,
                                                  std::uint32_t textColor,
                                                  std::uint32_t prefixColor) {
                IncomingMessageEvent event;
                event.chatType = type;
                event.text = text;
                event.prefix = prefix;
                event.textColor = textColor;
                event.prefixColor = prefixColor;
                event.fromAddEntry = true;
                Enqueue(std::move(event));
            });
            chatHookBound = true;
        }

        if (!rakHookBound && sampRakHooks) {
            sampRakHooks->AddOnServerMessageHandler([this](std::int32_t& color, std::string& text) {
                IncomingMessageEvent event;
                event.chatType = -1;
                event.text = text;
                event.textColor = static_cast<std::uint32_t>(color);
                event.fromRakNet = true;
                Enqueue(std::move(event));
                return true;
            });
            rakHookBound = true;
        }
    }

    void MergeIntoPending(PendingMessage& pending, const IncomingMessageEvent& incoming, std::uint64_t nowMs) {
        pending.event.fromAddEntry = pending.event.fromAddEntry || incoming.fromAddEntry;
        pending.event.fromRakNet = pending.event.fromRakNet || incoming.fromRakNet;
        pending.lastSeenAtMs = nowMs;

        if (incoming.fromAddEntry || !pending.event.fromAddEntry) {
            pending.event.chatType = incoming.chatType;
            pending.event.text = incoming.text;
            pending.event.prefix = incoming.prefix;
            pending.event.textColor = incoming.textColor;
            pending.event.prefixColor = incoming.prefixColor;
            return;
        }

        if (pending.event.text.empty()) {
            pending.event.text = incoming.text;
        }
        if (pending.event.prefix.empty()) {
            pending.event.prefix = incoming.prefix;
        }
        if (pending.event.textColor == 0) {
            pending.event.textColor = incoming.textColor;
        }
        if (pending.event.prefixColor == 0) {
            pending.event.prefixColor = incoming.prefixColor;
        }
    }

    void PruneRecentMessages(std::uint64_t nowMs) {
        while (!recentMessages.empty() && nowMs - recentMessages.front().dispatchedAtMs > kIncomingMessageRecentDedupMs) {
            recentMessages.pop_front();
        }
        while (recentMessages.size() > kMaxRecentMessages) {
            recentMessages.pop_front();
        }
    }

    bool IsDuplicateOfRecent(const std::vector<std::string>& keys, const IncomingMessageEvent& event, std::uint64_t nowMs) {
        PruneRecentMessages(nowMs);

        for (RecentMessage& recent : recentMessages) {
            if (!KeysIntersect(recent.keys, keys)) {
                continue;
            }

            const bool complementarySource =
                (event.fromAddEntry && !recent.fromAddEntry && recent.fromRakNet)
                || (event.fromRakNet && !recent.fromRakNet && recent.fromAddEntry);
            if (!complementarySource) {
                continue;
            }

            recent.fromAddEntry = recent.fromAddEntry || event.fromAddEntry;
            recent.fromRakNet = recent.fromRakNet || event.fromRakNet;
            recent.dispatchedAtMs = nowMs;
            return true;
        }

        return false;
    }

    void RememberRecent(const IncomingMessageEvent& event, const std::vector<std::string>& keys, std::uint64_t nowMs) {
        RecentMessage recent;
        recent.keys = keys;
        recent.fromAddEntry = event.fromAddEntry;
        recent.fromRakNet = event.fromRakNet;
        recent.dispatchedAtMs = nowMs;
        recentMessages.push_back(std::move(recent));
        while (recentMessages.size() > kMaxRecentMessages) {
            recentMessages.pop_front();
        }
    }

    void Enqueue(IncomingMessageEvent event) {
        const std::vector<std::string> keys = BuildMessageKeys(event.text, event.prefix);
        if (keys.empty()) {
            return;
        }

        const std::uint64_t nowMs = GetTickCount64();

        std::lock_guard lock(mutex);
        if (!active) {
            return;
        }

        if (IsDuplicateOfRecent(keys, event, nowMs)) {
            return;
        }

        for (auto it = pendingMessages.begin(); it != pendingMessages.end(); ++it) {
            if (!KeysIntersect(it->keys, keys)) {
                continue;
            }
            if (nowMs - it->firstSeenAtMs > kIncomingMessageMergeWindowMs) {
                continue;
            }

            const bool sameSource =
                (event.fromAddEntry && it->event.fromAddEntry)
                || (event.fromRakNet && it->event.fromRakNet);
            if (sameSource) {
                continue;
            }

            MergeIntoPending(*it, event, nowMs);
            ReadyMessage ready{ std::move(it->event), it->keys, it->firstSeenAtMs };
            RememberRecent(ready.event, ready.keys, nowMs);
            readyMessages.push_back(std::move(ready));
            pendingMessages.erase(it);
            return;
        }

        pendingMessages.push_back(PendingMessage{
            std::move(event),
            keys,
            nowMs,
            nowMs,
        });

        while (pendingMessages.size() > kMaxPendingMessages) {
            PendingMessage overflow = std::move(pendingMessages.front());
            pendingMessages.pop_front();
            ReadyMessage ready{ std::move(overflow.event), std::move(overflow.keys), overflow.firstSeenAtMs };
            RememberRecent(ready.event, ready.keys, nowMs);
            readyMessages.push_back(std::move(ready));
        }
    }

    void Tick() {
        std::vector<MessageHandler> handlersCopy;
        std::vector<ReadyMessage> ready;

        {
            std::lock_guard lock(mutex);
            if (!active) {
                return;
            }

            const std::uint64_t nowMs = GetTickCount64();
            while (!pendingMessages.empty() && nowMs - pendingMessages.front().firstSeenAtMs >= kIncomingMessageMergeWindowMs) {
                PendingMessage pending = std::move(pendingMessages.front());
                pendingMessages.pop_front();
                ReadyMessage ready{ std::move(pending.event), std::move(pending.keys), pending.firstSeenAtMs };
                RememberRecent(ready.event, ready.keys, nowMs);
                readyMessages.push_back(std::move(ready));
            }

            handlersCopy = handlers;
            ready.assign(readyMessages.begin(), readyMessages.end());
            readyMessages.clear();
        }

        if (handlersCopy.empty() || ready.empty()) {
            return;
        }

        std::stable_sort(ready.begin(), ready.end(), [](const ReadyMessage& lhs, const ReadyMessage& rhs) {
            return lhs.readyAtMs < rhs.readyAtMs;
        });

        for (const ReadyMessage& item : ready) {
            for (const MessageHandler& handler : handlersCopy) {
                handler(item.event);
            }
        }
    }

    void Shutdown() {
        std::lock_guard lock(mutex);
        active = false;
        pendingMessages.clear();
        readyMessages.clear();
        recentMessages.clear();
    }
};

IncomingMessageRouter::IncomingMessageRouter() = default;
IncomingMessageRouter::~IncomingMessageRouter() = default;
IncomingMessageRouter::IncomingMessageRouter(IncomingMessageRouter&&) noexcept = default;
IncomingMessageRouter& IncomingMessageRouter::operator=(IncomingMessageRouter&&) noexcept = default;

void IncomingMessageRouter::SetSampHooks(SampHooks* sampHooks) {
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    impl_->sampHooks = sampHooks;
    impl_->active = true;
    impl_->ConnectHooks();
}

void IncomingMessageRouter::SetSampRakHooks(SampRakHooks* sampRakHooks) {
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    impl_->sampRakHooks = sampRakHooks;
    impl_->active = true;
    impl_->ConnectHooks();
}

void IncomingMessageRouter::AddOnMessageHandler(MessageHandler handler) {
    if (!handler) {
        return;
    }
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }

    std::lock_guard lock(impl_->mutex);
    impl_->handlers.push_back(std::move(handler));
}

void IncomingMessageRouter::Tick() {
    if (impl_) {
        impl_->Tick();
    }
}

void IncomingMessageRouter::Shutdown() {
    if (impl_) {
        impl_->Shutdown();
    }
}
