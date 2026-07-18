#include "app_config.h"

#include "debug_log.h"
#include "user_files_path.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>

namespace {

namespace fs = std::filesystem;

constexpr wchar_t kUnifiedConfigFileName[] = L"HelperByOrc.json";
constexpr wchar_t kProfilesRegistryFileName[] = L"profiles.json";
constexpr wchar_t kNotepadAssetsFolderName[] = L"notepad";
constexpr int kConfigSchemaVersion = 1;
constexpr int kProfilesSchemaVersion = 2;
constexpr char kDefaultProfileId[] = "default";
constexpr char kDefaultProfileName[] = "Default";
// Preserve temporarily absent providers while keeping profiles.json bounded.
constexpr std::size_t kMaxLuaProviderSettings = 512;
constexpr std::size_t kMaxLuaProviderIdBytes = 192;
constexpr std::chrono::milliseconds kSnapshotBuildDebounceWindow{ 500 };
constexpr std::chrono::milliseconds kSnapshotWriterCoalesceWindow{ 200 };
constexpr std::chrono::milliseconds kSnapshotRetryWindow{ 5000 };

double ConfigPerfNowMs() {
    static const double s_invFrequencyMs = [] {
        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
            return 0.0;
        }
        return 1000.0 / static_cast<double>(frequency.QuadPart);
    }();

    if (s_invFrequencyMs <= 0.0) {
        return static_cast<double>(GetTickCount64());
    }

    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) * s_invFrequencyMs;
}

double DurationMs(std::chrono::milliseconds value) {
    return static_cast<double>(value.count());
}

std::string SectionSource(std::string_view sectionName) {
    std::string source = "section:";
    source.append(sectionName);
    return source;
}

std::string SourceOrFallback(std::string source, std::string_view fallback) {
    if (!source.empty()) {
        return source;
    }
    return std::string(fallback);
}

bool SourceListContains(std::string_view sources, std::string_view source) {
    if (source.empty()) {
        return true;
    }

    std::size_t start = 0;
    while (start <= sources.size()) {
        const std::size_t comma = sources.find(',', start);
        const std::size_t end = comma == std::string_view::npos ? sources.size() : comma;
        if (sources.substr(start, end - start) == source) {
            return true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return false;
}

void AppendSource(std::string& sources, std::string_view source) {
    if (source.empty() || SourceListContains(sources, source)) {
        return;
    }

    if (!sources.empty()) {
        sources.push_back(',');
    }
    sources.append(source);
}

fs::path ResolveLegacyConfigPath(HMODULE module) {
    WCHAR path[MAX_PATH]{};
    if (module && GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path))) > 0) {
        return fs::path(path).parent_path() / kUnifiedConfigFileName;
    }

    std::error_code error;
    const fs::path currentPath = fs::current_path(error);
    if (error) {
        debuglog::WriteError("AppConfig: current directory unavailable error=%d", error.value());
        return fs::path(kUnifiedConfigFileName);
    }
    return currentPath / kUnifiedConfigFileName;
}

fs::path ResolveProfilesRoot(HMODULE module) {
    if (const std::optional<fs::path> helperDataPath = helper_paths::ResolveHelperDataDirectory()) {
        return *helperDataPath / L"profiles";
    }

    const fs::path fallback = ResolveLegacyConfigPath(module).parent_path() / L"profiles";
    debuglog::WriteError("[profiles] GTA userfiles path unavailable, falling back to module directory: %ls", fallback.c_str());
    return fallback;
}

fs::path ResolveLuaVariablesRoot(HMODULE module) {
    if (const std::optional<fs::path> helperDataPath = helper_paths::ResolveHelperDataDirectory()) {
        return *helperDataPath / L"vars";
    }

    const fs::path fallback = ResolveLegacyConfigPath(module).parent_path() / L"vars";
    debuglog::WriteError("[tags][code] HelperByOrc data path unavailable, falling back to module directory: %ls", fallback.c_str());
    return fallback;
}

fs::path ProfileConfigPath(const fs::path& profilesRoot, std::string_view profileId) {
    return profilesRoot / fs::path(std::string(profileId)) / kUnifiedConfigFileName;
}

bool IsValidLuaProviderSettingId(std::string_view id) {
    return id.starts_with("lua:")
        && id.size() > 4
        && id.size() <= kMaxLuaProviderIdBytes
        && id.find('\0') == std::string_view::npos;
}

jsonutil::JsonObject MakeDefaultConfigRoot() {
    jsonutil::JsonObject root;
    root["schema_version"] = kConfigSchemaVersion;
    return root;
}

std::optional<jsonutil::JsonObject> ReadJsonObjectFile(
    const fs::path& path,
    std::string* error = nullptr,
    std::string* persistedContent = nullptr) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) {
            *error = "failed to open file";
        }
        return std::nullopt;
    }

    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string parseError;
    const auto rootValue = jsonutil::ParseJson(content, parseError);
    const jsonutil::JsonObject* object = rootValue ? rootValue->TryObject() : nullptr;
    if (!object) {
        if (error) {
            *error = parseError.empty() ? "invalid JSON object" : parseError;
        }
        return std::nullopt;
    }

    if (persistedContent) {
        *persistedContent = content;
    }

    return *object;
}

bool WriteJsonTextFile(const fs::path& path, std::string_view output) {
    std::error_code directoryError;
    fs::create_directories(path.parent_path(), directoryError);
    if (directoryError) {
        debuglog::WriteError(
            "[profiles] failed to create directory for JSON file: %ls error=%d",
            path.parent_path().c_str(),
            directoryError.value());
        return false;
    }

    const fs::path tempPath = path.wstring() + L".tmp";
    std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        debuglog::WriteError("[profiles] failed to open JSON file for write: %ls", tempPath.c_str());
        return false;
    }

    file.write(output.data(), static_cast<std::streamsize>(output.size()));
    file.close();
    if (!file) {
        debuglog::WriteError("[profiles] failed to write JSON file: %ls", tempPath.c_str());
        std::error_code removeError;
        fs::remove(tempPath, removeError);
        return false;
    }

    if (!MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        debuglog::WriteError("[profiles] failed to replace JSON file: %ls error=%lu", path.c_str(), GetLastError());
        std::error_code removeError;
        fs::remove(tempPath, removeError);
        return false;
    }

    return true;
}

