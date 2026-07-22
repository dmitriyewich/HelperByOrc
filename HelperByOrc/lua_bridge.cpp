#include "code_variables.h"

#include "app_config.h"
#include "debug_log.h"
#include "lua_bridge.h"
#include "resource.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

struct lua_State;
struct lua_Debug;

using lua_Number = double;
using lua_CFunction = int(__cdecl*)(lua_State*);
using lua_Hook = void(__cdecl*)(lua_State*, lua_Debug*);

namespace {

constexpr int LUA_REGISTRYINDEX = -10000;
constexpr int LUA_NOREF = -2;
constexpr int LUA_REFNIL = -1;
constexpr int LUA_TNIL = 0;
constexpr int LUA_TBOOLEAN = 1;
constexpr int LUA_TNUMBER = 3;
constexpr int LUA_TSTRING = 4;
constexpr int LUA_TTABLE = 5;
constexpr int LUA_TFUNCTION = 6;
constexpr int LUA_MASKCOUNT = 8;
constexpr int LUAJIT_MODE_ALLFUNC = 3;
constexpr int LUAJIT_MODE_OFF = 0x0000;
constexpr int kBridgeProtocolVersion = 2;
constexpr char kBridgeProtocolRegistryKey[] = "HelperByOrc.bridge.protocol";
constexpr int kHookInstructionInterval = 10000;
constexpr int kHookMaximumCalls = 200;
constexpr double kMaxExactLuaInteger = 9007199254740991.0;
constexpr double kInt64MinAsDouble = -9223372036854775808.0;
constexpr double kInt64ExclusiveMaxAsDouble = 9223372036854775808.0;
constexpr std::uint64_t kMaxLuaSourceBytes = 1024 * 1024;

struct LuaApi {
    HMODULE module = nullptr;
    int(__cdecl* gettop)(lua_State*) = nullptr;
    void(__cdecl* settop)(lua_State*, int) = nullptr;
    void(__cdecl* pushvalue)(lua_State*, int) = nullptr;
    void(__cdecl* pushnil)(lua_State*) = nullptr;
    void(__cdecl* pushnumber)(lua_State*, lua_Number) = nullptr;
    void(__cdecl* pushboolean)(lua_State*, int) = nullptr;
    void(__cdecl* pushlstring)(lua_State*, const char*, size_t) = nullptr;
    void(__cdecl* pushcclosure)(lua_State*, lua_CFunction, int) = nullptr;
    int(__cdecl* pushthread)(lua_State*) = nullptr;
    void(__cdecl* createtable)(lua_State*, int, int) = nullptr;
    void(__cdecl* setfield)(lua_State*, int, const char*) = nullptr;
    void(__cdecl* getfield)(lua_State*, int, const char*) = nullptr;
    void(__cdecl* rawseti)(lua_State*, int, int) = nullptr;
    void(__cdecl* rawgeti)(lua_State*, int, int) = nullptr;
    int(__cdecl* type)(lua_State*, int) = nullptr;
    int(__cdecl* toboolean)(lua_State*, int) = nullptr;
    lua_Number(__cdecl* tonumber)(lua_State*, int) = nullptr;
    const char*(__cdecl* tolstring)(lua_State*, int, size_t*) = nullptr;
    const void*(__cdecl* topointer)(lua_State*, int) = nullptr;
    int(__cdecl* pcall)(lua_State*, int, int, int) = nullptr;
    int(__cdecl* l_ref)(lua_State*, int) = nullptr;
    void(__cdecl* l_unref)(lua_State*, int, int) = nullptr;
    int(__cdecl* l_error)(lua_State*, const char*, ...) = nullptr;
    lua_Hook(__cdecl* gethook)(lua_State*) = nullptr;
    int(__cdecl* gethookmask)(lua_State*) = nullptr;
    int(__cdecl* gethookcount)(lua_State*) = nullptr;
    int(__cdecl* sethook)(lua_State*, lua_Hook, int, int) = nullptr;
    int(__cdecl* jit_setmode)(lua_State*, int, int) = nullptr;
};

LuaApi g_lua;
std::mutex g_backendMutex;
lua_bridge::Backend g_backend = lua_bridge::Backend::Waiting;
std::string g_luaResolveError;
std::uint64_t g_backendEpoch = 0;
bool g_protocolMismatchLogged = false;
std::mutex g_hostStatusMutex;
lua_bridge::HostState g_cachedHostState = lua_bridge::HostState::Unavailable;
std::uint64_t g_hostStatusCheckedAtMs = 0;

template <typename T>
bool ResolveFunction(HMODULE module, const char* name, T& target) {
    target = reinterpret_cast<T>(GetProcAddress(module, name));
    return target != nullptr;
}

int CountLoadedLua51Modules() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    int count = 0;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, L"lua51.dll") == 0) {
                ++count;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return count;
}

bool ResolveMoonLoaderLuaApiLocked() {
    if (!GetModuleHandleW(L"MoonLoader.asi")) {
        g_luaResolveError = "MoonLoader.asi is not loaded";
        return false;
    }
    if (CountLoadedLua51Modules() != 1) {
        g_luaResolveError = "expected exactly one loaded lua51.dll";
        return false;
    }
    g_lua = {};
    g_lua.module = GetModuleHandleW(L"lua51.dll");
    if (!g_lua.module) {
        g_luaResolveError = "lua51.dll is not loaded";
        return false;
    }

    bool ok = true;
    ok = ResolveFunction(g_lua.module, "lua_gettop", g_lua.gettop) && ok;
    ok = ResolveFunction(g_lua.module, "lua_settop", g_lua.settop) && ok;
    ok = ResolveFunction(g_lua.module, "lua_pushvalue", g_lua.pushvalue) && ok;
    ok = ResolveFunction(g_lua.module, "lua_pushnil", g_lua.pushnil) && ok;
    ok = ResolveFunction(g_lua.module, "lua_pushnumber", g_lua.pushnumber) && ok;
    ok = ResolveFunction(g_lua.module, "lua_pushboolean", g_lua.pushboolean) && ok;
    ok = ResolveFunction(g_lua.module, "lua_pushlstring", g_lua.pushlstring) && ok;
    ok = ResolveFunction(g_lua.module, "lua_pushcclosure", g_lua.pushcclosure) && ok;
    ok = ResolveFunction(g_lua.module, "lua_pushthread", g_lua.pushthread) && ok;
    ok = ResolveFunction(g_lua.module, "lua_createtable", g_lua.createtable) && ok;
    ok = ResolveFunction(g_lua.module, "lua_setfield", g_lua.setfield) && ok;
    ok = ResolveFunction(g_lua.module, "lua_getfield", g_lua.getfield) && ok;
    ok = ResolveFunction(g_lua.module, "lua_rawseti", g_lua.rawseti) && ok;
    ok = ResolveFunction(g_lua.module, "lua_rawgeti", g_lua.rawgeti) && ok;
    ok = ResolveFunction(g_lua.module, "lua_type", g_lua.type) && ok;
    ok = ResolveFunction(g_lua.module, "lua_toboolean", g_lua.toboolean) && ok;
    ok = ResolveFunction(g_lua.module, "lua_tonumber", g_lua.tonumber) && ok;
    ok = ResolveFunction(g_lua.module, "lua_tolstring", g_lua.tolstring) && ok;
    ok = ResolveFunction(g_lua.module, "lua_topointer", g_lua.topointer) && ok;
    ok = ResolveFunction(g_lua.module, "lua_pcall", g_lua.pcall) && ok;
    ok = ResolveFunction(g_lua.module, "luaL_ref", g_lua.l_ref) && ok;
    ok = ResolveFunction(g_lua.module, "luaL_unref", g_lua.l_unref) && ok;
    ok = ResolveFunction(g_lua.module, "luaL_error", g_lua.l_error) && ok;
    ok = ResolveFunction(g_lua.module, "lua_gethook", g_lua.gethook) && ok;
    ok = ResolveFunction(g_lua.module, "lua_gethookmask", g_lua.gethookmask) && ok;
    ok = ResolveFunction(g_lua.module, "lua_gethookcount", g_lua.gethookcount) && ok;
    ok = ResolveFunction(g_lua.module, "lua_sethook", g_lua.sethook) && ok;
    ok = ResolveFunction(g_lua.module, "luaJIT_setmode", g_lua.jit_setmode) && ok;
    if (!ok) {
        g_luaResolveError = "lua51.dll does not expose the required LuaJIT/Lua 5.1 C API";
        g_lua = {};
        return false;
    }

    g_luaResolveError.clear();
    debuglog::WriteInfo("[tags][code][lua] resolved pending MoonLoader Lua 5.1 C API from lua51.dll");
    return true;
}

