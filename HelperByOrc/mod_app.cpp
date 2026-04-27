#include "mod_app.h"

#include "app_config.h"
#include "debug_log.h"
#include "feature_flags.h"
#include "minhook_utils.h"
#include "resource.h"
#include "ui_settings.h"

#include <GameVersion.h>
#include <d3dx9tex.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <tlhelp32.h>
#include <unordered_set>
#include <vector>

namespace {

constexpr float kSidebarExpandedWidth = 148.0f;
constexpr float kSidebarCollapsedWidth = 50.0f;
constexpr float kLogoExpandedSize = 128.0f;
constexpr float kLogoCollapsedSize = 50.0f;
constexpr float kWindowMargin = 12.0f;
constexpr int kSampCursorModeNone = 0;
constexpr int kSampCursorModeLockCam = 3;
constexpr uint64_t kCursorReassertIntervalMs = 200;
constexpr uint64_t kCursorReassertTraceIntervalMs = 2500;
constexpr uint64_t kCursorTraceIntervalMs = 700;
constexpr uint64_t kCursorUnavailableTraceIntervalMs = 1500;
constexpr uint64_t kUiScaleTraceIntervalMs = 2000;
constexpr std::string_view kShellSectionName = "shell";
constexpr ImGuiChildFlags kBorderedChildFlags = ImGuiChildFlags_Borders;
constexpr ImGuiChildFlags kPlainChildFlags = ImGuiChildFlags_None;
constexpr char kIconHouse[] = "\xEF\xA0\x8C";
constexpr char kIconKeyboard[] = "\xEF\x84\x9C";
constexpr char kIconNewspaper[] = "\xEF\x87\xAA";
constexpr char kIconBook[] = "\xEF\x80\xAD";
constexpr char kIconCubes[] = "\xEF\x86\xB3";
constexpr char kIconGear[] = "\xEF\x80\x93";

namespace fs = std::filesystem;

struct TabDefinition {
    MainTab tab;
    UiText label;
    UiText compactLabel;
    const char* icon;
};

const std::array<TabDefinition, 6> kTabs = {{
    { MainTab::Home, UiText::TabHome, UiText::TabHomeCompact, kIconHouse },
    { MainTab::Binder, UiText::TabBinder, UiText::TabBinderCompact, kIconKeyboard },
    { MainTab::SmiHelper, UiText::TabSmiHelper, UiText::TabSmiHelperCompact, kIconNewspaper },
    { MainTab::Misc, UiText::TabMisc, UiText::TabMiscCompact, kIconCubes },
    { MainTab::Notepad, UiText::TabNotepad, UiText::TabNotepadCompact, kIconBook },
    { MainTab::Settings, UiText::TabSettings, UiText::TabSettingsCompact, kIconGear },
}};

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }

    std::string result(static_cast<std::size_t>(required - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), required, nullptr, nullptr);
    return result;
}

std::string PathToUtf8(const fs::path& path) {
    return WideToUtf8(path.wstring());
}

struct ImGuiStringUserData {
    std::string* value = nullptr;
};

int ImGuiStringResizeCallback(ImGuiInputTextCallbackData* data) {
    auto* userData = static_cast<ImGuiStringUserData*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize && userData && userData->value) {
        userData->value->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = userData->value->data();
    }
    return 0;
}

bool InputTextString(const char* label, std::string& value, ImGuiInputTextFlags flags = 0, std::size_t minBuffer = 128) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value };
    return ImGui::InputText(
        label,
        value.data(),
        value.capacity() + 1,
        flags | ImGuiInputTextFlags_CallbackResize,
        ImGuiStringResizeCallback,
        &userData);
}

std::string TrimAsciiWhitespace(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string NormalizePathForCompare(std::string value) {
    std::replace(value.begin(), value.end(), '/', '\\');
    return LowerAscii(std::move(value));
}

bool StartsWithNoCase(const std::string& value, const std::string& prefix) {
    const std::string lowerValue = LowerAscii(value);
    const std::string lowerPrefix = LowerAscii(prefix);
    return lowerValue.size() >= lowerPrefix.size() && lowerValue.compare(0, lowerPrefix.size(), lowerPrefix) == 0;
}

std::string JoinTags(const std::vector<const char*>& tags) {
    if (tags.empty()) {
        return "-";
    }

    std::string result;
    for (const char* tag : tags) {
        if (!result.empty()) {
            result += ",";
        }
        result += tag;
    }
    return result;
}

std::vector<const char*> ConflictTagsForPath(const std::string& path) {
    const std::string lower = LowerAscii(path);
    std::vector<const char*> tags;

    const auto addIf = [&](const char* token, const char* tag) {
        if (lower.find(token) != std::string::npos) {
            tags.push_back(tag);
        }
    };

    addIf("sampfuncs", "sampfuncs");
    addIf("moonloader", "moonloader");
    addIf("cleo", "cleo");
    addIf("modloader", "modloader");
    addIf("silentpatch", "silentpatch");
    addIf("ginput", "input-hook");
    addIf("widescreen", "widescreen");
    addIf("skygfx", "graphics-hook");
    addIf("reshade", "graphics-hook");
    addIf("enb", "graphics-hook");
    addIf("dxvk", "graphics-hook");
    addIf("d3d9.dll", "d3d9-proxy");
    addIf("dinput8.dll", "input-proxy");
    addIf("ddraw.dll", "graphics-proxy");
    addIf("dxgi.dll", "graphics-proxy");
    addIf("winmm.dll", "loader-proxy");
    addIf("vorbishooked", "loader-proxy");
    addIf("crash", "crashfix");
    addIf("anticrash", "crashfix");
    addIf("samp addon", "samp-addon");
    addIf("sampaddon", "samp-addon");
    if constexpr (feature_flags::kEnableArizonaIntegration) {
        addIf("_chat.asi", "chat-hook");
        addIf("chat.asi", "chat-hook");
    }
    addIf("rivatuner", "overlay");
    addIf("rtss", "overlay");
    addIf("discord", "overlay");
    addIf("gameoverlayrenderer", "overlay");
    addIf("steam", "overlay");
    addIf("overwolf", "overlay");
    addIf("fraps", "overlay");
    addIf("msiafterburner", "overlay");
    addIf("obs", "overlay");

    return tags;
}

std::vector<const char*> AppCompatTagsForData(const std::string& data) {
    const std::string lower = LowerAscii(data);
    std::vector<const char*> tags;

    const auto addIf = [&](const char* token, const char* tag) {
        if (lower.find(token) != std::string::npos) {
            tags.push_back(tag);
        }
    };

    addIf("disabledxmaximizedwindowedmode", "disable-fullscreen-optimizations");
    addIf("dwm8and16bitmitigation", "dwm8-16bit-mitigation");
    addIf("runasadmin", "run-as-admin");
    addIf("win7rtm", "win7-compat");
    addIf("win8rtm", "win8-compat");
    addIf("winxpsp", "xp-compat");
    addIf("vista", "vista-compat");
    addIf("highdpiaware", "high-dpi-aware");
    addIf("dpiunaware", "dpi-unaware");
    addIf("disablethemes", "disable-themes");
    addIf("disabledwm", "disable-dwm");
    addIf("ignorefreelibrary", "ignore-free-library");
    addIf("256color", "256-color");
    addIf("640x480", "640x480");
    if (lower.find('$') != std::string::npos) {
        tags.push_back("shim-db");
    }

    return tags;
}

const char* RegistryTypeName(DWORD type) {
    switch (type) {
    case REG_NONE:
        return "REG_NONE";
    case REG_SZ:
        return "REG_SZ";
    case REG_EXPAND_SZ:
        return "REG_EXPAND_SZ";
    case REG_BINARY:
        return "REG_BINARY";
    case REG_DWORD:
        return "REG_DWORD";
    case REG_MULTI_SZ:
        return "REG_MULTI_SZ";
    case REG_QWORD:
        return "REG_QWORD";
    default:
        return "REG_OTHER";
    }
}

std::string GetModulePathUtf8(HMODULE module) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(module, path, MAX_PATH)) {
        return {};
    }
    return WideToUtf8(path);
}

fs::path GetModulePathFs(HMODULE module) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(module, path, MAX_PATH)) {
        return {};
    }
    return fs::path(path);
}

fs::path GetModuleDirectory(HMODULE module) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(module, path, MAX_PATH)) {
        return {};
    }
    fs::path modulePath(path);
    return modulePath.parent_path();
}

std::string FileTimeToText(const FILETIME& fileTime) {
    SYSTEMTIME utc{};
    SYSTEMTIME local{};
    if (!FileTimeToSystemTime(&fileTime, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
        return "unknown";
    }

    char buffer[64]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%04u-%02u-%02u %02u:%02u:%02u",
        local.wYear,
        local.wMonth,
        local.wDay,
        local.wHour,
        local.wMinute,
        local.wSecond);
    return buffer;
}

std::uint64_t Fnva64File(const fs::path& path, bool& ok) {
    ok = false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }

    std::uint64_t hash = 14695981039346656037ull;
    std::array<std::uint8_t, 64 * 1024> buffer{};
    DWORD bytesRead = 0;
    while (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0) {
        for (DWORD i = 0; i < bytesRead; ++i) {
            hash ^= buffer[i];
            hash *= 1099511628211ull;
        }
    }

    ok = GetLastError() == ERROR_HANDLE_EOF || bytesRead == 0;
    CloseHandle(file);
    return hash;
}

