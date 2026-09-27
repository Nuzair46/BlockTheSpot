#pragma once
#include <stddef.h>
#include <stdint.h>

#define BTS_MOD_API_VERSION 2u

/* Windows x64 C ABI. All strings and this structure live until process exit.
   Do not free them. Initialization runs once in Spotify's main process, before
   the host installs registered patches and hooks and outside DllMain's loader lock. */
typedef struct BtsModHost {
    uint32_t size;
    uint32_t api_version;
    const wchar_t* spotify_directory;
    const wchar_t* mod_directory;
    const wchar_t* config_path; /* same-stem .ini, or NULL */
    const char* spotify_version;
    void* log_context;
    void (__cdecl *log)(void* context, int level, const char* message); /* 0 error, 1 info, 2 debug */
    /* API v2: register a UTF-8 declarative patch pack during bts_mod_init only.
       Source is copied/validated immediately (max 1 MiB). One successful call
       per mod. Registration is committed only if initialization succeeds.
       Pass log_context as context. Returns nonzero on success; errors are logged. */
    int (__cdecl *register_ini)(void* context, const char* source, size_t length);
} BtsModHost;

/* Optional export: int __cdecl bts_mod_init(const BtsModHost* host);
   Return nonzero on success. Keep DllMain minimal. No hot unload is supported.
   A DLL without this export is loaded normally (DllMain only). */
typedef int (__cdecl *BtsModInit)(const BtsModHost* host);
