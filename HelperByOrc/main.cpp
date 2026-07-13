#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "debug_log.h"
#include "mod_app.h"

namespace {

HMODULE g_module = nullptr;
HANDLE g_shutdownEvent = nullptr;

DWORD WINAPI ModBootstrapThreadProc(LPVOID) {
    OutputDebugStringA("[HelperByOrc] bootstrap thread started");
    HMODULE pinnedModule = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&ModBootstrapThreadProc),
            &pinnedModule)) {
        OutputDebugStringA("[HelperByOrc] process-lifetime module pin failed; bootstrap aborted");
        return 0;
    }
    ModApp::Instance().OnProcessAttach(g_module);
    debuglog::WriteInfo("[bootstrap] worker started, process-lifetime pin active, attach completed");
    if (!g_shutdownEvent) {
        debuglog::WriteError("[bootstrap] shutdown event unavailable after attach");
        return 0;
    }
    debuglog::WriteInfo("[bootstrap] waiting for process-lifetime shutdown signal");
    WaitForSingleObject(g_shutdownEvent, INFINITE);
    debuglog::WriteInfo("[bootstrap] shutdown signal received, stopping modules");
    ModApp::Instance().Shutdown();
    OutputDebugStringA("[HelperByOrc] bootstrap thread finished");
    return 0;
}

void SignalShutdown() {
    if (g_shutdownEvent) {
        SetEvent(g_shutdownEvent);
    }
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_module = hModule;
        OutputDebugStringA("[HelperByOrc] DllMain PROCESS_ATTACH");
        g_shutdownEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        if (!g_shutdownEvent) {
            OutputDebugStringA("[HelperByOrc] bootstrap event creation failed; aborting load");
            return FALSE;
        }

        HANDLE bootstrapThread = CreateThread(nullptr, 0, &ModBootstrapThreadProc, nullptr, 0, nullptr);
        if (!bootstrapThread) {
            CloseHandle(g_shutdownEvent);
            g_shutdownEvent = nullptr;
            OutputDebugStringA("[HelperByOrc] bootstrap thread creation failed; aborting load");
            return FALSE;
        }
        CloseHandle(bootstrapThread);
    } else if (reason == DLL_PROCESS_DETACH) {
        OutputDebugStringA("[HelperByOrc] DllMain PROCESS_DETACH");
        if (reserved != nullptr) {
            SignalShutdown();
        } else {
            OutputDebugStringA("[HelperByOrc] explicit FreeLibrary unload is unsupported");
        }
    }

    return TRUE;
}
