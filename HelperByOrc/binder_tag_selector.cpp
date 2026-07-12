#include "binder_tag_selector.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>

namespace binder_tags {
namespace {

struct Token {
    std::string value{};
    bool quoted = false;
};

struct TokenizeResult {
    std::vector<Token> tokens{};
    Error error = Error::None;
};

std::string Trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

std::wstring FoldUtf8(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int wideLength = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (wideLength <= 0) {
        std::wstring fallback;
        fallback.reserve(value.size());
        for (const unsigned char ch : value) {
            fallback.push_back(static_cast<wchar_t>(std::tolower(ch)));
        }
        return fallback;
    }
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        wide.data(),
        wideLength);
    CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));
    return wide;
}

TokenizeResult Tokenize(std::string_view raw) {
    TokenizeResult result;
    std::size_t i = 0;
    while (i < raw.size()) {
        while (i < raw.size()
            && (raw[i] == ',' || std::isspace(static_cast<unsigned char>(raw[i])) != 0)) {
            ++i;
        }
        if (i >= raw.size()) {
            break;
        }

        if (raw[i] == '"' || raw[i] == '\'') {
            const char quote = raw[i++];
            std::string value;
            bool closed = false;
            bool escaped = false;
            while (i < raw.size()) {
                const char ch = raw[i++];
                if (escaped) {
                    value.push_back(ch);
                    escaped = false;
                } else if (ch == '\\') {
                    escaped = true;
                } else if (ch == quote) {
                    closed = true;
                    break;
                } else {
                    value.push_back(ch);
                }
            }
            if (!closed || escaped) {
                result.error = Error::UnterminatedQuote;
                return result;
            }
            if (i < raw.size() && raw[i] != ','
                && std::isspace(static_cast<unsigned char>(raw[i])) == 0) {
                result.error = Error::InvalidSyntax;
                return result;
            }
            result.tokens.push_back(Token{ std::move(value), true });
            continue;
        }

        const std::size_t begin = i;
        while (i < raw.size() && raw[i] != ','
            && std::isspace(static_cast<unsigned char>(raw[i])) == 0) {
            if (raw[i] == '"' || raw[i] == '\'') {
                result.error = Error::InvalidSyntax;
                return result;
            }
            ++i;
        }
        if (begin == i) {
            result.error = Error::InvalidSyntax;
            return result;
        }
        result.tokens.push_back(Token{ std::string(raw.substr(begin, i - begin)), false });
    }
    return result;
}

bool ParsePositiveInt(std::string_view value, int& result) {
    if (value.empty()) {
        return false;
    }
    int parsed = 0;
    for (const char ch : value) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        if (parsed > (std::numeric_limits<int>::max() - (ch - '0')) / 10) {
            return false;
        }
        parsed = parsed * 10 + (ch - '0');
    }
    if (parsed <= 0) {
        return false;
    }
    result = parsed;
    return true;
}

bool ParseDisplayAlias(std::string_view value, int& number, std::string& displayName) {
    constexpr std::string_view kNumberSign = "\xE2\x84\x96";
    std::string text = Trim(value);
    if (!text.starts_with(kNumberSign)) {
        return false;
    }
    text = Trim(std::string_view(text).substr(kNumberSign.size()));
    std::size_t digits = 0;
    while (digits < text.size() && std::isdigit(static_cast<unsigned char>(text[digits])) != 0) {
        ++digits;
    }
    if (!ParsePositiveInt(std::string_view(text).substr(0, digits), number)) {
        return false;
    }
    displayName = Trim(std::string_view(text).substr(digits));
    return true;
}

bool ParseStableId(std::string_view value, std::string& stableId) {
    constexpr std::string_view kPrefix = "@bind-";
    if (!value.starts_with('@')) {
        return false;
    }
    if (!value.starts_with(kPrefix)) {
        return false;
    }
    int number = 0;
    if (!ParsePositiveInt(value.substr(kPrefix.size()), number)) {
        return false;
    }
    stableId = std::string(value.substr(1));
    return true;
}

