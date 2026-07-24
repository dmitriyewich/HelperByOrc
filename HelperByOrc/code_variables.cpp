#include "code_variables.h"

#include "app_config.h"
#include "debug_log.h"
#include "lua_bridge.h"
#include "text_encoding.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <exception>
#include <mutex>
#include <system_error>
#include <utility>

namespace codevars {
namespace {

namespace fs = std::filesystem;

constexpr std::uint64_t kTelemetryWindowMs = 5000;
constexpr std::uint64_t kFailureLogThrottleMs = 5000;
constexpr std::uint64_t kMaxLuaFileBytes = 1024 * 1024;
constexpr double kInt64MinAsDouble = -9223372036854775808.0;
constexpr double kInt64ExclusiveMaxAsDouble = 9223372036854775808.0;

double PerfNowMs() {
    static const double multiplier = [] {
        LARGE_INTEGER frequency{};
        return QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0
            ? 1000.0 / static_cast<double>(frequency.QuadPart)
            : 0.0;
    }();
    if (multiplier <= 0.0) {
        return static_cast<double>(GetTickCount64());
    }
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) * multiplier;
}

std::string ToLowerAscii(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char ch : value) {
        lowered.push_back(static_cast<char>(
            ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch));
    }
    return lowered;
}

bool IsValidVariableName(std::string_view value) {
    if (value.empty() || value.size() > kMaxVariableNameBytes) {
        return false;
    }
    for (const unsigned char ch : value) {
        const bool asciiAlphaNumeric = (ch >= 'A' && ch <= 'Z')
            || (ch >= 'a' && ch <= 'z')
            || (ch >= '0' && ch <= '9');
        if (!asciiAlphaNumeric && ch != '_') {
            return false;
        }
    }
    return true;
}

std::optional<std::string> NormalizeProviderText(std::string_view value, std::size_t limit) {
    std::string utf8 = textencoding::GameToUtf8(value);
    if (utf8.size() > limit || !textencoding::IsUtf8(utf8)) {
        return std::nullopt;
    }
    return utf8;
}

std::string WideToUtf8(const fs::path& path) {
    const std::wstring value = path.wstring();
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            size,
            nullptr,
            nullptr)
        != size) {
        return {};
    }
    return result;
}

std::string NormalizedRelativePath(const fs::path& root, const fs::path& path) {
    std::error_code error;
    fs::path relative = fs::relative(path, root, error);
    if (error) {
        return {};
    }
    std::string utf8 = WideToUtf8(relative.lexically_normal());
    std::replace(utf8.begin(), utf8.end(), '\\', '/');
    return ToLowerAscii(utf8);
}

bool HasSuffix(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size()
        && value.substr(value.size() - suffix.size()) == suffix;
}

bool CacheKeyBelongsToProvider(std::string_view key, std::string_view providerId) {
    const std::string prefix = std::to_string(providerId.size()) + ":" + std::string(providerId) + "|";
    return key.starts_with(prefix);
}

bool CacheKeyBelongsToVariable(
    std::string_view key,
    std::string_view providerId,
    std::string_view normalizedName) {
    const std::string providerPrefix = std::to_string(providerId.size()) + ":" + std::string(providerId) + "|";
    if (!key.starts_with(providerPrefix)) {
        return false;
    }
    const std::string_view rest = key.substr(providerPrefix.size());
    if (rest.size() < 2 || rest[1] != '|') {
        return false;
    }
    const std::string namePrefix = std::to_string(normalizedName.size()) + ":" + std::string(normalizedName) + "|";
    return rest.substr(2).starts_with(namePrefix);
}

bool IsPathInside(const fs::path& root, const fs::path& path) {
    std::error_code rootError;
    std::error_code pathError;
    const fs::path normalizedRoot = fs::weakly_canonical(root, rootError);
    const fs::path normalizedPath = fs::weakly_canonical(path, pathError);
    if (rootError || pathError) {
        return false;
    }
    auto rootIt = normalizedRoot.begin();
    auto pathIt = normalizedPath.begin();
    for (; rootIt != normalizedRoot.end(); ++rootIt, ++pathIt) {
        if (pathIt == normalizedPath.end() || *rootIt != *pathIt) {
            return false;
        }
    }
    return true;
}

std::string MakeCacheKey(
    std::string_view providerId,
    VariableKind kind,
    std::string_view name,
    std::string_view parameter,
    std::uint64_t contextDiscriminator) {
    std::string key;
    key.reserve(providerId.size() + name.size() + parameter.size() + 72);
    const auto appendPart = [&key](std::string_view part) {
        key += std::to_string(part.size());
        key.push_back(':');
        key.append(part);
        key.push_back('|');
    };
    appendPart(providerId);
    key.push_back(kind == VariableKind::Simple ? 's' : 'f');
    key.push_back('|');
    appendPart(name);
    appendPart(parameter);
    key += std::to_string(contextDiscriminator);
    return key;
}

void HashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= kFnvPrime;
    }
}

void HashString(std::uint64_t& hash, std::string_view value) {
    const std::uint64_t size = value.size();
    HashBytes(hash, &size, sizeof(size));
    HashBytes(hash, value.data(), value.size());
}

std::uint64_t MakeContextDiscriminator(const TagsModule::EvaluationContext& context) {
    std::uint64_t hash = 14695981039346656037ull;
    HashString(hash, context.bindCommand);
    const unsigned char hasBinderRuntime = context.runningBindRuntimeId != 0 ? 1 : 0;
    HashBytes(hash, &hasBinderRuntime, sizeof(hasBinderRuntime));
    return hash;
}

struct TransparentStringHash {
    using is_transparent = void;

    std::size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }
};

struct TransparentStringEqual {
    using is_transparent = void;

    bool operator()(std::string_view left, std::string_view right) const noexcept {
        return left == right;
    }
};

} // namespace

class Runtime::Impl {
public:
    struct VariableRecord {
        Registration registration{};
        std::string normalizedName{};
        bool active = false;
        bool quarantined = false;
        std::uint32_t consecutiveFailures = 0;
        std::uint64_t lastErrorLogAtMs = 0;
        std::uint64_t suppressedErrorLogs = 0;
    };

