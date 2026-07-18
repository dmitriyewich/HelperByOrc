#pragma once

#include <filesystem>
#include <string>

namespace lua_bridge {

enum class Backend {
    Waiting,
    MoonLoader,
    Standalone,
    Faulted,
};

enum class HostState {
    Unavailable,
    Missing,
    Current,
    Outdated,
};

struct Status {
    Backend backend = Backend::Waiting;
    HostState host = HostState::Unavailable;
    bool moonLoaderAvailable = false;
    std::filesystem::path variablesRoot{};
    std::filesystem::path hostPath{};
    std::string detail{};
};

void Tick();
void Shutdown();
Status CurrentStatus();
bool InstallOrUpdateMoonLoaderHost(std::string* error = nullptr);
const char* BackendName(Backend backend);

} // namespace lua_bridge
