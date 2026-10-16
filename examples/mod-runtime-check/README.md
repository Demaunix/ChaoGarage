# Runtime check for mods

`chaogarage_require.h` lets a mod's runtime DLL refuse to start, with one clear message, when the loader it runs under lacks something the mod needs — including loaders released **before** 1.1.0, which never read `allstars-mod.json`. See [docs/MOD-AUTHORS.md](../../docs/MOD-AUTHORS.md#protecting-players-who-still-have-an-older-loader).

`example_runtime.cpp` is a complete (empty) runtime using it. Build it as a 32-bit DLL with the same toolchain as the loader, for example from the repository root:

```powershell
pwsh -File tools\build.ps1
pwsh -File tools\build-tests.ps1      # also builds out\example_runtime.dll
```

The tests run it under the 1.1.0 loader (the check passes silently) and next to a stand-in for an older loader (it shows its message and returns `E_FAIL`).

Copy `chaogarage_require.h` together with `src/common/loader_info.h` into your own project; both are MIT-licensed.
