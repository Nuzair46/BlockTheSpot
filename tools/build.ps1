[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$SdkRoot,
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path $vswhere)) { throw 'Install Visual Studio 2022 or Build Tools with Desktop development with C++.' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'The Visual Studio x64 C++ toolchain is missing. Install Microsoft.VisualStudio.Component.VC.Tools.x86.x64.' }
$toolVersion = (Get-Content (Join-Path $vs 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt')).Trim()
$vc = Join-Path $vs "VC\Tools\MSVC\$toolVersion"
if (!$SdkRoot) {
    $keys = @('HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots')
    foreach ($key in $keys) {
        $entry = Get-ItemProperty $key -ErrorAction SilentlyContinue
        if ($entry.KitsRoot10) { $SdkRoot = $entry.KitsRoot10; break }
    }
}
if (!$SdkRoot -or !(Test-Path (Join-Path $SdkRoot 'Include'))) {
    throw 'Windows SDK headers are missing. Install a Windows 10/11 SDK in Visual Studio Installer, or pass -SdkRoot with a complete SDK directory.'
}
$SdkRoot = (Resolve-Path $SdkRoot).Path.TrimEnd('\') + '\'
$sdk = Get-ChildItem (Join-Path $SdkRoot 'Include') -Directory |
    Where-Object { (Test-Path (Join-Path $_.FullName 'um\Windows.h')) -and (Test-Path (Join-Path $SdkRoot "Lib\$($_.Name)\ucrt\x64\ucrt.lib")) } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (!$sdk) { throw 'SDK must contain matching um/shared/ucrt headers and x64 um/ucrt libraries.' }
$version = $sdk.Name
foreach ($part in @("Include\$version\shared\sdkddkver.h", "Lib\$version\um\x64\kernel32.lib",
    "bin\$version\x64\rc.exe", "bin\$version\x86\rc.exe", "bin\$version\x64\mt.exe")) {
    if (!(Test-Path (Join-Path $SdkRoot $part))) { throw "Incomplete Windows SDK: missing $part. Install the Windows SDK through Visual Studio Installer." }
}
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
$buildArgs = @('/m', '/t:Rebuild', "/p:Configuration=$Configuration", '/p:Platform=x64', "/p:SolutionDir=$root\",
    "/p:WindowsSdkDir=$SdkRoot", "/p:UniversalCRTSdkDir=$SdkRoot", "/p:UCRTContentRoot=$SdkRoot",
    "/p:WindowsSdkBinPath=${SdkRoot}bin\", "/p:WindowsSdkVerBinPath=${SdkRoot}bin\$version\", "/p:WindowsTargetPlatformVersion=$version", "/p:UCRTVersion=$version", '/verbosity:minimal', '/nologo')
Write-Host "Building $Configuration x64 with MSVC $toolVersion and SDK $version"
& $msbuild (Join-Path $root 'Loader\Loader.vcxproj') @buildArgs
if ($LASTEXITCODE) { throw 'Native DLL build failed.' }

$out = Join-Path $root 'out\tools'
New-Item -ItemType Directory -Force $out | Out-Null
$previousInclude = $env:INCLUDE
$previousLib = $env:LIB
try {
    $env:INCLUDE = "$vc\include;$SdkRoot\Include\$version\ucrt;$SdkRoot\Include\$version\shared;$SdkRoot\Include\$version\um"
    $env:LIB = "$vc\lib\x64;$SdkRoot\Lib\$version\ucrt\x64;$SdkRoot\Lib\$version\um\x64"
    $compiler = Join-Path $vc 'bin\Hostx64\x64\cl.exe'
    function Compile([string]$Name, [string[]]$Sources, [string[]]$Extra = @()) {
        & $compiler /nologo /std:c++20 /EHsc /W4 /WX /MT /O2 "/Fo$out\" "/Fe$out\$Name.exe" @Sources @Extra
        if ($LASTEXITCODE) { throw "$Name compilation failed." }
    }
    Compile 'patch-tool' @((Join-Path $root 'tools\patch-tool.cpp'))
    if (!$SkipTests) {
        Compile 'core-tests' @((Join-Path $root 'tests\core.cpp'))
        & "$out\core-tests.exe"
        if ($LASTEXITCODE) { throw 'Core tests failed.' }
        Compile 'mod-tests' @((Join-Path $root 'tests\mods.cpp'))
        & "$out\mod-tests.exe"
        if ($LASTEXITCODE) { throw 'Portable mod tests failed.' }
        $mods = Join-Path $out 'mods'
        New-Item -ItemType Directory -Force $mods | Out-Null
        foreach ($entry in @(@('mod-fixture', 'mod_fixture.cpp'), @('plain-mod', 'plain_mod.cpp'), @('cef-fixture', 'cef_fixture.cpp'))) {
            & $compiler /nologo /LD /std:c++20 /EHsc /MT /W4 /WX "/Fo$out\" (Join-Path $root "tests\$($entry[1])") /link "/OUT:$mods\$($entry[0]).dll" "/IMPLIB:$mods\$($entry[0]).lib"
            if ($LASTEXITCODE) { throw 'Mod fixture build failed.' }
        }
        Compile 'mod-loader-tests' @((Join-Path $root 'tests\mod_loader_windows.cpp'),
            (Join-Path $root 'Hook\mod_loader.cpp'), (Join-Path $root 'Hook\log_thread.cpp'),
            (Join-Path $root 'Hook\memory.cpp'), (Join-Path $root 'Hook\pattern.cpp'),
            (Join-Path $root 'Hook\cef_zip_reader_hook.cpp')) @('/link', 'version.lib')
        foreach ($mode in @('enabled', 'disabled', 'unsupported')) {
            & "$out\mod-loader-tests.exe" $mods $mode
            if ($LASTEXITCODE) { throw "Windows mod-loader tests failed ($mode)." }
        }
        Compile 'windows-tests' @((Join-Path $root 'tests\windows.cpp'), (Join-Path $root 'Hook\log_thread.cpp')) @('/link', 'version.lib')
        & "$out\windows-tests.exe" "$out\logs"
        if ($LASTEXITCODE) { throw 'Windows tests failed.' }
        $fixture = Join-Path $out 'forwarding'
        New-Item -ItemType Directory -Force $fixture | Out-Null
        $names = Get-Content (Join-Path $root 'Loader\chrome_elf.def') | Where-Object { $_ -match '=chrome_elf_required\.' } |
            ForEach-Object { ($_.Trim() -split '=')[0] }
        @('LIBRARY chrome_elf_required', 'EXPORTS') + @($names | ForEach-Object { "  $_=bts_test_forward" }) |
            Set-Content (Join-Path $fixture 'original.def') -Encoding ASCII
        & $compiler /nologo /LD /EHsc /MT "/Fo$out\" (Join-Path $root 'tests\forward_original.cpp') /link "/DEF:$fixture\original.def" "/OUT:$fixture\chrome_elf_required.dll" "/IMPLIB:$fixture\chrome_elf_required.lib"
        if ($LASTEXITCODE) { throw 'Forwarding fixture build failed.' }
        Copy-Item "$root\out\x64\$Configuration\chrome_elf.dll", "$root\out\x64\$Configuration\blockthespot.dll" $fixture -Force
        Compile 'forwarding-tests' @((Join-Path $root 'tests\forwarding.cpp'))
        & "$out\forwarding-tests.exe" $fixture
        if ($LASTEXITCODE) { throw 'Export forwarding tests failed.' }
        & "$out\patch-tool.exe" inspect "$root\config.ini"
        if ($LASTEXITCODE) { throw 'Repository config failed validation.' }
    }
} finally {
    $env:INCLUDE = $previousInclude
    $env:LIB = $previousLib
}
Write-Host "Built DLLs: $root\out\x64\$Configuration"
