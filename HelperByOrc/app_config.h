#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "json_utils.h"

#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

struct ConfigProfile {
    std::string id{};
    std::string name{};
    std::filesystem::path configPath{};
    bool active = false;
};

class AppConfig {
public:
    using Mutation = std::function<void(jsonutil::JsonObject&)>;

    static AppConfig& Instance();

    void OnProcessAttach(HMODULE module);
    void Shutdown();

    void QueueMutation(Mutation mutation);
    void QueueSectionReplace(std::string sectionName, jsonutil::JsonValue value);

    jsonutil::JsonValue ReadSection(std::string_view sectionName);
    jsonutil::JsonObject ReadSectionObject(std::string_view sectionName);

    const std::filesystem::path& ConfigPath() const;
    std::filesystem::path ActiveProfileDirectory() const;
    std::filesystem::path ProfilesRoot() const;
    std::filesystem::path ProfilesRegistryPath() const;
    std::string ActiveProfileId() const;
    std::vector<ConfigProfile> Profiles() const;
    bool SwitchProfile(std::string_view profileId, std::string* error = nullptr);
    bool CreateProfile(std::string_view name, bool copyCurrentConfig, bool activate, std::string* error = nullptr);
    bool DuplicateProfile(std::string_view sourceProfileId, std::string_view name, bool activate, std::string* error = nullptr);
    bool RenameProfile(std::string_view profileId, std::string_view name, std::string* error = nullptr);
    bool DeleteProfile(std::string_view profileId, std::string* error = nullptr);
    void ProcessPendingWrites();

private:
    void LoadProfilesLocked(HMODULE module);
    bool SaveProfilesRegistryLocked() const;
    void RefreshProfileStateLocked();
    void EnsureLoadedLocked();
    bool ProcessPendingWritesOnce();
    bool WriteSnapshot(const jsonutil::JsonObject& snapshot) const;

    std::filesystem::path configPath_{};
    std::filesystem::path profilesRoot_{};
    std::filesystem::path profilesRegistryPath_{};
    std::string activeProfileId_{};
    std::vector<ConfigProfile> profiles_{};
    mutable std::mutex mutex_{};
    bool loaded_ = false;
    jsonutil::JsonObject root_{};
    std::deque<Mutation> pendingMutations_{};
};
