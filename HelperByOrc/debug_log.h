#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace debuglog {

enum class Level : int {
    Off = 0,
    Error = 1,
    Info = 2,
};

void Initialize(HMODULE module);
void Shutdown();
void SetLevel(Level level);
Level GetLevel();
void WriteError(const char* format, ...);
void WriteInfo(const char* format, ...);
void WriteAlways(Level level, const char* format, ...);
void Write(const char* format, ...);

} // namespace debuglog
