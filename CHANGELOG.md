# Changelog

## 1.3.1

**A mod carrying its own copy of the Metal Sonic core now starts before the other mods.** Goes with the GameBanana releases of All-Stars Reconnected (Steam Multiplayer) 1.2.0 and Metal Sonic 1.1.5, which don't require it. Tested in the game with such a mod next to a code-changing mod, and with a usual set of mods.

* Fixed: that core checks all of the game's code when it starts and refuses if a mod before it has changed any of it. A mod carrying it (a file `runtime\MetalSonicMod.dll`) under another ID started in folder-name order, so a code-changing mod with an earlier folder name stopped every mod at start. The start order is now: the mod with ID `metal-sonic`, then a mod carrying the core, then the rest by folder name.
* `load_order` is checked against this order. A declaration that can't be met because of it says so, instead of suggesting a folder rename.
* New capability: `startup-order-metal-core-first`.
* Unchanged: all other mods start in the same order as with 1.3.0, and game files are combined exactly as before.

## 1.3.0

**Mods that add to the same game file keep working when one of them updates, and mods can update themselves.** Made for the GameBanana releases of All-Stars Reconnected (Steam Multiplayer) 1.1.1, whose automatic updates need it, and Metal Sonic 1.1.4. Tested in the game (2026-10-28) with both and Egg Hangar: every mod started with no message, and a staged Steam Multiplayer update was put in place at the next start with the player's settings kept.

* New: a plan operation **`patch`** ships only a mod's own additions and small edits to an archive member, and the loader applies them at start to whatever version of the member is there (the game's original, or another mod's whole member). Two formats: `ssrt-patch/1` (rows and fields of SSRT tables such as `Shopping.dat`) and `scene-patch/1` (pictures, images, texts and blocks of menu scenes). Entries are named by name, key or content, never by position, so a patch keeps fitting when the other mod updates.
* Two mods are refused only when they change the very same entry differently; the message names the entry. `add_if_absent` leaves an entry that is already there; `"optional": true` lets a whole patch give way.
* New: several mods may seed one shader cache.
* Patched members are kept in `mods\.chaogarage-cache` and rebuilt only when something changes; entries not used and not written for a week are removed.
* New: **mods can update themselves.** A mod that downloads its own update stages it in `<game>\mods-updates`; at the next start, before any mod is read, the loader checks the staging (every file's size and SHA-256 against the mod's `.ready` list) and moves it into `mods\`, keeping the replaced folder in `mods-updates\previous`. Made for the Steam Multiplayer mod's automatic updates. Only an installed mod is updated; a staging that doesn't match is removed; one log line per update.
* New capabilities: `xpac-member-patch-ssrt`, `xpac-member-patch-scene`, `rtsc-merge-shared`, `pending-mod-updates`. `ChaoGarage.dll` now contains zlib 1.3.1.
* New tool `chaogarage-members` for mod authors: makes, applies, compares and pins patches with the loader's own code.
* Unchanged: mods without patches are verified, combined and served exactly as by 1.2.0.

## 1.2.0

**Two mods may now change the same archive member, when one of them says how.** Made for the Steam Multiplayer mod (its first GameBanana release needs ChaoGarage 1.2.0), whose footer key pictures live in the same game files Metal Sonic changes. Tested in the game: with this pair, every installed mod started (Steam Multiplayer, Metal Sonic and the other installed mods, with no message), and Steam Multiplayer's key pictures appeared next to Metal Sonic's changes.

* New: a plan operation on an XPAC member may carry **`variants`**: the mod's version of that member made to sit on top of another mod's change of it, named by the SHA-256 of that other mod's payload. When both mods are installed and the other mod's content is exactly that one, the variant is used, so both changes are in the game. Before 1.2.0 such a pair stopped every mod ("cannot be used together").
* New: a plan operation may be marked **`"optional": true`**: if another mod changes the same member and no variant fits, this change gives way (the mod's other changes stay) instead of stopping every mod.
* Each combined member is written to `mods\SsasrMods.log` (which mod's version was used, or which optional change was skipped). Without a fitting variant or an optional change, two mods changing one member are refused as before, and the message now names both mods and the member.
* Variant payloads are checked like every payload (length and SHA-256) before anything starts; a present `optional` or `variants` field of the wrong type is refused, never read as absent.
* New capabilities: `xpac-member-variants`, `xpac-member-optional`. A mod that uses these fields should require them in its `allstars-mod.json`: ChaoGarage 1.1.0 then tells the player to install the newer loader (with the download link) instead of reporting a conflict. Earlier loaders ignore the new fields.
* Unchanged: everything else from 1.1.0 - the checks before start, the messages and their advice (only the technical detail of a member conflict now names the two mods and the member; "needs a newer ChaoGarage loader", "was made for a different loader build", ...), the start order, the mod runtime interface, the pair check. Plans without the new fields are verified, combined and served exactly as by 1.1.0. `plan.json` stays schema 1 (the fields are additions).
* Tests: end-to-end cases that read the combined archive back through the loader's redirection, the 1.1.0 release meeting a mod that needs 1.2.0, and mutants for the new rules.

## 1.1.0

**The loader now explains when something does not fit — once, before anything starts — and is released on its own.**

* New: before any mod is prepared, loaded or started, every mod's `allstars-mod.json` is checked for the loader requirements it declares (`host_abi`, `mod_ini_schema`, `plan_schema`, `capabilities`, exact `host_sha256`/`proxy_sha256` builds), its format (`manifest_schema`, `must_understand`) and its declared start order (`load_order`). A description that is present but cannot be read is never treated as "no requirements".
* New: one message per start when mods are not started, naming each mod, the kind of problem and what to do — "install the newest loader" only when a mod declared something this release does not provide. Damaged files, clashing mods and failing mod start-up get their own advice. Also written to `mods\SsasrMods.log`.
* New: `ChaoGarage.dll` and `dinput8.dll` check that they come from the same release (start-up interface 2). A mismatched or pre-1.1.0 partner is refused with a message instead of mods silently not loading. A missing `ChaoGarage.dll` and an unsupported game executable are now explained too.
* New: `ChaoGarageGetLoaderInfo` lets a mod's runtime check the loader it runs under; `examples/mod-runtime-check` shows how a mod can warn players who still use a loader older than 1.1.0.
* New: version information in both DLLs; reproducible builds (same bytes from the same source in any folder); `SHA256SUMS.txt` and `BUILD-INFO.json` with each release.
* New capabilities: `bootstrap-abi-2`, `loader-info-export`, `startup-requirements-check`, `startup-load-order-check`, `startup-warning-dialog`, `rtsc-merge-16384-records`. No longer provided: `bootstrap-abi-1`.
* Unchanged: how mods are verified, combined and served (archive, file, copy and shader-cache code is byte-identical to 1.0, with shader caches of up to 16,384 records), the start order, the mod runtime interface (`SsasrHostApiV1`), the start-up point and the rollback rules. Mods without `allstars-mod.json` go through exactly the same verification and loading as before.
* Distribution: the loader is a separate download. Mod packages should no longer include `ChaoGarage.dll` or `dinput8.dll`.

## 1.0 (unversioned)

The loader as shipped inside mod packages (for example *Metal Sonic 1.1.0 Beta 3*): virtual archive redirection, mod runtimes started before the game's C runtime, all-or-nothing rollback. These files carried no version information.
