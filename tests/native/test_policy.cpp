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
#include <bcrypt.h>
#include "bootstrap_policy.h"
#include "chaogarage_release.h"

static unsigned passed=0;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); return false; } } while (0)

struct MockEnvironment final : ssasr::InitEnvironment {
    bool location=true,mods=true,hash_read=true,supported=true;
    /* 1.3.2: patched = the exe is Steam's with the large-address patch (the raw hash differs, the normalized hash is Steam's);
       bit = the large-address bit is set in the file; other_diff = some other byte differs too; norm_read = it could be parsed. */
    bool patched=false,bit=true,other_diff=false,norm_read=true;std::vector<ssasr::Notice> notices;
    ssasr::HostLoad host=ssasr::HostLoad::Loaded;DWORD host_error=0;
    ssasr::Pairing pair=ssasr::Pairing::Match;
    HRESULT result=S_OK;std::string events;std::vector<ssasr::Warning> warnings;std::vector<DWORD> codes;
    bool ResolveLocation() override {events+='L';return location;}
    bool HasCandidateMod() override {events+='M';return mods;}
    bool HashExecutable(uint8_t hash[32]) override {events+='H';memcpy(hash,ssasr::kGameSha256,32);if(!supported||patched)hash[9]^=1;return hash_read;}
    bool HashExecutableNormalized(uint8_t hash[32],bool& largeAddress) override {
        events+='N';memcpy(hash,ssasr::kGameSha256,32);if(!patched||other_diff)hash[3]^=1;largeAddress=bit;return norm_read;}
    void Note(ssasr::Notice n) override {events+='n';notices.push_back(n);}
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
    { MockEnvironment e;e.supported=false;if(!One(e,InitResult::UnknownExecutable,"LMHNW",(int)Warning::UnsupportedExecutable))return false; }
    /* 1.3.2: the Steam exe with only the 4 GB patch is accepted, with one log note and no warning; the host is given Steam's hash */
    { MockEnvironment e;e.patched=true;if(!One(e,InitResult::Loaded,"LMHNnDPI"))return false;CHECK(e.notices.size()==1&&e.notices[0]==ssasr::Notice::LargeAddressPatchAccepted); }
    { MockEnvironment e;e.patched=true;e.other_diff=true;if(!One(e,InitResult::UnknownExecutable,"LMHNW",(int)Warning::UnsupportedExecutable))return false;CHECK(e.notices.empty()); }  /* bit + another byte */
    { MockEnvironment e;e.patched=true;e.bit=false;if(!One(e,InitResult::UnknownExecutable,"LMHNW",(int)Warning::UnsupportedExecutable))return false;CHECK(e.notices.empty()); }        /* checksum alone: not the patch */
    { MockEnvironment e;e.patched=true;e.norm_read=false;if(!One(e,InitResult::UnknownExecutable,"LMHNW",(int)Warning::UnsupportedExecutable))return false; }                          /* not parseable */
    { MockEnvironment e;if(!One(e,InitResult::Loaded,"LMHDPI"))return false;CHECK(e.notices.empty()); }                                                                          /* the Steam exe itself: no second pass, no note */
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
/* SHA-256 of a buffer (the test's own, so that it does not rely on the code under test). */
static bool Sha256(const BYTE* data,size_t n,uint8_t out[32]) {
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE h=nullptr;
    bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0&&BCryptCreateHash(alg,&h,nullptr,0,nullptr,0,0)>=0;
    for (size_t at=0;ok&&at<n;) { ULONG k=(ULONG)((n-at)<65536?(n-at):65536);ok=BCryptHashData(h,const_cast<BYTE*>(data+at),k,0)>=0;at+=k; }
    ok=ok&&BCryptFinishHash(h,out,32,0)>=0;if(h)BCryptDestroyHash(h);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok;
}
/* The normalized hash the way the proxy computes it: the bytes chunk by chunk through NormalizeExeChunk. */
static bool NormalizedHash(const std::vector<BYTE>& file,size_t chunk,uint8_t out[32],bool* bit=nullptr) {
    ssasr::ExePatchFields f;if(!ssasr::LocateExePatchFields(file.data(),file.size()<1024?file.size():1024,f))return false;
    if(bit)*bit=f.largeAddressBit;
    std::vector<BYTE> copy;
    for (size_t at=0;at<file.size();at+=chunk) { size_t n=file.size()-at<chunk?file.size()-at:chunk;std::vector<BYTE> c(file.begin()+at,file.begin()+at+n);ssasr::NormalizeExeChunk(f,at,c.data(),n);copy.insert(copy.end(),c.begin(),c.end()); }
    return Sha256(copy.data(),copy.size(),out);
}
static IMAGE_NT_HEADERS32* Nt(std::vector<BYTE>& f) { return reinterpret_cast<IMAGE_NT_HEADERS32*>(f.data()+reinterpret_cast<IMAGE_DOS_HEADER*>(f.data())->e_lfanew); }
static void SetBit(std::vector<BYTE>& f,bool on) { if(on)Nt(f)->FileHeader.Characteristics|=IMAGE_FILE_LARGE_ADDRESS_AWARE;else Nt(f)->FileHeader.Characteristics&=(WORD)~IMAGE_FILE_LARGE_ADDRESS_AWARE; }
static void SetChecksum(std::vector<BYTE>& f,DWORD v) { Nt(f)->OptionalHeader.CheckSum=v; }
/* Writes the bytes to a file in the temp folder, runs the proxy's HashExecutableFile on it, and deletes the file. */
static bool FileHash(const std::vector<BYTE>& bytes,bool normalized,uint8_t out[32],bool* bit) {
    wchar_t dir[MAX_PATH];if(!GetTempPathW(MAX_PATH,dir))return false;
    std::wstring path=std::wstring(dir)+L"chaogarage-policy-test-"+std::to_wstring(GetCurrentProcessId())+L".bin";
    HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY,nullptr);if(h==INVALID_HANDLE_VALUE)return false;
    DWORD put=0;bool ok=bytes.empty()||WriteFile(h,bytes.data(),(DWORD)bytes.size(),&put,nullptr);CloseHandle(h);
    bool result=ok&&ssasr::HashExecutableFile(path.c_str(),normalized,out,bit);DeleteFileW(path.c_str());return result;
}
/* `base` = a Steam-like file (the synthetic one, or a copy of the real exe): bit clear, checksum zero. */
static bool ExeVariants(const std::vector<BYTE>& base) {
    uint8_t want[32],got[32];bool bit=false;
    CHECK(NormalizedHash(base,4096,want,&bit)&&!bit);++passed;
    auto same=[&](const std::vector<BYTE>& f,bool expectBit){ return NormalizedHash(f,4096,got,&bit)&&memcmp(got,want,32)==0&&bit==expectBit; };
    auto differs=[&](const std::vector<BYTE>& f){ return NormalizedHash(f,4096,got,&bit)&&memcmp(got,want,32)!=0; };
    { auto f=base;SetBit(f,true);CHECK(same(f,true));++passed; }                                   /* the patch, checksum untouched */
    { auto f=base;SetBit(f,true);SetChecksum(f,0xDEADBEEF);CHECK(same(f,true));++passed; }          /* the patch with its own checksum */
    { auto f=base;SetChecksum(f,0x12345);CHECK(same(f,false));++passed; }                          /* checksum alone: equal normalized, but the bit is clear (the proxy refuses it) */
    { auto f=base;SetBit(f,true);f[f.size()-1]^=1;CHECK(differs(f));++passed; }                    /* bit + the last byte */
    { auto f=base;SetBit(f,true);f[base.size()/2]^=1;CHECK(differs(f));++passed; }                 /* bit + a middle byte */
    { auto f=base;SetBit(f,true);f[0x40]^=1;CHECK(differs(f));++passed; }                          /* bit + a byte of the DOS stub */
    { auto f=base;SetBit(f,true);Nt(f)->FileHeader.Characteristics^=0x2;CHECK(differs(f));++passed; }   /* another Characteristics bit */
    { auto f=base;SetBit(f,true);Nt(f)->FileHeader.TimeDateStamp^=1;CHECK(differs(f));++passed; }       /* a header field next to the two */
    { auto f=base;SetBit(f,true);Nt(f)->OptionalHeader.DllCharacteristics^=1;CHECK(differs(f));++passed; } /* a field next to the checksum */
    for (int d: {21,23,87,92}) {                                                                    /* the bytes just before and after each field (23 = Characteristics' high byte) */
        auto f=base;SetBit(f,true);f[reinterpret_cast<IMAGE_DOS_HEADER*>(f.data())->e_lfanew+d]^=1;CHECK(differs(f));++passed; }
    { auto f=base;SetBit(f,true);f.push_back(0);CHECK(differs(f));++passed; }                      /* a longer file */
    { auto f=base;SetBit(f,true);f.pop_back();CHECK(differs(f));++passed; }                        /* a shorter file */
    for (size_t chunk: {size_t(1),size_t(3),size_t(1000),size_t(16384),base.size()+5}) {            /* the proxy's streaming and one pass give the same hash */
        auto f=base;SetBit(f,true);SetChecksum(f,0xCAFE);uint8_t a[32];CHECK(NormalizedHash(f,chunk,a)&&memcmp(a,want,32)==0);++passed; }
    /* the proxy's own file code (HashExecutableFile), on real files in the temp folder */
    uint8_t h[32];bool b=false;
    { auto f=base;SetBit(f,true);SetChecksum(f,0xAB);CHECK(FileHash(f,true,h,&b)&&b&&memcmp(h,want,32)==0);++passed; }       /* the patched file */
    { CHECK(FileHash(base,true,h,&b)&&!b&&memcmp(h,want,32)==0);++passed; }                                                     /* the Steam file itself */
    { auto f=base;SetBit(f,true);f[base.size()/3]^=1;CHECK(FileHash(f,true,h,&b)&&b&&memcmp(h,want,32)!=0);++passed; }         /* bit + one byte */
    { auto f=base;SetChecksum(f,5);CHECK(FileHash(f,true,h,&b)&&!b&&memcmp(h,want,32)==0);++passed; }                          /* checksum only: same hash, bit clear */
    { auto f=base;SetBit(f,true);uint8_t raw[32],rawFile[32];CHECK(Sha256(f.data(),f.size(),raw)&&FileHash(f,false,rawFile,nullptr)&&memcmp(raw,rawFile,32)==0);++passed; }  /* the plain hash is unchanged */
    { std::vector<BYTE> junk={'n','o','t',' ','a',' ','P','E'};CHECK(!FileHash(junk,true,h,&b)&&!b);++passed; }               /* not a PE file */
    CHECK(!ssasr::HashExecutableFile(L"Z:\\no\\such\\file.exe",true,h,&b)&&!ssasr::HashExecutableFile(L"Z:\\no\\such\\file.exe",false,h,nullptr));++passed;
    return true;
}
bool ExeCases() {
    std::vector<BYTE> base(20000);for (size_t i=0;i<base.size();++i)base[i]=(BYTE)(i*131+7);   /* made-up bytes, then a valid header */
    SeedMetadata(base.data());
    Nt(base)->FileHeader.Characteristics=0x103;Nt(base)->OptionalHeader.CheckSum=0;
    if (!ExeVariants(base))return false;
    /* the header reader refuses what is not a PE32 file it can place the two fields in */
    ssasr::ExePatchFields f;
    CHECK(!ssasr::LocateExePatchFields(base.data(),0x100,f)&&!f.valid);++passed;            /* too short */
    CHECK(!ssasr::LocateExePatchFields(base.data(),0x100+4+20+67,f));++passed;                /* cut before the checksum ends */
    CHECK(ssasr::LocateExePatchFields(base.data(),0x100+4+20+68,f)&&f.valid&&f.checksum==0x100+24+64&&f.characteristics==0x100+22);++passed;
    { auto b=base;b[0]='X';CHECK(!ssasr::LocateExePatchFields(b.data(),b.size(),f));++passed; }
    { auto b=base;reinterpret_cast<IMAGE_DOS_HEADER*>(b.data())->e_lfanew=0x20;CHECK(!ssasr::LocateExePatchFields(b.data(),b.size(),f));++passed; }
    { auto b=base;reinterpret_cast<IMAGE_DOS_HEADER*>(b.data())->e_lfanew=0x800;CHECK(!ssasr::LocateExePatchFields(b.data(),b.size(),f));++passed; }
    { auto b=base;b[0x100]='Q';CHECK(!ssasr::LocateExePatchFields(b.data(),b.size(),f));++passed; }
    { auto b=base;Nt(b)->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;CHECK(!ssasr::LocateExePatchFields(b.data(),b.size(),f));++passed; }
    { auto b=base;Nt(b)->FileHeader.SizeOfOptionalHeader=60;CHECK(!ssasr::LocateExePatchFields(b.data(),b.size(),f));++passed; }
    /* the bit and the checksum do not change what the start-up checks see of the loaded image's header */
    { BYTE image[4096]{};SeedMetadata(image);auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(image+0x100);
      nt->FileHeader.Characteristics=0x103;CHECK(ssasr::SupportedMetadata(image,sizeof(image)));++passed;
      nt->FileHeader.Characteristics=0x123;nt->OptionalHeader.CheckSum=0x77;CHECK(ssasr::SupportedMetadata(image,sizeof(image)));++passed; }
    return true;
}
/* test_policy.exe --exe <a COPY of the Steam exe>: the same variants on the real file's bytes, in memory (the file is only read). */
static int RealExe(const wchar_t* path) {
    HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE){printf("cannot open the copy\n");return 2;}
    LARGE_INTEGER n{};GetFileSizeEx(h,&n);std::vector<BYTE> b((size_t)n.QuadPart);DWORD got=0;bool ok=ReadFile(h,b.data(),(DWORD)b.size(),&got,nullptr)&&got==b.size();CloseHandle(h);if(!ok){printf("cannot read the copy\n");return 2;}
    uint8_t raw[32];if(!Sha256(b.data(),b.size(),raw)||!ssasr::EqualSha256(raw)){printf("the copy is not the Steam exe\n");return 2;}
    if(!ExeVariants(b))return 1;
    { uint8_t norm[32];if(!NormalizedHash(b,4096,norm)||!ssasr::EqualNormalizedSha256(norm)||memcmp(norm,raw,32)!=0){printf("the Steam exe's normalized hash is not the pinned value\n");return 1;}++passed; }
    printf("PASS: %u cases including the real Steam exe copy (%zu bytes)\n",passed,b.size());return 0;
}
int wmain(int argc,wchar_t** argv) {
    if (argc==3&&std::wstring(argv[1])==L"--exe") return RealExe(argv[2]);
    if (!PolicyCases()||!TextCases()||!IatCases()||!GateCases()||!ExeCases()||!ProxyCases())return 1;
    printf("PASS: %u native policy, warning-text, IAT, exact gate and genuine-system forwarding cases. No game access.\n",passed);
    return 0;
}
