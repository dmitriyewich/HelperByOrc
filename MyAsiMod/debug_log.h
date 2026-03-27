#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace debuglog {

void Initialize(HMODULE module);
void Shutdown();
void Write(const char* format, ...);

} // namespace debuglog