    struct CacheEntry {
        std::optional<std::string> value{};
        std::uint64_t updatedAtMs = 0;
    };

    struct ProviderRecord {
        ProviderStatus status{};
        fs::path sourcePath{};
        DWORD sessionThreadId = 0;
        bool sessionActive = false;
        bool sessionReady = false;
    };

    struct PerfStats {
        std::uint64_t windowStartMs = 0;
        std::uint64_t evaluations = 0;
        std::uint64_t luaEvaluations = 0;
        std::uint64_t hudEvaluations = 0;
        std::uint64_t cacheHits = 0;
        std::uint64_t cacheMisses = 0;
        std::uint64_t errors = 0;
        std::uint64_t timeouts = 0;
        std::uint64_t threadRejects = 0;
        std::uint64_t lookups = 0;
        std::uint64_t warmLookups = 0;
        std::uint64_t coldLookups = 0;
        std::uint64_t pureSkips = 0;
        double totalMs = 0.0;
        double maxMs = 0.0;
        double lookupTotalMs = 0.0;
        double lookupMaxMs = 0.0;
        double warmTotalMs = 0.0;
        double coldTotalMs = 0.0;
        double luaTotalMs = 0.0;
        double luaMaxMs = 0.0;
        double hudTotalMs = 0.0;
        double hudMaxMs = 0.0;
    };

    using ActiveIndex = std::unordered_map<
        std::string,
        std::shared_ptr<VariableRecord>,
        TransparentStringHash,
        TransparentStringEqual>;

    mutable std::mutex mutex{};
    fs::path luaRoot{};
    std::unordered_map<std::string, std::unique_ptr<ProviderRecord>> providers{};
    std::vector<std::string> providerOrder{};
    std::vector<std::shared_ptr<VariableRecord>> variables{};
    ActiveIndex activeSimpleIndex{};
    ActiveIndex activeFunctionIndex{};
    std::unordered_set<std::string> reservedSimpleNames{};
    std::unordered_set<std::string> reservedFunctionNames{};
    mutable std::unordered_map<std::string, CacheEntry> persistentCache{};
    mutable std::unordered_map<std::string, std::optional<std::string>> lastGood{};
    mutable PerfStats perf{};
    std::uint64_t generation = 0;
    std::uint64_t catalogRevision = 1;
    DWORD ownerThreadId = 0;
    bool sampReady = false;
    bool globalEnabled = true;
    bool reloadRequested = false;
    std::uint64_t lastFailureLogAtMs = 0;

    void EnsureCacheSlotLocked(const std::string& key) const {
        if (persistentCache.contains(key) || lastGood.contains(key)) {
            return;
        }
        if (persistentCache.size() + lastGood.size() < kMaxCacheEntries) {
            return;
        }
        if (!lastGood.empty()) {
            lastGood.erase(lastGood.begin());
        } else if (!persistentCache.empty()) {
            persistentCache.erase(persistentCache.begin());
        }
    }

    void StorePersistentLocked(
        const std::string& key,
        std::optional<std::string> value,
        std::uint64_t updatedAtMs) const {
        lastGood.erase(key);
        EnsureCacheSlotLocked(key);
        persistentCache[key] = { std::move(value), updatedAtMs };
    }

    void StoreLastGoodLocked(
        const std::string& key,
        std::optional<std::string> value,
        std::uint64_t updatedAtMs) const {
        if (const auto persistent = persistentCache.find(key); persistent != persistentCache.end()) {
            persistent->second.value = std::move(value);
            persistent->second.updatedAtMs = updatedAtMs;
            return;
        }
        EnsureCacheSlotLocked(key);
        lastGood[key] = std::move(value);
    }

    std::optional<std::string> LastGoodValueLocked(const std::string& key) const {
        if (const auto it = lastGood.find(key); it != lastGood.end()) {
            return it->second;
        }
        if (const auto it = persistentCache.find(key); it != persistentCache.end()) {
            return it->second.value;
        }
        return std::nullopt;
    }