bool WriteJsonObjectFile(const fs::path& path, const jsonutil::JsonObject& object) {
    std::string output;
    jsonutil::WriteJson(jsonutil::JsonValue(object), output, 0);
    return WriteJsonTextFile(path, output);
}

std::string TrimProfileName(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    std::string result;
    result.reserve(end - begin);
    for (std::size_t i = begin; i < end; ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (ch >= 0x20) {
            result.push_back(static_cast<char>(ch));
        }
    }
    return result;
}

std::string NormalizeProfileId(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    bool previousDash = false;

    for (const unsigned char ch : value) {
        if (std::isalnum(ch) != 0) {
            result.push_back(static_cast<char>(std::tolower(ch)));
            previousDash = false;
            continue;
        }
        if (ch == '-' || ch == '_' || std::isspace(ch) != 0) {
            if (!result.empty() && !previousDash) {
                result.push_back('-');
                previousDash = true;
            }
        }
    }

    while (!result.empty() && result.back() == '-') {
        result.pop_back();
    }

    if (result.empty()) {
        result = "profile";
    }
    if (result == "." || result == ".." || result == "profiles" || result == "profile-registry") {
        result += "-1";
    }
    return result;
}

bool ProfileIdExists(const std::vector<ConfigProfile>& profiles, std::string_view id) {
    return std::any_of(profiles.begin(), profiles.end(), [&](const ConfigProfile& profile) {
        return profile.id == id;
    });
}

std::string MakeUniqueProfileId(const std::vector<ConfigProfile>& profiles, std::string_view name) {
    const std::string base = NormalizeProfileId(name);
    std::string candidate = base;
    int suffix = 2;
    while (ProfileIdExists(profiles, candidate)) {
        candidate = base + "-" + std::to_string(suffix++);
    }
    return candidate;
}

ConfigProfile* FindProfile(std::vector<ConfigProfile>& profiles, std::string_view id) {
    const auto it = std::find_if(profiles.begin(), profiles.end(), [&](const ConfigProfile& profile) {
        return profile.id == id;
    });
    return it == profiles.end() ? nullptr : &(*it);
}

const ConfigProfile* FindProfile(const std::vector<ConfigProfile>& profiles, std::string_view id) {
    const auto it = std::find_if(profiles.begin(), profiles.end(), [&](const ConfigProfile& profile) {
        return profile.id == id;
    });
    return it == profiles.end() ? nullptr : &(*it);
}

bool IsPathInsideDirectory(const fs::path& directory, const fs::path& candidate) {
    std::error_code rootError;
    const fs::path root = fs::absolute(directory, rootError).lexically_normal();
    std::error_code childError;
    const fs::path child = fs::absolute(candidate, childError).lexically_normal();
    if (rootError || childError) {
        debuglog::WriteError(
            "[profiles] path containment resolution failed root=%ls child=%ls rootError=%d childError=%d",
            directory.c_str(),
            candidate.c_str(),
            rootError.value(),
            childError.value());
        return false;
    }
    auto rootIt = root.begin();
    auto childIt = child.begin();
    for (; rootIt != root.end(); ++rootIt, ++childIt) {
        if (childIt == child.end() || *rootIt != *childIt) {
            return false;
        }
    }
    return true;
}

void RemoveIncompleteProfileDirectory(
    const fs::path& profilesRoot,
    const fs::path& profileDirectory,
    const char* operation) {
    if (!IsPathInsideDirectory(profilesRoot, profileDirectory)) {
        debuglog::WriteError(
            "[profiles] %s rollback blocked outside profiles root: %ls",
            operation,
            profileDirectory.c_str());
        return;
    }

    std::error_code removeError;
    fs::remove_all(profileDirectory, removeError);
    if (removeError) {
        debuglog::WriteError(
            "[profiles] %s rollback cleanup failed: %ls error=%d",
            operation,
            profileDirectory.c_str(),
            removeError.value());
    }
}

bool CopyDirectoryIfExists(const fs::path& source, const fs::path& target, std::string* error = nullptr) {
    std::error_code existsError;
    const bool sourceExists = fs::exists(source, existsError);
    if (existsError) {
        if (error) {
            *error = "source asset directory is unavailable";
        }
        debuglog::WriteError("[profiles] source asset directory stat failed: %ls error=%d", source.c_str(), existsError.value());
        return false;
    }
    if (!sourceExists) {
        return true;
    }
    if (!fs::is_directory(source, existsError) || existsError) {
        if (error) {
            *error = "source asset directory is unavailable";
        }
        debuglog::WriteError("[profiles] source asset directory unavailable: %ls error=%d", source.c_str(), existsError.value());
        return false;
    }

    std::error_code createError;
    fs::create_directories(target, createError);
    if (createError) {
        if (error) {
            *error = "failed to create target asset directory";
        }
        debuglog::WriteError("[profiles] failed to create target asset directory: %ls error=%d", target.c_str(), createError.value());
        return false;
    }

    std::error_code iteratorError;
    for (fs::recursive_directory_iterator it(source, iteratorError), end; it != end && !iteratorError; it.increment(iteratorError)) {
        const fs::directory_entry& entry = *it;
        std::error_code relativeError;
        const fs::path relative = fs::relative(entry.path(), source, relativeError);
        if (relativeError) {
            if (error) {
                *error = "failed to resolve profile asset path";
            }
            debuglog::WriteError(
                "[profiles] failed to resolve profile asset path: source=%ls entry=%ls error=%d",
                source.c_str(),
                entry.path().c_str(),
                relativeError.value());
            return false;
        }
        const fs::path destination = target / relative;
        std::error_code copyError;
        if (entry.is_directory(copyError)) {
            fs::create_directories(destination, copyError);
        } else if (entry.is_regular_file(copyError)) {
            fs::create_directories(destination.parent_path(), copyError);
            if (!copyError) {
                fs::copy_file(entry.path(), destination, fs::copy_options::overwrite_existing, copyError);
            }
        }
        if (copyError) {
            if (error) {
                *error = "failed to copy profile assets";
            }
            debuglog::WriteError(
                "[profiles] failed to copy profile assets: source=%ls target=%ls error=%d",
                entry.path().c_str(),
                destination.c_str(),
                copyError.value());
            return false;
        }
    }
    if (iteratorError) {
        if (error) {
            *error = "failed to enumerate profile assets";
        }
        debuglog::WriteError("[profiles] failed to enumerate profile assets: %ls error=%d", source.c_str(), iteratorError.value());
        return false;
    }
    return true;
}

} // namespace

