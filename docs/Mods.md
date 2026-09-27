# Experimental mods

The host loads mods from `%APPDATA%\Spotify\patches`, independent of the working
directory. BlockTheSpot is itself a bundled DLL mod. The host contains the shared
patch engine and CEF hooks; it has no built-in adblocking rules or signatures.

## Installing and disabling mods

Drop a mod into `patches` and restart Spotify. Mods default to enabled. There is
no registration list and no `settings.ini` requirement.

- `blockthespot.dll` owns `blockthespot.ini`.
- `othermod.dll` owns an optional `othermod.ini`.
- An INI without a matching DLL is a standalone declarative mod, unless it has
  `[Mod] Type=DLL`, which marks it as a DLL companion requiring that DLL.

Pairing ignores case. A companion INI is never loaded as a second standalone
mod. The bundled INI declares `Type=DLL`, so leaving it behind after removing
the bundled DLL does not silently enable adblocking.

Set `[Mod] Enable=0` in a mod's own INI to disable it. Missing Enable means `1`.
For a DLL without an INI, add a same-named INI with that flag or move the DLL out
of `patches`. To disable everything, including BlockTheSpot, set `[Mods] Enable=0`
in root `config.ini`. Root configuration also owns logging, crashpad process
handling, host compatibility, and CEF ABI offsets. Mod-specific features and
signatures belong in the mod's INI.

The loader discovers up to 256 DLL/INI files directly in `patches`. Use ASCII
filenames up to 128 characters. Subfolders and other extensions are ignored;
links and copies of the host/proxy DLLs are rejected. DLL initialization and
patch-group precedence use case-insensitive filename order. Native DLLs run
only in Spotify's main process, outside DllMain's loader lock, before registered
patches and CEF hooks are installed.

Close Spotify before replacing files. There is no hot reload or unload. DLL mods
execute with Spotify's permissions and can crash it; use authors you trust.
A failed load or initializer is reported and later mods are still attempted.
When updating INIs, carry preferences into the new version and preserve its new
signatures. The host does not rewrite mod configurations.

`blockthespot-status.txt` contains `Mod <filename>` and per-target rows. `loaded`
means a DLL loaded and its optional initializer succeeded; target rows report
whether its registered patches applied. `ready` on a standalone INI means it
registered. Log messages identify their mod. Set root `[Log] Level=1` for
informational messages.

All mods are skipped if root `[Compatibility] Spotify` does not match the
installed version. Declarative packs must also declare their own exact version.
A DLL's optional companion INI may declare the same restriction; omitting it
accepts any version supported by the host. Renaming a DLL/INI pair does not
change its compatibility or grant special treatment.

## Declarative patch packs

Patch names are local to one INI. Packs can combine native patches, frontend
patches, URL rules, and stylesheet patches. The bundled
[`blockthespot.ini`](../patches/blockthespot.ini) is a full example, submitted by
its DLL through the API below. [`sample.ini`](../examples/mods/sample.ini) is a
disabled synthetic standalone example, with no real Spotify signatures.

`[NativePatches]` is a contiguous numbered list of section names. Each patch has
`Signature`, `Offset`, `Value`, optional `Enable=0|1` (default `1`), and optional
`Module=Spotify.dll` (default) or `Module=Spotify.exe`. Patches search the loaded
module's `.text`. Arbitrary paths and addresses are rejected. `Enable=0` on
`NativePatches` skips that group.

`[Buffer_modify]` lists exact SPA filenames. Each file section lists patch section
names. A patch has `Signature_1`, `Offset_1`, and `Value_1`, with an optional
complete second triple ending in `_2`. Both JS and exact CSS filenames work.
`Enable=0` on `Buffer_modify` skips these frontend patches.

`[URL_block]` lists ASCII path substrings beginning with `/`, for example
`1=/ads/`. Queries and fragments are excluded from matching. `Enable=0` disables
that mod's URL rules; other mods' rules still apply.

