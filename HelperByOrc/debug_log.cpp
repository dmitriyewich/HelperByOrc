#include "debug_log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string_view>

namespace debuglog {
namespace {

HMODULE g_module = nullptr;
Level g_level = Level::Info;
std::mutex g_mutex;
bool g_sessionInitialized = false;

bool ResolveLogPath(HMODULE module, char (&path)[MAX_PATH]) {
    if (!module || !GetModuleFileNameA(module, path, MAX_PATH)) {
        return false;
    }

    char* slash = std::strrchr(path, '\\');
    if (!slash) {
        return false;
    }

    *(slash + 1) = '\0';
    return strcat_s(path, "HelperByOrc.log") == 0;
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
    return static_cast<int>(g_level) >= static_cast<int>(level);
}

void WriteInternal(Level level, bool force, const char* format, va_list args) {
    if (!format) {
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

    char path[MAX_PATH]{};
    if (!ResolveLogPath(g_module, path)) {
        return;
    }

    FILE* file = nullptr;
    if (fopen_s(&file, path, "a") != 0 || !file) {
        return;
    }

    std::fprintf(file, "%s\n", message);
    std::fclose(file);
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
        return;
    }
    g_sessionInitialized = true;

    char path[MAX_PATH]{};
    if (!ResolveLogPath(module, path)) {
        OutputDebugStringA("[HelperByOrc] failed to resolve HelperByOrc.log path\n");
        return;
    }

    const HANDLE file = CreateFileA(
        path,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        char message[160]{};
        std::snprintf(
            message,
            sizeof(message),
            "[HelperByOrc] failed to reset HelperByOrc.log: win32=%lu\n",
            static_cast<unsigned long>(GetLastError()));
        OutputDebugStringA(message);
        return;
    }

    CloseHandle(file);
}

void Shutdown() {
    std::lock_guard lock(g_mutex);
    g_module = nullptr;
}

void SetLevel(Level level) {
    std::lock_guard lock(g_mutex);
    g_level = level;
}

Level GetLevel() {
    std::lock_guard lock(g_mutex);
    return g_level;
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
