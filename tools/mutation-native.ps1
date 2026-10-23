<#
  Proves the native tests can fail: each mutant copies the tree, makes one deliberate defect, builds the loader and the
  tests, and runs the test program that must catch it. An unmutated copy runs first and must pass. A mutant that does not
  build, or whose test does not fail, makes the script fail.
  Usage:  pwsh -File tools\mutation-native.ps1 -Work <short folder> [-Only N01,N02]
#>
param([Parameter(Mandatory)][string]$Work, [string[]]$Only = @())
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$Work = [IO.Path]::GetFullPath($Work)
$shell = (Get-Process -Id $PID).Path   # the PowerShell running this script (pwsh or Windows PowerShell) builds each copy
$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$mutants = @(
  @{ id = 'N01'; what = 'unknown must_understand feature accepted'; file = 'src\host\compat.h'; find = 'if (!known) refuse(d, Kind::NeedsNewerLoader'; repl = 'if (false) refuse(d, Kind::NeedsNewerLoader'; test = 'vectors' },
  @{ id = 'N02'; what = 'malformed loader number read as no requirement'; file = 'src\host\compat.h'; find = 'else refuse(d, Kind::DescriptionUnusable, std::string("\"loader.") + k + "\" in its description file is not a whole number");'; repl = 'else {}'; test = 'vectors' },
  @{ id = 'N03'; what = 'declared loader requirements not checked at start-up'; file = 'src\host\host.cpp'; find = 'for(auto&c:list)compat::checkLoader(c.desc,self,host,proxy);'; repl = ''; test = 'e2e' },
  @{ id = 'N04'; what = 'a 1.0 dinput8.dll accepted silently'; file = 'src\host\host.cpp'; find = 'if(v1||proxyRelease!=CHAOGARAGE_RELEASE_W)'; repl = 'if(false)'; test = 'e2e' },
  @{ id = 'N05'; what = 'refused mods are prepared and started anyway'; file = 'src\host\host.cpp'; find = 'if(!refused.empty()){Refused r;r.entries=std::move(refused);throw r;}'; repl = 'if(false){}'; test = 'e2e' },
  @{ id = 'N06'; what = 'untrusted text not cleaned in the warning'; file = 'src\host\messages.h'; find = "out += bad ? L'?' : c;"; repl = 'out += bad ? c : c;'; test = 'e2e' },
  @{ id = 'N07'; what = 'the proxy warns again after the host already did'; file = 'src\proxy\bootstrap_policy.cpp'; find = 'if (result!=CHAOGARAGE_HOST_NOT_STARTED_WARNED) environment.Warn'; repl = 'environment.Warn'; test = 'policy' },
  @{ id = 'N08'; what = 'the proxy treats a 1.0 host as a matching partner'; file = 'src\proxy\proxy.cpp'; find = 'if (!get) return ssasr::Pairing::Older;'; repl = 'if (!get) return ssasr::Pairing::Match;'; test = 'pair-older' },
  @{ id = 'N09'; what = 'declared start order not checked'; file = 'src\host\host.cpp'; find = 'auto orderProblems=compat::checkOrder(order);'; repl = 'std::vector<std::pair<size_t,compat::Problem>> orderProblems;'; test = 'e2e' },
  @{ id = 'N10'; what = 'one dialog per problem instead of one per start'; file = 'src\host\host.cpp'; find = '}catch(const Refused&r){redirects.clear();warn(r.entries,false);return NotStartedWarned;}'; repl = '}catch(const Refused&r){redirects.clear();for(auto&e:r.entries)warn({e},false);return NotStartedWarned;}'; test = 'e2e' },
  @{ id = 'N11'; what = 'a failed rollback no longer stops the start'; file = 'src\host\host.cpp'; find = 'if(publishing&&!undo(failed)){log(L"FATAL: rollback failed; game startup must stop.");warn({entry,{Cat::RollbackFailed,failed,L"After the problem above, its changes could not be undone."}},true);return Unrecoverable;}'; repl = 'if(publishing&&!undo(failed)){}'; test = 'e2e' },
  @{ id = 'N12'; what = 'a plain http homepage shown as the mod page'; file = 'src\host\compat.h'; find = 'bool ok = h.compare(0, 8, "https://") == 0 && h.size() <= 200;'; repl = 'bool ok = h.size() <= 200;'; test = 'e2e' },
  @{ id = 'N13'; what = 'an uninspectable description read as absent'; file = 'src\host\compat.h'; find = 'if (error == kFileNotFound || error == kPathNotFound) return Presence::Absent;'; repl = 'if (error == kFileNotFound || error == kPathNotFound || error != 0) return Presence::Absent;'; test = 'vectors' },
  @{ id = 'N14'; what = 'a linked description ignored as absent'; file = 'src\host\compat.h'; find = 'if (attributes & kReparsePoint) { refuse(d, Kind::DescriptionUnusable, "its description file (allstars-mod.json) is a link; the loader reads only a real file inside the mod folder"); return Presence::Refused; }'; repl = 'if (attributes & kReparsePoint) return Presence::Absent;'; test = 'e2e' },
  @{ id = 'N15'; what = 'member variants never used'; file = 'src\host\virtual_xpac.h'; find = 'for(auto&v:c.variants)if(v.over==sha)return &v;return nullptr;'; repl = '(void)c;(void)sha;return nullptr;'; test = 'e2e' },
  @{ id = 'N16'; what = 'a variant used for any content of the other mod'; file = 'src\host\virtual_xpac.h'; find = 'if(v.over==sha)return &v;'; repl = 'if(v.over==sha||!sha.empty())return &v;'; test = 'e2e' },
  @{ id = 'N17'; what = 'a later optional change does not give way'; file = 'src\host\virtual_xpac.h'; find = '}else if(rc.optional){'; repl = '}else if(false){'; test = 'e2e' },
  @{ id = 'N18'; what = 'the optional change wins instead of giving way'; file = 'src\host\virtual_xpac.h'; find = 'keepLeft.insert(k);'; repl = ''; test = 'e2e' },
  @{ id = 'N19'; what = 'a wrongly typed optional flag read as absent'; file = 'src\host\virtual_xpac.h'; find = 'require(f.kind==Json::Bool,"Invalid optional flag (expected true or false)");'; repl = ''; test = 'e2e' },
  @{ id = 'N21'; what = 'a merged right side keeps its variants pointing at old sources'; file = 'src\host\virtual_xpac.h'; find = 'if(v.length)v.source+=sourceShift;'; repl = 'if(v.length){}'; test = 'e2e' },
  @{ id = 'N20'; what = 'two different combined versions guessed between'; file = 'src\host\virtual_xpac.h'; find = 'if(rv&&lv&&rv->sha!=lv->sha)throw'; repl = 'if(false)throw'; test = 'e2e' }
)
foreach ($m in $mutants) {
    $text = [IO.File]::ReadAllText((Join-Path $root $m.file)) -replace "`r`n", "`n"
    if (-not $text.Contains(($m.find -replace "`r`n", "`n"))) { throw "mutant $($m.id): text not found in $($m.file)" }
}
function Copy-Tree([string]$dest) {
    if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
    New-Item -ItemType Directory -Force $dest | Out-Null
    foreach ($d in 'src', 'tests', 'examples', 'tools') { Copy-Item -LiteralPath (Join-Path $root $d) -Destination $dest -Recurse }
}
function Test-Copy([string]$tree, [string]$test) {
    $out = Join-Path $tree 'out'
    $ErrorActionPreference = 'Continue'   # Windows PowerShell turns a child's error output into a terminating error under 'Stop'
    & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $tree 'tools\build.ps1') -Out $out *> $null; if ($LASTEXITCODE) { return 'no-build' }
    & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $tree 'tools\build-tests.ps1') -Out $out *> $null; if ($LASTEXITCODE) { return 'no-build' }
    $vectors = Join-Path $tree 'tests\vectors\loader-requirements.json'
    switch ($test) {
        'vectors' { & (Join-Path $out 'test_compat.exe') $vectors *> $null }
        'policy' { & (Join-Path $out 'test_policy.exe') *> $null }
        'pair-older' { $d = Join-Path $tree 'pair'; New-Item -ItemType Directory -Force $d | Out-Null; Copy-Item (Join-Path $out 'test_host_pair.exe') $d; Copy-Item (Join-Path $out 'fixture_legacy_host.dll') (Join-Path $d 'ChaoGarage.dll'); & (Join-Path $d 'test_host_pair.exe') older *> $null }
        'e2e' { & (Join-Path $out 'run_e2e.exe') $out (Join-Path $tree 'e') $vectors *> $null }
        'all' {
            & (Join-Path $out 'test_compat.exe') $vectors *> $null; if ($LASTEXITCODE) { return 'failed' }
            & (Join-Path $out 'test_policy.exe') *> $null; if ($LASTEXITCODE) { return 'failed' }
            & (Join-Path $out 'run_e2e.exe') $out (Join-Path $tree 'e') $vectors *> $null
        }
    }
    if ($LASTEXITCODE) { 'failed' } else { 'passed' }
}
$results = @()
Copy-Tree (Join-Path $Work 'mc')
$control = Test-Copy (Join-Path $Work 'mc') 'all'
Write-Host "control (unmutated): $control"
if ($control -ne 'passed') { throw "the unmutated copy does not pass ($control); mutant results would mean nothing" }
$results += [ordered]@{ id = 'CONTROL'; outcome = $control }
$bad = 0
foreach ($m in $mutants) {
    if ($Only.Count -and $Only -notcontains $m.id) { continue }
    $tree = Join-Path $Work ('m' + $m.id.Substring(1))
    Copy-Tree $tree
    $path = Join-Path $tree $m.file
    [IO.File]::WriteAllText($path, (([IO.File]::ReadAllText($path) -replace "`r`n", "`n").Replace(($m.find -replace "`r`n", "`n"), $m.repl)))
    $r = Test-Copy $tree $m.test
    $outcome = switch ($r) { 'failed' { 'KILLED' } 'passed' { 'SURVIVED' } default { 'INVALID-BUILD' } }
    if ($outcome -ne 'KILLED') { $bad++ }
    Write-Host ("{0} {1,-52} -> {2}" -f $m.id, $m.what, $outcome)
    $results += [ordered]@{ id = $m.id; what = $m.what; test = $m.test; outcome = $outcome }
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Work 'mutation-native.json') -Encoding utf8
if ($bad) { Write-Host "$bad mutant(s) not killed"; exit 1 }
Write-Host 'all native mutants killed'
