[CmdletBinding()]
param([string]$SpotifyDir = (Join-Path $env:APPDATA 'Spotify'))
$ErrorActionPreference = 'Stop'
$issues = [System.Collections.Generic.List[string]]::new()
function Read-Exports([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $pe = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($pe -lt 0 -or $pe + 264 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550) { throw "Invalid PE file: $Path" }
    if ([BitConverter]::ToUInt16($bytes, $pe + 4) -ne 0x8664) { throw "Expected x64 file: $Path" }
    $count = [BitConverter]::ToUInt16($bytes, $pe + 6)
    $optional = $pe + 24
    $sectionStart = $optional + [BitConverter]::ToUInt16($bytes, $pe + 20)
    $sections = @()
    for ($i = 0; $i -lt $count; $i++) {
        $offset = $sectionStart + 40 * $i
        $sections += [PSCustomObject]@{ RVA=[BitConverter]::ToUInt32($bytes,$offset+12); Size=[BitConverter]::ToUInt32($bytes,$offset+16); Raw=[BitConverter]::ToUInt32($bytes,$offset+20) }
    }
    function Offset([uint32]$Rva) {
        foreach ($s in $sections) {
            if ($Rva -ge $s.RVA -and $Rva - $s.RVA -lt $s.Size) { return [int]($s.Raw + $Rva - $s.RVA) }
        }
        throw 'PE address is outside its file sections'
    }
    function CString([uint32]$Rva) {
        $start = Offset $Rva
        $end = $start
        while ($end -lt $bytes.Length -and $bytes[$end]) { $end++ }
        if ($end -eq $bytes.Length) { throw 'Unterminated export name' }
        return [Text.Encoding]::ASCII.GetString($bytes,$start,$end-$start)
    }
    $exports = @{}
    $exportRva = [BitConverter]::ToUInt32($bytes,$optional+112)
    $exportSize = [BitConverter]::ToUInt32($bytes,$optional+116)
    if (!$exportRva) { return $exports }
    $table = Offset $exportRva
    $namesCount = [BitConverter]::ToUInt32($bytes,$table+24)
    $functions = Offset ([BitConverter]::ToUInt32($bytes,$table+28))
    $names = Offset ([BitConverter]::ToUInt32($bytes,$table+32))
    $ordinals = Offset ([BitConverter]::ToUInt32($bytes,$table+36))
    for ($i=0; $i -lt $namesCount; $i++) {
        $name = CString ([BitConverter]::ToUInt32($bytes,$names+4*$i))
        $ordinal = [BitConverter]::ToUInt16($bytes,$ordinals+2*$i)
        $address = [BitConverter]::ToUInt32($bytes,$functions+4*$ordinal)
        $forward = ''
        if ($address -ge $exportRva -and $address - $exportRva -lt $exportSize) { $forward = CString $address }
        $exports[$name] = $forward
    }
    return $exports
}
foreach ($name in @('Spotify.exe','Spotify.dll','libcef.dll','chrome_elf.dll','chrome_elf_required.dll','blockthespot.dll','config.ini')) {
    if (!(Test-Path (Join-Path $SpotifyDir $name) -PathType Leaf)) { $issues.Add("Missing $name") }
}
if (!$issues.Count) {
    try {
        $spotify = (Get-Item (Join-Path $SpotifyDir 'Spotify.exe')).VersionInfo
        $installed = "$($spotify.FileMajorPart).$($spotify.FileMinorPart).$($spotify.FileBuildPart).$($spotify.FilePrivatePart)"
        $config = Get-Content (Join-Path $SpotifyDir 'config.ini') -Raw
        if ($config -notmatch '(?m)^Spotify=(\d+\.\d+\.\d+\.\d+)\s*$') { $issues.Add('Config has no supported Spotify version') }
        elseif ($Matches[1] -ne $installed) { $issues.Add("Spotify $installed does not match signature pack $($Matches[1])") }
        Write-Output "Spotify: $installed"
        $cef = (Get-Item (Join-Path $SpotifyDir 'libcef.dll')).VersionInfo.FileVersion
        $original = (Get-Item (Join-Path $SpotifyDir 'chrome_elf_required.dll')).VersionInfo.FileVersion
        if ($cef -notmatch 'chromium-(\d+\.\d+\.\d+\.\d+)') { $issues.Add('Cannot determine the expected original Chromium DLL version') }
        elseif ($Matches[1] -ne $original) { $issues.Add("Original chrome_elf version $original does not match Chromium $($Matches[1])") }
        $proxyExports = Read-Exports (Join-Path $SpotifyDir 'chrome_elf.dll')
        $originalExports = Read-Exports (Join-Path $SpotifyDir 'chrome_elf_required.dll')
        $hookExports = Read-Exports (Join-Path $SpotifyDir 'blockthespot.dll')
        if (!$hookExports.ContainsKey('bts_load_anchor')) { $issues.Add('Hook DLL is from an older or incompatible release') }
        if (!$proxyExports.Count) { $issues.Add('Proxy DLL has no exports') }
        foreach ($name in $proxyExports.Keys) {
            if ($proxyExports[$name] -ne "chrome_elf_required.$name") { $issues.Add("Proxy export $name is not a supported forwarder"); break }
            if (!$originalExports.ContainsKey($name)) { $issues.Add("Original DLL is missing required export $name") }
        }
        foreach ($name in $originalExports.Keys) {
            if (!$proxyExports.ContainsKey($name)) { $issues.Add("Proxy is missing original export $name") }
        }
        Write-Output "Preferences: $(if (Test-Path (Join-Path $SpotifyDir 'settings.ini')) {'settings.ini (preserved during updates)'} else {'config defaults'})"
    } catch { $issues.Add($_.Exception.Message) }
}
$status = Join-Path $SpotifyDir 'blockthespot-status.txt'
if (Test-Path $status) { Write-Output "`nLast runtime report (may be from an earlier launch):"; Get-Content $status }
if ($issues.Count) {
    Write-Output "`nInstallation needs attention:"
    $issues | ForEach-Object { Write-Output "- $_" }
    Write-Output 'Restore the matching original chrome_elf.dll from this Spotify build, then reinstall all patch files from one release. Preserve settings.ini.'
    exit 1
}
Write-Output "`nInstallation checks passed. Start Spotify and check blockthespot-status.txt for runtime patch results."
