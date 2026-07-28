#include "arizona_cef_dialogs.h"

#include "debug_log.h"
#include "json_utils.h"
#include "raknet_bitstream_view.h"
#include "samp_api.h"
#include "samp_rak_hooks.h"
#include "text_encoding.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <string>
#include <utility>

namespace {

constexpr std::uint8_t kArizonaCefPacketId = 220;
constexpr std::uint8_t kCefEvalPacketType = 17;
constexpr std::uint8_t kCefSendMessagePacketType = 18;
constexpr std::string_view kQueryPrefix = "helperbyorc-arizona-cef-dialogs";
constexpr std::string_view kCloseEventPrefix = "hbo|";
constexpr int kDefaultQueryTimeoutMs = 500;
constexpr int kMaxQueryTimeoutMs = 3000;
constexpr std::size_t kEvalAnonWrapperBytes = 12;
constexpr std::size_t kCefEvalPacketOverheadBytes = 8;
constexpr std::size_t kMaxCloseReadyPacketBytes = 681;
constexpr std::size_t kMaxCloseActionPacketBytes = 320;

void AppendJsUnicodeEscape(std::string& out, wchar_t ch) {
    char buffer[7]{};
    std::snprintf(buffer, sizeof(buffer), "\\u%04X", static_cast<unsigned int>(ch));
    out += buffer;
}

std::string MakeJsStringLiteral(std::string_view value) {
    std::wstring wide;
    if (!value.empty()) {
        const int wideLength = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0);
        if (wideLength > 0) {
            wide.resize(static_cast<std::size_t>(wideLength));
            if (MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    value.data(),
                    static_cast<int>(value.size()),
                    wide.data(),
                    wideLength)
                <= 0) {
                wide.clear();
            }
        }
    }

    std::string result;
    result.reserve((wide.empty() ? value.size() : wide.size() * 6) + 2);
    result.push_back('"');

    if (!wide.empty() || value.empty()) {
        for (const wchar_t ch : wide) {
            switch (ch) {
            case L'\\':
                result += "\\\\";
                break;
            case L'"':
                result += "\\\"";
                break;
            case L'\b':
                result += "\\b";
                break;
            case L'\f':
                result += "\\f";
                break;
            case L'\n':
                result += "\\n";
                break;
            case L'\r':
                result += "\\r";
                break;
            case L'\t':
                result += "\\t";
                break;
            default:
                if (ch >= 0x20 && ch <= 0x7E) {
                    result.push_back(static_cast<char>(ch));
                } else {
                    AppendJsUnicodeEscape(result, ch);
                }
                break;
            }
        }
        result.push_back('"');
        return result;
    }

    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (ch < 0x20) {
                char buffer[7]{};
                std::snprintf(buffer, sizeof(buffer), "\\u%04X", ch);
                result += buffer;
            } else {
                result.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    result.push_back('"');
    return result;
}

std::uint32_t MakeDialogTextFingerprint(std::string_view value) {
    if (value.empty()) {
        return 0;
    }

    const int wideLength = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (wideLength <= 0) {
        return 0;
    }

    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            wide.data(),
            wideLength)
        <= 0) {
        return 0;
    }

    auto isHex = [](wchar_t ch) {
        return (ch >= L'0' && ch <= L'9')
            || (ch >= L'a' && ch <= L'f')
            || (ch >= L'A' && ch <= L'F');
    };
    auto colorTagLength = [&](std::size_t offset) -> std::size_t {
        for (const std::size_t hexLength : { std::size_t{ 6 }, std::size_t{ 8 } }) {
            const std::size_t close = offset + hexLength + 1;
            if (close >= wide.size() || wide[offset] != L'{' || wide[close] != L'}') {
                continue;
            }
            if (std::all_of(wide.begin() + offset + 1, wide.begin() + close, isHex)) {
                return hexLength + 2;
            }
        }
        return 0;
    };

    constexpr std::uint32_t kFnvOffset = 2166136261u;
    constexpr std::uint32_t kFnvPrime = 16777619u;
    std::uint32_t hash = kFnvOffset;
    bool hasText = false;
    bool pendingSpace = false;
    for (std::size_t index = 0; index < wide.size();) {
        const std::size_t tagLength = colorTagLength(index);
        if (tagLength != 0) {
            index += tagLength;
            continue;
        }

        const wchar_t ch = wide[index++];
        if (std::iswspace(ch)) {
            pendingSpace = hasText;
            continue;
        }
        if (pendingSpace) {
            hash = (hash ^ static_cast<std::uint16_t>(L' ')) * kFnvPrime;
            pendingSpace = false;
        }
        hash = (hash ^ static_cast<std::uint16_t>(ch)) * kFnvPrime;
        hasText = true;
    }
    return hasText ? hash : 0;
}