    void ClearProviderVariablesLocked(std::string_view providerId) {
        variables.erase(
            std::remove_if(
                variables.begin(),
                variables.end(),
                [&](const std::shared_ptr<VariableRecord>& variable) {
                    return variable && variable->registration.providerId == providerId;
                }),
            variables.end());
        for (auto it = persistentCache.begin(); it != persistentCache.end();) {
            if (CacheKeyBelongsToProvider(it->first, providerId)) {
                it = persistentCache.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = lastGood.begin(); it != lastGood.end();) {
            if (CacheKeyBelongsToProvider(it->first, providerId)) {
                it = lastGood.erase(it);
            } else {
                ++it;
            }
        }
    }

    void RebuildIndexLocked() {
        activeSimpleIndex.clear();
        activeFunctionIndex.clear();
        std::unordered_map<std::string, std::size_t> nameClaims;
        nameClaims.reserve(variables.size());
        for (const std::shared_ptr<VariableRecord>& variable : variables) {
            if (variable) {
                ++nameClaims[variable->normalizedName];
            }
        }

        for (auto& [id, provider] : providers) {
            provider->status.registeredVariables = 0;
            if (provider->status.state == ProviderState::Conflict) {
                provider->status.state = provider->status.enabled
                    ? (provider->sessionReady
                            ? ProviderState::Ready
                            : ProviderState::WaitingForMoonLoader)
                    : ProviderState::Disabled;
                provider->status.detail.clear();
            }
        }

        for (const std::shared_ptr<VariableRecord>& variable : variables) {
            if (!variable) {
                continue;
            }
            variable->active = false;
            const auto providerIt = providers.find(variable->registration.providerId);
            if (providerIt == providers.end()) {
                continue;
            }
            ProviderRecord& provider = *providerIt->second;
            ++provider.status.registeredVariables;

            const bool reserved = reservedSimpleNames.contains(variable->normalizedName)
                || reservedFunctionNames.contains(variable->normalizedName);
            const bool duplicate = nameClaims[variable->normalizedName] > 1;
            if (reserved || duplicate) {
                if (provider.sessionReady) {
                    provider.status.state = ProviderState::Conflict;
                    provider.status.detail = reserved
                        ? "variable name conflicts with built-in or tags.custom_vars"
                        : "duplicate code variable name";
                }
            }
        }

        std::size_t activeCount = 0;
        for (const std::shared_ptr<VariableRecord>& variable : variables) {
            if (!variable) {
                continue;
            }
            const auto providerIt = providers.find(variable->registration.providerId);
            if (providerIt == providers.end()
                || providerIt->second->status.state != ProviderState::Ready
                || variable->quarantined) {
                continue;
            }
            variable->active = true;
            ActiveIndex& activeIndex = variable->registration.kind == VariableKind::Simple
                ? activeSimpleIndex
                : activeFunctionIndex;
            activeIndex[variable->normalizedName] = variable;
            ++activeCount;
        }

        ++catalogRevision;
        debuglog::WriteInfo(
            "[tags][code][registry] providers=%llu variables=%llu active=%llu revision=%llu",
            static_cast<unsigned long long>(providers.size()),
            static_cast<unsigned long long>(variables.size()),
            static_cast<unsigned long long>(activeCount),
            static_cast<unsigned long long>(catalogRevision));
    }

    static bool ConfiguredProviderEnabled(
        const std::unordered_map<std::string, bool>& providers,
        std::string_view id) {
        const auto it = providers.find(std::string(id));
        return it != providers.end() && it->second;
    }

    void ClearProvidersLocked() {
        providers.clear();
        providerOrder.clear();
        variables.clear();
        activeSimpleIndex.clear();
        activeFunctionIndex.clear();
        persistentCache.clear();
        lastGood.clear();
        ++catalogRevision;
    }

    void DiscoverLuaFilesLocked(
        const fs::path& root,
        const std::unordered_map<std::string, bool>& enabledProviders,
        std::vector<std::pair<std::string, fs::path>>& discovered,
        std::size_t& skipped) {
        std::error_code existsError;
        if (!fs::exists(root, existsError) || existsError) {
            return;
        }

        std::error_code iteratorError;
        for (fs::recursive_directory_iterator it(root, iteratorError), end;
             it != end && !iteratorError;
             it.increment(iteratorError)) {
            std::error_code entryError;
            if (!it->is_regular_file(entryError) || entryError) {
                continue;
            }
            const fs::path path = it->path();
            const std::string relative = NormalizedRelativePath(root, path);
            if (relative.empty()) {
                ++skipped;
                continue;
            }
            if (!HasSuffix(relative, ".lua")) {
                continue;
            }
            if (!IsPathInside(root, path)) {
                ++skipped;
                debuglog::WriteError("[tags][code][load] rejected path outside root: %ls", path.c_str());
                continue;
            }
            const std::uint64_t size = fs::file_size(path, entryError);
            if (entryError || size > kMaxLuaFileBytes) {
                ++skipped;
                debuglog::WriteError(
                    "[tags][code][load] rejected Lua file size path=%ls size=%llu",
                    path.c_str(),
                    static_cast<unsigned long long>(size));
                continue;
            }
            const std::string id = "lua:" + relative;
            const std::size_t availableProviders = providers.size() < kMaxProviders
                ? kMaxProviders - providers.size()
                : 0;
            if (id.size() > kMaxProviderIdBytes || availableProviders == 0) {
                ++skipped;
                continue;
            }
            if (discovered.size() < availableProviders) {
                discovered.emplace_back(id, path);
                continue;
            }
            ++skipped;
            const auto largest = std::max_element(
                discovered.begin(),
                discovered.end(),
                [](const auto& left, const auto& right) {
                    return left.first < right.first;
                });
            if (largest != discovered.end() && id < largest->first) {
                *largest = { id, path };
            }
        }
        if (iteratorError) {
            debuglog::WriteError(
                "[tags][code][load] directory enumeration failed root=%ls error=%d",
                root.c_str(),
                iteratorError.value());
        }

        std::sort(discovered.begin(), discovered.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
        const auto uniqueEnd = std::unique(
            discovered.begin(),
            discovered.end(),
            [](const auto& left, const auto& right) {
                return left.first == right.first;
            });
        skipped += static_cast<std::size_t>(std::distance(uniqueEnd, discovered.end()));
        discovered.erase(uniqueEnd, discovered.end());

        for (const auto& [id, path] : discovered) {
            auto provider = std::make_unique<ProviderRecord>();
            provider->status.id = id;
            provider->status.displayName = WideToUtf8(path.filename());
            provider->status.enabled = globalEnabled && ConfiguredProviderEnabled(enabledProviders, id);
            provider->status.state = provider->status.enabled
                ? ProviderState::WaitingForMoonLoader
                : ProviderState::Disabled;
            provider->status.generation = generation;
            provider->status.detail = provider->status.enabled
                ? "waiting for MoonLoader host"
                : "disabled until explicitly enabled";
            provider->sourcePath = path;
            providerOrder.push_back(id);
            providers.emplace(id, std::move(provider));
        }
    }

    void ScanProfileLocked(std::string_view reason) {
        const double beginMs = PerfNowMs();
        ++generation;
        luaRoot = AppConfig::Instance().LuaVariablesRoot();
        const GlobalLuaVariablesConfig settings = AppConfig::Instance().LuaVariablesConfig();
        globalEnabled = settings.enabled;

        std::vector<std::pair<std::string, fs::path>> luaFiles;
        std::size_t skipped = 0;
        DiscoverLuaFilesLocked(luaRoot, settings.providers, luaFiles, skipped);
        reloadRequested = false;

        debuglog::WriteInfo(
            "[tags][code][load] reason=%.*s root=%ls generation=%llu lua=%llu skipped=%llu enabled=%d elapsed=%.2fms",
            static_cast<int>(reason.size()),
            reason.data(),
            luaRoot.c_str(),
            static_cast<unsigned long long>(generation),
            static_cast<unsigned long long>(luaFiles.size()),
            static_cast<unsigned long long>(skipped),
            globalEnabled ? 1 : 0,
            PerfNowMs() - beginMs);
    }

    void MaybeLogPerf(std::uint64_t nowMs) {
        PerfStats snapshot;
        {
            std::lock_guard lock(mutex);
            if (perf.windowStartMs == 0) {
                perf.windowStartMs = nowMs;
                return;
            }
            if (nowMs - perf.windowStartMs < kTelemetryWindowMs) {
                return;
            }
            snapshot = perf;
            perf = {};
            perf.windowStartMs = nowMs;
        }
        if (snapshot.lookups == 0 && snapshot.threadRejects == 0) {
            return;
        }
        const double callbackAverageMs = snapshot.evaluations > 0
            ? snapshot.totalMs / static_cast<double>(snapshot.evaluations)
            : 0.0;
        const double lookupAverageMs = snapshot.lookups > 0
            ? snapshot.lookupTotalMs / static_cast<double>(snapshot.lookups)
            : 0.0;
        const double warmAverageMs = snapshot.warmLookups > 0
            ? snapshot.warmTotalMs / static_cast<double>(snapshot.warmLookups)
            : 0.0;
        const double coldAverageMs = snapshot.coldLookups > 0
            ? snapshot.coldTotalMs / static_cast<double>(snapshot.coldLookups)
            : 0.0;
        const double luaAverageMs = snapshot.luaEvaluations > 0
            ? snapshot.luaTotalMs / static_cast<double>(snapshot.luaEvaluations)
            : 0.0;
        const double hudAverageMs = snapshot.hudEvaluations > 0
            ? snapshot.hudTotalMs / static_cast<double>(snapshot.hudEvaluations)
            : 0.0;
        debuglog::WriteInfo(
            "[tags][code][perf] window=%llums lookup=%llu warm=%llu cold=%llu pureSkip=%llu hit=%llu miss=%llu "
            "lookupAvg=%.3fms lookupMax=%.2fms warmAvg=%.3fms coldAvg=%.3fms eval=%llu callbackAvg=%.3fms callbackMax=%.2fms "
            "lua=%llu luaAvg=%.3fms luaMax=%.2fms "
            "hud=%llu hudAvg=%.3fms hudMax=%.2fms error=%llu timeout=%llu threadReject=%llu",
            static_cast<unsigned long long>(snapshot.windowStartMs == 0 ? 0 : nowMs - snapshot.windowStartMs),
            static_cast<unsigned long long>(snapshot.lookups),
            static_cast<unsigned long long>(snapshot.warmLookups),
            static_cast<unsigned long long>(snapshot.coldLookups),
            static_cast<unsigned long long>(snapshot.pureSkips),
            static_cast<unsigned long long>(snapshot.cacheHits),
            static_cast<unsigned long long>(snapshot.cacheMisses),
            lookupAverageMs,
            snapshot.lookupMaxMs,
            warmAverageMs,
            coldAverageMs,
            static_cast<unsigned long long>(snapshot.evaluations),
            callbackAverageMs,
            snapshot.maxMs,
            static_cast<unsigned long long>(snapshot.luaEvaluations),
            luaAverageMs,
            snapshot.luaMaxMs,
            static_cast<unsigned long long>(snapshot.hudEvaluations),
            hudAverageMs,
            snapshot.hudMaxMs,
            static_cast<unsigned long long>(snapshot.errors),
            static_cast<unsigned long long>(snapshot.timeouts),
            static_cast<unsigned long long>(snapshot.threadRejects));
    }
};

Value Value::Nil() {
    return {};
}

Value Value::String(std::string value) {
    return Value{ ValueType::String, std::move(value) };
}

Value Value::Int64(std::int64_t value) {
    return Value{ ValueType::Int64, value };
}

Value Value::Double(double value) {
    return Value{ ValueType::Double, value };
}

Value Value::Bool(bool value) {
    return Value{ ValueType::Bool, value };
}

std::string NormalizeName(std::string_view value) {
    return ToLowerAscii(value);
}

std::optional<std::string> FormatValue(const Value& value) {
    switch (value.type) {
    case ValueType::Nil:
        return std::string();
    case ValueType::String: {
        const auto* text = std::get_if<std::string>(&value.data);
        return text ? NormalizeProviderText(*text, kMaxResultBytes) : std::nullopt;
    }
    case ValueType::Int64: {
        const auto* number = std::get_if<std::int64_t>(&value.data);
        return number ? std::optional<std::string>(std::to_string(*number)) : std::nullopt;
    }
    case ValueType::Double: {
        const auto* number = std::get_if<double>(&value.data);
        if (!number || !std::isfinite(*number)) {
            return std::nullopt;
        }
        char buffer[64]{};
        const auto result = std::to_chars(
            std::begin(buffer),
            std::end(buffer),
            *number,
            std::chars_format::general);
        return result.ec == std::errc()
            ? std::optional<std::string>(std::string(buffer, result.ptr))
            : std::nullopt;
    }
    case ValueType::Bool: {
        const auto* boolean = std::get_if<bool>(&value.data);
        return boolean ? std::optional<std::string>(*boolean ? "true" : "false") : std::nullopt;
    }
    default:
        return std::nullopt;
    }
}

const char* ProviderStateName(ProviderState state) {
    switch (state) {
    case ProviderState::Disabled:
        return "disabled";
    case ProviderState::WaitingForMoonLoader:
        return "waiting_moonloader";
    case ProviderState::Loading:
        return "loading";
    case ProviderState::Ready:
        return "ready";
    case ProviderState::Conflict:
        return "conflict";
    case ProviderState::Faulted:
        return "faulted";
    default:
        return "unknown";
    }
}

Runtime& Runtime::Instance() {
    static Runtime runtime;
    return runtime;
}

Runtime::Runtime()
    : impl_(std::make_unique<Impl>()) {}

Runtime::~Runtime() = default;

void Runtime::OnProcessAttach() {
    std::lock_guard lock(impl_->mutex);
    impl_->ClearProvidersLocked();
    impl_->ScanProfileLocked("startup");
}

void Runtime::Shutdown() {
    lua_bridge::Shutdown();
    std::lock_guard lock(impl_->mutex);
    ++impl_->generation;
    impl_->ClearProvidersLocked();
    impl_->ownerThreadId = 0;
    impl_->sampReady = false;
    impl_->reloadRequested = false;
    debuglog::WriteInfo("[tags][code] shutdown");
}

void Runtime::ReloadProfile() {
    DWORD ownerThreadId = 0;
    {
        std::lock_guard lock(impl_->mutex);
        ownerThreadId = impl_->ownerThreadId;
    }
    if (ownerThreadId != 0 && GetCurrentThreadId() != ownerThreadId) {
        RequestReload();
        return;
    }
    std::lock_guard lock(impl_->mutex);
    impl_->ClearProvidersLocked();
    impl_->ScanProfileLocked("reload");
}

void Runtime::RequestReload() {
    std::lock_guard lock(impl_->mutex);
    impl_->reloadRequested = true;
}

void Runtime::Tick(bool sampReady) {
    const DWORD currentThread = GetCurrentThreadId();
    bool reload = false;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->ownerThreadId == 0) {
            impl_->ownerThreadId = currentThread;
            debuglog::WriteInfo("[tags][code] owner thread established tid=%lu", static_cast<unsigned long>(currentThread));
        }
        if (impl_->ownerThreadId != currentThread) {
            return;
        }
        impl_->sampReady = sampReady;
        reload = impl_->reloadRequested;
    }
    if (reload) {
        ReloadProfile();
    }
    impl_->MaybeLogPerf(GetTickCount64());
}

