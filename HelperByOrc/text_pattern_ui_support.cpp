#include "text_pattern_ui_support.h"

#include <algorithm>
#include <array>

namespace text_pattern_ui {
namespace {

constexpr std::array kReferenceItems{
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"(\A\z)", UiText::UnwantedRegexRefEmptyMessage},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"(\A[ ]+\z)", UiText::UnwantedRegexRefSpacesOnly},
    ReferenceItem{UiText::UnwantedRegexRefCategoryBasic, R"(\A)", UiText::UnwantedRegexRefAbsoluteStart},
    ReferenceItem{UiText::UnwantedRegexRefCategoryBasic, R"(\z)", UiText::UnwantedRegexRefAbsoluteEnd},
    ReferenceItem{UiText::UnwantedRegexRefCategoryBasic, R"(\Q...\E)", UiText::UnwantedRegexRefQuotedLiteral},
    ReferenceItem{UiText::UnwantedRegexRefCategoryBasic, R"(\.)", UiText::UnwantedRegexRefEscapedMeta},
    ReferenceItem{UiText::UnwantedRegexRefCategoryBasic, ".", UiText::UnwantedRegexRefAnyChar},
    ReferenceItem{UiText::UnwantedRegexRefCategoryClasses, "[abc]", UiText::UnwantedRegexRefClass},
    ReferenceItem{UiText::UnwantedRegexRefCategoryClasses, "[^abc]", UiText::UnwantedRegexRefNegatedClass},
    ReferenceItem{UiText::UnwantedRegexRefCategoryClasses, "[a-z]", UiText::UnwantedRegexRefRange},
    ReferenceItem{UiText::UnwantedRegexRefCategoryClasses, R"(\d)", UiText::UnwantedRegexRefUnicodeDigit},
    ReferenceItem{UiText::UnwantedRegexRefCategoryClasses, R"(\p{L})", UiText::UnwantedRegexRefUnicodeLetter},
    ReferenceItem{UiText::UnwantedRegexRefCategoryClasses, R"(\s)", UiText::UnwantedRegexRefWhitespace},
    ReferenceItem{UiText::UnwantedRegexRefCategoryClasses, R"(\h)", UiText::UnwantedRegexRefHorizontalWhitespace},
    ReferenceItem{UiText::UnwantedRegexRefCategoryClasses, R"(\w)", UiText::UnwantedRegexRefWordChar},
    ReferenceItem{UiText::UnwantedRegexRefCategoryClasses, R"(\b)", UiText::UnwantedRegexRefWordBoundary},
    ReferenceItem{UiText::UnwantedRegexRefCategoryQuantifiers, "?", UiText::UnwantedRegexRefOptional},
    ReferenceItem{UiText::UnwantedRegexRefCategoryQuantifiers, "*", UiText::UnwantedRegexRefZeroOrMore},
    ReferenceItem{UiText::UnwantedRegexRefCategoryQuantifiers, "+", UiText::UnwantedRegexRefOneOrMore},
    ReferenceItem{UiText::UnwantedRegexRefCategoryQuantifiers, "{3}", UiText::UnwantedRegexRefExactCount},
    ReferenceItem{UiText::UnwantedRegexRefCategoryQuantifiers, "{1,4}", UiText::UnwantedRegexRefRangeCount},
    ReferenceItem{UiText::UnwantedRegexRefCategoryQuantifiers, "*?", UiText::UnwantedRegexRefLazy},
    ReferenceItem{UiText::UnwantedRegexRefCategoryGroups, "(?:...)", UiText::UnwantedRegexRefNonCapturingGroup},
    ReferenceItem{UiText::UnwantedRegexRefCategoryGroups, "(a|b)", UiText::UnwantedRegexRefAlternation},
    ReferenceItem{UiText::UnwantedRegexRefCategoryGroups, "(?=...)", UiText::UnwantedRegexRefPositiveLookahead},
    ReferenceItem{UiText::UnwantedRegexRefCategoryGroups, "(?!...)", UiText::UnwantedRegexRefNegativeLookahead},
    ReferenceItem{UiText::UnwantedRegexRefCategoryGroups, "(?<=a)", UiText::UnwantedRegexRefPositiveLookbehind},
    ReferenceItem{UiText::UnwantedRegexRefCategoryGroups, "(?<!a)", UiText::UnwantedRegexRefNegativeLookbehind},
    ReferenceItem{UiText::UnwantedRegexRefCategoryGroups, "(?>...)", UiText::UnwantedRegexRefAtomicGroup},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"(\[[0-9]{1,4}\])", UiText::UnwantedTokenPlayerIdHelp},
    ReferenceItem{
        UiText::UnwantedRegexRefCategoryReady,
        R"(\{[0-9A-Fa-f]{6}(?:[0-9A-Fa-f]{2})?\})",
        UiText::UnwantedTokenColorHelp,
        true},
    ReferenceItem{
        UiText::UnwantedRegexRefCategoryReady,
        R"((?=[A-Za-z0-9_]{3,24}(?:[^A-Za-z0-9_]|\z))[A-Za-z0-9]+_[A-Za-z0-9]+)",
        UiText::UnwantedTokenNicknameHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"([0-9]+)", UiText::UnwantedTokenIntegerHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"([+-]?[0-9]+)", UiText::UnwantedTokenIntegerHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"([+-]?[0-9]+(?:[.,][0-9]+)?)", UiText::UnwantedTokenDecimalHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"([+-]?(?:[0-9]{1,3}(?:[ .,'’][0-9]{3})+|[0-9]+)(?:[.,][0-9]+)?)", UiText::UnwantedTokenGroupedNumberHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"([+-]?[0-9]+(?:[.,][0-9]+)?%)", UiText::UnwantedTokenPercentageHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"([+-]?[0-9]+(?:[.,][0-9]+)?[kKкК]{1,2}[0-9]*)", UiText::UnwantedTokenCompactAmountHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"([+-]?\$(?:[0-9]{1,3}(?:[ .,'’][0-9]{3})+|[0-9]+)(?:[.,][0-9]+)?)", UiText::UnwantedTokenMoneyHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"([+-]?\$[0-9]+(?:[.,][0-9]+)?[kKкК]{1,2}[0-9]*)", UiText::UnwantedTokenMoneyHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"((?:[01][0-9]|2[0-3]):[0-5][0-9])", UiText::UnwantedTokenClockHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"((?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9])", UiText::UnwantedTokenClockSecondsHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"([0-9]{1,3}:[0-5][0-9])", UiText::UnwantedTokenDurationHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"([0-9]{1,6}(?:[.,][0-9]+)?\h*(?:секунд(?:а|ы)?|сек\.?|минут(?:а|ы)?|мин\.?|час(?:а|ов)?|дн(?:я|ей|и)|день|дней|недел(?:я|и|ь)|месяц(?:а|ев)?))", UiText::UnwantedTokenDurationWordsHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"((?:0[1-9]|[12][0-9]|3[01])[./-](?:0[1-9]|1[0-2])[./-](?:[0-9]{2}|(?:19|20)[0-9]{2}))", UiText::UnwantedTokenDateDmyHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"((?:19|20)[0-9]{2}-(?:0[1-9]|1[0-2])-(?:0[1-9]|[12][0-9]|3[01]))", UiText::UnwantedTokenDateYmdHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"(\[[^\]\r\n]{1,64}\])", UiText::UnwantedTokenBracketPrefixHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"([^\]\r\n]{1,64})", UiText::TextPatternTokenBracketInnerHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"([\p{L}\p{N} _-]{1,64})", UiText::TextPatternTokenUnicodePhraseHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"("[^"\r\n]{1,160}")", UiText::TextPatternTokenDoubleQuotedHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"('[^'\r\n]{1,160}')", UiText::TextPatternTokenSingleQuotedHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"(\([^()\r\n]{1,160}\))", UiText::TextPatternTokenParenthesizedHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"(/[A-Za-z0-9_]{1,32})", UiText::TextPatternTokenSlashCommandHelp},
    ReferenceItem{UiText::UnwantedRegexRefCategoryReady, R"(\h+)", UiText::TextPatternTokenHorizontalWhitespaceHelp},
    ReferenceItem{
        UiText::UnwantedRegexRefCategoryReady,
        R"((?:https?://)?(?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?\.)+[A-Za-z]{2,63}(?::(?:[0-9]{1,4}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?(?:/(?:[^\s\]\)}>]*[^\s\]\)}>,;!?])?)?)",
        UiText::UnwantedTokenDomainHelp},
};

} // namespace