void LogPeFileDetails(const fs::path& path, const char* label) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        debuglog::WriteError("[diag][file] %s PE open failed gle=%lu path=\"%s\"",
            label,
            static_cast<unsigned long>(GetLastError()),
            PathToUtf8(path).c_str());
        return;
    }

    IMAGE_DOS_HEADER dos{};
    DWORD bytesRead = 0;
    bool ok = ReadFile(file, &dos, sizeof(dos), &bytesRead, nullptr) && bytesRead == sizeof(dos)
        && dos.e_magic == IMAGE_DOS_SIGNATURE;
    IMAGE_NT_HEADERS32 nt{};
    if (ok) {
        SetFilePointer(file, dos.e_lfanew, nullptr, FILE_BEGIN);
        ok = ReadFile(file, &nt, sizeof(nt), &bytesRead, nullptr) && bytesRead == sizeof(nt)
            && nt.Signature == IMAGE_NT_SIGNATURE;
    }

    if (ok) {
        debuglog::WriteInfo(
            "[diag][file] %s PE entry=0x%08X imageBase=0x%08X sizeOfImage=0x%X timestamp=0x%08X checksum=0x%08X sections=%u path=\"%s\"",
            label,
            static_cast<unsigned>(nt.OptionalHeader.AddressOfEntryPoint),
            static_cast<unsigned>(nt.OptionalHeader.ImageBase),
            static_cast<unsigned>(nt.OptionalHeader.SizeOfImage),
            static_cast<unsigned>(nt.FileHeader.TimeDateStamp),
            static_cast<unsigned>(nt.OptionalHeader.CheckSum),
            static_cast<unsigned>(nt.FileHeader.NumberOfSections),
            PathToUtf8(path).c_str());
    } else {
        debuglog::WriteError("[diag][file] %s PE parse failed path=\"%s\"", label, PathToUtf8(path).c_str());
    }

    CloseHandle(file);
}

void LogFileFingerprint(const fs::path& path, const char* label) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        debuglog::WriteInfo(
            "[diag][file] %s missing gle=%lu path=\"%s\"",
            label,
            static_cast<unsigned long>(GetLastError()),
            PathToUtf8(path).c_str());
        return;
    }

    const std::uint64_t size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32u) | data.nFileSizeLow;
    bool hashOk = false;
    const std::uint64_t hash = Fnva64File(path, hashOk);
    const std::string pathText = PathToUtf8(path);
    const std::string tags = JoinTags(ConflictTagsForPath(pathText));
    debuglog::WriteInfo(
        "[diag][file] %s size=%llu mtime=\"%s\" fnv64=%016llX hashOk=%d tags=%s path=\"%s\"",
        label,
        static_cast<unsigned long long>(size),
        FileTimeToText(data.ftLastWriteTime).c_str(),
        static_cast<unsigned long long>(hash),
        hashOk ? 1 : 0,
        tags.c_str(),
        pathText.c_str());
}

bool ShouldInventoryFile(const fs::path& path) {
    const std::string lowerName = LowerAscii(PathToUtf8(path.filename()));
    const std::string lowerExt = LowerAscii(PathToUtf8(path.extension()));
    static constexpr const char* kExtensions[] = {
        ".asi", ".dll", ".exe", ".cs", ".cleo", ".lua", ".luac", ".sf", ".asi.disabled", ".dll.disabled",
    };
    for (const char* ext : kExtensions) {
        if (lowerExt == ext || lowerName.ends_with(ext)) {
            return true;
        }
    }
    return !ConflictTagsForPath(lowerName).empty();
}

void LogDirectoryInventory(const fs::path& directory, const char* label, bool recursive, std::size_t limit) {
    std::error_code ec;
    if (!fs::exists(directory, ec)) {
        debuglog::WriteInfo("[diag][inventory] %s missing path=\"%s\"", label, PathToUtf8(directory).c_str());
        return;
    }

    std::size_t matched = 0;
    std::size_t logged = 0;
    const auto logPath = [&](const fs::path& path) {
        if (!ShouldInventoryFile(path)) {
            return;
        }

        ++matched;
        if (logged >= limit) {
            return;
        }

        ++logged;
        LogFileFingerprint(path, label);
    };

    if (recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(directory, fs::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                break;
            }
            if (entry.is_regular_file(ec)) {
                logPath(entry.path());
            }
        }
    } else {
        for (const auto& entry : fs::directory_iterator(directory, fs::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                break;
            }
            if (entry.is_regular_file(ec)) {
                logPath(entry.path());
            }
        }
    }

    debuglog::WriteInfo(
        "[diag][inventory] %s done matched=%llu logged=%llu limit=%llu recursive=%d path=\"%s\"",
        label,
        static_cast<unsigned long long>(matched),
        static_cast<unsigned long long>(logged),
        static_cast<unsigned long long>(limit),
        recursive ? 1 : 0,
        PathToUtf8(directory).c_str());
}

std::string ModuleOrigin(const std::string& path, const std::string& gameDir) {
    wchar_t windowsDir[MAX_PATH]{};
    GetWindowsDirectoryW(windowsDir, MAX_PATH);
    const std::string winDir = WideToUtf8(windowsDir);
    if (!gameDir.empty() && StartsWithNoCase(path, gameDir)) {
        return "game";
    }
    if (!winDir.empty() && StartsWithNoCase(path, winDir)) {
        return "system";
    }
    return "other";
}

void LogLoadedModuleSnapshot(const char* label, bool onlyNew, const fs::path& gameDir) {
    static std::unordered_set<std::string> s_seenModules;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        debuglog::WriteError("[diag][modules] %s snapshot failed gle=%lu", label, static_cast<unsigned long>(GetLastError()));
        return;
    }

    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    std::size_t total = 0;
    std::size_t logged = 0;
    std::size_t suspects = 0;
    const std::string gameDirText = PathToUtf8(gameDir);

    if (Module32FirstW(snapshot, &module)) {
        do {
            ++total;
            const std::string path = WideToUtf8(module.szExePath);
            const std::string key = LowerAscii(path);
            const bool inserted = s_seenModules.insert(key).second;
            if (onlyNew && !inserted) {
                continue;
            }

            const std::vector<const char*> tags = ConflictTagsForPath(path);
            const std::string origin = ModuleOrigin(path, gameDirText);
            const std::string lowerName = LowerAscii(WideToUtf8(module.szModule));
            const bool coreInteresting = lowerName == "samp.dll" || lowerName == "gta_sa.exe"
                || lowerName == "helperbyorc.asi" || lowerName == "d3d9.dll" || lowerName == "dinput8.dll";
            const bool interesting = coreInteresting || !tags.empty() || origin != "system" || !onlyNew;
            if (!interesting) {
                continue;
            }

            if (!tags.empty()) {
                ++suspects;
            }
            ++logged;
            debuglog::WriteInfo(
                "[diag][module] %s new=%d origin=%s base=0x%08X size=0x%X tags=%s name=\"%s\" path=\"%s\"",
                label,
                inserted ? 1 : 0,
                origin.c_str(),
                static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(module.modBaseAddr)),
                static_cast<unsigned>(module.modBaseSize),
                JoinTags(tags).c_str(),
                WideToUtf8(module.szModule).c_str(),
                path.c_str());
        } while (Module32NextW(snapshot, &module));
    }

    CloseHandle(snapshot);
    debuglog::WriteInfo(
        "[diag][modules] %s done total=%llu logged=%llu suspects=%llu onlyNew=%d",
        label,
        static_cast<unsigned long long>(total),
        static_cast<unsigned long long>(logged),
        static_cast<unsigned long long>(suspects),
        onlyNew ? 1 : 0);
}

void LogKnownProxyFiles(const fs::path& gameDir) {
    static constexpr const wchar_t* kProxyNames[] = {
        L"d3d9.dll",
        L"dinput8.dll",
        L"ddraw.dll",
        L"dxgi.dll",
        L"winmm.dll",
        L"vorbisHooked.dll",
        L"vorbisFile.dll",
        L"bass.dll",
    };

    for (const wchar_t* name : kProxyNames) {
        const fs::path path = gameDir / name;
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            LogFileFingerprint(path, "proxy-candidate");
        }
    }
}

constexpr const wchar_t* kAppCompatLayersKey =
    L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";

void LogAppCompatExactLayer(HKEY root, const char* rootName, const fs::path& currentExe) {
    HKEY key = nullptr;
    const LONG openResult = RegOpenKeyExW(root, kAppCompatLayersKey, 0, KEY_READ, &key);
    if (openResult != ERROR_SUCCESS) {
        debuglog::WriteInfo(
            "[diag][appcompat] root=%s exact-current-exe open=0x%08lX found=0 key='%ls' exe='%s'",
            rootName,
            static_cast<unsigned long>(openResult),
            kAppCompatLayersKey,
            PathToUtf8(currentExe).c_str());
        return;
    }

    DWORD valueType = 0;
    DWORD valueBytes = 0;
    LONG queryResult = RegQueryValueExW(
        key,
        currentExe.c_str(),
        nullptr,
        &valueType,
        nullptr,
        &valueBytes);

    std::string valueText;
    if (queryResult == ERROR_SUCCESS && valueBytes > 0) {
        std::vector<BYTE> buffer(valueBytes + sizeof(wchar_t), 0);
        queryResult = RegQueryValueExW(
            key,
            currentExe.c_str(),
            nullptr,
            &valueType,
            buffer.data(),
            &valueBytes);
        if (queryResult == ERROR_SUCCESS && (valueType == REG_SZ || valueType == REG_EXPAND_SZ)) {
            valueText = WideToUtf8(reinterpret_cast<const wchar_t*>(buffer.data()));
        } else if (queryResult == ERROR_SUCCESS) {
            valueText = "<non-string>";
        }
    }

    RegCloseKey(key);
    debuglog::WriteInfo(
        "[diag][appcompat] root=%s exact-current-exe result=0x%08lX found=%d type=%s(%lu) tags=%s exe='%s' data='%s'",
        rootName,
        static_cast<unsigned long>(queryResult),
        queryResult == ERROR_SUCCESS ? 1 : 0,
        RegistryTypeName(valueType),
        static_cast<unsigned long>(valueType),
        JoinTags(AppCompatTagsForData(valueText)).c_str(),
        PathToUtf8(currentExe).c_str(),
        valueText.c_str());
}

