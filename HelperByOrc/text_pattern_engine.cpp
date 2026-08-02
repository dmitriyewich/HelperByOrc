#include "text_pattern_engine.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#define PCRE2_STATIC
#include <pcre2.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace text_pattern {
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
    std::uint32_t captureCount = 0;
    std::vector<std::string> captureNames{};

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
    return Match(subject, nullptr);
}

void CaptureSnapshot::Clear() {
    programIdentity_ = nullptr;
    ranges_.clear();
}

MatchResult Program::Match(std::string_view subject, CaptureSnapshot* captures) const {
    if (captures) {
        captures->Clear();
    }
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
        if (captures) {
            const PCRE2_SIZE* const offsets = pcre2_get_ovector_pointer(impl_->matchData);
            const std::size_t offsetCount = pcre2_get_ovector_count(impl_->matchData);
            captures->ranges_.resize(static_cast<std::size_t>(impl_->captureCount) + 1);
            for (std::size_t groupIndex = 0; groupIndex <= impl_->captureCount; ++groupIndex) {
                CaptureSnapshot::Range& range = captures->ranges_[groupIndex];
                if (groupIndex >= offsetCount
                    || offsets[groupIndex * 2] == PCRE2_UNSET
                    || offsets[groupIndex * 2 + 1] == PCRE2_UNSET) {
                    range = {};
                    continue;
                }
                range.begin = static_cast<std::size_t>(offsets[groupIndex * 2]);
                range.end = static_cast<std::size_t>(offsets[groupIndex * 2 + 1]);
            }
            captures->programIdentity_ = impl_.get();
        }
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

std::size_t Program::CaptureCount() const {
    return impl_ ? impl_->captureCount : 0;
}

std::string_view Program::CaptureName(std::size_t groupIndex) const {
    if (!impl_ || groupIndex >= impl_->captureNames.size()) {
        return {};
    }
    return impl_->captureNames[groupIndex];
}

std::string_view Program::Capture(
    std::string_view subject,
    const CaptureSnapshot& captures,
    std::size_t groupIndex) const {
    if (!impl_
        || captures.programIdentity_ != impl_.get()
        || groupIndex >= captures.ranges_.size()) {
        return {};
    }
    const CaptureSnapshot::Range& range = captures.ranges_[groupIndex];
    if (range.begin == std::string_view::npos
        || range.end == std::string_view::npos
        || range.begin > range.end
        || range.end > subject.size()) {
        return {};
    }
    return subject.substr(range.begin, range.end - range.begin);
}

std::string_view Program::Capture(
    std::string_view subject,
    const CaptureSnapshot& captures,
    std::string_view selector) const {
    if (selector.empty()) {
        return {};
    }

    std::size_t groupIndex = 0;
    const char* const begin = selector.data();
    const char* const end = begin + selector.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, groupIndex);
    if (parsed.ec == std::errc{} && parsed.ptr == end) {
        return Capture(subject, captures, groupIndex);
    }

    if (!impl_) {
        return {};
    }
    for (std::size_t index = 1; index < impl_->captureNames.size(); ++index) {
        if (impl_->captureNames[index] == selector) {
            return Capture(subject, captures, index);
        }
    }
    return {};
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

    std::uint32_t captureCount = 0;
    const int captureInfoResult = pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &captureCount);
    if (captureInfoResult != 0) {
        result.error = ErrorMessage(captureInfoResult);
        return result;
    }
    impl->captureCount = captureCount;

    std::uint32_t nameCount = 0;
    const int nameCountResult = pcre2_pattern_info(code, PCRE2_INFO_NAMECOUNT, &nameCount);
    if (nameCountResult != 0) {
        result.error = ErrorMessage(nameCountResult);
        return result;
    }
    if (nameCount > 0) {
        impl->captureNames.resize(static_cast<std::size_t>(captureCount) + 1);
        std::uint32_t entrySize = 0;
        PCRE2_SPTR nameTable = nullptr;
        const int entrySizeResult = pcre2_pattern_info(code, PCRE2_INFO_NAMEENTRYSIZE, &entrySize);
        const int nameTableResult = pcre2_pattern_info(code, PCRE2_INFO_NAMETABLE, &nameTable);
        if (entrySizeResult != 0 || nameTableResult != 0 || !nameTable || entrySize < 3) {
            const int infoError = entrySizeResult != 0 ? entrySizeResult : nameTableResult;
            result.error = infoError != 0 ? ErrorMessage(infoError) : "PCRE2 invalid name table";
            return result;
        }

        for (std::uint32_t nameIndex = 0; nameIndex < nameCount; ++nameIndex) {
            const PCRE2_SPTR entry = nameTable + static_cast<std::size_t>(nameIndex) * entrySize;
            const std::size_t groupIndex =
                (static_cast<std::size_t>(entry[0]) << 8) | static_cast<std::size_t>(entry[1]);
            if (groupIndex >= impl->captureNames.size()) {
                result.error = "PCRE2 invalid named capture index";
                return result;
            }
            std::size_t nameLength = 0;
            while (nameLength + 2 < entrySize && entry[nameLength + 2] != 0) {
                ++nameLength;
            }
            impl->captureNames[groupIndex].assign(
                reinterpret_cast<const char*>(entry + 2),
                nameLength);
        }
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

} // namespace text_pattern
