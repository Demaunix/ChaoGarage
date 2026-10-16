#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <bcrypt.h>
#include <intrin.h>
#include <string>
#include "bootstrap_policy.h"
#include "chaogarage_release.h"
#include "loader_info.h"

// A named namespace, not an anonymous one: MSVC names anonymous namespaces after the source file's full path, which would make the DLL's bytes depend on the build folder.
namespace chaogarage_proxy {
HMODULE g_self = nullptr;
BYTE* g_game = nullptr;
void* g_startup_original = nullptr;
volatile LONG g_phase = 0; // 0 waiting,1 initializing,2 complete,3 disabled
DWORD g_initializing_thread = 0;
ssasr::InitResult g_result = ssasr::InitResult::NeverCalled;
INIT_ONCE g_system_once = INIT_ONCE_STATIC_INIT;
HMODULE g_system = nullptr;
FARPROC g_system_exports[6]{};

__declspec(noreturn) void Unrecoverable() {
    OutputDebugStringW(L"SSASR loader: host could not restore partial changes; refusing to start the game.\n");
    TerminateProcess(GetCurrentProcess(),static_cast<UINT>(SSASR_HOST_ROLLBACK_FAILED));
    __fastfail(7);
}

class WindowsIat final : public ssasr::IatOperations {
    bool Writable(void* address,DWORD& old) override {
        return VirtualProtect(address,sizeof(void*),PAGE_READWRITE,&old)!=FALSE;
    }
    void* CompareExchange(void* volatile* address,void* replacement,void* expected) override {
        return InterlockedCompareExchangePointer(address,replacement,expected);
    }
    bool RestoreProtection(void* address,DWORD old) override {
        DWORD ignored=0;return VirtualProtect(address,sizeof(void*),old,&ignored)!=FALSE;
    }
};

BOOL WINAPI InitializationActive() {
    return InterlockedCompareExchange(&g_phase,0,0)==1 && GetCurrentThreadId()==g_initializing_thread;
}

bool ModulePath(HMODULE module,std::wstring& path) {
    wchar_t buffer[32768];
    DWORD n=GetModuleFileNameW(module,buffer,32768);
    if (!n || n>=32768) return false;
    path.assign(buffer,n);return true;
}
bool ParentPath(const std::wstring& path,std::wstring& parent) {
    auto index=path.find_last_of(L"\\/");
    if (index==std::wstring::npos || index==0) return false;
    parent=path.substr(0,index);return true;
}

/* The host's info, read without trusting it: a damaged host must not crash the
   start, only produce a "cannot read" warning. No C++ objects live in this frame. */
bool ReadLoaderInfo(ChaoGarageGetLoaderInfoFn get,ChaoGarageLoaderInfoV1& copy,wchar_t (&release)[40]) {
    __try {
        const ChaoGarageLoaderInfoV1* info=get();
        if (!info || info->size<sizeof(ChaoGarageLoaderInfoV1) || info->version<CHAOGARAGE_LOADER_INFO_V1) return false;
        copy=*info;
        release[0]=0;
        if (info->release) { size_t i=0; for (;i<39 && info->release[i];++i) release[i]=info->release[i]; release[i]=0; }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

class WindowsEnvironment final : public ssasr::InitEnvironment {
    std::wstring executable_,directory_,host_release_;
    HMODULE host_=nullptr;
    SsasrModsHostInitializeFn initialize_=nullptr;
public:
    bool ResolveLocation() override {
        std::wstring proxy,parent;
        return ModulePath(nullptr,executable_) && ParentPath(executable_,directory_) &&
            ModulePath(g_self,proxy) && ParentPath(proxy,parent) &&
            CompareStringOrdinal(directory_.c_str(),-1,parent.c_str(),-1,TRUE)==CSTR_EQUAL;
    }
    bool HasCandidateMod() override {
        const std::wstring mods=directory_+L"\\mods";
        WIN32_FIND_DATAW data{};
        HANDLE search=FindFirstFileW((mods+L"\\*").c_str(),&data);
        if (search==INVALID_HANDLE_VALUE) return false;
        bool found=false;
        do {
            if (!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ||
                wcscmp(data.cFileName,L".")==0 || wcscmp(data.cFileName,L"..")==0) continue;
            const std::wstring manifest=mods+L"\\"+data.cFileName+L"\\mod.ini";
            DWORD attributes=GetFileAttributesW(manifest.c_str());
            if (attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY)) {found=true;break;}
        } while (FindNextFileW(search,&data));
        FindClose(search);return found;
    }
    bool HashExecutable(uint8_t hash[32]) override {
        HANDLE file=CreateFileW(executable_.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        if (file==INVALID_HANDLE_VALUE) return false;
        BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE digest=nullptr;
        bool okay=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
        if (okay) okay=BCryptCreateHash(algorithm,&digest,nullptr,0,nullptr,0,0)>=0;
        BYTE data[16384];DWORD count=0;
        while (okay) {
            if (!ReadFile(file,data,sizeof(data),&count,nullptr)) {okay=false;break;}
            if (!count) break;
            if (BCryptHashData(digest,data,count,0)<0) {okay=false;break;}
        }
        if (okay) okay=BCryptFinishHash(digest,hash,32,0)>=0;
        if (digest) BCryptDestroyHash(digest);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
        CloseHandle(file);return okay;
    }
    ssasr::HostLoad LoadHost(DWORD& error) override {
        const std::wstring path=directory_+L"\\ChaoGarage.dll";
        if (GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES) {
            error=GetLastError();
            return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND ? ssasr::HostLoad::NotFound : ssasr::HostLoad::LoadFailed;
        }
        host_=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!host_) { error=GetLastError(); return ssasr::HostLoad::LoadFailed; }
        initialize_=reinterpret_cast<SsasrModsHostInitializeFn>(GetProcAddress(host_,"SsasrModsHostInitialize"));
        /* Even a rejected host stays resident: unloading an extension that
           might have initialized callback state would be less predictable. */
        return initialize_ ? ssasr::HostLoad::Loaded : ssasr::HostLoad::NotALoader;
    }
    /* A host from before 1.1.0 has no ChaoGarageGetLoaderInfo and accepts only bootstrap version 1;
       it is never called by this proxy. A host from another 1.x release is not called either. */
    ssasr::Pairing CheckPair() override {
        auto get=reinterpret_cast<ChaoGarageGetLoaderInfoFn>(GetProcAddress(host_,CHAOGARAGE_GET_LOADER_INFO_NAME));
        if (!get) return ssasr::Pairing::Older;
        ChaoGarageLoaderInfoV1 info{};wchar_t release[40]{};
        if (!ReadLoaderInfo(get,info,release)) return ssasr::Pairing::InvalidInfo;
        host_release_=ssasr::CleanRelease(release,40);
        if (info.bootstrap_abi!=SSASR_BOOTSTRAP_ABI_V2 || host_release_!=CHAOGARAGE_RELEASE_W) return ssasr::Pairing::OtherRelease;
        return ssasr::Pairing::Match;
    }
    HRESULT InitializeHost(const uint8_t hash[32]) override {
        SsasrBootstrapV2 context{};
        context.v1.size=sizeof(context);context.v1.version=SSASR_BOOTSTRAP_ABI_V2;
        context.v1.game_image=reinterpret_cast<HMODULE>(g_game);context.v1.bootstrap_image=g_self;
        context.v1.game_directory=directory_.c_str();context.v1.executable_sha256=hash;
        context.v1.flags=SSASR_BOOTSTRAP_DISK_AUTHENTICATED|SSASR_BOOTSTRAP_NATIVE_CRT_GATE|
            SSASR_BOOTSTRAP_GAME_INITIALIZATION_NOT_STARTED;
        context.v1.initializing_thread_id=g_initializing_thread;context.v1.native_gate_rva=ssasr::kStartupCallRva;
        context.v1.initialization_is_active=&InitializationActive;
        context.bootstrap_release=CHAOGARAGE_RELEASE_W;
        try { return initialize_(&context.v1); }
        catch (...) { Unrecoverable(); }
    }
    /* Outside DllMain, on the game's main thread before its CRT starts (the host shows
       its warnings at the same point). user32 is loaded only when there is something to say. */
    void Warn(ssasr::Warning warning,DWORD code) override {
        const std::wstring text=ssasr::WarningText(warning,code,host_release_);
        AppendLog(text);
        OutputDebugStringW((L"ChaoGarage: "+text+L"\n").c_str());
        HMODULE user=LoadLibraryExW(L"user32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto box=user?reinterpret_cast<int (WINAPI*)(HWND,LPCWSTR,LPCWSTR,UINT)>(GetProcAddress(user,"MessageBoxW")):nullptr;
        if (box) box(nullptr,text.c_str(),ssasr::WarningTitle(),MB_OK|MB_ICONWARNING|MB_SETFOREGROUND);
    }
private:
    void AppendLog(const std::wstring& text) {
        const std::wstring mods=directory_+L"\\mods";
        DWORD a=GetFileAttributesW(mods.c_str());
        if (a==INVALID_FILE_ATTRIBUTES || !(a&FILE_ATTRIBUTE_DIRECTORY) || (a&FILE_ATTRIBUTE_REPARSE_POINT)) return;
        HANDLE h=CreateFileW((mods+L"\\SsasrMods.log").c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (h==INVALID_HANDLE_VALUE) return;
        SYSTEMTIME t;GetSystemTime(&t);wchar_t prefix[64];
        swprintf_s(prefix,L"%04u-%02u-%02u %02u:%02u:%02u UTC  dinput8.dll " CHAOGARAGE_RELEASE_W L": ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
        std::wstring line=prefix;
        for (wchar_t c:text) line+=(c==L'\r'||c==L'\n')?L' ':c;
        line+=L"\r\n";
        int n=WideCharToMultiByte(CP_UTF8,0,line.data(),(int)line.size(),nullptr,0,nullptr,nullptr);
        if (n>0) { std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,line.data(),(int)line.size(),s.data(),n,nullptr,nullptr);DWORD k;WriteFile(h,s.data(),n,&k,nullptr); }
        CloseHandle(h);
    }
};

bool ReadMetadata(BYTE* image) {
    __try { return reinterpret_cast<uintptr_t>(image)==ssasr::kGameBase && ssasr::SupportedMetadata(image,0x1000); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadStartupCaller(uintptr_t caller) {
    __try { return ssasr::SupportedStartupCaller(g_game,ssasr::kImageSize,caller); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void WINAPI StartupInformation(LPSTARTUPINFOA info) {
    const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    reinterpret_cast<decltype(&GetStartupInfoA)>(g_startup_original)(info);
    const DWORD native_error=GetLastError();
    if (ReadStartupCaller(caller) && InterlockedCompareExchange(&g_phase,1,0)==0) {
        g_initializing_thread=GetCurrentThreadId();
        WindowsIat operations;
        auto* slot=reinterpret_cast<void* volatile*>(g_game+ssasr::kGetStartupInfoIatRva);
        const auto result=ssasr::ReplaceIat(operations,slot,reinterpret_cast<void*>(&StartupInformation),g_startup_original);
        if (result==ssasr::IatResult::Replaced) {
            try {
                WindowsEnvironment environment;
                g_result=ssasr::Initialize(environment);
                if (g_result==ssasr::InitResult::UnrecoverablePartialPublication) Unrecoverable();
            } catch (...) {
                /* Host ABI requires errors as HRESULT and complete rollback.
                   This catches C++ allocation failures in bootstrap setup. */
                g_result=ssasr::InitResult::HostRejected;
                OutputDebugStringW(L"SSASR loader: initialization failed; no late retry.\n");
            }
            InterlockedExchange(&g_phase,2);
        } else {
            InterlockedExchange(&g_phase,3);
            OutputDebugStringW(L"SSASR loader: startup gate restoration failed; mods disabled.\n");
        }
    }
    SetLastError(native_error);
}

/* No allocation, paths, hashing, LoadLibrary, worker thread, hook builder,
   host initialization, file IO, user interface or game function calls occur here. */
void Attach(HMODULE module) {
    g_self=module;
    g_game=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    if (!ReadMetadata(g_game)) {g_phase=3;return;}
    WindowsIat operations;
    g_startup_original=reinterpret_cast<void*>(&GetStartupInfoA);
    auto* slot=reinterpret_cast<void* volatile*>(g_game+ssasr::kGetStartupInfoIatRva);
    if (ssasr::ReplaceIat(operations,slot,g_startup_original,reinterpret_cast<void*>(&StartupInformation))
        !=ssasr::IatResult::Replaced) g_phase=3;
}

BOOL CALLBACK LoadSystemDinput(PINIT_ONCE,PVOID,PVOID*) {
    wchar_t path[MAX_PATH+32];
    const UINT length=GetSystemDirectoryW(path,MAX_PATH);
    if (!length || length>=MAX_PATH) return TRUE;
    wcscpy_s(path+length,_countof(path)-length,L"\\dinput8.dll");
    HMODULE module=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module || module==g_self) return TRUE;
    static const char* names[]={"DirectInput8Create","DllCanUnloadNow","DllGetClassObject",
        "DllRegisterServer","DllUnregisterServer","GetdfDIJoystick"};
    FARPROC addresses[6]{};
    for (int i=0;i<6;++i) {
        addresses[i]=GetProcAddress(module,names[i]);
        if (!addresses[i]) return TRUE;
    }
    for (int i=0;i<6;++i) g_system_exports[i]=addresses[i];
    g_system=module;return TRUE;
}
FARPROC SystemExport(int index) {
    const DWORD native_error=GetLastError();
    InitOnceExecuteOnce(&g_system_once,LoadSystemDinput,nullptr,nullptr);
    SetLastError(native_error);
    return g_system?g_system_exports[index]:nullptr;
}
}

using namespace chaogarage_proxy;

extern "C" HRESULT WINAPI ProxyDirectInput8Create(HINSTANCE instance,DWORD version,REFIID iid,LPVOID* output,LPUNKNOWN outer) {
    auto f=reinterpret_cast<decltype(&DirectInput8Create)>(SystemExport(0));
    if (!f) {if (output)*output=nullptr;return E_FAIL;}
    return f(instance,version,iid,output,outer);
}
extern "C" HRESULT WINAPI ProxyDllCanUnloadNow() {
    auto f=reinterpret_cast<HRESULT(WINAPI*)()>(SystemExport(1));return f?f():S_FALSE;
}
extern "C" HRESULT WINAPI ProxyDllGetClassObject(REFCLSID id,REFIID iid,LPVOID* output) {
    auto f=reinterpret_cast<HRESULT(WINAPI*)(REFCLSID,REFIID,LPVOID*)>(SystemExport(2));
    if (!f) {if(output)*output=nullptr;return CLASS_E_CLASSNOTAVAILABLE;}return f(id,iid,output);
}
extern "C" HRESULT WINAPI ProxyDllRegisterServer() {
    auto f=reinterpret_cast<HRESULT(WINAPI*)()>(SystemExport(3));return f?f():E_FAIL;
}
extern "C" HRESULT WINAPI ProxyDllUnregisterServer() {
    auto f=reinterpret_cast<HRESULT(WINAPI*)()>(SystemExport(4));return f?f():E_FAIL;
}
extern "C" const DIDATAFORMAT* WINAPI ProxyGetdfDIJoystick() {
    auto f=reinterpret_cast<const DIDATAFORMAT*(WINAPI*)()>(SystemExport(5));return f?f():nullptr;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if (reason==DLL_PROCESS_ATTACH) Attach(instance);
    return TRUE;
}
