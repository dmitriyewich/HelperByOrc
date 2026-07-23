#pragma once

#include "json_utils.h"

#include <filesystem>
#include <memory>
#include <string>

namespace notepadstorage {

struct OpenResult {
    jsonutil::JsonObject state{};
    bool global = false;
    bool migrated = false;
    unsigned long error = 0;
    std::string message{};
};

class Store {
public:
    Store();
    ~Store();

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    OpenResult Open(
        std::filesystem::path globalRoot,
        std::filesystem::path legacyRoot,
        const jsonutil::JsonObject& legacyState);
    void QueueSave(jsonutil::JsonValue state);
    bool Flush();
    void Stop();

    bool IsGlobal() const;
    unsigned long TakeLastError();
    const std::filesystem::path& RootDirectory() const;
    const std::filesystem::path& StatePath() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace notepadstorage