std::span<const ReferenceItem> ReferenceItems() {
    return kReferenceItems;
}

UiText TokenLabel(text_pattern_builder::TokenKind kind) {
    using text_pattern_builder::TokenKind;
    switch (kind) {
    case TokenKind::Literal: return UiText::TextPatternTokenLiteral;
    case TokenKind::Color: return UiText::UnwantedTokenColor;
    case TokenKind::PlayerId: return UiText::UnwantedTokenPlayerId;
    case TokenKind::BracketPrefix: return UiText::UnwantedTokenBracketPrefix;
    case TokenKind::Nickname: return UiText::UnwantedTokenNickname;
    case TokenKind::Integer: return UiText::UnwantedTokenInteger;
    case TokenKind::Decimal: return UiText::UnwantedTokenDecimal;
    case TokenKind::GroupedNumber: return UiText::UnwantedTokenGroupedNumber;
    case TokenKind::Percentage: return UiText::UnwantedTokenPercentage;
    case TokenKind::CompactAmount: return UiText::UnwantedTokenCompactAmount;
    case TokenKind::Money: return UiText::UnwantedTokenMoney;
    case TokenKind::Clock: return UiText::UnwantedTokenClock;
    case TokenKind::ClockSeconds: return UiText::UnwantedTokenClockSeconds;
    case TokenKind::Duration: return UiText::UnwantedTokenDuration;
    case TokenKind::DurationWords: return UiText::UnwantedTokenDurationWords;
    case TokenKind::DateDmy: return UiText::UnwantedTokenDateDmy;
    case TokenKind::DateYmd: return UiText::UnwantedTokenDateYmd;
    case TokenKind::Domain: return UiText::UnwantedTokenDomain;
    case TokenKind::BracketInner: return UiText::TextPatternTokenBracketInner;
    case TokenKind::UnicodePhrase: return UiText::TextPatternTokenUnicodePhrase;
    case TokenKind::NonSpace: return UiText::TextPatternTokenNonSpace;
    case TokenKind::LineText: return UiText::TextPatternTokenLineText;
    case TokenKind::DoubleQuoted: return UiText::TextPatternTokenDoubleQuoted;
    case TokenKind::SingleQuoted: return UiText::TextPatternTokenSingleQuoted;
    case TokenKind::Parenthesized: return UiText::TextPatternTokenParenthesized;
    case TokenKind::SlashCommand: return UiText::TextPatternTokenSlashCommand;
    case TokenKind::HorizontalWhitespace: return UiText::TextPatternTokenHorizontalWhitespace;
    default: return UiText::Count;
    }
}

