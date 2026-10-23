// A synthetic stand-in for the game process, used to run the SHIPPED ChaoGarage.dll through its real entry
// point. It contains no game code or data. It is linked at the game's image base (0x400000, fixed) and gives
// the host what it checks at the start-up gate: the ten file-function pointer slots at the game's import
// addresses (filled here with the real kernel32 functions), the gate RVA, the flags and an "initialization is
// active" callback for this thread. It is run on a separate desktop by run_e2e; a watcher thread records every
// message box the host shows (title and text) and presses OK, so the real MessageBoxW path is exercised.
//   fixture_game <game folder> <mode> <result.json> [runtime dll]
// modes: v2 | v2-other | v2-null | v2-host-release | v1 | bad-flags | inactive | twice | runtime-only
// If <game folder>\read-back.txt names a file (relative to the game folder), it is read after a successful start
// through the game's file-function slots (so through the loader's redirection) into <game folder>\read-back.bin.
#include "bootstrap_api.h"
#include "mod_api.h"
#include "loader_info.h"
#include "chaogarage_release.h"
#include <psapi.h>
#include <string>
#include <vector>
#include <set>

__declspec(align(4096)) static unsigned char g_space[0x600000];   // uninitialized: covers the game's import slots once linked at 0x400000
static volatile LONG g_active = 1;
static DWORD g_thread = 0;
static BOOL WINAPI Active() { return InterlockedCompareExchange(&g_active, 0, 0) == 1 && GetCurrentThreadId() == g_thread; }

struct Dialog { std::wstring title, text; };
static CRITICAL_SECTION g_lock;
static std::vector<Dialog> g_dialogs;
static std::set<HWND> g_seen;
static volatile LONG g_stop = 0;

static BOOL CALLBACK Visit(HWND w, LPARAM) {
    DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
    wchar_t cls[32]{};
    if (pid != GetCurrentProcessId() || !GetClassNameW(w, cls, 32) || wcscmp(cls, L"#32770") != 0 || !IsWindowVisible(w)) return TRUE;
    EnterCriticalSection(&g_lock);
    const bool first = g_seen.insert(w).second;
    if (first) {
        wchar_t title[512]{}; GetWindowTextW(w, title, 512);
        std::vector<wchar_t> text(65536); GetDlgItemTextW(w, 0xFFFF, text.data(), (int)text.size());
        g_dialogs.push_back({title, text.data()});
    }
    LeaveCriticalSection(&g_lock);
    PostMessageW(w, first ? WM_COMMAND : WM_CLOSE, first ? MAKEWPARAM(IDOK, BN_CLICKED) : 0, 0);
    return TRUE;
}
static DWORD WINAPI Watch(LPVOID) {
    while (!InterlockedCompareExchange(&g_stop, 0, 0)) { EnumWindows(Visit, 0); Sleep(15); }
    return 0;
}

static std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr); return s;
}
static std::string Json(const std::wstring& w) {
    std::string s = Utf8(w), r = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { r += '\\'; r += (char)c; }
        else if (c < 0x20) { char b[8]; sprintf_s(b, "\\u%04x", c); r += b; }
        else r += (char)c;
    }
    return r + "\"";
}