AppConfig& AppConfig::Instance() {
    static AppConfig instance;
    return instance;
}

void AppConfig::OnProcessAttach(HMODULE module) {
    std::lock_guard lock(mutex_);
    profilesRoot_ = ResolveProfilesRoot(module);
    profilesRegistryPath_ = profilesRoot_ / kProfilesRegistryFileName;
    luaVariablesRoot_ = ResolveLuaVariablesRoot(module);
    LoadProfilesLocked(module);
    loaded_ = false;
    root_.clear();
    lastSerializedSnapshot_.clear();
    ResetSnapshotTrackingLocked();
    pendingMutations_.clear();
    debuglog::WriteInfo(
        "AppConfig::OnProcessAttach profile=%s path=%ls profiles=%ls",
        activeProfileId_.c_str(),
        configPath_.c_str(),
        profilesRoot_.c_str());
}

void AppConfig::Shutdown() {
    debuglog::WriteInfo("AppConfig::Shutdown begin");
    FlushPendingWrites();
    StopSnapshotWriter();
    debuglog::WriteInfo("AppConfig::Shutdown done");
}

void AppConfig::QueueMutation(Mutation mutation, std::string source) {
    if (!mutation) {
        return;
    }

    std::lock_guard lock(mutex_);
    EnsureLoadedLocked();
    PendingMutation pending;
    pending.kind = PendingMutation::Kind::Opaque;
    pending.mutation = std::move(mutation);
    pending.source = SourceOrFallback(std::move(source), "mutation");
    pendingMutations_.push_back(std::move(pending));
}

void AppConfig::QueueSectionReplace(std::string sectionName, jsonutil::JsonValue value, std::string source) {
    std::lock_guard lock(mutex_);
    EnsureLoadedLocked();

    PendingMutation pending;
    pending.kind = PendingMutation::Kind::SectionReplace;
    pending.source = SourceOrFallback(std::move(source), SectionSource(sectionName));
    pending.sectionName = std::move(sectionName);
    pending.sectionValue = std::move(value);
    pendingMutations_.push_back(std::move(pending));
}

void AppConfig::QueueSectionMutation(std::string sectionName, SectionMutation mutation, std::string source) {
    if (!mutation) {
        return;
    }

    std::lock_guard lock(mutex_);
    EnsureLoadedLocked();

    PendingMutation pending;
    pending.kind = PendingMutation::Kind::SectionMutation;
    pending.source = SourceOrFallback(std::move(source), SectionSource(sectionName));
    pending.sectionName = std::move(sectionName);
    pending.sectionMutation = std::move(mutation);
    pendingMutations_.push_back(std::move(pending));
}

jsonutil::JsonValue AppConfig::ReadSection(std::string_view sectionName) {
    std::lock_guard lock(mutex_);
    EnsureLoadedLocked();

    const auto it = root_.find(std::string(sectionName));
    return it == root_.end() ? jsonutil::JsonValue(nullptr) : it->second;
}

jsonutil::JsonObject AppConfig::ReadSectionObject(std::string_view sectionName) {
    const jsonutil::JsonValue section = ReadSection(sectionName);
    if (const jsonutil::JsonObject* object = section.TryObject()) {
        return *object;
    }
    return {};
}

const std::filesystem::path& AppConfig::ConfigPath() const {
    return configPath_;
}

std::filesystem::path AppConfig::ActiveProfileDirectory() const {
    std::lock_guard lock(mutex_);
    return configPath_.parent_path();
}

std::filesystem::path AppConfig::ProfilesRoot() const {
    std::lock_guard lock(mutex_);
    return profilesRoot_;
}

std::filesystem::path AppConfig::ProfilesRegistryPath() const {
    std::lock_guard lock(mutex_);
    return profilesRegistryPath_;
}

std::filesystem::path AppConfig::LuaVariablesRoot() const {
    std::lock_guard lock(mutex_);
    return luaVariablesRoot_;
}

GlobalLuaVariablesConfig AppConfig::LuaVariablesConfig() const {
    std::lock_guard lock(mutex_);
    return GlobalLuaVariablesConfig{ luaVariablesEnabled_, luaProviderSettings_ };
}

bool AppConfig::SetLuaProviderEnabled(std::string_view providerId, bool enabled, std::string* error) {
    std::lock_guard lock(mutex_);
    const std::string id(providerId);
    if (!IsValidLuaProviderSettingId(id)) {
        if (error) {
            *error = "invalid global Lua provider id";
        }
        return false;
    }
    const auto existing = luaProviderSettings_.find(id);
    const bool wasEnabled = existing != luaProviderSettings_.end() && existing->second;
    if (wasEnabled == enabled) {
        return true;
    }
    if (enabled && existing == luaProviderSettings_.end()
        && luaProviderSettings_.size() >= kMaxLuaProviderSettings) {
        if (error) {
            *error = "global Lua provider setting limit exceeded";
        }
        return false;
    }

    const std::optional<bool> previous = existing == luaProviderSettings_.end()
        ? std::nullopt
        : std::optional<bool>(existing->second);
    if (enabled) {
        luaProviderSettings_[id] = true;
    } else {
        luaProviderSettings_.erase(id);
    }
    if (SaveProfilesRegistryLocked()) {
        debuglog::WriteInfo("[profiles][lua] provider=%s enabled=%d", id.c_str(), enabled ? 1 : 0);
        return true;
    }

    if (previous) {
        luaProviderSettings_[id] = *previous;
    } else {
        luaProviderSettings_.erase(id);
    }
    if (error) {
        *error = "failed to save global Lua provider settings";
    }
    debuglog::WriteError("[profiles][lua] provider setting rolled back id=%s", id.c_str());
    return false;
}

