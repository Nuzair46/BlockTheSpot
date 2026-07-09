# BlockTheSpot Signature Patching Tutorial

This document explains how BlockTheSpot patches Spotify right now. It is a
practical workflow for adding or updating `config.ini` signatures, verifying
that they match, and debugging failures.

## Patch Types

BlockTheSpot has three main patch surfaces:

- Native `Spotify.dll` byte patches.
- JavaScript/CSS buffer patches inside files read from Spotify's CEF zip reader.
- URL blocking and libcef offset configuration.

Native patches are applied after `Spotify.dll` is loaded. Buffer patches are
applied when Spotify reads frontend assets such as `xpui-pip-mini-player.js` or
route bundles.

## Files That Matter

- `Hook/spotify_native_patch.cpp`
- `Hook/spotify_native_patch.h`
- `Hook/cef_zip_reader_hook.cpp`
- `Hook/cef_url_hook.cpp`
- `Hook/pattern.cpp`
- `Hook/memory.cpp`
- `Hook/loader.cpp`
- `Hook/loader.h`
- `Loader/dllmain.cpp`
- `config.ini`

## Build Setup

Install or verify:

- Visual Studio with Desktop development with C++.
- x64 C++ build tools.
- MASM support for `Loader/chrome_dll.asm`.

Use `Debug|x64` while inspecting signatures and `Release|x64` after the
signatures are stable. Do not use `Win32` for this workflow.

Before launching Spotify under the debugger, the Spotify folder should contain:

- `Spotify.exe`
- `chrome_elf.dll` from this repo
- `chrome_elf_required.dll`, which is the original Spotify DLL renamed
- `blockthespot.dll`
- `blockthespot.pdb`
- `config.ini`

The loader uses relative paths such as `./blockthespot.dll`,
`./chrome_elf_required.dll`, and `./config.ini`, so the working directory must
be the Spotify install folder.

## Launching From Visual Studio

Set `Hook` as the startup project.

In project properties, set:

- `Configuration` = `Debug`
- `Platform` = `x64`
- `Configuration Properties -> Debugging -> Command` = full path to
  `Spotify.exe`
- `Configuration Properties -> Debugging -> Working Directory` = Spotify
  install folder
- `Configuration Properties -> Debugging -> Command Arguments` = empty
- `Configuration Properties -> Debugging -> Debugger Type` = `Native Only`

If Visual Studio reports that `blockthespot.dll is not a valid Win32
application`, it is trying to launch the DLL directly. Set `Command` to
`Spotify.exe`.

## Logging

For patch work, use debug logging:

```ini
[Log]
Level=2
```

Useful healthy log lines include:

- `init_log_thread: initialized`
- `Developer: patch applied.`
- `ProductStatePrefetchKeys: patch applied.` when enabled
- `do_hook_cef_url: patch applied.`
- `do_hook_cef_zip_reader: patch applied.`
- `Loader initialized successfully.`

`signature_2 empty, stop processing` is expected for patch sections that only
define `Signature_1`.

`FindPattern failed.` means the configured signature did not match the buffer or
native `.text` section being scanned.

## Runtime Flow

The high-level loader path is:

```text
Loader/dllmain.cpp
  -> queues bts_main()

Hook/loader.cpp
  bts_main()
    -> initializes logging
    -> loads spotify.dll
    -> loads libcef.dll
    -> hook_spotify_native_patches(spotify_dll_handle)
    -> libcef_IAT_hook_GetProcAddress(spotify_dll_handle)
    -> hook_cef_url(libcef_dll_handle)
    -> hook_cef_reader(libcef_dll_handle)
    -> modify_css_init()
```

The native patch path is:

```text
Hook/spotify_native_patch.cpp
  hook_spotify_native_patches()
    -> reads numbered patch section names from [NativePatches]
    -> checks each listed section's Enable value
    -> applies enabled sections

  apply_spotify_native_patch()
    -> reads [section] Signature
    -> parses the signature with ?? wildcards
    -> reads [section] Value
    -> reads [section] Offset
    -> scans Spotify.dll .text with FindPattern()
    -> writes Value at matched_address + Offset
```