void LogAppCompatLayersForRoot(HKEY root, const char* rootName, const fs::path& gameDir, const fs::path& currentExe) {
    LogAppCompatExactLayer(root, rootName, currentExe);

    HKEY key = nullptr;
    const LONG openResult = RegOpenKeyExW(root, kAppCompatLayersKey, 0, KEY_READ, &key);
    if (openResult != ERROR_SUCCESS) {
        debuglog::WriteInfo(
            "[diag][appcompat] root=%s open=0x%08lX matches=0 key='%ls'",
            rootName,
            static_cast<unsigned long>(openResult),
            kAppCompatLayersKey);
        return;
    }

    const std::string gameDirText = NormalizePathForCompare(PathToUtf8(gameDir));
    const std::string currentExeText = NormalizePathForCompare(PathToUtf8(currentExe));
    std::size_t matches = 0;
    for (DWORD index = 0;; ++index) {
        wchar_t valueName[1024]{};
        wchar_t valueData[2048]{};
        DWORD valueNameChars = static_cast<DWORD>(std::size(valueName));
        DWORD valueDataBytes = sizeof(valueData);
        DWORD valueType = 0;

        const LONG enumResult = RegEnumValueW(
            key,
            index,
            valueName,
            &valueNameChars,
            nullptr,
            &valueType,
            reinterpret_cast<LPBYTE>(valueData),
            &valueDataBytes);
        if (enumResult == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (enumResult != ERROR_SUCCESS) {
            debuglog::WriteError(
                "[diag][appcompat] root=%s enum failed index=%lu result=0x%08lX",
                rootName,
                static_cast<unsigned long>(index),
                static_cast<unsigned long>(enumResult));
            break;
        }

        const std::string valueNameText = WideToUtf8(valueName);
        const std::string valueDataText = valueType == REG_SZ || valueType == REG_EXPAND_SZ ? WideToUtf8(valueData) : "<non-string>";
        const std::string lowerName = NormalizePathForCompare(valueNameText);
        std::vector<const char*> matchReasons;
        if (!currentExeText.empty() && lowerName == currentExeText) {
            matchReasons.push_back("exact-current-exe");
        }
        if (!gameDirText.empty() && StartsWithNoCase(lowerName, gameDirText)) {
            matchReasons.push_back("same-game-dir");
        }
        if (lowerName.find("gta_sa.exe") != std::string::npos) {
            matchReasons.push_back("gta-sa");
        }
        if (lowerName.find("samp.exe") != std::string::npos) {
            matchReasons.push_back("samp");
        }
        if (lowerName.find("main.exe") != std::string::npos) {
            matchReasons.push_back("main-exe");
        }
        if constexpr (feature_flags::kEnableArizonaIntegration) {
            if (lowerName.find("arizona") != std::string::npos) {
                matchReasons.push_back("arizona");
            }
        }
        const bool interesting = !matchReasons.empty();
        if (!interesting) {
            continue;
        }

        ++matches;
        debuglog::WriteInfo(
            "[diag][appcompat] root=%s value='%s' type=%s(%lu) match=%s tags=%s data='%s'",
            rootName,
            valueNameText.c_str(),
            RegistryTypeName(valueType),
            static_cast<unsigned long>(valueType),
            JoinTags(matchReasons).c_str(),
            JoinTags(AppCompatTagsForData(valueDataText)).c_str(),
            valueDataText.c_str());
    }

    RegCloseKey(key);
    debuglog::WriteInfo(
        "[diag][appcompat] root=%s matches=%llu gameDir='%s' currentExe='%s'",
        rootName,
        static_cast<unsigned long long>(matches),
        PathToUtf8(gameDir).c_str(),
        PathToUtf8(currentExe).c_str());
}

void LogAppCompatEnvironment() {
    wchar_t compatLayer[4096]{};
    const DWORD compatLayerChars = GetEnvironmentVariableW(
        L"__COMPAT_LAYER",
        compatLayer,
        static_cast<DWORD>(std::size(compatLayer)));
    if (compatLayerChars == 0) {
        debuglog::WriteInfo(
            "[diag][appcompat] env __COMPAT_LAYER present=0 gle=%lu",
            static_cast<unsigned long>(GetLastError()));
        return;
    }

    const bool truncated = compatLayerChars >= std::size(compatLayer);
    const std::string value = WideToUtf8(compatLayer);
    debuglog::WriteInfo(
        "[diag][appcompat] env __COMPAT_LAYER present=1 truncated=%d tags=%s data='%s'",
        truncated ? 1 : 0,
        JoinTags(AppCompatTagsForData(value)).c_str(),
        value.c_str());
}

void LogAppCompatShimModules() {
    static constexpr const wchar_t* kShimModules[] = {
        L"apphelp.dll",
        L"AcLayers.dll",
        L"AcGenral.dll",
        L"AcSpecfc.dll",
        L"AcXtrnal.dll",
        L"AcDwm.dll",
        L"AcRes.dll",
    };

    for (const wchar_t* moduleName : kShimModules) {
        HMODULE module = GetModuleHandleW(moduleName);
        if (!module) {
            debuglog::WriteInfo("[diag][appcompat] shim module=%ls loaded=0", moduleName);
            continue;
        }

        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(module, path, MAX_PATH);
        debuglog::WriteInfo(
            "[diag][appcompat] shim module=%ls loaded=1 base=0x%08X path='%s'",
            moduleName,
            static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(module)),
            WideToUtf8(path).c_str());
    }
}

void LogAppCompatLayers(const fs::path& gameDir, const fs::path& currentExe) {
    LogAppCompatEnvironment();
    LogAppCompatLayersForRoot(HKEY_CURRENT_USER, "HKCU", gameDir, currentExe);
    LogAppCompatLayersForRoot(HKEY_LOCAL_MACHINE, "HKLM", gameDir, currentExe);
    LogAppCompatShimModules();
}

void LogStartupDiagnostics(HMODULE module) {
    const fs::path gameDir = GetModuleDirectory(nullptr);
    const fs::path currentExe = GetModulePathFs(nullptr);
    wchar_t currentDir[MAX_PATH]{};
    GetCurrentDirectoryW(MAX_PATH, currentDir);

    debuglog::WriteInfo(
        "[diag][startup] pid=%lu tick=%llums exe=\"%s\" module=\"%s\" cwd=\"%s\" cmd=\"%s\"",
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long long>(GetTickCount64()),
        GetModulePathUtf8(nullptr).c_str(),
        GetModulePathUtf8(module).c_str(),
        WideToUtf8(currentDir).c_str(),
        GetCommandLineA());

    LogFileFingerprint(gameDir / L"gta_sa.exe", "gta_sa.exe");
    LogPeFileDetails(gameDir / L"gta_sa.exe", "gta_sa.exe");
    LogFileFingerprint(gameDir / L"samp.dll", "samp.dll");
    LogPeFileDetails(gameDir / L"samp.dll", "samp.dll");
    LogFileFingerprint(gameDir / L"samp.exe", "samp.exe");
    LogFileFingerprint(GetModulePathUtf8(module), "HelperByOrc");

    LogKnownProxyFiles(gameDir);
    LogAppCompatLayers(gameDir, currentExe);
    LogDirectoryInventory(gameDir, "root-plugin", false, 200);
    LogDirectoryInventory(gameDir / L"scripts", "scripts", true, 300);
    LogDirectoryInventory(gameDir / L"cleo", "cleo", true, 300);
    LogDirectoryInventory(gameDir / L"moonloader", "moonloader", true, 300);
    LogDirectoryInventory(gameDir / L"modloader", "modloader", true, 500);
    LogDirectoryInventory(gameDir / L"SAMPFUNCS", "SAMPFUNCS", true, 300);
    LogLoadedModuleSnapshot("startup", false, gameDir);
}

std::size_t ToTabIndex(MainTab tab) {
    return static_cast<std::size_t>(tab);
}

const TabDefinition& GetTabDefinition(MainTab tab) {
    return kTabs[ToTabIndex(tab)];
}

const char* GetTabLabel(MainTab tab, bool compact = false) {
    const TabDefinition& definition = GetTabDefinition(tab);
    return UiSettings::Instance().Text(compact ? definition.compactLabel : definition.label);
}

const char* GetTabIcon(MainTab tab) {
    return GetTabDefinition(tab).icon;
}

std::string FormatTabLabelWithIcon(MainTab tab) {
    return std::string(GetTabIcon(tab)) + " " + GetTabLabel(tab);
}

debuglog::Level ToDebugLogLevel(UiLogLevel level) {
    switch (level) {
    case UiLogLevel::Off:
        return debuglog::Level::Off;
    case UiLogLevel::Error:
        return debuglog::Level::Error;
    case UiLogLevel::Info:
    default:
        return debuglog::Level::Info;
    }
}

const char* ToUiLogLevelName(UiLogLevel level) {
    switch (level) {
    case UiLogLevel::Off:
        return "off";
    case UiLogLevel::Error:
        return "error";
    case UiLogLevel::Info:
    default:
        return "info";
    }
}

float Scale(float value) {
    return UiSettings::Instance().Scale(value);
}

ImVec2 ScaleVec(float x, float y) {
    return UiSettings::Instance().Scale(ImVec2(x, y));
}

bool LoadBinaryResource(HMODULE module, int resourceId, const void** data, DWORD* size) {
    if (!module || !data || !size) {
        return false;
    }

    *data = nullptr;
    *size = 0;

    const HRSRC resource = FindResourceA(module, MAKEINTRESOURCEA(resourceId), RT_RCDATA);
    if (!resource) {
        return false;
    }

    const HGLOBAL loadedResource = LoadResource(module, resource);
    if (!loadedResource) {
        return false;
    }

    const DWORD resourceSize = SizeofResource(module, resource);
    if (resourceSize == 0) {
        return false;
    }

    const void* resourceData = LockResource(loadedResource);
    if (!resourceData) {
        return false;
    }

    *data = resourceData;
    *size = resourceSize;
    return true;
}

void ClampWindowRect(const ImVec2& displaySize, ImVec2& position, ImVec2& size, float scale) {
    const float margin = kWindowMargin * scale;
    const float maxWidth = std::max(360.0f * scale, displaySize.x - margin * 2.0f);
    const float maxHeight = std::max(280.0f * scale, displaySize.y - margin * 2.0f);
    const float minWidth = std::min(840.0f * scale, maxWidth);
    const float minHeight = std::min(560.0f * scale, maxHeight);

    size.x = std::clamp(size.x, minWidth, maxWidth);
    size.y = std::clamp(size.y, minHeight, maxHeight);
    position.x = std::clamp(position.x, margin, displaySize.x - size.x - margin);
    position.y = std::clamp(position.y, margin, displaySize.y - size.y - margin);
}