void Pop(lua_State* state, int count) {
    if (count > 0) {
        g_lua.settop(state, -count - 1);
    }
}

void PushString(lua_State* state, std::string_view value) {
    g_lua.pushlstring(state, value.data(), value.size());
}

std::string LuaString(lua_State* state, int index) {
    size_t size = 0;
    const char* value = g_lua.tolstring(state, index, &size);
    return value ? std::string(value, size) : std::string();
}

bool ReadRequiredString(lua_State* state, int index, std::size_t limit, std::string& value) {
    if (g_lua.type(state, index) != LUA_TSTRING) {
        return false;
    }
    value = LuaString(state, index);
    return value.size() <= limit;
}

std::uint64_t ReadGeneration(lua_State* state, int index) {
    if (g_lua.type(state, index) != LUA_TNUMBER) {
        return 0;
    }
    const double value = g_lua.tonumber(state, index);
    return value >= 1.0
            && value <= kMaxExactLuaInteger
            && std::isfinite(value)
            && std::floor(value) == value
        ? static_cast<std::uint64_t>(value)
        : 0;
}

int ReturnError(lua_State* state, std::string_view error) {
    g_lua.pushnil(state);
    PushString(state, error);
    return 2;
}

int ReturnSuccess(lua_State* state) {
    g_lua.pushboolean(state, 1);
    return 1;
}

bool IsMainState(lua_State* state) {
    const int top = g_lua.gettop(state);
    const bool mainState = g_lua.pushthread(state) != 0;
    g_lua.settop(state, top);
    return mainState;
}

struct LuaSession {
    lua_State* state = nullptr;
    const void* vmIdentity = nullptr;
    std::string providerId{};
    std::uint64_t generation = 0;
    DWORD threadId = 0;
    std::uint64_t attachedAtMs = 0;
    bool ready = false;
    bool active = true;
    std::vector<int> references{};
};

std::mutex g_sessionsMutex;
std::unordered_map<const void*, std::shared_ptr<LuaSession>> g_sessions;

const void* LuaVmIdentity(lua_State* state) {
    if (!state) {
        return nullptr;
    }
    const int top = g_lua.gettop(state);
    g_lua.pushvalue(state, LUA_REGISTRYINDEX);
    const void* identity = g_lua.topointer(state, -1);
    g_lua.settop(state, top);
    return identity;
}

enum class LoadClaimState {
    Empty,
    Pending,
    Claimed,
};

struct ProviderLoadClaim {
    LoadClaimState state = LoadClaimState::Empty;
    std::uint64_t token = 0;
    std::uint64_t backendEpoch = 0;
    std::uint64_t generation = 0;
    DWORD ownerThreadId = 0;
    std::string providerId{};
    const void* claimedVmIdentity = nullptr;
};

std::mutex g_hostRoleMutex;
const void* g_controllerVmIdentity = nullptr;
ProviderLoadClaim g_providerLoadClaim;
std::uint64_t g_nextLoadClaimToken = 1;

void ClearProviderLoadClaimLocked() {
    g_providerLoadClaim = {};
}

bool IsControllerVmLocked(lua_State* state) {
    const void* vmIdentity = LuaVmIdentity(state);
    return vmIdentity && vmIdentity == g_controllerVmIdentity;
}

void ResetHostRoles() {
    std::lock_guard lock(g_hostRoleMutex);
    g_controllerVmIdentity = nullptr;
    ClearProviderLoadClaimLocked();
}

bool IsBridgeProtocolVerified(lua_State* state) {
    const int top = g_lua.gettop(state);
    g_lua.getfield(state, LUA_REGISTRYINDEX, kBridgeProtocolRegistryKey);
    const double version = g_lua.type(state, -1) == LUA_TNUMBER
        ? g_lua.tonumber(state, -1)
        : 0.0;
    g_lua.settop(state, top);
    return std::isfinite(version)
        && std::floor(version) == version
        && static_cast<int>(version) == kBridgeProtocolVersion;
}

std::shared_ptr<LuaSession> FindSession(
    lua_State* state,
    std::string_view providerId,
    std::uint64_t generation) {
    const void* vmIdentity = LuaVmIdentity(state);
    if (!vmIdentity) {
        return {};
    }
    std::lock_guard lock(g_sessionsMutex);
    const auto it = g_sessions.find(vmIdentity);
    if (it == g_sessions.end()
        || !it->second
        || !it->second->active
        || it->second->providerId != providerId
        || it->second->generation != generation) {
        return {};
    }
    return it->second;
}

std::shared_ptr<LuaSession> FindCurrentSession(
    lua_State* state,
    std::string_view providerId) {
    const std::shared_ptr<LuaSession> session = FindSession(
        state,
        providerId,
        codevars::Runtime::Instance().Generation());
    return session && GetCurrentThreadId() == session->threadId ? session : nullptr;
}

void ReleaseSession(const std::shared_ptr<LuaSession>& session) {
    if (!session) {
        return;
    }
    session->active = false;
    if (session->state && GetCurrentThreadId() == session->threadId) {
        for (const int reference : session->references) {
            if (reference != LUA_NOREF && reference != LUA_REFNIL) {
                g_lua.l_unref(session->state, LUA_REGISTRYINDEX, reference);
            }
        }
    }
    session->references.clear();
    std::lock_guard lock(g_sessionsMutex);
    const auto it = g_sessions.find(session->vmIdentity);
    if (it != g_sessions.end() && it->second == session) {
        g_sessions.erase(it);
    }
}

void ReleaseAllSessions(std::string_view detail) {
    std::vector<std::shared_ptr<LuaSession>> sessions;
    {
        std::lock_guard lock(g_sessionsMutex);
        sessions.reserve(g_sessions.size());
        for (const auto& [vmIdentity, session] : g_sessions) {
            (void)vmIdentity;
            if (session) {
                sessions.push_back(session);
            }
        }
    }
    for (const std::shared_ptr<LuaSession>& session : sessions) {
        codevars::Runtime::Instance().DetachProvider(session->providerId, session->generation, detail);
        ReleaseSession(session);
    }
}

struct HookBudget {
    int calls = 0;
};

thread_local HookBudget* g_activeHookBudget = nullptr;

void __cdecl TimeoutHook(lua_State* state, lua_Debug*) {
    if (!g_activeHookBudget) {
        return;
    }
    ++g_activeHookBudget->calls;
    if (g_activeHookBudget->calls >= kHookMaximumCalls) {
        g_lua.l_error(state, "HelperByOrc callback instruction limit exceeded");
    }
}

struct HookGuard {
    lua_State* state = nullptr;
    lua_Hook previousHook = nullptr;
    int previousMask = 0;
    int previousCount = 0;
    HookBudget* previousBudget = nullptr;
    HookBudget budget{};

    explicit HookGuard(lua_State* value)
        : state(value),
          previousHook(g_lua.gethook(value)),
          previousMask(g_lua.gethookmask(value)),
          previousCount(g_lua.gethookcount(value)),
          previousBudget(g_activeHookBudget) {
        g_activeHookBudget = &budget;
        g_lua.sethook(state, &TimeoutHook, LUA_MASKCOUNT, kHookInstructionInterval);
    }

