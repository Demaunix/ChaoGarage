# Building, testing and verifying

## Toolchain (pinned)

| Tool | Version | Used for |
|---|---|---|
| Microsoft C/C++ compiler and linker (Visual Studio 2022 Build Tools, MSVC) | 14.42.34433 (cl 19.42.34435), host x64, target **x86** | both DLLs and the tests |
| Windows SDK | 10.0.22621.0 (headers, x86 libraries, `rc.exe`) | Windows headers, import libraries, version resources |
| PowerShell | 7.x | the scripts in `tools\` |

`tools\toolchain.ps1` sets `INCLUDE`, `LIB` and `PATH` to exactly these versions (no developer prompt, no `vcvars`); a different installed version is never picked up silently. Paths can be overridden with `-MsvcRoot`, `-SdkRoot`, `-SdkVersion` if yours differ, but then the bytes will differ too.

## Build

```powershell
pwsh -File tools\build.ps1                 # -> out\ChaoGarage.dll, out\dinput8.dll, out\TOOLCHAIN.json
```

Compiler options are those of the 1.0 builds (`/std:c++17 /EHsc /O2 /MT /GS`; the host also with `UNICODE`, `WIN32_LEAN_AND_MEAN`, `NOMINMAX`), plus `/W4 /WX` and `/Brepro`. No debug information or `.pdb` path is written into the DLLs.

**Reproducible:** `/Brepro` replaces time stamps with a hash of the inputs, sources are compiled with paths relative to the tree, and the code uses no anonymous namespaces (MSVC names those after the source file's full path). Building the same tree in another folder gives byte-identical DLLs; `tools\package.ps1` records the source tree's SHA-256 in `BUILD-INFO.json` so anyone can check a release:

```powershell
# from the release's source ZIP, anywhere:
pwsh -File tools\build.ps1 -Out C:\temp\cg
Get-FileHash C:\temp\cg\ChaoGarage.dll, C:\temp\cg\dinput8.dll   # compare with SHA256SUMS.txt
```

## Tests

```powershell
pwsh -File tools\build-tests.ps1
pwsh -File tools\run-tests.ps1 -Work C:\cgw         # keep -Work short (loader cache paths must stay under 260 characters)
pwsh -File tools\mutation-native.ps1 -Work C:\cgm   # optional: proves the tests fail when a safety rule is broken
```

No game, game file or network is used. What runs:

| Program | What it checks |
|---|---|
| `test_compat` | every case of `tests\vectors\loader-requirements.json` through the loader's own description reader, and that the vector file lists exactly the capabilities the host is built with |
| `test_policy` | the bootstrap's decision order with an injected environment (when it stays silent, when it warns, that it warns at most once), the warning texts, the import-slot replacement rules, the exact start-up gate predicate, and that the **built** `dinput8.dll` forwards to the system DirectInput |
| `test_host_pair` | the bootstrap's own code loading a real `ChaoGarage.dll` next to it: the built host (match), a host without release information (older), another release, unreadable information, a DLL without the entry point, and no file |
| `test_cleanup` | removal of obsolete generated archive caches (unchanged 1.0 test) |
| `run_e2e` | the **built** `ChaoGarage.dll` through its real entry point in `fixture_game.exe`, a synthetic stand-in process linked at the game's image base that contains no game code; ~30 cases (healthy and legacy mods, every refusal kind, consolidation into one message, rollback and stop, mismatched bootstrap versions, untrusted text, start order). It runs on a separate desktop and records each real `MessageBoxW` (title and text) before pressing OK. Optionally `-OldHost <a pre-1.1.0 ChaoGarage.dll>` adds what such a host does with new mods |

The same vector file is used by the All-Stars Mod Manager's tests (`LoaderVectorTests`), so the loader and the checker are tested against the same cases; where they intentionally differ, the case says why.

**Not covered by these tests:** the bootstrap's start-up gate inside the real game process, and anything in the game itself. Those are observed only by starting the game; the release notes say what was observed.
