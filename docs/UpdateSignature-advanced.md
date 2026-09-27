# Signature maintenance and native development

The bundled pack targets Spotify **1.3.1.234 for Windows x64**. The compatibility
version is explicit in `[Compatibility] Spotify`; runtime hooks check the
installed executable before using signatures or CEF member offsets.

## Build and test

Use Visual Studio 2022 / Build Tools, MSVC v143, and a Windows 10/11 SDK with
x64 libraries and resource tools. MASM is no longer needed.

```powershell
.\tools\build.ps1
python -m unittest discover -s tests -p 'test_*.py'
```

Both DLLs must be installed together. `out/x64/Release` contains `chrome_elf.dll`,
`blockthespot.dll`, and their PDBs. `out/tools` contains `patch-tool.exe` and test
executables. Use `-Configuration Debug` for native debugging. `-SdkRoot` accepts
a complete SDK tree with matching `Include`, `Lib`, and `bin` version directories.
An incomplete C++ installation produces an actionable error before compilation.

On Linux/macOS, use a C++20 compiler and run:

```sh
python3 tools/test.py --sanitize
python3 -m unittest discover -s tests -p 'test_*.py'
```

Tests use synthetic assets. They cover parser errors, 0xFF values, exact-capacity
lists, buffer boundaries, ambiguous/overlapping matches, transactional failure,
URL path handling, JavaScript syntax failures, and validator/runtime agreement.
Windows tests also exercise callback writes, logging concurrency and rotation,
health reports, and argument forwarding through the actual built proxy DLL.

## Architecture

- `Common/patch.h`: portable parser, unique-match scanner, write planning, and
  transactional application. The DLL and offline tool compile this same code.
- `Common/config.h`: one INI parser and configuration model. Runtime reads the
  pack and optional preferences once, before installing CEF callbacks.
- `Loader/chrome_elf.def`: native Windows export forwarders to the original
  `chrome_elf_required.dll`. No assembly trampoline alters registers or the stack.
  See Microsoft's [EXPORTS format](https://learn.microsoft.com/en-us/cpp/build/reference/exports).
  The project generates the export library separately before linking the proxy.
- `Hook/dllmain.cpp`: queues deferred initialization; no waits or hook teardown
  occur during DLL detach. Installed hooks remain pinned for process lifetime.
- `Hook/loader.cpp`: resolves paths from the patch DLL's directory, verifies
  compatibility and original Chromium DLL version, then initializes hooks.
- `Hook/cef_zip_reader_hook.cpp`: validates CEF member bounds and applies a
  transaction only when CEF returns the complete target file in one read.
- `Hook/log_thread.cpp`: synchronous bounded logging and a per-feature report;
  the legacy filename remains, but there is no logging worker thread.

Spotify 1.3.1 imports `GetProcAddress` through an API-set descriptor. The IAT
helper searches imported function names across descriptors instead of assuming
`kernel32.dll`. Check the hook status before investigating signature failures.

## Pack format and preferences

`config.ini` owns compatibility, signatures, URL rules, file mappings, and CEF
ABI offsets. `settings.ini` overrides only the preferences documented in
`settings.example.ini`; unknown preferences are rejected. Release updates must
preserve `settings.ini`. All keys and section names are case-insensitive.

A signature is whitespace-separated two-digit hex bytes. `??` matches exactly
one byte. Replacement values cannot contain wildcards. `FF` is a valid literal.
Empty patterns, all-wildcard signatures, malformed hex, duplicate keys, negative
offsets, and trailing numeric text are rejected. Offsets are unsigned decimal
byte counts relative to the signature's start. Patterns and values are limited
to 4096 bytes; INI files to 1 MiB.

Numbered lists start at 1, have no gaps, and allow up to 256 entries. A JavaScript
patch section contains `Signature_1`, `Offset_1`, and `Value_1`, with an optional
complete second triple ending in `_2`. Native Developer and Homepage_vbar use
unsuffixed keys. CEF offsets are aligned x64 member offsets, additionally checked
against each object's advertised structure size before access. Changing an
offset requires inspecting the new CEF layout; bounds alone cannot prove ABI
compatibility.

All signatures for one file are matched against its original bytes. Every
signature must match exactly once, every write must fit, and writes must not
overlap. Only after all checks pass are bytes changed. A failed transaction
leaves the buffer unchanged. File length never changes. A split ZIP read is
reported and left unchanged; buffering across reads is not implemented.

URL rules are ASCII path substrings beginning with `/`. Queries and fragments
are excluded from matching, and request URLs are never logged.

## Validate clean installed assets

`Apps/xpui.spa` is a ZIP archive containing the clean JS/CSS. After building the
shared engine, install Python 3 and Node.js and run:

```powershell
python tools/validate_signatures.py "$env:APPDATA\Spotify"
```

On Linux/WSL, first build the Linux engine with `tools/test.py`, then pass the
installation's Linux path. The Python wrapper calls `patch-tool` for parsing,
matching, and applying patches, and `node --check` for each patched JS file.
It validates Developer against `Spotify.dll`'s `.text`, all configured JS files,
and the optional CSS patch even when that option is disabled. It never modifies
the installation. Use `--config` or `--engine` to select alternative inputs.

To extract clean configured JS files for inspection:

```powershell
python tools/validate_signatures.py "$env:APPDATA\Spotify" --dump-dir out/clean-assets
```

Do not commit Spotify assets. Keep dumps under ignored `out/`. Runtime Debug
builds no longer dump scripts into Spotify's installation folder.

## Porting workflow

1. Record the new Spotify executable version and inspect its CEF layout and
   original Chrome ELF exports. Build and install both DLLs with matching
   original `chrome_elf_required.dll`; the doctor checks this relationship.
2. Inspect the clean SPA entries. Update file mappings if component files moved.
3. Anchor signatures on meaningful translation keys, property names, or nearby
   control flow. Wildcard minified bindings and CSS hashes. A wildcard still
   matches one byte, so different identifier lengths require a revised pattern.
4. Calculate the exact write offset and equal-length replacement. Inspect the
   intended native branch or JavaScript behavior, not just the matching text.
5. Update `[Compatibility] Spotify` and the version comment at the top of the
   pack after reviewing the new ABI. Run the offline validator and tests.
6. Back up the installed patch, install both rebuilt DLLs and the pack, and run
   `doctor.ps1`. Start Spotify and check the health report while visiting Home,
   album, and miniplayer views. Validate account-specific behavior manually.

A syntax-valid replacement can still change the wrong behavior. Offline checks
prove matching, bounds, and syntax; live checks establish that the relevant
views and hooks are actually exercised.

## Runtime diagnostics and debugging

All patch-owned paths are relative to the installed patch DLL, independent of
the process working directory. Keep `config.ini`, optional `settings.ini`, both
patch DLLs, and the original `chrome_elf_required.dll` beside `Spotify.exe`.

`blockthespot-status.txt` includes the current update time, process ID, supported
version, initialization state, and a row for each configured file/feature:

| State | Meaning |
| --- | --- |
| pending | Target file has not been read yet |
| ready | Hook installed; waiting for matching activity |
| active | A URL request matched a blocking rule |
| applied | All writes for the file validated and applied |
| skipped | Disabled, unsupported version, or partial ZIP read |
| failed | Configuration, compatibility, hook, or signature error |

Failures remain visible for that launch even if a later read succeeds. A report
from an earlier PID or launch is not proof of current health. The read-only
`doctor.ps1` checks architecture, file presence, versions, and forwarded exports;
it cannot prove that a UI view loaded or a request was blocked.

For extra diagnostics, put this in `settings.ini` and restart:

```ini
[Log]
Level=2
```

Level 0 records errors, 1 adds feature statuses, and 2 adds redacted request
activity. `blockthespot.log` rotates near 1 MiB into `blockthespot.log.1`; failure
to rotate does not permit unbounded growth. No URL strings or query tokens are
written. Restore the normal log level after testing.

For Visual Studio debugging, select x64, build Debug, and set the debugger's
Command to the installed `Spotify.exe` with Native Only debugging. Copy the
matching DLLs and PDBs first. Set breakpoints in `bts_main`, `bts::plan`, or the
ZIP `read_file` callback. Launching `blockthespot.dll` directly is invalid.

## 1.3.1.234 signature notes

The About dialog patch in `xpui-desktop-modals.js` appends HTML links to
this repository and Discord using Spotify's existing credits renderer. Its
two writes preserve the original copyright text, typography, platform labels,
and file length. The renderer parses HTML rather than Markdown, so links use
`<a href="…">` tags. Repeated platform translation prefixes are factored to reserve
space for the credits string, stored on the existing platform Map.

`tools/generate_about_patch.py` is the readable source for the
`[about_blockthespot]` section. It prints the two signatures and padded
replacements; update that source and regenerate the section when porting.
The minified bindings and class names are matched exactly because their values
are used by the replacement. Tests verify the rendered credits, unchanged
platform labels, and rollback when either signature becomes stale.

The config changes are:

- **Developer:** the new branch is `test r14d,r14d; jne +7`. Replacing that
  `jne` with `jmp` selects the existing true assignment. In this build the
  signature starts at RVA `0x8BBED`, with the write at `0x8BBF0` (offset 3).
- **Home ads:** `1602.js` and `home-hpto.js` no longer exist, and the old
  `bannerMode` / `isHptoHidden` selectors are absent from the SPA JavaScript.
  The replacement targets the null-render guard next to
  `data-testid:"home-ads-container"`. The obsolete selector patches and file
  mappings have been removed.
- **Leaderboard:** target the null-render guard with `test-ref-div` as a
  semantic anchor; CSS hashes and minified bindings are wildcarded.
- **Miniplayer:** anchor on `web-player.pip-mini-player.upsell.title`, with
  wildcarded CSS hashes and bindings. Replace `return(0,?.jsx)` with
  `return null&&  ` so the render expression short-circuits. This removes the
  need for separate opening and closing comment patches.
- **Album banner:** force the existing null branch next to
  `catalogue-restricted-banner`, also using a single write.
- **Optional homepage CSS:** wildcard the selector hash and replace
  `display:flex` with `display:none` at offset 147. Only a match at byte zero
  is eligible, currently in `2992.css`; the embedded copy in debug-window CSS
  is ignored. This option remains disabled by default.

These are byte patterns, so `??` matches exactly one byte. They tolerate
identifier/hash changes of the same length, not arbitrary changes in minifier
output or component structure. Revalidate on each Spotify update.
