#include "mod_api.h"
#include "loader_info.h"
#include "chaogarage_release.h"
#include "resources.h"
#include "virtual_io.h"
#include "member_compose.h"
#include "legacy_cache.h"
#include "compat.h"
#include "messages.h"
#include "capabilities.h"
#include "pending_updates.h"
#include <cwctype>
#include <numeric>
using namespace ssasr;
namespace msg = ssasr::messages;
using msg::Cat;
// A named namespace, not an anonymous one: MSVC names anonymous namespaces after the source file's full path, which would make the DLL's bytes depend on the build folder.
namespace chaogarage_host {
constexpr HRESULT Unrecoverable=SSASR_HOST_ROLLBACK_FAILED;
constexpr HRESULT NotStartedWarned=CHAOGARAGE_HOST_NOT_STARTED_WARNED;
std::wstring gameRoot,logPath;
volatile LONG hostEntered=0;
std::map<std::wstring,Redirect> redirects;
struct Hook{uintptr_t address;void* before;void* after;bool applied=false;DWORD originalProtection=0;bool protectionDirty=false;};
std::vector<Hook> hooks;
struct Mod{std::wstring dir,runtime;HMODULE module=nullptr;SsasrModInitializeFn initialize=nullptr;SsasrModRollbackFn rollback=nullptr;bool started=false;std::wstring id,label,version;int startRank=2;};
std::vector<Mod>mods;
// 1.3.2: what the summary block at the end of the start lists (summary()): folders not started on purpose (one plain sentence
// each), and every ChaoGarage mod whose mod.ini was read (label, version), in folder order.
std::vector<std::wstring>skippedFolders;
struct SeenMod{std::wstring label,version;};
std::vector<SeenMod>seenMods;
std::vector<LegacyCache>legacyCaches;
std::set<std::wstring>usedMemberCache;   // 1.3.0: the member cache entries this start uses

const ChaoGarageLoaderInfoV1 kInfo{sizeof(ChaoGarageLoaderInfoV1),CHAOGARAGE_LOADER_INFO_V1,CHAOGARAGE_RELEASE_W,SSASR_BOOTSTRAP_ABI_V2,
    SSASR_HOST_ABI_V1,1,1,kCapabilities,CHAOGARAGE_RELEASES_URL_W};

void __cdecl log(const wchar_t*message){if(logPath.empty())return;HANDLE h=CreateFileW(logPath.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return;SYSTEMTIME t;GetSystemTime(&t);wchar_t prefix[64];swprintf_s(prefix,L"%04u-%02u-%02u %02u:%02u:%02u UTC  ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);std::wstring w=prefix;w+=message;w+=L"\r\n";int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),s.data(),n,nullptr,nullptr);DWORD k;WriteFile(h,s.data(),n,&k,nullptr);CloseHandle(h);}
// 1.3.2: how long each step of the start takes, in milliseconds since the loader was entered (one log line per step).
LARGE_INTEGER timingStart{};
void timingBegin(){QueryPerformanceCounter(&timingStart);}
// Milliseconds since this process was created (the game's start, before the loader got control), or -1.
double processAgeMs(){FILETIME created,exited,kernel,user,now;if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))return -1;GetSystemTimeAsFileTime(&now);
    auto t=[](const FILETIME&f){return (uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;};return t(now)>=t(created)?double(t(now)-t(created))/10000.0:-1;}
void timing(const std::wstring&step){LARGE_INTEGER now,freq;if(!QueryPerformanceCounter(&now)||!QueryPerformanceFrequency(&freq)||freq.QuadPart<=0)return;
    if(!timingStart.QuadPart)timingStart=now;static LARGE_INTEGER last=timingStart;if(last.QuadPart>now.QuadPart)last=now;
    double total=double(now.QuadPart-timingStart.QuadPart)*1000.0/double(freq.QuadPart),delta=double(now.QuadPart-last.QuadPart)*1000.0/double(freq.QuadPart);last=now;
    wchar_t text[96];swprintf_s(text,L": +%.1f ms (%.1f ms since the loader was entered)",delta,total);log((L"Start-up timing: "+step+text).c_str());}
std::wstring ansi(const char*s){if(!s)return {};int n=MultiByteToWideChar(CP_ACP,0,s,-1,nullptr,0);if(!n)return {};std::wstring w(n,0);MultiByteToWideChar(CP_ACP,0,s,-1,w.data(),n);w.resize(n-1);return w;}
const Redirect*lookup(const char*path){struct PreserveError{DWORD saved=GetLastError();~PreserveError(){SetLastError(saved);}}error;if(!path||!*path)return nullptr;try{auto p=fold(full(ansi(path)));auto i=redirects.find(p);return i==redirects.end()?nullptr:&i->second;}catch(...){return nullptr;}}
HANDLE WINAPI openFile(LPCSTR p,DWORD access,DWORD sharing,LPSECURITY_ATTRIBUTES sa,DWORD creation,DWORD flags,HANDLE templ){
    if(auto r=lookup(p)){if(r->archive)return virtual_io::open(r->archive,r->logical,access,sharing,sa,creation,flags,templ);if(!r->writable&&(creation!=OPEN_EXISTING||(access&(GENERIC_WRITE|GENERIC_ALL|DELETE|WRITE_DAC|WRITE_OWNER|FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES)))){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}return CreateFileW(r->physical.c_str(),access,sharing,sa,creation,flags,templ);}return CreateFileA(p,access,sharing,sa,creation,flags,templ);}
DWORD WINAPI attributes(LPCSTR p){if(auto r=lookup(p))return GetFileAttributesW(r->physical.c_str());return GetFileAttributesA(p);}
BOOL WINAPI deleteFile(LPCSTR p){if(auto r=lookup(p)){if(!r->writable){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return DeleteFileW(r->physical.c_str());}return DeleteFileA(p);}
BOOL WINAPI moveFile(LPCSTR a,LPCSTR b){auto ar=lookup(a),br=lookup(b);if(ar||br){if(!ar||!br||!ar->writable||!br->writable){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return MoveFileW(ar->physical.c_str(),br->physical.c_str());}return MoveFileA(a,b);}
bool change(Hook&h, bool installing){auto slot=(void*volatile*)h.address;bool exchanged=true;DWORD old=0;
    if(installing||h.applied){if(!VirtualProtect((void*)h.address,sizeof(void*),PAGE_READWRITE,&old))return false;if(installing)h.originalProtection=old;h.protectionDirty=true;
        void*was=InterlockedCompareExchangePointer(slot,installing?h.after:h.before,installing?h.before:h.after);exchanged=was==(installing?h.before:h.after);if(exchanged)h.applied=installing;}
    if(h.protectionDirty){DWORD ignored=0;if(!VirtualProtect((void*)h.address,sizeof(void*),h.originalProtection,&ignored))return false;h.protectionDirty=false;}return exchanged;}
// Undo in reverse order. `failed` names what could not be undone, for the warning.
bool undo(std::wstring& failed){bool ok=true;for(auto i=mods.rbegin();i!=mods.rend();++i)if(i->started){if(FAILED(i->rollback())){ok=false;if(failed.empty())failed=i->label;}else i->started=false;}for(auto i=hooks.rbegin();i!=hooks.rend();++i)if((i->applied||i->protectionDirty)&&!change(*i,false)){ok=false;if(failed.empty())failed=L"The loader";}return ok;}
std::wstring ini(const std::wstring&p,const wchar_t*k){wchar_t s[2048]{};DWORD n=GetPrivateProfileStringW(L"mod",k,L"",s,2048,p.c_str());require(n&&n<2047,"Missing or oversized mod.ini field");return s;}
// 1.3.2: mod.ini as text, for the other-loader test only. Windows' own ini reader misses a section header on the first line of a
// file that starts with a UTF-8 byte order mark, which editors and tools add. Reads UTF-16 LE with its mark, UTF-8 with or without
// its mark, else the ANSI code page; any line ends (LF, CRLF, CR); at most 1 MB. Empty on any doubt.
std::wstring iniText(const std::wstring&path){
    Handle f(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));if(f.h==INVALID_HANDLE_VALUE)return {};
    LARGE_INTEGER n{};if(!GetFileSizeEx(f.h,&n)||n.QuadPart<=0||n.QuadPart>(1<<20))return {};
    std::string b((size_t)n.QuadPart,'\0');DWORD got=0;if(!ReadFile(f.h,b.data(),(DWORD)b.size(),&got,nullptr)||got!=b.size())return {};
    if(b.size()>=2&&(BYTE)b[0]==0xFF&&(BYTE)b[1]==0xFE){std::wstring w((b.size()-2)/2,L'\0');if(!w.empty())memcpy(w.data(),b.data()+2,w.size()*sizeof(wchar_t));return w;}
    const size_t at=b.size()>=3&&(BYTE)b[0]==0xEF&&(BYTE)b[1]==0xBB&&(BYTE)b[2]==0xBF?3:0;if(at==b.size())return {};
    for(UINT cp:{UINT(CP_UTF8),UINT(CP_ACP)}){const DWORD flags=cp==CP_UTF8?MB_ERR_INVALID_CHARS:0;int k=MultiByteToWideChar(cp,flags,b.data()+at,(int)(b.size()-at),nullptr,0);
        if(k>0){std::wstring w((size_t)k,L'\0');if(MultiByteToWideChar(cp,flags,b.data()+at,(int)(b.size()-at),w.data(),k)==k)return w;}}
    return {};}
// Spaces, tabs and every other control character (Windows' ini reader skips them too) off both ends.
std::wstring trimmedText(const std::wstring&s){size_t a=0,e=s.size();while(a<e&&s[a]<=L' ')++a;while(e>a&&s[e-1]<=L' ')--e;return s.substr(a,e-a);}
// 1.3.2: a mod.ini with no [mod] section but a [Main] section with an ID= is another mod loader's format (for example the ASR Mod
// Manager's): that folder is skipped with one log line instead of refusing every mod. Read as text (iniText), the way Windows reads
// an ini: [section] lines, key=value lines, names in any case, the first [Main] section and the first of a repeated key, one pair of
// enclosing quotes removed from a value; CR, LF and NUL end a line. Fail-closed both ways: Windows' own reader is asked whether a
// [mod] section has entries, and any line this reader takes for a [mod] header (also "[mod" with no "]", or "[mod" followed by a
// space or a comment) counts as one. Any doubt (an unreadable file, an empty or very long ID, a [mod] section) returns false, and
// the file is judged as before. Gives the ID, name and version for the log.
struct OtherLoaderMod{std::wstring id,name,version;};
bool anotherLoadersMod(const std::wstring&config,OtherLoaderMod&out){
    {wchar_t probe[8]{};if(GetPrivateProfileSectionW(L"mod",probe,8,config.c_str())>0)return false;}
    const auto text=iniText(config);if(text.empty())return false;
    bool hasMod=false,hasMain=false,inMain=false;std::map<std::wstring,std::wstring>main;
    auto modHeader=[](const std::wstring&name){if(name.size()<3||_wcsnicmp(name.c_str(),L"mod",3)!=0)return false;if(name.size()==3)return true;const wchar_t c=name[3];return !(iswalnum(c)||c==L'-'||c==L'_'||c==L'.');};
    for(size_t at=0;at<=text.size();){size_t e=text.find_first_of(std::wstring(L"\r\n\0",3),at);if(e==std::wstring::npos)e=text.size();const auto line=trimmedText(text.substr(at,e-at));at=e+1;
        if(line.empty()||line[0]==L';')continue;
        if(line[0]==L'['){const auto close=line.find(L']');inMain=false;const auto section=trimmedText(line.substr(1,close==std::wstring::npos?std::wstring::npos:close-1));
            if(modHeader(section))hasMod=true;else if(close!=std::wstring::npos&&_wcsicmp(section.c_str(),L"Main")==0&&!hasMain)hasMain=inMain=true;continue;}
        if(!inMain)continue;const auto eq=line.find(L'=');if(eq==std::wstring::npos)continue;
        auto value=trimmedText(line.substr(eq+1));if(value.size()>=2&&value.front()==L'"'&&value.back()==L'"')value=value.substr(1,value.size()-2);
        main.emplace(fold(trimmedText(line.substr(0,eq))),value);}
    if(hasMod||!hasMain)return false;
    auto field=[&](const wchar_t*k){auto i=main.find(k);return i==main.end()?std::wstring():i->second;};
    out.id=field(L"id");if(out.id.empty()||out.id.size()>=255)return false;
    out.name=field(L"name");out.version=field(L"version");return true;}
// 1.3.2: for the summary: a direct subfolder of `dir` that holds a mod.ini (a mod unpacked one folder too deep), or empty.
std::wstring innerModFolder(const std::wstring&dir){WIN32_FIND_DATAW d;HANDLE f=FindFirstFileW((dir+L"\\*").c_str(),&d);if(f==INVALID_HANDLE_VALUE)return {};std::wstring found;int seen=0;
    do{std::wstring name=d.cFileName;if(name==L"."||name==L".."||!(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))continue;
        auto a=GetFileAttributesW((dir+L"\\"+name+L"\\mod.ini").c_str());if(a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY)){found=name;break;}}while(++seen<256&&FindNextFileW(f,&d));
    FindClose(f);return found;}
std::wstring optionalIni(const std::wstring&p,const wchar_t*k){wchar_t s[512]{};DWORD n=GetPrivateProfileStringW(L"mod",k,L"",s,512,p.c_str());return n<511?std::wstring(s,n):std::wstring();}
std::string narrowAscii(const std::wstring&s){std::string n;for(auto c:s){require(c>0&&c<128,"Expected ASCII manifest field");n+=(char)c;}return n;}
std::string utf8(const std::wstring&w){if(w.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);std::string s(n>0?n:0,0);if(n>0)WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),s.data(),n,nullptr,nullptr);return s;}
std::wstring leaf(const std::wstring&dir){auto p=dir.find_last_of(L'\\');return p==std::wstring::npos?dir:dir.substr(p+1);}
std::wstring hex32(uint32_t v){wchar_t b[16];swprintf_s(b,L"0x%08lX",(unsigned long)v);return b;}

// A failure that knows which mod it concerns and what kind it is, so the one warning can say what to do.
struct Failure:std::runtime_error{msg::Entry entry;Failure(msg::Entry e,const char*technical):std::runtime_error(technical),entry(std::move(e)){}};
// Requirements checked before anything was prepared, loaded or published; every problem of every mod at once.
struct Refused:std::exception{std::vector<msg::Entry>entries;};

// 1.3.2: one plain block at the end of every start that read the mods folder, so a player (or a helper reading the log) sees at a
// glance what runs: the mods started (name and version), the folders skipped on purpose, and the problems that stopped the mods,
// each with its reason and what to do, in the words of the message on screen. Logged only; never a reason to stop.
std::wstring counted(size_t n,const wchar_t*word){return std::to_wstring(n)+L" "+word+(n==1?L"":L"s");}
std::wstring withVersion(const std::wstring&label,const std::wstring&version){return label+(version.empty()?std::wstring(L" (no version given)"):L", version "+msg::clean(version,32));}
void summary(const std::vector<msg::Entry>&problems,bool stopped){try{
    std::vector<std::wstring>lines;
    if(problems.empty()){
        if(mods.empty())lines.push_back(L"Summary: no ChaoGarage mods to start"+(skippedFolders.empty()?std::wstring(L"."):L"; "+counted(skippedFolders.size(),L"folder")+L" skipped."));
        else{lines.push_back(L"Summary: "+counted(mods.size(),L"mod")+L" started"+(skippedFolders.empty()?std::wstring():L", "+counted(skippedFolders.size(),L"folder")+L" skipped")+L". Start order:");
            for(auto&m:mods)lines.push_back(L"  Started: "+withVersion(m.label,m.version)+L".");}
    }else{
        lines.push_back(stopped?L"Summary: the game start was stopped to protect your files; no mods are running.":L"Summary: no mods were started (the game runs without mods) because of "+counted(problems.size(),L"problem")+L":");
        // "Refused": a mod turned down by the checks before anything started; "Failed": a mod whose code could not load, start or undo
        // its changes; "Problem": something that is not one mod (the loader files, another tool, a folder link).
        auto kind=[](const msg::Entry&e)->const wchar_t*{if(e.who.rfind(L"Mod \"",0)!=0)return L"  Problem: ";
            return e.cat==Cat::RuntimeLoad||e.cat==Cat::RuntimeInit||e.cat==Cat::RollbackFailed?L"  Failed: ":L"  Refused: ";};
        for(auto&e:problems)lines.push_back(kind(e)+e.who+L" "+msg::headline(e.cat)+(e.detail.empty()?std::wstring():L" "+e.detail)+L" What to do: "+msg::action(e));
        if(!stopped)for(auto&s:seenMods)if(std::none_of(problems.begin(),problems.end(),[&](const msg::Entry&e){return e.who==s.label;}))
            lines.push_back(L"  Not running (only because of the "+std::wstring(problems.size()==1?L"problem":L"problems")+L" above): "+withVersion(s.label,s.version)+L".");
    }
    for(auto&s:skippedFolders)lines.push_back(L"  "+s);
    for(auto&l:lines)log(msg::clean(l,2000).c_str());
}catch(...){}}
// The one warning: logged entry by entry, then the summary block (before the box, which waits for the player), then shown.
void warn(const std::vector<msg::Entry>&entries,bool stopped){
    for(auto&e:entries)log(msg::clean(e.who+L" "+msg::headline(e.cat)+L" "+e.detail,2000).c_str());
    summary(entries,stopped);
    MessageBoxW(nullptr,msg::compose(entries,stopped).c_str(),msg::title().c_str(),MB_OK|MB_ICONWARNING|MB_SETFOREGROUND);
}
std::wstring technical(const char*what){return L"Technical detail: "+msg::wide(what)+L".";}
// The reader's findings are sentence fragments ("it needs ..."); on their own line they read as a sentence.
std::wstring sentence(const std::string&fragment){auto w=msg::wide(fragment);if(!w.empty()&&w[0]>=L'a'&&w[0]<=L'z')w[0]=(wchar_t)(w[0]-L'a'+L'A');if(!w.empty()&&w.back()!=L'.')w+=L'.';return w;}
Cat classify(const std::string&what){
    for(auto c:{"Two mods replace the same resource","Mods require different original archives","Two mods change the same archive member"})
        if(what.rfind(c,0)==0)return Cat::Conflict;
    return Cat::Files;
}
std::wstring loadError(DWORD e){
    std::wstring s=L"Windows error "+std::to_wstring(e);
    switch(e){case ERROR_BAD_EXE_FORMAT:return s+L": it is not a 32-bit Windows DLL.";case ERROR_MOD_NOT_FOUND:return s+L": the DLL or a file it needs is missing.";
        case ERROR_FILE_NOT_FOUND:case ERROR_PATH_NOT_FOUND:return s+L": the file was not found.";case ERROR_ACCESS_DENIED:return s+L": access was denied.";
        case ERROR_DLL_INIT_FAILED:return s+L": the DLL's own start-up failed.";}
    return s+L".";
}

std::string moduleSha(HMODULE m){wchar_t b[32768];DWORD n=GetModuleFileNameW(m,b,32768);if(!n||n>=32768)return {};try{return hash_file(std::wstring(b,n));}catch(...){return {};}}
std::string selfSha(){HMODULE self=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)&kInfo,&self))return {};return moduleSha(self);}