std::string MakeInputQueryCode() {
    return R"JS(
var d = document.querySelector('.dialog');
if (!d) return null;
var i = d.querySelector('input.dialog-input__field, textarea.dialog-input__field');
return i ? i.value : null;
)JS";
}

std::string MakeListItemQueryCode() {
    return R"JS(
var d = document.querySelector('.dialog');
if (!d) return 0;
var list = d.querySelector('.dialog-list-loop__list');
if (!list) return 0;
var items = list.querySelectorAll('.dialog-list-loop__list-item');
for (var i = 0; i < items.length; i++) {
    if (items[i].classList.contains('dialog-list-loop__list-item--active')) return i;
}
return 0;
)JS";
}

std::string MakeListItemsQueryCode() {
    return R"JS(
var d = document.querySelector('.dialog');
if (!d) return [];
var items = d.querySelectorAll('.dialog-list-loop__list-item');
return Array.prototype.map.call(items, function (item) {
    return item ? (item.innerText || item.textContent || '') : '';
});
)JS";
}

std::string MakeInputPresenceQueryCode() {
    return R"JS(
var d = document.querySelector('.dialog');
if (!d) return false;
return !!d.querySelector('input.dialog-input__field, textarea.dialog-input__field');
)JS";
}

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

} // namespace

void ArizonaCefDialogs::SetSampApi(SampApi* sampApi) {
    sampApi_ = sampApi;
    debuglog::WriteInfo("ArizonaCefDialogs::SetSampApi assigned=%d", sampApi_ ? 1 : 0);
}

void ArizonaCefDialogs::SetSampRakHooks(SampRakHooks* sampRakHooks) {
    sampRakHooks_ = sampRakHooks;
    debuglog::WriteInfo("ArizonaCefDialogs::SetSampRakHooks assigned=%d", sampRakHooks_ ? 1 : 0);
}

void ArizonaCefDialogs::OnProcessAttach() {
    if (attached_ || !sampRakHooks_) {
        return;
    }

    shutdown_ = false;
    attached_ = true;
    sampRakHooks_->AddOnSendPacketHandler([this](std::uint8_t packetId, RakNetBitStreamView& view) {
        return HandleOutgoingPacket(packetId, view);
    });
    sampRakHooks_->AddOnShowDialogHandler([this](
                                              std::uint16_t& dialogId,
                                              std::uint8_t& style,
                                              std::string& title,
                                              std::string& button1,
                                              std::string& button2,
                                              std::string& text) {
        return HandleShowDialog(dialogId, style, title, button1, button2, text);
    });
    sampRakHooks_->AddOnSendDialogResponseHandler([this](
                                                     std::uint16_t& dialogId,
                                                     std::uint8_t& button,
                                                     std::uint16_t& listItem,
                                                     std::string& input) {
        return HandleSendDialogResponse(dialogId, button, listItem, input);
    });
    debuglog::WriteInfo("ArizonaCefDialogs attached");
}

void ArizonaCefDialogs::Shutdown() {
    {
        std::lock_guard lock(mutex_);
        shutdown_ = true;
        pendingQueries_.clear();
        pendingClose_ = {};
        cachedInputFieldPresentKnown_ = false;
        cachedInputFieldPresent_ = false;
        nextInputFieldProbeAtMs_ = 0;
    }
    debuglog::WriteInfo("ArizonaCefDialogs shutdown");
}

void ArizonaCefDialogs::Tick() {
    if (shutdown_) {
        return;
    }

    const std::uint64_t now = GetTickCount64();
    PruneExpiredQueries(now);

    if (!IsDialogActive() || !CanUseCef()) {
        std::lock_guard lock(mutex_);
        cachedInputFieldPresentKnown_ = false;
        cachedInputFieldPresent_ = false;
        nextInputFieldProbeAtMs_ = 0;
        return;
    }

    const int style = LastDialogStyle();
    const bool inputStyle = sampApi_
        ? sampApi_->isDialogInputStyle(style)
        : style == SampApi::DIALOG_STYLE_INPUT || style == SampApi::DIALOG_STYLE_PASSWORD;
    if (!inputStyle) {
        std::lock_guard lock(mutex_);
        cachedInputFieldPresentKnown_ = true;
        cachedInputFieldPresent_ = false;
        nextInputFieldProbeAtMs_ = 0;
        return;
    }

    bool shouldProbe = false;
    {
        std::lock_guard lock(mutex_);
        shouldProbe = now >= nextInputFieldProbeAtMs_;
        if (shouldProbe) {
            nextInputFieldProbeAtMs_ = now + 250;
        }
    }

    if (shouldProbe && !HasPendingQuery(QueryKind::InputPresent)) {
        BeginQuery(MakeInputPresenceQueryCode(), QueryKind::InputPresent, kDefaultQueryTimeoutMs);
    }
}