UiText TokenHelp(text_pattern_builder::TokenKind kind) {
    using text_pattern_builder::TokenKind;
    switch (kind) {
    case TokenKind::Literal: return UiText::TextPatternTokenLiteralHelp;
    case TokenKind::Color: return UiText::UnwantedTokenColorHelp;
    case TokenKind::PlayerId: return UiText::UnwantedTokenPlayerIdHelp;
    case TokenKind::BracketPrefix: return UiText::UnwantedTokenBracketPrefixHelp;
    case TokenKind::Nickname: return UiText::UnwantedTokenNicknameHelp;
    case TokenKind::Integer: return UiText::UnwantedTokenIntegerHelp;
    case TokenKind::Decimal: return UiText::UnwantedTokenDecimalHelp;
    case TokenKind::GroupedNumber: return UiText::UnwantedTokenGroupedNumberHelp;
    case TokenKind::Percentage: return UiText::UnwantedTokenPercentageHelp;
    case TokenKind::CompactAmount: return UiText::UnwantedTokenCompactAmountHelp;
    case TokenKind::Money: return UiText::UnwantedTokenMoneyHelp;
    case TokenKind::Clock: return UiText::UnwantedTokenClockHelp;
    case TokenKind::ClockSeconds: return UiText::UnwantedTokenClockSecondsHelp;
    case TokenKind::Duration: return UiText::UnwantedTokenDurationHelp;
    case TokenKind::DurationWords: return UiText::UnwantedTokenDurationWordsHelp;
    case TokenKind::DateDmy: return UiText::UnwantedTokenDateDmyHelp;
    case TokenKind::DateYmd: return UiText::UnwantedTokenDateYmdHelp;
    case TokenKind::Domain: return UiText::UnwantedTokenDomainHelp;
    case TokenKind::BracketInner: return UiText::TextPatternTokenBracketInnerHelp;
    case TokenKind::UnicodePhrase: return UiText::TextPatternTokenUnicodePhraseHelp;
    case TokenKind::NonSpace: return UiText::TextPatternTokenNonSpaceHelp;
    case TokenKind::LineText: return UiText::TextPatternTokenLineTextHelp;
    case TokenKind::DoubleQuoted: return UiText::TextPatternTokenDoubleQuotedHelp;
    case TokenKind::SingleQuoted: return UiText::TextPatternTokenSingleQuotedHelp;
    case TokenKind::Parenthesized: return UiText::TextPatternTokenParenthesizedHelp;
    case TokenKind::SlashCommand: return UiText::TextPatternTokenSlashCommandHelp;
    case TokenKind::HorizontalWhitespace: return UiText::TextPatternTokenHorizontalWhitespaceHelp;
    default: return UiText::Count;
    }
}

