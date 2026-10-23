<#
  Builds ChaoGarage.dll and dinput8.dll (32-bit) from this tree with the pinned toolchain (tools\toolchain.ps1).
  /Brepro makes the output depend only on the inputs: two builds of the same tree, in any folder, give the same bytes.

  Usage:  pwsh -File tools\build.ps1 [-Out <folder>]          (default: <tree>\out)
#>
[CmdletBinding()]
param([string]$Out = '')
. (Join-Path $PSScriptRoot 'toolchain.ps1')
if (-not $Out) { $Out = Join-Path $root 'out' }
$Out = [IO.Path]::GetFullPath($Out)

# The compiler options of the 1.0 builds (host: with the UNICODE/lean defines it was built with; proxy: without), plus /W4 /WX and /Brepro.
$common = @('/nologo', '/std:c++17', '/EHsc', '/O2', '/MT', '/GS', '/W4', '/WX', '/Brepro', '/c')
$hostDefines = @('/DUNICODE', '/D_UNICODE', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX')
$linkCommon = @('/NOLOGO', '/DLL', '/MACHINE:X86', '/DYNAMICBASE', '/NXCOMPAT', '/Brepro', '/INCREMENTAL:NO', '/OPT:REF', '/OPT:ICF')

function Build-Dll([string]$name, [string]$file, [string]$dir, [string[]]$sources, [string[]]$libs, [string[]]$defines, [string[]]$extraObjs = @()) {
    $obj = Join-Path $Out "obj\$name"
    New-Item -ItemType Directory -Force $obj | Out-Null
    $objs = @()
    foreach ($s in $sources) {
        $o = Join-Path $obj ([IO.Path]::GetFileNameWithoutExtension($s) + '.obj')
        Invoke-Tool $cl ($common + $defines + @('/Isrc\common', "/I$dir", "/I$zlibDir", "/Fo$o", $s))
        $objs += $o
    }
    $objs += $extraObjs
    $res = Join-Path $obj "$name.res"
    Invoke-Tool $rc @('/nologo', '/Isrc\common', "/fo$res", "$dir\$name.rc")
    Invoke-Tool $link ($linkCommon + @("/DEF:$dir\exports.def", "/OUT:$(Join-Path $Out $file)") + $objs + @($res) + $libs)
}

New-Item -ItemType Directory -Force $Out | Out-Null
# zlib 1.3.1 (third_party, unchanged source), compiled as C at its own warning level, without the gzip file code (NO_GZIP).
# The objects stay in obj\zlib; build-tests.ps1 links the same ones.
$zlibDir = 'third_party\zlib-1.3.1'
$zlibObj = Join-Path $Out 'obj\zlib'
New-Item -ItemType Directory -Force $zlibObj | Out-Null
$zlibObjs = foreach ($c in 'adler32', 'deflate', 'inffast', 'inflate', 'inftrees', 'trees', 'zutil') {
    $o = Join-Path $zlibObj "$c.obj"
    Invoke-Tool $cl @('/nologo', '/TC', '/O2', '/MT', '/GS', '/W3', '/Brepro', '/c', '/DNO_GZIP', '/D_CRT_SECURE_NO_WARNINGS', "/I$zlibDir", "/Fo$o", "$zlibDir\$c.c")
    $o
}
Build-Dll 'host' 'ChaoGarage.dll' 'src\host' @('src\host\host.cpp') @('kernel32.lib', 'bcrypt.lib', 'user32.lib') $hostDefines $zlibObjs
Build-Dll 'proxy' 'dinput8.dll' 'src\proxy' @('src\proxy\bootstrap_policy.cpp', 'src\proxy\proxy.cpp') @('kernel32.lib', 'bcrypt.lib', '/IGNORE:4222') @()
foreach ($f in 'ChaoGarage.dll', 'dinput8.dll') { Write-Host ("{0}  {1}" -f (Get-FileHash (Join-Path $Out $f) -Algorithm SHA256).Hash.ToLowerInvariant(), $f) }
$toolchain | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Out 'TOOLCHAIN.json') -Encoding utf8
