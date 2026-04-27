#pragma once

#include <filesystem>
#include <optional>

namespace helper_paths {

std::optional<std::filesystem::path> ResolveGtaUserFilesDirectory();
std::optional<std::filesystem::path> ResolveHelperDataDirectory();

} // namespace helper_paths
