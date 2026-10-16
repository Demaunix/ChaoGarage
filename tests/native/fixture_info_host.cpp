// Stands for a host of another release (FIXTURE_RELEASE) or one whose release information cannot be read
// (FIXTURE_BAD_INFO), for the proxy's pairing test. It is never initialized by that test.
#include "bootstrap_api.h"
#include "loader_info.h"
static const char* const kCaps[] = {"host-abi-1", nullptr};
#ifdef FIXTURE_BAD_INFO
static const ChaoGarageLoaderInfoV1 kInfo{4, CHAOGARAGE_LOADER_INFO_V1, L"1.1.0", SSASR_BOOTSTRAP_ABI_V2, 1, 1, 1, kCaps, L""};
#else
static const ChaoGarageLoaderInfoV1 kInfo{sizeof(ChaoGarageLoaderInfoV1), CHAOGARAGE_LOADER_INFO_V1, L"9.9.9", SSASR_BOOTSTRAP_ABI_V2, 1, 1, 1, kCaps, L""};
#endif
extern "C" HRESULT __cdecl SsasrModsHostInitialize(const SsasrBootstrapV1*) { return E_FAIL; }
extern "C" const ChaoGarageLoaderInfoV1* __cdecl ChaoGarageGetLoaderInfo(void) { return &kInfo; }
