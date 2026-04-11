#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class SampHooks;
class SampRakHooks;

struct IncomingMessageEvent {
    int chatType = -1;
    std::string text;
    std::string prefix;
    std::uint32_t textColor = 0;
    std::uint32_t prefixColor = 0;
    bool fromAddEntry = false;
    bool fromRakNet = false;
};

class IncomingMessageRouter {
public:
    using MessageHandler = std::function<void(const IncomingMessageEvent&)>;

    IncomingMessageRouter();
    ~IncomingMessageRouter();

    IncomingMessageRouter(const IncomingMessageRouter&) = delete;
    IncomingMessageRouter& operator=(const IncomingMessageRouter&) = delete;
    IncomingMessageRouter(IncomingMessageRouter&&) noexcept;
    IncomingMessageRouter& operator=(IncomingMessageRouter&&) noexcept;

    void SetSampHooks(SampHooks* sampHooks);
    void SetSampRakHooks(SampRakHooks* sampRakHooks);
    void AddOnMessageHandler(MessageHandler handler);
    void Tick();
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
