#pragma once
// What this host build provides, as named in allstars-mod.json "loader.capabilities" and in the
// managed tools' loader profile. Names are append-only: a name never changes meaning.
// The 1.0 host (profile ssasr-host-1) provided thirteen; twelve of them carry over. The rest are new in 1.1.0.
namespace ssasr {
inline const char* const kCapabilities[] = {
    "host-abi-1", "mod-ini-schema-1", "plan-schema-1", "plan-format-xpac", "plan-format-file",
    "plan-format-copy", "plan-format-rtsc-merge", "xpac-member-merge", "startup-order-folder-name", "startup-order-metal-sonic-first",
    "runtime-init-before-game-crt", "runtime-rollback-on-failed-init",
    "bootstrap-abi-2",              // the dinput8.dll/ChaoGarage.dll pair names its release and refuses a mismatched partner
    "loader-info-export",           // ChaoGarageGetLoaderInfo is available to mod runtimes
    "startup-requirements-check",   // allstars-mod.json loader requirements are enforced before any mod code is loaded
    "startup-load-order-check",     // allstars-mod.json load_order is checked against the real start order
    "startup-warning-dialog",       // refusals are explained in one dialog per start
    "rtsc-merge-16384-records",     // a shader cache may hold up to 16384 records
    nullptr};
// Provided by earlier builds, not by this one. A mod that requires one of these was made for an earlier loader,
// so the message points to the mod's page rather than to a loader update.
inline const char* const kRetiredCapabilities[] = {
    "bootstrap-abi-1",              // 1.0 pair interface; 1.1.0 recognises it only to refuse a mismatched dinput8.dll
    nullptr};
}
