<#
  Builds the native test programs next to the loader files in <Out> (default <tree>\out), with the same pinned toolchain.
  Run tools\build.ps1 first. Nothing here is part of a release.

  Usage:  pwsh -File tools\build-tests.ps1 [-Out <folder>]
#>
[CmdletBinding()]
param([string]$Out = '')
. (Join-Path $PSScriptRoot 'toolchain.ps1')
if (-not $Out) { $Out = Join-Path $root 'out' }
$Out = [IO.Path]::GetFullPath($Out)
foreach ($f in 'ChaoGarage.dll', 'dinput8.dll') { if (-not (Test-Path (Join-Path $Out $f))) { throw "build the loader first: $f not in $Out" } }

$cc = @('/nologo', '/std:c++17', '/EHsc', '/O2', '/MT', '/GS', '/W4', '/WX', '/Brepro', '/c', '/DUNICODE', '/D_UNICODE', '/DNOMINMAX')
$inc = @('/Isrc\common', '/Isrc\host', '/Isrc\proxy', '/Iexamples\mod-runtime-check')
$obj = Join-Path $Out 'obj\tests'
New-Item -ItemType Directory -Force $obj | Out-Null

function Build-Exe([string]$name, [string[]]$sources, [string[]]$libs, [string[]]$linkExtra = @(), [string[]]$defines = @()) {
    $objs = foreach ($s in $sources) { $o = Join-Path $obj ("$name-" + [IO.Path]::GetFileNameWithoutExtension($s) + '.obj'); Invoke-Tool $cl ($cc + $defines + $inc + @("/Fo$o", $s)); $o }
    Invoke-Tool $link (@('/NOLOGO', '/MACHINE:X86', '/SUBSYSTEM:CONSOLE', '/Brepro', '/INCREMENTAL:NO', "/OUT:$(Join-Path $Out "$name.exe")") + $linkExtra + $objs + $libs)
}
function Build-TestDll([string]$name, [string[]]$sources, [string]$def, [string[]]$libs, [string[]]$defines = @()) {
    $objs = foreach ($s in $sources) { $o = Join-Path $obj ("$name-" + [IO.Path]::GetFileNameWithoutExtension($s) + '.obj'); Invoke-Tool $cl ($cc + $defines + $inc + @("/Fo$o", $s)); $o }
    $defArg = if ($def) { @("/DEF:$def") } else { @() }
    Invoke-Tool $link (@('/NOLOGO', '/DLL', '/MACHINE:X86', '/Brepro', '/INCREMENTAL:NO', "/OUT:$(Join-Path $Out "$name.dll")") + $defArg + $objs + $libs)
}

Build-Exe 'test_compat' @('tests\native\test_compat.cpp') @('kernel32.lib', 'bcrypt.lib')
# test_policy and test_host_pair compile the proxy sources the way the proxy is built (no UNICODE defines)
$ccSaved = $cc; $cc = @('/nologo', '/std:c++17', '/EHsc', '/O2', '/MT', '/GS', '/W4', '/WX', '/Brepro', '/c')
Build-Exe 'test_policy' @('tests\native\test_policy.cpp', 'src\proxy\bootstrap_policy.cpp') @('kernel32.lib', 'bcrypt.lib')
Build-Exe 'test_host_pair' @('tests\native\test_host_pair.cpp', 'src\proxy\bootstrap_policy.cpp') @('kernel32.lib', 'bcrypt.lib')
$cc = $ccSaved
Build-Exe 'test_cleanup' @('tests\native\test_cleanup.cpp') @('kernel32.lib', 'bcrypt.lib')
Build-TestDll 'fixture_runtime' @('tests\native\fixture_runtime.cpp') 'tests\native\fixture_runtime.def' @('kernel32.lib')
Build-TestDll 'fixture_legacy_host' @('tests\native\fixture_legacy_host.cpp') 'tests\native\fixture_legacy_host.def' @('kernel32.lib')
Build-TestDll 'fixture_other_release_host' @('tests\native\fixture_info_host.cpp') 'tests\native\fixture_info_host.def' @('kernel32.lib')
Build-TestDll 'fixture_bad_info_host' @('tests\native\fixture_info_host.cpp') 'tests\native\fixture_info_host.def' @('kernel32.lib') @('/DFIXTURE_BAD_INFO')
Build-TestDll 'fixture_inert_host' @('tests\native\fixture_inert_host.cpp') '' @('kernel32.lib')
Build-TestDll 'example_runtime' @('examples\mod-runtime-check\example_runtime.cpp') 'examples\mod-runtime-check\exports.def' @('kernel32.lib', 'user32.lib')
# the stand-in game process: at the game's fixed image base, so the host's checks of the start-up context hold
Build-Exe 'fixture_game' @('tests\native\fixture_game.cpp') @('kernel32.lib', 'user32.lib') @('/BASE:0x400000', '/FIXED', '/DYNAMICBASE:NO')
Build-Exe 'run_e2e' @('tests\native\run_e2e.cpp') @('kernel32.lib', 'bcrypt.lib', 'user32.lib')
Write-Host "tests built in $Out"
