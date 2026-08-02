#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace text_pattern_builder {

enum class TokenKind {
    Literal,
    Color,
    PlayerId,
    BracketPrefix,
    Nickname,
    Integer,
    Decimal,
    GroupedNumber,
    Percentage,
    CompactAmount,
    Money,
    Clock,
    ClockSeconds,
    Duration,
    DurationWords,
    DateDmy,
    DateYmd,
    Domain,
    BracketInner,
    UnicodePhrase,
    NonSpace,
    LineText,
    DoubleQuoted,
    SingleQuoted,
    Parenthesized,
    SlashCommand,
    HorizontalWhitespace,
};

enum class Confidence {
    Exact,
    Recommended,
    Broad,
};

struct Options {
    bool colors = true;
    bool playerIds = true;
    bool bracketPrefixes = true;
    bool nicknames = true;
    bool numbers = true;
    bool money = true;
    bool time = true;
    bool domains = true;
};

struct Token {
    TokenKind kind = TokenKind::Integer;
    std::size_t offset = 0;
    std::size_t length = 0;
    std::string source;
    std::string pattern;
};

struct Result {
    std::string exact;
    std::string recommended;
    std::string contains;
    std::vector<Token> tokens;
    std::string error;
};

struct Suggestion {
    TokenKind kind = TokenKind::Literal;
    std::string pattern;
    Confidence confidence = Confidence::Broad;
};

struct SelectionResult {
    std::string source;
    std::vector<Suggestion> suggestions;
    std::string error;
};

struct Replacement {
    std::size_t offset = 0;
    std::size_t length = 0;
    std::string pattern;
};

Result Build(std::string_view sample, const Options& options);
SelectionResult SuggestSelection(
    std::string_view sample,
    std::size_t selectionStart,
    std::size_t selectionEnd);
std::string BuildWithReplacements(
    std::string_view sample,
    std::span<const Replacement> replacements,
    bool anchored,
    std::string& error);
} // namespace text_pattern_builder

// Transitional alias for the existing Unwanted Messages implementation.
// New shared consumers should use text_pattern_builder directly.
namespace unwanted_regex_builder = text_pattern_builder;
