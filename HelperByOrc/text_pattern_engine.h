#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace text_pattern {

struct CompileResult;

enum class MatchStatus {
    NoMatch,
    Match,
    MatchLimit,
    DepthLimit,
    HeapLimit,
    InvalidUtf8,
    Error,
};

struct MatchResult {
    MatchStatus status = MatchStatus::NoMatch;
    int errorCode = 0;
};

class Program final {
public:
    Program();
    ~Program();
    Program(Program&&) noexcept;
    Program& operator=(Program&&) noexcept;

    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;

    MatchResult Match(std::string_view subject) const;
    bool MatchesEmpty() const;

private:
    struct Impl;
    explicit Program(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;

    friend struct CompileResult;
    friend CompileResult Compile(std::string_view pattern, bool nocase);
};

struct CompileResult {
    std::unique_ptr<Program> program;
    std::string error;
    std::size_t errorOffset = 0;
};

CompileResult Compile(std::string_view pattern, bool nocase);
const char* MatchStatusName(MatchStatus status);

} // namespace text_pattern

// Transitional alias for the existing Unwanted Messages implementation.
// New shared consumers should use text_pattern directly.
namespace unwanted_regex = text_pattern;
