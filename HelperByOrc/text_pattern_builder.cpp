#include "text_pattern_builder.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

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

bool ReadNumber(std::string_view value, std::size_t offset, std::size_t& length, TokenKind& kind, std::string& pattern) {
    length = 0;
    if (offset >= value.size()) {
        return false;
    }
    std::size_t end = offset;
    if ((value[end] == '+' || value[end] == '-') && end + 1 < value.size()
        && std::isdigit(static_cast<unsigned char>(value[end + 1]))) {
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
    } else if (end < value.size() && (value[end] == 'k' || value[end] == 'K')) {
        ++end;
        while (end < value.size() && std::isdigit(static_cast<unsigned char>(value[end]))) {
            ++end;
        }
        kind = TokenKind::CompactAmount;
        pattern = "[+-]?[0-9]+(?:[.,][0-9]+)?[kK][0-9]*";
    } else {
        kind = decimal ? TokenKind::Decimal : TokenKind::Integer;
        pattern = decimal ? "[+-]?[0-9]+(?:[.,][0-9]+)?" : "[+-]?[0-9]+";
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
    for (std::size_t i = 0; i < sample.size();) {
        std::size_t scalarLength = 0;
        if (!ReadUtf8Scalar(sample, i, scalarLength)) {
            result.error = "invalid_utf8";
            return result;
        }
        i += scalarLength;
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
        } else if (options.time && ReadClock(sample, i, length)) {
            AppendToken(result, TokenKind::Clock, sample, i, length, "(?:[01][0-9]|2[0-3]):[0-5][0-9]");
        } else if (options.time && ReadDuration(sample, i, length)) {
            AppendToken(result, TokenKind::Duration, sample, i, length, "[0-9]{1,3}:[0-5][0-9]");
        } else if (options.nicknames && ReadNickname(sample, i, length)) {
            AppendToken(result, TokenKind::Nickname, sample, i, length,
                "(?=[A-Za-z0-9_]{3,24}(?:[^A-Za-z0-9_]|\\z))[A-Za-z0-9]+_[A-Za-z0-9]+");
        } else if (options.bracketPrefixes && ReadBracketPrefix(sample, i, length)) {
            AppendToken(result, TokenKind::BracketPrefix, sample, i, length, "\\[[^\\]\\r\\n]{1,64}\\]");
        } else if (options.money && sample[i] == '$' && i + 1 < sample.size()) {
            std::size_t numberLength = 0;
            TokenKind numberKind{};
            std::string numberPattern;
            if (ReadNumber(sample, i + 1, numberLength, numberKind, numberPattern)) {
                if (numberKind == TokenKind::Percentage) {
                    --numberLength;
                    numberPattern = "[+-]?[0-9]+(?:[.,][0-9]+)?";
                }
                length = numberLength + 1;
                AppendToken(result, TokenKind::Money, sample, i, length, "\\$" + numberPattern);
            }
        } else if (options.numbers) {
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

} // namespace text_pattern_builder
