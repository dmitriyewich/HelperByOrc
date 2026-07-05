#include "app_config.h"

#include "debug_log.h"
#include "user_files_path.h"

#include <algorithm>
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
constexpr int kProfilesSchemaVersion = 1;
constexpr char kDefaultProfileId[] = "default";
constexpr char kDefaultProfileName[] = "Default";

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

fs::path ResolveLegacyConfigPath(HMODULE module) {
    WCHAR path[MAX_PATH]{};
    if (module && GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path))) > 0) {
        return fs::path(path).parent_path() / kUnifiedConfigFileName;
    }

    return fs::current_path() / kUnifiedConfigFileName;
}

fs::path ResolveProfilesRoot(HMODULE module) {
    if (const std::optional<fs::path> helperDataPath = helper_paths::ResolveHelperDataDirectory()) {
        return *helperDataPath / L"profiles";
    }

    const fs::path fallback = ResolveLegacyConfigPath(module).parent_path() / L"profiles";
    debuglog::WriteError("[profiles] GTA userfiles path unavailable, falling back to module directory: %ls", fallback.c_str());
    return fallback;
}

fs::path ProfileConfigPath(const fs::path& profilesRoot, std::string_view profileId) {
    return profilesRoot / fs::path(std::string(profileId)) / kUnifiedConfigFileName;
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
    const fs::path root = fs::absolute(directory).lexically_normal();
    const fs::path child = fs::absolute(candidate).lexically_normal();
    auto rootIt = root.begin();
    auto childIt = child.begin();
    for (; rootIt != root.end(); ++rootIt, ++childIt) {
        if (childIt == child.end() || *rootIt != *childIt) {
            return false;
        }
    }
    return true;
}

