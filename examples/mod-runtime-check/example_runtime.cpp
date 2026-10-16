// A minimal mod runtime that refuses to start under a loader without the features it needs.
// Build (x86): see examples/mod-runtime-check/README.md. It does nothing in the game.
#include "mod_api.h"
#include "chaogarage_require.h"

static const char* const kNeeds[] = { "startup-requirements-check", "xpac-member-merge", nullptr };

extern "C" HRESULT __cdecl SsasrModInitialize(const SsasrHostApiV1* api, const wchar_t* mod_directory) {
    HRESULT hr = ChaoGarageRequire(L"Example Mod", kNeeds);
    if (FAILED(hr)) return hr;
    if (!api || api->size < sizeof(SsasrHostApiV1) || !mod_directory) return E_INVALIDARG;
    // ... install the mod's own hooks here; no game function may be called yet ...
    return S_OK;
}
extern "C" HRESULT __cdecl SsasrModRollback() {
    // ... undo whatever SsasrModInitialize installed ...
    return S_OK;
}
