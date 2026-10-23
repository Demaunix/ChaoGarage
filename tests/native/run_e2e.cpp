// End-to-end checks of the SHIPPED ChaoGarage.dll (and the example runtime check) through their real entry
// points, in synthetic game folders that contain no game files. Each case runs fixture_game.exe on a separate,
// invisible desktop, so the host's real MessageBoxW calls are captured without appearing on the user's screen.
//   run_e2e <out folder with the built files> <work folder> <loader-requirements.json> [--old-host <ChaoGarage.dll of 1.0>] [--prev-release <folder with the 1.1.0 pair>]
#include "files.h"
#include "json.h"
#include "chaogarage_release.h"
#include <cstdio>
#include <functional>
#include <map>
using namespace ssasr;

static std::wstring g_out, g_work, g_desktop = L"ChaoGarageE2E";
static unsigned g_pass = 0, g_fail = 0;
static std::string g_case;
static void Check(bool ok, const std::string& what) { if (ok) ++g_pass; else { ++g_fail; std::printf("FAIL [%s] %s\n", g_case.c_str(), what.c_str()); } }
static std::string U8(const std::wstring& w) { if (w.empty()) return {}; int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr); std::string s((size_t)n, 0); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr); return s; }
static void Put(const std::wstring& p, const std::string& bytes) { Handle f(CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr)); require(f.h != INVALID_HANDLE_VALUE, "write fixture file"); if (!bytes.empty()) write(f.h, bytes.data(), (DWORD)bytes.size()); }
static std::string Get(const std::wstring& p) { if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) return {}; return text_file(p); }
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static void Dir(const std::wstring& p) { size_t i = 3; while ((i = p.find(L'\\', i + 1)) != std::wstring::npos) CreateDirectoryW(p.substr(0, i).c_str(), nullptr); CreateDirectoryW(p.c_str(), nullptr); }
static std::string Sha(const std::string& s) { Hash h; if (!s.empty()) h.add(s.data(), (DWORD)s.size()); return h.finish(); }
// Synthetic XPAC archives, laid out exactly as the loader lays out a composed archive (rows by key, 2048-byte alignment).
static const std::map<uint32_t, std::string> g_xpac = {{0x10, "original ten"}, {0x20, "original twenty"}, {0x30, "original thirty"}};
static uint64_t Align(uint64_t n) { return (n + 2047) & ~uint64_t(2047); }
static std::string Xpac(const std::map<uint32_t, std::string>& members) {
    const uint32_t start = (uint32_t)Align(24 + 20ull * members.size());
    std::string head(start, '\0'), data; const uint32_t h[6] = {0, 0, start, (uint32_t)members.size(), 0, 0}; memcpy(head.data(), h, 24);
    size_t i = 0; uint64_t cursor = start;
    for (auto& [k, b] : members) {
        const uint32_t r[5] = {k, (uint32_t)cursor, (uint32_t)b.size(), (uint32_t)b.size(), 0}; memcpy(head.data() + 24 + 20 * i++, r, 20);
        data += b; data.append((size_t)(Align(cursor + b.size()) - cursor - b.size()), '\0'); cursor = Align(cursor + b.size());
    }
    return head + data;
}
static std::string KeyText(uint32_t k) { char b[9]; sprintf_s(b, "%08X", k); return b; }
// One plan operation on Resource/test.xpac. variants: (the other mod's payload SHA-256, this mod's version for it); raw: extra JSON fields as written.
struct XOp { uint32_t key; std::string bytes; bool optional = false; std::vector<std::pair<std::string, std::string>> variants; std::string raw; };
// Removes a case folder from an earlier run. Only below the work folder, never through a link.
static void Wipe(const std::wstring& p) {
    require(_wcsnicmp(p.c_str(), g_work.c_str(), g_work.size()) == 0 && p.size() > g_work.size() + 1, "wipe outside the work folder");
    DWORD a = GetFileAttributesW(p.c_str()); if (a == INVALID_FILE_ATTRIBUTES) return;
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) { require(RemoveDirectoryW(p.c_str()) || DeleteFileW(p.c_str()), "remove link"); return; }
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) { SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL); require(DeleteFileW(p.c_str()) != 0, "delete fixture file"); return; }
    WIN32_FIND_DATAW d; HANDLE f = FindFirstFileW((p + L"\\*").c_str(), &d);
    if (f != INVALID_HANDLE_VALUE) { do { std::wstring n = d.cFileName; if (n != L"." && n != L"..") Wipe(p + L"\\" + n); } while (FindNextFileW(f, &d)); FindClose(f); }
    require(RemoveDirectoryW(p.c_str()) != 0, "remove fixture folder");
}

struct Mod {
    std::wstring folder; std::string id, iniName, sidecar, behavior, iniSchema = "1", plan;
    bool copyGroup = false, badPlanHash = false, noSidecar = true;
    std::wstring runtime = L"fixture_runtime.dll";
    std::string fileGroupBase;   // when set: a "file" group replacing Resource/shared.bin whose original hash is this
    std::vector<XOp> xops;       // when set: an "xpac" group changing Resource/test.xpac (g_xpac)
};
static Mod M(const wchar_t* folder, const char* id) { Mod m; m.folder = folder; m.id = id; return m; }
static Mod& With(Mod& m, const std::string& sidecar) { m.sidecar = sidecar; m.noSidecar = false; return m; }

