#include "app_config.h"

#include "debug_log.h"

#include <fstream>
#include <iterator>
#include <utility>

namespace {

constexpr wchar_t kUnifiedConfigFileName[] = L"HelperByOrc.json";
constexpr int kConfigSchemaVersion = 1;

std::filesystem::path ResolveConfigPath(HMODULE module) {
    WCHAR path[MAX_PATH]{};
    if (module && GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path))) > 0) {
        return std::filesystem::path(path).parent_path() / kUnifiedConfigFileName;
    }

    return std::filesystem::current_path() / kUnifiedConfigFileName;
}

} // namespace

AppConfig& AppConfig::Instance() {
    static AppConfig instance;
    return instance;
}

void AppConfig::OnProcessAttach(HMODULE module) {
    std::lock_guard lock(mutex_);
    configPath_ = ResolveConfigPath(module);
    loaded_ = false;
    root_.clear();
    pendingMutations_.clear();
}

void AppConfig::Shutdown() {
    ProcessPendingWrites();
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

void AppConfig::ProcessPendingWrites() {
    while (ProcessPendingWritesOnce()) {
    }
}

void AppConfig::EnsureLoadedLocked() {
    if (loaded_) {
        return;
    }

    loaded_ = true;
    root_.clear();
    root_["schema_version"] = kConfigSchemaVersion;

    if (configPath_.empty() || !std::filesystem::exists(configPath_)) {
        return;
    }

    std::ifstream file(configPath_, std::ios::binary);
    if (!file) {
        debuglog::Write("AppConfig: failed to open config for read: %ls", configPath_.c_str());
        return;
    }

    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string error;
    const auto rootValue = jsonutil::ParseJson(content, error);
    const jsonutil::JsonObject* object = rootValue ? rootValue->TryObject() : nullptr;
    if (!object) {
        debuglog::Write("AppConfig: invalid config, using defaults: %s", error.c_str());
        return;
    }

    root_ = *object;
    root_["schema_version"] = kConfigSchemaVersion;
}

bool AppConfig::ProcessPendingWritesOnce() {
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
        snapshot = root_;
    }

    WriteSnapshot(snapshot);
    return true;
}

bool AppConfig::WriteSnapshot(const jsonutil::JsonObject& snapshot) const {
    if (configPath_.empty()) {
        return false;
    }

    std::string output;
    jsonutil::WriteJson(jsonutil::JsonValue(snapshot), output, 0);

    const std::filesystem::path tempPath = configPath_.wstring() + L".tmp";
    std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        debuglog::Write("AppConfig: failed to open config for write: %ls", tempPath.c_str());
        return false;
    }

    file.write(output.data(), static_cast<std::streamsize>(output.size()));
    file.close();
    if (!file) {
        debuglog::Write("AppConfig: failed to write config: %ls", tempPath.c_str());
        std::error_code removeError;
        std::filesystem::remove(tempPath, removeError);
        return false;
    }

    if (!MoveFileExW(tempPath.c_str(), configPath_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        debuglog::Write("AppConfig: failed to replace config file: %lu", GetLastError());
        std::error_code removeError;
        std::filesystem::remove(tempPath, removeError);
        return false;
    }

    return true;
}