std::string AppConfig::ActiveProfileId() const {
    std::lock_guard lock(mutex_);
    return activeProfileId_;
}

std::vector<ConfigProfile> AppConfig::Profiles() const {
    std::lock_guard lock(mutex_);
    return profiles_;
}

bool AppConfig::SwitchProfile(std::string_view profileId, std::string* error) {
    FlushPendingWrites();

    std::lock_guard lock(mutex_);
    const ConfigProfile* profile = FindProfile(profiles_, profileId);
    if (!profile) {
        if (error) {
            *error = "profile not found";
        }
        debuglog::WriteError("[profiles] switch failed: profile not found id=%.*s", static_cast<int>(profileId.size()), profileId.data());
        return false;
    }

    if (activeProfileId_ == profile->id) {
        return true;
    }

    const std::string previousActiveProfileId = activeProfileId_;
    activeProfileId_ = profile->id;
    RefreshProfileStateLocked();
    if (!SaveProfilesRegistryLocked()) {
        activeProfileId_ = previousActiveProfileId;
        RefreshProfileStateLocked();
        if (error) {
            *error = "failed to save profile registry";
        }
        debuglog::WriteError("[profiles] switch rolled back after registry save failure id=%.*s", static_cast<int>(profileId.size()), profileId.data());
        return false;
    }

    loaded_ = false;
    root_.clear();
    lastSerializedSnapshot_.clear();
    ResetSnapshotTrackingLocked();
    pendingMutations_.clear();
    debuglog::WriteInfo("[profiles] switched active profile to id=%s path=%ls", activeProfileId_.c_str(), configPath_.c_str());
    return true;
}

bool AppConfig::CreateProfile(std::string_view name, bool copyCurrentConfig, bool activate, std::string* error) {
    FlushPendingWrites();

    std::lock_guard lock(mutex_);
    const std::string displayName = TrimProfileName(name);
    if (displayName.empty()) {
        if (error) {
            *error = "empty profile name";
        }
        return false;
    }

    const std::string id = MakeUniqueProfileId(profiles_, displayName);
    const fs::path targetPath = ProfileConfigPath(profilesRoot_, id);

    jsonutil::JsonObject snapshot = MakeDefaultConfigRoot();
    if (copyCurrentConfig) {
        EnsureLoadedLocked();
        snapshot = root_;
        snapshot["schema_version"] = kConfigSchemaVersion;
    }

    if (!WriteJsonObjectFile(targetPath, snapshot)) {
        if (error) {
            *error = "failed to write profile config";
        }
        return false;
    }

    const std::string previousActiveProfileId = activeProfileId_;
    profiles_.push_back(ConfigProfile{ id, displayName, targetPath, false });
    if (activate) {
        activeProfileId_ = id;
    }
    RefreshProfileStateLocked();
    if (!SaveProfilesRegistryLocked()) {
        activeProfileId_ = previousActiveProfileId;
        profiles_.erase(std::remove_if(profiles_.begin(), profiles_.end(), [&](const ConfigProfile& profile) {
            return profile.id == id;
        }), profiles_.end());
        RefreshProfileStateLocked();
        RemoveIncompleteProfileDirectory(profilesRoot_, targetPath.parent_path(), "create");
        if (error) {
            *error = "failed to save profile registry";
        }
        debuglog::WriteError("[profiles] create rolled back after registry save failure id=%s", id.c_str());
        return false;
    }
    if (activate) {
        loaded_ = false;
        root_.clear();
        lastSerializedSnapshot_.clear();
        ResetSnapshotTrackingLocked();
        pendingMutations_.clear();
    }

    debuglog::WriteInfo("[profiles] created profile id=%s copy_current=%d activate=%d", id.c_str(), copyCurrentConfig ? 1 : 0, activate ? 1 : 0);
    return true;
}

bool AppConfig::DuplicateProfile(std::string_view sourceProfileId, std::string_view name, bool activate, std::string* error) {
    FlushPendingWrites();

    std::lock_guard lock(mutex_);
    const ConfigProfile* source = FindProfile(profiles_, sourceProfileId);
    if (!source) {
        if (error) {
            *error = "source profile not found";
        }
        return false;
    }
    const std::string sourceIdForLog = source->id;

    const std::string displayName = TrimProfileName(name);
    if (displayName.empty()) {
        if (error) {
            *error = "empty profile name";
        }
        return false;
    }

    std::string readError;
    const std::optional<jsonutil::JsonObject> sourceConfig = ReadJsonObjectFile(source->configPath, &readError);
    if (!sourceConfig) {
        debuglog::WriteError("[profiles] duplicate source config unavailable id=%s error=%s", source->id.c_str(), readError.c_str());
        if (error) {
            *error = "source profile config is unavailable";
        }
        return false;
    }
    jsonutil::JsonObject snapshot = *sourceConfig;
    snapshot["schema_version"] = kConfigSchemaVersion;

    const std::string id = MakeUniqueProfileId(profiles_, displayName);
    const fs::path targetPath = ProfileConfigPath(profilesRoot_, id);
    if (!WriteJsonObjectFile(targetPath, snapshot)) {
        if (error) {
            *error = "failed to write duplicated profile config";
        }
        return false;
    }
    const fs::path sourceAssets = source->configPath.parent_path() / kNotepadAssetsFolderName;
    const fs::path targetAssets = targetPath.parent_path() / kNotepadAssetsFolderName;
    if (!CopyDirectoryIfExists(sourceAssets, targetAssets, error)) {
        RemoveIncompleteProfileDirectory(profilesRoot_, targetPath.parent_path(), "duplicate copy");
        return false;
    }
    const std::string previousActiveProfileId = activeProfileId_;
    profiles_.push_back(ConfigProfile{ id, displayName, targetPath, false });
    if (activate) {
        activeProfileId_ = id;
    }
    RefreshProfileStateLocked();
    if (!SaveProfilesRegistryLocked()) {
        activeProfileId_ = previousActiveProfileId;
        profiles_.erase(std::remove_if(profiles_.begin(), profiles_.end(), [&](const ConfigProfile& profile) {
            return profile.id == id;
        }), profiles_.end());
        RefreshProfileStateLocked();
        RemoveIncompleteProfileDirectory(profilesRoot_, targetPath.parent_path(), "duplicate");
        if (error) {
            *error = "failed to save profile registry";
        }
        debuglog::WriteError("[profiles] duplicate rolled back after registry save failure id=%s", id.c_str());
        return false;
    }
    if (activate) {
        loaded_ = false;
        root_.clear();
        lastSerializedSnapshot_.clear();
        ResetSnapshotTrackingLocked();
        pendingMutations_.clear();
    }

    debuglog::WriteInfo("[profiles] duplicated profile source=%s new=%s activate=%d", sourceIdForLog.c_str(), id.c_str(), activate ? 1 : 0);
    return true;
}