bool CopyDirectoryIfExists(const fs::path& source, const fs::path& target, std::string* error = nullptr) {
    std::error_code existsError;
    if (!fs::exists(source, existsError)) {
        return true;
    }
    if (existsError || !fs::is_directory(source, existsError)) {
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
        const fs::path relative = fs::relative(entry.path(), source);
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
    LoadProfilesLocked(module);
    loaded_ = false;
    root_.clear();
    lastSerializedSnapshot_.clear();
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

void AppConfig::QueueMutation(Mutation mutation) {
    if (!mutation) {
        return;
    }

    std::lock_guard lock(mutex_);
    EnsureLoadedLocked();
    pendingMutations_.push_back(std::move(mutation));
}

void AppConfig::QueueSectionReplace(std::string sectionName, jsonutil::JsonValue value) {
    QueueMutation([sectionName = std::move(sectionName), value = std::move(value)](jsonutil::JsonObject& root) mutable {
        root[sectionName] = std::move(value);
    });
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

    activeProfileId_ = profile->id;
    RefreshProfileStateLocked();
    if (!SaveProfilesRegistryLocked()) {
        if (error) {
            *error = "failed to save profile registry";
        }
        return false;
    }

    loaded_ = false;
    root_.clear();
    lastSerializedSnapshot_.clear();
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

    profiles_.push_back(ConfigProfile{ id, displayName, targetPath, false });
    if (activate) {
        activeProfileId_ = id;
        loaded_ = false;
        root_.clear();
        lastSerializedSnapshot_.clear();
        pendingMutations_.clear();
    }
    RefreshProfileStateLocked();
    if (!SaveProfilesRegistryLocked()) {
        if (error) {
            *error = "failed to save profile registry";
        }
        return false;
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

    jsonutil::JsonObject snapshot = MakeDefaultConfigRoot();
    std::string readError;
    if (const std::optional<jsonutil::JsonObject> sourceConfig = ReadJsonObjectFile(source->configPath, &readError)) {
        snapshot = *sourceConfig;
        snapshot["schema_version"] = kConfigSchemaVersion;
    } else {
        debuglog::WriteError("[profiles] duplicate source config unavailable id=%s error=%s", source->id.c_str(), readError.c_str());
    }

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
        std::error_code removeError;
        fs::remove_all(targetPath.parent_path(), removeError);
        return false;
    }

    profiles_.push_back(ConfigProfile{ id, displayName, targetPath, false });
    if (activate) {
        activeProfileId_ = id;
        loaded_ = false;
        root_.clear();
        lastSerializedSnapshot_.clear();
        pendingMutations_.clear();
    }
    RefreshProfileStateLocked();
    if (!SaveProfilesRegistryLocked()) {
        if (error) {
            *error = "failed to save profile registry";
        }
        return false;
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

    profile->name = displayName;
    if (!SaveProfilesRegistryLocked()) {
        if (error) {
            *error = "failed to save profile registry";
        }
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
    while (ProcessPendingWritesOnce()) {
    }
}

void AppConfig::FlushPendingWrites() {
    ProcessPendingWrites();
    FlushSnapshotWriter();
}

void AppConfig::LoadProfilesLocked(HMODULE module) {
    profiles_.clear();
    activeProfileId_.clear();
    configPath_.clear();

    std::error_code directoryError;
    fs::create_directories(profilesRoot_, directoryError);
    if (directoryError) {
        debuglog::WriteError("[profiles] failed to create profiles root: %ls error=%d", profilesRoot_.c_str(), directoryError.value());
    }

    std::optional<jsonutil::JsonObject> registry;
    if (fs::exists(profilesRegistryPath_)) {
        std::string readError;
        registry = ReadJsonObjectFile(profilesRegistryPath_, &readError);
        if (!registry) {
            debuglog::WriteError("[profiles] invalid registry, rebuilding defaults: %s", readError.c_str());
        }
    }

    if (registry) {
        activeProfileId_ = NormalizeProfileId(jsonutil::JsonStringOr(&*registry, "active_profile", std::string(kDefaultProfileId)));
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
        if (fs::exists(profileConfigPath)) {
            continue;
        }

        if (profile.id == kDefaultProfileId && fs::exists(legacyConfigPath)) {
            std::error_code copyError;
            fs::create_directories(profileConfigPath.parent_path(), copyError);
            if (!copyError) {
                fs::copy_file(legacyConfigPath, profileConfigPath, fs::copy_options::overwrite_existing, copyError);
            }
            if (!copyError) {
                debuglog::WriteInfo("[profiles] imported legacy config into default profile: %ls", profileConfigPath.c_str());
                continue;
            }
            debuglog::WriteError("[profiles] failed to import legacy config: %ls error=%d", legacyConfigPath.c_str(), copyError.value());
        }

        WriteJsonObjectFile(profileConfigPath, MakeDefaultConfigRoot());
    }

    RefreshProfileStateLocked();
    SaveProfilesRegistryLocked();
}

bool AppConfig::SaveProfilesRegistryLocked() const {
    jsonutil::JsonObject registry;
    registry["schema_version"] = kProfilesSchemaVersion;
    registry["active_profile"] = activeProfileId_;

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

void AppConfig::EnsureLoadedLocked() {
    if (loaded_) {
        return;
    }

    loaded_ = true;
    root_.clear();
    lastSerializedSnapshot_.clear();
    root_["schema_version"] = kConfigSchemaVersion;

    if (configPath_.empty() || !fs::exists(configPath_)) {
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

bool AppConfig::ProcessPendingWritesOnce() {
    fs::path targetPath;
    jsonutil::JsonObject snapshot;

    {
        std::lock_guard lock(mutex_);
        EnsureLoadedLocked();
        if (pendingMutations_.empty()) {
            return false;
        }

        while (!pendingMutations_.empty()) {
            Mutation mutation = std::move(pendingMutations_.front());
            pendingMutations_.pop_front();
            if (mutation) {
                mutation(root_);
            }
        }

        root_["schema_version"] = kConfigSchemaVersion;
        targetPath = configPath_;
        snapshot = root_;
    }

    WriteSnapshot(std::move(targetPath), std::move(snapshot));
    return true;
}

bool AppConfig::WriteSnapshot(fs::path targetPath, jsonutil::JsonObject snapshot) {
    if (targetPath.empty()) {
        debuglog::WriteError("AppConfig: write skipped, config path is empty");
        return false;
    }

    StartSnapshotWriter();
    {
        std::lock_guard writerLock(writerMutex_);
        writerPendingPath_ = targetPath;
        writerPendingSnapshot_ = std::move(snapshot);
        writerHasPending_ = true;
    }
    writerCv_.notify_one();
    debuglog::WriteInfo("AppConfig snapshot queued for async serialize: %ls", targetPath.c_str());
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
    writerPendingPath_.clear();
    writerPendingSnapshot_.clear();
}

void AppConfig::FlushSnapshotWriter() {
    std::unique_lock writerLock(writerMutex_);
    if (!writerThread_.joinable()) {
        return;
    }

    writerCv_.notify_one();
    writerIdleCv_.wait(writerLock, [this]() {
        return !writerHasPending_ && !writerBusy_;
    });
}

void AppConfig::SnapshotWriterLoop() {
    for (;;) {
        fs::path targetPath;
        jsonutil::JsonObject snapshot;
        std::string output;
        double serializeMs = 0.0;

        {
            std::unique_lock writerLock(writerMutex_);
            writerCv_.wait(writerLock, [this]() {
                return writerStop_ || writerHasPending_;
            });
            if (writerStop_ && !writerHasPending_) {
                break;
            }

            targetPath = std::move(writerPendingPath_);
            snapshot = std::move(writerPendingSnapshot_);
            writerPendingPath_.clear();
            writerPendingSnapshot_.clear();
            writerHasPending_ = false;
            writerBusy_ = true;
        }

        const double serializeBeginMs = ConfigPerfNowMs();
        jsonutil::WriteJson(jsonutil::JsonValue(snapshot), output, 0);
        serializeMs = ConfigPerfNowMs() - serializeBeginMs;

        bool skipUnchanged = false;
        {
            std::lock_guard lock(mutex_);
            skipUnchanged = output == lastSerializedSnapshot_;
        }

        if (skipUnchanged) {
            debuglog::WriteInfo(
                "AppConfig snapshot skipped unchanged async: %ls serialize=%.2fms bytes=%zu",
                targetPath.c_str(),
                serializeMs,
                output.size());

            {
                std::lock_guard writerLock(writerMutex_);
                writerBusy_ = false;
            }
            writerIdleCv_.notify_all();
            continue;
        }

        const double writeBeginMs = ConfigPerfNowMs();
        const bool saved = WriteJsonTextFile(targetPath, output);
        const double writeMs = ConfigPerfNowMs() - writeBeginMs;
        if (saved) {
            {
                std::lock_guard lock(mutex_);
                lastSerializedSnapshot_ = output;
            }
            debuglog::WriteInfo(
                "AppConfig snapshot saved async: %ls serialize=%.2fms write=%.2fms bytes=%zu",
                targetPath.c_str(),
                serializeMs,
                writeMs,
                output.size());
        } else {
            debuglog::WriteError("AppConfig: failed to save snapshot async: %ls", targetPath.c_str());
        }

        {
            std::lock_guard writerLock(writerMutex_);
            writerBusy_ = false;
        }
        writerIdleCv_.notify_all();
    }
}
