#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>

namespace debuglog {

enum class Level : int {
    Off = 0,
    Error = 1,
    Info = 2,
};

enum class FileOperation : int {
    None = 0,
    ResolvePath,
    ResetOpen,
    AppendOpen,
    Write,
    Flush,
};

struct FileStatus {
    bool handleOpen = false;
    bool fileExists = false;
    DWORD lastError = ERROR_SUCCESS;
    FileOperation lastFailure = FileOperation::None;
    std::uint64_t successfulWrites = 0;
    std::uint64_t openAttempts = 0;
    std::uint64_t recoveries = 0;
};

void Initialize(HMODULE module);
void Shutdown();
void SetLevel(Level level);
Level GetLevel();
FileStatus GetFileStatus();
bool RetryFileOpen();
const char* FileOperationName(FileOperation operation);
void WriteError(const char* format, ...);
void WriteInfo(const char* format, ...);
void WriteAlways(Level level, const char* format, ...);
void Write(const char* format, ...);

} // namespace debuglog