The buffer patch path is:

```text
Hook/cef_zip_reader_hook.cpp
  cef_zip_reader_read_file_hook()
    -> observes the file name being read
    -> checks [Buffer_modify] for configured files
    -> checks the matching file section for patch section names
    -> do_patch_buffer()
      -> reads Signature_N / Value_N / Offset_N
      -> scans the in-memory file buffer with FindPattern()
      -> writes Value_N at matched_address + Offset_N
```

## Native Spotify.dll Patches

Native patches are configured through the `[NativePatches]` list and one section
per patch.

Example:

```ini
[NativePatches]
1=Developer
2=ProductStatePrefetchKeys

[Developer]
Enable=1
Signature=85 ?? 75 0B 33 ?? 84 C0 75 07 40 8A F7 EB 05 33 ?? 40 8A F2 40 88 74 24 ?? 40 8A CE E8
Value=EB 07
Offset=8

[ProductStatePrefetchKeys]
Enable=0
Signature=4C 8D 05 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 4E 20 4C 8D 05 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 8A D8 E8 ?? ?? ?? ??
Value=B3 01
Offset=37
```

Rules:

- `[NativePatches]` is required for native patching.
- The list is read from `1` upward and stops at the first missing number.
- Keep the numbering contiguous.
- A listed section with `Enable=0` is skipped.
- A section not listed in `[NativePatches]` is ignored.
- The current hard limit is 64 native patch sections.
- The log prefix is the section name, for example
  `ProductStatePrefetchKeys: patch applied.`

### Native Patch Fields

Each native patch section uses:

```ini
[PatchSectionName]
Enable=1
Signature=...
Value=...
Offset=...
```

`Signature` is the byte pattern to find in `Spotify.dll`'s `.text` section.
Use `??` for wildcard bytes.

`Value` is the replacement byte sequence.

`Offset` is the number of bytes from the start of the matched signature to the
first byte that should be overwritten.

The patcher validates that `Value` fits inside the matched signature range and
then calls `patch_instruction()` in `Hook/memory.cpp`, which temporarily changes
page protection to `PAGE_EXECUTE_READWRITE`, writes the bytes, and restores the
previous page protection.

### Adding A Native Patch

1. Find the target instruction in Ghidra.
2. Decide the smallest safe byte replacement.
3. Build a signature around stable nearby instructions.
4. Wildcard relative call/jump displacements and RIP-relative addresses.
5. Verify the signature matches exactly one intended site.
6. Calculate `Offset` from the beginning of the signature to the patch point.
7. Add the patch section to `config.ini`.
8. Add the section name to `[NativePatches]`.
9. Run Spotify with `Level=2` logging and check for
   `<section>: patch applied.`

Example section:

```ini
[NativePatches]
1=Developer
2=ProductStatePrefetchKeys
3=PatchFoo

[PatchFoo]
Enable=1
Signature=48 8D 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 2E
Value=B0 01 90 90 90
Offset=7
```

### Developer Native Patch

The current `Developer` patch targets `FUN_180084614`.

It changes:

```text
1800849fc  JNZ 180084a05
```

to:

```text
1800849fc  JMP 180084a05
```

The patch skips the false path and forces the same local developer-mode branch
that Spotify normally reaches only when its account/internal conditions allow
it.

Expected log line:

```text
Developer: patch applied.
```

### ProductStatePrefetchKeys Native Patch

`ProductStatePrefetchKeys` targets the ProductState `prefetch_keys` read in
`FUN_180646410`.

The replacement is:

```text
B3 01    ; MOV BL, 1
```

It overwrites the `MOV BL, AL` that stores the `prefetch_keys` result. That
forces the prefetch policy gate true while leaving later local playback and
offline-state checks intact.

