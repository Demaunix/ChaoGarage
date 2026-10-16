<#
  Dot-sourced by build.ps1 and build-tests.ps1. Pins the toolchain instead of discovering it: Microsoft C/C++ (MSVC)
  14.42.34433, host x64, target x86, and the Windows SDK 10.0.22621.0 headers, libraries and resource compiler.
  INCLUDE, LIB and PATH are set here (no developer prompt, no vcvars), so another installed version cannot be used by accident.
#>
param(
    [string]$MsvcRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.42.34433',
    [string]$SdkRoot = 'C:\Program Files (x86)\Windows Kits\10',
    [string]$SdkVersion = '10.0.22621.0'
)
$ErrorActionPreference = 'Stop'
$script:root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $MsvcRoot 'bin\Hostx64\x86'
$script:cl = Join-Path $bin 'cl.exe'; $script:link = Join-Path $bin 'link.exe'
$script:rc = Join-Path $SdkRoot "bin\$SdkVersion\x64\rc.exe"
foreach ($t in $script:cl, $script:link, $script:rc) { if (-not (Test-Path -LiteralPath $t -PathType Leaf)) { throw "pinned tool not found: $t" } }
$env:INCLUDE = @((Join-Path $MsvcRoot 'include'), (Join-Path $SdkRoot "Include\$SdkVersion\ucrt"), (Join-Path $SdkRoot "Include\$SdkVersion\um"), (Join-Path $SdkRoot "Include\$SdkVersion\shared")) -join ';'
$env:LIB = @((Join-Path $MsvcRoot 'lib\x86'), (Join-Path $SdkRoot "Lib\$SdkVersion\ucrt\x86"), (Join-Path $SdkRoot "Lib\$SdkVersion\um\x86")) -join ';'
$env:LIBPATH = ''
$env:PATH = "$bin;$(Join-Path $MsvcRoot 'bin\Hostx64\x64');$env:SystemRoot\System32"
$env:CL = ''; $env:_CL_ = ''; $env:LINK = ''; $env:_LINK_ = ''
$script:toolchain = [ordered]@{
    cl = (Get-Item $script:cl).VersionInfo.FileVersion; link = (Get-Item $script:link).VersionInfo.FileVersion
    rc = (Get-Item $script:rc).VersionInfo.FileVersion; msvc = Split-Path $MsvcRoot -Leaf; sdk = $SdkVersion; target = 'x86'
}

# Sources are compiled with paths relative to the tree root, so no folder name of the build machine reaches an output file.
function Invoke-Tool([string]$tool, [string[]]$arguments) {
    Push-Location $script:root
    try {
        $output = & $tool @arguments 2>&1
        if ($LASTEXITCODE -ne 0) { $output | ForEach-Object { Write-Host $_ }; throw "$(Split-Path $tool -Leaf) failed ($LASTEXITCODE)" }
    } finally { Pop-Location }
}
