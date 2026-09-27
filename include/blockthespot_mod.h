#pragma once
#include <stddef.h>
#include <stdint.h>

#define BTS_MOD_API_VERSION 1u

/* Windows x64 C ABI. All strings and this structure live until process exit.
   Do not free them. Initialization runs once in Spotify's main process, after
   BlockTheSpot's hooks are ready and outside DllMain's loader lock. */
typedef struct BtsModHost {
    uint32_t size;
    uint32_t api_version;
    const wchar_t* spotify_directory;
    const wchar_t* mod_directory;
    const wchar_t* config_path; /* same-stem .ini, or NULL */
    const char* spotify_version;
    void* log_context;
    void (__cdecl *log)(void* context, int level, const char* message); /* 0 error, 1 info, 2 debug */
} BtsModHost;

/* Optional export: int __cdecl bts_mod_init(const BtsModHost* host);
   Return nonzero on success. Keep DllMain minimal. No hot unload is supported.
   A DLL without this export is loaded normally (DllMain only). */
typedef int (__cdecl *BtsModInit)(const BtsModHost* host);