    ~HookGuard() {
        g_lua.sethook(state, previousHook, previousMask, previousCount);
        g_activeHookBudget = previousBudget;
    }
};

codevars::ValueType ReadValueTypeOption(lua_State* state, int optionsIndex, bool& valid) {
    valid = true;
    if (g_lua.type(state, optionsIndex) != LUA_TTABLE) {
        return codevars::ValueType::String;
    }
    g_lua.getfield(state, optionsIndex, "result_type");
    if (g_lua.type(state, -1) == LUA_TNIL) {
        Pop(state, 1);
        g_lua.getfield(state, optionsIndex, "result");
    }
    if (g_lua.type(state, -1) == LUA_TNIL) {
        Pop(state, 1);
        return codevars::ValueType::String;
    }
    if (g_lua.type(state, -1) != LUA_TSTRING) {
        valid = false;
        Pop(state, 1);
        return codevars::ValueType::String;
    }
    const std::string value = LuaString(state, -1);
    Pop(state, 1);
    if (value == "nil") {
        return codevars::ValueType::Nil;
    }
    if (value == "int64" || value == "integer") {
        return codevars::ValueType::Int64;
    }
    if (value == "double" || value == "number") {
        return codevars::ValueType::Double;
    }
    if (value == "bool" || value == "boolean") {
        return codevars::ValueType::Bool;
    }
    if (value != "string" && value != "utf8") {
        valid = false;
    }
    return codevars::ValueType::String;
}

bool HasExplicitResultType(lua_State* state, int optionsIndex) {
    if (g_lua.type(state, optionsIndex) != LUA_TTABLE) {
        return false;
    }
    g_lua.getfield(state, optionsIndex, "result_type");
    bool explicitType = g_lua.type(state, -1) != LUA_TNIL;
    Pop(state, 1);
    if (!explicitType) {
        g_lua.getfield(state, optionsIndex, "result");
        explicitType = g_lua.type(state, -1) != LUA_TNIL;
        Pop(state, 1);
    }
    return explicitType;
}

codevars::VariableEffect ReadEffectOption(lua_State* state, int optionsIndex, bool& valid) {
    valid = true;
    if (g_lua.type(state, optionsIndex) != LUA_TTABLE) {
        return codevars::VariableEffect::Action;
    }
    g_lua.getfield(state, optionsIndex, "effect");
    if (g_lua.type(state, -1) == LUA_TNIL) {
        Pop(state, 1);
        return codevars::VariableEffect::Action;
    }
    if (g_lua.type(state, -1) != LUA_TSTRING) {
        valid = false;
        Pop(state, 1);
        return codevars::VariableEffect::Action;
    }
    const std::string value = LuaString(state, -1);
    Pop(state, 1);
    if (value == "pure") {
        return codevars::VariableEffect::Pure;
    }
    if (value != "action") {
        valid = false;
    }
    return codevars::VariableEffect::Action;
}

codevars::CachePolicy ReadCacheOption(
    lua_State* state,
    int optionsIndex,
    std::uint32_t& ttlMs,
    bool& valid) {
    ttlMs = 0;
    valid = true;
    if (g_lua.type(state, optionsIndex) != LUA_TTABLE) {
        return codevars::CachePolicy::Expansion;
    }

    g_lua.getfield(state, optionsIndex, "no_cache");
    const bool noCache = g_lua.type(state, -1) != LUA_TNIL && g_lua.toboolean(state, -1) != 0;
    Pop(state, 1);
    if (noCache) {
        return codevars::CachePolicy::None;
    }

    g_lua.getfield(state, optionsIndex, "cache");
    if (g_lua.type(state, -1) == LUA_TNIL) {
        Pop(state, 1);
        return codevars::CachePolicy::Expansion;
    }
    if (g_lua.type(state, -1) != LUA_TSTRING) {
        valid = false;
        Pop(state, 1);
        return codevars::CachePolicy::Expansion;
    }
    const std::string cache = LuaString(state, -1);
    Pop(state, 1);
    if (cache == "expansion") {
        return codevars::CachePolicy::Expansion;
    }
    if (cache == "none") {
        return codevars::CachePolicy::None;
    }
    if (cache == "event") {
        return codevars::CachePolicy::Event;
    }
    if (cache == "ttl") {
        g_lua.getfield(state, optionsIndex, "ttl_ms");
        const double ttl = g_lua.type(state, -1) == LUA_TNUMBER ? g_lua.tonumber(state, -1) : 0.0;
        Pop(state, 1);
        if (std::isfinite(ttl)
            && std::floor(ttl) == ttl
            && ttl >= 1.0
            && ttl <= 3600000.0) {
            ttlMs = static_cast<std::uint32_t>(ttl);
            return codevars::CachePolicy::Ttl;
        }
        valid = false;
        return codevars::CachePolicy::Expansion;
    }
    valid = false;
    return codevars::CachePolicy::Expansion;
}

std::string ReadExampleOption(lua_State* state, int optionsIndex, bool& valid) {
    valid = true;
    if (g_lua.type(state, optionsIndex) != LUA_TTABLE) {
        return {};
    }
    g_lua.getfield(state, optionsIndex, "example");
    if (g_lua.type(state, -1) == LUA_TNIL) {
        Pop(state, 1);
        return {};
    }
    if (g_lua.type(state, -1) != LUA_TSTRING) {
        valid = false;
        Pop(state, 1);
        return {};
    }
    std::string result = LuaString(state, -1);
    Pop(state, 1);
    if (result.size() > codevars::kMaxExampleBytes) {
        valid = false;
        result.clear();
    }
    return result;
}

codevars::EvaluationOutcome EvaluateLua(
    const std::weak_ptr<LuaSession>& weakSession,
    int reference,
    codevars::VariableKind kind,
    codevars::ValueType resultType,
    bool legacyCoercion,
    const codevars::EvaluationRequest& request) {
    codevars::EvaluationOutcome outcome;
    const std::shared_ptr<LuaSession> session = weakSession.lock();
    if (!session || !session->active || !session->ready || !request.context) {
        outcome.error = "Lua provider session is inactive";
        return outcome;
    }
    if (GetCurrentThreadId() != session->threadId
        || session->threadId != codevars::Runtime::Instance().OwnerThreadId()
        || session->generation != codevars::Runtime::Instance().Generation()) {
        outcome.error = "Lua callback attempted outside its owner thread or generation";
        return outcome;
    }

    lua_State* state = session->state;
    const int top = g_lua.gettop(state);
    g_lua.rawgeti(state, LUA_REGISTRYINDEX, reference);
    if (g_lua.type(state, -1) != LUA_TFUNCTION) {
        g_lua.settop(state, top);
        outcome.error = "Lua callback reference is invalid";
        return outcome;
    }

    int arguments = 0;
    if (kind == codevars::VariableKind::Function) {
        PushString(state, request.parameter);
        ++arguments;
    }
    const std::string_view thisbind = request.context->bindCommand;
    if (!thisbind.empty() || request.context->runningBindRuntimeId != 0) {
        PushString(state, thisbind);
    } else {
        g_lua.pushnil(state);
    }
    ++arguments;

    int status = 0;
    {
        HookGuard hook(state);
        status = g_lua.pcall(state, arguments, 1, 0);
    }
    if (status != 0) {
        outcome.error = LuaString(state, -1);
        outcome.timeout = outcome.error.find("instruction limit exceeded") != std::string::npos;
        g_lua.settop(state, top);
        return outcome;
    }

    const int type = g_lua.type(state, -1);
    if (type == LUA_TNIL) {
        outcome.ok = true;
        outcome.value = codevars::Value::Nil();
        g_lua.settop(state, top);
        return outcome;
    }

    switch (resultType) {
    case codevars::ValueType::String:
        if (type == LUA_TSTRING) {
            outcome.value = codevars::Value::String(LuaString(state, -1));
            outcome.ok = true;
        } else if (legacyCoercion && type == LUA_TNUMBER) {
            const double value = g_lua.tonumber(state, -1);
            outcome.value = codevars::Value::Double(value);
            const std::optional<std::string> formatted = codevars::FormatValue(outcome.value);
            if (formatted.has_value()) {
                outcome.value = codevars::Value::String(*formatted);
                outcome.ok = true;
            }
        } else if (legacyCoercion && type == LUA_TBOOLEAN) {
            outcome.value = codevars::Value::String(g_lua.toboolean(state, -1) ? "true" : "false");
            outcome.ok = true;
        }
        break;
    case codevars::ValueType::Int64:
        if (type == LUA_TNUMBER) {
            const double value = g_lua.tonumber(state, -1);
            if (std::isfinite(value)
                && std::floor(value) == value
                && value >= kInt64MinAsDouble
                && value < kInt64ExclusiveMaxAsDouble) {
                outcome.value = codevars::Value::Int64(static_cast<std::int64_t>(value));
                outcome.ok = true;
            }
        }
        break;
    case codevars::ValueType::Double:
        if (type == LUA_TNUMBER) {
            const double value = g_lua.tonumber(state, -1);
            if (std::isfinite(value)) {
                outcome.value = codevars::Value::Double(value);
                outcome.ok = true;
            }
        }
        break;
    case codevars::ValueType::Bool:
        if (type == LUA_TBOOLEAN) {
            outcome.value = codevars::Value::Bool(g_lua.toboolean(state, -1) != 0);
            outcome.ok = true;
        }
        break;
    case codevars::ValueType::Nil:
        break;
    }
    if (!outcome.ok) {
        outcome.error = "Lua callback returned a value with the wrong declared type";
    }
    g_lua.settop(state, top);
    return outcome;
}

