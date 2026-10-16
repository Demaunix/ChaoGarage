# Installing, updating and removing ChaoGarage

ChaoGarage is two files placed next to the game's executable. There is no installer and nothing runs in the background.

## Install

1. Download `ChaoGarage-<version>-win32.zip` from the [latest release](https://github.com/Demaunix/ChaoGarage/releases/latest). Optionally compare its SHA-256 with `SHA256SUMS.txt` from the same release (PowerShell: `Get-FileHash .\ChaoGarage-1.1.0-win32.zip`).
2. Open the game folder: in Steam, right-click *Sonic & SEGA All-Stars Racing* → **Manage → Browse local files**. It contains `Sonic & SEGA All-Stars Racing.exe`.
3. Copy **both** `ChaoGarage.dll` and `dinput8.dll` from the ZIP into that folder.
4. Create a folder named `mods` there if it does not exist, and put each mod in its own folder inside it:
   ```
   <game folder>\
     Sonic & SEGA All-Stars Racing.exe
     ChaoGarage.dll
     dinput8.dll
     mods\
       Metal Sonic\        mod.ini, plan.json, runtime\..., payloads\...
       Another Mod\        mod.ini, ...
   ```
5. Start the game through Steam.

Install order does not matter (loader first or mods first), as long as both loader files are present when the game starts. With no `mods` folder, or no mod in it, the loader does nothing and the game starts exactly as without it.

## Another `dinput8.dll` is already there

`dinput8.dll` is a common file name for game add-ons. If the game folder already has one that did **not** come from ChaoGarage (or from a mod package that shipped ChaoGarage), it belongs to another tool: back it up before replacing it. Only one `dinput8.dll` can be next to the game. The ChaoGarage `dinput8.dll` passes all DirectInput calls to the Windows system copy, so controllers keep working.

To check which one you have: right-click the file → **Properties → Details**. ChaoGarage 1.1.0 and later say *ChaoGarage bootstrap* with the version. Earlier ChaoGarage builds had no version information.

## Update

Replace **both** files with the ones from the new release. A `ChaoGarage.dll` and a `dinput8.dll` from different releases are refused at start with a message ("from different releases"); the game then starts without mods until both match.

### Coming from a mod that included the loader

Some mod packages, for example *Metal Sonic 1.1.0 Beta 3*, shipped `ChaoGarage.dll` and `dinput8.dll` themselves. Those are earlier ChaoGarage builds. Replace both with the files from this release; the mod folders need no changes (1.1.0 reads the same mod format and verifies, combines and serves mods with unchanged code). Mod packages that follow the current packaging advice no longer include the loader files and link here instead.

## Using the All-Stars Mod Manager

The manager is optional. It can switch mods on and off, check them and apply changes safely, but it does not install or update the loader: copy the two files as above. The manager and its command-line checker recognise each loader release by the SHA-256 of both files.

## Remove

Close the game and delete `ChaoGarage.dll` and `dinput8.dll` from the game folder (or move the `mods` folder elsewhere to keep the loader but disable all mods). The loader never changes the game's own archives (it serves mod content in their place while the game runs) and keeps prepared files only inside the mod's own folder (`mods\<mod>\.cache`). What a mod's own code does is described by that mod. Delete a mod by deleting its folder.

## What is changed on your PC

* Nothing outside the game folder.
* In the game folder: only what you copy there, plus `mods\SsasrMods.log` (the loader's log) and, for some mods, prepared files inside `mods\<mod>\.cache`.
* No registry entries, no services, no network access, no automatic updates.
