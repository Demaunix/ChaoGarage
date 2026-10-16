// Runs tests/vectors/loader-requirements.json through the loader's own reader (compat.h) and checks the
// vector file's "self" against the capability list the host is built with. No files of any game are used.
#include "files.h"
#include "compat.h"
#include "capabilities.h"
#include "chaogarage_release.h"
#include <cstdio>
using namespace ssasr;

static unsigned passed = 0, failed = 0;
static void check(bool ok, const std::string& what) { if (ok) ++passed; else { ++failed; std::printf("FAIL %s\n", what.c_str()); } }

static std::string replaceAll(std::string s, const std::string& a, const std::string& b) { for (size_t p = 0; (p = s.find(a, p)) != std::string::npos; p += b.size()) s.replace(p, a.size(), b); return s; }
static std::string fromHex(const std::string& h) { std::string r; for (size_t i = 0; i + 1 < h.size(); i += 2) r += (char)std::stoi(h.substr(i, 2), nullptr, 16); return r; }
static const std::string kHost(64, 'a'), kProxy(64, 'b');

static std::string caseBytes(const Json& c, const std::string& other) {
    if (c.has("hex")) return fromHex(c.at("hex").str());
    if (c.has("repeat")) { auto& r = c.at("repeat"); return r.at("prefix").str() + std::string(r.at("count").u32(), r.at("fill").str()[0]) + r.at("suffix").str(); }
    if (c.has("nest")) { unsigned n = c.at("nest").u32(); return "{\"x\":" + std::string(n, '[') + std::string(n, ']') + "}"; }
    auto t = c.at("text").str();
    t = replaceAll(t, "$HOST_UPPER", std::string(64, 'A'));
    t = replaceAll(t, "$HOST", kHost);
    t = replaceAll(t, "$PROXY", kProxy);
    return replaceAll(t, "$OTHER", other);
}
static const char* kindName(compat::Kind k) {
    switch (k) { case compat::Kind::NeedsNewerLoader: return "needs-newer-loader"; case compat::Kind::MadeForOtherLoader: return "made-for-other-loader";
    case compat::Kind::DescriptionUnusable: return "description-unusable"; default: return "load-order"; }
}

