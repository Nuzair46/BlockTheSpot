#include "pch.h"
#include "loader.h"

// A normal import dependency loads this DLL before the proxy. The anchor does
// no work under the loader lock; initialization is deferred to the startup APC.
extern "C" __declspec(dllexport) void bts_load_anchor() {}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        patch_module = module;
        if (!QueueUserAPC(bts_main, GetCurrentThread(), 0))
            OutputDebugStringW(L"BlockTheSpot: unable to queue initialization\n");
    }
    return TRUE;
}
