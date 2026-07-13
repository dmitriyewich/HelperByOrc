#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace text_pattern_builder {

enum class TokenKind {
    Color,
    PlayerId,
    BracketPrefix,
    Nickname,
    Integer,
    Decimal,
    Percentage,
    CompactAmount,
    Money,
    Clock,
    Duration,
    Domain,
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

Result Build(std::string_view sample, const Options& options);
} // namespace text_pattern_builder

// Transitional alias for the existing Unwanted Messages implementation.
// New shared consumers should use text_pattern_builder directly.
namespace unwanted_regex_builder = text_pattern_builder;
