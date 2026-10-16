/* One release identity for both loader files. ChaoGarage.dll and dinput8.dll are
   built from the same tree and accept each other only when these values match.
   Plain #defines only: this header is also read by the resource compiler. */
#pragma once
#define CHAOGARAGE_VERSION_MAJOR 1
#define CHAOGARAGE_VERSION_MINOR 1
#define CHAOGARAGE_VERSION_PATCH 0
#define CHAOGARAGE_VERSION_RC 1,1,0,0
#define CHAOGARAGE_RELEASE_A "1.1.0"
#define CHAOGARAGE_RELEASE_W L"1.1.0"
/* Shown in warnings as text only; nothing opens it. */
#define CHAOGARAGE_RELEASES_URL_W L"https://github.com/Demaunix/ChaoGarage/releases/latest"