int wmain(int argc, wchar_t** argv) {
    try {
        require(argc == 2, "usage: test_compat <loader-requirements.json>");
        auto v = JsonParser(text_file(argv[1])).parse();
        require(v.at("schema").str() == "chaogarage-loader-vectors/1", "vector schema");
        auto& self = v.at("self");
        // The vectors describe exactly what this host build provides.
        std::vector<std::string> listed; for (auto& c : self.at("capabilities").list()) listed.push_back(c.str());
        std::vector<std::string> built; for (auto k = kCapabilities; *k; ++k) built.push_back(*k);
        check(listed == built, "vector capabilities equal the host's kCapabilities");
        check(self.at("release").str() == CHAOGARAGE_RELEASE_A && self.at("host_abi").u32() == 1 && self.at("mod_ini_schema").u32() == 1 && self.at("plan_schema").u32() == 1, "vector self matches the release");
        compat::Self me{1, 1, 1, kCapabilities, CHAOGARAGE_RELEASE_A, kRetiredCapabilities};
        auto other = self.at("other_sha256").str();
        unsigned hostCalls = 0;
        for (auto& c : v.at("cases").list()) {
            auto name = c.at("name").str();
            auto expect = c.at("loader").str();
            compat::Description d;
            if (!c.has("absent")) d = compat::read(caseBytes(c, other), "vec-mod");
            compat::checkLoader(d, me, [&] { ++hostCalls; return kHost; }, [&] { return kProxy; });
            std::string got = d.usable ? "start" : std::string("refuse:") + kindName(d.problems.front().kind);
            check(got == expect, name + ": expected " + expect + ", got " + got + (d.problems.empty() ? "" : " (" + d.problems.front().detail + ")"));
            if (!d.usable) for (auto& p : d.problems) check(!p.detail.empty() && p.detail.size() < 600, name + ": problem text present and bounded");
        }
        check(hostCalls > 0, "host hash consulted only for pinned cases (and at least once)");
        for (auto& oc : v.at("order_cases").list()) {
            auto name = oc.at("name").str();
            std::vector<compat::Description> descs;
            std::vector<std::pair<std::string, std::string>> mods;   // folder, id
            for (auto& m : oc.at("mods").list()) {
                mods.push_back({m.at("folder").str(), m.at("id").str()});
                descs.push_back(m.has("text") ? compat::read(m.at("text").str(), m.at("id").str()) : compat::Description{});
            }
            // the loader's order: folder names ordinally, then metal-sonic first (stable)
            std::vector<size_t> at(mods.size()); for (size_t i = 0; i < at.size(); ++i) at[i] = i;
            std::sort(at.begin(), at.end(), [&](size_t a, size_t b) { return mods[a].first < mods[b].first; });
            std::stable_sort(at.begin(), at.end(), [&](size_t a, size_t b) { return (mods[a].second == "metal-sonic") > (mods[b].second == "metal-sonic"); });
            std::vector<compat::Started> order; for (auto i : at) order.push_back({mods[i].second, mods[i].first, &descs[i]});
            std::vector<std::string> refused;
            for (auto& [k, p] : compat::checkOrder(order)) if (std::find(refused.begin(), refused.end(), order[k].folder) == refused.end()) refused.push_back(order[k].folder);
            std::vector<std::string> want; for (auto& r : oc.at("refused").list()) want.push_back(r.str());
            std::sort(refused.begin(), refused.end()); std::sort(want.begin(), want.end());
            check(refused == want, "order: " + name);
        }
        // only a confirmed absence makes a legacy mod; a folder, a link or a name that cannot be inspected is refused (review N1)
        struct P { uint32_t attributes, error; compat::Presence want; const char* what; };
        for (auto& p : {P{0xFFFFFFFFu, 2, compat::Presence::Absent, "file not found"}, P{0xFFFFFFFFu, 3, compat::Presence::Absent, "path not found"},
                        P{0xFFFFFFFFu, 5, compat::Presence::Refused, "access denied"}, P{0xFFFFFFFFu, 32, compat::Presence::Refused, "sharing violation"},
                        P{0xFFFFFFFFu, 123, compat::Presence::Refused, "invalid name"}, P{0x10, 0, compat::Presence::Refused, "a folder"},
                        P{0x400 | 0x20, 0, compat::Presence::Refused, "a file link"}, P{0x400 | 0x10, 0, compat::Presence::Refused, "a folder link"},
                        P{0x20, 0, compat::Presence::ReadFile, "an ordinary file"}, P{0x80, 0, compat::Presence::ReadFile, "a normal file"}}) {
            compat::Description d;
            auto got = compat::presence(p.attributes, p.error, d);
            check(got == p.want && d.present == (p.want != compat::Presence::Absent) && d.usable == (p.want != compat::Presence::Refused) &&
                  (p.want != compat::Presence::Refused || (!d.problems.empty() && d.problems[0].kind == compat::Kind::DescriptionUnusable)), std::string("description presence: ") + p.what);
        }
        // bounded quoting of untrusted text: control and bidirectional-override characters never reach a message
        auto q = compat::quote(std::string("evil\xe2\x80\xae" "txt.exe\n") + std::string(200, 'x'));
        check(q.find("\xe2\x80\xae") == std::string::npos && q.find('\n') == std::string::npos && compat::u32(q).size() <= 63, "quote() strips formatting characters and bounds length");
        std::printf("%s: %u passed, %u failed (vector file %ls)\n", failed ? "FAIL" : "PASS", passed, failed, argv[1]);
        return failed ? 1 : 0;
    } catch (const std::exception& e) { std::printf("FAIL exception: %s\n", e.what()); return 2; }
}
