<center>
  <h1 align="center">BlockTheSpot</h1>
  <h4 align="center">A multi-purpose adblocker and skip-bypass for <strong>Spotify for Windows (64 bit)</strong></h4>
  <h5 align="center">Please support Spotify by purchasing premium</h5>
  <p align="center">
    <a href="https://github.com/Nuzair46/BlockTheSpot/releases/latest"><img src="https://raw.githubusercontent.com/Nuzair46/BlockTheSpot-Installer/main/assets/blockthespot.png" alt="BlockTheSpot" /></a>
  </p>
</center>

[![Build status](https://github.com/Nuzair46/BlockTheSpot/actions/workflows/manual-release.yml/badge.svg?branch=master)](https://github.com/Nuzair46/BlockTheSpot/actions/workflows/manual-release.yml) [![Discord](https://discord.com/api/guilds/807273906872123412/widget.png)](https://discord.gg/eYudMwgYtY) ![Downloads](https://img.shields.io/github/downloads/Nuzair46/BlockTheSpot/total.svg)

## Overview

BlockTheSpot focuses on the Windows desktop client and keeps the patch surface small:

- blocks ad-related requests
- applies signature-based SPA patches through `config.ini`
- enables Spotify's hidden developer menu
- adds BlockTheSpot's GitHub and Discord links to **Help > About Spotify**

This project is for the standard [Spotify desktop app](https://www.spotify.com/download/windows/) only. It does not support the Microsoft Store build.

## Requirements

- Windows 64-bit
- Spotify desktop client installed in `%APPDATA%\Spotify`
- Spotify fully closed before install, update, or uninstall

## Install or update

The bundled signature pack targets **Spotify 1.3.1.234 x64**. Other versions are
reported as unsupported and version-sensitive patches are skipped.

Download the files from one [release](https://github.com/Nuzair46/BlockTheSpot/releases).
Close Spotify completely before changing its DLLs.

1. On a fresh Spotify installation, rename Spotify's original `chrome_elf.dll`
   to `chrome_elf_required.dll`.
2. Copy the release's `chrome_elf.dll`, `blockthespot.dll`, and `config.ini` to
   `%APPDATA%\Spotify` together.
3. Optionally copy `settings.example.ini` to `settings.ini` and edit your
   preferences. Keep your existing `settings.ini` when updating the patch.
4. Run the release's installation check in PowerShell:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\doctor.ps1
   ```

   For another installation location, add `-SpotifyDir 'D:\Apps\Spotify'`.
5. Start Spotify and inspect `blockthespot-status.txt` in its installation folder.

When updating only BlockTheSpot, keep `chrome_elf_required.dll` and replace the
three patch files together. After **Spotify itself updates**, the original DLL
must come from that new Spotify build. If Spotify has restored its stock
`chrome_elf.dll`, use that file to replace `chrome_elf_required.dll` before
installing the proxy again. If you cannot identify the stock DLL, repair or
reinstall Spotify first; do not rename the patch's proxy as the original.

The separate [BlockTheSpot-Installer](https://github.com/Nuzair46/BlockTheSpot-Installer/releases)
can automate installation. Check which patch release and Spotify version it
supports. This repository does not update that installer's behavior.

## Preferences and troubleshooting

`config.ini` is the versioned signature pack. Put personal preferences in
`settings.ini`; missing preferences use the pack's defaults. Supported keys
are listed in [settings.example.ini](settings.example.ini). Restart Spotify
after changing either file. Move existing custom feature flags from `config.ini`
to `settings.ini` before replacing the pack.

`blockthespot-status.txt` reports each feature as pending, ready, active, applied,
skipped, or failed, with an update time and process ID. A pending SPA file has
not been read yet; visit its view. A ready hook has initialized but has not yet
observed a matching request. A failed signature leaves that file's bytes
unchanged. Partial ZIP reads are skipped rather than patched across chunks.

`blockthespot.log` records errors at the default `[Log] Level=0`. Levels `1` and
`2` add status and debug messages. Logs omit request URLs and rotate at about
1 MiB into `blockthespot.log.1`. The health report is written at every log level.
If no new report appears, run `doctor.ps1` and check that all files came from the
same release. The doctor inspects files without loading or modifying the DLLs;
it does not replace runtime verification.

If Spotify cannot start, close its remaining processes, remove the proxy
`chrome_elf.dll`, and restore the matching original `chrome_elf_required.dll` as
`chrome_elf.dll`. Repair Spotify if the original is missing or from another build.

## Uninstall

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
