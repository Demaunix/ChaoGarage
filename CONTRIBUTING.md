# Contributing

Thanks for helping. A few rules keep the loader safe for everyone's game:

* **No game files.** Never commit, attach or link files from the game, the game executable, Steam or a console SDK — not in code, tests, issues or pull requests. Tests use synthetic files only.
* **Keep the start-up rules.** Nothing may run in `DllMain` beyond what `proxy.cpp` documents; no game function may be called before the game's own start-up; any change that is published must be undone completely if something later fails.
* **Messages tell the truth.** A message may only suggest a loader update when a mod declared something the loader does not provide. Untrusted text (mod names, IDs, folder names) is always shortened and cleaned before it is shown.
* **Interfaces are append-only.** Never change the meaning of a capability name, a field of a public structure or an exported function; add new ones instead.
* **Tests with every change.** Run `tools\build.ps1`, `tools\build-tests.ps1` and `tools\run-tests.ps1`; add a vector to `tests/vectors/loader-requirements.json` for description changes (the All-Stars Mod Manager's tests use the same file) and an end-to-end case for start-up changes.
* Code is contributed under the MIT license.

Bug reports and compatibility reports have issue templates; please include the copied start-up message and `mods\SsasrMods.log`.