std::wstring modsRoot(){return child(gameRoot,L"mods");}
// The mods folder and the log next to it, when the folder exists. A link there is refused, as before.
bool openMods(){auto root=modsRoot();DWORD a=GetFileAttributesW(root.c_str());if(a==INVALID_FILE_ATTRIBUTES)return false;
    if(!(a&FILE_ATTRIBUTE_DIRECTORY)||(a&FILE_ATTRIBUTE_REPARSE_POINT))throw Failure({Cat::Environment,L"The mods folder",L"It is not a real folder (a file or a link); the loader only reads a real mods folder."},"mods must be a real directory");
    logPath=root+L"\\SsasrMods.log";return true;}
// 1.3.0: updates a mod staged during an earlier session go in now, before any mod is read (pending_updates.h). One line
// each in the log; never a reason to stop the start.
void applyPending(){try{for(auto&r:apply_pending_updates(modsRoot())){std::wstring line=L"Mod update for \""+msg::clean(r.folder,100)+L"\""+(r.version.empty()?L"":L" (version "+msg::clean(r.version,32)+L")");
        line+=r.applied?L": put in place"+(r.detail.empty()?std::wstring():L" ("+r.detail+L")")+L"; the replaced folder is in mods-updates\\previous.":L": not put in place: "+r.detail+L".";log(msg::clean(line,2000).c_str());}}
    catch(...){log(L"Mod updates: skipped after an internal error.");}}
