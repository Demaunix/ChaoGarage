<#
  Packages a tested build (tools\build.ps1, build-tests.ps1, run-tests.ps1 first) into <Out>\release\:
      ChaoGarage-<version>-win32.zip    top folder ChaoGarage-<version>\: the two DLLs, README.txt, LICENSE.txt, notices, changelog, BUILD-INFO.json
      ChaoGarage-<version>-source.zip   top folder ChaoGarage-<version>-source\: exactly the files listed in SOURCE-MANIFEST.json
      SHA256SUMS.txt                    SHA-256 of both archives and of both DLLs
  Entry order and time stamps are fixed, so the same inputs give the same archive bytes.
  It refuses to package when the tests did not pass for these exact DLLs, or when a personal path or the current account
  name appears in a DLL or a source file.
  Usage:  pwsh -File tools\package.ps1 [-Out <folder>]
#>
[CmdletBinding()]
param([string]$Out = '')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
$root = Split-Path -Parent $PSScriptRoot
if (-not $Out) { $Out = Join-Path $root 'out' }
$Out = [IO.Path]::GetFullPath($Out)
$utf8 = New-Object Text.UTF8Encoding($false)
function Sha([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function ShaBytes([byte[]]$b) { [BitConverter]::ToString([Security.Cryptography.SHA256]::HashData($b)).Replace('-', '').ToLowerInvariant() }

# ---- version from the single release header ----
$header = [IO.File]::ReadAllText((Join-Path $root 'src\common\chaogarage_release.h'))
$version = [regex]::Match($header, '#define CHAOGARAGE_RELEASE_A "([0-9.]+)"').Groups[1].Value
if (-not $version) { throw 'release version not found in src\common\chaogarage_release.h' }

# ---- the tested DLLs ----
$dlls = 'ChaoGarage.dll', 'dinput8.dll'
foreach ($d in $dlls) { if (-not (Test-Path -LiteralPath (Join-Path $Out $d))) { throw "build first: $d not in $Out" } }
$resultsPath = Join-Path $Out 'test-results.json'
if (-not (Test-Path -LiteralPath $resultsPath)) { throw 'run tools\run-tests.ps1 first (no test-results.json)' }
$results = Get-Content -LiteralPath $resultsPath -Raw | ConvertFrom-Json
if ($results.failed_runs -ne 0) { throw 'the last test run had failures' }
if ($results.host_sha256 -ne (Sha (Join-Path $Out 'ChaoGarage.dll')) -or $results.proxy_sha256 -ne (Sha (Join-Path $Out 'dinput8.dll'))) { throw 'test-results.json is for other DLLs; run the tests again' }

# ---- source manifest: every file of the tree except build output and git data ----
$files = Get-ChildItem -LiteralPath $root -Recurse -File -Force | Where-Object {
    $rel = $_.FullName.Substring($root.Length + 1)
    $rel -notmatch '^(out|\.git)(\\|$)'
}
$entries = foreach ($f in $files) { [pscustomobject]@{ path = $f.FullName.Substring($root.Length + 1).Replace('\', '/'); sha256 = Sha $f.FullName; bytes = $f.Length; full = $f.FullName } }
$list = [System.Collections.Generic.List[object]]::new($entries)
$list.Sort([Comparison[object]]{ param($a, $b) [string]::CompareOrdinal($a.path, $b.path) })
$entries = @($list)
$sourceHash = ShaBytes ($utf8.GetBytes((($entries | ForEach-Object { "$($_.sha256)  $($_.path)" }) -join "`n")))

# ---- privacy scan: no personal path, account name or debug-file path in what is published ----
$patterns = @('[A-Za-z]:\\Users\\', '[A-Za-z]:/Users/')
if ($env:USERNAME -and $env:USERNAME.Length -ge 3) { $patterns += [regex]::Escape($env:USERNAME) }
$scan = @($entries | ForEach-Object { @{ path = $_.full; extra = @() } }) + @($dlls | ForEach-Object { @{ path = (Join-Path $Out $_); extra = @('\.pdb\b', 'RSDS') } })   # a DLL must not name a debug file
foreach ($s in $scan) {
    $bytes = [IO.File]::ReadAllBytes($s.path)
    foreach ($text in [Text.Encoding]::Latin1.GetString($bytes), [Text.Encoding]::Unicode.GetString($bytes)) {
        foreach ($p in $patterns + $s.extra) { if ($text -match $p) { throw "private-looking text ($p) in $($s.path); nothing was packaged" } }
    }
}

# ---- build info ----
$toolchain = Get-Content -LiteralPath (Join-Path $Out 'TOOLCHAIN.json') -Raw | ConvertFrom-Json
$buildInfo = [ordered]@{
    product = 'ChaoGarage'; version = $version
    source_tree_sha256 = $sourceHash; source_file_count = $entries.Count
    source_hash_rule = 'sha256 of the text "<sha256>  <path>" lines (LF-separated, no trailing LF) for the files in SOURCE-MANIFEST.json, ordinal path order'
    toolchain = $toolchain
    files = @($dlls | ForEach-Object { [ordered]@{ name = $_; sha256 = Sha (Join-Path $Out $_); bytes = (Get-Item (Join-Path $Out $_)).Length } })
    tests = @($results.runs | ForEach-Object { [ordered]@{ name = $_.name; exit = $_.exit } })
    in_game = 'not observed by the build or the tests; see the release notes'
}
$buildInfoText = ($buildInfo | ConvertTo-Json -Depth 5) -replace "`r`n", "`n"

$readme = @"
ChaoGarage $version - mod loader for Sonic & SEGA All-Stars Racing (PC, Steam)

Install
  1. Steam: right-click the game > Manage > Browse local files.
  2. Copy BOTH files from this folder next to the game's .exe:
       ChaoGarage.dll
       dinput8.dll
     Replace older copies of both files.
  3. Put mods into the "mods" folder there (mods\<Mod Name>\mod.ini).
  4. Start the game through Steam.

The two files must come from the same release. If something does not fit,
the game starts without mods and one message says what to do (also written
to mods\SsasrMods.log). Nothing is downloaded, installed or run in the background.

Remove: delete the two files.

Help, source code, checksums:
  https://github.com/Demaunix/ChaoGarage
Unofficial fan project; not affiliated with SEGA. MIT license (LICENSE.txt);
the DLLs also contain Microsoft C/C++ runtime code (THIRD-PARTY-NOTICES.md).
"@ -replace "`r`n", "`n" -replace "`n", "`r`n"

# ---- archives ----
$rel = Join-Path $Out 'release'
New-Item -ItemType Directory -Force $rel | Out-Null
$stamp = [DateTimeOffset]::new(2026, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
function Write-Zip([string]$zipPath, [object[]]$items) {   # items: @{ name; bytes }
    if (Test-Path -LiteralPath $zipPath) { [IO.File]::Delete($zipPath) }
    $sorted = [System.Collections.Generic.List[object]]::new($items)
    $sorted.Sort([Comparison[object]]{ param($a, $b) [string]::CompareOrdinal($a.name, $b.name) })
    $fs = [IO.File]::Open($zipPath, [IO.FileMode]::CreateNew)
    try {
        $zip = [IO.Compression.ZipArchive]::new($fs, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($i in $sorted) {
                $e = $zip.CreateEntry($i.name, [IO.Compression.CompressionLevel]::Optimal); $e.LastWriteTime = $stamp
                $s = $e.Open(); try { $s.Write($i.bytes, 0, $i.bytes.Length) } finally { $s.Dispose() }
            }
        } finally { $zip.Dispose() }
    } finally { $fs.Dispose() }
}
$top = "ChaoGarage-$version"
$binItems = @(
    @{ name = "$top/ChaoGarage.dll"; bytes = [IO.File]::ReadAllBytes((Join-Path $Out 'ChaoGarage.dll')) },
    @{ name = "$top/dinput8.dll"; bytes = [IO.File]::ReadAllBytes((Join-Path $Out 'dinput8.dll')) },
    @{ name = "$top/README.txt"; bytes = $utf8.GetBytes($readme) },
    @{ name = "$top/LICENSE.txt"; bytes = [IO.File]::ReadAllBytes((Join-Path $root 'LICENSE')) },
    @{ name = "$top/THIRD-PARTY-NOTICES.md"; bytes = [IO.File]::ReadAllBytes((Join-Path $root 'THIRD-PARTY-NOTICES.md')) },
    @{ name = "$top/CHANGELOG.md"; bytes = [IO.File]::ReadAllBytes((Join-Path $root 'CHANGELOG.md')) },
    @{ name = "$top/BUILD-INFO.json"; bytes = $utf8.GetBytes($buildInfoText) }
)
$binZip = Join-Path $rel "ChaoGarage-$version-win32.zip"
Write-Zip $binZip $binItems
$srcTop = "ChaoGarage-$version-source"
$manifestText = (($entries | Select-Object path, sha256, bytes | ConvertTo-Json -Depth 3) -replace "`r`n", "`n")
$srcItems = @($entries | ForEach-Object { @{ name = "$srcTop/$($_.path)"; bytes = [IO.File]::ReadAllBytes($_.full) } }) + @(@{ name = "$srcTop/SOURCE-MANIFEST.json"; bytes = $utf8.GetBytes($manifestText) })
$srcZip = Join-Path $rel "ChaoGarage-$version-source.zip"
Write-Zip $srcZip $srcItems
$sums = @(
    "$(Sha $binZip)  ChaoGarage-$version-win32.zip",
    "$(Sha $srcZip)  ChaoGarage-$version-source.zip",
    "$(Sha (Join-Path $Out 'ChaoGarage.dll'))  ChaoGarage.dll",
    "$(Sha (Join-Path $Out 'dinput8.dll'))  dinput8.dll"
) -join "`n"
[IO.File]::WriteAllText((Join-Path $rel 'SHA256SUMS.txt'), $sums + "`n", $utf8)
[IO.File]::WriteAllText((Join-Path $rel 'BUILD-INFO.json'), $buildInfoText, $utf8)
[IO.File]::WriteAllText((Join-Path $rel 'SOURCE-MANIFEST.json'), $manifestText, $utf8)
Write-Host "source tree sha256: $sourceHash ($($entries.Count) files)"
Get-Content -LiteralPath (Join-Path $rel 'SHA256SUMS.txt')
