#include "../../include/blockthespot_mod.h"

// Build from a Visual Studio x64 developer prompt:
// cl /LD /MT /EHsc hello.cpp /link /OUT:hello.dll
extern "C" __declspec(dllexport) int __cdecl bts_mod_init(const BtsModHost* host) {
    if (!host || host->api_version != BTS_MOD_API_VERSION || host->size < sizeof(BtsModHost)) return 0;
    host->log(host->log_context, 1, "Hello from a BlockTheSpot DLL mod.");
    return 1;
}
