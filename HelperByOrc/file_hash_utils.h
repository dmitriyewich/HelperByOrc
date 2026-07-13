#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace file_hash {

struct Result {
    std::uint64_t fnv1a64 = 14695981039346656037ull;
    std::string sha256{};
    unsigned long fnvError = 0;
    unsigned long sha256Error = 0;
    bool fnvOk = false;
    bool sha256Ok = false;
};

Result Compute(const std::filesystem::path& path, bool includeSha256);

} // namespace file_hash
