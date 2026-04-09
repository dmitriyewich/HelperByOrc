#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "mod_app.h"

namespace {

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    UNREFERENCED_PARAMETER(reserved);

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        ModApp::Instance().OnProcessAttach(hModule);
    } else if (reason == DLL_PROCESS_DETACH) {
        ModApp::Instance().Shutdown();
    }

    return TRUE;
}
