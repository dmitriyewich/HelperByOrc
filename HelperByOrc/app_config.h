#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "json_utils.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

struct ConfigProfile {
    std::string id{};
    std::string name{};
    std::filesystem::path configPath{};
    bool active = false;
};

struct GlobalLuaVariablesConfig {
    bool enabled = true;
    std::unordered_map<std::string, bool> providers{};
};

class AppConfig {
public:
    using Mutation = std::function<void(jsonutil::JsonObject&)>;
    using SectionMutation = std::function<void(jsonutil::JsonObject&)>;

    static AppConfig& Instance();

    void OnProcessAttach(HMODULE module);
    void Shutdown();

    void QueueMutation(Mutation mutation, std::string source = {});
    void QueueSectionReplace(std::string sectionName, jsonutil::JsonValue value, std::string source = {});
    void QueueSectionMutation(std::string sectionName, SectionMutation mutation, std::string source = {});

    jsonutil::JsonValue ReadSection(std::string_view sectionName);
    jsonutil::JsonObject ReadSectionObject(std::string_view sectionName);

    const std::filesystem::path& ConfigPath() const;
    std::filesystem::path ActiveProfileDirectory() const;
    std::filesystem::path ProfilesRoot() const;
    std::filesystem::path ProfilesRegistryPath() const;
    std::filesystem::path LuaVariablesRoot() const;
    GlobalLuaVariablesConfig LuaVariablesConfig() const;
    bool SetLuaProviderEnabled(std::string_view providerId, bool enabled, std::string* error = nullptr);
    std::string ActiveProfileId() const;
    std::vector<ConfigProfile> Profiles() const;
    bool SwitchProfile(std::string_view profileId, std::string* error = nullptr);
    bool CreateProfile(std::string_view name, bool copyCurrentConfig, bool activate, std::string* error = nullptr);
    bool DuplicateProfile(std::string_view sourceProfileId, std::string_view name, bool activate, std::string* error = nullptr);
    bool RenameProfile(std::string_view profileId, std::string_view name, std::string* error = nullptr);
    bool DeleteProfile(std::string_view profileId, std::string* error = nullptr);
    void ProcessPendingWrites();
    void FlushPendingWrites();

private:
    struct PendingMutation {
        enum class Kind {
            Opaque,
            SectionReplace,
            SectionMutation,
        };

        Kind kind = Kind::Opaque;
        Mutation mutation{};
        SectionMutation sectionMutation{};
        std::string sectionName{};
        jsonutil::JsonValue sectionValue{};
        std::string source{};
    };

    struct SnapshotRequest {
        std::filesystem::path targetPath{};
        std::uint64_t revision = 0;
        std::string sources{};
        std::uint64_t mutations = 0;
        std::uint64_t changed = 0;
        std::uint64_t noop = 0;
        double requestedAtMs = 0.0;
        double debounceMs = 0.0;
        bool force = false;
    };

    void LoadProfilesLocked(HMODULE module);
    bool SaveProfilesRegistryLocked() const;
    void RefreshProfileStateLocked();
    void EnsureLoadedLocked();
    void ResetSnapshotTrackingLocked();
    bool ProcessPendingWritesOnce(bool forceSnapshot);
    bool RequestSnapshotWrite(SnapshotRequest request);
    void StartSnapshotWriter();
    void StopSnapshotWriter();
    void FlushSnapshotWriter();
    void SnapshotWriterLoop();

    std::filesystem::path configPath_{};
    std::filesystem::path profilesRoot_{};
    std::filesystem::path profilesRegistryPath_{};
    std::filesystem::path luaVariablesRoot_{};
    std::string activeProfileId_{};
    std::vector<ConfigProfile> profiles_{};
    jsonutil::JsonObject profilesRegistryRoot_{};
    bool luaVariablesEnabled_ = true;
    std::unordered_map<std::string, bool> luaProviderSettings_{};
    mutable std::mutex mutex_{};
    bool loaded_ = false;
    jsonutil::JsonObject root_{};
    std::string lastSerializedSnapshot_{};
    std::deque<PendingMutation> pendingMutations_{};
    std::uint64_t rootRevision_ = 0;
    std::uint64_t persistedRevision_ = 0;
    std::uint64_t snapshotRequestedRevision_ = 0;
    bool snapshotRetryPending_ = false;
    double lastChangedMutationMs_ = 0.0;
    double nextSnapshotRetryMs_ = 0.0;
    std::string pendingSnapshotSources_{};
    std::uint64_t pendingSnapshotMutations_ = 0;
    std::uint64_t pendingSnapshotChanged_ = 0;
    std::uint64_t pendingSnapshotNoop_ = 0;

    std::mutex writerMutex_{};
    std::condition_variable writerCv_{};
    std::condition_variable writerIdleCv_{};
    std::thread writerThread_{};
    bool writerStop_ = false;
    bool writerBusy_ = false;
    bool writerHasPending_ = false;
    SnapshotRequest writerPendingRequest_{};
};
