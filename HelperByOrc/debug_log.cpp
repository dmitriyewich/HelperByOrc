#include "debug_log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <string_view>

namespace debuglog {
namespace {

HMODULE g_module = nullptr;
std::atomic<Level> g_level{Level::Info};
std::mutex g_mutex;
bool g_sessionInitialized = false;
bool g_sessionResetComplete = false;
HANDLE g_file = INVALID_HANDLE_VALUE;
wchar_t g_path[MAX_PATH]{};
bool g_pathResolved = false;
DWORD g_lastError = ERROR_SUCCESS;
FileOperation g_lastFailure = FileOperation::None;
std::uint64_t g_successfulWrites = 0;
std::uint64_t g_openAttempts = 0;
std::uint64_t g_recoveries = 0;
ULONGLONG g_lastStatusPathCheckAt = 0;
bool g_fileExists = false;
// The plugin is the only writer. Readers may tail the file, while denying
// shared write/delete keeps both ownership and the directory entry stable.
constexpr DWORD kLogShareMode = FILE_SHARE_READ;
constexpr ULONGLONG kStatusPathCheckIntervalMs = 5000;

bool ResolveLogPath(HMODULE module, wchar_t (&path)[MAX_PATH]) {
    path[0] = L'\0';
    if (!module) {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }

    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, modulePath, MAX_PATH);
    if (length == 0) {
        return false;
    }
    if (length >= MAX_PATH) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }

    wchar_t* slash = std::wcsrchr(modulePath, L'\\');
    if (!slash) {
        SetLastError(ERROR_INVALID_NAME);
        return false;
    }

    *(slash + 1) = L'\0';
    if (wcscat_s(modulePath, L"HelperByOrc.log") != 0
        || wcscpy_s(path, modulePath) != 0) {
        path[0] = L'\0';
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
    return true;
}

void SetFailure(FileOperation operation, DWORD error) {
    g_lastFailure = operation;
    g_lastError = error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
}

void CloseFile() {
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
}

bool RefreshFileExists() {
    if (g_path[0] == L'\0') {
        g_fileExists = false;
        return false;
    }
    const DWORD attributes = GetFileAttributesW(g_path);
    g_fileExists = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    return g_fileExists;
}

bool OpenFile(bool reset) {
    if (!g_pathResolved) {
        if (!ResolveLogPath(g_module, g_path)) {
            SetFailure(FileOperation::ResolvePath, GetLastError());
            return false;
        }
        g_pathResolved = true;
    }

    ++g_openAttempts;
    const HANDLE file = CreateFileW(
        g_path,
        FILE_APPEND_DATA,
        kLogShareMode,
        nullptr,
        reset ? CREATE_ALWAYS : OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD openError = GetLastError();
        RefreshFileExists();
        SetFailure(reset ? FileOperation::ResetOpen : FileOperation::AppendOpen, openError);
        return false;
    }

    g_file = file;
    if (reset) {
        g_sessionResetComplete = true;
    }
    g_lastStatusPathCheckAt = GetTickCount64();
    g_fileExists = true;
    return true;
}

bool EnsureFileOpen() {
    return g_file != INVALID_HANDLE_VALUE || OpenFile(!g_sessionResetComplete);
}

bool WriteLine(const char* line, DWORD lineLength, bool allowRetry) {
    if (!EnsureFileOpen()) {
        return false;
    }

    DWORD bytesWritten = 0;
    const BOOL writeSucceeded = WriteFile(g_file, line, lineLength, &bytesWritten, nullptr);
    if (writeSucceeded && bytesWritten == lineLength) {
        ++g_successfulWrites;
        g_fileExists = true;
        return true;
    }

    const DWORD writeError = writeSucceeded ? ERROR_WRITE_FAULT : GetLastError();
    SetFailure(FileOperation::Write, writeError);
    CloseFile();
    if (!allowRetry || bytesWritten != 0) {
        return false;
    }

    ++g_recoveries;
    return OpenFile(!g_sessionResetComplete) && WriteLine(line, lineLength, false);
}

bool IsErrorLike(std::string_view message) {
    return message.find("FAILED") != std::string_view::npos
        || message.find("failed") != std::string_view::npos
        || message.find("ERROR") != std::string_view::npos
        || message.find("Error") != std::string_view::npos
        || message.find("error") != std::string_view::npos
        || message.find("ошиб") != std::string_view::npos;
}

bool ShouldWrite(Level level) {
    return static_cast<int>(g_level.load(std::memory_order_relaxed))
        >= static_cast<int>(level);
}

void WriteInternal(Level level, bool force, const char* format, va_list args) {
    if (!format) {
        return;
    }
    if (!force && !ShouldWrite(level)) {
        return;
    }

    char message[4096]{};
    std::vsnprintf(message, sizeof(message), format, args);

    std::lock_guard lock(g_mutex);
    if (!force && !ShouldWrite(level)) {
        return;
    }

    OutputDebugStringA(message);
    OutputDebugStringA("\n");

    if (!g_module) {
        return;
    }

    char line[sizeof(message) + 2]{};
    const int lineLength = std::snprintf(line, sizeof(line), "%s\r\n", message);
    if (lineLength <= 0 || lineLength >= static_cast<int>(sizeof(line))) {
        return;
    }
    WriteLine(line, static_cast<DWORD>(lineLength), true);
}

} // namespace