// Folders in start order before metal-sonic is moved first: ordinal by name (the same sort as always).
std::vector<std::wstring> modFolders(){auto root=modsRoot();WIN32_FIND_DATAW data;HANDLE f=FindFirstFileW((root+L"\\*").c_str(),&data);if(f==INVALID_HANDLE_VALUE)return {};std::vector<std::wstring>dirs;
    do{std::wstring name=data.cFileName;if(name==L"."||name==L".."||!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))continue;
        if(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){FindClose(f);throw Failure({Cat::Environment,L"Mod folder \""+name+L"\"",L"It is a link (junction) to another place; the loader only reads real folders."},"Mod junctions are not supported");}
        dirs.push_back(root+L"\\"+name);}while(FindNextFileW(f,&data));FindClose(f);std::sort(dirs.begin(),dirs.end());return dirs;}

struct Candidate{std::wstring dir,folder,label,homepage,version;std::string id;compat::Description desc;int startRank=2;};
// The start order, within each rank by folder name: 0 the mod with ID metal-sonic, 1 a mod carrying its own copy of the Metal Sonic core, 2 the rest.
// That core checks all of the game's code when it starts and refuses if a mod before it has changed any of it.
int startRank(const std::string&id,const std::wstring&dir){if(id=="metal-sonic")return 0;auto a=GetFileAttributesW((dir+L"\\runtime\\MetalSonicMod.dll").c_str());return a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY)?1:2;}

