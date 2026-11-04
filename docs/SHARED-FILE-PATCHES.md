# Design note: mods that share a game file keep working when one of them updates

Status: **loader part built on branch `patch-engine-1.3.0`, not released** (2026-10-23). Target: ChaoGarage 1.3.0. Done: both patch engines, the loader applying them at start, the cache, shared shader caches, the `chaogarage-members` tool, tests (rules, end to end, and all 41 real shared members). Not done yet: Mod Manager/checker 1.4.0, moving Egg Hangar and Steam Multiplayer to patches, the in-game test, the release. How to use it: [MOD-AUTHORS.md](MOD-AUTHORS.md#adding-to-a-game-file-another-mod-also-changes-130).

What the build changed from the plan below, found while testing on the real files:
* The spike's in-game question (does the game read a repacked scene?) was already answered: Egg Hangar's 21 tested scenes are exactly zlib 1.3.1 output (Python's `zlib.compress`, level 9). The loader uses the same zlib (copied unchanged from Ghidra's copy, no download) at level 6 (5× faster than level 9 for 1.5% larger scenes).
* The game's own `Shopping.dat` has two rows named `Item19` and two named `Item23`, and the Roster Pack changes one of the `Item23` rows. A field change names the row's `occurrence` when the name is there more than once.
* The game's `navigator_info` scenes list one text (`INFO`) twice; repeated texts stay repeated, and a change sets every copy.
* A scene patch's images are added when at least one of its pictures is kept (all of them, as its author built them; Metal Sonic adds 3 images for 1 picture). This keeps the result independent of folder order.
* Where the plan said "cache only", the cache entry is named after every input including the loader build, checked by SHA-256 at every start, and written as a temporary file published in one step.

## The problem in one paragraph

Today a mod changes a game file inside an archive (`Resource\*.xpac`) by shipping the **whole file**. When two mods change the same file, ChaoGarage 1.2.0 can only combine them in two ways. One mod can ship a ready-made version "on top of" the other mod's **exact** file (`variants`). Or one change can give way (`optional`). So when Metal Sonic changes a shared file in an update, Egg Hangar's ready-made version stops fitting. Egg then loses its course pictures until Egg is rebuilt and re-released. Every update of one mod can force updates of the others.

## The idea in one paragraph

For file types the loader can read, a mod ships **only its own additions or small edits**, not the whole file. Examples: "add these 5 shop rows", "add this picture and this title to the course-select screen", "change the word NOTHING to UNMUTE". At game start, ChaoGarage takes the file as it currently is (the game's original, or another mod's whole-file version) and applies each mod's additions to it. An addition doesn't depend on what else is in the file, so it keeps fitting when another mod updates. Only a **real clash** still needs a decision: two mods filling the very same row, picture or word differently.

The key point for the user: **only the mods that add things on top of others need to change** (Egg Hangar, Steam Multiplayer). Metal Sonic and the All-Stars Roster Pack can keep shipping whole files and update as often as they like.

## 1. What the user's mods share today (inventory)

Checked on 2026-10-23 against the installed mods (`mods\` of the Steam game, read-only) and the public `Metal Sonic 1.1.0 Beta 3.zip`. Each shared file was compared entry by entry with the game's original, and each mod's added entries were compared with the other mod's. The scripts and their full output are outside the repository, in the Claude data folder: `side-session-notes\local_5d362cce-…\shared-file-inventory\` (`overlaps.py`; `what_changed.py`/`.txt`: each mod against the stock files; `cross.py`/`.txt`: entries two mods both add; `total.py`: unpacked sizes). The loader log of the same day lists the same 41 combined members.

### Archive members two installed mods change (41)

| Shared file | Format | Mods | What each mod does to it |
|---|---|---|---|
| `Shopping.dat` (Base.xpac `AA924689`) | SSRT table | Roster Pack + Egg Hangar | Roster Pack: **adds** row `Item93` (Metal Sonic) and **edits** 2 racer rows (to platform "All"). Egg: **adds** 5 rows `Item98`–`Item102` (Death Egg songs). Public Metal Beta 3 also adds its own `Item93`. |
| `fe_track_select_*` ×7 languages | scene (`.stz`) | Metal + Egg | Metal: **adds** 2 texts. Egg: **adds** 3 pictures + 4 texts. One of Egg's texts is the same entry Metal adds (`A12E891E` = "METAL SONIC"), with identical words. |
| `fe_leaderbaords_*` ×7 | scene | Metal + Egg | Metal: **adds** 1 picture + 2 texts. Egg: **adds** 2 pictures + 1 text. **One picture name is the same in both (`E76B1254`), with different pictures**; Egg's current build keeps Metal's (see below). |
| `fe_license_records_*` ×7 | scene | Metal + Egg | Metal: **adds** 2 texts. Egg: **adds** 1 text. |
| `navigator_info_*` ×7 | scene | Metal + Steam Multiplayer | Metal: **adds** 1 picture (3 images). Steam MP: **repaints** 1 existing image (the A-key picture, used by 39 pictures; their positions are unchanged) and **edits** the font block. |
| lobby scene ×7 | scene | Metal + Steam Multiplayer | Metal: **adds** 1 picture. Steam MP: **repaints** 1 existing image and **adds** 1 picture (yellow "muted" bar) cut from that repainted image. |
| `dialogs_*` ×5 (EN/FR/GE/IT/SP) | scene (text table) | Metal + Steam Multiplayer | Metal: **adds** 1 picture. Steam MP: **edits** 1–2 existing texts (VIEW GAMER CARD → VIEW STEAM PROFILE, NOTHING → UNMUTE; German EINSTELLEN → TON AUS). |

Almost everything is "different entries of the same file", which the idea above combines with no clash. There are two exceptions, both between Metal and Egg:

* **`E76B1254` in the 7 leaderboard scenes**: both mods add a picture under this name, and the pictures differ. Egg's builder works the way Codex's did: "entries already in the base win". So today, with Metal installed, Metal's picture is shown and Egg's other picture and title are still added. The patch format must be able to say the same thing ("add only if absent"); otherwise Egg would lose all its leaderboard additions next to Metal.
* **`A12E891E` in the 7 track-select scenes**: identical words in both, so fine. If Metal ever changes that word, Egg's entry should give way rather than clash ("add only if absent" again).

Today these 41 members cost Egg about 62 MB and Steam Multiplayer about 19 MB of ready-made combined copies, which stop fitting when Metal changes.

### Other shared things

* **Whole script files** (`Scripts\*.sbn`): `FrontEndScripts.sbn` is shipped by the Roster Pack and by public Metal; `SetupObjects.sbn`/`Behaviours.sbn` by Metal. Egg and Steam Multiplayer already avoid shipping them: their own code changes the scripts while the game loads them. But Egg's change is built for the **exact** script versions it knows (stock, Roster Pack 0.1.0-alpha.3, Metal Beta 3). For any other version, Egg's menus switch themselves off (logged, no crash). It's the same kind of problem in a different place: see section 6.
* **Shader caches** (`ShaderCache\*.rtsc`): several mods add shader records. The loader already merges one mod's records into the game's cache. But two mods naming the **same** cache file are refused today ("two mods replace the same resource"). No two *installed* mods do this. Public Metal shares cache files with the Roster Pack and with the installed Metal layer, but those are alternatives, not meant to be installed together.
* `Base.xpac FECF5CFC` (racer settings, 14 tables): Roster Pack and public Metal both add `MechaSonic` rows. Not a problem today (those two are alternatives).

## 2. Loader-side merge operations, per format

A new kind of archive operation, `"kind": "patch"`, names a small **patch file** in the mod's folder. The first release understands two patch formats. Everything is addressed by the **name or hash of the entry**, never by its position in the file. So a patch fits any version of the file that still has (or still lacks) the entries it talks about.

Every "add" operation has two modes:
* **`add`**: the entry must not already exist with different content. If it does, that's a clash (section 3).
* **`add_if_absent`**: if the entry already exists (from the original file or from an earlier mod), it's left as it is and logged. This is how Egg's and Codex's builders already work, and how Egg handles `E76B1254` and `A12E891E` without losing its other additions.

### 2a. SSRT tables (`Shopping.dat`, racer settings, other `.dat` tables)

| Operation | Meaning |
|---|---|
| `add_row` / `add_row_if_absent` (table, row name, fields) | Adds a row. Fields are given by their hash and type (whole number, decimal, text). A row may carry a field other rows don't have (the console's `DLCIndex`). |
| `set_field` (table, row, field, value, optional `expect`) | Changes one field of an existing row. With `expect`, only if the field still has that value. |

How it's built: the way Codex's and Egg's builders already do it. Every original byte stays where it is. New rows, their texts and a longer row list go at the end of the file, and the header points to the new list. A file with several tables (the racer settings have 14) gets one new row list per changed table, with that table's own pointer changed. Text fields that `set_field` changes get new text at the end and a changed pointer. Rows are added in mod order (section 2d), so the Shop list order is stable.

### 2b. Menu scenes (`.stz` in SumoToolResources)

A scene is six zlib-packed sections. Section 3 holds one picture list (`PTEX`: named pictures plus the images they're cut from), one text list (`TEXT`: key → words), font blocks and other blocks. Section 4 holds the matching position fix-up lists. A patch first lists its **images** (DDS files in the mod, each stored once even if several pictures use it), then its operations:

| Operation | Meaning |
|---|---|
| `replace_image` (SHA-256 of the image it replaces, new image) | Repaints one existing image, found by its content, not its position. |
| `add_sprite` / `add_sprite_if_absent` (key, rectangle, image) | Adds a named picture. The image is one of the patch's own images, or an existing image named by its SHA-256 (its content after the repaints of every patch: images are repainted first; a picture that needs an image another mod repainted clashes with that mod). Steam MP's yellow bar is cut from the image it repaints. |
| `add_text` / `add_text_if_absent` (key, words) | Adds a text entry. |
| `set_text` (key, words, optional `expect`) | Changes the words of an existing entry. |
| `replace_chunk` (tag, SHA-256 of the block it replaces, new block) | Swaps one whole block that has no finer operation (Steam MP's font block). Only if the block's fix-up list is unchanged (true for Steam MP today), otherwise the patch carries both. |

Inside one patch the order is fixed: images replaced, then pictures added, then texts added or changed, then blocks swapped. Pictures and texts are kept **sorted by key**, as the game's files and `codexui/build_ui.py` (`make_ptex`, `merge_text`) do. New images are appended after the existing ones: all of a patch's images, when at least one of its pictures is kept. The loader then rebuilds the picture and text lists and their fix-up lists by the same steps as `merge_ptex`/`merge_text`/`check_relos`, which built Egg's user-tested scenes.

The loader **refuses** a scene that isn't in this exact shape (as "files don't match" when it is the original member, as *cannot be used together* when it is another mod's version): more than one picture or text list, fix-up lists not in the expected form, unknown section layout. It never guesses.

Unpacking needs a zlib reader in the loader. Built: zlib 1.3.1, unchanged (third_party), with a notice in `THIRD-PARTY-NOTICES.md`. Repacking: real compression (fast level), not "stored" blocks. Stored blocks would be simpler, but a track-select scene unpacks to 13.7 MB (Egg's version 14.8 MB) against about 3.4 MB packed. The 32-bit game reads the whole member into memory, so stored scenes would cost about 4× the memory while loading.

### 2c. Shader caches

Several mods may name the same `ShaderCache\*.rtsc`; the loader merges all their records into one private cache (it already does this for one mod). The game's own record wins over a mod's, as today. Two mods with **different** records under one shader key are a clash, as in section 3. That's the same rule the loader already applies to one mod's own duplicates (`rtsc.h`).

### 2d. How a shared member is put together at start

The order is **folder-name order**, the order in which the loader combines archives today (`discover()` in `host.cpp`). The "Metal Sonic first" rule is about starting mod code, not about files.

1. Start from the game's original member.
2. Combine all mods' whole-file changes (`replace`/`add`) first, exactly by the 1.2.0 rules (`variants`, `optional`). This gives one base. This needs `discover()` to work in two passes (whole files of every mod first, patches second); today it merges mod by mod.
3. Apply each mod's patch to that base, in folder-name order.
4. Write the result once to a cache file and serve it like any other member. The cache is named after the SHA-256 of all its inputs **and the loader build**, so a new loader never reuses an old result. It's written to a temporary file and published in one step, as shader caches already are, because two copies of the game can start at the same time. Each later start checks it by SHA-256.

So a patch works on top of the game's original **and** on top of another mod's whole file. That's why Metal and the Roster Pack don't need to change.

A mod's own check stays as in 1.2.0 for everything else. `expected_composed_sha256` is still "the archive with your own changes on the original game file", but patch members count in it **as the original member**, unchanged. Their result is proven separately by `expected_on_stock_sha256` (section 4), over the unpacked content. This way no hash depends on the bytes a compressor writes, so a later loader with a different zlib can't fail a mod whose content is right.

## 3. Conflict rules (true clashes)

| Case | Result |
|---|---|
| Two mods `add` the same entry (row / picture / text key) with **identical** content | Fine. Added once, logged. |
| `add_if_absent` and the entry exists (any content) | Left as it is, logged. Not a clash. |
| An entry added by an earlier mod's `add_if_absent`, then a later mod's `add` with different content | The later `add` wins and the earlier entry is dropped (logged). The result is then the same in either folder order: renaming a folder never changes it. |
| `add` and the entry exists with **different** content (in the base or from an earlier mod) | Clash. |
| Two mods `set` the same field/text to different values | Clash. |
| A `set`, `replace_image` or `replace_chunk` finds no matching entry, or its `expect` doesn't match (another mod changed that entry) | Clash. |
| A patch file can't be read, or the patch does not apply to the original member, or its result there is not the plan's pin | The mod's files don't match (as today). |
| The patches do not fit another mod's whole version of the member (its shape, or two mods patching it in different formats) | Clash (*cannot be used together*). |

A clash is decided per member, the same way 1.2.0 does it. If the patch is marked `"optional": true`, the **whole patch for that member** gives way (so a picture never appears without its title) and the log names the entry. Otherwise the player gets the existing *cannot be used together* message, now naming the entry ("Shopping.dat row Item93"). The loader never picks between two different values on its own; `add_if_absent` is the mod author's explicit choice.

## 4. Schema and capability names

In `plan.json` (plan schema stays 1):

```json
{ "key": "AA924689", "kind": "patch", "patch_format": "ssrt-patch/1",
  "patch": "patches/Shopping.patch.json", "patch_sha256": "...",
  "base_stored_sha256": "<the game's original member, as for replace>",
  "expected_on_stock_sha256": "<SHA-256 of the unpacked result of this patch on the original member>",
  "optional": true }
```

* `expected_on_stock_sha256` lets the loader prove at start that it builds exactly what the mod author tested: it applies the patch to the original member once and compares. It covers the **unpacked** content, so a different compressor can't make it fail. A mismatch is reported as the mod's files not matching.
* Images named by a patch are separate files, each with its own SHA-256 in the patch file, checked like payloads.
* For one member, a mod ships either a whole `replace` or a patch, never both.
* New capability names (exact facts, append-only, as now): `xpac-member-patch-ssrt`, `xpac-member-patch-scene`, `rtsc-merge-shared`. A mod that uses patches lists them in `loader.capabilities`.
* What older programs say about a mod with patches:
  * ChaoGarage 1.1.0 and 1.2.0 check capabilities before reading plans, so they show *needs a newer loader*.
  * A loader from before 1.1.0 never reads `allstars-mod.json`. It fails while reading the plan with an unclear error ("files missing or do not match"). That's the existing limit for any new feature (COMPATIBILITY.md section 5).
  * Mod Manager 1.3.0 stops on the unknown operation (`PackageInspector.cs`) and calls the mod broken, not "needs a newer manager". Adding `"must_understand": ["xpac-member-patch/1"]` to such mods may get the clearer answer, if the manager reads the description before the plan; this needs checking. Otherwise the note on the mod's page has to say "Mod Manager 1.4.0 or newer".
* Proposed release: **ChaoGarage 1.3.0** (new features → minor version under the release rules). Mods without patches load exactly as with 1.2.0.

The patch file formats (`ssrt-patch/1`, `scene-patch/1`) are JSON in the loader's existing JSON subset, with the image files beside them. The ChaoGarage release also ships a small command-line tool built from the same source (`chaogarage-members` extract / make / apply / same / digest). Mod build scripts and the manager can then produce and check results with the loader's own code.

## 5. Mod Manager and checker (`A:\All-Stars-Mod-Manager`)

* New profile `chaogarage-1.3.0` with the three capabilities.
* `PackageInspector`/`PackageVerifier`: accept `kind: patch`. Check `patch_sha256`, the images listed in patches, and `expected_on_stock_sha256`, either by calling `chaogarage-members` (apply, digest) or with a C# port of the two formats. Shared test files keep the loader and the manager in step.
* `Conflicts.cs`: predict clashes **per entry**, not just per member. Read the patches and, for whole-file mods, the entries of their files. Report clashes like "Egg Hangar and X both add Shopping.dat row Item98 with different content", and show "left as it is" / "gives way" cases as information.
* `Update-ModHashes.ps1`: also fill `patch_sha256` and `expected_on_stock_sha256`. `PACKAGE-FORMAT.md` and `MOD-AUTHORS.md`: document the new operation.
* The loader's result cache lives in a folder without `mod.ini`, so neither the loader nor the manager treats it as a mod. The manager should leave it alone (and may offer to delete it).

## 6. Migration

| Mod | Change | When |
|---|---|---|
| **ChaoGarage** | 1.3.0: patch engine for SSRT tables and scenes, zlib library, two-pass combining, shared shader caches, patch tool, tests. | first |
| **Mod Manager** | 1.4.0: profile, patch operation, entry-level conflicts. | with or right after the loader |
| **Egg Hangar** | `Shopping.dat` → one SSRT patch (5 rows). 21 scenes → scene patches (2–3 pictures and 1–4 texts each; `E76B1254` and `A12E891E` as `add_if_absent`, which keeps today's result next to Metal). The 62 MB of ready-made copies go away. Needs ChaoGarage 1.3.0. | first mod |
| **Steam Multiplayer** | 19 shared scenes → patches: `set_text` for dialogs, `replace_image` + `add_sprite` for lobby/navigator, `replace_chunk` for the font. Its 7 unshared main-menu scenes can stay whole files. | its next release |
| **Metal Sonic, Roster Pack** | Nothing required. They keep shipping whole files, and Egg's and Steam MP's patches fit any version that keeps the entries they use. (Optional later: move their additions to patches too, so even two "owner" mods never need variants.) | — |

During the switch, a player with the old Egg 1.2.0 and a new Metal is in today's situation (Egg's change gives way). Once Egg 1.3.0 is out, later Metal updates no longer affect Egg.

**What still needs a mod update after this** (honest list):
* A real clash (two mods `add`/`set` the same entry differently).
* An "owner" mod removing or repainting an entry another mod edits. For example, if Metal repainted the same A-key image Steam MP repaints.
* **Script code** (`.sbn`). Egg's runtime edits arrays inside compiled script modules, for exact versions only, and a loader can't merge compiled code safely. A better fix is mod-side, in Egg Hangar itself: Egg's runtime finds the arrays by name or anchor instead of matching whole-module hashes. A later loader could add whole-module `add_module`/`replace_module` operations. That would help Steam MP's script swaps, but not Egg's edits.
* File types not covered (audio banks, models, XML, images outside scenes): still whole files + `variants`.
* Native code (two mods hooking the same game function) isn't affected by any of this.

## 7. Trade-offs and risks

For:
* Owner mods update freely, add-on mods stop breaking, and the GameBanana pages need far fewer updates.
* Smaller downloads (Egg: about 62 MB less; Steam MP: about 19 MB less).
* Clashes are named exactly (row, picture, text), by the loader and by the checker before the game starts.

Against:
* The loader learns two game formats and gains a zlib library: more code at start-up, in a 32-bit game, before the game runs. Mitigations:
  * all-or-nothing, as now;
  * a self-check against the author's tested result;
  * results cached and checked again by SHA-256;
  * the engine tested against all 41 real members: it must rebuild the scenes and tables Egg and Steam MP ship today, compared section by section after unpacking.
* Combined menus are screens nobody has seen in that exact combination. A wrong result is a visual glitch (overlapping pictures), not a crash, but each new combination still needs one in-game look.
* Players need ChaoGarage 1.3.0 once (one more loader download), and manager users need 1.4.0.
* Start-up: the first start after any change unpacks, edits and repacks about 40 scenes (about 180 MB unpacked in all; the 7 track-select scenes alone are 14 MB each). Expect a few seconds once. Later starts only check the cache by SHA-256, similar to the hashing the loader already does.

To settle before building (short spike):
1. Time and memory of the first start with real compression of the 40 scenes. Check that the game loads a repacked scene: one modified scene, one look in the game.
2. Where the result cache lives. Proposed: `mods\.chaogarage-cache\`, outside every mod folder, so updating or removing one mod never leaves files inside another mod's folder.
3. Whether `optional` should stay "the whole patch for that member gives way" (proposed: no half features) or allow giving way per entry.

Alternatives considered:
* **More variants** (Egg ships a version for every Metal release): still needs an Egg update for each Metal update. Doesn't solve it.
* **Each mod patches files from its own DLL while the game loads them**: no loader change, but every mod re-implements the formats, and the order between mods becomes guesswork. Worse.
* **Keep today's `optional`**: nothing breaks, but features silently disappear after an update. That's today.

## 8. Effort estimate

Counted in focused Claude sessions. The user's part is approvals and short in-game checks.

| Step | Sessions |
|---|---|
| Spike: repacked scene in game, time/memory, cache location | 0.5 (+ 1 short in-game check) |
| Loader: zlib library (vendored, notice, tests) | 0.5 |
| Loader: SSRT patch engine (several tables, text fields) + tests | 1 |
| Loader: scene patch engine (unpack/pack, pictures, texts, fix-ups, images, blocks, shape checks) + tests on all 41 real members | 2.5–3 |
| Loader: two-pass combining, clash rules, messages, log, cache, shared shader caches, patch tool | 1.5–2 |
| Independent check + ChaoGarage 1.3.0 release | 0.5–1 |
| Manager/checker 1.4.0 (C# port or tool call, entry-level conflicts) | 2–2.5 |
| Egg Hangar 1.3.0 migration + in-game check | 1 |
| Steam Multiplayer migration + in-game check (both games) | 1 |
| **Total** | **about 11–14 sessions**, with 3–4 short in-game test rounds |

A smaller first step is possible: SSRT tables + shared shader caches only (about 3–4 sessions). That fixes `Shopping.dat` but not the 40 scenes, which is where most of the breakage happens, so the full plan is recommended.