void Runtime::SetReservedNames(
    std::unordered_set<std::string> simpleNames,
    std::unordered_set<std::string> functionNames) {
    std::lock_guard lock(impl_->mutex);
    impl_->reservedSimpleNames = std::move(simpleNames);
    impl_->reservedFunctionNames = std::move(functionNames);
    impl_->RebuildIndexLocked();
}

std::optional<std::string> Runtime::Resolve(
    VariableKind kind,
    std::string_view normalizedName,
    std::string_view parameter,
    const TagsModule::EvaluationContext& context,
    ExpansionCache* expansionCache,
    bool* action,
    DeferredParameter deferredParameter) const {
    const double lookupBeginMs = PerfNowMs();

    const DWORD currentThread = GetCurrentThreadId();
    std::shared_ptr<Impl::VariableRecord> variable;
    bool sampReady = false;
    {
        std::lock_guard lock(impl_->mutex);
        const Impl::ActiveIndex& activeIndex = kind == VariableKind::Simple
            ? impl_->activeSimpleIndex
            : impl_->activeFunctionIndex;
        const auto indexIt = activeIndex.find(normalizedName);
        if (indexIt == activeIndex.end()) {
            return std::nullopt;
        }
        if (impl_->ownerThreadId == 0 || currentThread != impl_->ownerThreadId) {
            ++impl_->perf.threadRejects;
            const std::uint64_t now = GetTickCount64();
            if (impl_->lastFailureLogAtMs == 0 || now - impl_->lastFailureLogAtMs >= kFailureLogThrottleMs) {
                impl_->lastFailureLogAtMs = now;
                debuglog::WriteError(
                    "[tags][code] callback rejected on non-owner thread current=%lu owner=%lu",
                    static_cast<unsigned long>(currentThread),
                    static_cast<unsigned long>(impl_->ownerThreadId));
            }
            return std::nullopt;
        }
        variable = indexIt->second;
        const auto providerIt = impl_->providers.find(variable->registration.providerId);
        if (providerIt == impl_->providers.end() || providerIt->second->status.state != ProviderState::Ready) {
            return std::nullopt;
        }
        sampReady = impl_->sampReady;
    }

    std::string deferredParameterStorage;
    if (deferredParameter.resolve) {
        deferredParameterStorage = deferredParameter.resolve(deferredParameter.context);
        parameter = deferredParameterStorage;
    }
    if (parameter.size() > kMaxParameterBytes) {
        return std::nullopt;
    }

    const bool isAction = variable->registration.effect == VariableEffect::Action;
    if (action) {
        *action = isAction;
    }
    if (isAction && !context.allowSideEffects) {
        const double lookupElapsedMs = PerfNowMs() - lookupBeginMs;
        std::lock_guard lock(impl_->mutex);
        ++impl_->perf.lookups;
        ++impl_->perf.pureSkips;
        impl_->perf.lookupTotalMs += lookupElapsedMs;
        impl_->perf.lookupMaxMs = std::max(impl_->perf.lookupMaxMs, lookupElapsedMs);
        return std::string();
    }

    const std::string cacheKey = MakeCacheKey(
        variable->registration.providerId,
        kind,
        variable->normalizedName,
        parameter,
        variable->registration.cachePolicy == CachePolicy::Event
            ? 0
            : MakeContextDiscriminator(context));
    const std::uint64_t now = GetTickCount64();
    const bool hud = context.activationSource == "hud";

    if (variable->registration.cachePolicy == CachePolicy::Expansion && expansionCache) {
        const auto it = expansionCache->values.find(cacheKey);
        if (it != expansionCache->values.end()) {
            const double lookupElapsedMs = PerfNowMs() - lookupBeginMs;
            std::lock_guard lock(impl_->mutex);
            ++impl_->perf.cacheHits;
            ++impl_->perf.lookups;
            ++impl_->perf.warmLookups;
            impl_->perf.lookupTotalMs += lookupElapsedMs;
            impl_->perf.lookupMaxMs = std::max(impl_->perf.lookupMaxMs, lookupElapsedMs);
            impl_->perf.warmTotalMs += lookupElapsedMs;
            return it->second;
        }
    }

    {
        std::lock_guard lock(impl_->mutex);
        const auto it = impl_->persistentCache.find(cacheKey);
        if (it != impl_->persistentCache.end()) {
            const bool ttlHit = variable->registration.cachePolicy == CachePolicy::Ttl
                && variable->registration.ttlMs > 0
                && now >= it->second.updatedAtMs
                && now - it->second.updatedAtMs < variable->registration.ttlMs;
            const bool eventHit = variable->registration.cachePolicy == CachePolicy::Event;
            if (ttlHit || eventHit) {
                const double lookupElapsedMs = PerfNowMs() - lookupBeginMs;
                ++impl_->perf.cacheHits;
                ++impl_->perf.lookups;
                ++impl_->perf.warmLookups;
                impl_->perf.lookupTotalMs += lookupElapsedMs;
                impl_->perf.lookupMaxMs = std::max(impl_->perf.lookupMaxMs, lookupElapsedMs);
                impl_->perf.warmTotalMs += lookupElapsedMs;
                return it->second.value;
            }
        }
        ++impl_->perf.cacheMisses;
    }

    const double beginMs = PerfNowMs();
    EvaluationOutcome outcome;
    try {
        outcome = variable->registration.callback(EvaluationRequest{ &context, parameter, sampReady });
    } catch (const std::exception& error) {
        outcome.error = std::string("internal callback exception: ") + error.what();
    } catch (...) {
        outcome.error = "internal callback exception";
    }
    const double elapsedMs = PerfNowMs() - beginMs;
    if (elapsedMs > kDefaultCallbackBudgetMs) {
        outcome.ok = false;
        outcome.timeout = true;
        outcome.error = "callback exceeded " + std::to_string(kDefaultCallbackBudgetMs) + "ms budget";
    }

    std::optional<std::string> formatted;
    if (outcome.ok) {
        const bool typeMatches = outcome.value.type == variable->registration.resultType
            || outcome.value.type == ValueType::Nil;
        if (!typeMatches) {
            outcome.ok = false;
            outcome.error = "callback returned a value with the wrong declared type";
        } else {
            formatted = FormatValue(outcome.value);
            if (!formatted.has_value()) {
                outcome.ok = false;
                outcome.error = "callback returned invalid UTF-8, non-finite number, or oversized result";
            }
        }
    }

    std::optional<std::string> fallback;
    bool logFailure = false;
    std::uint64_t suppressedErrorLogs = 0;
    const double lookupElapsedMs = PerfNowMs() - lookupBeginMs;
    {
        std::lock_guard lock(impl_->mutex);
        ++impl_->perf.lookups;
        ++impl_->perf.coldLookups;
        impl_->perf.lookupTotalMs += lookupElapsedMs;
        impl_->perf.lookupMaxMs = std::max(impl_->perf.lookupMaxMs, lookupElapsedMs);
        impl_->perf.coldTotalMs += lookupElapsedMs;
        ++impl_->perf.evaluations;
        ++impl_->perf.luaEvaluations;
        impl_->perf.luaTotalMs += elapsedMs;
        impl_->perf.luaMaxMs = std::max(impl_->perf.luaMaxMs, elapsedMs);
        if (hud) {
            ++impl_->perf.hudEvaluations;
            impl_->perf.hudTotalMs += elapsedMs;
            impl_->perf.hudMaxMs = std::max(impl_->perf.hudMaxMs, elapsedMs);
        }
        impl_->perf.totalMs += elapsedMs;
        impl_->perf.maxMs = std::max(impl_->perf.maxMs, elapsedMs);

        if (outcome.ok) {
            variable->consecutiveFailures = 0;
            if (variable->registration.cachePolicy == CachePolicy::Ttl
                || variable->registration.cachePolicy == CachePolicy::Event) {
                impl_->StorePersistentLocked(cacheKey, formatted, now);
            } else {
                impl_->StoreLastGoodLocked(cacheKey, formatted, now);
            }
            if (variable->registration.cachePolicy == CachePolicy::Expansion && expansionCache) {
                if (expansionCache->values.size() < kMaxCacheEntries) {
                    expansionCache->values[cacheKey] = formatted;
                }
            }
        } else {
            ++impl_->perf.errors;
            if (outcome.timeout) {
                ++impl_->perf.timeouts;
            }
            ++variable->consecutiveFailures;
            if (variable->lastErrorLogAtMs == 0
                || now < variable->lastErrorLogAtMs
                || now - variable->lastErrorLogAtMs >= kFailureLogThrottleMs) {
                logFailure = true;
                suppressedErrorLogs = variable->suppressedErrorLogs;
                variable->suppressedErrorLogs = 0;
                variable->lastErrorLogAtMs = now;
            } else {
                ++variable->suppressedErrorLogs;
            }
            fallback = impl_->LastGoodValueLocked(cacheKey);
            if (outcome.timeout || variable->consecutiveFailures >= 3) {
                variable->quarantined = true;
                variable->active = false;
                impl_->RebuildIndexLocked();
            }
        }
    }

    if (!outcome.ok && logFailure) {
        debuglog::WriteError(
            "[tags][code][eval] provider=%s variable=%s error=%s failures=%u timeout=%d suppressed=%llu",
            variable->registration.providerId.c_str(),
            variable->registration.name.c_str(),
            outcome.error.c_str(),
            variable->consecutiveFailures,
            outcome.timeout ? 1 : 0,
            static_cast<unsigned long long>(suppressedErrorLogs));
    }
    if (!outcome.ok) {
        return fallback;
    }
    return formatted;
}

