#include "debug_log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace debuglog {
namespace {

HMODULE g_module = nullptr;
Level g_level = Level::Info;

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

void WriteInternal(Level level, const char* format, va_list args) {
    if (!format) {
        return;
    }

    char message[4096]{};
    std::vsnprintf(message, sizeof(message), format, args);

    if (!ShouldWrite(level)) {
        return;
    }

    OutputDebugStringA(message);
    OutputDebugStringA("\n");

    if (!g_module) {
        return;
    }

    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(g_module, path, MAX_PATH)) {
        return;
    }

    char* slash = std::strrchr(path, '\\');
    if (!slash) {
        return;
    }

    *(slash + 1) = '\0';
    strcat_s(path, "HelperByOrc.log");

    FILE* file = nullptr;
    if (fopen_s(&file, path, "a") != 0 || !file) {
        return;
    }

    std::fprintf(file, "%s\n", message);
    std::fclose(file);
}

} // namespace

void Initialize(HMODULE module) {
    g_module = module;
}

void Shutdown() {
    g_module = nullptr;
}

void SetLevel(Level level) {
    g_level = level;
}

Level GetLevel() {
    return g_level;
}

void WriteError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    WriteInternal(Level::Error, format, args);
    va_end(args);
}

void WriteInfo(const char* format, ...) {
    va_list args;
    va_start(args, format);
    WriteInternal(Level::Info, format, args);
    va_end(args);
}

void Write(const char* format, ...) {
    if (!format) {
        return;
    }

    const Level effectiveLevel = IsErrorLike(format) ? Level::Error : Level::Info;
    va_list args;
    va_start(args, format);
    WriteInternal(effectiveLevel, format, args);
    va_end(args);
}

} // namespace debuglog
