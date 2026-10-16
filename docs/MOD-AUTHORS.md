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

### Capabilities provided by ChaoGarage 1.1.0

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

The 1.0 loader provided `host-abi-1` through `runtime-rollback-on-failed-init` plus `bootstrap-abi-1`. 1.1.0 no longer provides `bootstrap-abi-1` (the two loader files now use interface 2); a mod that requires it is reported as *made for a different loader*, not as needing a newer one. Names are never reused with another meaning.

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
