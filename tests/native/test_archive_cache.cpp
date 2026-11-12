// Offline checks for the start-up records (src/host/archive_cache.h): original archives (1.3.2), mod payload files and combined layouts (1.3.3): hashed once, remembered by file identity,
// and hashed in full again on any difference or doubt. Synthetic files only.
//   test_archive_cache <work folder>
//   test_archive_cache --measure <archive to read> <scratch folder>      reads the archive only (never writes it); prints the
//                                                                         time of the full check and of the check from a record
#include "archive_cache.h"
#include <cstdio>
using namespace ssasr;

static unsigned g_fail = 0, g_pass = 0;
static void Check(bool ok, const char* what) { if (ok) ++g_pass; else { ++g_fail; std::printf("FAIL %s\n", what); } }
static std::wstring g_work;

static void WriteFile(const std::wstring& p, const std::string& bytes) {
    Handle f(CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)); require(f.h != INVALID_HANDLE_VALUE, "create");
    if (!bytes.empty()) write(f.h, bytes.data(), (DWORD)bytes.size());
}
static std::string ReadFile_(const std::wstring& p) { return text_file(p); }
static std::string Synthetic(size_t n, uint32_t seed) { std::string s(n, 0); uint32_t x = seed; for (auto& c : s) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; c = (char)x; } return s; }
static std::string Sha(const std::string& s) { Hash h; if (!s.empty()) h.add(s.data(), (DWORD)s.size()); return h.finish(); }

enum class How { Hashed, Memory, Record, Refused };
// Checks the file the way virtual_xpac does: open (writers denied), then verify_original; says which way the check was settled.
static How Verify(const std::wstring& path, const std::string& expected) {
    auto& st = archive_cache::state().stats; auto before = st;
    Handle f(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)); require(f.h != INVALID_HANDLE_VALUE, "open");
    try { archive_cache::verify_original(f.h, size(f.h), path, expected); } catch (const std::exception&) { return How::Refused; }
    if (st.fromMemory > before.fromMemory) return How::Memory;
    if (st.fromRecord > before.fromRecord) return How::Record;
    return How::Hashed;
}
static void Dir(const std::wstring& p) { CreateDirectoryW(p.c_str(), nullptr); }
static void Wipe(const std::wstring& p) {
    WIN32_FIND_DATAW d; HANDLE f = FindFirstFileW((p + L"\\*").c_str(), &d);
    if (f != INVALID_HANDLE_VALUE) { do { std::wstring n = d.cFileName; if (n == L"." || n == L"..") continue; auto q = p + L"\\" + n;
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { Wipe(q); RemoveDirectoryW(q.c_str()); } else DeleteFileW(q.c_str()); } while (FindNextFileW(f, &d)); FindClose(f); }
}
static double Ms(LARGE_INTEGER a, LARGE_INTEGER b) { LARGE_INTEGER q; QueryPerformanceFrequency(&q); return double(b.QuadPart - a.QuadPart) * 1000.0 / double(q.QuadPart); }