bool ArizonaCefDialogs::IsDialogActive() const {
    return sampApi_ && sampApi_->sampModule() && sampApi_->isSupportedVersion() && sampApi_->isDialogActive();
}

bool ArizonaCefDialogs::SetInputText(std::string_view text) {
    if (const std::optional<bool> cachedPresent = CachedInputFieldPresent();
        cachedPresent.has_value() && !*cachedPresent) {
        debuglog::WriteError("ArizonaCefDialogs::SetInputText skipped: cached DOM input field is absent");
        return false;
    }

    const std::string js = std::string(R"JS(
var d = document.querySelector('.dialog');
if (!d) return;
var i = d.querySelector('input.dialog-input__field, textarea.dialog-input__field');
if (!i) return;
i.focus();
i.value = )JS")
        + MakeJsStringLiteral(text)
        + R"JS(;
i.dispatchEvent(new Event('input', { bubbles: true }));
i.dispatchEvent(new Event('change', { bubbles: true }));
)JS";

    const bool ok = EvalAnon(js, true);
    if (ok) {
        std::lock_guard lock(mutex_);
        cachedInputText_ = std::string(text);
    }
    return ok;
}

bool ArizonaCefDialogs::SetInputCursor(int start, int finish) {
    if (start < 0) {
        start = 0;
    }
    if (finish < 0) {
        finish = 0;
    }
    if (finish < start) {
        std::swap(start, finish);
    }

    const std::string js = std::string(R"JS(
var d = document.querySelector('.dialog');
if (!d) return;
var i = d.querySelector('input.dialog-input__field, textarea.dialog-input__field');
if (!i) return;
var start = Math.max(0, Math.min()JS")
        + std::to_string(start)
        + R"JS(, i.value.length));
var finish = Math.max(0, Math.min()JS"
        + std::to_string(finish)
        + R"JS(, i.value.length));
if (finish < start) {
    var tmp = start;
    start = finish;
    finish = tmp;
}
i.focus();
if (typeof i.setSelectionRange === 'function') {
    i.setSelectionRange(start, finish);
}
)JS";

    return EvalAnon(js, true);
}

bool ArizonaCefDialogs::CloseWithButton(int button) {
    if (button != 0 && button != 1) {
        return false;
    }

    int expectedDialogId = -1;
    std::uint64_t expectedDialogGeneration = 0;
    std::uint8_t closeToken = 0;
    std::string expectedTitle;
    {
        std::lock_guard lock(mutex_);
        expectedDialogId = lastDialogInfo_.id;
        expectedDialogGeneration = dialogGeneration_;
        expectedTitle = lastDialogInfo_.title;
        nextCloseToken_ = static_cast<std::uint8_t>((nextCloseToken_ + 1) % 10);
        closeToken = nextCloseToken_;
        pendingClose_ = PendingClose{
            true,
            button,
            closeToken,
            expectedDialogId,
            expectedDialogGeneration,
        };
    }
    const std::uint32_t expectedTitleFingerprint = MakeDialogTextFingerprint(expectedTitle);

    // Arizona can accept a synthetic packet without executing an oversized CEF
    // eval. Keep the observer stage within the same proven budget as live DOM
    // queries, then queue the actual button action as a separate short eval.
    const std::string js = std::string(R"JS(var t=)JS")
        + std::to_string(expectedTitleFingerprint)
        + R"JS(,p=)JS"
        + std::to_string(button)
        + R"JS(,n=)JS"
        + std::to_string(closeToken)
        + R"JS(,m=e=>window.cef.SendMessage('hbo|'+e+p+n,0),h=e=>{for(var s=(e&&e.textContent||'').replace(/\s+/g,' ').trim(),v=2166136261,j=0;j<s.length;j++)v=Math.imul(v^s.charCodeAt(j),16777619);return s?v>>>0:0},f=()=>{for(var a=document.querySelectorAll('.dialog'),i=a.length;i--;){var d=a[i];if(d.clientHeight&&(!t||h(d.querySelector('.dialog__header-text'))==t))return 1}},x=window._hbo;x&&x();m('s');if(f())m('r');else{var o=new MutationObserver(()=>{if(f()){o.disconnect();clearTimeout(q);m('r')}}),q=setTimeout(()=>{o.disconnect();m('t')},1500);window._hbo=()=>{o.disconnect();clearTimeout(q)};o.observe(document,{subtree:1,childList:1})})JS";

    const std::size_t scriptBytes = js.size() + kEvalAnonWrapperBytes;
    const std::size_t packetBytes = scriptBytes + kCefEvalPacketOverheadBytes;
    if (packetBytes > kMaxCloseReadyPacketBytes) {
        {
            std::lock_guard lock(mutex_);
            if (pendingClose_.active && pendingClose_.token == closeToken) {
                pendingClose_ = {};
            }
        }
        debuglog::WriteError(
            "ArizonaCefDialogs close readiness rejected id=%d generation=%llu scriptBytes=%llu packetBytes=%llu packetLimit=%llu",
            expectedDialogId,
            static_cast<unsigned long long>(expectedDialogGeneration),
            static_cast<unsigned long long>(scriptBytes),
            static_cast<unsigned long long>(packetBytes),
            static_cast<unsigned long long>(kMaxCloseReadyPacketBytes));
        return false;
    }

    const bool queued = EvalAnon(js, true);
    if (!queued) {
        std::lock_guard lock(mutex_);
        if (pendingClose_.active && pendingClose_.token == closeToken) {
            pendingClose_ = {};
        }
    } else {
        debuglog::WriteInfo(
            "ArizonaCefDialogs close readiness queued id=%d generation=%llu button=%d token=%u scriptBytes=%llu packetBytes=%llu observerTimeoutMs=1500",
            expectedDialogId,
            static_cast<unsigned long long>(expectedDialogGeneration),
            button,
            closeToken,
            static_cast<unsigned long long>(scriptBytes),
            static_cast<unsigned long long>(packetBytes));
    }
    return queued;
}

bool ArizonaCefDialogs::SetListItem(int index) {
    if (index < 0) {
        return false;
    }

    const std::string js = std::string(R"JS(
var d = document.querySelector('.dialog');
if (!d) return;
var items = d.querySelectorAll('.dialog-list-loop__list-item');
var item = items[)JS")
        + std::to_string(index)
        + R"JS(];
if (item) item.click();
)JS";

    const bool ok = EvalAnon(js, true);
    if (ok) {
        std::lock_guard lock(mutex_);
        cachedListItem_ = std::to_string(index);
    }
    return ok;
}

bool ArizonaCefDialogs::SendRespond(int id, int button, int listItem, std::string_view inputText) {
    if (!sampApi_ || !sampApi_->sampModule() || !sampApi_->isSupportedVersion()) {
        debuglog::WriteError("ArizonaCefDialogs::SendRespond failed: SampApi is unavailable");
        return false;
    }
    if (id < 0 || button < 0 || button > 1 || listItem < -1) {
        debuglog::WriteError(
            "ArizonaCefDialogs::SendRespond failed: invalid args id=%d button=%d list=%d",
            id,
            button,
            listItem);
        return false;
    }

    return sampApi_->sendDialogResponse(id, button, listItem, inputText, false);
}

std::string ArizonaCefDialogs::CachedInputText() const {
    std::lock_guard lock(mutex_);
    return cachedInputText_;
}

std::string ArizonaCefDialogs::CachedListItem() const {
    std::lock_guard lock(mutex_);
    return cachedListItem_;
}

std::string ArizonaCefDialogs::CachedListItemsJson() const {
    std::lock_guard lock(mutex_);
    return cachedListItemsJson_;
}

std::optional<bool> ArizonaCefDialogs::CachedInputFieldPresent() const {
    std::lock_guard lock(mutex_);
    if (!cachedInputFieldPresentKnown_) {
        return std::nullopt;
    }
    return cachedInputFieldPresent_;
}

std::string ArizonaCefDialogs::QueryInputText(int timeoutMs) {
    return Query(MakeInputQueryCode(), QueryKind::InputText, timeoutMs, CachedInputText());
}

std::string ArizonaCefDialogs::QueryListItem(int timeoutMs) {
    return Query(MakeListItemQueryCode(), QueryKind::ListItem, timeoutMs, CachedListItem());
}

std::string ArizonaCefDialogs::QueryListItems(int timeoutMs) {
    return Query(MakeListItemsQueryCode(), QueryKind::ListItems, timeoutMs, CachedListItemsJson());
}

bool ArizonaCefDialogs::HasPendingInputTextQuery() const {
    return HasPendingQuery(QueryKind::InputText);
}

bool ArizonaCefDialogs::HasPendingListItemQuery() const {
    return HasPendingQuery(QueryKind::ListItem);
}

bool ArizonaCefDialogs::HasPendingListItemsQuery() const {
    return HasPendingQuery(QueryKind::ListItems);
}

int ArizonaCefDialogs::LastDialogId() const {
    std::lock_guard lock(mutex_);
    return lastDialogInfo_.id;
}

std::uint64_t ArizonaCefDialogs::LastDialogGeneration() const {
    std::lock_guard lock(mutex_);
    return dialogGeneration_;
}

int ArizonaCefDialogs::LastDialogStyle() const {
    std::lock_guard lock(mutex_);
    return lastDialogInfo_.style;
}

std::string ArizonaCefDialogs::LastDialogTitle() const {
    std::lock_guard lock(mutex_);
    return lastDialogInfo_.title;
}

std::string ArizonaCefDialogs::LastDialogButton1() const {
    std::lock_guard lock(mutex_);
    return lastDialogInfo_.button1;
}

std::string ArizonaCefDialogs::LastDialogButton2() const {
    std::lock_guard lock(mutex_);
    return lastDialogInfo_.button2;
}

std::string ArizonaCefDialogs::LastDialogText() const {
    std::lock_guard lock(mutex_);
    return lastDialogInfo_.text;
}

int ArizonaCefDialogs::LastRespondId() const {
    std::lock_guard lock(mutex_);
    return lastRespond_.id;
}

int ArizonaCefDialogs::LastRespondButton() const {
    std::lock_guard lock(mutex_);
    return lastRespond_.button;
}

int ArizonaCefDialogs::LastRespondList() const {
    std::lock_guard lock(mutex_);
    return lastRespond_.list;
}

std::string ArizonaCefDialogs::LastRespondInput() const {
    std::lock_guard lock(mutex_);
    return lastRespond_.input;
}

std::string ArizonaCefDialogs::LastRespondJoined() const {
    std::lock_guard lock(mutex_);
    return std::to_string(lastRespond_.id)
        + ";"
        + std::to_string(lastRespond_.button)
        + ";"
        + std::to_string(lastRespond_.list)
        + ";"
        + lastRespond_.input;
}

bool ArizonaCefDialogs::HandleOutgoingPacket(std::uint8_t packetId, RakNetBitStreamView& view) {
    if (packetId != kArizonaCefPacketId || view.GetNumberOfUnreadBits() < 32) {
        return true;
    }

    view.IgnoreBits(8);
    const std::uint8_t packetType = view.ReadUInt8();
    if (packetType != kCefSendMessagePacketType) {
        return true;
    }

    const std::uint16_t length = view.ReadUInt16();
    if (view.GetNumberOfUnreadBytes() < static_cast<int>(length)) {
        return true;
    }

    const std::string message = textencoding::GameToUtf8(view.ReadString(length));
    if (StartsWith(message, kCloseEventPrefix)) {
        const std::string_view eventCode(
            message.data() + kCloseEventPrefix.size(),
            message.size() - kCloseEventPrefix.size());
        if (eventCode.size() != 3
            || (eventCode[1] != '0' && eventCode[1] != '1')
            || eventCode[2] < '0'
            || eventCode[2] > '9') {
            debuglog::WriteError("ArizonaCefDialogs close script sent invalid event");
            return false;
        }

        const int requestedButton = eventCode[1] == '1' ? 1 : 0;
        const std::uint8_t closeToken = static_cast<std::uint8_t>(eventCode[2] - '0');
        int dialogId = -1;
        std::uint64_t generation = 0;
        bool currentClose = false;
        {
            std::lock_guard lock(mutex_);
            dialogId = lastDialogInfo_.id;
            generation = dialogGeneration_;
            currentClose = pendingClose_.active
                && pendingClose_.button == requestedButton
                && pendingClose_.token == closeToken
                && pendingClose_.dialogId == dialogId
                && pendingClose_.dialogGeneration == generation;
            if (eventCode[0] == 'r' && currentClose) {
                pendingClose_ = {};
            } else if (eventCode[0] == 't' && currentClose) {
                pendingClose_ = {};
            }
        }
        if (eventCode[0] == 'r') {
            if (!currentClose) {
                debuglog::WriteError(
                    "ArizonaCefDialogs stale close readiness ignored id=%d generation=%llu button=%d token=%u",
                    dialogId,
                    static_cast<unsigned long long>(generation),
                    requestedButton,
                    closeToken);
                return false;
            }

            const bool actionQueued = QueueCloseAction(requestedButton, closeToken);
            if (actionQueued) {
                debuglog::WriteInfo(
                    "ArizonaCefDialogs close action queued id=%d generation=%llu action=%s token=%u",
                    dialogId,
                    static_cast<unsigned long long>(generation),
                    requestedButton == 1 ? "primary-enter" : "secondary-callback",
                    closeToken);
            } else {
                debuglog::WriteError(
                    "ArizonaCefDialogs close action queue failed id=%d generation=%llu action=%s token=%u",
                    dialogId,
                    static_cast<unsigned long long>(generation),
                    requestedButton == 1 ? "primary-enter" : "secondary-callback",
                    closeToken);
            }
            return false;
        }
        if (eventCode[0] == 't' && !currentClose) {
            debuglog::WriteInfo(
                "ArizonaCefDialogs stale close timeout ignored id=%d generation=%llu button=%d token=%u",
                dialogId,
                static_cast<unsigned long long>(generation),
                requestedButton,
                closeToken);
            return false;
        }

        const char* event = nullptr;
        bool failed = false;
        switch (eventCode[0]) {
        case 's':
            event = "started";
            break;
        case 'c':
            event = "clicked";
            break;
        case 't':
            event = "timeout";
            failed = true;
            break;
        default:
            debuglog::WriteError("ArizonaCefDialogs close script sent unknown event");
            return false;
        }

        const char* action = requestedButton == 1 ? "primary" : "secondary";
        if (failed) {
            debuglog::WriteError(
                "ArizonaCefDialogs close script failed id=%d generation=%llu event=%s action=%s",
                dialogId,
                static_cast<unsigned long long>(generation),
                event,
                action);
        } else {
            debuglog::WriteInfo(
                "ArizonaCefDialogs close script event id=%d generation=%llu event=%s action=%s",
                dialogId,
                static_cast<unsigned long long>(generation),
                event,
                action);
        }
        return false;
    }

    const std::string prefix = std::string(kQueryPrefix) + "|";
    if (!StartsWith(message, prefix)) {
        return true;
    }

    const std::string_view payload(message.data() + prefix.size(), message.size() - prefix.size());
    std::string error;
    const std::optional<jsonutil::JsonValue> parsed = jsonutil::ParseJson(payload, error);
    if (!parsed.has_value()) {
        debuglog::WriteError(
            "ArizonaCefDialogs query response JSON parse failed wireBytes=%u utf8Bytes=%llu jsonBytes=%llu: %s",
            static_cast<unsigned>(length),
            static_cast<unsigned long long>(message.size()),
            static_cast<unsigned long long>(payload.size()),
            error.c_str());
        return false;
    }

    const jsonutil::JsonObject* object = parsed->TryObject();
    if (!object) {
        debuglog::WriteError("ArizonaCefDialogs query response ignored: root is not object");
        return false;
    }

    const auto requestIt = object->find("requestId");
    if (requestIt == object->end()) {
        return false;
    }

    const double* requestIdNumber = requestIt->second.TryNumber();
    if (!requestIdNumber) {
        return false;
    }

    const auto valueIt = object->find("value");
    const std::string value = valueIt == object->end() ? std::string() : JsonValueToString(valueIt->second);
    CompleteQuery(static_cast<std::uint32_t>(*requestIdNumber), value);
    return false;
}

bool ArizonaCefDialogs::HandleShowDialog(
    std::uint16_t& dialogId,
    std::uint8_t& style,
    std::string& title,
    std::string& button1,
    std::string& button2,
    std::string& text) {
    {
        std::lock_guard lock(mutex_);
        if (++dialogGeneration_ == 0) {
            dialogGeneration_ = 1;
        }
        lastDialogInfo_.id = dialogId;
        lastDialogInfo_.style = style;
        lastDialogInfo_.title = title;
        lastDialogInfo_.button1 = button1;
        lastDialogInfo_.button2 = button2;
        lastDialogInfo_.text = text;
        pendingClose_ = {};
        cachedInputText_.clear();
        cachedListItem_ = "0";
        cachedListItemsJson_.clear();
        cachedInputFieldPresentKnown_ = false;
        cachedInputFieldPresent_ = false;
        nextInputFieldProbeAtMs_ = 0;
    }

    debuglog::WriteInfo(
        "ArizonaCefDialogs cached dialog id=%u style=%u title=%s",
        dialogId,
        style,
        title.c_str());
    return true;
}

bool ArizonaCefDialogs::HandleSendDialogResponse(
    std::uint16_t& dialogId,
    std::uint8_t& button,
    std::uint16_t& listItem,
    std::string& input) {
    {
        std::lock_guard lock(mutex_);
        lastRespond_.id = dialogId;
        lastRespond_.button = button;
        lastRespond_.list = NormalizeListItem(listItem);
        lastRespond_.input = input;
        pendingClose_ = {};
    }

    debuglog::WriteInfo(
        "ArizonaCefDialogs cached respond id=%u button=%u list=%d inputLen=%llu",
        dialogId,
        button,
        NormalizeListItem(listItem),
        static_cast<unsigned long long>(input.size()));
    return true;
}

bool ArizonaCefDialogs::EvalCef(std::string_view code, bool logFailure) const {
    if (!CanUseCef()) {
        if (logFailure) {
            debuglog::WriteError("ArizonaCefDialogs::EvalCef failed: CEF packet bridge is unavailable");
        }
        return false;
    }
    const std::string gameCode = textencoding::Utf8ToGame(code);
    if (gameCode.size() > 0xFFFF) {
        if (logFailure) {
            debuglog::WriteError("ArizonaCefDialogs::EvalCef failed: code is too long");
        }
        return false;
    }

    BitStream bitStream;
    bitStream.Write(kCefEvalPacketType);
    bitStream.Write(static_cast<std::int32_t>(0));
    bitStream.Write(static_cast<std::uint16_t>(gameCode.size()));
    bitStream.Write(static_cast<std::uint8_t>(0));
    if (!gameCode.empty()) {
        bitStream.Write(gameCode.data(), static_cast<int>(gameCode.size()));
    }

    const bool ok = sampRakHooks_->EmulateIncomingPacket(kArizonaCefPacketId, bitStream);
    if (!ok && logFailure) {
        debuglog::WriteError("ArizonaCefDialogs::EvalCef failed: synthetic packet queue rejected eval");
    }
    return ok;
}

bool ArizonaCefDialogs::EvalAnon(std::string_view code, bool logFailure) const {
    std::string wrapped;
    wrapped.reserve(code.size() + 16);
    wrapped += "(() => {";
    wrapped += code;
    wrapped += "})()";
    return EvalCef(wrapped, logFailure);
}

bool ArizonaCefDialogs::QueueCloseAction(int button, std::uint8_t token) const {
    if ((button != 0 && button != 1) || token > 9) {
        return false;
    }

    std::string js = button == 1
        ? std::string(R"JS(var e=new KeyboardEvent('keydown',{key:'Enter',code:'Enter',bubbles:true});if(e.keyCode!=13)Object.defineProperty(e,'keyCode',{get:()=>13});window.dispatchEvent(e);)JS")
        : std::string(R"JS(if(window.cef&&typeof window.cef.doDialogResponse=='function')window.cef.doDialogResponse();else{var d=document.querySelector('.dialog'),z=d&&d.querySelector('.dialog__button--secondary,.dialog__header-close');if(!z)return;z.click()})JS");
    js += "window.cef&&window.cef.SendMessage('hbo|c";
    js.push_back(button == 1 ? '1' : '0');
    js.push_back(static_cast<char>('0' + token));
    js += "',0)";

    const std::size_t scriptBytes = js.size() + kEvalAnonWrapperBytes;
    const std::size_t packetBytes = scriptBytes + kCefEvalPacketOverheadBytes;
    if (packetBytes > kMaxCloseActionPacketBytes) {
        debuglog::WriteError(
            "ArizonaCefDialogs close action rejected button=%d scriptBytes=%llu packetBytes=%llu packetLimit=%llu",
            button,
            static_cast<unsigned long long>(scriptBytes),
            static_cast<unsigned long long>(packetBytes),
            static_cast<unsigned long long>(kMaxCloseActionPacketBytes));
        return false;
    }
    return EvalAnon(js, true);
}

std::uint32_t ArizonaCefDialogs::BeginQuery(std::string_view code, QueryKind kind, int timeoutMs) {
    const int clampedTimeoutMs = ClampQueryTimeout(timeoutMs);
    std::uint32_t requestId = 0;
    {
        std::lock_guard lock(mutex_);
        if (shutdown_) {
            return 0;
        }
        requestId = ++nextRequestId_;
        pendingQueries_[requestId] = PendingQuery{
            kind,
            GetTickCount64() + static_cast<std::uint64_t>(clampedTimeoutMs),
        };
    }

    const std::string js = std::string(R"JS(
var value;
try { value = (function () { )JS")
        + std::string(code)
        + R"JS( })(); } catch (e) { value = null; }
if (!window.cef || !window.cef.SendMessage) return;
var data = { requestId: )JS"
        + std::to_string(requestId)
        + R"JS(, value: value };
var json = JSON.stringify(data).replace(/[^\x00-\x7F]/g, function (character) {
    return '\\u' + ('0000' + character.charCodeAt(0).toString(16)).slice(-4);
});
window.cef.SendMessage()JS"
        + MakeJsStringLiteral(std::string(kQueryPrefix) + "|")
        + R"JS( + json, 0);
)JS";

    if (!EvalAnon(js, false)) {
        std::lock_guard lock(mutex_);
        pendingQueries_.erase(requestId);
        return 0;
    }

    return requestId;
}

std::string ArizonaCefDialogs::Query(std::string_view code, QueryKind kind, int timeoutMs, std::string fallback) {
    const int clampedTimeoutMs = ClampQueryTimeout(timeoutMs);
    PruneExpiredQueries(GetTickCount64());
    if (!HasPendingQuery(kind)) {
        BeginQuery(code, kind, clampedTimeoutMs);
    }

    return fallback;
}

void ArizonaCefDialogs::CompleteQuery(std::uint32_t requestId, std::string value) {
    std::lock_guard lock(mutex_);
    const auto it = pendingQueries_.find(requestId);
    if (it == pendingQueries_.end()) {
        return;
    }

    const QueryKind kind = it->second.kind;
    pendingQueries_.erase(it);
    if (kind == QueryKind::InputText) {
        cachedInputText_ = std::move(value);
    } else if (kind == QueryKind::ListItem) {
        cachedListItem_ = value.empty() ? std::string("0") : std::move(value);
    } else if (kind == QueryKind::ListItems) {
        cachedListItemsJson_ = std::move(value);
    } else if (kind == QueryKind::InputPresent) {
        cachedInputFieldPresentKnown_ = true;
        cachedInputFieldPresent_ = value == "true" || value == "1";
    }
}

void ArizonaCefDialogs::PruneExpiredQueries(std::uint64_t now) {
    std::lock_guard lock(mutex_);
    for (auto it = pendingQueries_.begin(); it != pendingQueries_.end();) {
        if (it->second.deadlineAtMs <= now) {
            it = pendingQueries_.erase(it);
        } else {
            ++it;
        }
    }
}

bool ArizonaCefDialogs::HasPendingQuery(QueryKind kind) const {
    std::lock_guard lock(mutex_);
    return std::any_of(pendingQueries_.begin(), pendingQueries_.end(), [&](const auto& item) {
        return item.second.kind == kind;
    });
}

bool ArizonaCefDialogs::CanUseCef() const {
    return sampRakHooks_ && sampRakHooks_->IsInstalled();
}

int ArizonaCefDialogs::ClampQueryTimeout(int timeoutMs) {
    if (timeoutMs <= 0) {
        return kDefaultQueryTimeoutMs;
    }
    return std::clamp(timeoutMs, 1, kMaxQueryTimeoutMs);
}

int ArizonaCefDialogs::NormalizeListItem(std::uint16_t value) {
    return value == 0xFFFF ? -1 : static_cast<int>(value);
}

std::string ArizonaCefDialogs::JsonValueToString(const jsonutil::JsonValue& value) {
    if (value.IsNull()) {
        return {};
    }
    if (const bool* boolValue = value.TryBool()) {
        return *boolValue ? "true" : "false";
    }
    if (const double* numberValue = value.TryNumber()) {
        const double rounded = std::round(*numberValue);
        if (std::fabs(*numberValue - rounded) < 0.000001) {
            return std::to_string(static_cast<long long>(rounded));
        }

        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "%.15g", *numberValue);
        return buffer;
    }
    if (const std::string* stringValue = value.TryString()) {
        return *stringValue;
    }

    std::string json;
    jsonutil::WriteJson(value, json);
    return json;
}
