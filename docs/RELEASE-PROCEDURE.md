# Release procedure

Releases are made by hand by a maintainer, after review. Nothing in this repository publishes, uploads or updates anything by itself, and a local development build is never published.

## 1. Prepare

1. Decide the version. Both files always get the same version. Change `src/common/chaogarage_release.h` (numbers, strings) and add the release to `CHANGELOG.md`.
2. If the host gains a capability, append its name to `src/host/capabilities.h` (never rename or reuse one), list it in `docs/MOD-AUTHORS.md`, and update `tests/vectors/loader-requirements.json` (`self.capabilities`). A capability that a build stops providing moves to `kRetiredCapabilities`.
3. If the releases page address changes, update `CHAOGARAGE_RELEASES_URL_W` (it is compiled into both files and into the example runtime check).

## 2. Build and test from a clean checkout

```powershell
git clone https://github.com/Demaunix/ChaoGarage; cd ChaoGarage; git checkout <commit>
pwsh -File tools\build.ps1
pwsh -File tools\build-tests.ps1
pwsh -File tools\run-tests.ps1 -Work C:\cgw
pwsh -File tools\mutation-native.ps1 -Work C:\cgm      # when loader logic changed
pwsh -File tools\package.ps1                            # -> out\release\
```

`package.ps1` refuses to package if `out\test-results.json` is missing, is for other DLLs, or has a failure.

## 3. Check the result

* Build the same commit a second time in another folder; both DLLs must be byte-identical to the first build.
* `dumpbin /headers /exports /imports`: machine x86; `ChaoGarage.dll` exports `SsasrModsHostInitialize`, `ChaoGarageGetLoaderInfo`, `ChaoGarageVirtualCounters`; `dinput8.dll` exports the six DirectInput names (ordinals 1–6) and imports only `kernel32` and `bcrypt`.
* No personal paths, user names or debug-file paths in either DLL (`package.ps1` scans for them) or in any file of the source ZIP.
* Independent code review of the changes since the last release.
* If possible, start the real game with the new pair, once with no mods and once with the mods you test, and record what you observed in the release notes. Say plainly if this was not done.

## 4. Publish (maintainer with repository access)

1. Tag the reviewed commit `v<version>` and push the tag.
2. Create the GitHub release from the tag. Upload exactly the files from `out\release\`: `ChaoGarage-<version>-win32.zip`, `ChaoGarage-<version>-source.zip`, `SHA256SUMS.txt`. Paste the release notes from `CHANGELOG.md`, including what was and was not tested in the game.
3. Download each asset from the public release page (`/releases/latest/download/<file>`) and compare its SHA-256 with `SHA256SUMS.txt`.

## 5. After publishing

* Add the new pair to the All-Stars Mod Manager's loader profile table (`src/Core/Game/loader-profiles.json`: both SHA-256 values, interface versions, capability names, evidence), so the manager and checker recognise it; release the manager separately.
* Mod pages keep linking to `https://github.com/Demaunix/ChaoGarage/releases/latest`; nothing needs to change there unless a mod now requires the new release.

## Rolling back a release

Mark the release as a pre-release or delete it, re-publish the previous release as *latest*, and say why in its notes. Players keep whatever pair they installed; a pair from one release always keeps working with the mods it accepted.