The surrounding logic chooses the mode passed to the media/playback object
roughly as:

```text
prefetch_keys false -> mode 0
prefetch_keys true, but missing extra local/offline conditions -> mode 1
prefetch_keys true and local/offline conditions pass -> mode 2
```

Expected log line when enabled:

```text
ProductStatePrefetchKeys: patch applied.
```

## JavaScript And CSS Buffer Patches

Buffer patches are configured in two stages:

1. `[Buffer_modify]` lists frontend files to inspect.
2. Each file section lists patch section names to apply to that file.

Example:

```ini
[Buffer_modify]
Enable=1
1=xpui-pip-mini-player.js
2=1602.js

[xpui-pip-mini-player.js]
1=miniplayer_begin
2=miniplayer_end

[miniplayer_begin]
Signature_1=72 65 74 75 72 6E 28 30 2C ?? 2E 6A 73 78
Value_1=20 6E 75 6C 6C 3B 2F 2A
Offset_1=6
```

Rules:

- `[Buffer_modify] Enable=1` must be set for buffer patching.
- The `[Buffer_modify]` list is read from `1` upward and stops at the first
  missing number.
- Each file section works the same way: `1=patch_name`, `2=patch_name`, and so
  on.
- Each patch section can contain multiple signatures using numbered keys:
  `Signature_1`, `Value_1`, `Offset_1`, then `Signature_2`, `Value_2`,
  `Offset_2`, etc.

### Buffer Patch Fields

`Signature_N` is the byte pattern to find in the in-memory frontend file buffer.

`Value_N` is the replacement byte sequence.

`Offset_N` is the number of bytes from the start of the matched signature to the
first byte that should be overwritten.

The buffer patcher writes directly into the already-loaded file buffer before
Spotify consumes it.

## Updating A Buffer Signature

Use this loop when a JavaScript or CSS signature stops matching.

1. Set `[Log] Level=2`.
2. Build `Debug|x64`.
3. Launch Spotify from Visual Studio with the Spotify folder as working
   directory.
4. Use the dumped frontend files from the Spotify working folder.
5. Search the dumped file for stable semantic text near the target code.
6. Build a new hex signature.
7. Wildcard unstable bytes.
8. Recalculate `Offset_N`.
9. Update only one patch section.
10. Relaunch and confirm `FindPattern failed.` is gone for that section.

Debug builds currently dump useful frontend files such as:

- `dump_xpui-snapshot.js`
- `dump_xpui-pip-mini-player.js`

Use the dumped files instead of searching the Visual Studio Memory window.

### Choosing Stable Anchors

Prefer:

- readable API names
- translation keys
- stable strings
- object field names
- nearby call shapes that survive minifier churn

Avoid:

- hashed CSS class strings
- very short minified identifiers such as `Tk`, `Ve`, `n`, `x_`, `as`
- long signatures full of relative addresses or generated names

### Converting Text To Hex

The dumped JS text is already bytes. Convert those bytes to hex for
`Signature_N`.

Example:

```text
function Tk(e){
```

becomes:

```text
66 75 6E 63 74 69 6F 6E 20 54 6B 28 65 29 7B
```

Wildcard minifier-dependent bytes:

```text
66 75 6E 63 74 69 6F 6E 20 ?? ?? 28 65 29 7B
```

### Recalculating Offsets

`Offset_N` is always relative to the start of the matched signature.

The patching flow is:

1. `FindPattern` locates the start of the match.
2. `Offset_N` moves from that start to the exact patch point.
3. `Value_N` is written there.

Do not reuse a previous offset after changing the signature prefix. If the
start of the signature moved, the offset probably moved too.

## URL Blocking

URL blocking is configured in `[URL_block]`:

```ini
[URL_block]
Enable=1
1=/ads/
2=/ad-logic/
3=/gabo-receiver-service/
4=/desktop-update/
```

The URL hook checks these substrings against request URLs and blocks matching
requests.