int BridgeGeneration(lua_State* state) {
    if (!IsBridgeProtocolVerified(state)) {
        return ReturnError(state, "bridge.hello(protocol_version) is required");
    }
    g_lua.pushnumber(state, static_cast<double>(codevars::Runtime::Instance().Generation()));
    return 1;
}

int BridgeHello(lua_State* state) {
    const double version = g_lua.type(state, 1) == LUA_TNUMBER ? g_lua.tonumber(state, 1) : 0.0;
    if (!std::isfinite(version)
        || std::floor(version) != version
        || static_cast<int>(version) != kBridgeProtocolVersion) {
        return ReturnError(state, "HelperByOrc Lua bridge protocol mismatch");
    }
    g_lua.pushnumber(state, static_cast<double>(kBridgeProtocolVersion));
    g_lua.setfield(state, LUA_REGISTRYINDEX, kBridgeProtocolRegistryKey);
    g_lua.pushboolean(state, 1);
    PushString(state, "moonloader");
    return 2;
}

int BridgeActivate(lua_State* state) {
    if (!IsBridgeProtocolVerified(state)) {
        return ReturnError(state, "bridge.hello(protocol_version) is required");
    }

    const DWORD currentThreadId = GetCurrentThreadId();
    const DWORD ownerThreadId = codevars::Runtime::Instance().OwnerThreadId();
    const std::uint64_t generation = codevars::Runtime::Instance().Generation();
    std::uint64_t backendEpoch = 0;
    {
        std::lock_guard lock(g_backendMutex);
        if (g_backend != lua_bridge::Backend::MoonLoader) {
            return ReturnError(state, "MoonLoader backend is not active");
        }
        backendEpoch = g_backendEpoch;
    }
    if (ownerThreadId == 0 || currentThreadId != ownerThreadId || generation == 0) {
        return ReturnError(state, "MoonLoader host is not running on the established owner thread");
    }

    g_lua.pushboolean(state, 1);
    g_lua.pushnumber(state, static_cast<double>(generation));
    g_lua.pushnumber(state, static_cast<double>(backendEpoch));
    return 3;
}

int BridgeClaimRole(lua_State* state) {
    if (!IsBridgeProtocolVerified(state)) {
        return ReturnError(state, "bridge.hello(protocol_version) is required");
    }
    const std::uint64_t requestedEpoch = ReadGeneration(state, 1);
    const void* vmIdentity = LuaVmIdentity(state);
    if (requestedEpoch == 0 || !vmIdentity) {
        return ReturnError(state, "invalid host role claim");
    }

    const DWORD currentThreadId = GetCurrentThreadId();
    const DWORD ownerThreadId = codevars::Runtime::Instance().OwnerThreadId();
    const std::uint64_t generation = codevars::Runtime::Instance().Generation();
    {
        std::lock_guard lock(g_backendMutex);
        if (g_backend != lua_bridge::Backend::MoonLoader || requestedEpoch != g_backendEpoch) {
            return ReturnError(state, "stale MoonLoader backend epoch");
        }
    }
    if (ownerThreadId == 0 || currentThreadId != ownerThreadId) {
        return ReturnError(state, "host role must be claimed on the established owner thread");
    }

    std::lock_guard lock(g_hostRoleMutex);
    if (g_controllerVmIdentity == vmIdentity) {
        PushString(state, "controller");
        return 1;
    }
    if (!g_controllerVmIdentity) {
        g_controllerVmIdentity = vmIdentity;
        PushString(state, "controller");
        return 1;
    }
    if (g_providerLoadClaim.state != LoadClaimState::Pending
        || g_providerLoadClaim.backendEpoch != requestedEpoch
        || g_providerLoadClaim.generation != generation
        || g_providerLoadClaim.ownerThreadId != currentThreadId) {
        return ReturnError(state, "host state has no current provider load claim");
    }
    if (!IsMainState(state)) {
        return ReturnError(state, "provider role must be claimed in the script main Lua state");
    }

    g_providerLoadClaim.state = LoadClaimState::Claimed;
    g_providerLoadClaim.claimedVmIdentity = vmIdentity;
    PushString(state, "provider");
    PushString(state, g_providerLoadClaim.providerId);
    g_lua.pushnumber(state, static_cast<double>(g_providerLoadClaim.generation));
    g_lua.pushnumber(state, static_cast<double>(g_providerLoadClaim.token));
    return 4;
}

int BridgeBeginProviderLoad(lua_State* state) {
    if (!IsBridgeProtocolVerified(state)) {
        return ReturnError(state, "bridge.hello(protocol_version) is required");
    }
    std::string providerId;
    if (!ReadRequiredString(state, 1, codevars::kMaxProviderIdBytes, providerId)) {
        return ReturnError(state, "invalid provider id");
    }
    const std::uint64_t generation = ReadGeneration(state, 2);
    const std::uint64_t requestedEpoch = ReadGeneration(state, 3);
    if (generation == 0 || requestedEpoch == 0) {
        return ReturnError(state, "invalid provider load generation or backend epoch");
    }

    const DWORD currentThreadId = GetCurrentThreadId();
    const DWORD ownerThreadId = codevars::Runtime::Instance().OwnerThreadId();
    const std::uint64_t currentGeneration = codevars::Runtime::Instance().Generation();
    const std::vector<codevars::LuaPlanEntry> plan = codevars::Runtime::Instance().LuaPlan();
    {
        std::lock_guard lock(g_backendMutex);
        if (g_backend != lua_bridge::Backend::MoonLoader || requestedEpoch != g_backendEpoch) {
            return ReturnError(state, "stale MoonLoader backend epoch");
        }
    }
    if (ownerThreadId == 0
        || currentThreadId != ownerThreadId
        || generation != currentGeneration) {
        return ReturnError(state, "provider load is not on the current owner thread/generation");
    }
    const auto provider = std::find_if(
        plan.begin(),
        plan.end(),
        [&](const codevars::LuaPlanEntry& entry) {
            return entry.id == providerId && entry.enabled && entry.generation == generation;
        });
    if (provider == plan.end()) {
        return ReturnError(state, "provider is not enabled in the current Lua plan");
    }

    std::lock_guard lock(g_hostRoleMutex);
    if (!IsControllerVmLocked(state)) {
        return ReturnError(state, "only the active Host controller may start a provider");
    }
    if (g_providerLoadClaim.state != LoadClaimState::Empty) {
        return ReturnError(state, "another provider load claim is still active");
    }
    if (g_nextLoadClaimToken == 0
        || static_cast<double>(g_nextLoadClaimToken) > kMaxExactLuaInteger) {
        g_nextLoadClaimToken = 1;
    }

    g_providerLoadClaim.state = LoadClaimState::Pending;
    g_providerLoadClaim.token = g_nextLoadClaimToken++;
    g_providerLoadClaim.backendEpoch = requestedEpoch;
    g_providerLoadClaim.generation = generation;
    g_providerLoadClaim.ownerThreadId = currentThreadId;
    g_providerLoadClaim.providerId = std::move(providerId);
    g_lua.pushnumber(state, static_cast<double>(g_providerLoadClaim.token));
    return 1;
}

