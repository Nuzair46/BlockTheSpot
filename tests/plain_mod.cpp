#include <windows.h>
static int calls = 0;
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) ++calls;
    return TRUE;
}
extern "C" __declspec(dllexport) int bts_test_calls() { return calls; }
