#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace binder_tags {

enum class Action {
    Disable,
    Enable,
    Start,
    Stop,
    Pause,
    Unpause,
    FastMenu,
    UnfastMenu,
    Random,
    Ended,
    Popup,
};

enum class Error {
    None,
    ParamRequired,
    InvalidSyntax,
    TooManyArguments,
    UnterminatedQuote,
    InvalidStableId,
    CategoryNotFound,
    FolderNotFound,
    Ambiguous,
    BindNotFound,
};

struct CategoryEntry {
    std::string id{};
    std::string name{};
    std::vector<std::vector<std::string>> folderPaths{};
};

struct BindEntry {
    int index = -1;
    int number = 0;
    std::string stableId{};
    std::string displayName{};
    std::string categoryId{};
    std::vector<std::string> folderPath{};
    bool enabled = true;
    bool effectivelyEnabled = true;
};

struct Catalog {
    std::vector<CategoryEntry> categories{};
    std::vector<BindEntry> binds{};
};

struct Context {
    int bindIndex = -1;
    std::string categoryId{};
    std::vector<std::string> folderPath{};
};

struct ResolveResult {
    std::vector<int> indices{};
    Error error = Error::None;
};

ResolveResult Resolve(Action action, std::string_view rawParam, const Context& context, const Catalog& catalog);

std::string_view ErrorCode(Error error);
std::string StableSelector(std::string_view stableId);
std::string QuoteToken(std::string_view value);
std::string JoinFolderPath(const std::vector<std::string>& path);

bool EqualNoCaseUtf8(std::string_view left, std::string_view right);
bool ContainsNoCaseUtf8(std::string_view value, std::string_view fragment);
bool IsReservedFolderName(std::string_view name);

} // namespace binder_tags
