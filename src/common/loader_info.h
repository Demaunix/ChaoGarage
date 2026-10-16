#pragma once
#include <stdint.h>

/* Exported by ChaoGarage.dll from release 1.1.0 on. Its absence means a release
   before 1.1.0. The proxy uses it to confirm a matching pair before calling the
   host; a mod's runtime may use it to check the loader it runs under (see
   examples/mod-runtime-check). Everything it points to is static and valid for the
   life of the process. Read only the fields covered by `size`. */
#define CHAOGARAGE_LOADER_INFO_V1 1u
#define CHAOGARAGE_GET_LOADER_INFO_NAME "ChaoGarageGetLoaderInfo"

struct ChaoGarageLoaderInfoV1 {
    uint32_t size;                    /* sizeof(ChaoGarageLoaderInfoV1) or larger */
    uint32_t version;                 /* CHAOGARAGE_LOADER_INFO_V1 */
    const wchar_t* release;           /* L"1.1.0" */
    uint32_t bootstrap_abi;           /* bootstrap interface this host requires (2) */
    uint32_t host_abi;                /* interface given to mod runtimes (SsasrHostApiV1 = 1) */
    uint32_t mod_ini_schema;          /* mod.ini [mod] schema read (1) */
    uint32_t plan_schema;             /* plan.json schema read (1) */
    const char* const* capabilities;  /* NULL-terminated lowercase names */
    const wchar_t* releases_url;      /* text for messages; never opened */
};

typedef const ChaoGarageLoaderInfoV1* (__cdecl* ChaoGarageGetLoaderInfoFn)(void);