bool AppConfig::RenameProfile(std::string_view profileId, std::string_view name, std::string* error) {
    std::lock_guard lock(mutex_);
    ConfigProfile* profile = FindProfile(profiles_, profileId);
    if (!profile) {
        if (error) {
            *error = "profile not found";
        }
        return false;
    }

    const std::string displayName = TrimProfileName(name);
    if (displayName.empty()) {
        if (error) {
            *error = "empty profile name";
        }
        return false;
    }
    if (profile->name == displayName) {
        return true;
    }

    const std::string previousName = profile->name;
    profile->name = displayName;
    if (!SaveProfilesRegistryLocked()) {
        profile->name = previousName;
        if (error) {
            *error = "failed to save profile registry";
        }
        debuglog::WriteError("[profiles] rename rolled back after registry save failure id=%s", profile->id.c_str());
        return false;
    }

    debuglog::WriteInfo("[profiles] renamed profile id=%s", profile->id.c_str());
    return true;
}

bool AppConfig::DeleteProfile(std::string_view profileId, std::string* error) {
    FlushPendingWrites();

    std::lock_guard lock(mutex_);
    if (profiles_.size() <= 1) {
        if (error) {
            *error = "cannot delete the last profile";
        }
        return false;
    }

    const auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const ConfigProfile& profile) {
        return profile.id == profileId;
    });
    if (it == profiles_.end()) {
        if (error) {
            *error = "profile not found";
        }
        return false;
    }

    const std::string deletedId = it->id;
    const bool deletingActive = activeProfileId_ == deletedId;
    const fs::path profileDirectory = it->configPath.parent_path();
    if (!IsPathInsideDirectory(profilesRoot_, profileDirectory)) {
        if (error) {
            *error = "profile directory is outside profiles root";
        }
        debuglog::WriteError("[profiles] delete blocked, directory is outside profiles root: %ls", profileDirectory.c_str());
        return false;
    }

    std::error_code removeError;
    fs::remove_all(profileDirectory, removeError);
    if (removeError) {
        if (error) {
            *error = "failed to remove profile directory";
        }
        debuglog::WriteError("[profiles] failed to remove profile directory: %ls error=%d", profileDirectory.c_str(), removeError.value());
        return false;
    }

    profiles_.erase(it);
    if (deletingActive || !FindProfile(profiles_, activeProfileId_)) {
        activeProfileId_ = profiles_.front().id;
        loaded_ = false;
        root_.clear();
        lastSerializedSnapshot_.clear();
        ResetSnapshotTrackingLocked();
        pendingMutations_.clear();
    }
    RefreshProfileStateLocked();
    if (!SaveProfilesRegistryLocked()) {
        if (error) {
            *error = "failed to save profile registry";
        }
        return false;
    }

    debuglog::WriteInfo("[profiles] deleted profile id=%s active_now=%s", deletedId.c_str(), activeProfileId_.c_str());
    return true;
}

void AppConfig::ProcessPendingWrites() {
    while (ProcessPendingWritesOnce(false)) {
    }
}

void AppConfig::FlushPendingWrites() {
    while (ProcessPendingWritesOnce(true)) {
    }
    FlushSnapshotWriter();
}

