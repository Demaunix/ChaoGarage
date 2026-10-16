#pragma once
// The one start-up warning: what failed, for which mod, and what the player can do.
// Every piece of text from a mod (names, IDs, folder names, details) is bounded and
// stripped of control and invisible formatting characters here. Addresses are text
// only; nothing is opened, downloaded or run.
#include <windows.h>
#include <string>
#include <vector>
#include "chaogarage_release.h"

namespace ssasr::messages {

enum class Cat {
    NeedsNewerLoader,    // a declared requirement this release does not meet and a newer one could
    MadeForOtherLoader,  // pinned to other loader builds, or an older interface this release does not provide
    DescriptionUnusable, // allstars-mod.json present but unreadable or inconsistent
    LoadOrder,           // a declared start order the folder names do not give
    Files,               // missing/damaged mod files, or game files that differ from what the mod was made for
    Conflict,            // two installed mods that cannot be used together
    RuntimeLoad,         // Windows could not load the mod's DLL, or it lacks the two entry points
    RuntimeInit,         // the mod's own start-up code reported an error
    Environment,         // something outside the mods (another tool, folder links)
    PairMismatch,        // ChaoGarage.dll and dinput8.dll from different releases
    RollbackFailed,      // changes could not be undone: the game start is stopped
};

struct Entry { Cat cat; std::wstring who; std::wstring detail; std::wstring homepage; };

inline std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"?";
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

// One line, at most `max` UTF-16 units, no control/invisible formatting characters, no broken surrogate pairs.
inline std::wstring clean(const std::wstring& s, size_t max) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        bool pair = c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF;
        if (out.size() + (pair ? 2 : 1) > max) { out += L'\x2026'; break; }
        if (pair) { out += c; out += s[++i]; continue; }
        bool bad = c < 0x20 || (c >= 0x7F && c <= 0x9F) || (c >= 0xD800 && c <= 0xDFFF) || (c >= 0x200B && c <= 0x200F) ||
                   (c >= 0x2028 && c <= 0x202E) || (c >= 0x2060 && c <= 0x2069) || c == 0xFEFF || (c >= 0xFFF9 && c <= 0xFFFB);
        out += bad ? L'?' : c;
    }
    return out;
}

inline const wchar_t* headline(Cat c) {
    switch (c) {
    case Cat::NeedsNewerLoader: return L"needs a newer ChaoGarage loader.";
    case Cat::MadeForOtherLoader: return L"was made for a different ChaoGarage loader build.";
    case Cat::DescriptionUnusable: return L"has a description file (allstars-mod.json) that the loader cannot use.";
    case Cat::LoadOrder: return L"would start in the wrong order.";
    case Cat::Files: return L"has files that are missing or do not match what it expects.";
    case Cat::Conflict: return L"cannot be used together with another installed mod.";
    case Cat::RuntimeLoad: return L"has a code file (DLL) that Windows could not load.";
    case Cat::RuntimeInit: return L"reported an error while starting.";
    case Cat::Environment: return L"could not be started because of something outside the mods.";
    case Cat::PairMismatch: return L"ChaoGarage.dll and dinput8.dll are from different releases.";
    case Cat::RollbackFailed: return L"could not undo its changes, so the game start was stopped.";
    }
    return L"could not be started.";
}

inline std::wstring action(const Entry& e) {
    switch (e.cat) {
    case Cat::NeedsNewerLoader:
        return L"Install the newest ChaoGarage release (both ChaoGarage.dll and dinput8.dll) from: " CHAOGARAGE_RELEASES_URL_W;
    case Cat::MadeForOtherLoader:
        return e.homepage.empty() ? std::wstring(L"Look for an updated version of this mod where you downloaded it (for example its GameBanana page).")
                                  : L"Look for an updated version of this mod on its page: " + e.homepage;
    case Cat::DescriptionUnusable:
        return L"Reinstall the mod from its page, or ask its author for a corrected file. To play without it, move its folder out of the mods folder.";
    case Cat::LoadOrder:
        return L"Rename a mod folder as described, or ask the mods' authors. Updating the loader does not change the order.";
    case Cat::Files:
        return L"Reinstall the mod. If the game's own files were changed, restore them (Steam: Properties > Installed Files > Verify integrity). Updating the loader does not fix this.";
    case Cat::Conflict:
        return L"Use only one of these mods: move the other folder out of the mods folder.";
    case Cat::RuntimeLoad:
        return L"Reinstall the mod. If it keeps happening, report it to the mod's author with mods\\SsasrMods.log.";
    case Cat::RuntimeInit:
        return L"This is not known to be a version problem. Report it to the mod's author with mods\\SsasrMods.log.";
    case Cat::Environment:
        return L"Close or remove other tools that change the game's files, then start the game again.";
    case Cat::PairMismatch:
        return L"Install both files from the same ChaoGarage release: " CHAOGARAGE_RELEASES_URL_W;
    case Cat::RollbackFailed:
        return L"Move this mod's folder out of the mods folder, start the game again, and report it to the mod's author with mods\\SsasrMods.log.";
    }
    return L"";
}

constexpr size_t kShownEntries = 6;

inline std::wstring title() { return L"ChaoGarage " CHAOGARAGE_RELEASE_W L" - mods were not started"; }

inline std::wstring compose(const std::vector<Entry>& entries, bool stopped) {
    std::wstring t = stopped ? L"The game start was stopped to protect your files.\r\n\r\n"
                             : L"The game will start without mods.\r\n\r\n";
    size_t shown = 0;
    for (auto& e : entries) {
        if (shown == kShownEntries) break;
        ++shown;
        t += L"\x2022 " + clean(e.who, 90) + L" " + headline(e.cat) + L"\r\n";
        if (!e.detail.empty()) t += L"   " + clean(e.detail, 420) + L"\r\n";
        t += L"   " + clean(action(e), 300) + L"\r\n\r\n";
    }
    if (entries.size() > shown) t += L"...and " + std::to_wstring(entries.size() - shown) + L" more, listed in mods\\SsasrMods.log.\r\n\r\n";
    t += L"Details: mods\\SsasrMods.log. Press Ctrl+C to copy this message. Nothing was downloaded or opened.";
    return t;
}

// Who a problem is about: the mod's display name and its folder, both bounded and cleaned by compose().
inline std::wstring label(const std::wstring& name, const std::wstring& folder) {
    auto n = clean(name, 48), f = clean(folder, 48);
    return n.empty() || n == f ? L"Mod \"" + f + L"\"" : L"Mod \"" + n + L"\" (folder \"" + f + L"\")";
}

}  // namespace ssasr::messages
