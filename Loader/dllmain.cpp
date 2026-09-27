#include "pch.h"
extern "C" __declspec(dllimport) void bts_load_anchor();

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) bts_load_anchor();
    return TRUE;
}