std::vector<CatalogVariable> Runtime::Catalog() const {
    std::vector<CatalogVariable> result;
    std::lock_guard lock(impl_->mutex);
    result.reserve(impl_->activeSimpleIndex.size() + impl_->activeFunctionIndex.size());
    for (const std::shared_ptr<Impl::VariableRecord>& variable : impl_->variables) {
        if (!variable || !variable->active) {
            continue;
        }
        CatalogVariable entry;
        entry.kind = variable->registration.kind;
        entry.effect = variable->registration.effect;
        entry.providerId = variable->registration.providerId;
        entry.name = variable->registration.name;
        entry.token = variable->registration.kind == VariableKind::Simple
            ? "{" + variable->registration.name + "}"
            : "[" + variable->registration.name + "(...)]";
        entry.example = variable->registration.example.empty()
            ? entry.token
            : variable->registration.example;
        entry.description = variable->registration.description;
        result.push_back(std::move(entry));
    }
    std::sort(result.begin(), result.end(), [](const CatalogVariable& left, const CatalogVariable& right) {
        if (left.providerId != right.providerId) {
            return left.providerId < right.providerId;
        }
        if (left.name != right.name) {
            return left.name < right.name;
        }
        return left.kind < right.kind;
    });
    return result;
}

std::uint64_t Runtime::CatalogRevision() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->catalogRevision;
}