void DrawLogoZoom(
    IDirect3DTexture9* texture,
    std::uint32_t textureWidth,
    std::uint32_t textureHeight,
    MainTab tab,
    const ImVec2& size,
    float zoom) {
    if (!texture || textureWidth == 0 || textureHeight == 0) {
        return;
    }

    const float cellWidth = static_cast<float>(textureWidth) / 3.0f;
    const float cellHeight = static_cast<float>(textureHeight) / 2.0f;
    const float safeZoom = std::max(0.1f, zoom);
    const int tabIndex = static_cast<int>(ToTabIndex(tab));
    const int column = tabIndex % 3;
    const int row = tabIndex / 3;
    const float centerX = (static_cast<float>(column) + 0.5f) * cellWidth;
    const float centerY = (static_cast<float>(row) + 0.5f) * cellHeight;
    const float zoomWidth = cellWidth / safeZoom;
    const float zoomHeight = cellHeight / safeZoom;
    const float x0 = centerX - zoomWidth * 0.5f;
    const float y0 = centerY - zoomHeight * 0.5f;
    const float x1 = centerX + zoomWidth * 0.5f;
    const float y1 = centerY + zoomHeight * 0.5f;

    const ImVec2 uv0(x0 / static_cast<float>(textureWidth), y0 / static_cast<float>(textureHeight));
    const ImVec2 uv1(x1 / static_cast<float>(textureWidth), y1 / static_cast<float>(textureHeight));
    ImGui::Image(reinterpret_cast<ImTextureID>(texture), size, uv0, uv1);
}

} // namespace

ModApp::ModApp() = default;

ModApp& ModApp::Instance() {
    static ModApp instance;
    return instance;
}

void ModApp::OnProcessAttach(HMODULE module) {
    module_ = module;
    debuglog::Initialize(module);
    debuglog::WriteInfo("ModApp attached");
    LogStartupDiagnostics(module);
    minHookInitialized_ = minhook::Initialize();
    if (!minHookInitialized_) {
        debuglog::WriteError("MinHook initialization failed");
    } else {
        debuglog::WriteInfo("MinHook initialized");
    }
    AppConfig::Instance().OnProcessAttach(module);
    debuglog::WriteInfo("AppConfig attached");
    UiSettings::Instance().Load();
    debuglog::SetLevel(ToDebugLogLevel(UiSettings::Instance().LogLevel()));
    debuglog::WriteInfo("UI settings loaded (log_level=%s)", ToUiLogLevelName(UiSettings::Instance().LogLevel()));
    LoadShellState();

    tags_.SetSampApi(&sampApi_);
    tags_.OnProcessAttach();
    sampApi_.attachModules([this](std::string_view text) { return tags_.ExpandText(text); });
    sampHooks_.SetSampApi(&sampApi_);
    sampHooks_.SetApplyDamageProtectionEnabled(UiSettings::Instance().ApplyDamageProtectionEnabled());
    sampHooks_.SetHotkeyBlockCallback([this]() {
        return overlay_.IsTextInputActive();
    });
    sampHooks_.AddOnSendCommandHandler([this](std::string& text) {
        text = tags_.ExpandOutgoingText(text, "command", text);
        return true;
    });
    sampHooks_.AddOnSendChatHandler([this](std::string& text) {
        text = tags_.ExpandOutgoingText(text, "chat", text);
        return true;
    });
    sampRakHooks_.SetSampApi(&sampApi_);
    sampRakHooks_.AddOnSendCommandHandler([this](std::string& text) {
        if (!SampHooks::IsOutgoingInputTransformActive()) {
            text = tags_.ExpandOutgoingText(text, "command", text);
        }
        return true;
    });
    sampRakHooks_.AddOnSendChatHandler([this](std::string& text) {
        if (!SampHooks::IsOutgoingInputTransformActive()) {
            text = tags_.ExpandOutgoingText(text, "chat", text);
        }
        return true;
    });
    incomingMessageRouter_.SetSampHooks(&sampHooks_);
    incomingMessageRouter_.SetSampRakHooks(&sampRakHooks_);
    binder_.OnProcessAttach(module);
    binder_.SetSampApi(&sampApi_);
    binder_.SetSampHooks(&sampHooks_);
    binder_.SetSampRakHooks(&sampRakHooks_);
    binder_.SetIncomingMessageRouter(&incomingMessageRouter_);
    binder_.SetTagsModule(&tags_);
    tags_.SetBinderModule(&binder_);

    overlay_.SetPrepareFrameCallback([this](IDirect3DDevice9* device) { PrepareUiForImGuiNewFrame(device); });
    overlay_.SetRenderCallback([this](IDirect3DDevice9* device) { RenderUi(device); });
    overlay_.SetUpdateCallback([this]() { Tick(); });
    overlay_.SetWindowMessageCallback([this](UINT message, WPARAM wparam, LPARAM lparam) {
        return binder_.OnWindowMessage(message, wparam, lparam);
    });
    overlay_.SetAuxiliaryUiVisibleCallback([this]() { return binder_.WantsOverlayRender(); });
    overlay_.SetAuxiliaryInputCaptureCallback([this]() {
        return binder_.WantsQuickMenuCursor() || binder_.WantsInputCapture();
    });
    overlay_.SetInputPipelineGateCallback([this]() { return sampUiPipelineReady_; });
    overlay_.SetInputCaptureChangedCallback([this](bool captured) { HandleOverlayInputCaptureChanged(captured); });
    overlay_.SetMenuToggleHotkeyConflictCallback([this](const std::vector<unsigned int>& keys, std::string& description) {
        return binder_.DescribeMainWindowHotkeyConflict(keys, description);
    });
    debuglog::WriteInfo("Overlay callbacks configured");
    StartDeferredOverlayThread();
    debuglog::WriteInfo("[ui][d3d] overlay attach deferred until SA:MP full-ready gate");
}

void ModApp::Shutdown() {
    debuglog::WriteInfo("[probe] shutdown begin ts=%llums", static_cast<unsigned long long>(GetTickCount64()));
    debuglog::WriteInfo("ModApp shutdown begin");
    StopDeferredOverlayThread();
    ::ClipCursor(nullptr);
    ::ReleaseCapture();
    overlayLastUiHold_ = false;

    sampApi_.Refresh();
    if (sampApi_.sampModule() && sampApi_.isSupportedVersion()) {
        sampApi_.Set_CursorMode(kSampCursorModeNone, false);
    }
    overlayCursorMode_ = kSampCursorModeNone;
    overlayCursorEnabled_ = false;

    overlay_.Shutdown();
    debuglog::WriteInfo("Overlay shutdown done");
    incomingMessageRouter_.Shutdown();
    debuglog::WriteInfo("Incoming router shutdown done");
    binder_.Shutdown();
    debuglog::WriteInfo("Binder shutdown done");
    tags_.Shutdown();
    debuglog::WriteInfo("Tags shutdown done");
    AppConfig::Instance().Shutdown();
    debuglog::WriteInfo("AppConfig shutdown done");
    sampRakHooks_.Shutdown();
    sampHooks_.Shutdown();
    debuglog::WriteInfo("SAMP hooks shutdown done");
    sampApi_.onTerminate();
    debuglog::WriteInfo("SampApi terminated");
    ReleaseUiResources();
    if (minHookInitialized_) {
        minhook::Uninitialize();
        minHookInitialized_ = false;
        debuglog::WriteInfo("MinHook uninitialized");
    }
    debuglog::WriteInfo("ModApp shutdown");
    debuglog::WriteInfo("[probe] shutdown end ts=%llums", static_cast<unsigned long long>(GetTickCount64()));
    debuglog::Shutdown();
}

void ModApp::HandleOverlayInputCaptureChanged(bool captured) {
    (void)captured;
    UpdateOverlayCursorMode();
}

