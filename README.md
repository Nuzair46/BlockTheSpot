# BlockTheSpot

[![Build and test](https://github.com/Nuzair46/BlockTheSpot/actions/workflows/ci.yml/badge.svg?branch=experimental)](https://github.com/Nuzair46/BlockTheSpot/actions/workflows/ci.yml) [![Discord](https://discord.com/api/guilds/807273906872123412/widget.png)](https://discord.gg/eYudMwgYtY)

This is the **experimental mod-loader branch** for the standard Windows x64
[Spotify desktop app](https://www.spotify.com/download/windows/). The Microsoft
Store app is unsupported. The current build targets **Spotify 1.3.1.234**.

BlockTheSpot itself is a bundled mod: `patches/blockthespot.dll` with
`patches/blockthespot.ini`. It blocks ad-related requests, patches the frontend,
enables the developer menu, and adds GitHub and Discord links to **Help > About
Spotify**. Other DLL and INI mods use the same loader and patch engine.

## Install or update the stable release (recommended)

Use the [BlockTheSpot Installer](https://github.com/Nuzair46/BlockTheSpot-Installer).

1. Download [BlockTheSpotInstaller.exe](https://github.com/Nuzair46/BlockTheSpot-Installer/releases/latest/download/BlockTheSpotInstaller.exe).
2. Run it and use the recommended Spotify version.
3. Click **Install / Patch**.

The installer handles the compatible desktop client, patch updates, and repairs.
It preserves the **stable release's** `settings.ini`, including across Spotify
reinstalls. **Reset BlockTheSpot settings to defaults** is optional.

The stable installer does not install this experimental branch or its new mod
layout. Use the instructions below to try the mod loader.

## Install the experimental build

Download `windows-experimental` from a successful
[experimental CI run](https://github.com/Nuzair46/BlockTheSpot/actions/workflows/ci.yml?query=branch%3Aexperimental),
or build this branch. Install the compatible Spotify desktop client first.
Close Spotify completely before changing files.

1. On a fresh installation, rename Spotify's original `chrome_elf.dll` to
   `chrome_elf_required.dll`.
2. Copy `chrome_elf.dll`, `bts-loader.dll`, and `config.ini` from the package to
   `%APPDATA%\Spotify`.
3. Copy the package's `patches` contents into `%APPDATA%\Spotify\patches`, keeping
   other mods already there. The bundled `blockthespot.dll` and `blockthespot.ini`
   must stay together in that folder.
4. Start Spotify and check `blockthespot-status.txt`.

The resulting layout is:

```text
Spotify/
  Spotify.exe
  chrome_elf.dll              proxy
  chrome_elf_required.dll     matching original Spotify DLL
  bts-loader.dll             host and shared patch engine
  config.ini                 loader settings and CEF compatibility
  patches/
    blockthespot.dll          bundled adblocking mod
    blockthespot.ini          its signatures, URL rules, and preferences
    othermod.dll             optional additional mod
    othermod.ini             optional settings for othermod.dll
    custom-ui.ini            optional standalone declarative mod
```

All mods are enabled automatically when present. A DLL's optional INI must have
exactly the same filename stem, with case ignored. No `settings.ini` entries are
required. See the [mod guide](docs/Mods.md) for formats and examples.

### Updating and migrating preferences

Keep the proxy, host, root config, and bundled mod from the same build. Before
replacing a mod's INI, back it up and carry your feature switches into the new
file; retain the new signatures and compatibility fields. Preserve other mods
when copying the package. Mods and preferences are read once at startup.

Older builds put adblocking in the root `blockthespot.dll` and signatures in
`config.ini`. Replace the root proxy with the new build, add `bts-loader.dll`,
and remove the old **root** `blockthespot.dll`. Move your `Developer`, `URL_block`,
`Buffer_modify`, and `Homepage_vbar` Enable values from the old `settings.ini`
into `patches/blockthespot.ini`. Logging and `LIBCEF/Block_crashpad` now belong
in root `config.ini`. This branch does not read `settings.ini`; keep it as a
backup if you plan to return to stable.

When updating only the patch, retain `chrome_elf_required.dll`. After **Spotify
itself updates**, that original must come from the new Spotify build. If Spotify
restored its stock `chrome_elf.dll`, use it to replace `chrome_elf_required.dll`
before installing the proxy again. If you cannot identify the original, repair
Spotify first; do not rename the patch proxy as the original.

To return to stable, remove `bts-loader.dll` and the bundled pair in `patches`,
then run the standard installer. Keep any other mods for later; stable does not
load them.

### Manual stable installation (optional)

Download `chrome_elf.dll`, `blockthespot.dll`, and `config.ini` from one
[stable release](https://github.com/Nuzair46/BlockTheSpot/releases). With Spotify
closed, preserve its matching original as `chrome_elf_required.dll`, then copy
those three release files beside `Spotify.exe`. Stable uses the root
`blockthespot.dll` and optional `settings.ini`; follow the documentation from that
release when setting preferences.

## Preferences and troubleshooting

Edit each mod's own INI. For example, `[Homepage_vbar] Enable=1` in
`patches/blockthespot.ini` enables the optional homepage CSS change. Set
`[Mod] Enable=0` there to disable the entire mod, or remove its DLL/INI pair.
Set `[Mods] Enable=0` in root `config.ini` to disable **all** mods, including
BlockTheSpot. Logging and CEF offsets also live in root `config.ini`.

`blockthespot-status.txt` reports each mod and patch target as pending, ready,
active, applied, skipped, or failed. A pending frontend file has not been read
yet; visit its view. A failed patch group contributes no writes, while unrelated
mods may still apply. Partial ZIP reads remain unchanged. Unsupported Spotify
versions skip all mods and version-sensitive hooks.

`blockthespot.log` records errors at `[Log] Level=0`. Levels `1` and `2` add status
and debug messages; URLs are redacted. Logs rotate at about 1 MiB. If the report
is missing, check that the proxy and host come from the same build and the
original DLL matches Spotify.

If Spotify cannot start, close its processes, remove the proxy `chrome_elf.dll`,
and restore the matching `chrome_elf_required.dll` as `chrome_elf.dll`. Repair
Spotify if the original is missing or from another build.

## Uninstall

For stable, use **Uninstall / Restore** in the installer.

For this experimental build, close Spotify, remove `chrome_elf.dll`,
`bts-loader.dll`, and `config.ini`, then rename the matching original
`chrome_elf_required.dll` to `chrome_elf.dll`. Remove the bundled DLL/INI from
`patches`; keep other mods and their preferences if needed. Logs and the status
report can also be removed.

## Build and contribute

Install Visual Studio 2022 / Build Tools with **Desktop development with C++**,
MSVC v143 x64, and a Windows 10/11 SDK. From PowerShell:

```powershell
.\tools\build.ps1
python -m unittest discover -s tests -p 'test_*.py'
.\tools\package.ps1
```

The build includes the proxy, host, bundled mod, offline validator, and native
regression tests. DLLs are under `out/x64/Release`, with the mod in its `patches`
subfolder. Tools are under `out/tools`. Packaging creates a new `out/experimental`
folder with the installation layout; choose a new `-Destination` when packaging
again. Use `-Configuration Debug` or `-SdkRoot` for local development.

Linux/macOS contributors can run `python3 tools/test.py --sanitize`, followed by
the Python tests. Python 3 and Node.js are required for synthetic JavaScript
validation. CI runs Linux sanitizers and the Windows build/integration tests.

See the [mod guide](docs/Mods.md), [C API](include/blockthespot_mod.h), and
[signature guide](docs/UpdateSignature-advanced.md). With the bundled Developer
patch enabled, Spotify's **Develop > Show debug window** opens its developer UI.

## Support

[Discord](https://discord.gg/eYudMwgYtY). Unsigned DLLs can trigger antivirus
false positives; source is available if you prefer to build your own binaries.
