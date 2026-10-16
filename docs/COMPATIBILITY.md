# Compatibility: what is checked, by whom, and when

This page is exact about what ChaoGarage 1.1.0 checks at game start, what the All-Stars Mod Manager and its checker (`AllStarsModCheck`) check before that, and what nobody can check. The mod description format (`allstars-mod.json`, format `allstars-mod/1`) is specified in the manager's `docs/PACKAGE-FORMAT.md`; the loader reads the same file with the same JSON rules.

## 1. The two loader files are one release

`ChaoGarage.dll` and `dinput8.dll` are built from the same source and carry the same release number (file properties → Details). At start:

| `dinput8.dll` | `ChaoGarage.dll` | Result |
|---|---|---|
| 1.1.0 | 1.1.0 | normal start |
| 1.1.0 | before 1.1.0 (no release information) | not started; message from `dinput8.dll`: *from different releases* |
| 1.1.0 | another 1.x release | not started; message names both releases |
| 1.1.0 | missing / not loadable / not a loader | not started; message says which |
| before 1.1.0 | 1.1.0 | not started; message from `ChaoGarage.dll`: *dinput8.dll is from an earlier release* |
| before 1.1.0 | before 1.1.0 | the old pair, unchanged: no new checks or messages |

Why the pair is strict: the two files pass each other a start-up context whose version changed in 1.1.0 (bootstrap interface 2 adds the release name). A 1.1.0 `dinput8.dll` never calls a host older than 1.1.0 (such a host would reject the new context without telling anyone), and a 1.1.0 host refuses the old context with a message instead of guessing. The checks happen only when there are mods to load; with no mods the game starts exactly as without the loader.

## 2. Checks before anything starts

When the pair matches and the game executable is the supported Steam build, `ChaoGarage.dll` checks **all** mods first. Only if everything passes does it prepare the mods' files, load their DLLs, redirect the game's file functions and start the mods. If anything fails, nothing of that is done for any mod, and one message lists the problems found in this step (up to six in the message; the log lists all).

| Check | Applies to | Outcome when it fails | Message says |
|---|---|---|---|
| `mod.ini` is not a link, says `schema=1` and has an `id` (a folder without `mod.ini` is not a mod and is skipped) | every mod | refused | newer format → *needs a newer loader*; otherwise *files missing or do not match* |
| unique mod IDs | every mod | refused | *cannot be used together* |
| `allstars-mod.json` absent (Windows reports "not found") | legacy-format mods | nothing to check | — |
| `allstars-mod.json` is a folder or a link, or cannot be inspected or opened (for example held open by another program) | mods with such a name | refused — it may hold a requirement | *cannot use* |
| `allstars-mod.json` present but larger than 4 MB, not UTF-8, outside the loader's JSON subset, or not one object | mods with a description | refused | *description file the loader cannot use* (never "update") |
| `manifest_schema` other than `allstars-mod/1[.n]` | mods with a description | refused | major 2+ → *needs a newer loader*; anything else → *cannot use* |
| `must_understand` names a feature this release does not know | mods with a description | refused | *needs a newer loader* |
| a description naming another mod's `id` | mods with a description | refused if it carries `loader` or `load_order` rules, otherwise ignored (logged) | *cannot use* |
| `loader.host_abi`, `mod_ini_schema`, `plan_schema` | mods that declare them | refused unless equal to 1 | higher → *needs a newer loader*; lower → *made for a different loader* |
| `loader.capabilities` | mods that declare them | refused if this release lacks one | *needs a newer loader* (names the missing ones) |
| `loader.host_sha256` / `proxy_sha256` | mods that pin exact loader files | refused unless the installed file's SHA-256 is listed | *made for a different loader build* (points to the mod's page) |
| `load_order.after` / `before` | mods that declare them, against installed mods | refused if the real order (folder names, `metal-sonic` first) breaks it | *wrong order*, with the rename that fixes it |
| a value of the wrong type in `loader` or `load_order` (including `null`) | mods that declare them | refused — never read as "no requirement" | *cannot use* |
| `target.executable_sha256` | mods that declare it | logged only (descriptive) | — |