## Libcef Offsets

Libcef vtable offsets are configured in `[LIBCEF]`:

```ini
[LIBCEF]
Block_crashpad=1
CEF_REQUEST_GET_URL_OFFSET=48
CEF_ZIP_READER_GET_READ_FILE_OFFSET=112
CEF_ZIP_READER_GET_FILE_NAME_OFFSET=72
```

If CEF hooks stop working after a Spotify/libcef update, verify these offsets
before debugging individual signatures.

## Visual Studio Breakpoint Tips

For buffer patches, a useful breakpoint is the `do_patch_buffer()` call site in
`patch_file()`:

```cpp
do_patch_buffer(file_name, patch_name, buffer, bufferSize);
```

At that point:

- `patch_name` is a local array.
- `file_name` identifies the frontend file.
- `buffer` and `bufferSize` identify the memory range being scanned.

Conditional breakpoint example:

```cpp
file_name != nullptr &&
patch_name != nullptr &&
strcmp(file_name, "xpui-snapshot.js") == 0 &&
strcmp(patch_name, "disable_metric") == 0
```

Use `strcmp(...) == 0` for `const char*` comparisons.

For native patches, break in `apply_spotify_native_patch()` and inspect:

- `section`
- `modify.signature`
- `modify.mask`
- `modify.value`
- `modify.offset`
- `address`

## Troubleshooting

### Build Toolset Missing

If Visual Studio reports that a platform toolset cannot be found, install that
toolset or retarget both projects:

- `Hook/Hook.vcxproj`
- `Loader/Loader.vcxproj`

### Spotify Launches But Hooks Do Not Work

Check:

- `chrome_elf.dll` from this repo is in the Spotify folder.
- The original DLL was renamed to `chrome_elf_required.dll`.
- `blockthespot.dll` is in the Spotify folder.
- `config.ini` is in the Spotify folder.
- Visual Studio is using the Spotify folder as working directory.
- The process is the main Spotify process, not a child process with `--type=`.

### Native Patch Does Not Apply

Check:

- The section is listed in `[NativePatches]`.
- The numbering in `[NativePatches]` is contiguous.
- The section has `Enable=1`.
- `Signature`, `Value`, and `Offset` are present.
- The signature matches the current `Spotify.dll`.
- The patch offset lands on the intended instruction.

### Buffer Patch Does Not Apply

Check:

- `[Buffer_modify] Enable=1`.
- The frontend file is listed in `[Buffer_modify]`.
- The patch section is listed under the file section.
- The patch section has `Signature_N`, `Value_N`, and `Offset_N`.
- The signature matches the dumped frontend file.
- The offset still points to the intended patch point.

### Memory Window Is Hard To Search

Use the dumped frontend files from disk. That is the fastest workflow for JS and
CSS signatures.

## Verification Checklist

Before shipping a signature update:

- Build `Debug|x64`.
- Launch Spotify from Visual Studio.
- Confirm `Loader initialized successfully.`
- Confirm native patch sections log `<section>: patch applied.`
- Confirm CEF URL and zip-reader hooks initialize.
- Confirm dumped frontend files are written when using debug helpers.
- Confirm updated buffer sections no longer log `FindPattern failed.`
- Build `Release|x64`.

For native patches, also verify the signature against the target DLL before
committing the config. A signature that matches multiple locations is not safe
unless each location is intentionally patchable with the same bytes.

## Summary Workflow

For native patches:

1. Locate the target instruction in Ghidra.
2. Build a unique signature.
3. Choose replacement bytes.
4. Calculate `Offset`.
5. Add the section and list it in `[NativePatches]`.
6. Verify logs and behavior.

For frontend buffer patches:

1. Dump the current frontend file.
2. Find stable nearby anchors.
3. Convert the signature to hex.
4. Wildcard unstable bytes.
5. Recalculate `Offset_N`.
6. Update one section at a time.
7. Relaunch and check logs.
