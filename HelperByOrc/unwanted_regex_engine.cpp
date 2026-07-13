#include "unwanted_regex_engine.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#define PCRE2_STATIC
#include <pcre2.h>

#include <array>
#include <utility>

namespace unwanted_regex {
namespace {

constexpr std::uint32_t kMatchLimit = 100000;
constexpr std::uint32_t kDepthLimit = 1000;
constexpr std::uint32_t kHeapLimitKiB = 512;

std::string ErrorMessage(int code) {
    std::array<PCRE2_UCHAR, 256> buffer{};
    const int length = pcre2_get_error_message(code, buffer.data(), buffer.size());
    if (length <= 0) {
        return "PCRE2 error " + std::to_string(code);
    }
    return std::string(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(length));
}

} // namespace

struct Program::Impl {
    pcre2_code* code = nullptr;
    pcre2_match_data* matchData = nullptr;
    pcre2_match_context* matchContext = nullptr;

    ~Impl() {
        if (matchContext) {
            pcre2_match_context_free(matchContext);
        }
        if (matchData) {
            pcre2_match_data_free(matchData);
        }
        if (code) {
            pcre2_code_free(code);
        }
    }
};

Program::Program() = default;
Program::Program(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Program::~Program() = default;
Program::Program(Program&&) noexcept = default;
Program& Program::operator=(Program&&) noexcept = default;

MatchResult Program::Match(std::string_view subject) const {
    if (!impl_ || !impl_->code || !impl_->matchData || !impl_->matchContext) {
        return {MatchStatus::Error, PCRE2_ERROR_NULL};
    }

    const int result = pcre2_match(
        impl_->code,
        reinterpret_cast<PCRE2_SPTR>(subject.data()),
        subject.size(),
        0,
        0,
        impl_->matchData,
        impl_->matchContext);
    if (result >= 0) {
        return {MatchStatus::Match, 0};
    }

    switch (result) {
    case PCRE2_ERROR_NOMATCH:
        return {MatchStatus::NoMatch, result};
    case PCRE2_ERROR_MATCHLIMIT:
        return {MatchStatus::MatchLimit, result};
    case PCRE2_ERROR_DEPTHLIMIT:
        return {MatchStatus::DepthLimit, result};
    case PCRE2_ERROR_HEAPLIMIT:
        return {MatchStatus::HeapLimit, result};
    case PCRE2_ERROR_UTF8_ERR1:
    case PCRE2_ERROR_UTF8_ERR2:
    case PCRE2_ERROR_UTF8_ERR3:
    case PCRE2_ERROR_UTF8_ERR4:
    case PCRE2_ERROR_UTF8_ERR5:
    case PCRE2_ERROR_UTF8_ERR6:
    case PCRE2_ERROR_UTF8_ERR7:
    case PCRE2_ERROR_UTF8_ERR8:
    case PCRE2_ERROR_UTF8_ERR9:
    case PCRE2_ERROR_UTF8_ERR10:
    case PCRE2_ERROR_UTF8_ERR11:
    case PCRE2_ERROR_UTF8_ERR12:
    case PCRE2_ERROR_UTF8_ERR13:
    case PCRE2_ERROR_UTF8_ERR14:
    case PCRE2_ERROR_UTF8_ERR15:
    case PCRE2_ERROR_UTF8_ERR16:
    case PCRE2_ERROR_UTF8_ERR17:
    case PCRE2_ERROR_UTF8_ERR18:
    case PCRE2_ERROR_UTF8_ERR19:
    case PCRE2_ERROR_UTF8_ERR20:
    case PCRE2_ERROR_UTF8_ERR21:
        return {MatchStatus::InvalidUtf8, result};
    default:
        return {MatchStatus::Error, result};
    }
}

bool Program::MatchesEmpty() const {
    return Match({}).status == MatchStatus::Match;
}

CompileResult Compile(std::string_view pattern, bool nocase) {
    CompileResult result;
    pcre2_compile_context* compileContext = pcre2_compile_context_create(nullptr);
    if (!compileContext) {
        result.error = "PCRE2 compile context allocation failed";
        return result;
    }

#ifdef PCRE2_EXTRA_NEVER_CALLOUT
    const int extraResult = pcre2_set_compile_extra_options(compileContext, PCRE2_EXTRA_NEVER_CALLOUT);
    if (extraResult != 0) {
        result.error = ErrorMessage(extraResult);
        pcre2_compile_context_free(compileContext);
        return result;
    }
#endif

    std::uint32_t options = PCRE2_UTF | PCRE2_UCP | PCRE2_NEVER_BACKSLASH_C;
    if (nocase) {
        options |= PCRE2_CASELESS;
    }

    int errorCode = 0;
    PCRE2_SIZE errorOffset = 0;
    pcre2_code* code = pcre2_compile(
        reinterpret_cast<PCRE2_SPTR>(pattern.data()),
        pattern.size(),
        options,
        &errorCode,
        &errorOffset,
        compileContext);
    pcre2_compile_context_free(compileContext);
    if (!code) {
        result.error = ErrorMessage(errorCode);
        result.errorOffset = static_cast<std::size_t>(errorOffset);
        return result;
    }

    auto impl = std::make_unique<Program::Impl>();
    impl->code = code;
    impl->matchData = pcre2_match_data_create_from_pattern(code, nullptr);
    impl->matchContext = pcre2_match_context_create(nullptr);
    if (!impl->matchData || !impl->matchContext) {
        result.error = "PCRE2 match state allocation failed";
        return result;
    }

    const int matchLimitResult = pcre2_set_match_limit(impl->matchContext, kMatchLimit);
    const int depthLimitResult = pcre2_set_depth_limit(impl->matchContext, kDepthLimit);
    const int heapLimitResult = pcre2_set_heap_limit(impl->matchContext, kHeapLimitKiB);
    if (matchLimitResult != 0 || depthLimitResult != 0 || heapLimitResult != 0) {
        const int errorCode = matchLimitResult != 0
            ? matchLimitResult
            : depthLimitResult != 0 ? depthLimitResult : heapLimitResult;
        result.error = ErrorMessage(errorCode);
        return result;
    }
    result.program = std::unique_ptr<Program>(new Program(std::move(impl)));
    return result;
}

const char* MatchStatusName(MatchStatus status) {
    switch (status) {
    case MatchStatus::MatchLimit:
        return "match_limit";
    case MatchStatus::DepthLimit:
        return "depth_limit";
    case MatchStatus::HeapLimit:
        return "heap_limit";
    case MatchStatus::InvalidUtf8:
        return "invalid_utf8";
    case MatchStatus::Error:
        return "error";
    case MatchStatus::Match:
        return "match";
    case MatchStatus::NoMatch:
    default:
        return "no_match";
    }
}

} // namespace unwanted_regex
