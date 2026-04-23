#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "debug_log.h"
#include "mod_app.h"

namespace {

HMODULE g_module = nullptr;
HANDLE g_shutdownEvent = nullptr;
HANDLE g_bootstrapThread = nullptr;
bool g_startedSynchronously = false;

DWORD WINAPI ModBootstrapThreadProc(LPVOID) {
    OutputDebugStringA("[HelperByOrc] bootstrap thread started");
    ModApp::Instance().OnProcessAttach(g_module);
    debuglog::WriteInfo("[bootstrap] worker started, attach completed");
    if (g_shutdownEvent) {
        debuglog::WriteInfo("[bootstrap] waiting for shutdown signal");
        WaitForSingleObject(g_shutdownEvent, INFINITE);
    }
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
            // Last-resort fallback keeps plugin functional if event creation fails.
            g_startedSynchronously = true;
            ModApp::Instance().OnProcessAttach(hModule);
            debuglog::WriteInfo("[bootstrap] fallback: sync attach (CreateEventA failed)");
            return TRUE;
        }

        g_bootstrapThread = CreateThread(nullptr, 0, &ModBootstrapThreadProc, nullptr, 0, nullptr);
        if (!g_bootstrapThread) {
            CloseHandle(g_shutdownEvent);
            g_shutdownEvent = nullptr;
            // Last-resort fallback keeps plugin functional if thread creation fails.
            g_startedSynchronously = true;
            ModApp::Instance().OnProcessAttach(hModule);
            debuglog::WriteInfo("[bootstrap] fallback: sync attach (CreateThread failed)");
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        OutputDebugStringA("[HelperByOrc] DllMain PROCESS_DETACH");
        SignalShutdown();
        if (g_startedSynchronously) {
            debuglog::WriteInfo("[bootstrap] fallback sync shutdown");
            ModApp::Instance().Shutdown();
        }

        if (reserved == nullptr && g_bootstrapThread) {
            // Explicit unload path: wait briefly so shutdown runs before code unmaps.
            WaitForSingleObject(g_bootstrapThread, 5000);
        }
        if (g_bootstrapThread) {
            CloseHandle(g_bootstrapThread);
            g_bootstrapThread = nullptr;
        }
        if (g_shutdownEvent) {
            CloseHandle(g_shutdownEvent);
            g_shutdownEvent = nullptr;
        }
    }

    return TRUE;
}
