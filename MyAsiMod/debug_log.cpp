#include "debug_log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace debuglog {
namespace {

HMODULE g_module = nullptr;

} // namespace

void Initialize(HMODULE module) {
    g_module = module;
}

void Shutdown() {
    g_module = nullptr;
}

void Write(const char* format, ...) {
    char message[1024]{};

    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

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

} // namespace debuglog
