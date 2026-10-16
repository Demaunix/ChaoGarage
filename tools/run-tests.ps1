<#
  Runs the native tests against the files in <Out> (build.ps1 and build-tests.ps1 first) and writes <Out>\test-results.json.
  No game, no game files and no network are used. The end-to-end cases run in synthetic folders below -Work, on a separate
  desktop created for the run, so the loader's message boxes never appear on screen.

  Usage:  pwsh -File tools\run-tests.ps1 [-Out <folder>] [-Work <short folder>] [-OldHost <a ChaoGarage.dll from before 1.1.0>]
  Keep -Work short (the loader's cache paths must stay under 260 characters).
#>
[CmdletBinding()]
param([string]$Out = '', [string]$Work = '', [string]$OldHost = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Out) { $Out = Join-Path $root 'out' }
if (-not $Work) { $Work = Join-Path $Out 'w' }
$Out = [IO.Path]::GetFullPath($Out); $Work = [IO.Path]::GetFullPath($Work)
$vectors = Join-Path $root 'tests\vectors\loader-requirements.json'
$results = [ordered]@{ host_sha256 = (Get-FileHash (Join-Path $Out 'ChaoGarage.dll')).Hash.ToLowerInvariant(); proxy_sha256 = (Get-FileHash (Join-Path $Out 'dinput8.dll')).Hash.ToLowerInvariant(); runs = @() }
$failed = 0
function Run([string]$name, [string]$exe, [string[]]$arguments) {
    $output = & $exe @arguments 2>&1 | Out-String
    $code = $LASTEXITCODE
    Write-Host ("{0,-28} exit {1}  {2}" -f $name, $code, ($output.Trim() -split "`n" | Select-Object -Last 1))
    if ($code -ne 0) { Write-Host $output; $script:failed++ }
    $script:results.runs += [ordered]@{ name = $name; exit = $code; output = $output.Trim() }
}
Run 'vectors (native reader)' (Join-Path $Out 'test_compat.exe') @($vectors)
Run 'proxy policy and forwarding' (Join-Path $Out 'test_policy.exe') @()
$pairs = Join-Path $Work 'pairs'
if (Test-Path -LiteralPath $pairs) { Remove-Item -LiteralPath $pairs -Recurse -Force }
foreach ($c in @(@('match', 'ChaoGarage.dll'), @('older', 'fixture_legacy_host.dll'), @('other-release', 'fixture_other_release_host.dll'), @('invalid-info', 'fixture_bad_info_host.dll'), @('not-a-loader', 'fixture_inert_host.dll'), @('missing', ''))) {
    $d = Join-Path $pairs $c[0]; New-Item -ItemType Directory -Force $d | Out-Null
    Copy-Item (Join-Path $Out 'test_host_pair.exe') $d
    if ($c[1]) { Copy-Item (Join-Path $Out $c[1]) (Join-Path $d 'ChaoGarage.dll') }
    Run "proxy pairing: $($c[0])" (Join-Path $d 'test_host_pair.exe') @($c[0])
}
$cleanup = Join-Path $Work 'cleanup'
if (Test-Path -LiteralPath $cleanup) { Remove-Item -LiteralPath $cleanup -Recurse -Force }
New-Item -ItemType Directory -Force $cleanup | Out-Null
Run 'legacy cache cleanup' (Join-Path $Out 'test_cleanup.exe') @($cleanup)
$e2e = @($Out, (Join-Path $Work 'e'), $vectors)
if ($OldHost) { $e2e += @('--old-host', [IO.Path]::GetFullPath($OldHost)); $results.old_host_sha256 = (Get-FileHash $OldHost).Hash.ToLowerInvariant() }
Run 'end-to-end (shipped host)' (Join-Path $Out 'run_e2e.exe') $e2e
$results.failed_runs = $failed
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Out 'test-results.json') -Encoding utf8
if ($failed) { Write-Host "$failed test program(s) failed"; exit 1 }
Write-Host 'all native tests passed'