void AppConfig::LoadProfilesLocked(HMODULE module) {
    profiles_.clear();
    activeProfileId_.clear();
    configPath_.clear();
    profilesRegistryRoot_.clear();
    luaVariablesEnabled_ = true;
    luaProviderSettings_.clear();

    std::error_code directoryError;
    fs::create_directories(profilesRoot_, directoryError);
    if (directoryError) {
        debuglog::WriteError("[profiles] failed to create profiles root: %ls error=%d", profilesRoot_.c_str(), directoryError.value());
    }

    std::optional<jsonutil::JsonObject> registry;
    std::error_code registryExistsError;
    const bool registryExists = fs::exists(profilesRegistryPath_, registryExistsError);
    if (registryExistsError) {
        debuglog::WriteError(
            "[profiles] registry stat failed: %ls error=%d",
            profilesRegistryPath_.c_str(),
            registryExistsError.value());
    } else if (registryExists) {
        std::string readError;
        registry = ReadJsonObjectFile(profilesRegistryPath_, &readError);
        if (!registry) {
            debuglog::WriteError("[profiles] invalid registry, rebuilding defaults: %s", readError.c_str());
        }
    }

    if (registry) {
        profilesRegistryRoot_ = *registry;
        activeProfileId_ = NormalizeProfileId(jsonutil::JsonStringOr(&*registry, "active_profile", std::string(kDefaultProfileId)));
        if (const jsonutil::JsonObject* luaVariables = jsonutil::JsonObjectOrNull(&*registry, "lua_variables")) {
            luaVariablesEnabled_ = jsonutil::JsonBoolOr(luaVariables, "enabled", true);
            if (const jsonutil::JsonObject* providers = jsonutil::JsonObjectOrNull(luaVariables, "providers")) {
                for (const auto& [id, value] : *providers) {
                    const bool* enabled = value.TryBool();
                    if (enabled
                        && *enabled
                        && IsValidLuaProviderSettingId(id)
                        && luaProviderSettings_.size() < kMaxLuaProviderSettings) {
                        luaProviderSettings_[id] = true;
                    }
                }
            }
        }
        if (const jsonutil::JsonArray* profiles = jsonutil::JsonArrayOrNull(&*registry, "profiles")) {
            for (const jsonutil::JsonValue& value : *profiles) {
                const jsonutil::JsonObject* object = value.TryObject();
                if (!object) {
                    continue;
                }

                std::string id = NormalizeProfileId(jsonutil::JsonStringOr(object, "id", ""));
                if (id.empty() || ProfileIdExists(profiles_, id)) {
                    continue;
                }

                std::string name = TrimProfileName(jsonutil::JsonStringOr(object, "name", id));
                if (name.empty()) {
                    name = id;
                }
                profiles_.push_back(ConfigProfile{ id, name, ProfileConfigPath(profilesRoot_, id), false });
            }
        }
    }

    if (profiles_.empty()) {
        activeProfileId_ = std::string(kDefaultProfileId);
        profiles_.push_back(ConfigProfile{
            std::string(kDefaultProfileId),
            std::string(kDefaultProfileName),
            ProfileConfigPath(profilesRoot_, kDefaultProfileId),
            false,
        });
    }

    if (!FindProfile(profiles_, activeProfileId_)) {
        activeProfileId_ = profiles_.front().id;
    }

    const fs::path legacyConfigPath = ResolveLegacyConfigPath(module);
    for (const ConfigProfile& profile : profiles_) {
        const fs::path profileConfigPath = ProfileConfigPath(profilesRoot_, profile.id);
        std::error_code profileExistsError;
        const bool profileExists = fs::exists(profileConfigPath, profileExistsError);
        if (profileExistsError) {
            debuglog::WriteError(
                "[profiles] profile config stat failed: %ls error=%d",
                profileConfigPath.c_str(),
                profileExistsError.value());
            continue;
        }
        if (profileExists) {
            continue;
        }

        std::error_code legacyExistsError;
        const bool legacyExists = profile.id == kDefaultProfileId
            && fs::exists(legacyConfigPath, legacyExistsError);
        if (legacyExistsError) {
            debuglog::WriteError(
                "[profiles] legacy config stat failed: %ls error=%d",
                legacyConfigPath.c_str(),
                legacyExistsError.value());
        } else if (legacyExists) {
            std::string importError;
            const std::optional<jsonutil::JsonObject> legacyConfig = ReadJsonObjectFile(legacyConfigPath, &importError);
            if (legacyConfig && WriteJsonObjectFile(profileConfigPath, *legacyConfig)) {
                debuglog::WriteInfo("[profiles] imported legacy config into default profile: %ls", profileConfigPath.c_str());
                continue;
            }
            debuglog::WriteError(
                "[profiles] failed to import legacy config safely: %ls error=%s",
                legacyConfigPath.c_str(),
                importError.empty() ? "write failed" : importError.c_str());
        }

        if (!WriteJsonObjectFile(profileConfigPath, MakeDefaultConfigRoot())) {
            debuglog::WriteError("[profiles] failed to initialize profile config: %ls", profileConfigPath.c_str());
        }
    }

    RefreshProfileStateLocked();
    if (!SaveProfilesRegistryLocked()) {
        debuglog::WriteError("[profiles] failed to save loaded profile registry: %ls", profilesRegistryPath_.c_str());
    }
}

bool AppConfig::SaveProfilesRegistryLocked() const {
    jsonutil::JsonObject registry = profilesRegistryRoot_;
    registry["schema_version"] = kProfilesSchemaVersion;
    registry["active_profile"] = activeProfileId_;

    jsonutil::JsonObject luaVariables;
    luaVariables["enabled"] = luaVariablesEnabled_;
    jsonutil::JsonObject luaProviders;
    for (const auto& [id, enabled] : luaProviderSettings_) {
        luaProviders[id] = enabled;
    }
    luaVariables["providers"] = jsonutil::JsonValue(std::move(luaProviders));
    registry["lua_variables"] = jsonutil::JsonValue(std::move(luaVariables));

    jsonutil::JsonArray profiles;
    for (const ConfigProfile& profile : profiles_) {
        jsonutil::JsonObject object;
        object["id"] = profile.id;
        object["name"] = profile.name;
        profiles.emplace_back(std::move(object));
    }
    registry["profiles"] = jsonutil::JsonValue(std::move(profiles));

    return WriteJsonObjectFile(profilesRegistryPath_, registry);
}

void AppConfig::RefreshProfileStateLocked() {
    for (ConfigProfile& profile : profiles_) {
        profile.configPath = ProfileConfigPath(profilesRoot_, profile.id);
        profile.active = profile.id == activeProfileId_;
        if (profile.active) {
            configPath_ = profile.configPath;
        }
    }

    if (configPath_.empty() && !profiles_.empty()) {
        activeProfileId_ = profiles_.front().id;
        profiles_.front().active = true;
        configPath_ = profiles_.front().configPath;
    }
}

void AppConfig::ResetSnapshotTrackingLocked() {
    rootRevision_ = 0;
    persistedRevision_ = 0;
    snapshotRequestedRevision_ = 0;
    snapshotRetryPending_ = false;
    lastChangedMutationMs_ = 0.0;
    nextSnapshotRetryMs_ = 0.0;
    pendingSnapshotSources_.clear();
    pendingSnapshotMutations_ = 0;
    pendingSnapshotChanged_ = 0;
    pendingSnapshotNoop_ = 0;
}

void AppConfig::EnsureLoadedLocked() {
    if (loaded_) {
        return;
    }

    loaded_ = true;
    root_.clear();
    lastSerializedSnapshot_.clear();
    ResetSnapshotTrackingLocked();
    root_["schema_version"] = kConfigSchemaVersion;

    if (configPath_.empty()) {
        return;
    }

    std::error_code configExistsError;
    const bool configExists = fs::exists(configPath_, configExistsError);
    if (configExistsError) {
        debuglog::WriteError("AppConfig: config stat failed path=%ls error=%d", configPath_.c_str(), configExistsError.value());
        return;
    }
    if (!configExists) {
        return;
    }

    std::string error;
    std::string persistedContent;
    const std::optional<jsonutil::JsonObject> rootObject = ReadJsonObjectFile(configPath_, &error, &persistedContent);
    if (!rootObject) {
        debuglog::WriteError("AppConfig: invalid config, using defaults: %s", error.c_str());
        return;
    }

    root_ = *rootObject;
    root_["schema_version"] = kConfigSchemaVersion;
    lastSerializedSnapshot_ = std::move(persistedContent);
    debuglog::WriteInfo("AppConfig loaded from disk: %ls", configPath_.c_str());
}

