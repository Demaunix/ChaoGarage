# For mod authors

## Packaging

* Ship your mod as **one folder** that players put into `mods\`: `mod.ini`, `plan.json`, your runtime DLL, your payloads, optionally `allstars-mod.json`. The format is specified in the All-Stars Mod Manager's `docs/PACKAGE-FORMAT.md`.
* **Do not include `ChaoGarage.dll` or `dinput8.dll`.** They are shared by every mod; a copy in your ZIP can overwrite a newer pair, or one file of it. Link to the loader's releases instead (text below). Your ZIP should neither install nor remove the loader.
* Keep your mod's own DLLs and payloads inside your mod folder.

## Text for your GameBanana page

Paste this into the **Requirements** (or Installation) section and adjust the version if your mod needs a later one:

> **Requires the ChaoGarage mod loader 1.1.0 or newer** (two files: `ChaoGarage.dll` and `dinput8.dll`).
> Download: https://github.com/Demaunix/ChaoGarage/releases/latest
>
> **Install order**
> 1. Install ChaoGarage: copy **both** `ChaoGarage.dll` and `dinput8.dll` into the game folder (Steam → right-click the game → Manage → Browse local files). Replace older copies of both files.
> 2. Install this mod: copy its folder into the game's `mods` folder (create `mods` if needed), so that you have `mods\<Mod Name>\mod.ini`.
> 3. Start the game through Steam.
>
> If a ChaoGarage message appears at start, it says what to do. This mod's ZIP does not contain the loader files.

If your mod works with any ChaoGarage release (it declares no loader requirements), write "Requires the ChaoGarage mod loader" without a version.

## Declaring what your mod needs from the loader

Add a `loader` block to your `allstars-mod.json`. ChaoGarage 1.1.0+ refuses your mod with a clear message if the installed loader does not meet it; the manager and the checker give the same answer before the game starts.

```json
{
  "manifest_schema": "allstars-mod/1",
  "must_understand": ["loader-requirements/1"],
  "id": "my-mod",
  "name": "My Mod",
  "version": "1.0.0",
  "homepage": "https://gamebanana.com/mods/000000",
  "loader": {
    "host_abi": 1,
    "capabilities": ["startup-requirements-check", "xpac-member-merge"]
  }
}
```

* **Ask only for what you use.** Capabilities, `host_abi`, `mod_ini_schema` and `plan_schema` are exact facts about the loader; they never mean "newest". A mod that declares nothing is accepted by every loader that can read its files.
* `homepage` (an `https://` address) is shown when your mod pins other loader builds, so players find your update.
* `host_sha256` / `proxy_sha256` pin **exact** loader files. Use them only when your mod truly cannot work with any other build: every future loader release will then refuse your mod until you update it.
* `"must_understand": ["loader-requirements/1"]` makes tools that predate the `loader` block refuse to call your mod compatible.
* The file must use the loader's JSON subset: UTF-8 without byte-order mark, no comments, no trailing commas, whole non-negative numbers only. A description the loader cannot read stops your mod (and, since the loader starts all mods or none, the others) with a message — it is never treated as "no requirements". The checker reports the exact problem: `AllStarsModCheck "<your mod folder>" --loader chaogarage-1.1.0`.

### Capabilities provided by ChaoGarage

The list of ChaoGarage 1.3.2 (no new names since 1.3.1).