bool ContainsBroadWildcard(std::string_view pattern) {
    bool escaped = false;
    bool inClass = false;
    bool inQuotedLiteral = false;
    for (std::size_t i = 0; i + 1 < pattern.size(); ++i) {
        const char ch = pattern[i];
        if (!inClass && ch == '\\' && pattern[i + 1] == (inQuotedLiteral ? 'E' : 'Q')) {
            inQuotedLiteral = !inQuotedLiteral;
            ++i;
            escaped = false;
            continue;
        }
        if (inQuotedLiteral) {
            continue;
        }
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '[') {
            inClass = true;
            continue;
        }
        if (ch == ']' && inClass) {
            inClass = false;
            continue;
        }
        if (!inClass && ch == '.' && (pattern[i + 1] == '*' || pattern[i + 1] == '+')) {
            return true;
        }
    }
    return false;
}

std::size_t Utf8CharacterOffset(std::string_view value, std::size_t byteOffset) {
    byteOffset = std::min(byteOffset, value.size());
    std::size_t characters = 0;
    for (std::size_t i = 0; i < byteOffset;) {
        const unsigned char lead = static_cast<unsigned char>(value[i]);
        std::size_t size = 1;
        if ((lead & 0xE0) == 0xC0) size = 2;
        else if ((lead & 0xF0) == 0xE0) size = 3;
        else if ((lead & 0xF8) == 0xF0) size = 4;
        i += std::min(size, byteOffset - i);
        ++characters;
    }
    return characters;
}

std::string FormatCompilePosition(std::string_view pattern, std::size_t byteOffset) {
    UiSettings& ui = UiSettings::Instance();
    byteOffset = std::min(byteOffset, pattern.size());
    const std::size_t lineStart = byteOffset == 0
        ? std::string_view::npos
        : pattern.rfind('\n', byteOffset - 1);
    const std::size_t actualStart = lineStart == std::string_view::npos ? 0 : lineStart + 1;
    const std::size_t lineEnd = pattern.find('\n', byteOffset);
    const std::size_t actualEnd = lineEnd == std::string_view::npos ? pattern.size() : lineEnd;
    std::string line(pattern.substr(actualStart, actualEnd - actualStart));
    constexpr std::size_t kMaxContextBytes = 96;
    std::size_t caretByte = byteOffset - actualStart;
    if (line.size() > kMaxContextBytes) {
        const std::size_t contextStart = caretByte > kMaxContextBytes / 2 ? caretByte - kMaxContextBytes / 2 : 0;
        line = line.substr(contextStart, kMaxContextBytes);
        caretByte -= contextStart;
        if (contextStart > 0) {
            line.insert(0, "...");
            caretByte += 3;
        }
    }
    const std::size_t caretChars = Utf8CharacterOffset(line, std::min(caretByte, line.size()));
    return ui.Format(
        UiText::UnwantedPcrePositionDetail,
        std::to_string(Utf8CharacterOffset(pattern, byteOffset) + 1).c_str(),
        line.c_str(),
        std::string(caretChars, ' ').c_str());
}

void AppendWarning(std::string& warning, std::string_view message) {
    if (message.empty()) {
        return;
    }
    if (!warning.empty()) {
        warning.push_back(' ');
    }
    warning.append(message);
}

} // namespace text_pattern_ui
