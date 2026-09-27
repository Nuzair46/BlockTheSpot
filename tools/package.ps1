[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$Destination = 'out/experimental'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$destinationPath = [IO.Path]::GetFullPath((Join-Path $root $Destination))
if (Test-Path $destinationPath) { throw "Package destination must be new: $destinationPath" }
$build = Join-Path $root "out/x64/$Configuration"
New-Item -ItemType Directory -Force (Join-Path $destinationPath 'patches') | Out-Null
foreach ($name in @('chrome_elf.dll', 'bts-loader.dll')) {
    Copy-Item (Join-Path $build $name) $destinationPath
}
Copy-Item (Join-Path $root 'config.ini') $destinationPath
Copy-Item (Join-Path $build 'patches/blockthespot.dll') (Join-Path $destinationPath 'patches')
Copy-Item (Join-Path $root 'patches/blockthespot.ini') (Join-Path $destinationPath 'patches')
Copy-Item (Join-Path $root 'patches/README.md') (Join-Path $destinationPath 'patches')
Copy-Item (Join-Path $root 'out/tools/patch-tool.exe') $destinationPath
foreach ($name in @('include', 'examples', 'docs', 'README.md')) {
    Copy-Item (Join-Path $root $name) $destinationPath -Recurse
}
Write-Host "Packaged loader and bundled mod: $destinationPath"