bool AppConfig::ProcessPendingWritesOnce(bool forceSnapshot) {
    SnapshotRequest request;
    bool hadPendingMutations = false;
    bool shouldRequestSnapshot = false;

    {
        std::lock_guard lock(mutex_);
        EnsureLoadedLocked();
        hadPendingMutations = !pendingMutations_.empty();
        const double nowMs = ConfigPerfNowMs();

        while (!pendingMutations_.empty()) {
            PendingMutation pending = std::move(pendingMutations_.front());
            pendingMutations_.pop_front();
            ++pendingSnapshotMutations_;
            AppendSource(pendingSnapshotSources_, pending.source);

            bool changed = false;
            switch (pending.kind) {
            case PendingMutation::Kind::Opaque:
                if (pending.mutation) {
                    pending.mutation(root_);
                    changed = true;
                }
                break;
            case PendingMutation::Kind::SectionReplace: {
                const auto existing = root_.find(pending.sectionName);
                if (existing != root_.end() && jsonutil::JsonEquals(existing->second, pending.sectionValue)) {
                    changed = false;
                } else {
                    root_[pending.sectionName] = std::move(pending.sectionValue);
                    changed = true;
                }
                break;
            }
            case PendingMutation::Kind::SectionMutation: {
                jsonutil::JsonObject before;
                const auto existing = root_.find(pending.sectionName);
                const jsonutil::JsonValue beforeValue = existing != root_.end() ? existing->second : jsonutil::JsonValue(nullptr);
                if (existing != root_.end()) {
                    if (const jsonutil::JsonObject* object = existing->second.TryObject()) {
                        before = *object;
                    }
                }

                jsonutil::JsonObject after = before;
                if (pending.sectionMutation) {
                    pending.sectionMutation(after);
                }

                jsonutil::JsonValue afterValue(std::move(after));
                if (jsonutil::JsonEquals(beforeValue, afterValue)) {
                    changed = false;
                } else {
                    root_[pending.sectionName] = std::move(afterValue);
                    changed = true;
                }
                break;
            }
            }

            if (changed) {
                ++rootRevision_;
                ++pendingSnapshotChanged_;
                lastChangedMutationMs_ = nowMs;
                root_["schema_version"] = kConfigSchemaVersion;
            } else {
                ++pendingSnapshotNoop_;
            }
        }

        const bool hasUnpersistedRevision = rootRevision_ > persistedRevision_;
        const bool hasNewRevision = rootRevision_ > snapshotRequestedRevision_;
        const bool retryDue = snapshotRetryPending_ && (forceSnapshot || nowMs >= nextSnapshotRetryMs_);
        const double debounceMs = lastChangedMutationMs_ > 0.0
            ? std::max(0.0, nowMs - lastChangedMutationMs_)
            : DurationMs(kSnapshotBuildDebounceWindow);
        const bool debounceReady = debounceMs >= DurationMs(kSnapshotBuildDebounceWindow);

        if (hasUnpersistedRevision && (hasNewRevision || retryDue) && (forceSnapshot || debounceReady)) {
            request.targetPath = configPath_;
            request.revision = rootRevision_;
            request.sources = pendingSnapshotSources_;
            request.mutations = pendingSnapshotMutations_;
            request.changed = pendingSnapshotChanged_;
            request.noop = pendingSnapshotNoop_;
            request.requestedAtMs = nowMs;
            request.debounceMs = debounceMs;
            request.force = forceSnapshot;

            snapshotRequestedRevision_ = rootRevision_;
            snapshotRetryPending_ = false;
            nextSnapshotRetryMs_ = 0.0;
            pendingSnapshotSources_.clear();
            pendingSnapshotMutations_ = 0;
            pendingSnapshotChanged_ = 0;
            pendingSnapshotNoop_ = 0;
            shouldRequestSnapshot = true;
        } else if (!hasUnpersistedRevision) {
            pendingSnapshotSources_.clear();
            pendingSnapshotMutations_ = 0;
            pendingSnapshotChanged_ = 0;
            pendingSnapshotNoop_ = 0;
        }
    }

    if (shouldRequestSnapshot) {
        RequestSnapshotWrite(std::move(request));
    }
    return hadPendingMutations || shouldRequestSnapshot;
}

bool AppConfig::RequestSnapshotWrite(SnapshotRequest request) {
    if (request.targetPath.empty()) {
        debuglog::WriteError("AppConfig: write skipped, config path is empty");
        return false;
    }

    StartSnapshotWriter();
    const std::string sourceLog = request.sources.empty() ? std::string("unknown") : request.sources;
    const std::uint64_t revision = request.revision;
    const std::uint64_t mutations = request.mutations;
    const std::uint64_t changed = request.changed;
    const std::uint64_t noop = request.noop;
    const double debounceMs = request.debounceMs;
    bool force = request.force;
    {
        std::lock_guard writerLock(writerMutex_);
        if (writerHasPending_ && request.revision < writerPendingRequest_.revision && !request.force) {
            return true;
        }
        if (writerHasPending_ && writerPendingRequest_.force) {
            request.force = true;
            force = true;
        }
        writerPendingRequest_ = std::move(request);
        writerHasPending_ = true;
    }
    writerCv_.notify_one();
    debuglog::WriteInfo(
        "AppConfig snapshot requested source=%s mutations=%llu changed=%llu noop=%llu debounce=%.0fms rev=%llu force=%d",
        sourceLog.c_str(),
        static_cast<unsigned long long>(mutations),
        static_cast<unsigned long long>(changed),
        static_cast<unsigned long long>(noop),
        debounceMs,
        static_cast<unsigned long long>(revision),
        force ? 1 : 0);
    return true;
}