std::vector<std::string> SplitPath(std::string_view value) {
    std::vector<std::string> path;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t slash = value.find('/', begin);
        const std::size_t end = slash == std::string_view::npos ? value.size() : slash;
        std::string part = Trim(value.substr(begin, end - begin));
        if (!part.empty()) {
            path.push_back(std::move(part));
        }
        if (slash == std::string_view::npos) {
            break;
        }
        begin = slash + 1;
    }
    return path;
}

bool PathEquals(const std::vector<std::string>& left, const std::vector<std::string>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (!EqualNoCaseUtf8(left[i], right[i])) {
            return false;
        }
    }
    return true;
}

bool PathStartsWith(const std::vector<std::string>& path, const std::vector<std::string>& prefix) {
    if (prefix.size() > path.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (!EqualNoCaseUtf8(path[i], prefix[i])) {
            return false;
        }
    }
    return true;
}

const CategoryEntry* FindCategory(
    const Catalog& catalog,
    std::string_view query,
    bool exact,
    Error& error) {
    const std::string trimmed = Trim(query);
    std::vector<const CategoryEntry*> exactMatches;
    std::vector<const CategoryEntry*> partialMatches;
    for (const CategoryEntry& category : catalog.categories) {
        if (EqualNoCaseUtf8(category.id, trimmed) || EqualNoCaseUtf8(category.name, trimmed)) {
            exactMatches.push_back(&category);
        } else if (!exact
            && (ContainsNoCaseUtf8(category.id, trimmed) || ContainsNoCaseUtf8(category.name, trimmed))) {
            partialMatches.push_back(&category);
        }
    }
    if (exactMatches.size() == 1) {
        return exactMatches.front();
    }
    if (exactMatches.size() > 1 || partialMatches.size() > 1) {
        error = Error::Ambiguous;
        return nullptr;
    }
    if (!exact && partialMatches.size() == 1) {
        return partialMatches.front();
    }
    error = Error::CategoryNotFound;
    return nullptr;
}

std::optional<std::vector<std::string>> FindFolder(
    const CategoryEntry& category,
    std::string_view query,
    bool exact,
    Error& error) {
    const std::vector<std::string> requested = SplitPath(query);
    const bool fullPathRequested = query.find('/') != std::string_view::npos;
    std::vector<const std::vector<std::string>*> exactMatches;
    std::vector<const std::vector<std::string>*> partialMatches;
    for (const auto& path : category.folderPaths) {
        const std::string full = JoinFolderPath(path);
        const std::string_view leaf = path.empty() ? std::string_view{} : std::string_view(path.back());
        const bool matchesExactly = PathEquals(path, requested)
            || EqualNoCaseUtf8(full, query)
            || (!fullPathRequested && EqualNoCaseUtf8(leaf, query));
        if (matchesExactly) {
            exactMatches.push_back(&path);
        } else if (!exact
            && (ContainsNoCaseUtf8(full, query)
                || (!fullPathRequested && ContainsNoCaseUtf8(leaf, query)))) {
            partialMatches.push_back(&path);
        }
    }
    if (exactMatches.size() == 1) {
        return *exactMatches.front();
    }
    if (exactMatches.size() > 1 || partialMatches.size() > 1) {
        error = Error::Ambiguous;
        return std::nullopt;
    }
    if (!exact && partialMatches.size() == 1) {
        return *partialMatches.front();
    }
    error = Error::FolderNotFound;
    return std::nullopt;
}

} // namespace

bool EqualNoCaseUtf8(std::string_view left, std::string_view right) {
    return FoldUtf8(left) == FoldUtf8(right);
}

bool ContainsNoCaseUtf8(std::string_view value, std::string_view fragment) {
    const std::wstring foldedFragment = FoldUtf8(fragment);
    if (foldedFragment.empty()) {
        return true;
    }
    return FoldUtf8(value).find(foldedFragment) != std::wstring::npos;
}

std::string JoinFolderPath(const std::vector<std::string>& path) {
    std::string result;
    for (const std::string& part : path) {
        if (!result.empty()) {
            result.push_back('/');
        }
        result += part;
    }
    return result;
}

