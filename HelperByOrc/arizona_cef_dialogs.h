#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

class RakNetBitStreamView;
class SampApi;
class SampRakHooks;

namespace jsonutil {
struct JsonValue;
}

class ArizonaCefDialogs {
public:
    void SetSampApi(SampApi* sampApi);
    void SetSampRakHooks(SampRakHooks* sampRakHooks);
    void OnProcessAttach();
    void Shutdown();
    void Tick();

    bool IsDialogActive() const;
    bool SetInputText(std::string_view text);
    bool SetInputCursor(int start, int finish);
    bool CloseWithButton(int button);
    bool SetListItem(int index);
    bool SendRespond(int id, int button, int listItem, std::string_view inputText);

    std::string CachedInputText() const;
    std::string CachedListItem() const;
    std::string QueryInputText(int timeoutMs);
    std::string QueryListItem(int timeoutMs);
    bool HasPendingInputTextQuery() const;
    bool HasPendingListItemQuery() const;

    int LastDialogId() const;
    int LastDialogStyle() const;
    std::string LastDialogTitle() const;
    std::string LastDialogButton1() const;
    std::string LastDialogButton2() const;
    std::string LastDialogText() const;

    int LastRespondId() const;
    int LastRespondButton() const;
    int LastRespondList() const;
    std::string LastRespondInput() const;
    std::string LastRespondJoined() const;

private:
    enum class QueryKind {
        InputText,
        ListItem,
    };

    struct DialogInfo {
        int id = -1;
        int style = -1;
        std::string title{};
        std::string button1{};
        std::string button2{};
        std::string text{};
    };

    struct RespondInfo {
        int id = -1;
        int button = -1;
        int list = -1;
        std::string input{};
    };

    struct PendingQuery {
        QueryKind kind = QueryKind::InputText;
        std::uint64_t deadlineAtMs = 0;
    };

    bool HandleOutgoingPacket(std::uint8_t packetId, RakNetBitStreamView& view);
    bool HandleShowDialog(
        std::uint16_t& dialogId,
        std::uint8_t& style,
        std::string& title,
        std::string& button1,
        std::string& button2,
        std::string& text);
    bool HandleSendDialogResponse(std::uint16_t& dialogId, std::uint8_t& button, std::uint16_t& listItem, std::string& input);

    bool EvalCef(std::string_view code, bool logFailure) const;
    bool EvalAnon(std::string_view code, bool logFailure) const;
    std::uint32_t BeginQuery(std::string_view code, QueryKind kind, int timeoutMs);
    std::string Query(std::string_view code, QueryKind kind, int timeoutMs, std::string fallback);
    void CompleteQuery(std::uint32_t requestId, std::string value);
    void PruneExpiredQueries(std::uint64_t now);
    bool HasPendingQuery(QueryKind kind) const;
    bool CanUseCef() const;

    static int ClampQueryTimeout(int timeoutMs);
    static int NormalizeListItem(std::uint16_t value);
    static std::string JsonValueToString(const jsonutil::JsonValue& value);

    SampApi* sampApi_ = nullptr;
    SampRakHooks* sampRakHooks_ = nullptr;
    bool attached_ = false;
    bool shutdown_ = false;

    mutable std::mutex mutex_;
    DialogInfo lastDialogInfo_{};
    RespondInfo lastRespond_{};
    std::string cachedInputText_{};
    std::string cachedListItem_{ "0" };
    std::uint32_t nextRequestId_ = 0;
    std::map<std::uint32_t, PendingQuery> pendingQueries_{};
};
