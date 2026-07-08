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
#include <string>
#include <utility>

namespace {

constexpr std::uint8_t kArizonaCefPacketId = 220;
constexpr std::uint8_t kCefEvalPacketType = 17;
constexpr std::uint8_t kCefSendMessagePacketType = 18;
constexpr std::string_view kQueryPrefix = "helperbyorc-arizona-cef-dialogs";
constexpr int kDefaultQueryTimeoutMs = 500;
constexpr int kMaxQueryTimeoutMs = 3000;

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

    const std::string js = std::string(R"JS(
var d = document.querySelector('.dialog');
if (!d) return;
if ()JS")
        + std::to_string(button)
        + R"JS() {
    var primary = d.querySelector('.dialog__button--primary:not(.dialog__button--disabled)');
    if (primary) { primary.click(); return; }
} else {
    var secondary = d.querySelector('.dialog__button--secondary:not(.dialog__button--disabled)');
    if (secondary) { secondary.click(); return; }
    var close = d.querySelector('.dialog__header-close');
    if (close) { close.click(); return; }
    if (window.cef && window.cef.doDialogResponse) window.cef.doDialogResponse();
}
)JS";
    return EvalAnon(js, true);
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

bool ArizonaCefDialogs::HasPendingInputTextQuery() const {
    return HasPendingQuery(QueryKind::InputText);
}

bool ArizonaCefDialogs::HasPendingListItemQuery() const {
    return HasPendingQuery(QueryKind::ListItem);
}

int ArizonaCefDialogs::LastDialogId() const {
    std::lock_guard lock(mutex_);
    return lastDialogInfo_.id;
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
    const std::string prefix = std::string(kQueryPrefix) + "|";
    if (!StartsWith(message, prefix)) {
        return true;
    }

    const std::string_view payload(message.data() + prefix.size(), message.size() - prefix.size());
    std::string error;
    const std::optional<jsonutil::JsonValue> parsed = jsonutil::ParseJson(payload, error);
    if (!parsed.has_value()) {
        debuglog::WriteError("ArizonaCefDialogs query response JSON parse failed: %s", error.c_str());
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
        lastDialogInfo_.id = dialogId;
        lastDialogInfo_.style = style;
        lastDialogInfo_.title = title;
        lastDialogInfo_.button1 = button1;
        lastDialogInfo_.button2 = button2;
        lastDialogInfo_.text = text;
        cachedInputText_.clear();
        cachedListItem_ = "0";
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
window.cef.SendMessage()JS"
        + MakeJsStringLiteral(std::string(kQueryPrefix) + "|")
        + R"JS( + JSON.stringify(data), 0);
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
