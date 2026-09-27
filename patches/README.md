# Installed mods

This folder ships with `blockthespot.dll` and its companion `blockthespot.ini`.
The DLL is built from `Mods/BlockTheSpot`; the INI holds its rules and feature
switches. The host and its `config.ini` live one directory above this folder.

Add other DLL or standalone INI mods here. Mods load automatically on Spotify's
next launch, with no settings.ini edits. A DLL's optional INI must have the same
stem: `othermod.dll` and `othermod.ini`.

To disable a mod, set `[Mod] Enable=0` in its own INI, or move its files out of
this folder while Spotify is closed. Keep third-party mods when updating the
bundled pair. Carry your feature preferences into updated INIs without retaining
old signature bytes. See [the mod guide](../docs/Mods.md) for the format and API.
