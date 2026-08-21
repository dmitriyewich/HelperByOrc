#include "user_files_path.h"

#include "debug_log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ShlObj.h>

namespace helper_paths {
namespace {

namespace fs = std::filesystem;

constexpr wchar_t kDefaultGtaUserFilesRelativePath[] = L"GTA San Andreas User Files";
constexpr wchar_t kHelperDirectoryName[] = L"HelperByOrc";

std::optional<fs::path> ResolveDocumentsDirectory() {
    PWSTR rawPath = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &rawPath);
    if (FAILED(hr) || !rawPath) {
        if (rawPath) {
            CoTaskMemFree(rawPath);
        }
        debuglog::WriteError("[paths] SHGetKnownFolderPath(FOLDERID_Documents) failed: 0x%08lX", static_cast<unsigned long>(hr));
        return std::nullopt;
    }

    fs::path path(rawPath);
    CoTaskMemFree(rawPath);
    return path;
}

} // namespace

std::optional<std::filesystem::path> ResolveGtaUserFilesDirectory() {
    const std::optional<fs::path> documentsPath = ResolveDocumentsDirectory();
    if (!documentsPath) {
        return std::nullopt;
    }

    fs::path path = *documentsPath / kDefaultGtaUserFilesRelativePath;
    debuglog::WriteInfo("[paths] GTA userfiles resolved through Documents: %ls", path.c_str());
    return path;
}

std::optional<std::filesystem::path> ResolveHelperDataDirectory() {
    const std::optional<fs::path> userFilesPath = ResolveGtaUserFilesDirectory();
    if (!userFilesPath) {
        return std::nullopt;
    }

    return *userFilesPath / kHelperDirectoryName;
}

} // namespace helper_paths
