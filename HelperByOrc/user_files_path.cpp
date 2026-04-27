#include "user_files_path.h"

#include "debug_log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ShlObj.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace helper_paths {
namespace {

namespace fs = std::filesystem;

constexpr std::uintptr_t kGtaSaGetUserFilesAddress = 0x744FB0;
constexpr wchar_t kDefaultGtaUserFilesRelativePath[] = L"GTA San Andreas User Files";
constexpr wchar_t kHelperDirectoryName[] = L"HelperByOrc";

using GetUserFilesDirFn = char*(__cdecl*)();

bool IsReadableExecutableAddress(std::uintptr_t address) {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) != sizeof(info)) {
        return false;
    }

    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }

    const DWORD executableFlags =
        PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (info.Protect & executableFlags) != 0;
}

char* CallGtaSaGetUserFilesDir() {
    char* result = nullptr;
    __try {
        result = reinterpret_cast<GetUserFilesDirFn>(kGtaSaGetUserFilesAddress)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = nullptr;
    }
    return result;
}

std::wstring MultiByteToWide(const char* text) {
    if (!text || !*text) {
        return {};
    }

    const int required = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (required <= 1) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(required - 1), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), required) <= 0) {
        return {};
    }
    return result;
}

std::optional<fs::path> ResolvePortableGtaUserFilesDirectory() {
    if (!GetModuleHandleA("portablegta.asi") && !GetModuleHandleA("PortableGTA.asi")) {
        return std::nullopt;
    }

    if (!IsReadableExecutableAddress(kGtaSaGetUserFilesAddress)) {
        debuglog::WriteError("[paths] portablegta detected but GTA userfiles getter is not executable");
        return std::nullopt;
    }

    const char* rawPath = CallGtaSaGetUserFilesDir();
    if (!rawPath || !*rawPath) {
        debuglog::WriteError("[paths] portablegta userfiles getter returned an empty path");
        return std::nullopt;
    }

    const std::wstring widePath = MultiByteToWide(rawPath);
    if (widePath.empty()) {
        debuglog::WriteError("[paths] portablegta userfiles path conversion failed");
        return std::nullopt;
    }

    fs::path path(widePath);
    if (path.is_relative()) {
        path = fs::absolute(path);
    }

    debuglog::WriteInfo("[paths] GTA userfiles resolved through portablegta: %ls", path.c_str());
    return path;
}

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
    if (const std::optional<fs::path> portablePath = ResolvePortableGtaUserFilesDirectory()) {
        return portablePath;
    }

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
