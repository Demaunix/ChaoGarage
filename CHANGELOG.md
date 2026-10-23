# Changelog

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
