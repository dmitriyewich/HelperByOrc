#pragma once

#include "tags_module.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace codevars {

constexpr std::size_t kMaxVariableNameBytes = 64;
constexpr std::size_t kMaxProviderIdBytes = 192;
constexpr std::size_t kMaxDescriptionBytes = 512;
constexpr std::size_t kMaxExampleBytes = 256;
constexpr std::size_t kMaxParameterBytes = 4096;
constexpr std::size_t kMaxResultBytes = 65536;
constexpr std::size_t kMaxProviders = 128;
constexpr std::size_t kMaxVariablesPerProvider = 128;
constexpr std::size_t kMaxVariables = 1024;
constexpr std::size_t kMaxCacheEntries = 4096;
constexpr std::uint32_t kHudMinimumRefreshMs = 100;
constexpr std::uint32_t kDefaultCallbackBudgetMs = 8;

enum class VariableKind {
    Simple,
    Function,
};

enum class VariableEffect {
    Pure,
    Action,
};

enum class ValueType {
    Nil,
    String,
    Int64,
    Double,
    Bool,
};

enum class CachePolicy {
    Expansion,
    None,
    Ttl,
    Event,
};

enum class ProviderState {
    Disabled,
    WaitingForMoonLoader,
    Loading,
    Ready,
    Conflict,
    Faulted,
};

struct Value {
    ValueType type = ValueType::Nil;
    std::variant<std::monostate, std::string, std::int64_t, double, bool> data{};

    static Value Nil();
    static Value String(std::string value);
    static Value Int64(std::int64_t value);
    static Value Double(double value);
    static Value Bool(bool value);
};

struct EvaluationOutcome {
    bool ok = false;
    bool timeout = false;
    Value value{};
    std::string error{};
};

struct EvaluationRequest {
    const TagsModule::EvaluationContext* context = nullptr;
    std::string_view parameter{};
    bool sampReady = false;
};

using EvaluationCallback = std::function<EvaluationOutcome(const EvaluationRequest&)>;

struct Registration {
    std::string providerId{};
    VariableKind kind = VariableKind::Simple;
    VariableEffect effect = VariableEffect::Action;
    ValueType resultType = ValueType::String;
    CachePolicy cachePolicy = CachePolicy::Expansion;
    std::uint32_t ttlMs = 0;
    std::string name{};
    std::string description{};
    std::string example{};
    EvaluationCallback callback{};
};

struct CatalogVariable {
    VariableKind kind = VariableKind::Simple;
    VariableEffect effect = VariableEffect::Action;
    std::string providerId{};
    std::string name{};
    std::string token{};
    std::string example{};
    std::string description{};
};

struct ProviderStatus {
    std::string id{};
    std::string displayName{};
    ProviderState state = ProviderState::WaitingForMoonLoader;
    bool enabled = false;
    std::size_t registeredVariables = 0;
    std::uint64_t generation = 0;
    std::string detail{};
};

struct LuaPlanEntry {
    std::string id{};
    std::string pathUtf8{};
    bool enabled = false;
    std::uint64_t generation = 0;
    ProviderState state = ProviderState::WaitingForMoonLoader;
};

struct ExpansionCache {
    std::unordered_map<std::string, std::optional<std::string>> values{};
};

class Runtime {
public:
    static Runtime& Instance();

    void OnProcessAttach();
    void Shutdown();
    void ReloadProfile();
    void RequestReload();
    void Tick(bool sampReady);

    void SetReservedNames(
        std::unordered_set<std::string> simpleNames,
        std::unordered_set<std::string> functionNames);

    std::optional<std::string> Resolve(
        VariableKind kind,
        std::string_view normalizedName,
        std::string_view parameter,
        const TagsModule::EvaluationContext& context,
        ExpansionCache* expansionCache,
        bool* action = nullptr) const;
    bool HasActive(VariableKind kind, std::string_view normalizedName) const;

    std::vector<CatalogVariable> Catalog() const;
    std::uint64_t CatalogRevision() const;
    std::vector<ProviderStatus> Providers() const;
    bool SetProviderEnabled(std::string_view providerId, bool enabled);

    std::vector<LuaPlanEntry> LuaPlan() const;
    std::uint64_t Generation() const;
    std::uint32_t OwnerThreadId() const;
    bool SampReady() const;

    bool BeginProviderSession(
        std::string_view providerId,
        std::uint64_t generation,
        std::uint32_t ownerThreadId,
        std::string& error);
    bool RegisterVariable(Registration registration, std::string& error);
    bool MarkProviderReady(std::string_view providerId, std::uint64_t generation, std::string& error);
    void DetachProvider(std::string_view providerId, std::uint64_t generation, std::string_view detail);
    void MarkProviderFault(std::string_view providerId, std::string_view detail);
    bool Invalidate(std::string_view providerId, std::string_view name);
    bool Publish(std::string_view providerId, std::string_view name, Value value, std::string& error);

private:
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

std::string NormalizeName(std::string_view value);
std::optional<std::string> FormatValue(const Value& value);
const char* ProviderStateName(ProviderState state);

} // namespace codevars
