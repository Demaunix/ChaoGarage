# Changelog

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