std::vector<ProviderStatus> Runtime::Providers() const {
    std::vector<ProviderStatus> result;
    std::lock_guard lock(impl_->mutex);
    result.reserve(impl_->providerOrder.size());
    for (const std::string& id : impl_->providerOrder) {
        const auto it = impl_->providers.find(id);
        if (it != impl_->providers.end()) {
            result.push_back(it->second->status);
        }
    }
    return result;
}

bool Runtime::SetProviderEnabled(std::string_view providerId, bool enabled) {
    {
        std::lock_guard lock(impl_->mutex);
        const auto it = impl_->providers.find(std::string(providerId));
        if (it == impl_->providers.end() || it->second->status.enabled == enabled) {
            return it != impl_->providers.end();
        }
    }

    std::string error;
    if (!AppConfig::Instance().SetLuaProviderEnabled(providerId, enabled, &error)) {
        debuglog::WriteError(
            "[tags][code] failed to persist provider state id=%.*s error=%s",
            static_cast<int>(providerId.size()),
            providerId.data(),
            error.c_str());
        return false;
    }

    {
        std::lock_guard lock(impl_->mutex);
        const auto it = impl_->providers.find(std::string(providerId));
        if (it == impl_->providers.end()) {
            return false;
        }
        it->second->status.enabled = enabled;
    }
    RequestReload();
    return true;
}

