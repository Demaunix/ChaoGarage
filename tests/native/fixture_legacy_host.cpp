// Stands for a host from before 1.1.0 in the tests: it has the 1.0 entry point (accepting only bootstrap
// version 1) and no ChaoGarageGetLoaderInfo. It never touches a game; if it is called it only reports so.
#include "bootstrap_api.h"
static volatile LONG g_calls = 0;
extern "C" HRESULT __cdecl SsasrModsHostInitialize(const SsasrBootstrapV1* b) {
    InterlockedIncrement(&g_calls);
    if (!b || b->size != sizeof(SsasrBootstrapV1) || b->version != SSASR_BOOTSTRAP_ABI_V1) return E_INVALIDARG;
    return E_FAIL;
}
extern "C" __declspec(dllexport) LONG __cdecl FixtureLegacyHostCalls() { return g_calls; }
