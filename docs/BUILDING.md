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
| `test_archive_cache` | 1.3.2: the check that an original archive is unmodified: hashed in full the first time, once per start however many mods name it, taken from `mods\.chaogarage-cache\verified-archives.txt` at a later start, and hashed in full again for a rewritten, replaced, changed or copied file and for a missing, empty, cut-off, edited or foreign record. `test_archive_cache --measure <archive> <scratch folder>` times the check on a real archive (reads it only). 1.3.3: the change time is part of the identity (a same-size edit or copy with the old dates put back is hashed again); archive payload files (`verified-mod-files.txt`, more than 256 of them, written once at the end of the start); a network path (`\\localhost\<drive>$`) is always hashed; combined-layout records (`verified-composed.txt`: known only for the same build, plan text, group, original, dimensions and result; one record per mod and archive; damaged or foreign records ignored) |
| `test_members` | 1.3.0: the member patch rules on small synthetic tables and scenes (adds, if-absent in either order, changes with expected values, repeated row names and texts, images found by content, blocks, clashes and optional patches giving way, damaged input, make-then-apply byte for byte), and zlib |
| `run_e2e` | the **built** `ChaoGarage.dll` through its real entry point in `fixture_game.exe`, a synthetic stand-in process linked at the game's image base that contains no game code; ~30 cases (healthy and legacy mods, every refusal kind, consolidation into one message, rollback and stop, mismatched bootstrap versions, untrusted text, start order). It runs on a separate desktop and records each real `MessageBoxW` (title and text) before pressing OK. Optionally `-OldHost <a pre-1.1.0 ChaoGarage.dll>` adds what such a host does with new mods |

`build-tests.ps1` also builds `chaogarage-members.exe`, the patch tool for mod authors (see MOD-AUTHORS.md). Its name avoids "patch": Windows asks for administrator rights for 32-bit programs with that word in their name.

**Real members (optional, needs the game and the installed mods; read-only):** `python tests\real\real_members.py --tool out\chaogarage-members.exe --game "<game folder>" --work out\real` makes each installed mod's patch for every archive member two mods share and checks that applying it gives back what the mods ship today: the mod's own member, its ready-made 1.2.0 variant over the other mod, and the same with the other mod's change as a patch too (2026-10-23: all 41 members, 29 byte for byte, 12 with the same content).

The same vector file is used by the All-Stars Mod Manager's tests (`LoaderVectorTests`), so the loader and the checker are tested against the same cases; where they intentionally differ, the case says why.

**Not covered by these tests:** the bootstrap's start-up gate inside the real game process, and anything in the game itself. Those are observed only by starting the game; the release notes say what was observed.
