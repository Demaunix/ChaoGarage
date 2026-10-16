/* chaogarage_require.h - a check a mod's runtime DLL can make at the start of SsasrModInitialize.

   Why: ChaoGarage 1.1.0 and later read a mod's allstars-mod.json and refuse, with a clear message, a mod
   whose declared loader requirements they do not meet. Loaders released before 1.1.0 cannot do that: they
   never read allstars-mod.json. Only code that is newer than the running loader can notice that the loader
   is too old, and the mod's own runtime is such code. This header gives it a few lines to do so.

   What it does: looks for ChaoGarageGetLoaderInfo in the loaded ChaoGarage.dll (absent before 1.1.0) and
   checks the capability names you list. If anything is missing it shows ONE message box naming your mod and
   the download page, and returns E_FAIL; return that from SsasrModInitialize so the loader starts the game
   without mods (a loader before 1.1.0 then shows its own short "mods could not be enabled" box as well).
   It never downloads, opens or runs anything.

   Use (C++, 32-bit, inside SsasrModInitialize - never from DllMain):
       static const char* const kNeeds[] = { "startup-requirements-check", nullptr };
       HRESULT hr = ChaoGarageRequire(L"My Mod", kNeeds);
       if (FAILED(hr)) return hr;

   List the same names in your allstars-mod.json ("loader": {"capabilities": [...]}) so that the manager, the
   checker and the loader itself (1.1.0+) give the same answer before your DLL is even loaded.
   MIT License, like the rest of ChaoGarage. Requires loader_info.h from ChaoGarage's src/common. */
#pragma once
#include <windows.h>
#include <cstring>
#include <string>
#include "loader_info.h"

#ifndef CHAOGARAGE_REQUIRE_RELEASES_URL
#define CHAOGARAGE_REQUIRE_RELEASES_URL L"https://github.com/Demaunix/ChaoGarage/releases/latest"
#endif

inline HRESULT ChaoGarageRequire(const wchar_t* mod_name, const char* const* capabilities) {
    HMODULE host = GetModuleHandleW(L"ChaoGarage.dll");
    auto get = host ? reinterpret_cast<ChaoGarageGetLoaderInfoFn>(GetProcAddress(host, CHAOGARAGE_GET_LOADER_INFO_NAME)) : nullptr;
    const ChaoGarageLoaderInfoV1* info = get ? get() : nullptr;
    const bool readable = info && info->size >= sizeof(ChaoGarageLoaderInfoV1) && info->version >= CHAOGARAGE_LOADER_INFO_V1 && info->capabilities;
    std::wstring missing;
    for (auto want = capabilities; readable && want && *want; ++want) {
        bool have = false;
        for (auto c = info->capabilities; *c && !have; ++c) have = strcmp(*c, *want) == 0;
        if (!have) { if (!missing.empty()) missing += L", "; for (auto p = *want; *p; ++p) missing += static_cast<wchar_t>(static_cast<unsigned char>(*p)); }
    }
    if (readable && missing.empty()) return S_OK;
    std::wstring name = mod_name && *mod_name ? mod_name : L"This mod";
    std::wstring text = name + L" needs a newer ChaoGarage loader.\r\n\r\n";
    text += readable ? L"The installed loader (release " + std::wstring(info->release ? info->release : L"?") + L") does not provide: " + missing + L"."
                     : std::wstring(L"The installed loader is older than ChaoGarage 1.1.0.");
    text += L"\r\n\r\nInstall the newest ChaoGarage release (both ChaoGarage.dll and dinput8.dll) from:\r\n" CHAOGARAGE_REQUIRE_RELEASES_URL
            L"\r\n\r\nThe game will start without mods. Nothing was downloaded or opened.";
    MessageBoxW(nullptr, text.c_str(), (name + L" - loader too old").c_str(), MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
    return E_FAIL;
}
