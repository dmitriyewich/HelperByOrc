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
    void ProcessPendingWrites();

private:
    void EnsureLoadedLocked();
    bool ProcessPendingWritesOnce();
    bool WriteSnapshot(const jsonutil::JsonObject& snapshot) const;

    std::filesystem::path configPath_{};
    mutable std::mutex mutex_{};
    bool loaded_ = false;
    jsonutil::JsonObject root_{};
    std::deque<Mutation> pendingMutations_{};
};
