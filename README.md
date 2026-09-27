<center>
  <h1 align="center">BlockTheSpot</h1>
  <h4 align="center">A multi-purpose adblocker and skip-bypass for <strong>Spotify for Windows (64 bit)</strong></h4>
  <h5 align="center">Please support Spotify by purchasing premium</h5>
  <p align="center">
    <a href="https://github.com/Nuzair46/BlockTheSpot-Installer/releases/latest"><img src="https://raw.githubusercontent.com/Nuzair46/BlockTheSpot-Installer/main/assets/blockthespot.png" alt="BlockTheSpot" /></a>
  </p>
</center>

[![Build status](https://github.com/Nuzair46/BlockTheSpot/actions/workflows/manual-release.yml/badge.svg?branch=master)](https://github.com/Nuzair46/BlockTheSpot/actions/workflows/manual-release.yml) [![Discord](https://discord.com/api/guilds/807273906872123412/widget.png)](https://discord.gg/eYudMwgYtY) ![Downloads](https://img.shields.io/github/downloads/Nuzair46/BlockTheSpot/total.svg)

## Overview

This is the **experimental mod-loader branch**. It loads DLL mods and INI patch
packs from `patches` beside Spotify. See the [mod guide](docs/Mods.md) for installing
mods, disabling them, and building your own. The standard installer installs the
stable release; use the [experimental CI build](https://github.com/Nuzair46/BlockTheSpot/actions/workflows/ci.yml?query=branch%3Aexperimental)
or build this branch to use the mod loader.

BlockTheSpot focuses on the Windows desktop client and keeps the patch surface small:

- blocks ad-related requests
- applies signature-based SPA patches through `config.ini`
- enables Spotify's hidden developer menu
- adds BlockTheSpot's GitHub and Discord links to **Help > About Spotify**
- loads external DLL and INI mods with per-mod settings and runtime reports

This project is for the standard [Spotify desktop app](https://www.spotify.com/download/windows/) only. It does not support the Microsoft Store build.

## Requirements

- Windows 64-bit
- The installer sets up the Spotify desktop client in `%APPDATA%\Spotify`

## Install or update the stable release (recommended)

Use the [BlockTheSpot Installer](https://github.com/Nuzair46/BlockTheSpot-Installer).

1. Download [BlockTheSpotInstaller.exe](https://github.com/Nuzair46/BlockTheSpot-Installer/releases/latest/download/BlockTheSpotInstaller.exe).
2. Run it and use the recommended Spotify version.
3. Click **Install / Patch**. The installer closes Spotify, installs the compatible
   desktop version when needed, and applies BlockTheSpot.

Run the installer again to update or repair the patch. It preserves your
`settings.ini` preferences, including across Spotify reinstalls. On the first
upgrade, supported preferences from an older `config.ini` move into `settings.ini`.
**Reset BlockTheSpot settings to defaults** is optional and off by default.

The bundled signature pack targets **Spotify 1.3.1.234 x64**. Other versions are
reported as unsupported and version-sensitive patches are skipped.

### Install the experimental mod loader

Download the `windows-experimental` artifact from a successful experimental CI
run, or build this branch with `tools/build.ps1`. Close Spotify and install the
matching DLL pair and `config.ini` using the manual steps below. Keep your
`settings.ini` and existing `patches` directory when updating.

Create `%APPDATA%\Spotify\patches`, then add mods there. DLLs and INIs are loaded
on the next launch. Add `[Mods] Enable=0` to `settings.ini` to disable all external
mods, or remove a problematic mod while Spotify is closed. The
[mod guide](docs/Mods.md) covers individual overrides and development examples.

To return to stable, remove the `[Mods]` section from `settings.ini` and run the
standard installer. Stable builds do not understand experimental mod preferences.

### Manual installation (optional)

Install the compatible Spotify desktop client in `%APPDATA%\Spotify` first.
Download the files from one [release](https://github.com/Nuzair46/BlockTheSpot/releases)
or one experimental build; keep both DLLs and the config together.
Close Spotify completely before changing its DLLs.

1. On a fresh Spotify installation, rename Spotify's original `chrome_elf.dll`
   to `chrome_elf_required.dll`.
2. Copy the release's `chrome_elf.dll`, `blockthespot.dll`, and `config.ini` to
   `%APPDATA%\Spotify` together.
3. Optionally copy `settings.example.ini` to `settings.ini` and edit your
   preferences. Keep your existing `settings.ini` when updating the patch.
4. Start Spotify and inspect `blockthespot-status.txt` in its installation folder.

When updating only BlockTheSpot, keep `chrome_elf_required.dll` and replace the
three patch files together. After **Spotify itself updates**, the original DLL
must come from that new Spotify build. If Spotify has restored its stock
`chrome_elf.dll`, use that file to replace `chrome_elf_required.dll` before
installing the proxy again. If you cannot identify the stock DLL, repair or
reinstall Spotify first; do not rename the patch's proxy as the original.

## Preferences and troubleshooting

`config.ini` is the versioned signature pack. Put personal preferences in
`settings.ini`; missing preferences use the pack's defaults. Supported keys
are listed in [settings.example.ini](settings.example.ini). Restart Spotify
after changing either file. For manual updates, move existing custom feature
flags from `config.ini` to `settings.ini` before replacing the pack.

`blockthespot-status.txt` reports each feature as pending, ready, active, applied,
skipped, or failed, with an update time and process ID. A pending SPA file has
not been read yet; visit its view. A ready hook has initialized but has not yet
observed a matching request. A failed signature contributes no writes from its
patch group; unrelated mod groups may still apply. Partial ZIP reads are skipped
rather than patched across chunks.

`blockthespot.log` records errors at the default `[Log] Level=0`. Levels `1` and
`2` add status and debug messages. Logs omit request URLs and rotate at about
1 MiB into `blockthespot.log.1`. The health report is written at every log level.
If no new report appears, run **Install / Patch** again and check the installer's
activity log. For manual installations, check that both patch DLLs and `config.ini`
came from the same release and that the original DLL matches your Spotify build.

If Spotify cannot start, close its remaining processes, remove the proxy
`chrome_elf.dll`, and restore the matching original `chrome_elf_required.dll` as
`chrome_elf.dll`. Repair Spotify if the original is missing or from another build.

## Uninstall

Choose **Uninstall / Restore** in the installer. It keeps `settings.ini` so your
preferences are available if you reinstall.

To uninstall manually:

1. Close Spotify completely.
2. Remove the patch's `chrome_elf.dll`, `blockthespot.dll`, and `config.ini`.
3. Rename the matching `chrome_elf_required.dll` back to `chrome_elf.dll`.
4. Optionally remove `settings.ini`, `blockthespot-status.txt`, and the two log files.

## Build and contribute

Install Visual Studio 2022 / Build Tools with **Desktop development with C++**,
the MSVC v143 x64 toolchain, and a Windows 10/11 SDK. From PowerShell:

```powershell
.\tools\build.ps1
```

This builds both Release DLLs, debug symbols, the shared signature validator,
and native regression tests. Output is under `out/x64/Release` and `out/tools`.
Use `-Configuration Debug` for debugging or `-SdkRoot` for a complete SDK in a
custom location. No Spotify files are required for the build or regression tests.

With Python 3 and Node.js installed, run the synthetic validator tests:

```powershell
python -m unittest discover -s tests -p 'test_*.py'
```

Linux/macOS contributors can test the portable C++20 engine with
`python3 tools/test.py --sanitize`, then run the same Python tests. Pull requests
run Linux sanitizers and the Windows Release build/tests automatically.

See the [advanced guide](docs/UpdateSignature-advanced.md) for clean-asset
validation, the patch format, runtime diagnostics, and signature maintenance.

## Experimental developer features

1. Open Spotify.
2. Click the two dots in the top-left corner.
3. Go to `Develop > Show debug window`.
4. Toggle experimental options there as needed.

## Defender warning

- Unsigned DLLs can trigger false positives in Windows Defender or other antivirus products.
- The source is fully available on GitHub for inspection.
- If you do not trust prebuilt binaries, build from source and compare the outputs yourself.

## Support

- Discord: https://discord.gg/eYudMwgYtY
