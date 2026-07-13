#include "native_file_dialog.h"

#include "debug_log.h"
#include "ui_frame_timing.h"

#include <chrono>
#include <commdlg.h>
#include <memory>
#include <new>
#include <windows.h>

namespace native_file_dialog {
namespace {

HWND CurrentProcessOwnerWindow() {
    const DWORD currentProcessId = GetCurrentProcessId();
    const HWND candidates[] = { GetForegroundWindow(), GetActiveWindow(), GetAncestor(GetFocus(), GA_ROOT) };
    for (const HWND candidate : candidates) {
        DWORD processId = 0;
        if (candidate != nullptr && GetWindowThreadProcessId(candidate, &processId) != 0
            && processId == currentProcessId) {
            return candidate;
        }
    }
    return nullptr;
}

} // namespace

std::optional<std::filesystem::path> OpenFile(const std::wstring& title, const std::wstring& filter) {
    constexpr DWORD kFileNameCapacity = 32768;
    std::unique_ptr<wchar_t[]> fileName(new (std::nothrow) wchar_t[kFileNameCapacity]);
    if (!fileName) {
        debuglog::WriteError("[ui][external] native file dialog buffer allocation failed");
        return std::nullopt;
    }
    fileName[0] = L'\0';

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = CurrentProcessOwnerWindow();
    dialog.lpstrFile = fileName.get();
    dialog.nMaxFile = kFileNameCapacity;
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrTitle = title.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    const auto startedAt = std::chrono::steady_clock::now();
    const BOOL selected = GetOpenFileNameW(&dialog);
    const double waitMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - startedAt).count();
    ui_frame_timing::MarkExternalWait();

    if (selected) {
        debuglog::WriteInfo("[ui][external] native file dialog selected wait=%.2fms", waitMs);
        return std::filesystem::path(fileName.get());
    }

    const DWORD error = CommDlgExtendedError();
    if (error == 0) {
        debuglog::WriteInfo("[ui][external] native file dialog cancelled wait=%.2fms", waitMs);
    } else {
        debuglog::WriteError(
            "[ui][external] native file dialog failed error=0x%08lX wait=%.2fms",
            static_cast<unsigned long>(error),
            waitMs);
    }
    return std::nullopt;
}

} // namespace native_file_dialog