void ModApp::UpdateOverlayCursorMode() {
    const bool wantsUi = overlay_.WantsUiCursor();
    const uint64_t now = GetTickCount64();
    if (!sampUiPipelineReady_) {
        if (overlayLastUiHold_) {
            ::ReleaseCapture();
            overlayLastUiHold_ = false;
            debuglog::WriteInfo("[ui] cursor pipeline gated: released capture while SA:MP is not fully initialized");
        }
        if (overlayCursorMode_ != kSampCursorModeNone || overlayCursorEnabled_) {
            overlayCursorMode_ = kSampCursorModeNone;
            overlayCursorEnabled_ = false;
            overlayCursorLastApplyMs_ = now;
        }
        static uint64_t s_lastGateTraceMs = 0;
        if (now - s_lastGateTraceMs >= kCursorUnavailableTraceIntervalMs) {
            s_lastGateTraceMs = now;
            debuglog::WriteInfo("[ui] cursor pipeline gated: waiting for full SA:MP initialization");
        }
        return;
    }

    HWND gameHw = overlay_.GetGameWindow();
    HWND fg = GetForegroundWindow();
    const bool appHasFocus = gameHw && fg && IsWindow(gameHw)
        && (fg == gameHw || IsChild(gameHw, fg) != FALSE);

    bool chatOrDialogActive = false;
    sampApi_.Refresh();
    if (sampApi_.sampModule() && sampApi_.isSupportedVersion()) {
        chatOrDialogActive = sampApi_.is_chat_opened() || sampApi_.isDialogActive();
    }

    const bool rmbHeld = (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const bool shouldHoldUi = appHasFocus && (wantsUi || chatOrDialogActive);

    static bool s_traceWantsUi = false;
    static bool s_traceFocus = false;
    static bool s_traceRmb = false;
    static bool s_traceChatDialog = false;
    static bool s_traceHold = false;
    static uint64_t s_lastCursorTraceMs = 0;
    const bool changedCore = wantsUi != s_traceWantsUi || appHasFocus != s_traceFocus
        || chatOrDialogActive != s_traceChatDialog || shouldHoldUi != s_traceHold;
    const bool changedRmbOnly = !changedCore && (rmbHeld != s_traceRmb);
    const bool allowRmbSpamSafeTrace = changedRmbOnly && (now - s_lastCursorTraceMs >= kCursorTraceIntervalMs);
    if (changedCore || allowRmbSpamSafeTrace) {
        s_traceWantsUi = wantsUi;
        s_traceFocus = appHasFocus;
        s_traceRmb = rmbHeld;
        s_traceChatDialog = chatOrDialogActive;
        s_traceHold = shouldHoldUi;
        s_lastCursorTraceMs = now;
        debuglog::WriteInfo(
            "[ui] cursor wantsUi=%d chatOpen=%d dialogOpen=%d chatOrDialog=%d fg=%d rmb=%d shouldHold=%d gameHw=%p fgHw=%p sampMode=%d sampEn=%d",
            wantsUi ? 1 : 0,
            (chatOrDialogActive && sampApi_.is_chat_opened()) ? 1 : 0,
            (chatOrDialogActive && sampApi_.isDialogActive()) ? 1 : 0,
            chatOrDialogActive ? 1 : 0,
            appHasFocus ? 1 : 0,
            rmbHeld ? 1 : 0,
            shouldHoldUi ? 1 : 0,
            gameHw,
            fg,
            overlayCursorMode_,
            overlayCursorEnabled_ ? 1 : 0);
    }

    if (overlayLastUiHold_ && !shouldHoldUi) {
        ::ReleaseCapture();
        debuglog::WriteInfo("[ui] ReleaseCapture due to UI-hold end");
    }
    overlayLastUiHold_ = shouldHoldUi;

    const int desiredMode = shouldHoldUi ? kSampCursorModeLockCam : kSampCursorModeNone;
    const bool desiredEnabled = shouldHoldUi;
    const bool desiredSameAsCache = overlayCursorMode_ == desiredMode && overlayCursorEnabled_ == desiredEnabled;
    const bool shouldReassert = desiredEnabled && (now - overlayCursorLastApplyMs_ >= kCursorReassertIntervalMs);

    if (desiredSameAsCache && !shouldReassert) {
        return;
    }

    if (!sampApi_.sampModule() || !sampApi_.isSupportedVersion()) {
        static uint64_t s_lastCursorUnavailableTraceMs = 0;
        if (wantsUi && now - s_lastCursorUnavailableTraceMs >= kCursorUnavailableTraceIntervalMs) {
            s_lastCursorUnavailableTraceMs = now;
            debuglog::WriteInfo(
                "[ui] cursor apply skipped: sampModule=%d supported=%d",
                sampApi_.sampModule() ? 1 : 0,
                sampApi_.isSupportedVersion() ? 1 : 0);
        }
        return;
    }

    if (!sampApi_.Set_CursorMode(desiredMode, desiredEnabled)) {
        debuglog::WriteError(
            "[ui] Set_CursorMode FAILED want mode=%d en=%d: %s",
            desiredMode,
            desiredEnabled ? 1 : 0,
            sampApi_.lastError().c_str());
        return;
    }

    bool shouldLogApply = true;
    if (shouldReassert && desiredSameAsCache) {
        static uint64_t s_lastReassertTraceMs = 0;
        shouldLogApply = (now - s_lastReassertTraceMs) >= kCursorReassertTraceIntervalMs;
        if (shouldLogApply) {
            s_lastReassertTraceMs = now;
        }
    }
    if (shouldLogApply) {
        debuglog::WriteInfo(
            "[ui] Set_CursorMode ok mode=%d en=%d (was %d / %d reassert=%d)",
            desiredMode,
            desiredEnabled ? 1 : 0,
            overlayCursorMode_,
            overlayCursorEnabled_ ? 1 : 0,
            shouldReassert ? 1 : 0);
    }

    overlayCursorMode_ = desiredMode;
    overlayCursorEnabled_ = desiredEnabled;
    overlayCursorLastApplyMs_ = now;
}

DWORD WINAPI ModApp::DeferredOverlayThreadProc(LPVOID param) {
    auto* self = static_cast<ModApp*>(param);
    if (!self) {
        return 0;
    }

    debuglog::WriteInfo("[ui][d3d] deferred overlay thread started");
    while (!self->deferredOverlayThreadStop_.load()) {
        if (self->RefreshSampGate()) {
            self->RequestOverlayAttachOnce("SA:MP full-ready gate");
            break;
        }
        Sleep(50);
    }
    debuglog::WriteInfo("[ui][d3d] deferred overlay thread finished");
    return 0;
}

void ModApp::StartDeferredOverlayThread() {
    if (overlayAttachRequested_.load() || deferredOverlayThread_) {
        return;
    }

    deferredOverlayThreadStop_.store(false);
    deferredOverlayThread_ = CreateThread(nullptr, 0, &DeferredOverlayThreadProc, this, 0, nullptr);
    if (!deferredOverlayThread_) {
        debuglog::WriteError("[ui][d3d] deferred overlay thread creation failed: %lu", GetLastError());
    }
}

void ModApp::StopDeferredOverlayThread() {
    deferredOverlayThreadStop_.store(true);
    if (!deferredOverlayThread_) {
        return;
    }

    if (GetCurrentThreadId() != GetThreadId(deferredOverlayThread_)) {
        WaitForSingleObject(deferredOverlayThread_, 5000);
    }
    CloseHandle(deferredOverlayThread_);
    deferredOverlayThread_ = nullptr;
}

void ModApp::RequestOverlayAttachOnce(const char* reason) {
    if (overlayAttachRequested_.exchange(true)) {
        return;
    }

    debuglog::WriteInfo(
        "[ui][d3d] overlay attach requested after %s; installing D3D hooks now",
        reason ? reason : "gate");
    overlay_.OnProcessAttach();
}

bool ModApp::RefreshSampGate() {
    const std::uint64_t now = GetTickCount64();
    if (now < nextSampRefreshAtMs_) {
        return sampUiPipelineReady_;
    }

    debuglog::WriteInfo("[probe] Refresh begin ts=%llums", static_cast<unsigned long long>(now));
    sampApi_.Refresh();
    const bool readyBeforeHooks = sampApi_.isSAMPInitilizeLua();
    sampHooks_.Refresh();
    sampRakHooks_.Refresh();
    const bool readyAfterHooks = sampApi_.isSAMPInitilizeLua();
    if (!readyAfterHooks || readyBeforeHooks != readyAfterHooks || sampUiPipelineReady_ != readyAfterHooks) {
        sampApi_.LogReadinessDiagnostics("tick");
    }
    if (!readyAfterHooks && now >= nextRuntimeModuleSnapshotAtMs_) {
        LogLoadedModuleSnapshot("runtime-new", true, GetModuleDirectory(nullptr));
        nextRuntimeModuleSnapshotAtMs_ = now + 2000;
    }
    if (!readyAfterHooks && sampApi_.sampModule() && sampApi_.isSupportedVersion()) {
        if (sampNotReadySinceMs_ == 0) {
            sampNotReadySinceMs_ = now;
            nextSampStuckTraceAtMs_ = now + 8000;
        } else if (now >= nextSampStuckTraceAtMs_) {
            debuglog::WriteError(
                "[probe][stuck] SA:MP stayed before full-ready gate for %llums; "
                "check [samp][diag] transfer owners, [diag][appcompat], apphelp/skygfx/d3d9-proxy/MoonLoader modules. lastError=\"%s\"",
                static_cast<unsigned long long>(now - sampNotReadySinceMs_),
                sampApi_.lastError().c_str());
            sampApi_.LogReadinessDiagnostics("stuck");
            LogLoadedModuleSnapshot("runtime-stuck", true, GetModuleDirectory(nullptr));
            nextSampStuckTraceAtMs_ = now + 8000;
        }
    } else {
        sampNotReadySinceMs_ = 0;
        nextSampStuckTraceAtMs_ = 0;
    }
    if (sampUiPipelineReady_ != readyAfterHooks) {
        debuglog::WriteInfo(
            "[probe] SA:MP input gate changed %d -> %d ts=%llums",
            sampUiPipelineReady_ ? 1 : 0,
            readyAfterHooks ? 1 : 0,
            static_cast<unsigned long long>(GetTickCount64()));
    }
    sampUiPipelineReady_ = readyAfterHooks;
    debuglog::WriteInfo(
        "[probe] Refresh end ts=%llums sampReady(beforeHooks=%d afterHooks=%d) module=%d supported=%d",
        static_cast<unsigned long long>(GetTickCount64()),
        readyBeforeHooks ? 1 : 0,
        readyAfterHooks ? 1 : 0,
        sampApi_.sampModule() ? 1 : 0,
        sampApi_.isSupportedVersion() ? 1 : 0);
    nextSampRefreshAtMs_ = now + 1000;
    return readyAfterHooks;
}

void ModApp::Tick() {
    AppConfig::Instance().ProcessPendingWrites();
    incomingMessageRouter_.Tick();
    binder_.SetGameInputForeground(overlay_.IsGameWindowForeground());
    binder_.Tick();
    tags_.Tick();

    RefreshSampGate();

    UpdateOverlayCursorMode();
}

void ModApp::ApplyMainStyle(float scale) const {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    ImVec4* colors = style.Colors;

    style.WindowPadding = ScaleVec(5.0f, 5.0f);
    style.FramePadding = ScaleVec(5.0f, 3.0f);
    style.ItemSpacing = ScaleVec(5.0f, 4.0f);
    style.ItemInnerSpacing = ScaleVec(3.0f, 3.0f);
    style.IndentSpacing = 8.0f * scale;
    style.ScrollbarSize = 10.0f * scale;
    style.GrabMinSize = 12.0f * scale;
    style.WindowBorderSize = 2.0f * scale;
    style.ChildBorderSize = 1.0f * scale;
    style.PopupBorderSize = 2.0f * scale;
    style.FrameBorderSize = 2.0f * scale;
    style.TabBorderSize = 1.5f * scale;
    style.WindowRounding = 6.0f * scale;
    style.ChildRounding = 6.0f * scale;
    style.FrameRounding = 4.0f * scale;
    style.PopupRounding = 5.0f * scale;
    style.ScrollbarRounding = 4.0f * scale;
    style.GrabRounding = 4.0f * scale;
    style.TabRounding = 4.0f * scale;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.5f, 0.5f);
    style.SeparatorTextBorderSize = 3.0f * scale;
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextPadding = ImVec2(8.0f * scale, style.FramePadding.y);

    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.92f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.39f, 0.46f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.12f, 0.15f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.14f, 0.18f, 0.98f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.14f, 0.16f, 0.20f, 0.97f);
    colors[ImGuiCol_Border] = ImVec4(0.34f, 0.39f, 0.48f, 0.82f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.30f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.19f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.23f, 0.28f, 0.38f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.27f, 0.36f, 0.51f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.34f, 0.39f, 0.48f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.47f, 0.60f, 0.80f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.62f, 0.72f, 0.92f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.47f, 0.60f, 0.80f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.62f, 0.72f, 0.92f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.13f, 0.16f, 0.21f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.19f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.13f, 0.16f, 0.21f, 0.75f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.13f, 0.15f, 0.19f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.17f, 0.19f, 0.23f, 0.90f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.26f, 0.29f, 0.38f, 0.75f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.34f, 0.39f, 0.48f, 0.90f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.47f, 0.60f, 0.80f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.22f, 0.26f, 0.34f, 0.78f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.34f, 0.41f, 0.58f, 0.90f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.47f, 0.60f, 0.80f, 0.96f);
    colors[ImGuiCol_Separator] = ImVec4(0.27f, 0.33f, 0.44f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.47f, 0.60f, 0.80f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.62f, 0.72f, 0.92f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.47f, 0.60f, 0.80f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.47f, 0.60f, 0.80f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.62f, 0.72f, 0.92f, 0.95f);
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.17f, 0.21f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.34f, 0.39f, 0.48f, 0.90f);
    colors[ImGuiCol_TabActive] = ImVec4(0.47f, 0.60f, 0.80f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.20f, 0.29f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.14f, 0.17f, 0.22f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.24f, 0.29f, 0.38f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.17f, 0.20f, 0.25f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.035f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.47f, 0.60f, 0.80f, 0.35f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.47f, 0.60f, 0.80f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.47f, 0.60f, 0.80f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.15f, 0.18f, 0.22f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.15f, 0.18f, 0.22f, 0.70f);
}