std::vector<LuaPlanEntry> Runtime::LuaPlan() const {
    std::vector<LuaPlanEntry> result;
    std::lock_guard lock(impl_->mutex);
    for (const std::string& id : impl_->providerOrder) {
        const auto it = impl_->providers.find(id);
        if (it == impl_->providers.end()) {
            continue;
        }
        result.push_back(LuaPlanEntry{
            id,
            WideToUtf8(it->second->sourcePath),
            it->second->status.enabled,
            impl_->generation,
            it->second->status.state,
        });
    }
    return result;
}

std::uint64_t Runtime::Generation() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->generation;
}

std::uint32_t Runtime::OwnerThreadId() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->ownerThreadId;
}

bool Runtime::IsCurrentSession(std::uint32_t ownerThreadId, std::uint64_t generation) const {
    std::lock_guard lock(impl_->mutex);
    return impl_->ownerThreadId == ownerThreadId && impl_->generation == generation;
}

bool Runtime::SampReady() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->sampReady;
}

bool Runtime::BeginProviderSession(
    std::string_view providerId,
    std::uint64_t generation,
    std::uint32_t ownerThreadId,
    std::string& error) {
    std::lock_guard lock(impl_->mutex);
    const auto it = impl_->providers.find(std::string(providerId));
    if (it == impl_->providers.end()) {
        error = "provider is not in the current Lua plan";
        return false;
    }
    Impl::ProviderRecord& provider = *it->second;
    if (!provider.status.enabled) {
        error = "provider is disabled";
        return false;
    }
    if (provider.status.state == ProviderState::Faulted
        || provider.status.state == ProviderState::Conflict) {
        error = "provider is disabled until an explicit reload";
        return false;
    }
    if (generation != impl_->generation) {
        error = "provider generation mismatch";
        return false;
    }
    if (impl_->ownerThreadId == 0
        || ownerThreadId == 0
        || ownerThreadId != impl_->ownerThreadId
        || ownerThreadId != GetCurrentThreadId()) {
        error = "provider did not attach on the established owner thread";
        return false;
    }
    if (provider.sessionActive) {
        error = "provider session is already active";
        return false;
    }

    impl_->ClearProviderVariablesLocked(providerId);
    provider.sessionActive = true;
    provider.sessionReady = false;
    provider.sessionThreadId = ownerThreadId;
    provider.status.state = ProviderState::Loading;
    provider.status.detail = "registering variables";
    provider.status.registeredVariables = 0;
    impl_->RebuildIndexLocked();
    return true;
}

bool Runtime::RegisterVariable(Registration registration, std::string& error) {
    registration.name = NormalizeName(registration.name);
    if (!IsValidVariableName(registration.name)) {
        error = "name must contain only A-Z, a-z, 0-9 or _ and be 1-64 bytes";
        return false;
    }
    if (registration.providerId.empty() || registration.providerId.size() > kMaxProviderIdBytes) {
        error = "invalid provider id";
        return false;
    }
    std::optional<std::string> description = NormalizeProviderText(
        registration.description,
        kMaxDescriptionBytes);
    std::optional<std::string> example = NormalizeProviderText(
        registration.example,
        kMaxExampleBytes);
    if (!description || !example || !registration.callback) {
        error = "invalid description, example, or callback";
        return false;
    }
    registration.description = std::move(*description);
    registration.example = std::move(*example);
    if (registration.cachePolicy == CachePolicy::Ttl
        && (registration.ttlMs == 0 || registration.ttlMs > 3600000)) {
        error = "ttl_ms must be in range 1..3600000";
        return false;
    }
    if (registration.kind == VariableKind::Function
        && registration.cachePolicy == CachePolicy::Event) {
        error = "event cache is supported only for simple variables";
        return false;
    }

    std::lock_guard lock(impl_->mutex);
    const auto providerIt = impl_->providers.find(registration.providerId);
    if (providerIt == impl_->providers.end()
        || !providerIt->second->sessionActive
        || providerIt->second->status.state != ProviderState::Loading
        || providerIt->second->sessionThreadId != GetCurrentThreadId()
        || impl_->ownerThreadId != GetCurrentThreadId()) {
        error = "provider session is not active";
        return false;
    }
    const std::size_t providerVariables = static_cast<std::size_t>(std::count_if(
        impl_->variables.begin(),
        impl_->variables.end(),
        [&](const std::shared_ptr<Impl::VariableRecord>& variable) {
            return variable && variable->registration.providerId == registration.providerId;
        }));
    if (providerVariables >= kMaxVariablesPerProvider || impl_->variables.size() >= kMaxVariables) {
        error = "variable limit exceeded";
        return false;
    }
    const bool duplicateInProvider = std::any_of(
        impl_->variables.begin(),
        impl_->variables.end(),
        [&](const std::shared_ptr<Impl::VariableRecord>& variable) {
            return variable
                && variable->registration.providerId == registration.providerId
                && variable->normalizedName == registration.name;
        });
    if (duplicateInProvider) {
        error = "duplicate variable name in provider";
        return false;
    }

    auto variable = std::make_shared<Impl::VariableRecord>();
    variable->normalizedName = registration.name;
    variable->registration = std::move(registration);
    impl_->variables.push_back(std::move(variable));
    impl_->RebuildIndexLocked();
    return true;
}

