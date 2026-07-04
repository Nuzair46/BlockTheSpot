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

This project is for the standard [Spotify desktop app](https://www.spotify.com/download/windows/) only. It does not support the Microsoft Store build.

## Requirements

- Windows 64-bit
- Spotify desktop client installed in `%APPDATA%\Spotify`
- Spotify fully closed before install, update, or uninstall

## Fresh install

- Use [BlockTheSpot-Installer](https://github.com/Nuzair46/BlockTheSpot-Installer/releases) for auto installing. Use the recommended Spotify version.

or

1. Open `%APPDATA%\Spotify`.
2. Rename the original `chrome_elf.dll` to `chrome_elf_required.dll`.
3. Download the latest release assets: `chrome_elf.dll`, `blockthespot.dll`, and `config.ini`.
4. Copy those files into `%APPDATA%\Spotify`.
5. Start Spotify.

## Update after Spotify updates

- Use [BlockTheSpot-Installer](https://github.com/Nuzair46/BlockTheSpot-Installer/releases) for auto installing. Use the recommended Spotify version. Some times latest version may not be supported.

1. Close Spotify completely.
2. Open `%APPDATA%\Spotify`.
3. Remove the custom `chrome_elf.dll`, `blockthespot.dll`, and `config.ini`.
4. If Spotify restored its stock `chrome_elf.dll`, rename it to `chrome_elf_required.dll`.
5. Download the latest release assets again.
6. Copy `chrome_elf.dll`, `blockthespot.dll`, and `config.ini` into `%APPDATA%\Spotify`.
7. Start Spotify.

## Uninstall

1. Close Spotify.
2. Remove `chrome_elf.dll`, `blockthespot.dll`, and `config.ini` from `%APPDATA%\Spotify`.
3. Rename `chrome_elf_required.dll` back to `chrome_elf.dll`.
4. Start Spotify again, or reinstall Spotify if you want a completely clean state.

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