compat::Description readDescription(const std::wstring&dir,const std::string&id){
    compat::Description d;auto path=dir+L"\\allstars-mod.json";DWORD a=GetFileAttributesW(path.c_str());
    switch(compat::presence(a,a==INVALID_FILE_ATTRIBUTES?GetLastError():0,d)){
    case compat::Presence::Absent:return d;     // no description: a legacy mod, nothing to check
    case compat::Presence::Refused:return d;    // a folder, a link, or not inspectable: never "no requirements"
    case compat::Presence::ReadFile:break;}
    // Open the name itself, never a link target, and check again on the handle (the name could change after the first look).
    Handle f(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(f.h==INVALID_HANDLE_VALUE){compat::refuse(d,compat::Kind::DescriptionUnusable,"its description file (allstars-mod.json) could not be opened (Windows error "+std::to_string(GetLastError())+")");return d;}
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(f.h,&info)){compat::refuse(d,compat::Kind::DescriptionUnusable,"its description file (allstars-mod.json) could not be inspected (Windows error "+std::to_string(GetLastError())+")");return d;}
    if(compat::presence(info.dwFileAttributes,0,d)!=compat::Presence::ReadFile)return d;
    LARGE_INTEGER n{};if(!GetFileSizeEx(f.h,&n)||n.QuadPart<0){compat::refuse(d,compat::Kind::DescriptionUnusable,"its description file (allstars-mod.json) could not be read");return d;}
    if((uint64_t)n.QuadPart>compat::kMaxDescriptionBytes){compat::refuse(d,compat::Kind::DescriptionUnusable,"its description file (allstars-mod.json) is larger than 4 MB");return d;}
    std::string bytes((size_t)n.QuadPart,'\0');DWORD got=0;
    if(!bytes.empty()&&(!ReadFile(f.h,bytes.data(),(DWORD)bytes.size(),&got,nullptr)||got!=bytes.size())){compat::refuse(d,compat::Kind::DescriptionUnusable,"its description file (allstars-mod.json) could not be read");return d;}
    return compat::read(bytes,id);
}
Cat category(compat::Kind k){switch(k){case compat::Kind::NeedsNewerLoader:return Cat::NeedsNewerLoader;case compat::Kind::MadeForOtherLoader:return Cat::MadeForOtherLoader;case compat::Kind::LoadOrder:return Cat::LoadOrder;default:return Cat::DescriptionUnusable;}}

