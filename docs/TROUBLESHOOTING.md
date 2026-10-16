# Troubleshooting

When ChaoGarage refuses to start mods it shows **one** message (title *ChaoGarage 1.1.0 - mods were not started*) and the game starts without mods. Press **Ctrl+C** on the message to copy its text. The same text is in `mods\SsasrMods.log` in the game folder. Nothing is downloaded or opened by the loader.

## The messages

| The message says | Cause | What to do |
|---|---|---|
| **ChaoGarage.dll and dinput8.dll are from different releases** | one of the two files was replaced, or a mod ZIP put an old copy of one of them | copy **both** files from the [latest release](https://github.com/Demaunix/ChaoGarage/releases/latest) |
| **There are mods in the mods folder, but ChaoGarage.dll is not next to the game's executable** | `dinput8.dll` is there, `ChaoGarage.dll` is not | copy both files again |
| **ChaoGarage.dll could not be loaded (Windows error …)** | the file is damaged, blocked, or not the 32-bit loader | download and copy both files again; check that antivirus did not quarantine it |
| **This game executable is not the version ChaoGarage supports** | not the Steam PC release, or a changed executable (for example a large-address patch) | restore the original executable (Steam → Properties → Installed Files → Verify integrity). A loader update does not change this |
| **Mod "…" needs a newer ChaoGarage loader** | the mod declared a loader feature, interface or format this release does not have | install the newest ChaoGarage release (both files) |
| **Mod "…" was made for a different ChaoGarage loader build** | the mod accepts only specific loader files, or needs something earlier builds had | look for an updated version of the mod (the message shows its page if the mod names one) |
| **Mod "…" has a description file (allstars-mod.json) that the loader cannot use** | the file is damaged or not in the format the loader reads | reinstall the mod from its page or ask its author. Do not edit or delete the file to get past the message: it may hold a requirement. To play without the mod, move its folder out of `mods` |
| **Mod "…" would start in the wrong order** | the mod must start before/after another installed mod, and the folder names give the other order | rename one of the two mod folders as the message says (mods start in folder-name order: digits, then capital letters, then lowercase letters; `metal-sonic` always first) |
| **Mod "…" has files that are missing or do not match what it expects** | a file of the mod is missing or changed, or a game file it changes is not the original | reinstall the mod; restore the game's files with *Verify integrity*. A loader update does not fix this |
| **Mod "…" cannot be used together with another installed mod** | two mods share an ID, or replace the same file or archive member | keep only one of them |
| **Mod "…" has a code file (DLL) that Windows could not load** | damaged download, missing dependency, or not a 32-bit DLL | reinstall the mod; tell its author the Windows error number |
| **Mod "…" does not export SsasrModInitialize and SsasrModRollback** | the DLL named in `mod.ini` is not a ChaoGarage mod runtime | reinstall the mod; tell its author |
| **Mod "…" reported an error while starting** | the mod's own start-up code failed | not known to be a version problem; send `mods\SsasrMods.log` to the mod's author |
| **Another program has already changed the game's file functions** | another mod loader or tool is active | remove the other tool, or ChaoGarage |
| **Mod "…" could not undo its changes, so the game start was stopped** | after another problem, a mod could not roll back | move that mod's folder out of `mods` and start again; report it to the author |
| **Mod "…" needs a newer ChaoGarage loader** *(title: "… - loader too old")* | shown by a **mod's own check** under a loader older than 1.1.0 | install the newest ChaoGarage release; the old loader then shows its own short message too |

## Nothing happens at all

* No message and no mods: check that both files are in the folder with the game's `.exe`, and that each mod is in its own folder directly inside `mods` with a `mod.ini`. With no mod present the loader stays silent by design.
* Look at `mods\SsasrMods.log`. A successful start ends with `READY: ChaoGarage 1.1.0 ...`.
* The loader does not support folder links (junctions) inside `mods`, and very long folder paths (over 260 characters) can make preparing a mod's files fail; keep the game in a normal Steam library folder.

## Reporting a problem

Open an issue with the copied message, `mods\SsasrMods.log`, the release you use (file properties → Details of both DLLs) and the list of mod folders. Do not attach game files.
