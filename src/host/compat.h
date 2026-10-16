#pragma once
// Reads a mod's optional allstars-mod.json (format allstars-mod/1) for the rules the
// loader itself enforces before any mod code is loaded:
//   * the file must be readable in the loader's JSON subset (json.h), at most 4 MiB, UTF-8;
//   * "manifest_schema" (major 1 only) and "must_understand" (known feature names only);
//   * "loader": host_abi / mod_ini_schema / plan_schema / capabilities / host_sha256 / proxy_sha256;
//   * "load_order": after / before, checked against the real start order (see checkOrder).
// "target" is descriptive and only logged. Mod-to-mod rules (requires, conflicts_with, ...)
// are not read here: the manager and the checker enforce them.
// A present but unreadable statement is never treated as "no requirement".
// The outcomes match the managed reader (AllStarsModManager SidecarReader/Conflicts) for
// these fields; tests/vectors/loader-requirements.json holds the shared cases.
#include "json.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ssasr::compat {

constexpr size_t kMaxDescriptionBytes = 4u * 1024u * 1024u;
inline const char* const kKnownFeatures[] = {"loader-requirements/1", "load-order/1", "target-build/1", nullptr};

enum class Kind { NeedsNewerLoader, MadeForOtherLoader, DescriptionUnusable, LoadOrder };
struct Problem { Kind kind; std::string detail; };   // detail: UTF-8 sentence fragment, may quote bounded untrusted text

// ---- text helpers (the managed reader works on UTF-16 strings; these reproduce its rules) ----