int BridgeProviderLoadStatus(lua_State* state) {
    if (!IsBridgeProtocolVerified(state)) {
        return ReturnError(state, "bridge.hello(protocol_version) is required");
    }
    const std::uint64_t token = ReadGeneration(state, 1);
    const std::uint64_t requestedEpoch = ReadGeneration(state, 2);
    std::lock_guard lock(g_hostRoleMutex);
    if (!IsControllerVmLocked(state)) {
        return ReturnError(state, "only the active Host controller may inspect a provider load");
    }
    if (token == 0
        || requestedEpoch == 0
        || g_providerLoadClaim.state == LoadClaimState::Empty
        || g_providerLoadClaim.token != token
        || g_providerLoadClaim.backendEpoch != requestedEpoch) {
        return ReturnError(state, "provider load claim is no longer current");
    }
    PushString(
        state,
        g_providerLoadClaim.state == LoadClaimState::Claimed ? "claimed" : "pending");
    return 1;
}

int BridgeFinishProviderLoad(lua_State* state) {
    if (!IsBridgeProtocolVerified(state)) {
        return ReturnError(state, "bridge.hello(protocol_version) is required");
    }
    const std::uint64_t token = ReadGeneration(state, 1);
    const std::uint64_t requestedEpoch = ReadGeneration(state, 2);
    std::lock_guard lock(g_hostRoleMutex);
    if (!IsControllerVmLocked(state)) {
        return ReturnError(state, "only the active Host controller may finish a provider load");
    }
    if (token == 0
        || requestedEpoch == 0
        || g_providerLoadClaim.state != LoadClaimState::Claimed
        || g_providerLoadClaim.token != token
        || g_providerLoadClaim.backendEpoch != requestedEpoch) {
        return ReturnError(state, "provider load claim was not claimed");
    }
    ClearProviderLoadClaimLocked();
    return ReturnSuccess(state);
}

int BridgeCancelProviderLoad(lua_State* state) {
    if (!IsBridgeProtocolVerified(state)) {
        return ReturnError(state, "bridge.hello(protocol_version) is required");
    }
    const std::uint64_t token = ReadGeneration(state, 1);
    const std::uint64_t requestedEpoch = ReadGeneration(state, 2);
    std::lock_guard lock(g_hostRoleMutex);
    if (!IsControllerVmLocked(state)) {
        return ReturnError(state, "only the active Host controller may cancel a provider load");
    }
    if (token == 0
        || requestedEpoch == 0
        || g_providerLoadClaim.state == LoadClaimState::Empty
        || g_providerLoadClaim.token != token
        || g_providerLoadClaim.backendEpoch != requestedEpoch) {
        return ReturnError(state, "provider load claim is no longer current");
    }
    ClearProviderLoadClaimLocked();
    return ReturnSuccess(state);
}

int BridgeReleaseRole(lua_State* state) {
    if (!IsBridgeProtocolVerified(state)) {
        return ReturnError(state, "bridge.hello(protocol_version) is required");
    }
    const void* vmIdentity = LuaVmIdentity(state);
    std::lock_guard lock(g_hostRoleMutex);
    if (vmIdentity && vmIdentity == g_controllerVmIdentity) {
        g_controllerVmIdentity = nullptr;
        ClearProviderLoadClaimLocked();
    }
    return ReturnSuccess(state);
}

int BridgePlan(lua_State* state) {
    if (!IsBridgeProtocolVerified(state)) {
        return ReturnError(state, "bridge.hello(protocol_version) is required");
    }
    const std::vector<codevars::LuaPlanEntry> plan = codevars::Runtime::Instance().LuaPlan();
    g_lua.createtable(state, static_cast<int>(plan.size()), 0);
    int index = 1;
    for (const codevars::LuaPlanEntry& item : plan) {
        g_lua.createtable(state, 0, 5);
        PushString(state, item.id);
        g_lua.setfield(state, -2, "id");
        PushString(state, item.pathUtf8);
        g_lua.setfield(state, -2, "path");
        g_lua.pushboolean(state, item.enabled ? 1 : 0);
        g_lua.setfield(state, -2, "enabled");
        g_lua.pushnumber(state, static_cast<double>(item.generation));
        g_lua.setfield(state, -2, "generation");
        PushString(state, codevars::ProviderStateName(item.state));
        g_lua.setfield(state, -2, "state");
        g_lua.rawseti(state, -2, index++);
    }
    return 1;
}

int BridgeAttach(lua_State* state) {
    if (!IsBridgeProtocolVerified(state)) {
        return ReturnError(state, "bridge.hello(protocol_version) is required");
    }
    std::string providerId;
    if (!ReadRequiredString(state, 1, codevars::kMaxProviderIdBytes, providerId)) {
        return ReturnError(state, "invalid provider id");
    }
    const std::uint64_t generation = ReadGeneration(state, 2);
    if (generation == 0 || !IsMainState(state)) {
        return ReturnError(state, "attach must run in the provider main Lua state");
    }
    {
        std::lock_guard lock(g_backendMutex);
        if (g_backend != lua_bridge::Backend::MoonLoader) {
            return ReturnError(state, "MoonLoader backend is not active");
        }
    }
    const void* vmIdentity = LuaVmIdentity(state);
    const std::uint64_t claimToken = ReadGeneration(state, 3);
    {
        std::lock_guard lock(g_hostRoleMutex);
        if (claimToken == 0
            || !vmIdentity
            || g_providerLoadClaim.state != LoadClaimState::Claimed
            || g_providerLoadClaim.claimedVmIdentity != vmIdentity
            || g_providerLoadClaim.token != claimToken
            || g_providerLoadClaim.providerId != providerId
            || g_providerLoadClaim.generation != generation) {
            return ReturnError(state, "provider state has no matching load claim");
        }
    }

    std::string error;
    const DWORD threadId = GetCurrentThreadId();
    if (!codevars::Runtime::Instance().BeginProviderSession(
            providerId,
            generation,
            threadId,
            error)) {
        return ReturnError(state, error);
    }

    auto session = std::make_shared<LuaSession>();
    session->state = state;
    session->vmIdentity = vmIdentity;
    if (!session->vmIdentity) {
        codevars::Runtime::Instance().DetachProvider(providerId, generation, "Lua VM identity is unavailable");
        return ReturnError(state, "Lua VM identity is unavailable");
    }
    session->providerId = providerId;
    session->generation = generation;
    session->threadId = threadId;
    session->attachedAtMs = GetTickCount64();
    {
        std::lock_guard lock(g_sessionsMutex);
        if (const auto existing = g_sessions.find(session->vmIdentity); existing != g_sessions.end()) {
            existing->second->active = false;
        }
        g_sessions[session->vmIdentity] = session;
    }
    debuglog::WriteInfo(
        "[tags][code][lua] attached provider=%s generation=%llu tid=%lu",
        providerId.c_str(),
        static_cast<unsigned long long>(generation),
        static_cast<unsigned long>(threadId));
    return ReturnSuccess(state);
}