static std::wstring NewGame(const std::string& name, bool modsFolder = true, const wchar_t* host = L"ChaoGarage.dll", const std::wstring& from = L"") {
    g_case = name;
    auto game = g_work + L"\\" + std::wstring(name.begin(), name.end());
    Wipe(game); Dir(game);
    const auto& pair = from.empty() ? g_out : from;
    require(CopyFileW((pair + L"\\" + host).c_str(), (game + L"\\ChaoGarage.dll").c_str(), FALSE) != 0, "copy host");
    require(CopyFileW((pair + L"\\dinput8.dll").c_str(), (game + L"\\dinput8.dll").c_str(), FALSE) != 0, "copy proxy");
    if (modsFolder) Dir(game + L"\\mods");
    return game;
}
static void AddMod(const std::wstring& game, const Mod& m) {
    auto dir = game + L"\\mods\\" + m.folder;
    Dir(dir + L"\\runtime");
    auto rt = Get(g_out + L"\\" + m.runtime);
    Put(dir + L"\\runtime\\" + m.runtime, rt);
    std::string plan = m.plan;
    if (plan.empty()) {
        plan = "{\"schema\":1,\"archive_groups\":[";
        if (m.copyGroup) {
            std::string payload = "synthetic payload of " + m.id;
            Dir(dir + L"\\payloads"); Put(dir + L"\\payloads\\data.bin", payload);
            plan += "{\"path\":\"Resource/" + m.id + ".bin\",\"format\":\"copy\",\"payload\":\"payloads/data.bin\",\"payload_sha256\":\"" + Sha(payload) + "\"}";
        }
        if (!m.fileGroupBase.empty()) {
            std::string payload = "replacement from " + m.id;
            Dir(dir + L"\\payloads"); Put(dir + L"\\payloads\\shared.bin", payload);
            plan += "{\"path\":\"Resource/shared.bin\",\"format\":\"file\",\"base_sha256\":\"" + m.fileGroupBase + "\",\"payload\":\"payloads/shared.bin\",\"payload_sha256\":\"" + Sha(payload) + "\"}";
        }
        if (!m.xops.empty()) {
            if (plan.back() != '[') plan += ",";
            Dir(dir + L"\\payloads"); auto composed = g_xpac; std::string ops;
            auto file = [&](const std::string& name, const std::string& bytes) { Put(dir + L"\\payloads\\" + std::wstring(name.begin(), name.end()), bytes); return "\"payload\":\"payloads/" + name + "\",\"payload_sha256\":\"" + Sha(bytes) + "\",\"stored_size\":" + std::to_string(bytes.size()); };
            for (auto& o : m.xops) {
                auto original = g_xpac.find(o.key);
                ops += std::string(ops.empty() ? "" : ",") + "{\"key\":\"" + KeyText(o.key) + "\",\"kind\":\"" + (original == g_xpac.end() ? "add\"" : "replace\",\"base_stored_sha256\":\"" + Sha(original->second) + "\"") + "," + file(KeyText(o.key) + ".bin", o.bytes);
                if (o.optional) ops += ",\"optional\":true";
                if (!o.variants.empty()) { ops += ",\"variants\":["; int i = 0;
                    for (auto& [over, bytes] : o.variants) { ops += std::string(i ? "," : "") + "{\"over_payload_sha256\":\"" + over + "\"," + file(KeyText(o.key) + "-v" + std::to_string(i) + ".bin", bytes) + "}"; ++i; }
                    ops += "]"; }
                ops += o.raw + "}";
                composed[o.key] = o.bytes;
            }
            auto out = Xpac(composed);
            plan += "{\"path\":\"Resource/test.xpac\",\"format\":\"xpac\",\"base_sha256\":\"" + Sha(Xpac(g_xpac)) + "\",\"expected_composed_sha256\":\"" + Sha(out) + "\",\"expected_composed_size\":" + std::to_string(out.size()) +
                    ",\"expected_member_count\":" + std::to_string(composed.size()) + ",\"operations\":[" + ops + "]}";
        }
        plan += "]}";
    }
    Put(dir + L"\\plan.json", plan);
    std::string ini = "[mod]\r\nschema=" + m.iniSchema + "\r\nid=" + m.id + "\r\n";
    if (!m.iniName.empty()) ini += "name=" + m.iniName + "\r\n";
    ini += "version=1.0.0\r\nplan=plan.json\r\nplan_sha256=" + (m.badPlanHash ? std::string(64, '0') : Sha(plan)) +
           "\r\nruntime=runtime\\" + U8(m.runtime) + "\r\nruntime_sha256=" + Sha(rt) + "\r\n";
    Put(dir + L"\\mod.ini", ini);
    if (!m.noSidecar) Put(dir + L"\\allstars-mod.json", m.sidecar);
    if (!m.behavior.empty()) Put(dir + L"\\behavior.txt", m.behavior);
}

struct Result { bool ran = false, layout = false; long hr = 0, hr2 = 0; std::vector<std::pair<std::string, std::string>> dialogs; std::vector<std::string> slots, runtimes, caps; std::string release; };
static Result Run(const std::wstring& game, const wchar_t* mode, const std::wstring& extra = L"") {
    Result r;
    auto result = game + L"\\result.json";
    DeleteFileW(result.c_str());
    std::wstring cmd = L"\"" + g_out + L"\\fixture_game.exe\" \"" + game + L"\" " + mode + L" \"" + result + L"\"" + (extra.empty() ? L"" : L" \"" + extra + L"\"");
    STARTUPINFOW si{}; si.cb = sizeof(si); std::wstring desk = L"WinSta0\\" + g_desktop; si.lpDesktop = desk.data();
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> line(cmd.begin(), cmd.end()); line.push_back(0);
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, game.c_str(), &si, &pi)) { Check(false, "start fixture_game"); return r; }
    if (WaitForSingleObject(pi.hProcess, 60000) != WAIT_OBJECT_0) { TerminateProcess(pi.hProcess, 99); Check(false, "fixture_game timed out (a dialog was not dismissed?)"); }
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    Check(code == 0, "fixture_game exit code " + std::to_string(code));
    if (!Exists(result)) { Check(false, "no result file"); return r; }
    auto j = JsonParser(text_file(result)).parse();
    r.ran = true; r.layout = j.at("layout_ok").b;
    Check(r.layout, "fixture address layout");
    r.hr = (long)(int32_t)j.at("hr").u32(); r.hr2 = (long)(int32_t)j.at("hr2").u32();
    for (auto& d : j.at("dialogs").list()) r.dialogs.push_back({d.at("title").str(), d.at("text").str()});
    for (auto& s : j.at("slots").list()) r.slots.push_back(s.str());
    for (auto& s : j.at("loaded_runtimes").list()) r.runtimes.push_back(s.str());
    if (j.has("info_release")) { r.release = j.at("info_release").str(); for (auto& c : j.at("info_capabilities").list()) r.caps.push_back(c.str()); }
    return r;
}
static bool Has(const std::string& s, const std::string& n) { return s.find(n) != std::string::npos; }
static bool AllSlots(const Result& r, const char* state) { if (r.slots.size() != 10) return false; for (auto& s : r.slots) if (s != state) return false; return true; }
// The block of the warning that talks about one mod (from its bullet to the next bullet).
static std::string Entry(const std::string& text, const std::string& who) {
    auto p = text.find(who); if (p == std::string::npos) return {};
    auto start = text.rfind("\xe2\x80\xa2", p); auto end = text.find("\xe2\x80\xa2", p);
    return text.substr(start == std::string::npos ? 0 : start, end == std::string::npos ? std::string::npos : end - (start == std::string::npos ? 0 : start));
}
static const long kWarned = (long)0x8004A010, kStop = (long)0x8004A001;
static const std::string kUrl = "https://github.com/Demaunix/ChaoGarage/releases/latest", kTitle = "ChaoGarage " CHAOGARAGE_RELEASE_A " - mods were not started";
static void OneDialog(const Result& r) { Check(r.dialogs.size() == 1, "exactly one warning, got " + std::to_string(r.dialogs.size())); if (!r.dialogs.empty()) Check(r.dialogs[0].first == kTitle, "warning title: " + r.dialogs[0].first); }
static void NothingStarted(const Result& r, const std::wstring& game) {
    Check(AllSlots(r, "original"), "no file function replaced");
    Check(r.runtimes.empty(), "no mod DLL loaded");
    Check(!Exists(game + L"\\calls.log"), "no mod code ran");
}

