// The proxy's decision order and warnings (with an injected environment), the startup-gate and IAT rules,
// and the shipped dinput8.dll forwarding to the system DirectInput. No game is used.
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include "bootstrap_policy.h"
#include "chaogarage_release.h"

static unsigned passed=0;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); return false; } } while (0)

struct MockEnvironment final : ssasr::InitEnvironment {
    bool location=true,mods=true,hash_read=true,supported=true;
    ssasr::HostLoad host=ssasr::HostLoad::Loaded;DWORD host_error=0;
    ssasr::Pairing pair=ssasr::Pairing::Match;
    HRESULT result=S_OK;std::string events;std::vector<ssasr::Warning> warnings;std::vector<DWORD> codes;
    bool ResolveLocation() override {events+='L';return location;}
    bool HasCandidateMod() override {events+='M';return mods;}
    bool HashExecutable(uint8_t hash[32]) override {events+='H';memcpy(hash,ssasr::kGameSha256,32);if(!supported)hash[9]^=1;return hash_read;}
    ssasr::HostLoad LoadHost(DWORD& error) override {events+='D';error=host_error;return host;}
    ssasr::Pairing CheckPair() override {events+='P';return pair;}
    HRESULT InitializeHost(const uint8_t hash[32]) override {events+='I';return ssasr::EqualSha256(hash)?result:E_UNEXPECTED;}
    void Warn(ssasr::Warning w,DWORD code) override {events+='W';warnings.push_back(w);codes.push_back(code);}
};
using ssasr::InitResult;using ssasr::Warning;
bool One(MockEnvironment& e,InitResult want,const char* events,int warning=-1,DWORD code=0) {
    CHECK(ssasr::Initialize(e)==want);
    CHECK(e.events==events);
    if (warning<0) CHECK(e.warnings.empty());
    else CHECK(e.warnings.size()==1 && e.warnings[0]==static_cast<Warning>(warning) && e.codes[0]==code);
    ++passed;return true;
}
bool PolicyCases() {
    /* silent: not next to the game, or no mod -> the game starts exactly as without the loader */
    { MockEnvironment e;e.location=false;if(!One(e,InitResult::InvalidLocation,"L"))return false; }
    { MockEnvironment e;e.mods=false;if(!One(e,InitResult::NoMods,"LM"))return false; }
    /* every other refusal is explained exactly once, and nothing later runs */
    { MockEnvironment e;e.hash_read=false;if(!One(e,InitResult::UnknownExecutable,"LMHW",(int)Warning::ExecutableUnreadable))return false; }
    { MockEnvironment e;e.supported=false;if(!One(e,InitResult::UnknownExecutable,"LMHW",(int)Warning::UnsupportedExecutable))return false; }
    { MockEnvironment e;e.host=ssasr::HostLoad::NotFound;e.host_error=ERROR_FILE_NOT_FOUND;if(!One(e,InitResult::HostUnavailable,"LMHDW",(int)Warning::HostMissing,ERROR_FILE_NOT_FOUND))return false; }
    { MockEnvironment e;e.host=ssasr::HostLoad::LoadFailed;e.host_error=ERROR_BAD_EXE_FORMAT;if(!One(e,InitResult::HostUnavailable,"LMHDW",(int)Warning::HostNotLoadable,ERROR_BAD_EXE_FORMAT))return false; }
    { MockEnvironment e;e.host=ssasr::HostLoad::NotALoader;if(!One(e,InitResult::HostUnavailable,"LMHDW",(int)Warning::HostNotALoader))return false; }
    /* mixed pairs: a 1.0 host (no info export), another release, unreadable info -> the host is never called */
    { MockEnvironment e;e.pair=ssasr::Pairing::Older;if(!One(e,InitResult::PairMismatch,"LMHDPW",(int)Warning::HostOlder))return false; }
    { MockEnvironment e;e.pair=ssasr::Pairing::OtherRelease;if(!One(e,InitResult::PairMismatch,"LMHDPW",(int)Warning::HostOtherRelease))return false; }
    { MockEnvironment e;e.pair=ssasr::Pairing::InvalidInfo;if(!One(e,InitResult::PairMismatch,"LMHDPW",(int)Warning::HostInvalid))return false; }
    /* host results: the host explains its own refusals; only an unexpected failure is explained here */
    { MockEnvironment e;if(!One(e,InitResult::Loaded,"LMHDPI"))return false; }
    { MockEnvironment e;e.result=S_FALSE;if(!One(e,InitResult::NoMods,"LMHDPI"))return false; }
    { MockEnvironment e;e.result=CHAOGARAGE_HOST_NOT_STARTED_WARNED;if(!One(e,InitResult::HostRejected,"LMHDPI"))return false; }
    { MockEnvironment e;e.result=E_INVALIDARG;if(!One(e,InitResult::HostRejected,"LMHDPIW",(int)Warning::HostUnexpected,(DWORD)E_INVALIDARG))return false; }
    { MockEnvironment e;e.result=SSASR_HOST_ROLLBACK_FAILED;if(!One(e,InitResult::UnrecoverablePartialPublication,"LMHDPI"))return false; }
    return true;
}
bool TextCases() {
    const std::wstring url=L"https://github.com/Demaunix/ChaoGarage/releases/latest";
    for (int w=0;w<=(int)Warning::HostUnexpected;++w) {
        auto t=ssasr::WarningText(static_cast<Warning>(w),5,L"0.9.0");
        CHECK(t.find(L"The game will start without mods.")==0);
        CHECK(t.find(L"Nothing was downloaded or opened.")!=std::wstring::npos);
        bool pairOrHost=w>=(int)Warning::HostMissing && w<=(int)Warning::HostInvalid;
        CHECK((t.find(url)!=std::wstring::npos)==pairOrHost);   /* the download address only where installing the loader is the fix */
        ++passed;
    }
    CHECK(ssasr::WarningText(Warning::HostOtherRelease,0,L"0.9.0").find(L"ChaoGarage.dll is release 0.9.0")!=std::wstring::npos);
    CHECK(ssasr::WarningText(Warning::HostOlder,0,L"").find(L"earlier release (before 1.1.0)")!=std::wstring::npos);
    CHECK(ssasr::WarningText(Warning::UnsupportedExecutable,0,L"").find(L"Updating the loader does not change this.")!=std::wstring::npos);
    CHECK(ssasr::WarningText(Warning::HostUnexpected,0x80070057,L"").find(L"0x80070057")!=std::wstring::npos);
    const wchar_t evil[]=L"1.1\x202e\n0123456789012345678901234567890123456789";
    auto clean=ssasr::CleanRelease(evil,_countof(evil));
    CHECK(clean.find(L'\x202e')==std::wstring::npos && clean.find(L'\n')==std::wstring::npos && clean.size()<=35);
    CHECK(std::wstring(ssasr::WarningTitle())==L"ChaoGarage " CHAOGARAGE_RELEASE_W L" - mods were not started");
    passed+=6;return true;
}
struct MockIat final : ssasr::IatOperations {
    bool writable=true,restore=true;unsigned writes=0,protects=0;
    bool Writable(void*,DWORD& old) override {old=PAGE_READONLY;++protects;return writable;}
    void* CompareExchange(void* volatile* address,void* replacement,void* expected) override {
        void* old=*address;if(old==expected){*address=replacement;++writes;}return old;
    }
    bool RestoreProtection(void*,DWORD old) override {++protects;return old==PAGE_READONLY&&restore;}
};
bool IatCases() {
    void* const original=reinterpret_cast<void*>(0x11110000);void* const hook=reinterpret_cast<void*>(0x22220000);
    void* volatile slot=original;MockIat good;
    CHECK(ssasr::ReplaceIat(good,&slot,original,hook)==ssasr::IatResult::Replaced&&slot==hook&&good.writes==1);++passed;
    CHECK(ssasr::ReplaceIat(good,&slot,hook,original)==ssasr::IatResult::Replaced&&slot==original&&good.writes==2);++passed;
    MockIat denied;denied.writable=false;
    CHECK(ssasr::ReplaceIat(denied,&slot,original,hook)==ssasr::IatResult::ProtectedFailure&&slot==original&&denied.writes==0);++passed;
    MockIat foreign;slot=reinterpret_cast<void*>(0x33330000);
    CHECK(ssasr::ReplaceIat(foreign,&slot,original,hook)==ssasr::IatResult::ForeignPointer&&slot==reinterpret_cast<void*>(0x33330000)&&foreign.writes==0);++passed;
    MockIat fail;fail.restore=false;slot=original;
    CHECK(ssasr::ReplaceIat(fail,&slot,original,hook)==ssasr::IatResult::RestoreFailure&&slot==original&&fail.writes==2);++passed;
    return true;
}
void SeedMetadata(BYTE* image) {
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x100;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(image+0x100);nt->Signature=IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine=IMAGE_FILE_MACHINE_I386;nt->FileHeader.TimeDateStamp=0x4B700995;nt->FileHeader.NumberOfSections=6;
    nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER32);auto& o=nt->OptionalHeader;
    o.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;o.ImageBase=ssasr::kGameBase;o.SizeOfImage=ssasr::kImageSize;
    o.AddressOfEntryPoint=0xAF82ED;o.NumberOfRvaAndSizes=16;
    o.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]={0x2E71C4,0x118};
    o.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT]={0x27D000,0x2CC};
}
bool GateCases() {
    BYTE image[4096]{};SeedMetadata(image);CHECK(ssasr::SupportedMetadata(image,sizeof(image)));++passed;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(image+0x100);
    ++nt->FileHeader.TimeDateStamp;CHECK(!ssasr::SupportedMetadata(image,sizeof(image)));++passed;
    --nt->FileHeader.TimeDateStamp;nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress=1;
    CHECK(!ssasr::SupportedMetadata(image,sizeof(image)));++passed;
    CHECK(!ssasr::SupportedMetadata(image,128));++passed;
    BYTE bytes[]={0x6a,0x60,0x68,0xc8,0x6b,0x6e,0x00,0xe8,0x1f,0x0c,0x00,0x00,
        0x83,0x65,0xfc,0x00,0x8d,0x45,0x90,0x50,0xff,0x15,0x28,0xd1,0x67,0x00,
        0xc7,0x45,0xfc,0xfe,0xff,0xff,0xff};
    CHECK(ssasr::SupportedStartupSpan(0x400000,0x648F4B,bytes,sizeof(bytes)));++passed;
    CHECK(!ssasr::SupportedStartupSpan(0x400000,0x6544AC,bytes,sizeof(bytes)));++passed;
    CHECK(!ssasr::SupportedStartupSpan(0x410000,0x648F4B,bytes,sizeof(bytes)));++passed;
    CHECK(!ssasr::SupportedStartupSpan(0x400000,0x648F4B,bytes,sizeof(bytes)-1));++passed;
    bytes[20]^=1;CHECK(!ssasr::SupportedStartupSpan(0x400000,0x648F4B,bytes,sizeof(bytes)));++passed;
    return true;
}
bool ProxyCases() {
    wchar_t path[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);CHECK(n>0&&n<32768);
    wchar_t* end=wcsrchr(path,L'\\');CHECK(end!=nullptr);wcscpy_s(end+1,32768-(end+1-path),L"dinput8.dll");
    HMODULE proxy=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);CHECK(proxy!=nullptr);
    /* Unknown fixture EXE metadata makes attach a pure pass-through; genuine
       dinput is loaded only on an exported call, never in this DllMain. */
    wchar_t system[MAX_PATH+32];UINT len=GetSystemDirectoryW(system,MAX_PATH);CHECK(len&&len<MAX_PATH);
    wcscpy_s(system+len,_countof(system)-len,L"\\dinput8.dll");
    HMODULE real=LoadLibraryExW(system,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);CHECK(real&&real!=proxy);
    const char* names[]={"DirectInput8Create","DllCanUnloadNow","DllGetClassObject","DllRegisterServer","DllUnregisterServer","GetdfDIJoystick"};
    for(int i=0;i<6;++i){CHECK(GetProcAddress(proxy,names[i])==GetProcAddress(proxy,MAKEINTRESOURCEA(i+1)));++passed;}
    using NoArgs=HRESULT(WINAPI*)();
    CHECK(reinterpret_cast<NoArgs>(GetProcAddress(proxy,names[1]))()==reinterpret_cast<NoArgs>(GetProcAddress(real,names[1]))());++passed;
    using Format=const DIDATAFORMAT*(WINAPI*)();
    CHECK(reinterpret_cast<Format>(GetProcAddress(proxy,names[5]))()==reinterpret_cast<Format>(GetProcAddress(real,names[5]))());++passed;
    using GetClass=HRESULT(WINAPI*)(REFCLSID,REFIID,LPVOID*);
    GUID invalid{};void* a=reinterpret_cast<void*>(1);void* b=reinterpret_cast<void*>(1);
    HRESULT pa=reinterpret_cast<GetClass>(GetProcAddress(proxy,names[2]))(invalid,invalid,&a);
    HRESULT pb=reinterpret_cast<GetClass>(GetProcAddress(real,names[2]))(invalid,invalid,&b);
    CHECK(pa==pb&&a==b);++passed;
    using Create=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);
    a=reinterpret_cast<void*>(1);b=reinterpret_cast<void*>(1);
    pa=reinterpret_cast<Create>(GetProcAddress(proxy,names[0]))(GetModuleHandleW(nullptr),0,invalid,&a,nullptr);
    pb=reinterpret_cast<Create>(GetProcAddress(real,names[0]))(GetModuleHandleW(nullptr),0,invalid,&b,nullptr);
    CHECK(pa==pb&&a==b);++passed;
    /* Registration/unregistration exports are checked but never invoked:
       testing those would modify the user's machine registration. */
    FreeLibrary(real);FreeLibrary(proxy);return true;
}
int main() {
    if (!PolicyCases()||!TextCases()||!IatCases()||!GateCases()||!ProxyCases())return 1;
    printf("PASS: %u native policy, warning-text, IAT, exact gate and genuine-system forwarding cases. No game access.\n",passed);
    return 0;
}