// Before anything is prepared, loaded or published: every mod's manifest basics and the requirements it declares
// for the loader, checked together so that one warning can list them all.
std::vector<Candidate> checkMods(const SsasrBootstrapV1*b){
    std::vector<msg::Entry>refused;std::vector<Candidate>list;auto root=modsRoot();
    for(auto&dir:modFolders()){
        auto config=child(dir,L"mod.ini");
        if(GetFileAttributesW(config.c_str())==INVALID_FILE_ATTRIBUTES){   // not a mod; the summary says so (a folder whose name starts with "." is the loader's own or hidden: not listed)
            auto folder=leaf(dir);if(!folder.empty()&&folder[0]!=L'.'){auto inner=innerModFolder(dir);
                skippedFolders.push_back(L"Skipped: folder \""+msg::clean(folder,100)+L"\": it has no mod.ini, so it is not a mod for ChaoGarage."+(inner.empty()?std::wstring():L" Its folder \""+msg::clean(inner,100)+L"\" has one: move that folder itself into the mods folder."));}
            continue;}
        Candidate c;c.dir=dir;c.folder=leaf(dir);c.label=msg::label(c.folder,c.folder);std::wstring name;
        try{
            no_reparse(root,config);
            {OtherLoaderMod other;if(anotherLoadersMod(config,other)){log(msg::clean(c.folder+L": another mod loader's mod (ID="+other.id+L"); not started by ChaoGarage",2000).c_str());
                std::wstring what=other.name.empty()?std::wstring():msg::clean(other.name,48);if(!other.version.empty())what+=(what.empty()?L"version ":L", version ")+msg::clean(other.version,32);
                skippedFolders.push_back(L"Skipped: folder \""+msg::clean(c.folder,100)+L"\""+(what.empty()?std::wstring():L" ("+what+L")")+L": a mod for another mod loader (its ID is "+msg::clean(other.id,100)+L"); ChaoGarage does not start it.");continue;}}
            name=optionalIni(config,L"name");if(!name.empty())c.label=msg::label(name,c.folder);c.version=optionalIni(config,L"version");
            auto schema=ini(config,L"schema");
            if(schema!=L"1"){
                bool digits=!schema.empty()&&schema.size()<10&&std::all_of(schema.begin(),schema.end(),[](wchar_t ch){return ch>=L'0'&&ch<=L'9';});
                if(digits&&std::stoul(schema)>1)throw Failure({Cat::NeedsNewerLoader,c.label,L"Its mod.ini uses format version "+schema+L", and this loader release reads version 1."},"Unsupported mod.ini version");
                throw Failure({Cat::Files,c.label,L"Its mod.ini does not state format version 1."},"Unsupported mod.ini version");
            }
            c.id=utf8(ini(config,L"id"));c.startRank=startRank(c.id,c.dir);
            c.desc=readDescription(dir,c.id);
        }catch(const Failure&f){refused.push_back(f.entry);continue;}
        catch(const std::exception&e){refused.push_back({Cat::Files,c.label,technical(e.what())});continue;}
        if(name.empty()&&!c.desc.name.empty())c.label=msg::label(msg::wide(c.desc.name),c.folder);
        c.homepage=msg::wide(c.desc.homepage);
        for(auto&n:c.desc.notes)log(msg::clean(c.label+L": "+msg::wide(n),2000).c_str());
        seenMods.push_back({c.label,c.version});
        list.push_back(std::move(c));
    }
    compat::Self self{SSASR_HOST_ABI_V1,1,1,kCapabilities,CHAOGARAGE_RELEASE_A,kRetiredCapabilities};
    std::string hostSha,proxySha;bool hostRead=false,proxyRead=false;
    auto host=[&]{if(!hostRead){hostSha=selfSha();hostRead=true;}return hostSha;};
    auto proxy=[&]{if(!proxyRead){proxySha=b->bootstrap_image?moduleSha(b->bootstrap_image):std::string();proxyRead=true;}return proxySha;};
    for(auto&c:list)compat::checkLoader(c.desc,self,host,proxy);
    std::vector<size_t>at(list.size());std::iota(at.begin(),at.end(),size_t(0));
    std::stable_sort(at.begin(),at.end(),[&](size_t x,size_t y){return list[x].startRank<list[y].startRank;});
    std::vector<compat::Started>order;for(auto i:at)order.push_back({list[i].id,utf8(list[i].folder),&list[i].desc,list[i].startRank==1});
    auto orderProblems=compat::checkOrder(order);
    std::string exe;if(b->executable_sha256){const char*x="0123456789abcdef";for(int i=0;i<32;++i){exe+=x[b->executable_sha256[i]>>4];exe+=x[b->executable_sha256[i]&15];}}
    for(auto&c:list){
        if(c.desc.applies&&!c.desc.targetExe.empty()&&std::find(c.desc.targetExe.begin(),c.desc.targetExe.end(),exe)==c.desc.targetExe.end())
            log(msg::clean(c.label+L": it lists other game builds than this one in \"target\" (descriptive only; never a reason to refuse it).",2000).c_str());
        for(auto&p:c.desc.problems)refused.push_back({category(p.kind),c.label,sentence(p.detail),c.homepage});
    }
    for(auto&[k,p]:orderProblems){auto&c=list[at[k]];refused.push_back({Cat::LoadOrder,c.label,sentence(p.detail),c.homepage});}
    for(size_t i=0;i<list.size();++i)for(size_t j=i+1;j<list.size();++j)if(list[i].id==list[j].id)
        refused.push_back({Cat::Conflict,list[j].label,L"It has the same mod ID ("+msg::wide(compat::quote(list[j].id))+L") as "+list[i].label+L"; each mod needs its own ID."});
    if(!refused.empty()){Refused r;r.entries=std::move(refused);throw r;}
    std::vector<Candidate>sorted;for(auto i:at)sorted.push_back(list[i]);
    return sorted;
}

