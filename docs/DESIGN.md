# Design notes

## Start-up sequence

1. **`dinput8.dll` loads with the game.** In `DllMain` it only checks the game's header fields and replaces the game's `GetStartupInfoA` import slot — no allocation, file access, `LoadLibrary`, threads or user interface (Microsoft's DLL guidance). For any other program it is a pure DirectInput pass-through.
2. **The game's C runtime start-up calls `GetStartupInfoA`.** The replacement runs once, on the main thread, before the game's own initialization, and only when the exact expected call site is confirmed. It restores the slot, then:
   1. stays silent if it is not next to the game's `.exe` or there is no mod (`mods\*\mod.ini`);
   2. hashes the game's `.exe` (SHA-256 of the supported Steam build), else warns;
   3. loads `ChaoGarage.dll` from the game folder, else warns;
   4. **pair check:** calls `ChaoGarageGetLoaderInfo`; a host without it (before 1.1.0), from another release, or with unreadable information is never initialized, and one warning says so;
   5. calls `SsasrModsHostInitialize` with the start-up context (bootstrap interface 2).
3. **`ChaoGarage.dll` (host)** validates the context (size, version, flags, gate, thread, "still initializing"); an invalid context is rejected silently. A version-1 context (a pre-1.1.0 `dinput8.dll`) or another release's name gets the pair warning. Then:
   1. **checks every mod** (`mod.ini` basics, `allstars-mod.json`: format, required features, `loader` requirements, `load_order` against the real order, duplicate IDs) — all problems of all mods are collected;
   2. if any: one warning, nothing prepared, loaded or redirected;
   3. otherwise the unchanged 1.0 path: verify and prepare each mod's files, load every runtime DLL and resolve both entry points, redirect the game's file functions (import slots only), call each runtime's `SsasrModInitialize` in start order;
   4. on any failure in 3: roll back started runtimes (newest first) and the file functions, one warning, the game starts without mods; if rollback fails, one warning and the start is stopped (`0x8004A001`; the bootstrap terminates the process).

Every message is shown with `MessageBoxW` on the main thread at step 2 — outside `DllMain` and before the game creates a window. The bootstrap loads `user32.dll` only when it has something to say; its static imports are unchanged (`kernel32`, `bcrypt`).

## Interfaces

| Interface | Between | Version | Change in 1.1.0 |
|---|---|---|---|
| `SsasrBootstrapV1/V2` → `SsasrModsHostInitialize` | `dinput8.dll` → `ChaoGarage.dll` | 2 | V2 = V1 layout + `bootstrap_release`; the 1.1.0 host recognises V1 only to explain the mismatch |
| `ChaoGarageGetLoaderInfo` | `ChaoGarage.dll` → bootstrap and mod runtimes | info 1 | new: release, interfaces, capability names, download page |
| `SsasrHostApiV1` → `SsasrModInitialize` / `SsasrModRollback` | `ChaoGarage.dll` → mod runtimes | 1 | **unchanged** — every existing mod runtime keeps working |
| `mod.ini` schema 1, `plan.json` schema 1 | mods → loader | 1 | unchanged (1.2.0 adds the optional XPAC operation fields `variants` and `optional`; older loaders ignore them) |
| `allstars-mod.json` format 1 | mods → loader and tools | 1 | now also read by the loader (only the rules in COMPATIBILITY.md) |

Why an exact release pair rather than version ranges between the two files: they are always released together, the old host cannot report its version, and the only safe outcome for a mismatch is a clear message. Mods, in contrast, negotiate by **capability names** and exact interface numbers, so a compatible old mod never needs an update.

## Boundaries

* The loader can only check what is on disk. It cannot know the newest release of anything and never contacts the network.
* A loader that is already distributed cannot gain new checks. Mods protect players on older loaders with their own runtime check (`examples/mod-runtime-check`).
* A matching SHA-256 shows a file is the one named; it is not a signature.
* Two native mod DLLs can interfere in ways no declaration describes; nothing here detects that.
* The supported game is the Steam PC executable with SHA-256 `c110b4d3b9729317a93e956f1a49be989c7ad3c2aaba7c54c41319c104381b03` (the original 2 GB build).