void ModApp::LoadShellState() {
    const jsonutil::JsonObject section = AppConfig::Instance().ReadSectionObject(kShellSectionName);
    sidebarCollapsed_ = jsonutil::JsonBoolOr(&section, "sidebar_collapsed", false);
    debuglog::WriteInfo("Shell state loaded (sidebar_collapsed=%d)", sidebarCollapsed_ ? 1 : 0);
}

void ModApp::QueueShellStateSave() const {
    const bool sidebarCollapsed = sidebarCollapsed_;
    debuglog::WriteInfo("Queue shell state save (sidebar_collapsed=%d)", sidebarCollapsed ? 1 : 0);
    AppConfig::Instance().QueueMutation([sidebarCollapsed](jsonutil::JsonObject& root) {
        jsonutil::JsonObject section;
        const auto existing = root.find(std::string(kShellSectionName));
        if (existing != root.end()) {
            if (const jsonutil::JsonObject* object = existing->second.TryObject()) {
                section = *object;
            }
        }

        section["sidebar_collapsed"] = sidebarCollapsed;
        root[std::string(kShellSectionName)] = jsonutil::JsonValue(std::move(section));
    });
}

void ModApp::ReloadConfigAfterProfileChange() {
    overlay_.CancelMenuToggleHotkeyCapture();
    UiSettings::Instance().Load();
    debuglog::SetLevel(ToDebugLogLevel(UiSettings::Instance().LogLevel()));
    LoadShellState();
    tags_.ReloadConfig();
    binder_.ReloadConfig();
    sampHooks_.SetApplyDamageProtectionEnabled(UiSettings::Instance().ApplyDamageProtectionEnabled());
    debuglog::WriteInfo(
        "[profiles] active profile applied id=%s path=%ls",
        AppConfig::Instance().ActiveProfileId().c_str(),
        AppConfig::Instance().ConfigPath().c_str());
}

void ModApp::SetSidebarCollapsed(bool collapsed) {
    if (sidebarCollapsed_ == collapsed) {
        return;
    }

    debuglog::WriteInfo("Sidebar collapsed changed %d -> %d", sidebarCollapsed_ ? 1 : 0, collapsed ? 1 : 0);
    sidebarCollapsed_ = collapsed;
    QueueShellStateSave();
}

void ModApp::EnsureLogoTexture(IDirect3DDevice9* device) {
    if (logoTexture_ || logoLoadAttempted_ || !device || !module_) {
        return;
    }

    logoLoadAttempted_ = true;

    const void* resourceData = nullptr;
    DWORD resourceSize = 0;
    if (!LoadBinaryResource(module_, IDR_MAIN_LOGO, &resourceData, &resourceSize)) {
        debuglog::WriteError("Failed to locate embedded logo resource");
        return;
    }

    D3DXIMAGE_INFO imageInfo{};
    const HRESULT infoResult = D3DXGetImageInfoFromFileInMemory(resourceData, resourceSize, &imageInfo);
    if (FAILED(infoResult)) {
        debuglog::WriteError("D3DXGetImageInfoFromFileInMemory failed: 0x%08lX", static_cast<unsigned long>(infoResult));
        return;
    }

    IDirect3DTexture9* texture = nullptr;
    const HRESULT textureResult = D3DXCreateTextureFromFileInMemory(device, resourceData, resourceSize, &texture);
    if (FAILED(textureResult) || !texture) {
        debuglog::WriteError("D3DXCreateTextureFromFileInMemory failed: 0x%08lX", static_cast<unsigned long>(textureResult));
        return;
    }

    logoTexture_ = texture;
    logoWidth_ = imageInfo.Width;
    logoHeight_ = imageInfo.Height;
    debuglog::WriteInfo("Logo texture loaded (%ux%u)", logoWidth_, logoHeight_);
}

void ModApp::ReleaseUiResources() {
    if (logoTexture_) {
        logoTexture_->Release();
        logoTexture_ = nullptr;
    }

    logoWidth_ = 0;
    logoHeight_ = 0;
    logoLoadAttempted_ = false;
}

MainTab ModApp::DrawAnimatedMenu(float width) {
    const float buttonPadding = Scale(8.0f);
    const float buttonHeight = Scale(38.0f);
    const float cornerRadius = Scale(7.0f);
    constexpr float alphaSpeed = 12.0f;
    constexpr float shiftSpeed = 8.0f;

    const float maxShift = sidebarCollapsed_ ? 0.0f : Scale(18.0f);
    const ImVec4 hoverColor(0.35f, 0.52f, 0.74f, 0.33f);
    const ImVec4 selectedColor(0.17f, 0.32f, 0.46f, 0.74f);
    const ImVec4 textColor(0.88f, 0.88f, 0.88f, 0.98f);
    const ImVec4 activeTextColor(1.0f, 1.0f, 1.0f, 1.0f);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, buttonPadding));

    for (std::size_t i = 0; i < kTabs.size(); ++i) {
        const TabDefinition& tab = kTabs[i];
        MenuAnimationState& animation = menuAnimations_[i];
        const bool isSelected = currentTab_ == tab.tab;

        ImGui::PushID(static_cast<int>(i));
        const ImVec2 itemSize(width, buttonHeight);
        const ImVec2 itemPosition = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("##tab", itemSize);
        const bool hovered = ImGui::IsItemHovered();

        const float targetAlpha = (hovered || isSelected) ? 1.0f : 0.0f;
        const float targetShift = (hovered || isSelected) ? maxShift : 0.0f;
        animation.alpha += (targetAlpha - animation.alpha) * std::min(io.DeltaTime * alphaSpeed, 1.0f);
        animation.shift += (targetShift - animation.shift) * std::min(io.DeltaTime * shiftSpeed, 1.0f);

        if (animation.alpha > 0.01f) {
            ImVec4 fill = isSelected ? selectedColor : hoverColor;
            fill.w *= animation.alpha;
            draw->AddRectFilled(
                itemPosition,
                ImVec2(itemPosition.x + itemSize.x, itemPosition.y + itemSize.y),
                ImGui::GetColorU32(fill),
                cornerRadius);
        }

        const char* text = GetTabIcon(tab.tab);
        std::string expandedLabel;
        if (!sidebarCollapsed_) {
            expandedLabel = FormatTabLabelWithIcon(tab.tab);
            text = expandedLabel.c_str();
        }
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const float textX = sidebarCollapsed_
            ? itemPosition.x + (width - textSize.x) * 0.5f
            : itemPosition.x + Scale(16.0f) + animation.shift;
        const float textY = itemPosition.y + (buttonHeight - textSize.y) * 0.5f;

        draw->AddText(
            ImVec2(textX, textY),
            ImGui::GetColorU32((isSelected || hovered) ? activeTextColor : textColor),
            text);

        if (sidebarCollapsed_ && hovered) {
            ImGui::SetTooltip("%s", GetTabLabel(tab.tab));
        }

        if (pressed) {
            currentTab_ = tab.tab;
        }

        ImGui::PopID();
    }

    ImGui::PopStyleVar();
    ImGui::EndGroup();
    return currentTab_;
}