// Strict UTF-8 as .NET's throwing decoder: no overlong forms, no surrogates, nothing above U+10FFFF.
inline bool decodeUtf8(const std::string& s, std::u32string* out) {
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        char32_t cp; size_t len;
        if (c < 0x80) { cp = c; len = 1; }
        else if (c >= 0xC2 && c <= 0xDF) { cp = c & 0x1F; len = 2; }
        else if (c >= 0xE0 && c <= 0xEF) { cp = c & 0x0F; len = 3; }
        else if (c >= 0xF0 && c <= 0xF4) { cp = c & 0x07; len = 4; }
        else return false;
        if (i + len > n) return false;
        for (size_t k = 1; k < len; ++k) { unsigned char cc = (unsigned char)s[i + k]; if ((cc & 0xC0) != 0x80) return false; cp = (cp << 6) | (cc & 0x3F); }
        if (len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return false;
        if (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) return false;
        if (out) out->push_back(cp);
        i += len;
    }
    return true;
}
inline std::string encodeUtf8(const std::u32string& s) {
    std::string r;
    for (char32_t c : s) {
        if (c < 0x80) r += (char)c;
        else if (c < 0x800) { r += (char)(0xC0 | (c >> 6)); r += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { r += (char)(0xE0 | (c >> 12)); r += (char)(0x80 | ((c >> 6) & 63)); r += (char)(0x80 | (c & 63)); }
        else { r += (char)(0xF0 | (c >> 18)); r += (char)(0x80 | ((c >> 12) & 63)); r += (char)(0x80 | ((c >> 6) & 63)); r += (char)(0x80 | (c & 63)); }
    }
    return r;
}
// char.IsWhiteSpace
inline bool isSpace(char32_t c) {
    return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}
// char.IsControl (category Cc)
inline bool isControl(char32_t c) { return c < 0x20 || (c >= 0x7F && c <= 0x9F); }
inline std::u32string trim32(const std::u32string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isSpace(s[a])) ++a;
    while (b > a && isSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}
inline size_t utf16Length(const std::u32string& s) { size_t n = 0; for (auto c : s) n += c > 0xFFFF ? 2 : 1; return n; }
// String values from json.h are UTF-8 once the whole file passed decodeUtf8 (\u escapes encode BMP non-surrogates).
inline std::u32string u32(const std::string& s) { std::u32string r; decodeUtf8(s, &r); return r; }
inline std::string trimmed(const std::string& s) { return encodeUtf8(trim32(u32(s))); }

// Bounded, single-line quotation of untrusted text for messages (control and invisible formatting characters become '?').
inline std::string quote(const std::string& s, size_t maxChars = 60) {
    std::u32string in = u32(s), out;
    for (char32_t c : in) {
        if (out.size() >= maxChars) { out += U'\x2026'; break; }
        bool bad = isControl(c) || (c >= 0x200B && c <= 0x200F) || (c >= 0x2028 && c <= 0x202E) || (c >= 0x2060 && c <= 0x2069) || c == 0xFEFF;
        out += bad ? U'?' : c;
    }
    return "\"" + encodeUtf8(out) + "\"";
}

inline bool schemaName(const std::string& s, unsigned long& major) {
    // \Aallstars-mod/(?<major>[0-9]{1,6})(?:\.(?<minor>[0-9]{1,6}))?\z, matched exactly (no trimming, no case folding)
    static const std::string prefix = "allstars-mod/";
    if (s.compare(0, prefix.size(), prefix) != 0) return false;
    size_t p = prefix.size(), d = 0; major = 0;
    while (p < s.size() && s[p] >= '0' && s[p] <= '9' && d < 7) { major = major * 10 + (unsigned long)(s[p] - '0'); ++p; ++d; }
    if (d < 1 || d > 6) return false;
    if (p == s.size()) return true;
    if (s[p] != '.') return false;
    ++p; d = 0;
    while (p < s.size() && s[p] >= '0' && s[p] <= '9') { ++p; ++d; }
    return d >= 1 && d <= 6 && p == s.size();
}
inline bool capabilityName(const std::string& s) {
    // \A[a-z0-9][a-z0-9._:\-]{0,63}\z
    if (s.empty() || s.size() > 64) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (i > 0 && (c == '.' || c == '_' || c == ':' || c == '-'));
        if (!ok) return false;
    }
    return true;
}
inline bool loaderHash(std::string& s) {
    // Trim, lower-case (ASCII; no other character lower-cases to a hex digit), then exactly 64 hex digits.
    s = trimmed(s);
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (s.size() != 64) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
// GoodId of the managed reader: not null/white space, trimmed length <= 200 UTF-16 units, no control characters.
inline bool goodId(const std::string& s) {
    auto c = u32(s), t = trim32(c);
    if (t.empty() || utf16Length(t) > 200) return false;
    for (auto ch : c) if (isControl(ch)) return false;
    return true;
}

// ---- the description ----

struct Description {
    bool present = false;          // the file exists (absent = legacy mod, no checks)
    bool usable = true;            // false: the mod must not start; problems say why
    bool applies = false;          // the loader/order rules below apply to this mod
    std::vector<Problem> problems;
    std::vector<std::string> notes; // logged only
    std::string id, name, homepage; // homepage: https:// printable ASCII, at most 200 characters, else empty
    bool loaderDeclared = false;
    std::optional<uint32_t> hostAbi, modIniSchema, planSchema;
    std::vector<std::string> capabilities, hostPins, proxyPins, targetExe, after, before;
};

inline const Json* member(const Json& o, const char* k) { auto i = o.o.find(k); return i == o.o.end() ? nullptr : &i->second; }
inline void refuse(Description& d, Kind k, std::string why) { d.usable = false; d.problems.push_back({k, std::move(why)}); }

// What the file system says about <mod>\allstars-mod.json, before anything is read. Only a confirmed absence makes a mod a
// legacy mod without description; a folder, a link, or a name that cannot be inspected may hide a requirement and is refused.
// attributes/error: GetFileAttributesW's result and, when that failed, GetLastError (values as in winerror.h / winnt.h).
enum class Presence { Absent, ReadFile, Refused };
inline Presence presence(uint32_t attributes, uint32_t error, Description& d) {
    constexpr uint32_t kInvalid = 0xFFFFFFFFu, kDirectory = 0x10u, kReparsePoint = 0x400u, kFileNotFound = 2u, kPathNotFound = 3u;
    if (attributes == kInvalid) {
        if (error == kFileNotFound || error == kPathNotFound) return Presence::Absent;
        d.present = true;
        refuse(d, Kind::DescriptionUnusable, "its description file (allstars-mod.json) could not be inspected (Windows error " + std::to_string(error) + ")");
        return Presence::Refused;
    }
    d.present = true;
    if (attributes & kReparsePoint) { refuse(d, Kind::DescriptionUnusable, "its description file (allstars-mod.json) is a link; the loader reads only a real file inside the mod folder"); return Presence::Refused; }
    if (attributes & kDirectory) { refuse(d, Kind::DescriptionUnusable, "\"allstars-mod.json\" in its folder is a folder, not a description file"); return Presence::Refused; }
    return Presence::ReadFile;
}
// List shape of the managed reader: an array; or one object (or, where allowed, one string) standing for a one-element list.
inline bool listOf(const Json& v, bool allowString, std::vector<const Json*>& items) {
    if (v.kind == Json::Array) { for (auto& e : v.a) items.push_back(&e); return true; }
    if (v.kind == Json::Object || (allowString && v.kind == Json::String)) { items.push_back(&v); return true; }
    return false;
}
inline void readHashes(const Json& parent, const char* field, std::vector<std::string>& into, Description& d, bool safety) {
    auto v = member(parent, field);
    if (!v) return;
    std::vector<const Json*> items;
    if (v->kind == Json::Array) for (auto& e : v->a) items.push_back(&e); else items.push_back(v);
    for (auto it : items) {
        std::string s;
        if (it->kind == Json::String && loaderHash(s = it->s)) { if (std::find(into.begin(), into.end(), s) == into.end()) into.push_back(s); continue; }
        if (safety) refuse(d, Kind::DescriptionUnusable, std::string("\"") + field + "\" in its description file must hold SHA-256 values (64 hexadecimal digits)");
        else d.notes.push_back(std::string("ignored an unreadable \"") + field + "\" entry (descriptive only)");
    }
}

inline Description read(const std::string& bytes, const std::string& modIniId) {
    Description d;
    d.present = true;
    if (bytes.size() > kMaxDescriptionBytes) { refuse(d, Kind::DescriptionUnusable, "its description file (allstars-mod.json) is larger than 4 MB"); return d; }
    if (!decodeUtf8(bytes, nullptr)) { refuse(d, Kind::DescriptionUnusable, "its description file (allstars-mod.json) is not valid UTF-8 text"); return d; }
    Json root;
    try { root = JsonParser(bytes).parse(); }
    catch (const std::exception&) {
        refuse(d, Kind::DescriptionUnusable, "its description file (allstars-mod.json) is not plain JSON in the form the loader reads (for example a comment, a trailing comma, a byte-order mark, a negative or fractional number, or a name used twice)");
        return d;
    }
    if (root.kind != Json::Object) { refuse(d, Kind::DescriptionUnusable, "its description file (allstars-mod.json) does not contain one JSON object"); return d; }

    // display fields (lenient)
    if (auto v = member(root, "id"); v && v->kind == Json::String) d.id = v->s;
    else if (auto p = member(root, "package_id"); p && p->kind == Json::String) d.id = p->s;
    if (auto v = member(root, "name"); v && v->kind == Json::String) d.name = v->s;
    if (auto v = member(root, "homepage"); v && v->kind == Json::String) {
        const auto& h = v->s;
        bool ok = h.compare(0, 8, "https://") == 0 && h.size() <= 200;
        for (unsigned char c : h) if (c <= ' ' || c > '~') ok = false;
        if (ok) d.homepage = h;
    }

    // gates: format name and must-understand features
    if (auto s = member(root, "manifest_schema")) {
        unsigned long major = 0;
        if (s->kind != Json::String || !schemaName(s->s, major))
            refuse(d, Kind::DescriptionUnusable, "its description file names an unknown format in \"manifest_schema\" (expected allstars-mod/1)");
        else if (major != 1)
            refuse(d, Kind::NeedsNewerLoader, "its description file uses format " + quote(s->s, 40) + ", and this loader release reads allstars-mod/1 only");
    }
    if (auto mu = member(root, "must_understand")) {
        std::vector<const Json*> items;
        if (!listOf(*mu, true, items)) refuse(d, Kind::DescriptionUnusable, "\"must_understand\" in its description file is not a list");
        for (auto it : items) {
            if (it->kind != Json::String) { refuse(d, Kind::DescriptionUnusable, "\"must_understand\" in its description file must list feature names as text"); continue; }
            auto name = trimmed(it->s);
            bool known = false;
            for (auto k = kKnownFeatures; *k; ++k) known = known || name == *k;
            if (!known) refuse(d, Kind::NeedsNewerLoader, "it relies on the description feature " + quote(name) + ", which this loader release does not know");
        }
    }
    if (!d.usable) return d;

    if (auto l = member(root, "loader")) {
        if (l->kind != Json::Object) refuse(d, Kind::DescriptionUnusable, "\"loader\" in its description file is not an object");
        else {
            d.loaderDeclared = true;
            auto num = [&](const char* k, std::optional<uint32_t>& into) {
                auto v = member(*l, k);
                if (!v) return;
                if (v->kind == Json::Number && v->n <= UINT32_MAX) into = (uint32_t)v->n;
                else refuse(d, Kind::DescriptionUnusable, std::string("\"loader.") + k + "\" in its description file is not a whole number");
            };
            num("host_abi", d.hostAbi);
            num("mod_ini_schema", d.modIniSchema);
            num("plan_schema", d.planSchema);
            if (auto c = member(*l, "capabilities")) {
                std::vector<const Json*> items;
                if (!listOf(*c, true, items)) refuse(d, Kind::DescriptionUnusable, "\"loader.capabilities\" in its description file is not a list");
                for (auto it : items) {
                    std::string s;
                    if (it->kind == Json::String && capabilityName(s = trimmed(it->s))) d.capabilities.push_back(s);
                    else refuse(d, Kind::DescriptionUnusable, "a loader capability in its description file is not a short lowercase name");
                }
            }
            readHashes(*l, "host_sha256", d.hostPins, d, true);
            readHashes(*l, "proxy_sha256", d.proxyPins, d, true);
        }
    }
    if (auto lo = member(root, "load_order")) {
        if (lo->kind != Json::Object) refuse(d, Kind::DescriptionUnusable, "\"load_order\" in its description file must be an object with \"after\" and/or \"before\"");
        else for (const char* field : {"after", "before"}) {
            auto f = member(*lo, field);
            if (!f) continue;
            std::vector<const Json*> items;
            if (!listOf(*f, true, items)) refuse(d, Kind::DescriptionUnusable, std::string("\"load_order.") + field + "\" in its description file is not a list");
            for (auto it : items) {
                const Json* idv = it->kind == Json::String ? it : it->kind == Json::Object ? member(*it, "id") : nullptr;
                if (idv && idv->kind == Json::String && goodId(idv->s)) (field[0] == 'a' ? d.after : d.before).push_back(trimmed(idv->s));
                else refuse(d, Kind::DescriptionUnusable, std::string("an entry in \"load_order.") + field + "\" of its description file is not a mod ID");
            }
        }
    }
    if (auto t = member(root, "target"); t && t->kind == Json::Object) readHashes(*t, "executable_sha256", d.targetExe, d, false);

    // A description written for a different mod: none of its rules are applied. If it declares loader or order rules (or they cannot be
    // read), the package is inconsistent and the mod does not start; otherwise it is ignored.
    if (!d.id.empty() && d.id != modIniId) {
        if (d.loaderDeclared || !d.after.empty() || !d.before.empty() || !d.usable)
            refuse(d, Kind::DescriptionUnusable, "its description file is for another mod (its id is " + quote(d.id) + ", mod.ini says " + quote(modIniId) + ") and contains loader or start-order rules");
        else d.notes.push_back("its description file names another mod (" + quote(d.id) + "); it was ignored");
        return d;
    }
    d.applies = d.usable;
    return d;
}

// ---- what this loader provides ----

struct Self {
    uint32_t hostAbi, modIniSchema, planSchema;
    const char* const* capabilities;   // NULL-terminated
    const char* release;
    const char* const* retired;        // NULL-terminated: provided by earlier builds, not by this one
};

// hostSha/proxySha are only called when a mod pins builds; they return "" when the file could not be hashed.
inline void checkLoader(Description& d, const Self& self, const std::function<std::string()>& hostSha, const std::function<std::string()>& proxySha) {
    if (!d.usable || !d.applies || !d.loaderDeclared) return;
    auto pin = [&](const std::vector<std::string>& wanted, const std::function<std::string()>& actual, const char* file) {
        if (wanted.empty()) return;
        auto a = actual();
        if (a.empty()) { d.usable = false; d.problems.push_back({Kind::MadeForOtherLoader, std::string("it accepts only specific ") + file + " builds, and the loader could not read its own " + file + " to compare"}); return; }
        if (std::find(wanted.begin(), wanted.end(), a) == wanted.end()) {
            d.usable = false;
            d.problems.push_back({Kind::MadeForOtherLoader, "it accepts only " + std::to_string(wanted.size()) + " specific " + file + " build" + (wanted.size() == 1 ? "" : "s") +
                ", and the installed one (release " + self.release + ", SHA-256 " + a.substr(0, 12) + "...) is not among them"});
        }
    };
    pin(d.hostPins, hostSha, "ChaoGarage.dll");
    pin(d.proxyPins, proxySha, "dinput8.dll");
    auto version = [&](const std::optional<uint32_t>& wanted, uint32_t have, const char* what) {
        if (!wanted || *wanted == have) return;
        d.usable = false;
        d.problems.push_back({*wanted > have ? Kind::NeedsNewerLoader : Kind::MadeForOtherLoader,
            std::string("it needs ") + what + " version " + std::to_string(*wanted) + ", and this loader provides version " + std::to_string(have)});
    };
    version(d.hostAbi, self.hostAbi, "host interface");
    version(d.modIniSchema, self.modIniSchema, "mod.ini");
    version(d.planSchema, self.planSchema, "plan");
    std::vector<std::string> missing, retired;
    for (auto& c : d.capabilities) {
        bool have = false, old = false;
        for (auto k = self.capabilities; *k; ++k) have = have || c == *k;
        for (auto k = self.retired; k && *k; ++k) old = old || c == *k;
        auto& into = old ? retired : missing;
        if (!have && std::find(into.begin(), into.end(), c) == into.end()) into.push_back(c);
    }
    auto names = [](const std::vector<std::string>& v) {
        std::string list;
        for (size_t i = 0; i < v.size() && i < 6; ++i) list += (i ? ", " : "") + v[i];
        return v.size() > 6 ? list + ", ..." : list;
    };
    if (!retired.empty()) {
        d.usable = false;
        d.problems.push_back({Kind::MadeForOtherLoader, std::string("it needs loader features that earlier loader builds provided and this release (") + self.release + ") no longer does: " + names(retired)});
    }
    if (!missing.empty()) {
        d.usable = false;
        d.problems.push_back({Kind::NeedsNewerLoader, std::string("it needs loader features this release (") + self.release + ") does not provide: " + names(missing)});
    }
}

// ---- declared start order against the real one ----

struct Started { std::string id, folder; const Description* d; };   // in the order the loader starts them

// Returns, for each index that violates its own declaration, the problem. Same rule as the managed reader: a named mod that is not
// started imposes nothing; a mod never constrains itself or a mod with its own ID; nothing can start before "metal-sonic".
inline std::vector<std::pair<size_t, Problem>> checkOrder(const std::vector<Started>& order) {
    std::vector<std::pair<size_t, Problem>> out;
    for (size_t i = 0; i < order.size(); ++i) {
        auto d = order[i].d;
        if (!d || !d->usable || !d->applies) continue;
        auto check = [&](const std::vector<std::string>& ids, bool mustBeAfter) {
            for (auto& want : ids)
                for (size_t j = 0; j < order.size(); ++j) {
                    if (j == i || order[j].id != want || order[j].id == order[i].id) continue;
                    if (mustBeAfter ? i > j : i < j) continue;
                    std::string fix = (!mustBeAfter && order[j].id == "metal-sonic")
                        ? "the loader always starts the mod with ID \"metal-sonic\" first, so nothing can start before it"
                        : "rename the folder " + quote(order[mustBeAfter ? i : j].folder) + " or " + quote(order[mustBeAfter ? j : i].folder) +
                          " so that the first one sorts later (folders start in plain character order: digits, then capital letters, then lowercase letters; \"metal-sonic\" first)";
                    out.push_back({i, {Kind::LoadOrder, std::string("it must start ") + (mustBeAfter ? "after " : "before ") + quote(want) + " (folder " + quote(order[j].folder) + "), but the loader starts it " +
                        (mustBeAfter ? "earlier" : "later") + "; " + fix}});
                }
        };
        check(d->after, true);
        check(d->before, false);
    }
    return out;
}

}  // namespace ssasr::compat