struct Slot { uintptr_t va; const char* name; };
static const Slot kSlots[] = {{0x67D04C, "CreateFileA"}, {0x67D0E0, "GetFileAttributesA"}, {0x67D0E4, "DeleteFileA"}, {0x67D0E8, "MoveFileA"},
    {0x67D060, "ReadFile"}, {0x67D198, "ReadFileEx"}, {0x67D070, "SetFilePointer"}, {0x67D064, "GetFileSize"}, {0x67D09C, "GetFileSizeEx"}, {0x67D03C, "CloseHandle"}};

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) return 64;
    const std::wstring game = argv[1], mode = argv[2], resultPath = argv[3];
    InitializeCriticalSection(&g_lock);
    g_thread = GetCurrentThreadId();
    std::string out = "{";
    auto field = [&](const char* k, const std::string& v) { if (out.size() > 1) out += ","; out += "\""; out += k; out += "\":"; out += v; };

    // the import slots must lie inside this image and hold the real functions, exactly as in the game
    const uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr), lo = (uintptr_t)g_space, hi = lo + sizeof(g_space);
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    bool layout = base == 0x400000 && lo <= 0x67D000 && 0x67D200 <= hi;
    if (layout) for (auto& s : kSlots) *(void**)s.va = (void*)GetProcAddress(kernel, s.name);
    field("layout_ok", layout ? "true" : "false");

    HANDLE watcher = CreateThread(nullptr, 0, Watch, nullptr, 0, nullptr);
    HRESULT hr = E_UNEXPECTED, hr2 = E_UNEXPECTED;
    if (layout && mode == L"runtime-only") {
        // a mod runtime called directly, as a loader from before 1.1.0 would (its ChaoGarage.dll is whatever is in the folder)
        LoadLibraryExW((game + L"\\ChaoGarage.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        HMODULE rt = argc > 4 ? LoadLibraryExW(argv[4], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32) : nullptr;
        auto init = rt ? (SsasrModInitializeFn)GetProcAddress(rt, "SsasrModInitialize") : nullptr;
        SsasrHostApiV1 api{sizeof(api), 1, GetModuleHandleW(nullptr), game.c_str(), SSASR_HOST_NATIVE_CRT_GATE, g_thread, 0x248F45, &Active, nullptr};
        hr = init ? init(&api, game.c_str()) : E_NOINTERFACE;
    } else if (layout) {
        HMODULE proxy = LoadLibraryExW((game + L"\\dinput8.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        HMODULE host = LoadLibraryExW((game + L"\\ChaoGarage.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto initialize = host ? (SsasrModsHostInitializeFn)GetProcAddress(host, "SsasrModsHostInitialize") : nullptr;
        auto info = host ? (ChaoGarageGetLoaderInfoFn)GetProcAddress(host, CHAOGARAGE_GET_LOADER_INFO_NAME) : nullptr;
        const wchar_t* hostRelease = nullptr;
        if (info) {
            auto i = info();
            std::string caps = "[";
            for (auto c = i->capabilities; c && *c; ++c) { if (caps.size() > 1) caps += ","; caps += "\""; caps += *c; caps += "\""; }
            field("info_release", Json(i->release)); field("info_capabilities", caps + "]");
            field("info_bootstrap_abi", std::to_string(i->bootstrap_abi)); field("info_host_abi", std::to_string(i->host_abi));
            hostRelease = i->release;
        }
        static const uint8_t exe[32] = {0xc1,0x10,0xb4,0xd3,0xb9,0x72,0x93,0x17,0xa9,0x3e,0x95,0x6f,0x1a,0x49,0xbe,0x98,0x9c,0x7a,0xd3,0xc2,0xaa,0xba,0x7c,0x54,0xc4,0x13,0x19,0xc1,0x04,0x38,0x1b,0x03};
        SsasrBootstrapV2 b{};
        b.v1.size = sizeof(b); b.v1.version = SSASR_BOOTSTRAP_ABI_V2; b.v1.game_image = GetModuleHandleW(nullptr); b.v1.bootstrap_image = proxy;
        b.v1.game_directory = game.c_str(); b.v1.executable_sha256 = exe; b.v1.flags = 7; b.v1.initializing_thread_id = g_thread;
        b.v1.native_gate_rva = 0x248F45; b.v1.initialization_is_active = &Active; b.bootstrap_release = CHAOGARAGE_RELEASE_W;
        if (mode == L"v2-other") b.bootstrap_release = L"9.9.9";
        if (mode == L"v2-null") b.bootstrap_release = nullptr;
        if (mode == L"v2-host-release") b.bootstrap_release = hostRelease;   // a matching pair of another release (the previous one, in run_e2e's --prev-release cases)
        if (mode == L"v1") { b.v1.size = sizeof(SsasrBootstrapV1); b.v1.version = SSASR_BOOTSTRAP_ABI_V1; }
        if (mode == L"bad-flags") b.v1.flags = 5;
        if (mode == L"inactive") InterlockedExchange(&g_active, 0);
        if (initialize) hr = initialize(&b.v1);
        if (initialize && mode == L"twice") hr2 = initialize(&b.v1);
        HANDLE request = CreateFileW((game + L"\\read-back.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hr == S_OK && request != INVALID_HANDLE_VALUE) {
            char rel[512]{}; DWORD got = 0; ReadFile(request, rel, sizeof(rel) - 1, &got, nullptr);
            std::string path = Utf8(game) + "\\" + std::string(rel, got);
            using Open = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
            using Read = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
            using Close = BOOL(WINAPI*)(HANDLE);
            HANDLE in = (*(Open*)0x67D04C)(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (in != INVALID_HANDLE_VALUE) {
                HANDLE dump = CreateFileW((game + L"\\read-back.bin").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
                std::vector<char> buffer(1 << 16); DWORD n = 0, k = 0;
                while ((*(Read*)0x67D060)(in, buffer.data(), (DWORD)buffer.size(), &n, nullptr) && n) WriteFile(dump, buffer.data(), n, &k, nullptr);
                CloseHandle(dump); (*(Close*)0x67D03C)(in);
            }
        }
        if (request != INVALID_HANDLE_VALUE) CloseHandle(request);
    }
    InterlockedExchange(&g_active, 0);
    Sleep(150);
    InterlockedExchange(&g_stop, 1);
    if (watcher) { WaitForSingleObject(watcher, 5000); CloseHandle(watcher); }

    char num[16]; sprintf_s(num, "%lu", (unsigned long)hr); field("hr", num); sprintf_s(num, "%lu", (unsigned long)hr2); field("hr2", num);   // unsigned: the loader's JSON reader has no negative numbers
    std::string slots = "[";
    if (layout) for (auto& s : kSlots) { if (slots.size() > 1) slots += ","; slots += *(void**)s.va == (void*)GetProcAddress(kernel, s.name) ? "\"original\"" : "\"hooked\""; }
    field("slots", slots + "]");
    std::string dialogs = "[";
    for (auto& d : g_dialogs) { if (dialogs.size() > 1) dialogs += ","; dialogs += "{\"title\":" + Json(d.title) + ",\"text\":" + Json(d.text) + "}"; }
    field("dialogs", dialogs + "]");
    // which mod runtimes are loaded in this process (paths under the game folder's mods)
    std::string mods = "[";
    HMODULE list[1024]; DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), list, sizeof(list), &needed))
        for (DWORD i = 0; i < needed / sizeof(HMODULE) && i < 1024; ++i) {
            wchar_t p[32768]; DWORD n = GetModuleFileNameW(list[i], p, 32768);
            std::wstring path(p, n), prefix = game + L"\\mods\\";
            if (n && _wcsnicmp(path.c_str(), prefix.c_str(), prefix.size()) == 0) { if (mods.size() > 1) mods += ","; mods += Json(path.substr(prefix.size())); }
        }
    field("loaded_runtimes", mods + "]");
    out += "}";
    HANDLE f = CreateFileW(resultPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return 65;
    DWORD k = 0; WriteFile(f, out.data(), (DWORD)out.size(), &k, nullptr); CloseHandle(f);
    return 0;
}