`[Stylesheets]` lists patch section names for CSS files whose names change between
Spotify releases. Each section has `Extension=.css`, unsuffixed `Signature`,
`Offset`, `Value`, and optional `Enable=0|1`. The signature must match **at byte
zero** to select a stylesheet, and must be unique in that file. Unrelated CSS
is ignored. `[Stylesheets] Enable=0` disables the group. The bundled optional
`Homepage_vbar` section uses this format and defaults to disabled.

Numbered lists are contiguous from `1`, with at most 256 entries. See the
[signature guide](UpdateSignature-advanced.md) for the byte grammar and bounds.

All groups match the same original bytes. Filename order determines precedence,
including the bundled DLL; it has no special priority. Within one group, every
signature must match exactly once, all writes must fit, and writes cannot
overlap. A failing group contributes no writes. A group overlapping an earlier
accepted group is rejected even if its bytes are identical. Transactions cover
one target at a time; unrelated groups can still apply. Partial ZIP reads are
left unchanged.

Validate a pack with the shared offline engine:

```powershell
.\out\tools\patch-tool.exe inspect-mod .\patches\blockthespot.ini
.\out\tools\patch-tool.exe apply-mod .\custom-ui.ini sample.js .\clean.js .\patched.js
node --check .\patched.js
```

Native inputs must be an extracted `.text` section, with target `Spotify.dll`
or `Spotify.exe`. Stylesheet targets use the patch section name. Add `--all`
to inspect or apply disabled options during **offline validation**; normal
commands respect Enable flags. Root `config.ini` can be checked with `inspect`.
Output only goes to the specified destination. Offline checks of one mod cannot
detect conflicts with other installed mods; runtime reports those.

## DLL API

Build a Windows x64 DLL. `DllMain` runs normally. For initialization outside
its loader lock, export:

```cpp
#include "blockthespot_mod.h"
extern "C" __declspec(dllexport)
int __cdecl bts_mod_init(const BtsModHost* host);
```

[`blockthespot_mod.h`](../include/blockthespot_mod.h) defines API **version 2**.
Check `host->api_version` and `host->size` before accessing fields. This version
adds declarative registration and initializes mods **before** host hooks; rebuild
mods written for experimental API v1 and its different initialization timing.

The host provides installation and mod directories, an optional same-stem INI
path, Spotify version, a logging callback, and `register_ini`. The structure and
strings remain valid until process exit; do not free them. Return nonzero on
success. Keep DllMain minimal and do not throw across the C API; see Microsoft's
[DLL initialization guidance](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices).

To use the shared engine, read your companion INI and call
`host->register_ini(host->log_context, source, length)` synchronously inside
`bts_mod_init`. It validates and copies at most 1 MiB of UTF-8 immediately.
Each mod can register one pack. Registration fails for invalid, disabled, or
incompatible packs, repeat calls, and calls after initialization. The host
commits the registration only if `bts_mod_init` succeeds; a failed initializer's
rules never become active. The bundled
[BlockTheSpot implementation](../Mods/BlockTheSpot/blockthespot.cpp) uses this
same API. DLLs can also implement their own behavior without registering a pack.

Reserved companion fields are `[Mod] Enable`, `[Mod] Type`, and
`[Compatibility] Spotify`. Other settings belong to the DLL. A DLL's companion
INI is not parsed as a declarative pack unless the DLL explicitly submits it.

DLLs without `bts_mod_init` are reported as DllMain-only modules. Loaded modules
remain resident even after initialization failure because they may have created
threads or hooks. This is not crash isolation. Dependencies resolve from the mod
folder, application folder, and Windows system directory. Put helper DLLs in a
subfolder if they should not load as independent mods.

Build the [hello example](../examples/mods/hello.cpp) from an x64 Visual Studio
prompt, then put `hello.dll` and optional `hello.ini` in `patches`. It only logs a
message, and demonstrates that a mod does not need BlockTheSpot enabled.