| Name | Meaning |
|---|---|
| `host-abi-1` | mod runtimes get `SsasrHostApiV1` |
| `mod-ini-schema-1`, `plan-schema-1` | reads `mod.ini` schema 1 and `plan.json` schema 1 |
| `plan-format-xpac`, `plan-format-file`, `plan-format-copy`, `plan-format-rtsc-merge` | the four plan group formats |
| `xpac-member-merge` | several mods may change different members of one archive |
| `startup-order-folder-name`, `startup-order-metal-sonic-first` | the start order |
| `runtime-init-before-game-crt` | runtimes start before the game's own C runtime: no game function may be called yet |
| `runtime-rollback-on-failed-init` | `SsasrModRollback` is called when a later mod fails |
| `bootstrap-abi-2` | the two files check that they come from one release |
| `loader-info-export` | `ChaoGarageGetLoaderInfo` is available to runtimes |
| `startup-requirements-check` | the `loader` block and `must_understand` are enforced before any mod code loads |
| `startup-load-order-check` | `load_order` is checked against the real start order |
| `startup-warning-dialog` | refusals are explained in one message per start |
| `rtsc-merge-16384-records` | shader caches may hold up to 16,384 records |
| `xpac-member-variants` | 1.2.0: an operation's `variants` are used (see [Sharing an archive member](#sharing-an-archive-member-with-another-mod-120)) |
| `xpac-member-optional` | 1.2.0: an operation marked `optional` gives way to another mod's change |
| `xpac-member-patch-ssrt` | 1.3.0: `patch` operations in the `ssrt-patch/1` format (see [Adding to a game file another mod also changes](#adding-to-a-game-file-another-mod-also-changes-130)) |
| `xpac-member-patch-scene` | 1.3.0: `patch` operations in the `scene-patch/1` format |
| `rtsc-merge-shared` | 1.3.0: several mods may seed the same shader cache |
| `pending-mod-updates` | 1.3.0: an update your runtime staged is put in place at the next start (see [Updating your mod while the game runs](#updating-your-mod-while-the-game-runs-130)) |
| `startup-order-metal-core-first` | 1.3.1: a mod with a file `runtime\MetalSonicMod.dll` starts before the other mods, right after the mod with ID `metal-sonic` |

The 1.0 loader provided `host-abi-1` through `runtime-rollback-on-failed-init` plus `bootstrap-abi-1`. 1.1.0 no longer provides `bootstrap-abi-1` (the two loader files now use interface 2); a mod that requires it is reported as *made for a different loader*, not as needing a newer one. Names are never reused with another meaning.

## Sharing an archive member with another mod (1.2.0)

The loader starts all mods or none, and before 1.2.0 two mods that changed the same member of a game archive (`Resource\*.xpac`) always stopped every mod. From 1.2.0 an operation in your `plan.json` can say how to combine with another mod:

```json
{ "key": "0720C59E", "kind": "replace", "base_stored_sha256": "...",
  "payload": "payloads/0720C59E.dat", "payload_sha256": "...", "stored_size": 288928,
  "optional": true,
  "variants": [
    { "over_mod": "metal-sonic", "over_payload_sha256": "<SHA-256 of Metal Sonic's payload for this member>",
      "payload": "payloads/0720C59E.metal.dat", "payload_sha256": "...", "stored_size": 311008 } ] }
```

* **`variants`**: your version of the member made on top of another mod's version. It is used when the other mod's content for this member has exactly the SHA-256 in `over_payload_sha256` (lowercase hex). `over_mod` is for people reading the plan only (the loader's log names mods by their `mod.ini` id). A variant is checked like any payload, even when it is not used.
* **`optional`**: if another mod changes the member and no variant fits (for example after that mod updated), your change gives way and the other mod's is used; your other changes stay. Without it, such a pair is refused as before.
* Order: a fitting variant first (of either mod); then an optional change gives way (the later mod's first, in folder order); otherwise *cannot be used together*. Two mods that each offer a different version for the other's content are refused, not guessed between. When two mods' changes were combined through a variant, a third mod combines with the result only through a variant for that combined content; when an optional change gave way, the remaining change keeps its own variants and optional flag.
* Your plan's `expected_composed_sha256` stays what it was: the archive with your own payloads on the original game file.
* Require the features, so a 1.1.0 loader asks the player to update instead of reporting a conflict:
  `"loader": { "capabilities": ["xpac-member-variants", "xpac-member-optional"] }`. Loaders before 1.2.0 ignore both fields.

## Adding to a game file another mod also changes (1.3.0)

`variants` (above) need a new release of your mod every time the other mod changes its version of the member. From 1.3.0, for two kinds of members, your mod can ship **only its own additions and small edits** — a *patch* — and the loader applies them at start to whatever version of the member is there: the game's original, or another mod's whole member.

| Format | Members | What a patch can do |
|---|---|---|
| `ssrt-patch/1` | SSRT tables (`Shopping.dat`, racer settings, other `.dat` tables) | add rows; change a field of a row |
| `scene-patch/1` | menu scenes (`.stz` in `SumoToolResources.xpac`) | add pictures (with their DDS images) and texts; change a text; repaint an image, found by its content; swap a whole block (for example a font), found by its content |

Everything is named by row name, picture key, text key or content hash, never by position (a row name the table has more than once is named with its `occurrence`, counted from 1; rows are only ever appended, so the count is stable). Each addition has two modes: `add` (the entry must not be there with other content) and `add_if_absent` (if the entry is already there, it is left as it is; a later mod's plain `add` of the same entry wins over it, in any folder order). A change can carry the value it expects (`expect_bits`, `expect_text`); if another mod changed that entry, the change does not apply. A scene's added images come with its pictures: they are added when at least one of the patch's pictures is kept.

**Making a patch.** Build your version of the member on the game's original, then let the tool write the patch. The tool is built from the loader's own code, so it writes and checks exactly what the loader will do:

```
chaogarage-members extract "<game>\Resource\SumoToolResources.xpac" DB06DABE stock.stz
chaogarage-members make scene stock.stz mine.stz patches\DB06DABE\patch.json [--if-absent]
chaogarage-members apply scene stock.stz check.stz patches\DB06DABE\patch.json
chaogarage-members same scene check.stz mine.stz           (prints "same")
chaogarage-members digest scene check.stz                  (the plan's expected_on_stock_sha256)
```

`make` writes the images and blocks the patch names next to it (`images\…`, `blocks\…`). It refuses changes a patch cannot express (removed or moved pictures, reordered rows, ...): ship those as a whole member.

**In `plan.json`**, in the archive's group, instead of a `replace`:

```json
{ "key": "DB06DABE", "kind": "patch", "patch_format": "scene-patch/1",
  "patch": "payloads/patches/DB06DABE/patch.json", "patch_sha256": "<SHA-256 of patch.json>",
  "base_stored_sha256": "<SHA-256 of the game's original member>",
  "expected_on_stock_sha256": "<chaogarage-members digest of the patch applied to the original>",
  "optional": true }
```

* A patched member counts as **the original** in your group's `expected_composed_sha256`/`_size` (your own archive layout keeps it unchanged). `expected_on_stock_sha256` pins your patch's result instead: for a table the SHA-256 of the result, for a scene a digest of its unpacked sections, so the loader may pack the scene differently and still match.
* Every file the patch names is checked by its SHA-256. A patch takes no `variants`; one member gets either a `replace` or a `patch` from your mod, never both.
* `"optional": true`: if your patch clashes with another mod's change of the same entry, your whole patch for that member gives way (your other changes stay) and the log names the entry. Without it the player gets *cannot be used together*, naming the entry.
* Require the formats you use: `"loader": { "capabilities": ["xpac-member-patch-scene"] }` (or `-ssrt`). ChaoGarage 1.1.0 and 1.2.0 then ask the player to update the loader. A loader from before 1.1.0 does not read that and refuses the plan with an unclear "files do not match".
* Order at start: all mods' whole-member changes are combined first (1.2.0 rules), then every patch in folder order. The result is kept in `mods\.chaogarage-cache` and reused while nothing changes; it is rebuilt when any input changes (a mod, the game file, the loader). At every start, cache entries this start did not use and that were not written for a week are removed (patched members and shared shader caches); an entry another running copy of the game still holds is kept.

**Shader caches (`rtsc-merge-shared`).** Several mods may now seed the same `ShaderCache\*.rtsc` with `rtsc_merge`: the game's records first, then each mod's in folder order. Two mods giving one shader key different records cannot be used together.

## Updating your mod while the game runs (1.3.0)

While the game runs, the loader keeps your payloads open, so your mod's folder can't be replaced then. If your runtime
updates the mod itself, stage the new version instead and let the loader put it in place at the next start (capability
`pending-mod-updates`; check it with `ChaoGarageGetLoaderInfo` before you download anything):

1. Write the complete new folder to `<game>\mods-updates\staged\<your folder name>\` (outside `mods\`, so it is never read as
   a mod). Check what you downloaded first: the loader only checks that the staging is complete and unchanged, not where it
   came from.
2. Write `<game>\mods-updates\staged\<your folder name>.ready` LAST (write it under another name, then rename it), ASCII:
   `format=1`, `folder=<your folder name>`, `version=<new version>`, `replaces=<lowercase SHA-256 of the installed mod.ini>`,
   `keep=<path>` for each of the player's own files (up to 8, e.g. `keep=config.ini`), then one `file=<sha256> <size> <path>`
   line per file of the staged folder (the path relative to the folder, `/` or `\`).
3. At the next start, before any mod is read, the loader checks the staging: the installed folder must still be the one you
   made it for (its `mod.ini` has the `replaces` SHA-256), so an uninstalled, replaced or hand-updated mod is never touched;
   the staged folder must hold exactly the listed files (sizes and SHA-256; kept files by name only), no links, a `mod.ini`.
   Then it copies the kept files from the installed folder over the staged ones (edits made after your download count),
   renames the marker to `<folder>.swapping` (its journal), moves `mods\<folder>` to `mods-updates\previous\<folder>`
   (replacing an older copy there) and the staged folder into `mods\`. A staging that doesn't match is removed; a folder in
   use stays as it is and the staging waits for the next start; each update is one line in `mods\SsasrMods.log`. Only a
   `.swapping` journal makes a later start finish (or undo) a swap that was cut short.

Hold `<game>\mods-updates\lock` (opened with no sharing) while you stage: the loader touches nothing while another game
holds it. Only an installed mod is updated this way; the loader never installs a new one from `mods-updates`, and never
follows a link there.

## Protecting players who still have an older loader

A loader released before 1.1.0 never reads `allstars-mod.json`, so it does not enforce your declared requirements (it still checks your manifest, hashes and plan). Your runtime DLL can: include [`examples/mod-runtime-check/chaogarage_require.h`](../examples/mod-runtime-check/chaogarage_require.h) and call it first thing in `SsasrModInitialize`:

```cpp
static const char* const kNeeds[] = { "startup-requirements-check", nullptr };

extern "C" HRESULT __cdecl SsasrModInitialize(const SsasrHostApiV1* api, const wchar_t* dir) {
    HRESULT hr = ChaoGarageRequire(L"My Mod", kNeeds);   // one message naming your mod and the download page
    if (FAILED(hr)) return hr;                           // the loader then starts the game without mods
    ...
}
```

It looks for `ChaoGarageGetLoaderInfo` (absent before 1.1.0) and the capabilities you list. It never downloads or opens anything. Under a pre-1.1.0 loader the player sees your message and then that loader's own short "mods could not be enabled" message. Under 1.1.0+ the loader has already checked your `allstars-mod.json`, so the call simply returns `S_OK`.

## Release checklist

1. `mod.ini` pins (`plan_sha256`, `runtime_sha256`) match the files (the manager repository has `tools/Update-ModHashes.ps1`).
2. `allstars-mod.json` (optional) passes `AllStarsModCheck "<mod folder>" --loader chaogarage-1.1.0`.
3. The ZIP contains your mod folder only — no `ChaoGarage.dll`, no `dinput8.dll`, no game files.
4. Your page links to https://github.com/Demaunix/ChaoGarage/releases/latest with the install order above.