std::string QuoteToken(std::string_view value) {
    std::string result = "\"";
    result.reserve(value.size() + 2);
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') {
            result.push_back('\\');
        }
        result.push_back(ch);
    }
    result.push_back('"');
    return result;
}

std::string StableSelector(std::string_view stableId) {
    return stableId.empty() ? std::string{} : "@" + std::string(stableId);
}

bool IsReservedFolderName(std::string_view name) {
    const std::string trimmed = Trim(name);
    return trimmed.empty() || trimmed.find('/') != std::string::npos || trimmed == "*" || trimmed == "**";
}

ResolveResult Resolve(Action action, std::string_view rawParam, const Context& context, const Catalog& catalog) {
    ResolveResult result;
    TokenizeResult parsed = Tokenize(rawParam);
    if (parsed.error != Error::None) {
        result.error = parsed.error;
        return result;
    }
    const std::vector<Token>& tokens = parsed.tokens;
    const std::size_t maximumArgs = action == Action::Random ? 2u : 3u;
    if (tokens.size() > maximumArgs) {
        result.error = Error::TooManyArguments;
        return result;
    }

    if (action != Action::Random && tokens.empty()) {
        if (context.bindIndex < 0) {
            result.error = Error::ParamRequired;
        } else {
            result.indices.push_back(context.bindIndex);
        }
        return result;
    }

    if (action != Action::Random) {
        const Token& first = tokens.front();
        std::string stableId;
        if (!first.quoted && first.value.starts_with('@')) {
            if (!ParseStableId(first.value, stableId)) {
                result.error = Error::InvalidStableId;
                return result;
            }
            if (tokens.size() != 1) {
                result.error = Error::InvalidSyntax;
                return result;
            }
            for (const BindEntry& bind : catalog.binds) {
                if (bind.stableId == stableId) {
                    result.indices.push_back(bind.index);
                    return result;
                }
            }
            result.error = Error::BindNotFound;
            return result;
        }

        int number = 0;
        if (!first.quoted && ParsePositiveInt(first.value, number)) {
            if (tokens.size() != 1) {
                result.error = Error::InvalidSyntax;
                return result;
            }
            for (const BindEntry& bind : catalog.binds) {
                if (bind.number == number) {
                    result.indices.push_back(bind.index);
                }
            }
            result.error = result.indices.empty() ? Error::BindNotFound : Error::None;
            return result;
        }

        std::string aliasName;
        if (ParseDisplayAlias(first.value, number, aliasName)) {
            if (tokens.size() != 1) {
                result.error = Error::InvalidSyntax;
                return result;
            }
            for (const BindEntry& bind : catalog.binds) {
                if (bind.number == number
                    && (aliasName.empty() || EqualNoCaseUtf8(bind.displayName, aliasName))) {
                    result.indices.push_back(bind.index);
                }
            }
            result.error = result.indices.empty() ? Error::BindNotFound : Error::None;
            return result;
        }
    }

    const bool isRandom = action == Action::Random;
    bool anyFolder = false;
    bool recursive = false;
    bool folderProvided = false;
    bool rootOnly = false;
    Token folderToken;
    std::optional<Token> categoryToken;
    std::string targetName;
    bool targetExact = false;

    if (isRandom) {
        if (tokens.empty()) {
            if (context.bindIndex < 0) {
                result.error = Error::ParamRequired;
                return result;
            }
            folderProvided = true;
            rootOnly = context.folderPath.empty();
            folderToken = Token{ JoinFolderPath(context.folderPath), true };
        } else if (tokens[0].value == "*") {
            anyFolder = true;
            if (tokens.size() == 2) {
                categoryToken = tokens[1];
            }
        } else {
            folderProvided = true;
            folderToken = tokens[0];
            rootOnly = folderToken.value.empty();
            constexpr std::string_view kRecursiveSuffix = "/**";
            if (folderToken.value.ends_with(kRecursiveSuffix)) {
                recursive = true;
                folderToken.value.erase(folderToken.value.size() - kRecursiveSuffix.size());
                if (Trim(folderToken.value).empty()) {
                    result.error = Error::InvalidSyntax;
                    return result;
                }
            }
            if (tokens.size() == 2) {
                categoryToken = tokens[1];
            }
        }
    } else {
        targetName = tokens[0].value;
        targetExact = tokens[0].quoted;
        if (tokens.size() >= 2) {
            folderProvided = true;
            folderToken = tokens[1];
            rootOnly = folderToken.value.empty();
            anyFolder = folderToken.value == "*";
        }
        if (tokens.size() == 3) {
            categoryToken = tokens[2];
        }
    }

    Error lookupError = Error::None;
    const CategoryEntry* category = nullptr;
    if (categoryToken.has_value()) {
        if (Trim(categoryToken->value).empty()) {
            result.error = Error::CategoryNotFound;
            return result;
        }
        category = FindCategory(catalog, categoryToken->value, categoryToken->quoted, lookupError);
    } else {
        for (const CategoryEntry& candidate : catalog.categories) {
            if (candidate.id == context.categoryId) {
                category = &candidate;
                break;
            }
        }
    }
    if (!category) {
        result.error = lookupError == Error::None ? Error::CategoryNotFound : lookupError;
        return result;
    }

    std::optional<std::vector<std::string>> folderPath;
    if (folderProvided && !anyFolder) {
        if (rootOnly) {
            folderPath = std::vector<std::string>{};
        } else {
            folderPath = FindFolder(*category, folderToken.value, folderToken.quoted, lookupError);
            if (!folderPath.has_value()) {
                result.error = lookupError;
                return result;
            }
        }
    }

    std::vector<int> candidates;
    for (const BindEntry& bind : catalog.binds) {
        if (bind.categoryId != category->id) {
            continue;
        }
        if (folderPath.has_value()) {
            const bool folderMatches = recursive
                ? PathStartsWith(bind.folderPath, *folderPath)
                : PathEquals(bind.folderPath, *folderPath);
            if (!folderMatches) {
                continue;
            }
        }
        candidates.push_back(bind.index);
    }

    if (isRandom) {
        result.indices = std::move(candidates);
        if (result.indices.empty()) {
            result.error = Error::BindNotFound;
        }
        return result;
    }

    std::vector<int> exactMatches;
    std::vector<int> partialMatches;
    for (const int index : candidates) {
        const auto it = std::find_if(catalog.binds.begin(), catalog.binds.end(), [&](const BindEntry& bind) {
            return bind.index == index;
        });
        if (it == catalog.binds.end()) {
            continue;
        }
        if (EqualNoCaseUtf8(it->displayName, targetName)) {
            exactMatches.push_back(index);
        } else if (!targetExact && ContainsNoCaseUtf8(it->displayName, targetName)) {
            partialMatches.push_back(index);
        }
    }
    if (exactMatches.size() == 1) {
        result.indices = std::move(exactMatches);
        return result;
    }
    if (exactMatches.size() > 1 || partialMatches.size() > 1) {
        result.error = Error::Ambiguous;
        return result;
    }
    if (!targetExact && partialMatches.size() == 1) {
        result.indices = std::move(partialMatches);
        return result;
    }
    result.error = Error::BindNotFound;
    return result;
}

std::string_view ErrorCode(Error error) {
    switch (error) {
    case Error::None: return {};
    case Error::ParamRequired: return "param_required";
    case Error::InvalidSyntax: return "invalid_syntax";
    case Error::TooManyArguments: return "too_many_args";
    case Error::UnterminatedQuote: return "unterminated_quote";
    case Error::InvalidStableId: return "invalid_stable_id";
    case Error::CategoryNotFound: return "category_not_found";
    case Error::FolderNotFound: return "folder_not_found";
    case Error::Ambiguous: return "bind_ambiguous";
    case Error::BindNotFound: return "bind_not_found";
    }
    return "invalid_syntax";
}

} // namespace binder_tags