int RegisterLuaVariable(lua_State* state, codevars::VariableKind kind) {
    std::string providerId;
    std::string name;
    std::string description;
    if (!ReadRequiredString(state, 1, codevars::kMaxProviderIdBytes, providerId)
        || !ReadRequiredString(state, 3, codevars::kMaxVariableNameBytes, name)
        || !ReadRequiredString(state, 4, codevars::kMaxDescriptionBytes, description)
        || g_lua.type(state, 5) != LUA_TFUNCTION) {
        return ReturnError(state, "invalid registration arguments");
    }
    const std::uint64_t generation = ReadGeneration(state, 2);
    const std::shared_ptr<LuaSession> session = FindSession(state, providerId, generation);
    if (!session || GetCurrentThreadId() != session->threadId) {
        return ReturnError(state, "Lua provider session is not active");
    }

    const int optionsIndex = 6;
    std::uint32_t ttlMs = 0;
    const bool legacyCoercion = !HasExplicitResultType(state, optionsIndex);
    bool effectValid = false;
    bool resultTypeValid = false;
    bool cacheValid = false;
    bool exampleValid = false;
    codevars::Registration registration;
    registration.providerId = providerId;
    registration.kind = kind;
    registration.effect = ReadEffectOption(state, optionsIndex, effectValid);
    registration.resultType = ReadValueTypeOption(state, optionsIndex, resultTypeValid);
    registration.cachePolicy = ReadCacheOption(state, optionsIndex, ttlMs, cacheValid);
    registration.ttlMs = ttlMs;
    registration.name = name;
    registration.description = description;
    registration.example = ReadExampleOption(state, optionsIndex, exampleValid);
    if (!effectValid || !resultTypeValid || !cacheValid || !exampleValid) {
        return ReturnError(state, "invalid effect, result_type, cache, ttl_ms, or example option");
    }

    if (g_lua.jit_setmode(state, 5, LUAJIT_MODE_ALLFUNC | LUAJIT_MODE_OFF) == 0) {
        return ReturnError(state, "failed to disable JIT for variable callback timeout");
    }
    g_lua.pushvalue(state, 5);
    const int reference = g_lua.l_ref(state, LUA_REGISTRYINDEX);
    if (reference == LUA_NOREF || reference == LUA_REFNIL) {
        return ReturnError(state, "failed to retain Lua callback");
    }
    session->references.push_back(reference);
    registration.callback = [
                                weakSession = std::weak_ptr<LuaSession>(session),
                                reference,
                                kind,
                                resultType = registration.resultType,
                                legacyCoercion](const codevars::EvaluationRequest& request) {
        return EvaluateLua(weakSession, reference, kind, resultType, legacyCoercion, request);
    };

    std::string error;
    if (!codevars::Runtime::Instance().RegisterVariable(std::move(registration), error)) {
        g_lua.l_unref(state, LUA_REGISTRYINDEX, reference);
        session->references.erase(
            std::remove(session->references.begin(), session->references.end(), reference),
            session->references.end());
        return ReturnError(state, error);
    }
    return ReturnSuccess(state);
}

int BridgeRegisterSimple(lua_State* state) {
    return RegisterLuaVariable(state, codevars::VariableKind::Simple);
}

int BridgeRegisterFunction(lua_State* state) {
    return RegisterLuaVariable(state, codevars::VariableKind::Function);
}

int BridgeRunProviderChunk(lua_State* state) {
    std::string providerId;
    if (!ReadRequiredString(state, 1, codevars::kMaxProviderIdBytes, providerId)
        || g_lua.type(state, 3) != LUA_TFUNCTION) {
        return ReturnError(state, "invalid provider chunk arguments");
    }
    const std::uint64_t generation = ReadGeneration(state, 2);
    const std::shared_ptr<LuaSession> session = FindSession(state, providerId, generation);
    if (!session || !IsMainState(state) || GetCurrentThreadId() != session->threadId) {
        return ReturnError(state, "provider chunk must run in the attached main Lua state");
    }

    const int top = g_lua.gettop(state);
    if (g_lua.jit_setmode(state, 3, LUAJIT_MODE_ALLFUNC | LUAJIT_MODE_OFF) == 0) {
        return ReturnError(state, "failed to disable JIT for provider load timeout");
    }
    g_lua.pushvalue(state, 3);
    int status = 0;
    {
        HookGuard hook(state);
        status = g_lua.pcall(state, 0, 0, 0);
    }
    if (status != 0) {
        const std::string error = LuaString(state, -1);
        g_lua.settop(state, top);
        return ReturnError(state, error);
    }
    g_lua.settop(state, top);
    return ReturnSuccess(state);
}

int BridgeReady(lua_State* state) {
    std::string providerId;
    if (!ReadRequiredString(state, 1, codevars::kMaxProviderIdBytes, providerId)
        || g_lua.type(state, 3) != LUA_TFUNCTION) {
        return ReturnError(state, "invalid ready arguments");
    }
    const std::uint64_t generation = ReadGeneration(state, 2);
    const std::shared_ptr<LuaSession> session = FindSession(state, providerId, generation);
    if (!session || !IsMainState(state) || GetCurrentThreadId() != session->threadId) {
        return ReturnError(state, "ready must run in the attached main Lua state");
    }

    const int top = g_lua.gettop(state);
    if (g_lua.jit_setmode(state, 3, LUAJIT_MODE_ALLFUNC | LUAJIT_MODE_OFF) == 0) {
        return ReturnError(state, "failed to disable JIT for backend self-test");
    }
    g_lua.pushvalue(state, 3);
    int status = 0;
    {
        HookGuard hook(state);
        status = g_lua.pcall(state, 0, 1, 0);
    }
    if (status != 0) {
        const std::string error = LuaString(state, -1);
        g_lua.settop(state, top);
        codevars::Runtime::Instance().MarkProviderFault(providerId, error);
        return ReturnError(state, "backend self-test failed: " + error);
    }
    const int resultType = g_lua.type(state, -1);
    const bool selfTestOk = resultType == LUA_TSTRING || resultType == LUA_TNUMBER;
    g_lua.settop(state, top);
    if (!selfTestOk) {
        codevars::Runtime::Instance().MarkProviderFault(providerId, "backend self-test returned an invalid value");
        return ReturnError(state, "backend self-test returned an invalid value");
    }

    std::string error;
    if (!codevars::Runtime::Instance().MarkProviderReady(providerId, generation, error)) {
        return ReturnError(state, error);
    }
    session->ready = true;
    debuglog::WriteInfo(
        "[tags][code][lua] ready provider=%s generation=%llu load=%llums",
        providerId.c_str(),
        static_cast<unsigned long long>(generation),
        static_cast<unsigned long long>(GetTickCount64() - session->attachedAtMs));
    return ReturnSuccess(state);
}

int BridgeDetach(lua_State* state) {
    std::string providerId;
    if (!ReadRequiredString(state, 1, codevars::kMaxProviderIdBytes, providerId)) {
        return ReturnError(state, "invalid provider id");
    }
    const std::uint64_t generation = ReadGeneration(state, 2);
    std::string detail = g_lua.type(state, 3) == LUA_TSTRING
        ? LuaString(state, 3)
        : "detached";
    if (detail.size() > codevars::kMaxDescriptionBytes) {
        detail.resize(codevars::kMaxDescriptionBytes);
    }
    const std::shared_ptr<LuaSession> session = FindSession(state, providerId, generation);
    if (session) {
        codevars::Runtime::Instance().DetachProvider(providerId, generation, detail);
        ReleaseSession(session);
    }
    return ReturnSuccess(state);
}