void ModApp::DrawSectionCard(const char* id, const char* title, const char* description, const ImVec4& accent) const {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyle().Colors[ImGuiCol_ChildBg]);
    if (ImGui::BeginChild(id, ImVec2(0.0f, Scale(86.0f)), ImGuiChildFlags_FrameStyle)) {
        ImGui::TextColored(accent, "%s", title);
        ImGui::Spacing();
        ImGui::TextWrapped("%s", description);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void ModApp::DrawHomeTab() const {
    const UiSettings& ui = UiSettings::Instance();
    ImGui::SeparatorText(ui.Text(UiText::TabHome));
    ImGui::TextWrapped("%s", ui.Text(UiText::HomeIntro));
    ImGui::Spacing();

    DrawSectionCard(
        "home_shell",
        ui.Text(UiText::HomeInterfaceTitle),
        ui.Text(UiText::HomeInterfaceDesc),
        ImVec4(0.75f, 0.90f, 1.0f, 1.0f));
    DrawSectionCard(
        "home_tabs",
        ui.Text(UiText::HomeTabsTitle),
        ui.Text(UiText::HomeTabsDesc),
        ImVec4(0.60f, 1.0f, 0.72f, 1.0f));
}

void ModApp::DrawBinderTab() const {
    const_cast<BinderModule&>(binder_).DrawMainTab();
}

void ModApp::DrawSmiHelperTab() const {
    const UiSettings& ui = UiSettings::Instance();
    ImGui::SeparatorText(ui.Text(UiText::TabSmiHelper));
    ImGui::TextWrapped("%s", ui.Text(UiText::SmiHelperIntro));
    ImGui::Spacing();

    DrawSectionCard(
        "smi_shell",
        ui.Text(UiText::SmiHelperShellTitle),
        ui.Text(UiText::SmiHelperShellDesc),
        ImVec4(0.75f, 0.90f, 1.0f, 1.0f));
}

void ModApp::DrawMiscTab() {
    tags_.DrawMiscTab();
}

void ModApp::DrawNotepadTab() const {
    const UiSettings& ui = UiSettings::Instance();
    ImGui::SeparatorText(ui.Text(UiText::TabNotepad));
    ImGui::TextWrapped("%s", ui.Text(UiText::NotepadIntro));
}

void ModApp::DrawSettingsTab() {
    UiSettings& ui = UiSettings::Instance();
    ImGui::SeparatorText(ui.Text(UiText::TabSettings));
    ImGui::TextWrapped("%s", ui.Text(UiText::SettingsIntro));
    ImGui::Spacing();

    AppConfig& config = AppConfig::Instance();
    std::vector<ConfigProfile> profiles = config.Profiles();
    std::string activeProfileId = config.ActiveProfileId();
    auto activeProfileIt = std::find_if(profiles.begin(), profiles.end(), [&](const ConfigProfile& profile) {
        return profile.id == activeProfileId;
    });
    if (activeProfileIt == profiles.end() && !profiles.empty()) {
        activeProfileIt = profiles.begin();
        activeProfileId = activeProfileIt->id;
    }

    const std::string activeProfileName =
        activeProfileIt == profiles.end() ? std::string() : activeProfileIt->name;
    if (profileNameBufferProfileId_ != activeProfileId) {
        profileNameBuffer_ = activeProfileName;
        profileNameBufferProfileId_ = activeProfileId;
        profileUiError_.clear();
    }

    ImGui::SeparatorText(ui.Text(UiText::SettingsProfilesSection));
    ImGui::TextWrapped("%s", ui.Text(UiText::SettingsProfilesIntro));
    ImGui::SetNextItemWidth(Scale(320.0f));
    if (ImGui::BeginCombo(ui.Text(UiText::SettingsActiveProfile), activeProfileName.c_str())) {
        for (const ConfigProfile& profile : profiles) {
            const bool selected = profile.id == activeProfileId;
            std::string label = profile.name;
            if (label.empty()) {
                label = profile.id;
            }
            if (ImGui::Selectable(label.c_str(), selected)) {
                std::string error;
                if (config.SwitchProfile(profile.id, &error)) {
                    profileNameBufferProfileId_.clear();
                    profileUiError_.clear();
                    ReloadConfigAfterProfileChange();
                    debuglog::WriteInfo("[profiles] UI switched profile id=%s", profile.id.c_str());
                } else {
                    profileUiError_ = ui.Text(UiText::SettingsProfileOperationFailed);
                    debuglog::WriteError("[profiles] UI switch failed id=%s error=%s", profile.id.c_str(), error.c_str());
                }
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SetNextItemWidth(Scale(320.0f));
    InputTextString(ui.Text(UiText::SettingsProfileName), profileNameBuffer_, ImGuiInputTextFlags_AutoSelectAll, 128);

    const auto requireProfileName = [&]() {
        if (TrimAsciiWhitespace(profileNameBuffer_).empty()) {
            profileUiError_ = ui.Text(UiText::SettingsProfileNameRequired);
            return false;
        }
        return true;
    };
    const auto failProfileOperation = [&](const char* action, const std::string& error) {
        profileUiError_ = ui.Text(UiText::SettingsProfileOperationFailed);
        debuglog::WriteError("[profiles] UI %s failed: %s", action, error.c_str());
    };

    if (ImGui::Button(ui.Text(UiText::SettingsProfileCreateEmpty))) {
        if (requireProfileName()) {
            std::string error;
            if (config.CreateProfile(profileNameBuffer_, false, true, &error)) {
                profileNameBufferProfileId_.clear();
                profileUiError_.clear();
                ReloadConfigAfterProfileChange();
            } else {
                failProfileOperation("create", error);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::SettingsProfileDuplicate))) {
        if (requireProfileName()) {
            std::string error;
            if (config.DuplicateProfile(activeProfileId, profileNameBuffer_, true, &error)) {
                profileNameBufferProfileId_.clear();
                profileUiError_.clear();
                ReloadConfigAfterProfileChange();
            } else {
                failProfileOperation("duplicate", error);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ui.Text(UiText::SettingsProfileRename))) {
        if (requireProfileName()) {
            std::string error;
            if (config.RenameProfile(activeProfileId, profileNameBuffer_, &error)) {
                profileNameBufferProfileId_.clear();
                profileUiError_.clear();
            } else {
                failProfileOperation("rename", error);
            }
        }
    }

    const bool canDeleteProfile = profiles.size() > 1;
    if (!canDeleteProfile) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(ui.Text(UiText::SettingsProfileDelete))) {
        profileDeleteTargetId_ = activeProfileId;
        profileDeletePopupPending_ = true;
    }
    if (!canDeleteProfile) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ui.Text(UiText::SettingsProfileCannotDeleteLast));
    }

    if (!profileUiError_.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.30f, 1.0f), "%s", profileUiError_.c_str());
    }

    if (profileDeletePopupPending_) {
        ImGui::OpenPopup(ui.Text(UiText::SettingsProfileDeleteTitle));
        profileDeletePopupPending_ = false;
    }
    if (ImGui::BeginPopupModal(
            ui.Text(UiText::SettingsProfileDeleteTitle),
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const std::vector<ConfigProfile> currentProfiles = config.Profiles();
        const auto deleteProfileIt = std::find_if(currentProfiles.begin(), currentProfiles.end(), [&](const ConfigProfile& profile) {
            return profile.id == profileDeleteTargetId_;
        });
        const std::string deleteProfileName =
            deleteProfileIt == currentProfiles.end() ? activeProfileName : deleteProfileIt->name;
        ImGui::TextWrapped(
            "%s",
            ui.Format(UiText::SettingsProfileDeleteQuestionFormat, deleteProfileName.c_str()).c_str());
        if (ImGui::Button(ui.Text(UiText::Delete))) {
            const std::string previousActiveProfileId = config.ActiveProfileId();
            std::string error;
            if (config.DeleteProfile(profileDeleteTargetId_, &error)) {
                profileDeleteTargetId_.clear();
                profileNameBufferProfileId_.clear();
                profileUiError_.clear();
                if (config.ActiveProfileId() != previousActiveProfileId) {
                    ReloadConfigAfterProfileChange();
                }
                ImGui::CloseCurrentPopup();
            } else {
                failProfileOperation("delete", error);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::Cancel))) {
            profileDeleteTargetId_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::TextWrapped("%s: %s", ui.Text(UiText::SettingsProfilesPath), PathToUtf8(config.ProfilesRoot()).c_str());
    ImGui::TextWrapped(
        "%s: %s",
        ui.Text(UiText::SettingsProfilesRegistryPath),
        PathToUtf8(config.ProfilesRegistryPath()).c_str());
    ImGui::Spacing();

    const UiLanguage languages[] = { UiLanguage::Russian, UiLanguage::English };
    const char* languageLabels[] = {
        ui.LanguageDisplayName(UiLanguage::Russian),
        ui.LanguageDisplayName(UiLanguage::English),
    };
    int languageIndex = ui.Language() == UiLanguage::English ? 1 : 0;
    if (ImGui::Combo(ui.Text(UiText::SettingsLanguage), &languageIndex, languageLabels, IM_ARRAYSIZE(languageLabels))) {
        ui.SetLanguage(languages[languageIndex]);
        debuglog::WriteInfo("Settings changed: language=%s", languageIndex == 1 ? "en" : "ru");
    }

    const UiLogLevel logLevels[] = { UiLogLevel::Off, UiLogLevel::Error, UiLogLevel::Info };
    const char* logLevelLabels[] = {
        ui.Text(UiText::SettingsLogLevelOff),
        ui.Text(UiText::SettingsLogLevelError),
        ui.Text(UiText::SettingsLogLevelInfo),
    };
    int logLevelIndex = static_cast<int>(ui.LogLevel());
    if (ImGui::Combo(ui.Text(UiText::SettingsLogLevel), &logLevelIndex, logLevelLabels, IM_ARRAYSIZE(logLevelLabels))) {
        const UiLogLevel selected = logLevels[std::clamp(logLevelIndex, 0, 2)];
        ui.SetLogLevel(selected);
        debuglog::SetLevel(ToDebugLogLevel(selected));
        debuglog::WriteInfo("Settings changed: log_level=%s", ToUiLogLevelName(selected));
    }

    bool applyDamageProtectionEnabled = ui.ApplyDamageProtectionEnabled();
    if (ImGui::Checkbox(ui.Text(UiText::SettingsApplyDamageProtection), &applyDamageProtectionEnabled)) {
        ui.SetApplyDamageProtectionEnabled(applyDamageProtectionEnabled);
        sampHooks_.SetApplyDamageProtectionEnabled(applyDamageProtectionEnabled);
        debuglog::WriteInfo(
            "Settings changed: apply_damage_protection=%d",
            applyDamageProtectionEnabled ? 1 : 0);
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(ui.Text(UiText::SettingsMainWindowHotkey));
    ImGui::Text("%s", ui.Format(UiText::HotkeyFormat, overlay_.MenuToggleHotkeyText().c_str()).c_str());
    if (ImGui::Button(ui.Text(UiText::ChangeHotkey))) {
        overlay_.BeginMenuToggleHotkeyCapture();
    }

    bool autoScale = ui.AutoScaleEnabled();
    if (ImGui::Checkbox(ui.Text(UiText::SettingsAutoScale), &autoScale)) {
        ui.SetAutoScaleEnabled(autoScale);
        debuglog::WriteInfo("Settings changed: auto_scale=%d", autoScale ? 1 : 0);
    }

    float scaleMultiplier = ui.ScaleMultiplier();
    if (ImGui::SliderFloat(ui.Text(UiText::SettingsScaleMultiplier), &scaleMultiplier, 0.75f, 2.0f, "%.2fx")) {
        ui.SetScaleMultiplier(scaleMultiplier);
        debuglog::WriteInfo("Settings changed: scale_multiplier=%.2f", scaleMultiplier);
    }

    ImGui::Text("%s: %.2fx", ui.Text(UiText::SettingsEffectiveScale), ui.CurrentScale());
    ImGui::TextWrapped("%s", ui.Text(UiText::SettingsScaleHint));

    if (ImGui::Button(ui.Text(UiText::SettingsResetDefaults))) {
        ui.ResetToDefaults();
        debuglog::SetLevel(ToDebugLogLevel(ui.LogLevel()));
        sampHooks_.SetApplyDamageProtectionEnabled(ui.ApplyDamageProtectionEnabled());
        debuglog::WriteInfo("Settings reset to defaults");
        overlay_.CancelMenuToggleHotkeyCapture();
    }

    ImGui::Spacing();
    const_cast<BinderModule&>(binder_).DrawSettingsSection();

    const std::string configPath = PathToUtf8(AppConfig::Instance().ConfigPath());
    ImGui::TextWrapped("%s: %s", ui.Text(UiText::SettingsConfigPath), configPath.c_str());
    ImGui::Text("%s", ui.Format(UiText::GtaVersionFormat, plugin::GetGameVersionName()).c_str());
}

void ModApp::PrepareUiForImGuiNewFrame(IDirect3DDevice9* device) {
    ImGuiIO& io = ImGui::GetIO();
    const float uiScale = UiSettings::Instance().UpdateScale(io.DisplaySize);
    static float s_lastLoggedScale = 0.0f;
    static uint64_t s_lastScaleTraceMs = 0;
    const uint64_t now = GetTickCount64();
    if (std::abs(uiScale - s_lastLoggedScale) > 0.001f || now - s_lastScaleTraceMs >= kUiScaleTraceIntervalMs) {
        s_lastLoggedScale = uiScale;
        s_lastScaleTraceMs = now;
        debuglog::WriteInfo(
            "[ui] frame prep scale=%.3f display=(%.1f,%.1f)",
            uiScale,
            io.DisplaySize.x,
            io.DisplaySize.y);
    }
    io.FontGlobalScale = uiScale;
    ApplyMainStyle(uiScale);
    EnsureLogoTexture(device);
}

void ModApp::RenderUi(IDirect3DDevice9* device) {
    ImGuiIO& io = ImGui::GetIO();
    const float uiScale = io.FontGlobalScale;

    const bool showMainWindow = overlay_.IsMenuOpen();
    static bool s_lastShowMainWindow = false;
    if (showMainWindow != s_lastShowMainWindow) {
        s_lastShowMainWindow = showMainWindow;
        debuglog::WriteInfo("[ui] main window visibility -> %d", showMainWindow ? 1 : 0);
    }
    if (!showMainWindow) {
        binder_.DrawOverlay();
        AppConfig::Instance().ProcessPendingWrites();
        return;
    }

    if (io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f) {
        if (!mainWindowInitialized_) {
            const float margin = kWindowMargin * uiScale;
            mainWindowSize_.x = std::min(1100.0f * uiScale, io.DisplaySize.x - margin * 2.0f);
            mainWindowSize_.y = std::min(720.0f * uiScale, io.DisplaySize.y - margin * 2.0f);
            mainWindowPos_.x = std::max(margin, (io.DisplaySize.x - mainWindowSize_.x) * 0.5f);
            mainWindowPos_.y = std::max(margin, (io.DisplaySize.y - mainWindowSize_.y) * 0.5f);
            mainWindowInitialized_ = true;
            appliedUiScale_ = uiScale;
        } else {
            const float scaleDelta = uiScale - appliedUiScale_;
            if (scaleDelta > 0.001f || scaleDelta < -0.001f) {
                const float ratio = uiScale / std::max(appliedUiScale_, 0.001f);
                mainWindowPos_.x *= ratio;
                mainWindowPos_.y *= ratio;
                mainWindowSize_.x *= ratio;
                mainWindowSize_.y *= ratio;
                appliedUiScale_ = uiScale;
            }
        }

        ClampWindowRect(io.DisplaySize, mainWindowPos_, mainWindowSize_, uiScale);
    }

    ImGui::SetNextWindowPos(mainWindowPos_, ImGuiCond_Always);
    ImGui::SetNextWindowSize(mainWindowSize_, ImGuiCond_Always);

    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin("HelperByOrc##main_window", nullptr, windowFlags)) {
        mainWindowPos_ = ImGui::GetWindowPos();
        mainWindowSize_ = ImGui::GetWindowSize();

        ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 pad = style.WindowPadding;
        const float titleHeight = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;
        const ImVec2 winPos = ImGui::GetWindowPos();
        const ImVec2 winSize = ImGui::GetWindowSize();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##titlebar", ImVec2(winSize.x, titleHeight));

        const ImVec2 titleMin = ImGui::GetItemRectMin();
        const ImVec2 titleMax = ImGui::GetItemRectMax();
        draw->AddRectFilled(
            titleMin,
            titleMax,
            ImGui::GetColorU32(ImGui::IsItemActive() ? style.Colors[ImGuiCol_TitleBgActive] : style.Colors[ImGuiCol_TitleBg]),
            Scale(6.0f),
            ImDrawFlags_RoundCornersTop);
        draw->AddText(
            ImVec2(titleMin.x + pad.x, titleMin.y + style.FramePadding.y),
            ImGui::GetColorU32(style.Colors[ImGuiCol_Text]),
            UiSettings::Instance().Text(UiText::AppBrand));
        draw->AddText(
            ImVec2(titleMin.x + Scale(110.0f), titleMin.y + style.FramePadding.y),
            ImGui::GetColorU32(style.Colors[ImGuiCol_TextDisabled]),
            FormatTabLabelWithIcon(currentTab_).c_str());

        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
            mainWindowPos_.x += io.MouseDelta.x;
            mainWindowPos_.y += io.MouseDelta.y;
        }

        const char* closeText = "X";
        const ImVec2 closeTextSize = ImGui::CalcTextSize(closeText);
        const float closeSide =
            std::max(titleHeight - Scale(6.0f), std::ceil(std::max(closeTextSize.x, ImGui::GetFontSize()) + Scale(4.0f)));
        const ImVec2 closePos(titleMax.x - pad.x - closeSide, titleMin.y + (titleHeight - closeSide) * 0.5f);
        ImGui::SetCursorScreenPos(closePos);
        const bool closePressed = ImGui::InvisibleButton("##close_main_window", ImVec2(closeSide, closeSide));
        const bool closeHovered = ImGui::IsItemHovered();
        const bool closeActive = ImGui::IsItemActive();
        const ImVec2 closeMin = ImGui::GetItemRectMin();
        const ImVec2 closeMax = ImGui::GetItemRectMax();

        if (closeHovered || closeActive) {
            const ImVec4 baseColor = closeActive ? style.Colors[ImGuiCol_ButtonActive] : style.Colors[ImGuiCol_ButtonHovered];
            draw->AddRectFilled(closeMin, closeMax, ImGui::GetColorU32(baseColor), std::max(Scale(3.0f), style.FrameRounding));
        }

        draw->AddText(
            ImVec2(closeMin.x + (closeSide - closeTextSize.x) * 0.5f, closeMin.y + (closeSide - closeTextSize.y) * 0.5f),
            ImGui::GetColorU32((closeHovered || closeActive) ? style.Colors[ImGuiCol_Text] : style.Colors[ImGuiCol_TextDisabled]),
            closeText);

        if (closePressed) {
            overlay_.SetMenuOpen(false);
        }

        const float sidebarWidth = Scale(sidebarCollapsed_ ? kSidebarCollapsedWidth : kSidebarExpandedWidth);
        const float logoSize = Scale(sidebarCollapsed_ ? kLogoCollapsedSize : kLogoExpandedSize);

        ImGui::SetCursorPos(ImVec2(pad.x, pad.y + titleHeight));
        ImGui::BeginGroup();

        if (ImGui::BeginChild("logo_panel", ImVec2(sidebarWidth, logoSize), kPlainChildFlags)) {
            if (logoTexture_) {
                DrawLogoZoom(
                    logoTexture_,
                    logoWidth_,
                    logoHeight_,
                    currentTab_,
                    ImVec2(logoSize, logoSize),
                    sidebarCollapsed_ ? 0.9f : 1.2f);
            } else {
                ImGui::SetCursorPosY(std::max(0.0f, (logoSize - ImGui::GetTextLineHeight()) * 0.5f));
                ImGui::TextUnformatted(
                    UiSettings::Instance().Text(sidebarCollapsed_ ? UiText::AppBrandCompact : UiText::AppBrand));
            }

            const char* toggleIcon = sidebarCollapsed_ ? ">" : "<";
            const float toggleSide = std::min(Scale(18.0f), std::max(Scale(14.0f), logoSize - Scale(10.0f)));
            const float togglePad = Scale(4.0f);
            const ImVec2 togglePos(
                std::max(0.0f, sidebarWidth - toggleSide - togglePad),
                togglePad);
            ImGui::SetCursorPos(togglePos);
            ImGui::InvisibleButton("##sidebar_toggle", ImVec2(toggleSide, toggleSide));
            const bool toggleHovered = ImGui::IsItemHovered();
            const bool togglePressed = ImGui::IsItemClicked();
            const ImVec2 toggleMin = ImGui::GetItemRectMin();
            const ImVec2 toggleTextSize = ImGui::CalcTextSize(toggleIcon);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(toggleMin.x + (toggleSide - toggleTextSize.x) * 0.5f, toggleMin.y + (toggleSide - toggleTextSize.y) * 0.5f),
                ImGui::GetColorU32(toggleHovered ? style.Colors[ImGuiCol_Text] : style.Colors[ImGuiCol_TextDisabled]),
                toggleIcon);
            if (togglePressed) {
                SetSidebarCollapsed(!sidebarCollapsed_);
            }
        }
        ImGui::EndChild();

        if (ImGui::BeginChild("vertical_menu", ImVec2(sidebarWidth, 0.0f), kPlainChildFlags)) {
            DrawAnimatedMenu(sidebarWidth);
        }
        ImGui::EndChild();
        ImGui::EndGroup();

        ImGui::SameLine();
        if (ImGui::BeginChild("main_content", ImVec2(0.0f, 0.0f), kBorderedChildFlags)) {
            switch (currentTab_) {
            case MainTab::Home:
                DrawHomeTab();
                break;
            case MainTab::Binder:
                DrawBinderTab();
                break;
            case MainTab::SmiHelper:
                DrawSmiHelperTab();
                break;
            case MainTab::Misc:
                DrawMiscTab();
                break;
            case MainTab::Notepad:
                DrawNotepadTab();
                break;
            case MainTab::Settings:
                DrawSettingsTab();
                break;
            }
        }
        ImGui::EndChild();
    }

    ImGui::End();
    overlay_.DrawMenuToggleHotkeyCapturePopup();
    binder_.DrawOverlay();
    AppConfig::Instance().ProcessPendingWrites();
}