bool Runtime::MarkProviderReady(
    std::string_view providerId,
    std::uint64_t generation,
    std::string& error) {
    std::lock_guard lock(impl_->mutex);
    const auto it = impl_->providers.find(std::string(providerId));
    if (it == impl_->providers.end()
        || generation != impl_->generation
        || !it->second->sessionActive
        || it->second->sessionThreadId != GetCurrentThreadId()) {
        error = "provider session is stale or on the wrong thread";
        return false;
    }
    it->second->status.state = ProviderState::Ready;
    it->second->sessionReady = true;
    it->second->status.detail = "ready";
    impl_->RebuildIndexLocked();
    return true;
}

void Runtime::DetachProvider(
    std::string_view providerId,
    std::uint64_t generation,
    std::string_view detail) {
    std::lock_guard lock(impl_->mutex);
    const auto it = impl_->providers.find(std::string(providerId));
    if (it == impl_->providers.end() || generation != impl_->generation) {
        return;
    }
    const bool preserveTerminalState = it->second->status.state == ProviderState::Faulted;
    impl_->ClearProviderVariablesLocked(providerId);
    it->second->sessionActive = false;
    it->second->sessionReady = false;
    it->second->sessionThreadId = 0;
    if (!preserveTerminalState) {
        it->second->status.state = it->second->status.enabled
            ? ProviderState::WaitingForMoonLoader
            : ProviderState::Disabled;
        it->second->status.detail = std::string(detail.substr(0, kMaxDescriptionBytes));
    }
    impl_->RebuildIndexLocked();
}

void Runtime::MarkProviderFault(std::string_view providerId, std::string_view detail) {
    std::lock_guard lock(impl_->mutex);
    const auto it = impl_->providers.find(std::string(providerId));
    if (it == impl_->providers.end()) {
        return;
    }
    impl_->ClearProviderVariablesLocked(providerId);
    it->second->status.state = ProviderState::Faulted;
    it->second->status.detail = std::string(detail.substr(0, 512));
    it->second->sessionActive = false;
    it->second->sessionReady = false;
    it->second->sessionThreadId = 0;
    impl_->RebuildIndexLocked();
    debuglog::WriteError(
        "[tags][code] provider faulted id=%s detail=%s",
        it->second->status.id.c_str(),
        it->second->status.detail.c_str());
}

bool Runtime::Invalidate(std::string_view providerId, std::string_view name) {
    const std::string normalized = NormalizeName(name);
    std::lock_guard lock(impl_->mutex);
    const auto providerIt = impl_->providers.find(std::string(providerId));
    if (providerIt == impl_->providers.end()
        || !providerIt->second->sessionActive
        || (providerIt->second->status.state != ProviderState::Loading
            && providerIt->second->status.state != ProviderState::Ready)) {
        return false;
    }
    const bool registered = std::any_of(
        impl_->variables.begin(),
        impl_->variables.end(),
        [&](const std::shared_ptr<Impl::VariableRecord>& variable) {
            return variable
                && variable->registration.providerId == providerId
                && variable->normalizedName == normalized;
        });
    if (!registered) {
        return false;
    }
    for (auto it = impl_->persistentCache.begin(); it != impl_->persistentCache.end();) {
        if (CacheKeyBelongsToVariable(it->first, providerId, normalized)) {
            it = impl_->persistentCache.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = impl_->lastGood.begin(); it != impl_->lastGood.end();) {
        if (CacheKeyBelongsToVariable(it->first, providerId, normalized)) {
            it = impl_->lastGood.erase(it);
        } else {
            ++it;
        }
    }
    return true;
}

bool Runtime::Publish(
    std::string_view providerId,
    std::string_view name,
    Value value,
    std::string& error) {
    const std::string normalized = NormalizeName(name);
    std::lock_guard lock(impl_->mutex);
    const auto providerIt = impl_->providers.find(std::string(providerId));
    if (providerIt == impl_->providers.end()
        || !providerIt->second->sessionActive
        || (providerIt->second->status.state != ProviderState::Loading
            && providerIt->second->status.state != ProviderState::Ready)) {
        error = "provider session is not active";
        return false;
    }
    std::shared_ptr<Impl::VariableRecord> variable;
    for (const auto& candidate : impl_->variables) {
        if (candidate
            && candidate->registration.providerId == providerId
            && candidate->normalizedName == normalized) {
            variable = candidate;
            break;
        }
    }
    if (!variable || variable->registration.cachePolicy != CachePolicy::Event) {
        error = "event variable was not registered";
        return false;
    }
    if (value.type == ValueType::Double && variable->registration.resultType == ValueType::Int64) {
        const double* number = std::get_if<double>(&value.data);
        if (number
            && std::isfinite(*number)
            && std::floor(*number) == *number
            && *number >= kInt64MinAsDouble
            && *number < kInt64ExclusiveMaxAsDouble) {
            value = Value::Int64(static_cast<std::int64_t>(*number));
        }
    }
    if (value.type != ValueType::Nil && value.type != variable->registration.resultType) {
        error = "published value does not match the declared result type";
        return false;
    }
    const std::optional<std::string> formatted = FormatValue(value);
    if (!IsValidVariableName(normalized) || !formatted.has_value()) {
        error = "invalid published name or value";
        return false;
    }
    const std::string key = MakeCacheKey(
        providerId,
        variable->registration.kind,
        normalized,
        {},
        0);
    impl_->StorePersistentLocked(key, formatted, GetTickCount64());
    return true;
}

} // namespace codevars