int BridgeFault(lua_State* state) {
    std::string providerId;
    if (!ReadRequiredString(state, 1, codevars::kMaxProviderIdBytes, providerId)) {
        return ReturnError(state, "invalid provider id");
    }
    const std::uint64_t generation = ReadGeneration(state, 2);
    const std::string detail = g_lua.type(state, 3) == LUA_TSTRING
        ? LuaString(state, 3)
        : "Lua provider failed";
    const std::shared_ptr<LuaSession> session = FindSession(state, providerId, generation);
    if (!session) {
        return ReturnError(state, "Lua provider session is not active");
    }
    if (generation != codevars::Runtime::Instance().Generation()
        || GetCurrentThreadId() != session->threadId) {
        ReleaseSession(session);
        return ReturnError(state, "Lua provider session is stale");
    }
    codevars::Runtime::Instance().MarkProviderFault(providerId, detail);
    ReleaseSession(session);
    return ReturnSuccess(state);
}

int BridgeReadSource(lua_State* state) {
    std::string providerId;
    if (!ReadRequiredString(state, 1, codevars::kMaxProviderIdBytes, providerId)) {
        return ReturnError(state, "invalid provider id");
    }
    const std::uint64_t generation = ReadGeneration(state, 2);
    const std::shared_ptr<LuaSession> session = FindSession(state, providerId, generation);
    if (!session) {
        return ReturnError(state, "provider session is not active");
    }

    const std::vector<codevars::LuaPlanEntry> plan = codevars::Runtime::Instance().LuaPlan();
    const auto it = std::find_if(plan.begin(), plan.end(), [&](const codevars::LuaPlanEntry& entry) {
        return entry.id == providerId && entry.generation == generation && entry.enabled;
    });
    if (it == plan.end()) {
        return ReturnError(state, "provider source is not in the active plan");
    }

    const int wideSize = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        it->pathUtf8.data(),
        static_cast<int>(it->pathUtf8.size()),
        nullptr,
        0);
    if (wideSize <= 0) {
        return ReturnError(state, "provider path is not valid UTF-8");
    }
    std::wstring path(static_cast<std::size_t>(wideSize), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        it->pathUtf8.data(),
        static_cast<int>(it->pathUtf8.size()),
        path.data(),
        wideSize);
    const std::filesystem::path sourcePath(path);
    std::error_code sizeError;
    const std::uint64_t sourceSize = std::filesystem::file_size(sourcePath, sizeError);
    if (sizeError || sourceSize > kMaxLuaSourceBytes) {
        return ReturnError(state, "provider source is unavailable or too large");
    }
    std::ifstream file(sourcePath, std::ios::binary);
    if (!file) {
        return ReturnError(state, "failed to open provider source");
    }
    std::string source(static_cast<std::size_t>(sourceSize), '\0');
    if (sourceSize > 0) {
        file.read(source.data(), static_cast<std::streamsize>(source.size()));
        if (!file || static_cast<std::uint64_t>(file.gcount()) != sourceSize) {
            return ReturnError(state, "failed to read provider source");
        }
    }
    char extra = '\0';
    if (file.read(&extra, 1)) {
        return ReturnError(state, "provider source changed while loading");
    }
    PushString(state, source);
    PushString(state, it->pathUtf8);
    return 2;
}

int BridgeInvalidate(lua_State* state) {
    std::string providerId;
    std::string name;
    if (!ReadRequiredString(state, 1, codevars::kMaxProviderIdBytes, providerId)
        || !ReadRequiredString(state, 2, codevars::kMaxVariableNameBytes, name)) {
        return ReturnError(state, "invalid invalidate arguments");
    }
    if (!FindCurrentSession(state, providerId)) {
        return ReturnError(state, "Lua provider session is not active");
    }
    return codevars::Runtime::Instance().Invalidate(providerId, name)
        ? ReturnSuccess(state)
        : ReturnError(state, "variable is not registered or provider is inactive");
}

codevars::Value ReadPublishedValue(lua_State* state, int index, bool& valid) {
    valid = true;
    switch (g_lua.type(state, index)) {
    case LUA_TNIL:
        return codevars::Value::Nil();
    case LUA_TSTRING:
        return codevars::Value::String(LuaString(state, index));
    case LUA_TNUMBER:
        return codevars::Value::Double(g_lua.tonumber(state, index));
    case LUA_TBOOLEAN:
        return codevars::Value::Bool(g_lua.toboolean(state, index) != 0);
    default:
        valid = false;
        return codevars::Value::Nil();
    }
}

int BridgePublish(lua_State* state) {
    std::string providerId;
    std::string name;
    if (!ReadRequiredString(state, 1, codevars::kMaxProviderIdBytes, providerId)
        || !ReadRequiredString(state, 2, codevars::kMaxVariableNameBytes, name)) {
        return ReturnError(state, "invalid publish arguments");
    }
    bool valid = false;
    codevars::Value value = ReadPublishedValue(state, 3, valid);
    if (!valid) {
        return ReturnError(state, "published value must be nil, string, number, or boolean");
    }
    if (!FindCurrentSession(state, providerId)) {
        return ReturnError(state, "Lua provider session is not active");
    }
    std::string error;
    return codevars::Runtime::Instance().Publish(providerId, name, std::move(value), error)
        ? ReturnSuccess(state)
        : ReturnError(state, error);
}

int BridgeLog(lua_State* state) {
    std::string providerId;
    std::string message;
    if (!ReadRequiredString(state, 1, codevars::kMaxProviderIdBytes, providerId)
        || !ReadRequiredString(state, 3, 2048, message)) {
        return ReturnError(state, "invalid log arguments");
    }
    if (!FindCurrentSession(state, providerId)) {
        return ReturnError(state, "Lua provider session is not active");
    }
    const std::string level = g_lua.type(state, 2) == LUA_TSTRING ? LuaString(state, 2) : "info";
    if (level == "error") {
        debuglog::WriteError("[tags][code][lua] provider=%s %s", providerId.c_str(), message.c_str());
    } else {
        debuglog::WriteInfo("[tags][code][lua] provider=%s %s", providerId.c_str(), message.c_str());
    }
    return ReturnSuccess(state);
}

void SetFunction(lua_State* state, const char* name, lua_CFunction function) {
    g_lua.pushcclosure(state, function, 0);
    g_lua.setfield(state, -2, name);
}

void PushBridgeTable(lua_State* state) {
    g_lua.createtable(state, 0, 21);
    SetFunction(state, "hello", &BridgeHello);
    SetFunction(state, "activate", &BridgeActivate);
    SetFunction(state, "claim_role", &BridgeClaimRole);
    SetFunction(state, "begin_provider_load", &BridgeBeginProviderLoad);
    SetFunction(state, "provider_load_status", &BridgeProviderLoadStatus);
    SetFunction(state, "finish_provider_load", &BridgeFinishProviderLoad);
    SetFunction(state, "cancel_provider_load", &BridgeCancelProviderLoad);
    SetFunction(state, "release_role", &BridgeReleaseRole);
    SetFunction(state, "generation", &BridgeGeneration);
    SetFunction(state, "plan", &BridgePlan);
    SetFunction(state, "attach", &BridgeAttach);
    SetFunction(state, "register_simple", &BridgeRegisterSimple);
    SetFunction(state, "register_function", &BridgeRegisterFunction);
    SetFunction(state, "run_provider_chunk", &BridgeRunProviderChunk);
    SetFunction(state, "ready", &BridgeReady);
    SetFunction(state, "detach", &BridgeDetach);
    SetFunction(state, "fault", &BridgeFault);
    SetFunction(state, "read_source", &BridgeReadSource);
    SetFunction(state, "invalidate", &BridgeInvalidate);
    SetFunction(state, "publish", &BridgePublish);
    SetFunction(state, "log", &BridgeLog);
}