int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring(argv[1]) == L"--measure") {
        std::wstring archive = argv[2], scratch = argv[3];
        Dir(scratch); Wipe(scratch); Dir(scratch + L"\\mods");
        archive_cache::set_directory(scratch + L"\\mods\\.chaogarage-cache");
        std::string expected = hash_file(archive);   // (the plan's hash; computed here once, not timed)
        LARGE_INTEGER t0, t1, t2, t3, t4; QueryPerformanceCounter(&t0);
        auto cold = Verify(archive, expected); QueryPerformanceCounter(&t1);
        auto again = Verify(archive, expected); QueryPerformanceCounter(&t2);
        archive_cache::forget_memory();
        auto warm = Verify(archive, expected); QueryPerformanceCounter(&t3);
        Handle f(CreateFileW(archive.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)); QueryPerformanceCounter(&t4);
        std::printf("archive %.0f MB: first check %s %.1f ms; second check in the same start %s %.3f ms; check at a later start from the record %s %.3f ms\n",
                    double(size(f.h)) / 1048576.0, cold == How::Hashed ? "(hashed in full)" : "(?)", Ms(t0, t1), again == How::Memory ? "(from memory)" : "(?)", Ms(t1, t2),
                    warm == How::Record ? "(from the record)" : "(?)", Ms(t2, t3));
        return (cold == How::Hashed && again == How::Memory && warm == How::Record) ? 0 : 1;
    }
    if (argc != 2) { std::printf("usage: test_archive_cache <work folder>\n"); return 2; }
    g_work = argv[1]; Dir(g_work); Wipe(g_work);
    const std::wstring game = g_work + L"\\game", mods = game + L"\\mods", cache = mods + L"\\.chaogarage-cache", record = cache + L"\\verified-archives.txt";
    Dir(game); Dir(mods); Dir(game + L"\\Resource");
    const std::wstring file = game + L"\\Resource\\a.xpac";
    std::string data = Synthetic(3 * 1024 * 1024 + 123, 7);
    WriteFile(file, data);
    const std::string sha = Sha(data);
    archive_cache::set_directory(cache);

    Check(Verify(file, sha) == How::Hashed, "no record yet: hashed in full");
    Check(GetFileAttributesW(record.c_str()) != INVALID_FILE_ATTRIBUTES, "the record is written after a full match");
    Check(Verify(file, sha) == How::Memory, "the same start again (another mod naming it): not hashed again");
    archive_cache::forget_memory();
    Check(Verify(file, sha) == How::Record, "a later start: taken from the record");
    archive_cache::forget_memory();
    Check(Verify(file, std::string(64, 'a')) == How::Refused, "a hash the file does not have: refused, the record does not vouch for it");
    archive_cache::forget_memory();
    Check(Verify(file, sha) == How::Record, "and the right record is still there afterwards");
    archive_cache::forget_memory();
    Check(Verify(file, "abc") == How::Refused, "an expected value that is not 64 hex digits: refused");

    // the same bytes written again: new last-write time
    Sleep(50); WriteFile(file, data); archive_cache::forget_memory();
    Check(Verify(file, sha) == How::Hashed, "the same file rewritten (time differs): hashed in full again");
    archive_cache::forget_memory();
    Check(Verify(file, sha) == How::Record, "and recorded again");

    // deleted and created again with the same content
    DeleteFileW(file.c_str()); Sleep(50); WriteFile(file, data); archive_cache::forget_memory();
    Check(Verify(file, sha) == How::Hashed, "deleted and created again with the same bytes: hashed in full again");

    // Each part of the file's identity on its own: the file is replaced in a way that keeps all the other parts.
    {   auto times = [&](FILETIME& created, FILETIME& written) { Handle f(CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)); require(f.h != INVALID_HANDLE_VALUE, "open");
                                                              FILETIME a; require(GetFileTime(f.h, &created, &a, &written) != 0, "times"); };
        auto setTimes = [&](const std::wstring& p, const FILETIME& created, const FILETIME& written) { Handle f(CreateFileW(p.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)); require(f.h != INVALID_HANDLE_VALUE, "open for times");
                                                                                   require(SetFileTime(f.h, &created, nullptr, &written) != 0, "set times"); };
        archive_cache::forget_memory();
        Check(Verify(file, sha) == How::Record, "(record valid before the identity cases)");
        FILETIME created{}, written{}; times(created, written);
        // 1. a new file (new file ID) with the same bytes, size, creation time and last-write time
        const std::wstring spare = game + L"\\Resource\\spare.xpac"; WriteFile(spare, data);
        DeleteFileW(file.c_str()); require(MoveFileW(spare.c_str(), file.c_str()) != 0, "rename"); setTimes(file, created, written); archive_cache::forget_memory();
        Check(Verify(file, sha) == How::Hashed, "same bytes, size and both times but another file (file ID differs): hashed in full");
        archive_cache::forget_memory(); Check(Verify(file, sha) == How::Record, "(recorded again)");
        // 2. the same file (same ID) with other bytes of another size, creation and write times put back: the size differs
        { std::string longer = data + "yy"; { Handle f(CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)); require(f.h != INVALID_HANDLE_VALUE, "reopen"); write(f.h, longer.data(), (DWORD)longer.size()); }
          setTimes(file, created, written); archive_cache::forget_memory();
          Check(Verify(file, sha) == How::Refused, "a longer file with the same file ID, creation time and write time: refused (the size is part of the identity)");
          { Handle f(CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)); write(f.h, data.data(), (DWORD)data.size()); }
          setTimes(file, created, written); archive_cache::forget_memory();
          // 1.3.2 trusted this case (same ID, size and times); 1.3.3's change time shows that the file was written.
          Check(Verify(file, sha) == How::Hashed, "the original bytes written back in place, size and both times put back, same file ID: hashed in full (the change time moved)"); }
        // 2b. (1.3.3) other bytes of the SAME size written in place, both times put back: the identity differs (change time), so the
        // file is hashed and refused. In 1.3.2 the identity matched and only a later full check of the layout noticed.
        { std::string other = data; other[2000] ^= 1;
          { Handle f(CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)); require(f.h != INVALID_HANDLE_VALUE, "reopen"); write(f.h, other.data(), (DWORD)other.size()); }
          setTimes(file, created, written); archive_cache::forget_memory();
          Check(Verify(file, sha) == How::Refused, "same size, other bytes, both times put back, same file ID: refused (1.3.3: the change time is part of the identity)");
          { Handle f(CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)); write(f.h, data.data(), (DWORD)data.size()); }
          setTimes(file, created, written); archive_cache::forget_memory();
          Check(Verify(file, sha) == How::Hashed, "(the original bytes back: hashed and recorded)"); archive_cache::forget_memory();
          Check(Verify(file, sha) == How::Record, "(and valid)"); }
        // 2c. (1.3.3) a same-size file copied over it with CopyFile (keeps the file ID and creation time, copies the write time):
        { const std::wstring donor = game + L"\\Resource\\donor.xpac"; std::string other = data; other[5] ^= 1; WriteFile(donor, other); setTimes(donor, created, written);
          archive_cache::forget_memory(); require(CopyFileW(donor.c_str(), file.c_str(), FALSE) != 0, "copy over");
          Check(Verify(file, sha) == How::Refused, "a same-size file copied over it, its dates kept: refused (the change time moved)");
          WriteFile(file, data); DeleteFileW(donor.c_str()); archive_cache::forget_memory(); Check(Verify(file, sha) == How::Hashed, "(original back)");
          times(created, written); archive_cache::forget_memory(); Check(Verify(file, sha) == How::Record, "(and valid)"); }
        // 3. the last-write time (setting it also moves the change time, so since 1.3.3 these two cases can't isolate one part)
        { FILETIME other = written; other.dwLowDateTime += 20000000; setTimes(file, created, other); archive_cache::forget_memory();
          Check(Verify(file, sha) == How::Hashed, "only the last-write time differs: hashed in full"); setTimes(file, created, written); archive_cache::forget_memory(); }
        // 4. the creation time alone (first make sure a record for the original identity is the one on file)
        Check(Verify(file, sha) == How::Hashed, "(original identity hashed and recorded again)");
        archive_cache::forget_memory(); Check(Verify(file, sha) == How::Record, "(and valid)"); archive_cache::forget_memory();
        { FILETIME other = created; other.dwLowDateTime += 20000000; setTimes(file, other, written); archive_cache::forget_memory();
          Check(Verify(file, sha) == How::Hashed, "only the creation time differs: hashed in full"); setTimes(file, created, written); archive_cache::forget_memory(); }
        Check(Verify(file, sha) != How::Refused, "(settled)");
    }

    // a changed file (one byte of the same size): the plan's hash no longer matches
    { std::string changed = data; changed[1000] ^= 1; Sleep(50); WriteFile(file, changed); archive_cache::forget_memory();
      Check(Verify(file, sha) == How::Refused, "a changed file: refused even though a record of the old one exists");
      Check(Verify(file, Sha(changed)) == How::Hashed, "(its own hash, as another plan might name it: hashed in full)");
      WriteFile(file, data); }

    // a longer file
    { std::string longer = data + "x"; Sleep(50); WriteFile(file, longer); archive_cache::forget_memory();
      Check(Verify(file, sha) == How::Refused, "a longer file: refused");
      WriteFile(file, data); }
    archive_cache::forget_memory();
    Check(Verify(file, sha) == How::Hashed, "the original again (new time): hashed in full");
    archive_cache::forget_memory();
    Check(Verify(file, sha) == How::Record, "recorded");

    // the same bytes at another path
    { const std::wstring other = game + L"\\Resource\\b.xpac"; WriteFile(other, data); archive_cache::forget_memory();
      Check(Verify(other, sha) == How::Hashed, "a copy at another path: hashed in full (a record is for one path)"); archive_cache::forget_memory();
      Check(Verify(file, sha) == How::Record, "the first path keeps its record"); }

    // damaged, truncated, missing, edited records
    archive_cache::forget_memory();
    { auto text = ReadFile_(record); auto bad = text; bad[bad.size() / 2] ^= 1; WriteFile(record, bad); archive_cache::forget_memory();
      Check(Verify(file, sha) == How::Hashed, "a record with one changed character: hashed in full");
      archive_cache::forget_memory(); Check(Verify(file, sha) == How::Record, "and the record was written again"); }
    { auto text = ReadFile_(record); WriteFile(record, text.substr(0, text.size() - 10)); archive_cache::forget_memory();
      Check(Verify(file, sha) == How::Hashed, "a cut-off record: hashed in full"); archive_cache::forget_memory(); }
    { WriteFile(record, ""); archive_cache::forget_memory(); Check(Verify(file, sha) == How::Hashed, "an empty record: hashed in full"); archive_cache::forget_memory(); }
    { DeleteFileW(record.c_str()); archive_cache::forget_memory(); Check(Verify(file, sha) == How::Hashed, "a missing record: hashed in full"); archive_cache::forget_memory(); }
    { WriteFile(record, "garbage\n"); archive_cache::forget_memory(); Check(Verify(file, sha) == How::Hashed, "not a record at all: hashed in full"); archive_cache::forget_memory(); }
    // a record whose check line is right but whose volume serial is another (a game folder moved to another drive)
    { archive_cache::forget_memory(); Check(Verify(file, sha) == How::Record, "(record valid again)");
      auto text = ReadFile_(record); auto at = text.find(".xpac|"); Check(at != std::string::npos, "(the record names the file)");
      // the first identity field after the path is the volume serial
      auto vol = text.substr(at + 6, 8); std::string other = vol == "00000001" ? "00000002" : "00000001";
      auto edited = text.substr(0, at + 6) + other + text.substr(at + 6 + 8);
      auto body = edited.substr(0, edited.rfind("end|")); edited = body + "end|" + archive_cache::text_hash(body) + "\n";
      WriteFile(record, edited); archive_cache::forget_memory();
      Check(Verify(file, sha) == How::Hashed, "a record made on another volume (correct check line): hashed in full"); archive_cache::forget_memory(); }
    // a record naming another game folder (the path part differs)
    { archive_cache::forget_memory(); Check(Verify(file, sha) == How::Record, "(record valid again)");
      auto text = ReadFile_(record); auto at = text.find("a.xpac|"); auto edited = text.substr(0, at) + "z.xpac|" + text.substr(at + 7);
      auto body = edited.substr(0, edited.rfind("end|")); edited = body + "end|" + archive_cache::text_hash(body) + "\n";
      WriteFile(record, edited); archive_cache::forget_memory();
      Check(Verify(file, sha) == How::Hashed, "a record for another path: hashed in full"); archive_cache::forget_memory(); }
    // a different sha in an otherwise right record (correct check line)
    { archive_cache::forget_memory(); Check(Verify(file, sha) == How::Record, "(record valid again)");
      auto text = ReadFile_(record); auto body = text.substr(0, text.rfind("end|")); auto at = body.find(sha); auto edited = body.substr(0, at) + std::string(64, 'b') + body.substr(at + 64);
      edited += "end|" + archive_cache::text_hash(edited) + "\n"; WriteFile(record, edited); archive_cache::forget_memory();
      Check(Verify(file, sha) == How::Hashed, "a record holding another hash: hashed in full"); archive_cache::forget_memory(); }

    // no cache folder configured: nothing is read or written, but one start still checks once
    { archive_cache::set_directory(L""); DeleteFileW(record.c_str());
      Check(Verify(file, sha) == How::Hashed, "no record folder: hashed in full"); Check(Verify(file, sha) == How::Memory, "no record folder: still once per start");
      Check(GetFileAttributesW(record.c_str()) == INVALID_FILE_ATTRIBUTES, "no record folder: nothing written"); }

    // the record folder is a link: no record is read or written
    { archive_cache::set_directory(cache); Wipe(cache); RemoveDirectoryW(cache.c_str());
      const std::wstring target = g_work + L"\\elsewhere"; Dir(target);
      std::wstring cmd = L"cmd /c mklink /J \"" + cache + L"\" \"" + target + L"\" >nul 2>nul"; std::vector<wchar_t> line(cmd.begin(), cmd.end()); line.push_back(0);
      STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
      if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { WaitForSingleObject(pi.hProcess, 20000); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
      if (GetFileAttributesW(cache.c_str()) != INVALID_FILE_ATTRIBUTES && (GetFileAttributesW(cache.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) {
          Check(Verify(file, sha) == How::Hashed, "the record folder is a link: hashed in full");
          Check(GetFileAttributesW((target + L"\\verified-archives.txt").c_str()) == INVALID_FILE_ATTRIBUTES, "and nothing is written through the link");
          RemoveDirectoryW(cache.c_str());
      } else std::printf("note: could not make a junction here; the link case was not run\n"); }

    // ---- 1.3.3: a mod's payload files (verify_payload), written at the end of the start (flush) ----
    {   archive_cache::set_directory(cache); Dir(cache);
        const std::wstring modDir = mods + L"\\Alpha", payload = modDir + L"\\p.bin", fileRecord = cache + L"\\verified-mod-files.txt"; Dir(modDir);
        std::string bytes = Synthetic(200 * 1024 + 7, 11); WriteFile(payload, bytes); const std::string psha = Sha(bytes);
        auto VerifyPayload = [&](const std::string& expected, std::string* error = nullptr) {
            auto& st = archive_cache::state().stats; auto before = st;
            Handle f(CreateFileW(payload.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)); require(f.h != INVALID_HANDLE_VALUE, "open payload");
            try { archive_cache::verify_payload(f.h, size(f.h), payload, expected); } catch (const std::exception& e) { if (error) *error = e.what(); return How::Refused; }
            if (st.filesFromMemory > before.filesFromMemory) return How::Memory;
            if (st.filesFromRecord > before.filesFromRecord) return How::Record;
            return How::Hashed; };
        DeleteFileW(fileRecord.c_str());
        Check(VerifyPayload(psha) == How::Hashed, "payload: no record yet: hashed in full");
        Check(GetFileAttributesW(fileRecord.c_str()) == INVALID_FILE_ATTRIBUTES, "payload: nothing is written before the end of the start");
        Check(VerifyPayload(psha) == How::Memory, "payload: named again in the same start: not hashed again");
        archive_cache::flush(); Check(GetFileAttributesW(fileRecord.c_str()) != INVALID_FILE_ATTRIBUTES, "payload: written once, at the end of the start");
        archive_cache::forget_memory(); Check(VerifyPayload(psha) == How::Record, "payload: a later start: taken from the record");
        archive_cache::forget_memory();
        { std::string error; Check(VerifyPayload(std::string(64, 'c'), &error) == How::Refused && error == "Payload hash mismatch", "payload: another hash: refused with the payload message"); }
        archive_cache::forget_memory();
        FILETIME created{}, accessed{}, written{}; { Handle f(CreateFileW(payload.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)); GetFileTime(f.h, &created, &accessed, &written); }
        // the realistic case: a mod re-extracted over itself, one file of the same size with other bytes and the same dates
        { std::string other = bytes; other[100] ^= 0x20; Sleep(20);
          { Handle f(CreateFileW(payload.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)); write(f.h, other.data(), (DWORD)other.size()); SetFileTime(f.h, &created, nullptr, &written); }
          std::string error; Check(VerifyPayload(psha, &error) == How::Refused && error == "Payload hash mismatch", "payload: same size, other bytes, dates put back: refused (the change time moved)");
          archive_cache::forget_memory();
          { Handle f(CreateFileW(payload.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)); write(f.h, bytes.data(), (DWORD)bytes.size()); SetFileTime(f.h, &created, nullptr, &written); }
          Check(VerifyPayload(psha) == How::Hashed, "payload: the right bytes back with the same dates: hashed again, then recorded"); archive_cache::forget_memory();
          Check(VerifyPayload(psha) == How::Record, "payload: (and valid)"); archive_cache::forget_memory(); }
        // a damaged payload record: hashed in full
        { auto text = ReadFile_(fileRecord); auto bad = text; bad[bad.size() / 3] ^= 1; WriteFile(fileRecord, bad); archive_cache::forget_memory();
          Check(VerifyPayload(psha) == How::Hashed, "payload: a damaged record: hashed in full"); archive_cache::forget_memory(); }
        // an archive record does not vouch for a mod file, nor the other way round (two separate records)
        { auto text = ReadFile_(fileRecord); Check(text.rfind("chaogarage-verified-mod-files 1\n", 0) == 0, "payload: the record has its own header"); }
        // many files: more than 1.3.2's 256-record limit fit (a big mod set has ~300 payload files)
        { for (int i = 0; i < 400; ++i) { auto p = modDir + L"\\many" + std::to_wstring(i) + L".bin"; std::string b = "file " + std::to_string(i); WriteFile(p, b);
              Handle f(CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)); archive_cache::verify_payload(f.h, size(f.h), p, Sha(b)); }
          archive_cache::forget_memory(); auto& st = archive_cache::state().stats; auto before = st.filesFromRecord;
          for (int i = 0; i < 400; ++i) { auto p = modDir + L"\\many" + std::to_wstring(i) + L".bin"; std::string b = "file " + std::to_string(i);
              Handle f(CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)); archive_cache::verify_payload(f.h, size(f.h), p, Sha(b)); }
          Check(st.filesFromRecord - before == 400, "payload: 400 files are all remembered across starts");
          Check(VerifyPayload(psha) == How::Record, "payload: and the first one still is"); archive_cache::forget_memory(); }
    }

    // ---- 1.3.3: combined layouts (composed_known / composed_verified) ----
    {   archive_cache::set_directory(cache); const std::wstring rec = cache + L"\\verified-composed.txt"; DeleteFileW(rec.c_str());
        const std::string build = Sha("build one"), plan = Sha("plan"), base = Sha("base"), out = Sha("composed");
        const std::wstring modA = mods + L"\\Alpha", arch = game + L"\\Resource\\a.xpac";
        archive_cache::set_build(build);
        auto place = archive_cache::composed_place(modA, arch); auto mat = archive_cache::composed_material(plan, 0, arch, base, out, 4096, 3);
        Check(!place.empty() && archive_cache::is_sha_text(mat), "layout: a place and a material");
        Check(!archive_cache::composed_known(place, mat, out), "layout: unknown before any full hash");
        archive_cache::composed_verified(place, mat, out);
        Check(GetFileAttributesW(rec.c_str()) == INVALID_FILE_ATTRIBUTES, "layout: not written before the end of the start"); archive_cache::forget_memory();
        Check(GetFileAttributesW(rec.c_str()) != INVALID_FILE_ATTRIBUTES, "layout: written at the end of the start");
        Check(archive_cache::composed_known(place, mat, out), "layout: known at a later start");
        archive_cache::forget_memory();
        Check(!archive_cache::composed_known(place, mat, Sha("another composed")), "layout: a plan asking for another result: not vouched for");
        Check(archive_cache::composed_material(Sha("plan 2"), 0, arch, base, out, 4096, 3) != mat, "layout: another plan text: another material");
        Check(archive_cache::composed_material(plan, 1, arch, base, out, 4096, 3) != mat, "layout: another group: another material");
        Check(archive_cache::composed_material(plan, 0, arch, Sha("base 2"), out, 4096, 3) != mat, "layout: another original: another material");
        Check(archive_cache::composed_material(plan, 0, arch, base, out, 4097, 3) != mat && archive_cache::composed_material(plan, 0, arch, base, out, 4096, 4) != mat, "layout: other dimensions: another material");
        Check(archive_cache::composed_material(plan, 0, game + L"\\Resource\\b.xpac", base, out, 4096, 3) != mat, "layout: another archive: another material");
        archive_cache::set_build(Sha("build two"));
        Check(archive_cache::composed_material(plan, 0, arch, base, out, 4096, 3) != mat, "layout: another loader build: another material");
        archive_cache::set_build(""); Check(archive_cache::composed_material(plan, 0, arch, base, out, 4096, 3).empty(), "layout: no build id: no material, nothing used");
        archive_cache::set_build(build);
        Check(archive_cache::composed_material("", 0, arch, base, out, 4096, 3).empty() && archive_cache::composed_material(plan, 0, arch, "x", out, 4096, 3).empty(), "layout: a missing or malformed hash: no material");
        Check(!archive_cache::composed_known("", mat, out) && !archive_cache::composed_known(place, "", out), "layout: an empty place or material is never known");
        // one record per place: a new material for the same mod and archive replaces the old one
        { auto mat2 = archive_cache::composed_material(Sha("plan 2"), 0, arch, base, out, 4096, 3); archive_cache::composed_verified(place, mat2, out); archive_cache::forget_memory();
          Check(archive_cache::composed_known(place, mat2, out) && !archive_cache::composed_known(place, mat, out), "layout: one record per mod and archive (the newer one)");
          archive_cache::composed_verified(place, mat, out); archive_cache::forget_memory(); }
        // another mod folder: its own place
        Check(!archive_cache::composed_known(archive_cache::composed_place(mods + L"\\Beta", arch), mat, out), "layout: another mod folder: not known");
        // damaged records
        { auto text = ReadFile_(rec); auto bad = text; bad[bad.size() / 2] ^= 1; WriteFile(rec, bad); archive_cache::forget_memory();
          Check(!archive_cache::composed_known(place, mat, out), "layout: one changed character: nothing known"); archive_cache::forget_memory();
          WriteFile(rec, text.substr(0, text.size() - 5)); archive_cache::forget_memory(); Check(!archive_cache::composed_known(place, mat, out), "layout: cut off: nothing known");
          WriteFile(rec, "chaogarage-verified-archives 2\n" + text.substr(text.find('\n') + 1)); archive_cache::forget_memory(); Check(!archive_cache::composed_known(place, mat, out), "layout: another record's header: nothing known");
          // a record holding another result with a correct check line
          auto body = text.substr(0, text.rfind("end|")); auto at = body.find(out); auto edited = body.substr(0, at) + std::string(64, 'd') + body.substr(at + 64);
          edited += "end|" + archive_cache::text_hash(edited) + "\n"; WriteFile(rec, edited); archive_cache::forget_memory();
          Check(!archive_cache::composed_known(place, mat, out), "layout: a record holding another result: not vouched for");
          WriteFile(rec, text); archive_cache::forget_memory(); Check(archive_cache::composed_known(place, mat, out), "layout: (the right record again)"); archive_cache::forget_memory(); }
        // no folder: nothing read or written, nothing known across "starts"
        { archive_cache::set_directory(L""); archive_cache::composed_verified(place, mat, out); archive_cache::forget_memory();
          Check(!archive_cache::composed_known(place, mat, out), "layout: no record folder: never known"); archive_cache::set_directory(cache); }
    }

    std::printf("%u checks passed, %u failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
