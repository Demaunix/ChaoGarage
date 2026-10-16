// A synthetic mod runtime for the end-to-end tests. It does nothing in any game: it records each call in
// <game folder>\calls.log and behaves as its mod folder's behavior.txt says: ok (default), fail, unrecoverable,
// rollback-fails.
#include "mod_api.h"
#include <string>

static std::wstring g_dir, g_game;
static std::wstring Leaf(const std::wstring& p) { auto i = p.find_last_of(L"\\/"); return i == std::wstring::npos ? p : p.substr(i + 1); }
static void Append(const std::wstring& line) {
    if (g_game.empty()) return;
    HANDLE h = CreateFileW((g_game + L"\\calls.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    std::wstring w = line + L"\n";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    DWORD k = 0; WriteFile(h, s.data(), (DWORD)s.size(), &k, nullptr); CloseHandle(h);
}
static std::string Behavior() {
    HANDLE h = CreateFileW((g_dir + L"\\behavior.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "ok";
    char b[64]{}; DWORD got = 0; ReadFile(h, b, sizeof(b) - 1, &got, nullptr); CloseHandle(h);
    std::string s(b, got); while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}
extern "C" HRESULT __cdecl SsasrModInitialize(const SsasrHostApiV1* api, const wchar_t* dir) {
    if (!api || !dir || api->size != sizeof(SsasrHostApiV1) || api->version != SSASR_HOST_ABI_V1) return E_INVALIDARG;
    g_dir = dir; g_game = api->game_directory ? api->game_directory : L"";
    Append(L"init " + Leaf(g_dir));
    auto b = Behavior();
    if (b == "fail") return E_FAIL;
    if (b == "unrecoverable") return SSASR_HOST_ROLLBACK_FAILED;
    return S_OK;
}
extern "C" HRESULT __cdecl SsasrModRollback() {
    Append(L"rollback " + Leaf(g_dir));
    return Behavior() == "rollback-fails" ? E_FAIL : S_OK;
}