void AppConfig::StartSnapshotWriter() {
    std::lock_guard writerLock(writerMutex_);
    if (writerThread_.joinable()) {
        return;
    }

    writerStop_ = false;
    writerThread_ = std::thread([this]() {
        SnapshotWriterLoop();
    });
}

void AppConfig::StopSnapshotWriter() {
    FlushSnapshotWriter();
    {
        std::lock_guard writerLock(writerMutex_);
        writerStop_ = true;
    }
    writerCv_.notify_one();
    if (writerThread_.joinable()) {
        writerThread_.join();
    }

    std::lock_guard writerLock(writerMutex_);
    writerStop_ = false;
    writerBusy_ = false;
    writerHasPending_ = false;
    writerPendingRequest_ = SnapshotRequest{};
}

void AppConfig::FlushSnapshotWriter() {
    std::unique_lock writerLock(writerMutex_);
    if (!writerThread_.joinable()) {
        return;
    }

    if (writerHasPending_) {
        writerPendingRequest_.force = true;
    }
    writerCv_.notify_one();
    writerIdleCv_.wait(writerLock, [this]() {
        return !writerHasPending_ && !writerBusy_;
    });
}

void AppConfig::SnapshotWriterLoop() {
    for (;;) {
        SnapshotRequest request;
        jsonutil::JsonObject snapshot;
        std::string output;
        double copyMs = 0.0;
        double serializeMs = 0.0;
        std::uint64_t snapshotRevision = 0;

        {
            std::unique_lock writerLock(writerMutex_);
            writerCv_.wait(writerLock, [this]() {
                return writerStop_ || writerHasPending_;
            });
            if (writerStop_ && !writerHasPending_) {
                break;
            }

            if (!writerStop_ && !writerPendingRequest_.force) {
                writerCv_.wait_for(writerLock, kSnapshotWriterCoalesceWindow, [this]() {
                    return writerStop_ || (writerHasPending_ && writerPendingRequest_.force);
                });
            }

            request = std::move(writerPendingRequest_);
            writerPendingRequest_ = SnapshotRequest{};
            writerHasPending_ = false;
            writerBusy_ = true;
        }

        const double copyBeginMs = ConfigPerfNowMs();
        {
            std::lock_guard lock(mutex_);
            snapshot = root_;
            snapshotRevision = rootRevision_;
            if (snapshotRevision > snapshotRequestedRevision_) {
                snapshotRequestedRevision_ = snapshotRevision;
            }
        }
        copyMs = ConfigPerfNowMs() - copyBeginMs;

        const double serializeBeginMs = ConfigPerfNowMs();
        jsonutil::WriteJson(jsonutil::JsonValue(snapshot), output, 0);
        serializeMs = ConfigPerfNowMs() - serializeBeginMs;
        const double queuedAgeMs = request.requestedAtMs > 0.0 ? ConfigPerfNowMs() - request.requestedAtMs : 0.0;
        const char* sourceLog = request.sources.empty() ? "unknown" : request.sources.c_str();

        debuglog::WriteInfo(
            "AppConfig snapshot copied async: %ls copy=%.2fms serialize=%.2fms rev=%llu requestedRev=%llu source=%s queuedAge=%.0fms bytesCandidate=%zu",
            request.targetPath.c_str(),
            copyMs,
            serializeMs,
            static_cast<unsigned long long>(snapshotRevision),
            static_cast<unsigned long long>(request.revision),
            sourceLog,
            queuedAgeMs,
            output.size());

        bool skipUnchanged = false;
        {
            std::lock_guard lock(mutex_);
            skipUnchanged = output == lastSerializedSnapshot_;
        }

        if (skipUnchanged) {
            {
                std::lock_guard lock(mutex_);
                persistedRevision_ = std::max(persistedRevision_, snapshotRevision);
                snapshotRetryPending_ = false;
                nextSnapshotRetryMs_ = 0.0;
            }
            debuglog::WriteInfo(
                "AppConfig snapshot skipped unchanged async: %ls copy=%.2fms serialize=%.2fms rev=%llu source=%s queuedAge=%.0fms bytes=%zu",
                request.targetPath.c_str(),
                copyMs,
                serializeMs,
                static_cast<unsigned long long>(snapshotRevision),
                sourceLog,
                queuedAgeMs,
                output.size());

            {
                std::lock_guard writerLock(writerMutex_);
                writerBusy_ = false;
            }
            writerIdleCv_.notify_all();
            continue;
        }

        const double writeBeginMs = ConfigPerfNowMs();
        const bool saved = WriteJsonTextFile(request.targetPath, output);
        const double writeMs = ConfigPerfNowMs() - writeBeginMs;
        if (saved) {
            {
                std::lock_guard lock(mutex_);
                lastSerializedSnapshot_ = output;
                persistedRevision_ = std::max(persistedRevision_, snapshotRevision);
                snapshotRetryPending_ = false;
                nextSnapshotRetryMs_ = 0.0;
            }
            debuglog::WriteInfo(
                "AppConfig snapshot saved async: %ls copy=%.2fms serialize=%.2fms write=%.2fms rev=%llu source=%s queuedAge=%.0fms bytes=%zu",
                request.targetPath.c_str(),
                copyMs,
                serializeMs,
                writeMs,
                static_cast<unsigned long long>(snapshotRevision),
                sourceLog,
                queuedAgeMs,
                output.size());
        } else {
            {
                std::lock_guard lock(mutex_);
                if (snapshotRevision > persistedRevision_) {
                    snapshotRetryPending_ = true;
                    nextSnapshotRetryMs_ = ConfigPerfNowMs() + DurationMs(kSnapshotRetryWindow);
                }
            }
            debuglog::WriteError(
                "AppConfig: failed to save snapshot async: %ls copy=%.2fms serialize=%.2fms write=%.2fms rev=%llu source=%s queuedAge=%.0fms",
                request.targetPath.c_str(),
                copyMs,
                serializeMs,
                writeMs,
                static_cast<unsigned long long>(snapshotRevision),
                sourceLog,
                queuedAgeMs);
        }

        {
            std::lock_guard writerLock(writerMutex_);
            writerBusy_ = false;
        }
        writerIdleCv_.notify_all();
    }
}