The capability names this release provides are listed by `ChaoGarageGetLoaderInfo` and in the table in [MOD-AUTHORS.md](MOD-AUTHORS.md#capabilities-provided-by-chaogarage-110).

## 3. Checks while starting (unchanged from 1.0, now with a clear message)

After the checks above, the loader verifies each mod's files exactly as before: plan and runtime SHA-256 pins, payload hashes, the original game files a mod was made for, combining changes to the same game archive. Then it loads each mod's DLL, redirects the game's file functions and calls each mod's `SsasrModInitialize` in start order. A failure here stops at the first problem; the message names the mod and the kind:

* *files missing or do not match* — reinstall the mod; restore modified game files. Updating the loader does not fix this.
* *cannot be used together* — two mods replace the same file or the same archive member.
* *code file Windows could not load* — with the Windows error (for example a 64-bit DLL).
* *reported an error while starting* — the mod's own start-up returned an error. This is **not** presented as a version problem.

All mods started so far are rolled back (their `SsasrModRollback` is called, newest first, and the file functions are restored) and the game starts without mods. If a rollback fails, the game start is stopped, after a message, to protect your files.

## 4. Who enforces what

| Rule | Loader 1.1.0 (at start) | Manager (before applying) | Checker (`AllStarsModCheck`) |
|---|---|---|---|
| `mod.ini`, plan, hashes, original game files, combining changes | **yes** | yes | yes |
| loader pair from one release | **yes** | recognises known pairs by hash | recognises known pairs by hash |
| description syntax, `manifest_schema`, `must_understand` | **yes** | yes (blocks) | yes (*could not be confirmed*) |
| `loader` requirements | **yes** | yes | yes |
| `load_order` | **yes** | yes | yes |
| `requires`, `conflicts_with`, exclusive claims, native patch overlap, extra files outside the mod folder | no (not read by the loader) | yes | yes |
| `target` game build | logged only | warns | *could not be confirmed* |
| `allstars-mod.json` that is a folder, a link, or cannot be read | refused | blocks (unreadable description) | *could not be confirmed* |
| two native DLLs working together | nobody can check | note | *insufficient information* unless the authors recorded it |

The manager and checker are separate downloads and never required. A loader from **before 1.1.0** checks only the first row: it never reads `allstars-mod.json`.

## 5. Older and newer, honestly

* **Old mods are not refused for their age.** A mod without `allstars-mod.json`, or one whose declarations this release meets, goes through the same unchanged verification and loading as with 1.0. There is no forced update and no "too old" rule based on dates or version numbers.
* **An already-released loader cannot learn new checks.** A pre-1.1.0 loader starts a new mod without reading its requirements. A new mod can still protect players on such a loader with the [runtime check](../examples/mod-runtime-check/chaogarage_require.h): its DLL checks the loader it runs under and shows one message naming the mod and this download page. (Under a pre-1.1.0 loader that loader also shows its own short "mods could not be enabled" message.)
* **No version is "newest" offline.** The loader compares the files on disk with what the mods declare. Messages say "a newer loader" only when a mod declared something this release does not provide.
* **A matching hash is not a signature.** Hash pins prove a file is the one a mod names, not who made it.

## 6. What 1.1.0 changed for the game

Nothing about how mods are loaded, combined or served: the archive, file, copy and shader-cache code is byte-for-byte the 1.0 code (shader caches with up to 16,384 records). The start order, the start-up point (before the game's own C runtime starts) and the rollback rules are unchanged. New are the checks in sections 1–2, the messages, and `ChaoGarageGetLoaderInfo`.

This has been tested with synthetic stand-in processes and synthetic mods (see [BUILDING.md](BUILDING.md#tests)), not inside the game; see the release notes for what was and was not observed in the game.