int wmain(int argc, wchar_t** argv) {
    try {
        require(argc >= 4, "usage: run_e2e <out> <work> <vectors> [--old-host <dll>]");
        g_out = full(argv[1]); g_work = full(argv[2]); Dir(g_work);
        std::wstring oldHost, prevRelease;
        for (int i = 4; i + 1 < argc; i += 2) { std::wstring flag = argv[i]; if (flag == L"--old-host") oldHost = full(argv[i + 1]); else if (flag == L"--prev-release") prevRelease = full(argv[i + 1]); else require(false, "unknown option"); }
        auto vectors = JsonParser(text_file(argv[3])).parse();
        std::vector<std::string> selfCaps; for (auto& c : vectors.at("self").at("capabilities").list()) selfCaps.push_back(c.str());
        HDESK desk = CreateDesktopW(g_desktop.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        require(desk != nullptr, "create the test desktop");
        const std::string hostSha = hash_file(g_out + L"\\ChaoGarage.dll"), proxySha = hash_file(g_out + L"\\dinput8.dll");

        { auto g = NewGame("e01-empty-mods"); auto r = Run(g, L"v2");
          Check(r.hr == S_FALSE && r.dialogs.empty() && AllSlots(r, "original") && r.runtimes.empty(), "empty mods folder: S_FALSE, silent, nothing changed");
          Check(r.release == CHAOGARAGE_RELEASE_A && r.caps == selfCaps, "ChaoGarageGetLoaderInfo: this release and exactly the vector capabilities"); }
        { auto g = NewGame("e02-no-mods-folder", false); auto r = Run(g, L"v2"); Check(r.hr == S_FALSE && r.dialogs.empty(), "no mods folder: S_FALSE, silent"); }
        { auto g = NewGame("e03-legacy-mod"); auto a = M(L"Alpha", "alpha"); a.copyGroup = true; AddMod(g, a); auto r = Run(g, L"v2");
          Check(r.hr == S_OK && r.dialogs.empty(), "legacy mod without description starts silently");
          Check(AllSlots(r, "hooked") && r.runtimes.size() == 1, "hooks published, one runtime loaded");
          Check(Get(g + L"\\calls.log") == "init Alpha\n", "runtime initialized once");
          Check(Exists(g + L"\\mods\\Alpha\\.cache"), "copy resource prepared");
          Check(Has(Get(g + L"\\mods\\SsasrMods.log"), "READY: ChaoGarage " CHAOGARAGE_RELEASE_A), "log says ready with the release"); }
        { auto g = NewGame("e04-requirements-met"); auto a = M(L"Alpha", "alpha");
          With(a, "{\"manifest_schema\":\"allstars-mod/1\",\"must_understand\":[\"loader-requirements/1\"],\"loader\":{\"host_abi\":1,\"mod_ini_schema\":1,\"plan_schema\":1,\"capabilities\":[\"startup-requirements-check\",\"xpac-member-merge\"],\"host_sha256\":[\"" + hostSha + "\"],\"proxy_sha256\":\"" + proxySha + "\"}}");
          AddMod(g, a); auto r = Run(g, L"v2");
          Check(r.hr == S_OK && r.dialogs.empty() && AllSlots(r, "hooked"), "declared requirements that the shipped pair meets (including its real file hashes) start silently"); }
        { auto g = NewGame("e05-needs-newer"); auto a = M(L"Alpha", "alpha"); a.copyGroup = true; AddMod(g, a);
          auto b = M(L"Beta", "beta"); With(b, "{\"name\":\"Beta Mod\",\"loader\":{\"capabilities\":[\"teleport-v9\"]}}"); AddMod(g, b);
          auto r = Run(g, L"v2"); Check(r.hr == kWarned, "refused with the warned result"); OneDialog(r);
          auto t = r.dialogs.empty() ? std::string() : r.dialogs[0].second;
          Check(Has(t, "The game will start without mods.") && Has(t, "Mod \"Beta Mod\" (folder \"Beta\") needs a newer ChaoGarage loader.") && Has(t, "teleport-v9") && Has(t, kUrl), "names the mod, the missing feature and the download page");
          Check(!Has(t, "Alpha"), "the healthy mod is not blamed");
          NothingStarted(r, g); Check(!Exists(g + L"\\mods\\Alpha\\.cache"), "nothing prepared for any mod (checked before preparation)");
          Check(Has(Get(g + L"\\mods\\SsasrMods.log"), "teleport-v9"), "the log has the same entry"); }
        { auto g = NewGame("e06-consolidated");
          auto a = M(L"Aa", "aa"); With(a, "{\"loader\":{\"host_abi\":2}}"); AddMod(g, a);
          auto b = M(L"Bb", "bb"); With(b, "{\"homepage\":\"https://gamebanana.com/mods/123456\",\"loader\":{\"host_sha256\":\"" + std::string(63, '0') + "1\"}}"); AddMod(g, b);
          auto c = M(L"Cc", "cc"); With(c, "{\"id\":\"cc\",}"); AddMod(g, c);
          AddMod(g, M(L"Dd", "dup")); AddMod(g, M(L"Ee", "dup"));
          auto r = Run(g, L"v2"); Check(r.hr == kWarned, "refused"); OneDialog(r);
          auto t = r.dialogs.empty() ? std::string() : r.dialogs[0].second;
          auto ea = Entry(t, "\"Aa\""), eb = Entry(t, "\"Bb\""), ec = Entry(t, "\"Cc\""), ee = Entry(t, "\"Ee\"");
          Check(Has(ea, "needs a newer ChaoGarage loader") && Has(ea, "host interface version 2") && Has(ea, kUrl), "Aa: newer loader needed, with the download page");
          Check(Has(eb, "was made for a different ChaoGarage loader build") && Has(eb, "https://gamebanana.com/mods/123456") && !Has(eb, kUrl), "Bb: made for another build, points to the mod's own page, not to a loader update");
          Check(Has(ec, "description file (allstars-mod.json) that the loader cannot use") && Has(ec, "Reinstall the mod") && !Has(ec, "Install the newest") && !Has(ec, "delete"), "Cc: unreadable description, no update promise, no advice to delete it");
          Check(Has(ee, "same mod ID") && Has(ee, "\"Dd\""), "Ee: duplicate ID names the other folder");
          NothingStarted(r, g); }
        { auto g = NewGame("e07-load-order"); auto a = M(L"Alpha", "alpha"); With(a, "{\"load_order\":{\"after\":[\"beta\"]}}"); AddMod(g, a); AddMod(g, M(L"Beta", "beta"));
          auto r = Run(g, L"v2"); OneDialog(r); auto t = r.dialogs.empty() ? std::string() : r.dialogs[0].second;
          Check(Has(t, "would start in the wrong order") && Has(t, "rename the folder") && Has(t, "Updating the loader does not change the order"), "order problem explained with the rename fix"); NothingStarted(r, g); }
        { auto g = NewGame("e08-damaged-files"); auto a = M(L"Alpha", "alpha"); a.badPlanHash = true; AddMod(g, a);
          auto r = Run(g, L"v2"); Check(r.hr == kWarned, "refused"); OneDialog(r); auto t = r.dialogs.empty() ? std::string() : r.dialogs[0].second;
          Check(Has(t, "has files that are missing or do not match") && Has(t, "Required file hash mismatch") && Has(t, "Updating the loader does not fix this") && !Has(t, kUrl), "damaged files: reinstall advice, no loader update promise");
          NothingStarted(r, g); }
        { auto g = NewGame("e09-init-failure"); AddMod(g, M(L"Alpha", "alpha")); auto b = M(L"Beta", "beta"); b.behavior = "fail"; AddMod(g, b);
          auto r = Run(g, L"v2"); Check(r.hr == kWarned, "refused"); OneDialog(r); auto t = r.dialogs.empty() ? std::string() : r.dialogs[0].second;
          Check(Has(t, "Mod \"Beta\" reported an error while starting.") && Has(t, "0x80004005") && Has(t, "not known to be a version problem") && !Has(t, kUrl), "runtime failure is not presented as a version problem");
          // as in 1.0: a runtime counts as started once its initialization was called, so the failing one is rolled back too, then the earlier one
          Check(Get(g + L"\\calls.log") == "init Alpha\ninit Beta\nrollback Beta\nrollback Alpha\n", "runtimes rolled back in reverse order, including the one that failed");
          Check(AllSlots(r, "original"), "file functions restored after the failure"); }
        { auto g = NewGame("e10-rollback-failure"); auto a = M(L"Alpha", "alpha"); a.behavior = "rollback-fails"; AddMod(g, a); auto b = M(L"Beta", "beta"); b.behavior = "fail"; AddMod(g, b);
          auto r = Run(g, L"v2"); Check(r.hr == kStop, "rollback failure stops the start (0x8004A001)"); OneDialog(r); auto t = r.dialogs.empty() ? std::string() : r.dialogs[0].second;
          Check(Has(t, "The game start was stopped to protect your files.") && Has(t, "Mod \"Alpha\" could not undo its changes"), "stop explained, naming the mod whose rollback failed"); }
        { auto g = NewGame("e11-unrecoverable"); auto a = M(L"Alpha", "alpha"); a.behavior = "unrecoverable"; AddMod(g, a);
          auto r = Run(g, L"v2"); Check(r.hr == kStop, "a runtime's unrecoverable result stops the start"); OneDialog(r);
          Check(!r.dialogs.empty() && Has(r.dialogs[0].second, "The game start was stopped"), "stop explained"); }
        { auto g = NewGame("e12-old-proxy"); auto a = M(L"Alpha", "alpha"); a.copyGroup = true; AddMod(g, a);
          auto r = Run(g, L"v1"); Check(r.hr == kWarned, "a 1.0 dinput8.dll (bootstrap version 1) is refused"); OneDialog(r);
          Check(!r.dialogs.empty() && Has(r.dialogs[0].second, "ChaoGarage.dll and dinput8.dll are from different releases.") && Has(r.dialogs[0].second, "dinput8.dll is from an earlier release (before 1.1.0)") && Has(r.dialogs[0].second, kUrl), "pair mismatch explained");
          NothingStarted(r, g); Check(!Exists(g + L"\\mods\\Alpha\\.cache"), "nothing prepared"); }
        { auto g = NewGame("e13-other-release"); AddMod(g, M(L"Alpha", "alpha")); auto r = Run(g, L"v2-other"); OneDialog(r);
          Check(!r.dialogs.empty() && Has(r.dialogs[0].second, "dinput8.dll is release 9.9.9"), "names the other release"); NothingStarted(r, g); }
        { auto g = NewGame("e14-unnamed-release"); AddMod(g, M(L"Alpha", "alpha")); auto r = Run(g, L"v2-null"); OneDialog(r);
          Check(!r.dialogs.empty() && Has(r.dialogs[0].second, "dinput8.dll is release unknown"), "missing release name handled"); NothingStarted(r, g); }
        { auto g = NewGame("e15-bad-flags"); AddMod(g, M(L"Alpha", "alpha")); auto r = Run(g, L"bad-flags");
          Check(r.hr == E_INVALIDARG && r.dialogs.empty(), "an invalid start-up context is rejected silently (no UI from an unverified caller)"); NothingStarted(r, g); }
        { auto g = NewGame("e16-gate-inactive"); AddMod(g, M(L"Alpha", "alpha")); auto r = Run(g, L"inactive");
          Check(r.hr == E_INVALIDARG && r.dialogs.empty(), "an expired start-up gate is rejected silently"); NothingStarted(r, g); }
        { auto g = NewGame("e17-twice"); AddMod(g, M(L"Alpha", "alpha")); auto r = Run(g, L"twice");
          Check(r.hr == S_OK && r.hr2 == (long)HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED) && r.dialogs.empty(), "a second call is refused without UI"); }
        { auto g = NewGame("e18-untrusted-text"); auto a = M(L"Alpha", "alpha");
          With(a, "{\"name\":\"Evil\xe2\x80\xae\\u0007txt.exe" + std::string(300, 'x') + "\",\"loader\":{\"capabilities\":[\"teleport-v9\"]}}"); AddMod(g, a);
          auto r = Run(g, L"v2"); OneDialog(r); auto t = r.dialogs.empty() ? std::string() : r.dialogs[0].second;
          Check(!Has(t, "\xe2\x80\xae") && !Has(t, "\x07") && Has(t, "\xe2\x80\xa6") && !Has(t, std::string(60, 'x')), "mod name cleaned and shortened"); }
        { auto g = NewGame("e19-metal-first"); AddMod(g, M(L"Alpha", "alpha")); AddMod(g, M(L"Zeta", "metal-sonic"));
          auto r = Run(g, L"v2"); Check(r.hr == S_OK && Get(g + L"\\calls.log") == "init Zeta\ninit Alpha\n", "metal-sonic starts first, then folder order"); }
        { auto g = NewGame("e20-modini-v2"); auto a = M(L"Alpha", "alpha"); a.iniSchema = "2"; AddMod(g, a);
          auto r = Run(g, L"v2"); OneDialog(r); Check(!r.dialogs.empty() && Has(r.dialogs[0].second, "mod.ini uses format version 2") && Has(r.dialogs[0].second, kUrl), "newer mod.ini format: update the loader"); NothingStarted(r, g); }
        { auto g = NewGame("e21-plan-v2"); auto a = M(L"Alpha", "alpha"); a.plan = "{\"schema\":2,\"archive_groups\":[]}"; AddMod(g, a);
          auto r = Run(g, L"v2"); OneDialog(r); Check(!r.dialogs.empty() && Has(r.dialogs[0].second, "plan.json uses format version 2") && Has(r.dialogs[0].second, kUrl), "newer plan format: update the loader"); NothingStarted(r, g); }
        { auto g = NewGame("e22-target-descriptive"); auto a = M(L"Alpha", "alpha"); With(a, "{\"target\":{\"executable_sha256\":\"" + std::string(64, 'e') + "\"}}"); AddMod(g, a);
          auto r = Run(g, L"v2"); Check(r.hr == S_OK && r.dialogs.empty(), "another target build is descriptive only");
          Check(Has(Get(g + L"\\mods\\SsasrMods.log"), "descriptive only"), "and is logged"); }
        { auto g = NewGame("e23-not-a-runtime"); auto a = M(L"Alpha", "alpha"); a.runtime = L"fixture_inert_host.dll"; AddMod(g, a);
          auto r = Run(g, L"v2"); OneDialog(r); Check(!r.dialogs.empty() && Has(r.dialogs[0].second, "does not export SsasrModInitialize and SsasrModRollback") && !Has(r.dialogs[0].second, kUrl), "a DLL without the entry points is explained");
          Check(AllSlots(r, "original"), "nothing published"); }
        { auto g = NewGame("e24-same-file"); std::string original = "synthetic original file"; Dir(g + L"\\Resource"); Put(g + L"\\Resource\\shared.bin", original);
          auto a = M(L"Alpha", "alpha"); a.fileGroupBase = Sha(original); AddMod(g, a); auto b = M(L"Beta", "beta"); b.fileGroupBase = Sha(original); AddMod(g, b);
          auto r = Run(g, L"v2"); OneDialog(r); auto t = r.dialogs.empty() ? std::string() : r.dialogs[0].second;
          Check(Has(t, "cannot be used together with another installed mod") && Has(t, "Two mods replace the same resource") && !Has(t, kUrl), "two mods replacing one file: conflict, not a version problem"); NothingStarted(r, g); }
        { auto g = NewGame("e25-foreign-description"); auto a = M(L"Alpha", "alpha"); With(a, "{\"id\":\"someone-else\",\"loader\":{\"host_abi\":1}}"); AddMod(g, a);
          auto r = Run(g, L"v2"); OneDialog(r); Check(!r.dialogs.empty() && Has(r.dialogs[0].second, "is for another mod"), "description for another mod with rules is refused"); NothingStarted(r, g); }
        { auto g = NewGame("e26-example-runtime"); auto a = M(L"Example", "example"); a.runtime = L"example_runtime.dll";
          With(a, "{\"loader\":{\"capabilities\":[\"startup-requirements-check\",\"xpac-member-merge\"]}}"); AddMod(g, a);
          auto r = Run(g, L"v2"); Check(r.hr == S_OK && r.dialogs.empty() && r.runtimes.size() == 1, "the example runtime's own check passes under 1.1.0"); }
        { auto g = NewGame("e27-runtime-check-old-host", true, L"fixture_legacy_host.dll"); Put(g + L"\\example_runtime.dll", Get(g_out + L"\\example_runtime.dll"));
          auto r = Run(g, L"runtime-only", g + L"\\example_runtime.dll");
          Check(r.hr == E_FAIL, "under a host without loader info the runtime refuses");
          Check(r.dialogs.size() == 1 && r.dialogs[0].first == "Example Mod - loader too old" && Has(r.dialogs[0].second, "older than ChaoGarage 1.1.0") && Has(r.dialogs[0].second, kUrl), "and tells the player which loader to get"); }
        { auto g = NewGame("e28-runtime-check-new-host"); Put(g + L"\\example_runtime.dll", Get(g_out + L"\\example_runtime.dll"));
          auto r = Run(g, L"runtime-only", g + L"\\example_runtime.dll"); Check(r.hr == S_OK && r.dialogs.empty(), "the runtime check passes against the shipped 1.1.0 host"); }
        { auto g = NewGame("e29-plain-http-homepage"); auto a = M(L"Alpha", "alpha"); With(a, "{\"homepage\":\"http://example.com/evil\",\"loader\":{\"host_sha256\":\"" + std::string(64, '1') + "\"}}"); AddMod(g, a);
          auto r = Run(g, L"v2"); OneDialog(r); auto t = r.dialogs.empty() ? std::string() : r.dialogs[0].second;
          Check(!Has(t, "http://example.com/evil") && Has(t, "where you downloaded it"), "only https:// homepages are shown"); }
        { auto g = NewGame("e30-junction"); Dir(g + L"\\elsewhere"); auto cmd = L"cmd.exe /c mklink /J \"" + g + L"\\mods\\Linked\" \"" + g + L"\\elsewhere\" >nul";
          _wsystem(cmd.c_str()); Check(Exists(g + L"\\mods\\Linked"), "junction fixture created");
          auto r = Run(g, L"v2"); OneDialog(r); Check(!r.dialogs.empty() && Has(r.dialogs[0].second, "is a link (junction)"), "a junction in mods is explained"); NothingStarted(r, g); }
        // a description that exists but is not a plain readable file may hide a requirement: refused before anything is prepared (review N1)
        auto sidecarCase = [&](const char* name, const std::function<void(const std::wstring& modDir, const std::wstring& game)>& make, const char* says) {
            auto g = NewGame(name); auto a = M(L"Alpha", "alpha"); a.copyGroup = true; AddMod(g, a); AddMod(g, M(L"Beta", "beta"));
            make(g + L"\\mods\\Beta", g);
            auto r = Run(g, L"v2"); Check(r.hr == kWarned, "refused"); OneDialog(r);
            auto t = r.dialogs.empty() ? std::string() : r.dialogs[0].second;
            Check(Has(t, "Mod \"Beta\" has a description file (allstars-mod.json) that the loader cannot use.") && Has(t, says) && !Has(t, kUrl), std::string("explained: ") + says);
            NothingStarted(r, g); Check(!Exists(g + L"\\mods\\Alpha\\.cache"), "nothing prepared for any mod");
        };
        sidecarCase("e31-description-is-folder", [&](const std::wstring& d, const std::wstring&) { Dir(d + L"\\allstars-mod.json"); }, "is a folder, not a description file");
        sidecarCase("e32-description-is-junction", [&](const std::wstring& d, const std::wstring& g) {
            Dir(g + L"\\elsewhere"); _wsystem((L"cmd.exe /c mklink /J \"" + d + L"\\allstars-mod.json\" \"" + g + L"\\elsewhere\" >nul").c_str());
            Check(Exists(d + L"\\allstars-mod.json"), "junction fixture created"); }, "is a link");
        {   // a file symbolic link to a description with an unmet requirement: only when Windows lets this account create one (Developer Mode)
            auto probe = g_work + L"\\symlink-probe"; Wipe(probe); Dir(probe); Put(probe + L"\\target.json", "{}");
            bool can = CreateSymbolicLinkW((probe + L"\\link.json").c_str(), (probe + L"\\target.json").c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
            Wipe(probe);
            if (can) sidecarCase("e33-description-is-file-link", [&](const std::wstring& d, const std::wstring& g) {
                Put(g + L"\\target.json", "{\"loader\":{\"host_abi\":2}}");
                Check(CreateSymbolicLinkW((d + L"\\allstars-mod.json").c_str(), (g + L"\\target.json").c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0, "file link created"); }, "is a link");
            else std::printf("SKIP [e33-description-is-file-link] this account cannot create file symbolic links (Developer Mode off); the attribute rule is covered by test_compat\n");
        }
        {   // a description another program holds open exclusively: cannot be read, so the mod is refused
            auto g = NewGame("e34-description-locked"); auto a = M(L"Alpha", "alpha"); a.copyGroup = true; AddMod(g, a);
            auto b = M(L"Beta", "beta"); With(b, "{\"loader\":{\"host_abi\":2}}"); AddMod(g, b);
            Handle lock(CreateFileW((g + L"\\mods\\Beta\\allstars-mod.json").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr));
            Check(lock.h != INVALID_HANDLE_VALUE, "lock fixture");
            auto r = Run(g, L"v2"); OneDialog(r); auto t = r.dialogs.empty() ? std::string() : r.dialogs[0].second;
            Check(Has(t, "could not be opened (Windows error 32)") && Has(t, "that the loader cannot use"), "an unreadable description is refused, never read as no requirement");
            NothingStarted(r, g); Check(!Exists(g + L"\\mods\\Alpha\\.cache"), "nothing prepared");
        }
        // 1.2.0: one archive changed by several mods, read back through the loader's redirection as the game would read it.
        auto xgame = [&](const char* name) { auto g = NewGame(name); Dir(g + L"\\Resource"); Put(g + L"\\Resource\\test.xpac", Xpac(g_xpac)); Put(g + L"\\read-back.txt", "Resource\\test.xpac"); return g; };
        auto xmod = [&](const wchar_t* folder, const char* id, std::vector<XOp> ops) { auto m = M(folder, id); m.xops = std::move(ops); return m; };
        auto composed = [&](std::map<uint32_t, std::string> changes) { auto m = g_xpac; for (auto& [k, b] : changes) m[k] = b; return Xpac(m); };
        auto started = [&](const std::wstring& g, const Result& r, const std::string& expected, const std::string& what) {
            Check(r.hr == S_OK && r.dialogs.empty() && AllSlots(r, "hooked"), what + ": started without a message");
            Check(Get(g + L"\\read-back.bin") == expected, what + ": the game reads the expected archive"); };
        auto refused = [&](const std::wstring& g, const Result& r, const std::string& says, const std::string& what) {
            Check(r.hr == kWarned, what + ": refused"); OneDialog(r); auto t = r.dialogs.empty() ? std::string() : r.dialogs[0].second;
            Check(Has(t, says) && !Has(t, kUrl), what + ": the message says \"" + says + "\" and is not a loader update"); NothingStarted(r, g); };
        auto logged = [&](const std::wstring& g, const std::string& line) { Check(Has(Get(g + L"\\mods\\SsasrMods.log"), line), "log: " + line); };
        const std::string A20 = "alpha's twenty", B20 = "beta's twenty", AB20 = "alpha's and beta's twenty";
        { auto g = xgame("x01-different-members"); AddMod(g, xmod(L"Alpha", "alpha", {{0x10, "alpha's ten"}})); AddMod(g, xmod(L"Beta", "beta", {{0x30, "beta's thirty"}}));
          auto r = Run(g, L"v2"); started(g, r, composed({{0x10, "alpha's ten"}, {0x30, "beta's thirty"}}), "two mods, different members (as in 1.1.0)"); }
        { auto g = xgame("x02-same-member"); AddMod(g, xmod(L"Alpha", "alpha", {{0x20, A20}})); AddMod(g, xmod(L"Beta", "beta", {{0x20, B20}}));
          auto r = Run(g, L"v2"); refused(g, r, "Two mods change the same archive member (alpha and beta, member 00000020)", "same member, nothing declared");
          Check(!r.dialogs.empty() && Has(r.dialogs[0].second, "cannot be used together with another installed mod"), "still the conflict message"); }
        { auto g = xgame("x03-variant-of-the-later-mod"); AddMod(g, xmod(L"Alpha", "alpha", {{0x10, "alpha's ten"}, {0x20, A20}}));
          AddMod(g, xmod(L"Beta", "beta", {{0x20, B20, false, {{Sha(A20), AB20}}}, {0x30, "beta's thirty"}}));
          auto r = Run(g, L"v2"); started(g, r, composed({{0x10, "alpha's ten"}, {0x20, AB20}, {0x30, "beta's thirty"}}), "the later mod's variant for the earlier one's content");
          logged(g, "Resource\\test.xpac, member 00000020: beta's version made for alpha's change is used (both changes)"); }
        { auto g = xgame("x04-variant-of-the-earlier-mod"); AddMod(g, xmod(L"Alpha", "alpha", {{0x20, A20, false, {{Sha(B20), AB20}}}})); AddMod(g, xmod(L"Beta", "beta", {{0x20, B20}}));
          auto r = Run(g, L"v2"); started(g, r, composed({{0x20, AB20}}), "the earlier mod's variant for the later one's content");
          logged(g, "member 00000020: alpha's version made for beta's change is used (both changes)"); }
        { auto g = xgame("x05-variant-for-other-content"); AddMod(g, xmod(L"Alpha", "alpha", {{0x20, A20}})); AddMod(g, xmod(L"Beta", "beta", {{0x20, B20, false, {{Sha("an older alpha's twenty"), AB20}}}}));
          auto r = Run(g, L"v2"); refused(g, r, "Two mods change the same archive member (alpha and beta, member 00000020)", "a variant for another version of the other mod is not used"); }
        { auto g = xgame("x06-later-optional"); AddMod(g, xmod(L"Alpha", "alpha", {{0x20, A20}})); AddMod(g, xmod(L"Beta", "beta", {{0x20, B20, true}, {0x30, "beta's thirty"}}));
          auto r = Run(g, L"v2"); started(g, r, composed({{0x20, A20}, {0x30, "beta's thirty"}}), "the later mod's optional change gives way, its others stay");
          logged(g, "member 00000020: beta's optional change is skipped; alpha's is used"); }
        { auto g = xgame("x07-earlier-optional"); AddMod(g, xmod(L"Alpha", "alpha", {{0x20, A20, true}})); AddMod(g, xmod(L"Beta", "beta", {{0x20, B20}}));
          auto r = Run(g, L"v2"); started(g, r, composed({{0x20, B20}}), "the earlier mod's optional change gives way");
          logged(g, "member 00000020: alpha's optional change is skipped; beta's is used"); }
        { auto g = xgame("x08-both-optional"); AddMod(g, xmod(L"Alpha", "alpha", {{0x20, A20, true}})); AddMod(g, xmod(L"Beta", "beta", {{0x20, B20, true}}));
          auto r = Run(g, L"v2"); started(g, r, composed({{0x20, A20}}), "both optional: the earlier (folder order) is kept"); }
        { auto g = xgame("x09-variant-before-optional"); AddMod(g, xmod(L"Alpha", "alpha", {{0x20, A20}})); AddMod(g, xmod(L"Beta", "beta", {{0x20, B20, true, {{Sha(A20), AB20}}}}));
          auto r = Run(g, L"v2"); started(g, r, composed({{0x20, AB20}}), "a fitting variant is used even when the change is optional"); }
        { auto g = xgame("x10-two-different-variants"); AddMod(g, xmod(L"Alpha", "alpha", {{0x20, A20, false, {{Sha(B20), "alpha's mix"}}}})); AddMod(g, xmod(L"Beta", "beta", {{0x20, B20, false, {{Sha(A20), "beta's mix"}}}}));
          auto r = Run(g, L"v2"); refused(g, r, "each has its own version made for the other's", "two different combined versions are not guessed between"); }
        { auto g = xgame("x11-two-equal-variants"); AddMod(g, xmod(L"Alpha", "alpha", {{0x20, A20, false, {{Sha(B20), AB20}}}})); AddMod(g, xmod(L"Beta", "beta", {{0x20, B20, false, {{Sha(A20), AB20}}}}));
          auto r = Run(g, L"v2"); started(g, r, composed({{0x20, AB20}}), "two identical combined versions"); }
        { auto g = xgame("x12-three-mods"); AddMod(g, xmod(L"Alpha", "alpha", {{0x20, A20}})); AddMod(g, xmod(L"Beta", "beta", {{0x20, B20, false, {{Sha(A20), AB20}}}}));
          AddMod(g, xmod(L"Gamma", "gamma", {{0x20, "gamma's twenty", false, {{Sha(A20), "gamma on alpha"}, {Sha(AB20), "all three"}}}}));
          auto r = Run(g, L"v2"); started(g, r, composed({{0x20, "all three"}}), "a third mod's variant for the combined content");
          logged(g, "member 00000020: gamma's version made for alpha + beta's change is used (both changes)"); }
        { auto g = xgame("x13-third-without-variant"); AddMod(g, xmod(L"Alpha", "alpha", {{0x20, A20}})); AddMod(g, xmod(L"Beta", "beta", {{0x20, B20, false, {{Sha(A20), AB20}}}}));
          AddMod(g, xmod(L"Gamma", "gamma", {{0x20, "gamma's twenty", false, {{Sha(A20), "gamma on alpha"}}}}));
          auto r = Run(g, L"v2"); refused(g, r, "Two mods change the same archive member (alpha + beta and gamma, member 00000020)", "a variant for one part of a combination is not used"); }
        { auto g = xgame("x14-added-member"); AddMod(g, xmod(L"Alpha", "alpha", {{0x40, "alpha's forty"}})); AddMod(g, xmod(L"Beta", "beta", {{0x40, "beta's forty", false, {{Sha("alpha's forty"), "both forties"}}}}));
          auto r = Run(g, L"v2"); started(g, r, composed({{0x40, "both forties"}}), "a member both mods add"); }
        { auto g = xgame("x15-optional-wrong-type"); AddMod(g, xmod(L"Alpha", "alpha", {{0x20, A20}})); AddMod(g, xmod(L"Beta", "beta", {{0x30, "beta's thirty", false, {}, ",\"optional\":\"yes\""}}));
          auto r = Run(g, L"v2"); refused(g, r, "Invalid optional flag", "a wrongly typed optional flag is never read as absent"); }
        { auto g = xgame("x16-variant-hash-in-capitals"); AddMod(g, xmod(L"Alpha", "alpha", {{0x20, A20}}));
          std::string upper = Sha(A20); for (auto& ch : upper) ch = (char)toupper((unsigned char)ch);
          AddMod(g, xmod(L"Beta", "beta", {{0x20, B20, false, {{upper, AB20}}}}));
          auto r = Run(g, L"v2"); refused(g, r, "Invalid variant over_payload_sha256", "a variant hash must be exact"); }
        { auto g = xgame("x17-variant-payload-damaged"); AddMod(g, xmod(L"Alpha", "alpha", {{0x20, A20}})); AddMod(g, xmod(L"Beta", "beta", {{0x20, B20, false, {{Sha(A20), AB20}}}}));
          Put(g + L"\\mods\\Beta\\payloads\\00000020-v0.bin", "damaged");
          auto r = Run(g, L"v2"); refused(g, r, "has files that are missing or do not match", "a damaged variant payload is refused even when unused elsewhere"); }
        { auto g = xgame("x18-variants-not-a-list"); AddMod(g, xmod(L"Beta", "beta", {{0x20, B20, false, {}, ",\"variants\":{}"}}));
          auto r = Run(g, L"v2"); refused(g, r, "Expected array", "variants must be a list"); }
        { auto g = xgame("x19-metal-and-multiplayer"); AddMod(g, xmod(L"Metal Sonic", "metal-sonic", {{0x20, "metal's twenty"}, {0x30, "metal's thirty"}}));
          auto mp = xmod(L"Steam Multiplayer", "steam-multiplayer", {{0x20, "keys on the original", false, {{Sha("metal's twenty"), "keys on metal's"}}}, {0x10, "steam tiles"}});
          With(mp, "{\"manifest_schema\":\"allstars-mod/1\",\"must_understand\":[\"loader-requirements/1\"],\"loader\":{\"capabilities\":[\"xpac-member-variants\",\"xpac-member-optional\"]}}"); AddMod(g, mp);
          auto r = Run(g, L"v2"); started(g, r, composed({{0x10, "steam tiles"}, {0x20, "keys on metal's"}, {0x30, "metal's thirty"}}), "Metal Sonic and the multiplayer mod's key pictures");
          Check(Get(g + L"\\calls.log") == "init Metal Sonic\ninit Steam Multiplayer\n", "both runtimes started, metal-sonic first");
          logged(g, "member 00000020: steam-multiplayer's version made for metal-sonic's change is used (both changes)"); }
        { auto g = xgame("x20-multiplayer-alone"); auto mp = xmod(L"Steam Multiplayer", "steam-multiplayer", {{0x20, "keys on the original", false, {{Sha("metal's twenty"), "keys on metal's"}}}});
          AddMod(g, mp); auto r = Run(g, L"v2"); started(g, r, composed({{0x20, "keys on the original"}}), "the multiplayer mod alone uses its own payload"); }
        // An earlier mod's variant after it was merged once already (its variant payload moved with its sources; review of 1.2.0).
        { auto g = xgame("x21-variant-after-a-merge"); AddMod(g, xmod(L"Alpha", "alpha", {{0x10, "alpha's ten"}}));
          AddMod(g, xmod(L"Beta", "beta", {{0x20, B20, false, {{Sha("gamma's twenty"), "beta on gamma"}}}})); AddMod(g, xmod(L"Gamma", "gamma", {{0x20, "gamma's twenty"}}));
          auto r = Run(g, L"v2"); started(g, r, composed({{0x10, "alpha's ten"}, {0x20, "beta on gamma"}}), "a variant of a mod merged before the other one");
          logged(g, "member 00000020: beta's version made for gamma's change is used (both changes)"); }
        { auto g = xgame("x22-variant-after-an-optional"); AddMod(g, xmod(L"Alpha", "alpha", {{0x20, A20, true}}));
          AddMod(g, xmod(L"Beta", "beta", {{0x20, B20, false, {{Sha("gamma's twenty"), "beta on gamma"}}}})); AddMod(g, xmod(L"Gamma", "gamma", {{0x20, "gamma's twenty"}}));
          auto r = Run(g, L"v2"); started(g, r, composed({{0x20, "beta on gamma"}}), "a variant of the change an optional one gave way to"); }
        if (!prevRelease.empty()) {
            // The previous release (1.1.0) meeting a mod that needs 1.2.0: the player is told to update the loader, before anything starts.
            { auto g = NewGame("p03-prev-release-needs-update", true, L"ChaoGarage.dll", prevRelease); Dir(g + L"\\Resource"); Put(g + L"\\Resource\\test.xpac", Xpac(g_xpac));
              AddMod(g, xmod(L"Metal Sonic", "metal-sonic", {{0x20, "metal's twenty"}}));
              auto mp = xmod(L"Steam Multiplayer", "steam-multiplayer", {{0x20, "keys on the original", false, {{Sha("metal's twenty"), "keys on metal's"}}}});
              With(mp, "{\"name\":\"Steam Multiplayer\",\"loader\":{\"capabilities\":[\"xpac-member-variants\"]}}"); AddMod(g, mp);
              auto r = Run(g, L"v2-host-release"); Check(r.hr == kWarned && r.dialogs.size() == 1, "1.1.0: refused with one message");
              auto t = r.dialogs.empty() ? std::string() : r.dialogs[0].second;
              Check(Has(Entry(t, "Steam Multiplayer"), "needs a newer ChaoGarage loader.") && Has(t, "xpac-member-variants") && Has(t, kUrl) && !Has(t, "Metal Sonic"), "1.1.0 tells the player to install a newer loader, with the download page, and does not blame Metal Sonic");
              NothingStarted(r, g); }
            { auto g = NewGame("p04-prev-release-undeclared", true, L"ChaoGarage.dll", prevRelease); Dir(g + L"\\Resource"); Put(g + L"\\Resource\\test.xpac", Xpac(g_xpac));
              AddMod(g, xmod(L"Alpha", "alpha", {{0x20, A20}})); AddMod(g, xmod(L"Beta", "beta", {{0x20, B20, false, {{Sha(A20), AB20}}}}));
              auto r = Run(g, L"v2-host-release"); Check(r.hr == kWarned && r.dialogs.size() == 1 && Has(r.dialogs[0].second, "cannot be used together"), "1.1.0 ignores variants: a mod using them must declare the capability (else this conflict)");
              NothingStarted(r, g); }
        }
        if (!oldHost.empty()) {
            // Private evidence only (needs a 1.0 ChaoGarage.dll): what an already-released host does with new mods.
            { auto g = NewGame("p01-old-host-runtime-check"); require(CopyFileW(oldHost.c_str(), (g + L"\\ChaoGarage.dll").c_str(), FALSE) != 0, "copy old host");
              auto a = M(L"Example", "example"); a.runtime = L"example_runtime.dll"; With(a, "{\"loader\":{\"capabilities\":[\"startup-requirements-check\"]}}"); AddMod(g, a);
              auto r = Run(g, L"v1"); Check(r.hr == E_FAIL, "1.0 host + mod whose runtime checks the loader: mods not started");
              Check(r.dialogs.size() == 2 && r.dialogs[0].first == "Example Mod - loader too old" && Has(r.dialogs[1].second, "The mods could not be enabled"), "the runtime's message, then the 1.0 host's own short message");
              Check(AllSlots(r, "original"), "the 1.0 host rolled back its file functions"); }
            { auto g = NewGame("p02-old-host-ignores-description"); require(CopyFileW(oldHost.c_str(), (g + L"\\ChaoGarage.dll").c_str(), FALSE) != 0, "copy old host");
              auto a = M(L"Alpha", "alpha"); With(a, "{\"loader\":{\"capabilities\":[\"teleport-v9\"],\"host_abi\":2}}"); AddMod(g, a);
              auto r = Run(g, L"v1"); Check(r.hr == S_OK && r.dialogs.empty() && Get(g + L"\\calls.log") == "init Alpha\n", "a 1.0 host never reads allstars-mod.json: declared requirements are not checked there (documented limit)"); }
        }
        CloseDesktop(desk);
        std::printf("%s: %u passed, %u failed (end-to-end, shipped ChaoGarage.dll %s)\n", g_fail ? "FAIL" : "PASS", g_pass, g_fail, hostSha.c_str());
        return g_fail ? 1 : 0;
    } catch (const std::exception& e) { std::printf("FAIL [%s] exception: %s\n", g_case.c_str(), e.what()); return 2; }
}