void Initialize(HMODULE module) {
    std::lock_guard lock(g_mutex);
    if (!module) {
        OutputDebugStringA("[HelperByOrc] debuglog initialization failed: module is null\n");
        return;
    }

    g_module = module;
    if (g_sessionInitialized) {
        if (g_file == INVALID_HANDLE_VALUE) {
            OpenFile(!g_sessionResetComplete);
        }
        return;
    }
    g_sessionInitialized = true;

    if (!ResolveLogPath(module, g_path)) {
        SetFailure(FileOperation::ResolvePath, GetLastError());
        OutputDebugStringA("[HelperByOrc] failed to resolve HelperByOrc.log path\n");
        return;
    }
    g_pathResolved = true;

    if (!OpenFile(true)) {
        char message[160]{};
        std::snprintf(
            message,
            sizeof(message),
            "[HelperByOrc] failed to reset HelperByOrc.log: win32=%lu\n",
            static_cast<unsigned long>(g_lastError));
        OutputDebugStringA(message);
    }
}

void Shutdown() {
    std::lock_guard lock(g_mutex);
    if (g_file != INVALID_HANDLE_VALUE) {
        if (!FlushFileBuffers(g_file)) {
            SetFailure(FileOperation::Flush, GetLastError());
        }
        CloseFile();
    }
    g_module = nullptr;
}

void SetLevel(Level level) {
    g_level.store(level, std::memory_order_relaxed);
}

Level GetLevel() {
    return g_level.load(std::memory_order_relaxed);
}

FileStatus GetFileStatus() {
    std::lock_guard lock(g_mutex);
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastStatusPathCheckAt >= kStatusPathCheckIntervalMs) {
        g_lastStatusPathCheckAt = now;
        RefreshFileExists();
    }
    return FileStatus{
        .handleOpen = g_file != INVALID_HANDLE_VALUE,
        .fileExists = g_fileExists,
        .lastError = g_lastError,
        .lastFailure = g_lastFailure,
        .successfulWrites = g_successfulWrites,
        .openAttempts = g_openAttempts,
        .recoveries = g_recoveries,
    };
}

bool RetryFileOpen() {
    std::lock_guard lock(g_mutex);
    CloseFile();
    ++g_recoveries;
    return OpenFile(!g_sessionResetComplete);
}

const char* FileOperationName(FileOperation operation) {
    switch (operation) {
    case FileOperation::ResolvePath:
        return "resolve-path";
    case FileOperation::ResetOpen:
        return "reset-open";
    case FileOperation::AppendOpen:
        return "append-open";
    case FileOperation::Write:
        return "write";
    case FileOperation::Flush:
        return "flush";
    case FileOperation::None:
    default:
        return "none";
    }
}

void WriteError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    WriteInternal(Level::Error, false, format, args);
    va_end(args);
}

void WriteInfo(const char* format, ...) {
    va_list args;
    va_start(args, format);
    WriteInternal(Level::Info, false, format, args);
    va_end(args);
}

void WriteAlways(Level level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    WriteInternal(level, true, format, args);
    va_end(args);
}

void Write(const char* format, ...) {
    if (!format) {
        return;
    }

    const Level effectiveLevel = IsErrorLike(format) ? Level::Error : Level::Info;
    va_list args;
    va_start(args, format);
    WriteInternal(effectiveLevel, false, format, args);
    va_end(args);
}

} // namespace debuglog