HMODULE CurrentModule() {
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&CurrentModule),
        &module);
    return module;
}

std::pair<const char*, std::size_t> EmbeddedHostSource() {
    HMODULE module = CurrentModule();
    HRSRC resource = module ? FindResourceW(module, MAKEINTRESOURCEW(IDR_LUA_VARS_HOST), MAKEINTRESOURCEW(10)) : nullptr;
    HGLOBAL loaded = resource ? LoadResource(module, resource) : nullptr;
    const DWORD size = resource ? SizeofResource(module, resource) : 0;
    const void* data = loaded ? LockResource(loaded) : nullptr;
    return { static_cast<const char*>(data), static_cast<std::size_t>(size) };
}

std::filesystem::path MoonLoaderHostPath() {
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path))) == 0) {
        return {};
    }
    return std::filesystem::path(path).parent_path() / L"moonloader" / L"HelperByOrcVarsHost.lua";
}

bool FileMatchesEmbeddedHost(const std::filesystem::path& path) {
    const auto [data, size] = EmbeddedHostSource();
    if (!data || size == 0) {
        return false;
    }
    std::error_code sizeError;
    if (std::filesystem::file_size(path, sizeError) != size || sizeError) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    std::string content(size, '\0');
    if (size > 0) {
        file.read(content.data(), static_cast<std::streamsize>(size));
    }
    return file && std::equal(content.begin(), content.end(), data);
}

lua_bridge::HostState CachedHostState(bool moonLoaderAvailable, const std::filesystem::path& path) {
    std::lock_guard lock(g_hostStatusMutex);
    const std::uint64_t now = GetTickCount64();
    if (g_hostStatusCheckedAtMs != 0 && now - g_hostStatusCheckedAtMs < 1000) {
        return g_cachedHostState;
    }
    g_hostStatusCheckedAtMs = now;
    if (!moonLoaderAvailable || path.empty()) {
        g_cachedHostState = lua_bridge::HostState::Unavailable;
        return g_cachedHostState;
    }
    std::error_code existsError;
    const bool exists = std::filesystem::exists(path, existsError);
    g_cachedHostState = existsError
        ? lua_bridge::HostState::Unavailable
        : !exists ? lua_bridge::HostState::Missing
                  : FileMatchesEmbeddedHost(path) ? lua_bridge::HostState::Current : lua_bridge::HostState::Outdated;
    return g_cachedHostState;
}

} // namespace

extern "C" __declspec(dllexport) int __cdecl luaopen_helperbyorc_bridge(lua_State* state) {
    if (!state) {
        return 0;
    }

    std::lock_guard lock(g_backendMutex);
    const DWORD ownerThreadId = codevars::Runtime::Instance().OwnerThreadId();
    if (ownerThreadId == 0 || GetCurrentThreadId() != ownerThreadId) {
        return 0;
    }
    if (!g_lua.module && !ResolveMoonLoaderLuaApiLocked()) {
        return 0;
    }

    const double protocol = g_lua.type(state, 1) == LUA_TNUMBER ? g_lua.tonumber(state, 1) : 0.0;
    if (!std::isfinite(protocol)
        || std::floor(protocol) != protocol
        || static_cast<int>(protocol) != kBridgeProtocolVersion) {
        if (!g_protocolMismatchLogged) {
            g_protocolMismatchLogged = true;
            debuglog::WriteError("[tags][code][lua] rejected MoonLoader host with incompatible bridge protocol");
        }
        return 0;
    }
    if (g_backend == lua_bridge::Backend::Waiting) {
        g_backend = lua_bridge::Backend::MoonLoader;
        ++g_backendEpoch;
        if (g_backendEpoch == 0 || static_cast<double>(g_backendEpoch) > kMaxExactLuaInteger) {
            g_backendEpoch = 1;
        }
        debuglog::WriteInfo(
            "[tags][code][lua] selected MoonLoader backend protocol=%d epoch=%llu tid=%lu",
            kBridgeProtocolVersion,
            static_cast<unsigned long long>(g_backendEpoch),
            static_cast<unsigned long>(ownerThreadId));
    }
    if (g_backend != lua_bridge::Backend::MoonLoader) {
        return 0;
    }

    PushBridgeTable(state);
    return 1;
}

namespace lua_bridge {

const char* BackendName(Backend backend) {
    switch (backend) {
    case Backend::MoonLoader:
        return "MoonLoader";
    case Backend::Waiting:
    default:
        return "Waiting for MoonLoader";
    }
}

void Shutdown() {
    std::lock_guard lock(g_backendMutex);
    ReleaseAllSessions("game shutdown");
    ResetHostRoles();
    {
        std::lock_guard hostLock(g_hostStatusMutex);
        g_cachedHostState = HostState::Unavailable;
        g_hostStatusCheckedAtMs = 0;
    }
}

Status CurrentStatus() {
    Status status;
    status.variablesRoot = AppConfig::Instance().LuaVariablesRoot();
    status.moonLoaderAvailable = GetModuleHandleW(L"MoonLoader.asi") != nullptr;
    status.hostPath = MoonLoaderHostPath();
    {
        std::lock_guard lock(g_backendMutex);
        status.backend = g_backend;
        if (g_backend == Backend::Waiting) {
            status.detail = !g_luaResolveError.empty()
                ? g_luaResolveError
                : status.moonLoaderAvailable
                    ? "waiting for HelperByOrcVarsHost.lua"
                    : "MoonLoader.asi is not loaded; Lua providers are unavailable";
        } else if (g_backend == Backend::MoonLoader) {
            status.detail = "providers run in MoonLoader Lua states";
        }
    }

    status.host = CachedHostState(status.moonLoaderAvailable, status.hostPath);
    return status;
}

bool InstallOrUpdateMoonLoaderHost(std::string* error) {
    const auto fail = [&](std::string message) {
        if (error) {
            *error = message;
        }
        debuglog::WriteError("[tags][code][lua] MoonLoader host install failed: %s", message.c_str());
        return false;
    };
    if (!GetModuleHandleW(L"MoonLoader.asi")) {
        return fail("MoonLoader.asi is not loaded");
    }
    const std::filesystem::path target = MoonLoaderHostPath();
    const auto [source, sourceSize] = EmbeddedHostSource();
    if (target.empty() || !source || sourceSize == 0) {
        return fail("embedded HelperByOrcVarsHost.lua is unavailable");
    }
    if (FileMatchesEmbeddedHost(target)) {
        return true;
    }

    std::error_code directoryError;
    std::filesystem::create_directories(target.parent_path(), directoryError);
    if (directoryError) {
        return fail("failed to create moonloader directory");
    }

    std::error_code existsError;
    const bool targetExists = std::filesystem::exists(target, existsError);
    if (existsError) {
        return fail("failed to inspect existing MoonLoader host");
    }
    if (targetExists) {
        const std::filesystem::path backup = target.wstring() + L".bak";
        std::error_code backupError;
        std::filesystem::copy_file(target, backup, std::filesystem::copy_options::overwrite_existing, backupError);
        if (backupError) {
            return fail("failed to back up existing MoonLoader host");
        }
    }

    const std::filesystem::path temporary = target.wstring() + L".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) {
        return fail("failed to create temporary MoonLoader host");
    }
    file.write(source, static_cast<std::streamsize>(sourceSize));
    file.close();
    if (!file || !MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        return fail("failed to atomically install MoonLoader host");
    }

    debuglog::WriteInfo("[tags][code][lua] installed MoonLoader host: %ls", target.c_str());
    {
        std::lock_guard lock(g_hostStatusMutex);
        g_hostStatusCheckedAtMs = 0;
    }
    return true;
}

} // namespace lua_bridge
