Place Windows x64 `.dll` mods and declarative `.ini` patch packs here, beside the
installed BlockTheSpot DLLs' `patches` directory. Restart Spotify to load changes.

Use simple ASCII filenames such as `my-mod.dll`. Only files directly in this
folder are discovered. Subfolders, links, and files with other extensions are
not loaded. `my-mod.ini` belongs to `my-mod.dll` when both exist; it is not also
applied as an INI patch pack.

See [the mod author guide](../docs/Mods.md) and [examples](../examples/mods).
The older branch's Spotify 1.2.93.667 packs have been retired; current built-in
patches continue to come from the root `config.ini`.
