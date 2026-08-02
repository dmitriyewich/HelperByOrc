#include "text_pattern_builder.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <utility>

namespace text_pattern_builder {
namespace {

bool IsAsciiAlphaNumeric(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0;
}

bool IsHex(char ch) {
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

bool IsEscapedMeta(char ch) {
    switch (ch) {
    case '\\':
    case '^':
    case '$':
    case '.':
    case '|':
    case '?':
    case '*':
    case '+':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
        return true;
    default:
        return false;
    }
}

bool ReadUtf8Scalar(std::string_view value, std::size_t offset, std::size_t& length) {
    length = 0;
    if (offset >= value.size()) {
        return false;
    }
    const unsigned char lead = static_cast<unsigned char>(value[offset]);
    if (lead < 0x80) {
        length = 1;
        return true;
    }
    std::size_t expected = 0;
    std::uint32_t codepoint = 0;
    std::uint32_t minimum = 0;
    if ((lead & 0xE0) == 0xC0) {
        expected = 2;
        codepoint = lead & 0x1F;
        minimum = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        expected = 3;
        codepoint = lead & 0x0F;
        minimum = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        expected = 4;
        codepoint = lead & 0x07;
        minimum = 0x10000;
    } else {
        return false;
    }
    if (offset + expected > value.size()) {
        return false;
    }
    for (std::size_t i = 1; i < expected; ++i) {
        const unsigned char tail = static_cast<unsigned char>(value[offset + i]);
        if ((tail & 0xC0) != 0x80) {
            return false;
        }
        codepoint = (codepoint << 6) | (tail & 0x3F);
    }
    if (codepoint < minimum || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        return false;
    }
    length = expected;
    return true;
}

bool IsValidUtf8(std::string_view value) {
    for (std::size_t offset = 0; offset < value.size();) {
        std::size_t length = 0;
        if (!ReadUtf8Scalar(value, offset, length)) {
            return false;
        }
        offset += length;
    }
    return true;
}

bool IsUtf8Boundary(std::string_view value, std::size_t offset) {
    return offset <= value.size()
        && (offset == value.size()
            || (static_cast<unsigned char>(value[offset]) & 0xC0) != 0x80);
}

std::string Escape(std::string_view value) {
    std::string out;
    out.reserve(value.size() * 2);
    for (char ch : value) {
        if (IsEscapedMeta(ch)) {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

bool ReadColor(std::string_view value, std::size_t offset, std::size_t& length) {
    length = 0;
    if (offset >= value.size() || value[offset] != '{') {
        return false;
    }
    const std::size_t close = value.find('}', offset + 1);
    if (close == std::string_view::npos) {
        return false;
    }
    const std::size_t digits = close - offset - 1;
    if (digits != 6 && digits != 8) {
        return false;
    }
    if (!std::all_of(value.begin() + offset + 1, value.begin() + close, IsHex)) {
        return false;
    }
    length = digits + 2;
    return true;
}

bool ReadPlayerId(std::string_view value, std::size_t offset, std::size_t& length) {
    length = 0;
    if (offset + 3 > value.size() || value[offset] != '[' || !std::isdigit(static_cast<unsigned char>(value[offset + 1]))) {
        return false;
    }
    std::size_t end = offset + 1;
    while (end < value.size() && std::isdigit(static_cast<unsigned char>(value[end])) && end - offset <= 4) {
        ++end;
    }
    if (end >= value.size() || value[end] != ']' || end == offset + 1) {
        return false;
    }
    length = end - offset + 1;
    return true;
}

bool ReadBracketPrefix(std::string_view value, std::size_t offset, std::size_t& length) {
    length = 0;
    if (offset != 0 || value.empty() || value.front() != '[') {
        return false;
    }
    const std::size_t close = value.find(']', 1);
    if (close == std::string_view::npos || close <= 1 || close > 64 || value.substr(1, close - 1).find_first_of("\r\n") != std::string_view::npos) {
        return false;
    }
    length = close + 1;
    return true;
}

bool ReadClock(std::string_view value, std::size_t offset, std::size_t& length) {
    length = 0;
    if ((offset > 0 && std::isdigit(static_cast<unsigned char>(value[offset - 1])))
        || (offset + 5 < value.size() && std::isdigit(static_cast<unsigned char>(value[offset + 5])))) {
        return false;
    }
    if (offset + 5 > value.size() || value[offset + 2] != ':') {
        return false;
    }
    for (std::size_t i : {0u, 1u, 3u, 4u}) {
        if (!std::isdigit(static_cast<unsigned char>(value[offset + i]))) {
            return false;
        }
    }
    const int hour = (value[offset] - '0') * 10 + value[offset + 1] - '0';
    const int minute = (value[offset + 3] - '0') * 10 + value[offset + 4] - '0';
    if (hour > 23 || minute > 59) {
        return false;
    }
    length = 5;
    return true;
}

bool ReadClockSeconds(std::string_view value, std::size_t offset, std::size_t& length) {
    length = 0;
    if ((offset > 0 && std::isdigit(static_cast<unsigned char>(value[offset - 1])))
        || (offset + 8 < value.size() && std::isdigit(static_cast<unsigned char>(value[offset + 8])))) {
        return false;
    }
    if (offset + 8 > value.size() || value[offset + 2] != ':' || value[offset + 5] != ':') {
        return false;
    }
    for (std::size_t i : {0u, 1u, 3u, 4u, 6u, 7u}) {
        if (!std::isdigit(static_cast<unsigned char>(value[offset + i]))) {
            return false;
        }
    }
    const int hour = (value[offset] - '0') * 10 + value[offset + 1] - '0';
    const int minute = (value[offset + 3] - '0') * 10 + value[offset + 4] - '0';
    const int second = (value[offset + 6] - '0') * 10 + value[offset + 7] - '0';
    if (hour > 23 || minute > 59 || second > 59) {
        return false;
    }
    length = 8;
    return true;
}

bool ReadFixedDigits(std::string_view value, std::size_t offset, std::size_t count) {
    return offset + count <= value.size()
        && std::all_of(
            value.begin() + offset,
            value.begin() + offset + count,
            [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; });
}

bool ReadDateDmy(std::string_view value, std::size_t offset, std::size_t& length) {
    length = 0;
    if (offset > 0 && std::isdigit(static_cast<unsigned char>(value[offset - 1]))) {
        return false;
    }
    if (!ReadFixedDigits(value, offset, 2) || offset + 8 > value.size()) {
        return false;
    }
    const char separator = value[offset + 2];
    if ((separator != '.' && separator != '/' && separator != '-')
        || value[offset + 5] != separator
        || !ReadFixedDigits(value, offset + 3, 2)) {
        return false;
    }
    const std::size_t yearDigits = ReadFixedDigits(value, offset + 6, 4) ? 4u : 2u;
    if (!ReadFixedDigits(value, offset + 6, yearDigits)) {
        return false;
    }
    if (yearDigits == 4
        && value.substr(offset + 6, 2) != "19"
        && value.substr(offset + 6, 2) != "20") {
        return false;
    }
    const int day = (value[offset] - '0') * 10 + value[offset + 1] - '0';
    const int month = (value[offset + 3] - '0') * 10 + value[offset + 4] - '0';
    if (day < 1 || day > 31 || month < 1 || month > 12) {
        return false;
    }
    length = 6 + yearDigits;
    if (offset + length < value.size()
        && std::isdigit(static_cast<unsigned char>(value[offset + length]))) {
        length = 0;
        return false;
    }
    return true;
}

bool ReadDateYmd(std::string_view value, std::size_t offset, std::size_t& length) {
    length = 0;
    if (offset > 0 && std::isdigit(static_cast<unsigned char>(value[offset - 1]))) {
        return false;
    }
    if (!ReadFixedDigits(value, offset, 4)
        || offset + 10 > value.size()
        || value[offset + 4] != '-'
        || value[offset + 7] != '-'
        || !ReadFixedDigits(value, offset + 5, 2)
        || !ReadFixedDigits(value, offset + 8, 2)) {
        return false;
    }
    const int month = (value[offset + 5] - '0') * 10 + value[offset + 6] - '0';
    const int day = (value[offset + 8] - '0') * 10 + value[offset + 9] - '0';
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }
    if (value.substr(offset, 2) != "19" && value.substr(offset, 2) != "20") {
        return false;
    }
    length = 10;
    if (offset + length < value.size()
        && std::isdigit(static_cast<unsigned char>(value[offset + length]))) {
        length = 0;
        return false;
    }
    return true;
}

bool ReadDuration(std::string_view value, std::size_t offset, std::size_t& length) {
    length = 0;
    std::size_t colon = offset;
    while (colon < value.size() && std::isdigit(static_cast<unsigned char>(value[colon])) && colon - offset < 3) {
        ++colon;
    }
    if (colon == offset || colon >= value.size() || value[colon] != ':' || colon + 3 > value.size()) {
        return false;
    }
    if (!std::isdigit(static_cast<unsigned char>(value[colon + 1]))
        || !std::isdigit(static_cast<unsigned char>(value[colon + 2]))) {
        return false;
    }
    const int minute = (value[colon + 1] - '0') * 10 + value[colon + 2] - '0';
    if (minute > 59) {
        return false;
    }
    length = colon + 3 - offset;
    return true;
}

bool ReadNickname(std::string_view value, std::size_t offset, std::size_t& length) {
    length = 0;
    if (offset >= value.size() || !IsAsciiAlphaNumeric(value[offset])) {
        return false;
    }
    if (offset > 0 && (IsAsciiAlphaNumeric(value[offset - 1]) || value[offset - 1] == '_')) {
        return false;
    }
    std::size_t end = offset;
    std::size_t underscores = 0;
    while (end < value.size() && (IsAsciiAlphaNumeric(value[end]) || value[end] == '_') && end - offset < 24) {
        underscores += value[end] == '_' ? 1u : 0u;
        ++end;
    }
    if (underscores != 1 || end - offset < 3 || value[offset] == '_' || value[end - 1] == '_') {
        return false;
    }
    if (end < value.size() && (IsAsciiAlphaNumeric(value[end]) || value[end] == '_')) {
        return false;
    }
    length = end - offset;
    return true;
}

bool ReadDomain(std::string_view value, std::size_t offset, std::size_t& length) {
    length = 0;
    std::size_t start = offset;
    if (offset > 0
        && (IsAsciiAlphaNumeric(value[offset - 1]) || value[offset - 1] == '-' || value[offset - 1] == '.')) {
        return false;
    }
    if (value.substr(offset, 7) == "http://") {
        offset += 7;
    } else if (value.substr(offset, 8) == "https://") {
        offset += 8;
    }
    if (offset >= value.size() || !IsAsciiAlphaNumeric(value[offset])) {
        return false;
    }
    std::size_t end = offset;
    while (end < value.size() && (IsAsciiAlphaNumeric(value[end]) || value[end] == '-' || value[end] == '.')) {
        ++end;
    }
    while (end > offset && value[end - 1] == '.') {
        --end;
    }
    if (end <= offset + 3) {
        return false;
    }
    const std::size_t lastDot = value.rfind('.', end - 1);
    if (lastDot == std::string_view::npos || lastDot < offset || end - lastDot - 1 < 2) {
        return false;
    }
    for (std::size_t i = lastDot + 1; i < end; ++i) {
        if (!std::isalpha(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }
    std::size_t labelStart = offset;
    while (labelStart < end) {
        const std::size_t labelEnd = value.find('.', labelStart);
        const std::size_t actualEnd = labelEnd == std::string_view::npos || labelEnd > end ? end : labelEnd;
        const std::size_t labelLength = actualEnd - labelStart;
        if (labelLength == 0 || labelLength > 63
            || !IsAsciiAlphaNumeric(value[labelStart])
            || !IsAsciiAlphaNumeric(value[actualEnd - 1])) {
            return false;
        }
        if (actualEnd == end) {
            break;
        }
        labelStart = actualEnd + 1;
    }
    if (end < value.size() && value[end] == ':') {
        const std::size_t colon = end;
        const std::size_t portStart = colon + 1;
        std::size_t portEnd = portStart;
        while (portEnd < value.size() && std::isdigit(static_cast<unsigned char>(value[portEnd]))) {
            ++portEnd;
        }
        const std::size_t portLength = portEnd - portStart;
        unsigned int port = 0;
        for (std::size_t i = portStart; i < portEnd && port <= 65535; ++i) {
            port = port * 10 + static_cast<unsigned int>(value[i] - '0');
        }
        if (portLength >= 1 && portLength <= 5 && port <= 65535) {
            end = portEnd;
        }
    }
    if (end < value.size() && value[end] == '/') {
        ++end;
        while (end < value.size() && !std::isspace(static_cast<unsigned char>(value[end]))) {
            if (value[end] == ')' || value[end] == ']' || value[end] == '}' || value[end] == '>') {
                break;
            }
            ++end;
        }
        while (end > start && std::string_view(".,!?:;").find(value[end - 1]) != std::string_view::npos) {
            --end;
        }
    }
    length = end - start;
    return true;
}

bool ReadGroupedNumber(
    std::string_view value,
    std::size_t offset,
    std::size_t& length,
    bool& signedValue) {
    length = 0;
    signedValue = false;
    if (offset > 0 && std::isdigit(static_cast<unsigned char>(value[offset - 1]))) {
        return false;
    }
    std::size_t cursor = offset;
    if (cursor < value.size() && (value[cursor] == '+' || value[cursor] == '-')) {
        signedValue = true;
        ++cursor;
    }
    const std::size_t firstGroup = cursor;
    while (cursor < value.size()
        && std::isdigit(static_cast<unsigned char>(value[cursor]))
        && cursor - firstGroup < 3) {
        ++cursor;
    }
    if (cursor == firstGroup || cursor >= value.size()) {
        return false;
    }
    const char separator = value[cursor];
    if (separator != ' ' && separator != '.' && separator != ',' && separator != '\'') {
        return false;
    }
    std::size_t groups = 0;
    while (cursor < value.size() && value[cursor] == separator) {
        ++cursor;
        if (!ReadFixedDigits(value, cursor, 3)) {
            return false;
        }
        cursor += 3;
        ++groups;
    }
    if (groups == 0 || (cursor < value.size() && std::isdigit(static_cast<unsigned char>(value[cursor])))) {
        return false;
    }
    if (cursor + 1 < value.size()
        && (value[cursor] == '.' || value[cursor] == ',')
        && std::isdigit(static_cast<unsigned char>(value[cursor + 1]))) {
        ++cursor;
        while (cursor < value.size() && std::isdigit(static_cast<unsigned char>(value[cursor]))) {
            ++cursor;
        }
    }
    length = cursor - offset;
    return true;
}

bool ReadDurationWords(std::string_view value, std::size_t offset, std::size_t& length) {
    length = 0;
    std::size_t cursor = offset;
    while (cursor < value.size() && std::isdigit(static_cast<unsigned char>(value[cursor])) && cursor - offset < 6) {
        ++cursor;
    }
    if (cursor == offset) {
        return false;
    }
    if (cursor + 1 < value.size() && (value[cursor] == '.' || value[cursor] == ',')
        && std::isdigit(static_cast<unsigned char>(value[cursor + 1]))) {
        ++cursor;
        while (cursor < value.size() && std::isdigit(static_cast<unsigned char>(value[cursor]))) {
            ++cursor;
        }
    }
    while (cursor < value.size() && (value[cursor] == ' ' || value[cursor] == '\t')) {
        ++cursor;
    }
    constexpr std::string_view suffixes[]{
        "секунда", "секунды", "секунд", "сек.", "сек",
        "минута", "минуты", "минут", "мин.", "мин",
        "часов", "часа", "час", "дней", "день", "дня", "дни",
        "неделя", "недели", "недель", "месяцев", "месяца", "месяц",
    };
    for (const std::string_view suffix : suffixes) {
        if (value.substr(cursor, suffix.size()) == suffix) {
            const std::size_t end = cursor + suffix.size();
            if (end == value.size()
                || (static_cast<unsigned char>(value[end]) < 0x80
                    && !IsAsciiAlphaNumeric(value[end])
                    && value[end] != '_')) {
                length = end - offset;
                return true;
            }
        }
    }
    return false;
}

bool ReadNumber(std::string_view value, std::size_t offset, std::size_t& length, TokenKind& kind, std::string& pattern) {
    length = 0;
    if (offset >= value.size()) {
        return false;
    }
    std::size_t end = offset;
    bool signedValue = false;
    if ((value[end] == '+' || value[end] == '-') && end + 1 < value.size()
        && std::isdigit(static_cast<unsigned char>(value[end + 1]))) {
        signedValue = true;
        ++end;
    }
    if (!std::isdigit(static_cast<unsigned char>(value[end]))) {
        return false;
    }
    while (end < value.size() && std::isdigit(static_cast<unsigned char>(value[end]))) {
        ++end;
    }
    bool decimal = false;
    if (end + 1 < value.size() && (value[end] == '.' || value[end] == ',')
        && std::isdigit(static_cast<unsigned char>(value[end + 1]))) {
        decimal = true;
        ++end;
        while (end < value.size() && std::isdigit(static_cast<unsigned char>(value[end]))) {
            ++end;
        }
    }
    if (end < value.size() && value[end] == '%') {
        ++end;
        kind = TokenKind::Percentage;
        pattern = "[+-]?[0-9]+(?:[.,][0-9]+)?%";
    } else if (end < value.size()
        && (value[end] == 'k' || value[end] == 'K'
            || value.substr(end, 2) == "к" || value.substr(end, 2) == "К")) {
        const bool asciiSuffix = value[end] == 'k' || value[end] == 'K';
        end += asciiSuffix ? 1u : 2u;
        if (!asciiSuffix && (value.substr(end, 2) == "к" || value.substr(end, 2) == "К")) {
            end += 2;
        }
        while (end < value.size() && std::isdigit(static_cast<unsigned char>(value[end]))) {
            ++end;
        }
        kind = TokenKind::CompactAmount;
        pattern = "[+-]?[0-9]+(?:[.,][0-9]+)?[kKкК]{1,2}[0-9]*";
    } else {
        kind = decimal ? TokenKind::Decimal : TokenKind::Integer;
        const char* sign = signedValue ? "[+-]?" : "";
        pattern = decimal
            ? std::string(sign) + "[0-9]+(?:[.,][0-9]+)?"
            : std::string(sign) + "[0-9]+";
    }
    length = end - offset;
    return true;
}

void AppendToken(Result& result, TokenKind kind, std::string_view sample, std::size_t offset, std::size_t length, std::string pattern) {
    result.recommended += pattern;
    result.tokens.push_back(Token{kind, offset, length, std::string(sample.substr(offset, length)), std::move(pattern)});
}

} // namespace

Result Build(std::string_view sample, const Options& options) {
    Result result;
    if (sample.empty()) {
        return result;
    }
    if (!IsValidUtf8(sample)) {
        result.error = "invalid_utf8";
        return result;
    }
    result.exact = "\\A" + Escape(sample) + "\\z";

    for (std::size_t i = 0; i < sample.size();) {
        std::size_t length = 0;
        if (options.playerIds && ReadPlayerId(sample, i, length)) {
            AppendToken(result, TokenKind::PlayerId, sample, i, length, "\\[[0-9]{1,4}\\]");
        } else if (options.colors && ReadColor(sample, i, length)) {
            AppendToken(result, TokenKind::Color, sample, i, length, "\\{[0-9A-Fa-f]{6}(?:[0-9A-Fa-f]{2})?\\}");
        } else if (options.domains && ReadDomain(sample, i, length)) {
            AppendToken(result, TokenKind::Domain, sample, i, length,
                "(?:https?://)?(?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?\\.)+[A-Za-z]{2,63}"
                "(?::(?:[0-9]{1,4}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?"
                "(?:/(?:[^\\s\\]\\)}>]*[^\\s\\]\\)}>,;!?])?)?");
        } else if (options.time && ReadClockSeconds(sample, i, length)) {
            AppendToken(result, TokenKind::ClockSeconds, sample, i, length, "(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]");
        } else if (options.time && ReadDateYmd(sample, i, length)) {
            AppendToken(result, TokenKind::DateYmd, sample, i, length,
                "(?:19|20)[0-9]{2}-(?:0[1-9]|1[0-2])-(?:0[1-9]|[12][0-9]|3[01])");
        } else if (options.time && ReadDateDmy(sample, i, length)) {
            AppendToken(result, TokenKind::DateDmy, sample, i, length, "(?:0[1-9]|[12][0-9]|3[01])[./-](?:0[1-9]|1[0-2])[./-](?:[0-9]{2}|(?:19|20)[0-9]{2})");
        } else if (options.time && ReadClock(sample, i, length)) {
            AppendToken(result, TokenKind::Clock, sample, i, length, "(?:[01][0-9]|2[0-3]):[0-5][0-9]");
        } else if (options.time && ReadDuration(sample, i, length)) {
            AppendToken(result, TokenKind::Duration, sample, i, length, "[0-9]{1,3}:[0-5][0-9]");
        } else if (options.nicknames && ReadNickname(sample, i, length)) {
            AppendToken(result, TokenKind::Nickname, sample, i, length,
                "(?=[A-Za-z0-9_]{3,24}(?:[^A-Za-z0-9_]|\\z))[A-Za-z0-9]+_[A-Za-z0-9]+");
        } else if (options.bracketPrefixes && ReadBracketPrefix(sample, i, length)) {
            AppendToken(result, TokenKind::BracketPrefix, sample, i, length, "\\[[^\\]\\r\\n]{1,64}\\]");
        } else if (options.money
            && (sample[i] == '$'
                || ((sample[i] == '+' || sample[i] == '-') && i + 1 < sample.size() && sample[i + 1] == '$'))) {
            const std::size_t dollarOffset = sample[i] == '$' ? i : i + 1;
            std::size_t numberLength = 0;
            TokenKind numberKind{};
            std::string numberPattern;
            bool groupedSigned = false;
            const bool grouped = ReadGroupedNumber(
                sample,
                dollarOffset + 1,
                numberLength,
                groupedSigned);
            if (grouped) {
                numberKind = TokenKind::GroupedNumber;
                numberPattern = "(?:[0-9]{1,3}(?:[ .,'’][0-9]{3})+|[0-9]+)(?:[.,][0-9]+)?";
            }
            if (grouped || ReadNumber(sample, dollarOffset + 1, numberLength, numberKind, numberPattern)) {
                if (numberKind == TokenKind::Percentage) {
                    --numberLength;
                    numberPattern = "[+-]?[0-9]+(?:[.,][0-9]+)?";
                }
                length = numberLength + 1 + (dollarOffset - i);
                const bool signAfterDollar = !numberPattern.empty() && numberPattern.rfind("[+-]?", 0) == 0;
                if (signAfterDollar) {
                    numberPattern.erase(0, 5);
                }
                AppendToken(
                    result,
                    TokenKind::Money,
                    sample,
                    i,
                    length,
                    std::string(signAfterDollar ? "\\$[+-]?" : "[+-]?\\$") + numberPattern);
            }
        } else if (options.numbers) {
            bool groupedSigned = false;
            if (ReadGroupedNumber(sample, i, length, groupedSigned)) {
                AppendToken(result, TokenKind::GroupedNumber, sample, i, length,
                    std::string(groupedSigned ? "[+-]?" : "")
                        + "(?:[0-9]{1,3}(?:[ .,'’][0-9]{3})+|[0-9]+)(?:[.,][0-9]+)?");
                i += length;
                continue;
            }
            TokenKind numberKind{};
            std::string numberPattern;
            if (ReadNumber(sample, i, length, numberKind, numberPattern)) {
                AppendToken(result, numberKind, sample, i, length, std::move(numberPattern));
            }
        }

        if (length > 0) {
            i += length;
            continue;
        }

        std::size_t charSize = 0;
        if (!ReadUtf8Scalar(sample, i, charSize)) {
            result = {};
            result.error = "invalid_utf8";
            return result;
        }
        result.recommended += Escape(sample.substr(i, charSize));
        i += charSize;
    }

    result.recommended = "\\A" + result.recommended + "\\z";
    result.contains = result.recommended.substr(2, result.recommended.size() - 4);
    return result;
}

SelectionResult SuggestSelection(
    std::string_view sample,
    std::size_t selectionStart,
    std::size_t selectionEnd) {
    SelectionResult result;
    if (!IsValidUtf8(sample)
        || selectionStart >= selectionEnd
        || selectionEnd > sample.size()
        || !IsUtf8Boundary(sample, selectionStart)
        || !IsUtf8Boundary(sample, selectionEnd)) {
        result.error = "invalid_selection";
        return result;
    }

    const std::string_view selected = sample.substr(selectionStart, selectionEnd - selectionStart);
    result.source.assign(selected);
    const auto add = [&](TokenKind kind, std::string pattern, Confidence confidence) {
        if (std::none_of(result.suggestions.begin(), result.suggestions.end(), [&](const Suggestion& existing) {
                return existing.pattern == pattern;
            })) {
            result.suggestions.push_back(Suggestion{kind, std::move(pattern), confidence});
        }
    };

    std::size_t length = 0;
    if (ReadColor(selected, 0, length) && length == selected.size()) {
        add(TokenKind::Color, "\\{[0-9A-Fa-f]{6}(?:[0-9A-Fa-f]{2})?\\}", Confidence::Recommended);
    }
    if (ReadPlayerId(selected, 0, length) && length == selected.size()) {
        add(TokenKind::PlayerId, "\\[[0-9]{1,4}\\]", Confidence::Recommended);
    }
    if (selected.front() == '[' && selected.back() == ']' && selected.size() > 2) {
        add(TokenKind::BracketPrefix, "\\[[^\\]\\r\\n]{1,64}\\]", Confidence::Recommended);
    }
    if (selectionStart > 0 && selectionEnd < sample.size()
        && sample[selectionStart - 1] == '[' && sample[selectionEnd] == ']') {
        add(TokenKind::BracketInner, "[^\\]\\r\\n]{1,64}", Confidence::Recommended);
    }
    if (ReadClockSeconds(selected, 0, length) && length == selected.size()) {
        add(TokenKind::ClockSeconds, "(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]", Confidence::Recommended);
    } else if (ReadClock(selected, 0, length) && length == selected.size()) {
        add(TokenKind::Clock, "(?:[01][0-9]|2[0-3]):[0-5][0-9]", Confidence::Recommended);
    } else if (ReadDuration(selected, 0, length) && length == selected.size()) {
        add(TokenKind::Duration, "[0-9]{1,3}:[0-5][0-9]", Confidence::Recommended);
    }
    if (ReadDateDmy(selected, 0, length) && length == selected.size()) {
        add(TokenKind::DateDmy, "(?:0[1-9]|[12][0-9]|3[01])[./-](?:0[1-9]|1[0-2])[./-](?:[0-9]{2}|(?:19|20)[0-9]{2})", Confidence::Recommended);
    }
    if (ReadDateYmd(selected, 0, length) && length == selected.size()) {
        add(TokenKind::DateYmd,
            "(?:19|20)[0-9]{2}-(?:0[1-9]|1[0-2])-(?:0[1-9]|[12][0-9]|3[01])",
            Confidence::Recommended);
    }
    if (ReadDurationWords(selected, 0, length) && length == selected.size()) {
        add(TokenKind::DurationWords,
            "[0-9]{1,6}(?:[.,][0-9]+)?\\h*(?:секунд(?:а|ы)?|сек\\.?|минут(?:а|ы)?|мин\\.?|час(?:а|ов)?|дн(?:я|ей|и)|день|дней|недел(?:я|и|ь)|месяц(?:а|ев)?)",
            Confidence::Recommended);
    }
    bool groupedSigned = false;
    if (ReadGroupedNumber(selected, 0, length, groupedSigned) && length == selected.size()) {
        add(TokenKind::GroupedNumber,
            "[+-]?(?:[0-9]{1,3}(?:[ .,'’][0-9]{3})+|[0-9]+)(?:[.,][0-9]+)?",
            Confidence::Recommended);
    }
    TokenKind numberKind{};
    std::string numberPattern;
    if (ReadNumber(selected, 0, length, numberKind, numberPattern) && length == selected.size()) {
        add(numberKind, std::move(numberPattern), Confidence::Recommended);
    }
    std::size_t moneyOffset = 0;
    if (selected.size() >= 2 && (selected.front() == '+' || selected.front() == '-') && selected[1] == '$') {
        moneyOffset = 1;
    }
    if (moneyOffset < selected.size() && selected[moneyOffset] == '$' && moneyOffset + 1 < selected.size()) {
        std::size_t moneyLength = 0;
        bool moneyGroupedSigned = false;
        TokenKind moneyNumberKind{};
        std::string moneyNumberPattern;
        const bool groupedMoney = ReadGroupedNumber(
            selected,
            moneyOffset + 1,
            moneyLength,
            moneyGroupedSigned);
        if (groupedMoney) {
            moneyNumberPattern = "(?:[0-9]{1,3}(?:[ .,'’][0-9]{3})+|[0-9]+)(?:[.,][0-9]+)?";
        }
        if (groupedMoney
            || ReadNumber(selected, moneyOffset + 1, moneyLength, moneyNumberKind, moneyNumberPattern)) {
            const bool signAfterDollar = moneyNumberPattern.rfind("[+-]?", 0) == 0;
            if (signAfterDollar) {
                moneyNumberPattern.erase(0, 5);
            }
            if (moneyOffset + 1 + moneyLength == selected.size()) {
                add(
                    TokenKind::Money,
                    std::string(signAfterDollar ? "\\$[+-]?" : "[+-]?\\$") + moneyNumberPattern,
                    Confidence::Recommended);
            }
        }
    }
    if (ReadNickname(selected, 0, length) && length == selected.size()) {
        add(
            TokenKind::Nickname,
            "(?=[A-Za-z0-9_]{3,24}(?:[^A-Za-z0-9_]|\\z))[A-Za-z0-9]+_[A-Za-z0-9]+",
            Confidence::Recommended);
    }
    if (ReadDomain(selected, 0, length) && length == selected.size()) {
        add(
            TokenKind::Domain,
            "(?:https?://)?(?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?\\.)+[A-Za-z]{2,63}"
            "(?::(?:[0-9]{1,4}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?"
            "(?:/(?:[^\\s\\]\\)}>]*[^\\s\\]\\)}>,;!?])?)?",
            Confidence::Broad);
    }
    if (selected.size() >= 2 && selected.front() == '"' && selected.back() == '"') {
        add(TokenKind::DoubleQuoted, "\"[^\"\\r\\n]{1,160}\"", Confidence::Recommended);
    }
    if (selected.size() >= 2 && selected.front() == '\'' && selected.back() == '\'') {
        add(TokenKind::SingleQuoted, "'[^'\\r\\n]{1,160}'", Confidence::Recommended);
    }
    if (selected.size() >= 2 && selected.front() == '(' && selected.back() == ')') {
        add(TokenKind::Parenthesized, "\\([^()\\r\\n]{1,160}\\)", Confidence::Recommended);
    }
    if (selected.front() == '/' && selected.size() <= 33
        && std::all_of(selected.begin() + 1, selected.end(), [](char ch) {
            return IsAsciiAlphaNumeric(ch) || ch == '_';
        })) {
        add(TokenKind::SlashCommand, "/[A-Za-z0-9_]{1,32}", Confidence::Recommended);
    }
    const bool horizontalSpace = std::all_of(selected.begin(), selected.end(), [](char ch) {
        return ch == ' ' || ch == '\t';
    });
    if (horizontalSpace) {
        add(TokenKind::HorizontalWhitespace, "\\h+", Confidence::Recommended);
    }
    if (selected.find_first_of("\r\n") == std::string_view::npos && selected.size() <= 256) {
        const bool phrase = selected.size() <= 64
            && std::all_of(selected.begin(), selected.end(), [](char ch) {
                const unsigned char byte = static_cast<unsigned char>(ch);
                return byte >= 0x80 || IsAsciiAlphaNumeric(ch) || ch == ' ' || ch == '_' || ch == '-';
            })
            && std::any_of(selected.begin(), selected.end(), [](char ch) {
                const unsigned char byte = static_cast<unsigned char>(ch);
                return byte >= 0x80 || std::isalpha(byte) != 0;
            });
        if (phrase) {
            add(TokenKind::UnicodePhrase, "[\\p{L}\\p{N} _-]{1,64}", Confidence::Recommended);
        }
        if (selected.find_first_of(" \t") == std::string_view::npos) {
            add(TokenKind::NonSpace, "\\S+", Confidence::Broad);
        }
        add(TokenKind::LineText, "[^\\r\\n]{1,256}", Confidence::Broad);
    }
    add(TokenKind::Literal, Escape(selected), Confidence::Exact);
    return result;
}

std::string BuildWithReplacements(
    std::string_view sample,
    std::span<const Replacement> replacements,
    bool anchored,
    std::string& error) {
    error.clear();
    if (!IsValidUtf8(sample)) {
        error = "invalid_utf8";
        return {};
    }
    std::vector<Replacement> ordered(replacements.begin(), replacements.end());
    std::sort(ordered.begin(), ordered.end(), [](const Replacement& left, const Replacement& right) {
        return left.offset < right.offset;
    });
    std::size_t cursor = 0;
    for (const Replacement& replacement : ordered) {
        if (replacement.pattern.empty()
            || replacement.length == 0
            || replacement.offset > sample.size()
            || replacement.length > sample.size() - replacement.offset
            || !IsUtf8Boundary(sample, replacement.offset)
            || !IsUtf8Boundary(sample, replacement.offset + replacement.length)) {
            error = "invalid_replacement";
            return {};
        }
        if (replacement.offset < cursor) {
            error = "overlapping_replacements";
            return {};
        }
        cursor = replacement.offset + replacement.length;
    }

    std::string pattern;
    pattern.reserve(sample.size() * 2 + 4);
    if (anchored) {
        pattern += "\\A";
    }
    cursor = 0;
    for (const Replacement& replacement : ordered) {
        pattern += Escape(sample.substr(cursor, replacement.offset - cursor));
        pattern += replacement.pattern;
        cursor = replacement.offset + replacement.length;
    }
    pattern += Escape(sample.substr(cursor));
    if (anchored) {
        pattern += "\\z";
    }
    return pattern;
}

} // namespace text_pattern_builder
