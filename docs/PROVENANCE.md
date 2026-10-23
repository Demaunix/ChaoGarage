# Provenance

## Where the code comes from

ChaoGarage was written for this project. Release 1.1.0 is built from the 1.0 loader's source with these changes:

| Part | 1.0 source | In 1.1.0 |
|---|---|---|
| archive, file, copy and shader-cache handling (`files.h`, `json.h`, `resources.h`, `rtsc.h`, `virtual_io.h`, `virtual_xpac.h`, `legacy_cache.h`) | yes | **byte-identical** (shader caches up to 16,384 records) |
| mod runtime interface (`mod_api.h`) | yes | **byte-identical** |
| host (`host.cpp`) | yes | new checks before preparation, messages, `ChaoGarageGetLoaderInfo`; the preparation, loading, redirection and rollback steps are unchanged |
| bootstrap (`proxy.cpp`, `bootstrap_policy.*`) | yes | pair check, messages, start-up context version 2; the start-up gate, executable check and DirectInput forwarding are unchanged |
| `compat.h`, `messages.h`, `capabilities.h`, `loader_info.h`, `chaogarage_release.h` | — | new |
| tests | partly | the 1.0 cleanup test unchanged; the policy test extended; the rest new |

Release 1.2.0 changes only the combination of two mods' changes to one archive member (`virtual_xpac.h`: the `variants` and `optional` operation fields; `host.cpp`: the log lines), the two capability names and the release number, and adds tests for them.

The 1.0 loader files that mods shipped before this repository existed were tied to their source by rebuilding them with the same Microsoft toolchain: they matched byte for byte except for embedded time stamps. Those earlier binaries are not part of this repository.

## What the release files contain

* `ChaoGarage.dll`, `dinput8.dll`: this project's code, compiled with MSVC 14.42 and statically linked with the Microsoft C/C++ runtime (`/MT`) — see [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).
* Documentation and the example runtime check: this project's own text and code.
* No game files, no game executable, no Steam or console SDK, no fonts or images from other projects.

## Artwork

`assets/banner.svg` is original artwork made for this project from plain shapes and text. It uses no logo, character or artwork from any game.

## Trademarks

*Sonic*, *SEGA* and *Sonic & SEGA All-Stars Racing* are trademarks or registered trademarks of SEGA Corporation. *Steam* is a trademark of Valve Corporation. *Windows*, *DirectInput* and *Visual Studio* are trademarks of Microsoft Corporation. ChaoGarage is an unofficial fan project, not affiliated with or endorsed by them. The names say which game and system the loader works with.
