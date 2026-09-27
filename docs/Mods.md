# Experimental mods

The `experimental` branch extends the current BlockTheSpot hooks with a mod
loader. Built-in patches still use `config.ini`. Add your own files to
`%APPDATA%\Spotify\patches` and restart Spotify; the working directory does not
affect discovery. No extra files are required when you have no mods installed.

## Installing and disabling mods

The loader discovers up to 256 DLL/INI files directly inside `patches`. Use ASCII
filenames, at most 128 characters. It ignores subfolders and other extensions,
and rejects links and copies of BlockTheSpot's own DLLs. Mods are discovered in
case-insensitive filename order. INI patches register first; DLL initialization
runs in filename order after BlockTheSpot installs its hooks, in Spotify's main
process only. Mods are not loaded into renderer or crash-handler processes.

Restart Spotify after adding, changing, or removing a mod. Close Spotify before
replacing DLLs. There is no hot reload or unload. Native DLL mods execute with
Spotify's permissions and can crash the client; install DLLs from authors you trust.

Put preferences in the root `settings.ini`:

```ini
[Mods]
Enable=1
hello.dll=0
custom-ui.ini=0
```

`Enable=0` disables all external mods without disabling built-in BlockTheSpot.
Per-file values are `0` or `1`, case insensitive, and override a mod's own
`[Mod] Enable` value. Remove a per-file preference to use its default. A paired
INI is controlled through its DLL's filename. A failed mod is reported and the
remaining mods are still attempted.

`blockthespot-status.txt` contains `Mod <filename>` and per-target rows. `ready`
means an INI registered; its target rows show pending, applied, skipped, or failed.
`loaded` means a DLL loaded and, when present, its initializer succeeded. It does
not prove the DLL's own hooks are working. Logs identify the mod that emitted a
message. Set `[Log] Level=1` to see informational plugin messages.

All external mods are skipped when the host signature pack does not support the
installed Spotify version. INI patch packs additionally require their own exact
`[Compatibility] Spotify` value. A DLL's optional companion INI can declare the
same restriction; omitting it means the DLL accepts any version supported by the
host. This does not infer compatibility from a DLL's filename or version resource.

## INI patch packs

Each standalone INI is independent: patch names are local to that file. It can
contain native patches, frontend patches, or both. See
[`examples/mods/sample.ini`](../examples/mods/sample.ini) for a disabled, synthetic
example; its bytes are not signatures for Spotify.

`[NativePatches]` is a contiguous numbered list of patch section names. Each
section has `Signature`, `Offset`, `Value`, optional `Enable=0|1` (default `1`),
and optional `Module=Spotify.dll` (the default) or `Module=Spotify.exe`. Native
patches search the already-loaded module's `.text` section. Arbitrary module
paths and absolute addresses are not accepted. `Enable=0` on `NativePatches`
skips all native patches in that mod.

`[Buffer_modify]` lists exact SPA filenames. Each file's section lists patch
section names. A patch has `Signature_1`, `Offset_1`, and `Value_1`, with an
optional complete second triple ending in `_2`. This supports JS and exact CSS
filenames. `Enable=0` on `Buffer_modify` skips that part of the mod. Byte grammar
and offsets are described in the [signature guide](UpdateSignature-advanced.md).

All signatures for a target are checked against the same original bytes. The
built-in patch group takes precedence, followed by mods in filename order.
Within one mod and target, all signatures must match exactly once, all writes
must fit, and writes must not overlap. A failed group contributes no writes.
A group that overlaps any earlier accepted group is rejected, even if its
replacement bytes would be identical. Unrelated groups can still apply. This is
a transaction per target, not across multiple modules or SPA files. Partial ZIP
reads are left untouched.

Use the shared offline engine before distributing an INI:

```powershell
.\out\tools\patch-tool.exe inspect-mod .\custom-ui.ini
.\out\tools\patch-tool.exe apply-mod .\custom-ui.ini sample.js .\clean.js .\patched.js
node --check .\patched.js
```

For native targets, pass `Spotify.dll` or `Spotify.exe` and an extracted `.text`
section as the input. Output goes only to the path you specify. Inspecting or
applying a single INI does not detect conflicts with other installed mods; the
runtime reports those in the health file.

## DLL mods

Build a Windows x64 DLL. Loading it invokes `DllMain` normally. For initialization
outside the loader lock, export this optional C function:

```cpp
#include "blockthespot_mod.h"
extern "C" __declspec(dllexport)
int __cdecl bts_mod_init(const BtsModHost* host);
```

[`include/blockthespot_mod.h`](../include/blockthespot_mod.h) defines API version 1.
Check `host->api_version` and `host->size` before reading its fields. It provides
the Spotify directory, patches directory, optional companion INI path, Spotify
version, and a logging callback. The structure and strings remain valid until
process exit. Return nonzero on success. Keep `DllMain` minimal and do not throw
exceptions across the API boundary; see Microsoft's
[DLL initialization guidance](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices).

DLLs without `bts_mod_init` also work: the loader reports them as DllMain-only
modules. A failed load or initializer is reported without stopping later DLLs.
Loaded DLLs remain resident even if their initializer fails, because they may
already have created threads or hooks. This is not a sandbox or crash-isolation
mechanism.

Put optional settings beside the DLL with the same stem, for example `hello.dll`
and `hello.ini`. Reserved sections are `[Mod] Enable` and `[Compatibility] Spotify`;
all other settings belong to the DLL. The loader never treats this companion INI
as a frontend/native patch pack. A DLL can also read its settings using the host
path. Keep helper DLLs outside the discovery folder if they should not load as
separate mods; dependencies resolve from the mod folder, application folder, and
Windows system directory, not the current working directory.

Build the [hello example](../examples/mods/hello.cpp) in an x64 Visual Studio
developer prompt, then copy `hello.dll` and `hello.ini` into `patches`. Its only
action is an informational log message.