// Unchanged discovery, now over the checked folders. Runtimes keep the start order: see startRank.
void discover(const std::vector<Candidate>&list){auto root=modsRoot();std::set<std::wstring>ids;
    std::vector<const Candidate*>byFolder;for(auto&c:list)byFolder.push_back(&c);
    std::sort(byFolder.begin(),byFolder.end(),[](const Candidate*x,const Candidate*y){return x->dir<y->dir;});
    for(auto*c:byFolder){auto&dir=c->dir;try{auto config=child(dir,L"mod.ini");no_reparse(root,config);require(ini(config,L"schema")==L"1","Unsupported mod.ini version");auto id=ini(config,L"id");require(ids.insert(id).second,"Duplicate mod ID");auto plan=child(dir,ini(config,L"plan"));no_reparse(root,plan);auto planSha=narrowAscii(ini(config,L"plan_sha256"));auto dataPlan=JsonParser(verified_text(plan,planSha)).parse();   // 1.3.3: one read; the hashed bytes are the parsed bytes
            if(dataPlan.has("schema")&&dataPlan.at("schema").kind==Json::Number&&dataPlan.at("schema").n>1)throw Failure({Cat::NeedsNewerLoader,c->label,L"Its plan.json uses format version "+std::to_wstring(dataPlan.at("schema").n)+L", and this loader release reads version 1."},"Unsupported plan version");
            auto paths=prepare(gameRoot,dir,dataPlan,planSha);timing(L"prepare of mod folder \""+msg::clean(c->folder,100)+L"\" done");for(auto&r:paths)if(r.archive){for(auto&change:r.archive->changes)change.second.owner=c->id;for(auto&[key,patchList]:r.archive->patches)for(auto&patch:patchList)patch.owner=c->id;}for(auto&r:paths)for(auto&seed:r.seeds)seed.owner=c->id;for(auto&g:dataPlan.at("archive_groups").list())if(g.at("format").str()=="xpac")legacyCaches.push_back({dir,g.at("expected_composed_sha256").str(),g.at("expected_composed_size").u32()});for(auto&r:paths){auto found=redirects.find(fold(r.logical));if(found==redirects.end())redirects.emplace(fold(r.logical),r);else if(!found->second.seeds.empty()&&!r.seeds.empty()){require(found->second.keyBytes==r.keyBytes,"Two mods replace the same resource; their shader records have different key sizes");found->second.seeds.insert(found->second.seeds.end(),r.seeds.begin(),r.seeds.end());}else {require(found->second.archive&&r.archive,"Two mods replace the same resource; combination needs a merge plan");found->second.archive=merge_virtual_xpac(found->second.archive,r.archive);}}auto runtime=child(dir,ini(config,L"runtime"));no_reparse(root,runtime);hash_matches(runtime,narrowAscii(ini(config,L"runtime_sha256")));Mod m;m.dir=dir;m.runtime=runtime;m.id=id;m.label=c->label;m.version=c->version;m.startRank=c->startRank;mods.push_back(m);}
        catch(const Failure&){throw;}
        catch(const std::exception&e){auto cat=classify(e.what());throw Failure({cat,c->label,cat==Cat::Conflict?L"Another mod changes the same game file in a way that cannot be combined. "+technical(e.what()):technical(e.what()),c->homepage},e.what());}}
    // 1.3.0: patched members, put together after every whole-member change of every mod was combined.
    auto cacheRoot=child(root,L".chaogarage-cache");std::string build;std::map<std::string,const Candidate*>byId;for(auto*c:byFolder)byId[c->id]=c;
    for(auto&item:redirects)if(auto&r=item.second;r.archive&&!r.archive->patches.empty()){
        if(build.empty()){build=archive_cache::state().build;require(!build.empty(),"Cannot read the loader's own file");}
        auto file=r.logical.size()>gameRoot.size()+1?r.logical.substr(gameRoot.size()+1):r.logical;
        auto who=[&](const std::string&id)->const Candidate*{auto i=byId.find(id);return i==byId.end()?nullptr:i->second;};
        try{compose_patched_members(*r.archive,root,cacheRoot,build,[&](const std::string&line){log(msg::clean(file+L", "+msg::wide(line),2000).c_str());},usedMemberCache);}
        catch(const MemberConflict&e){auto*c=who(e.later);throw Failure({Cat::Conflict,c?c->label:std::wstring(L"A mod"),L"Another mod changes the same entry of a game file in a way that cannot be combined. "+technical(e.what()),c?c->homepage:std::wstring()},e.what());}
        catch(const MemberPatchMismatch&e){auto*c=who(e.owner);throw Failure({Cat::Files,c?c->label:std::wstring(L"A mod"),technical(e.what()),c?c->homepage:std::wstring()},e.what());}
        catch(const std::exception&e){throw Failure({Cat::Environment,L"The loader",L"It could not put together "+file+L", which several mods change. "+technical(e.what())},e.what());}
    }
    timing(L"patched members composed");
    // 1.3.0: a shader cache several mods seed.
    for(auto&item:redirects)if(auto&r=item.second;r.seeds.size()>1){
        if(build.empty()){build=archive_cache::state().build;require(!build.empty(),"Cannot read the loader's own file");}
        auto file=r.logical.size()>gameRoot.size()+1?r.logical.substr(gameRoot.size()+1):r.logical;std::wstring owners;for(auto&seed:r.seeds)owners+=(owners.empty()?L"":L", ")+msg::wide(seed.owner);
        try{r.physical=prepare_shared_shaders(gameRoot,root,cacheRoot,r.logical,r.seeds,r.keyBytes,build,usedMemberCache);log(msg::clean(file+L": shader records of "+owners+L" merged",2000).c_str());}
        catch(const ShaderConflict&e){auto i=byId.find(e.later);auto*c=i==byId.end()?nullptr:i->second;throw Failure({Cat::Conflict,c?c->label:std::wstring(L"A mod"),L"Another mod changes the same entry of a game file in a way that cannot be combined. "+technical(e.what()),c?c->homepage:std::wstring()},e.what());}
        catch(const Failure&){throw;}
        catch(const std::exception&e){throw Failure({Cat::Environment,L"The loader",L"It could not put together "+file+L", which several mods change. "+technical(e.what())},e.what());}
    }
    timing(L"shared shader caches merged");
    std::stable_sort(mods.begin(),mods.end(),[](const Mod&a,const Mod&b){return a.startRank<b.startRank;});
    // Members two mods change that were combined (a variant, or an optional change that gave way): one log line each.
    for(auto&item:redirects)if(auto&r=item.second;r.archive)for(auto&note:r.archive->notes){auto file=r.logical.size()>gameRoot.size()+1?r.logical.substr(gameRoot.size()+1):r.logical;log(msg::clean(file+L", "+msg::wide(note),2000).c_str());}
}
void installRedirects(HMODULE game){HMODULE kernel=GetModuleHandleW(L"kernel32.dll");require(kernel!=nullptr,"Kernel module missing");struct Target{uintptr_t va;const char*name;void*replacement;};Target targets[]={{0x67D04C,"CreateFileA",(void*)&openFile},{0x67D0E0,"GetFileAttributesA",(void*)&attributes},{0x67D0E4,"DeleteFileA",(void*)&deleteFile},{0x67D0E8,"MoveFileA",(void*)&moveFile},
        {0x67D060,"ReadFile",(void*)&virtual_io::readFile},{0x67D198,"ReadFileEx",(void*)&virtual_io::readFileEx},
        {0x67D070,"SetFilePointer",(void*)&virtual_io::seekFile},{0x67D064,"GetFileSize",(void*)&virtual_io::getSize},
        {0x67D09C,"GetFileSizeEx",(void*)&virtual_io::getSizeEx},{0x67D03C,"CloseHandle",(void*)&virtual_io::close}};
    for(auto&t:targets){auto expected=(void*)GetProcAddress(kernel,t.name);require(expected!=nullptr,"System function unavailable");uintptr_t address=(uintptr_t)game+t.va-0x400000;require(*(void**)address==expected,"File API already hooked; refusing conflicting loader");hooks.push_back({address,expected,t.replacement,false});}
    for(auto&h:hooks)require(change(h,true),"Resource redirection publication failed");
}
msg::Entry pairEntry(const std::wstring&proxyRelease){
    return {Cat::PairMismatch,L"Loader files:",L"ChaoGarage.dll is release " CHAOGARAGE_RELEASE_W L"; dinput8.dll is "+(proxyRelease.empty()?std::wstring(L"from an earlier release (before 1.1.0)"):L"release "+msg::clean(proxyRelease,32))+L"."};
}
}
using namespace chaogarage_host;
extern "C" const ChaoGarageLoaderInfoV1* __cdecl ChaoGarageGetLoaderInfo(void){return &kInfo;}
extern "C" HRESULT __cdecl SsasrModsHostInitialize(const SsasrBootstrapV1*b){
    if(!b)return E_INVALIDARG;
    const bool v2=b->version==SSASR_BOOTSTRAP_ABI_V2&&b->size==sizeof(SsasrBootstrapV2),v1=b->version==SSASR_BOOTSTRAP_ABI_V1&&b->size==sizeof(SsasrBootstrapV1);
    if(!(v1||v2)||!b->initialization_is_active||!b->initialization_is_active()||b->initializing_thread_id!=GetCurrentThreadId()||b->native_gate_rva!=0x248F45||b->flags!=7)return E_INVALIDARG;
    if(InterlockedCompareExchange(&hostEntered,1,0))return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    timingBegin();const double ageAtEntry=processAgeMs();
    bool publishing=false;
    try{require(mods.empty()&&hooks.empty(),"Host initialization was invoked twice");require((uintptr_t)b->game_image==0x400000,"Unsupported game image base");gameRoot=full(b->game_directory);
        // The two loader files must come from the same release. A 1.0 dinput8.dll calls with version 1; a 1.1 one names its release.
        std::wstring proxyRelease;
        if(v2){auto r=reinterpret_cast<const SsasrBootstrapV2*>(b)->bootstrap_release;if(r){size_t n=wcsnlen(r,33);if(n<=32)proxyRelease.assign(r,n);}}
        if(v1||proxyRelease!=CHAOGARAGE_RELEASE_W){openMods();if(v2&&proxyRelease.empty())proxyRelease=L"unknown";warn({pairEntry(proxyRelease)},false);return NotStartedWarned;}
        if(!openMods())return S_FALSE;
        {wchar_t age[96]=L"";if(ageAtEntry>=0)swprintf_s(age,L" (the game process is %.0f ms old)",ageAtEntry);timing(L"host init entry"+std::wstring(age));}
        archive_cache::set_directory(child(modsRoot(),L".chaogarage-cache"));   // 1.3.2: verified original archives are remembered here (archive_cache.h)
        archive_cache::set_build(selfSha());   // 1.3.3: read once, before any mod file; names this build in the layout records and the member caches
        applyPending();timing(L"mods folder opened, pending updates applied");
        auto checked=checkMods(b);timing(L"mod folders read and checked (checkMods)");if(checked.empty()){summary({},false);return S_FALSE;}
        discover(checked);archive_cache::flush();if(mods.empty()){summary({},false);return S_FALSE;}
        // Load/resolve all components before any hook publication. Failure here
        // cannot expose Metal assets to a vanilla runtime.
        for(auto&m:mods){m.module=LoadLibraryExW(m.runtime.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!m.module)throw Failure({Cat::RuntimeLoad,m.label,loadError(GetLastError())},"Mod runtime failed to load");m.initialize=(SsasrModInitializeFn)GetProcAddress(m.module,"SsasrModInitialize");m.rollback=(SsasrModRollbackFn)GetProcAddress(m.module,"SsasrModRollback");if(!m.initialize||!m.rollback)throw Failure({Cat::RuntimeLoad,m.label,L"Its DLL does not export SsasrModInitialize and SsasrModRollback, so it is not a ChaoGarage mod runtime."},"Mod runtime ABI is incomplete");}
        if(!(b->initialization_is_active()&&GetCurrentThreadId()==b->initializing_thread_id))throw Failure({Cat::Environment,L"The loader",L"The game's start-up moved on before the mods were ready."},"Early initialization gate expired");
        publishing=true;
        try{installRedirects(b->game_image);}catch(const std::exception&e){throw Failure({Cat::Environment,L"The loader",L"Another program has already changed the game's file functions (for example another mod loader). "+technical(e.what())},e.what());}
        SsasrHostApiV1 api{sizeof(api),1,b->game_image,gameRoot.c_str(),SSASR_HOST_NATIVE_CRT_GATE,b->initializing_thread_id,b->native_gate_rva,b->initialization_is_active,&log};
        for(auto&m:mods){m.started=true;HRESULT hr=m.initialize(&api,m.dir.c_str());if(FAILED(hr)){wchar_t detail[128];swprintf_s(detail,L"Runtime initialization returned HRESULT 0x%08lX",(unsigned long)hr);log(detail);}
            if(hr==Unrecoverable){log(L"FATAL: a mod runtime reported unrecoverable partial changes; game startup must stop.");warn({{Cat::RollbackFailed,m.label,L"Its start-up code reported that it could not undo its own changes (0x8004A001)."}},true);return Unrecoverable;}
            if(FAILED(hr))throw Failure({Cat::RuntimeInit,m.label,L"Its start-up code returned error "+hex32((uint32_t)hr)+L"."},"Mod initialization rejected this session");}
        for(auto&c:legacyCaches){auto result=remove_legacy_cache(c);if(result==CacheCleanup::Removed)log((L"Removed obsolete generated archive cache: "+wide(c.hash)+L" ("+std::to_wstring(c.bytes)+L" bytes)").c_str());else if(result==CacheCleanup::Kept)log((L"Retained legacy cache because it is changed, locked, or inaccessible: "+wide(c.hash)).c_str());}
        // At every successful start (also when no mod uses patches any more), as long as the cache folder is a real folder.
        try{auto root=modsRoot();auto cache=child(root,L".chaogarage-cache");DWORD attr=GetFileAttributesW(cache.c_str());
            if(attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_DIRECTORY)&&!(attr&FILE_ATTRIBUTE_REPARSE_POINT))
                if(auto n=prune_member_cache(root,cache,usedMemberCache))log((L"Removed "+std::to_wstring(n)+L" unused cache entries (patched members, shared shader caches) not written for a week.").c_str());}catch(...){}
        {auto&st=archive_cache::state().stats;wchar_t line[200];swprintf_s(line,L"Original archives: %u hashed in full (%.0f MB), %u reused from earlier in this start, %u reused from the record of an earlier start.",st.hashed,double(st.hashedBytes)/1048576.0,st.fromMemory,st.fromRecord);log(line);
            swprintf_s(line,L"Archive payload files: %u hashed in full (%.0f MB), %u reused from earlier in this start, %u reused from the record of an earlier start.",st.filesHashed,double(st.filesHashedBytes)/1048576.0,st.filesFromMemory,st.filesFromRecord);log(line);
            swprintf_s(line,L"Combined archives: %u hashed in full (%.0f MB), %u reused from the record of an earlier start.",st.composedHashed,double(st.composedHashedBytes)/1048576.0,st.composedFromRecord);log(line);}
        timing(L"READY");
        log(L"READY: ChaoGarage " CHAOGARAGE_RELEASE_W L": mod resources and runtime initialized before game startup. Virtual archives enabled; no full XPAC disk copies.");summary({},false);return S_OK;
    }catch(const Refused&r){redirects.clear();warn(r.entries,false);return NotStartedWarned;}
    catch(const std::exception&e){
        msg::Entry entry=dynamic_cast<const Failure*>(&e)?static_cast<const Failure&>(e).entry:msg::Entry{Cat::Environment,L"The loader",L"An unexpected error occurred. "+technical(e.what())};
        std::wstring failed;
        if(publishing&&!undo(failed)){log(L"FATAL: rollback failed; game startup must stop.");warn({entry,{Cat::RollbackFailed,failed,L"After the problem above, its changes could not be undone."}},true);return Unrecoverable;}
        redirects.clear();log(msg::wide(e.what()).c_str());warn({entry},false);return NotStartedWarned;}
    catch(...){std::wstring failed;if(publishing&&!undo(failed)){warn({{Cat::RollbackFailed,failed.empty()?L"The loader":failed,L"An unexpected error occurred and the changes could not be undone."}},true);return Unrecoverable;}
        redirects.clear();warn({{Cat::Environment,L"The loader",L"An unexpected error occurred."}},false);return NotStartedWarned;}
}
extern "C" __declspec(dllexport) const ssasr::virtual_io::Counters* ChaoGarageVirtualCounters=&ssasr::virtual_io::counters;
