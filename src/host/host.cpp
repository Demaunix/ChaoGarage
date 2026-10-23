#include "mod_api.h"
#include "loader_info.h"
#include "chaogarage_release.h"
#include "resources.h"
#include "virtual_io.h"
#include "legacy_cache.h"
#include "compat.h"
#include "messages.h"
#include "capabilities.h"
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
struct Mod{std::wstring dir,runtime;HMODULE module=nullptr;SsasrModInitializeFn initialize=nullptr;SsasrModRollbackFn rollback=nullptr;bool started=false;std::wstring id,label;};
std::vector<Mod>mods;
std::vector<LegacyCache>legacyCaches;

const ChaoGarageLoaderInfoV1 kInfo{sizeof(ChaoGarageLoaderInfoV1),CHAOGARAGE_LOADER_INFO_V1,CHAOGARAGE_RELEASE_W,SSASR_BOOTSTRAP_ABI_V2,
    SSASR_HOST_ABI_V1,1,1,kCapabilities,CHAOGARAGE_RELEASES_URL_W};

void __cdecl log(const wchar_t*message){if(logPath.empty())return;HANDLE h=CreateFileW(logPath.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return;SYSTEMTIME t;GetSystemTime(&t);wchar_t prefix[64];swprintf_s(prefix,L"%04u-%02u-%02u %02u:%02u:%02u UTC  ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);std::wstring w=prefix;w+=message;w+=L"\r\n";int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),s.data(),n,nullptr,nullptr);DWORD k;WriteFile(h,s.data(),n,&k,nullptr);CloseHandle(h);}
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
std::wstring optionalIni(const std::wstring&p,const wchar_t*k){wchar_t s[512]{};DWORD n=GetPrivateProfileStringW(L"mod",k,L"",s,512,p.c_str());return n<511?std::wstring(s,n):std::wstring();}
std::string narrowAscii(const std::wstring&s){std::string n;for(auto c:s){require(c>0&&c<128,"Expected ASCII manifest field");n+=(char)c;}return n;}
std::string utf8(const std::wstring&w){if(w.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);std::string s(n>0?n:0,0);if(n>0)WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),s.data(),n,nullptr,nullptr);return s;}
std::wstring leaf(const std::wstring&dir){auto p=dir.find_last_of(L'\\');return p==std::wstring::npos?dir:dir.substr(p+1);}
std::wstring hex32(uint32_t v){wchar_t b[16];swprintf_s(b,L"0x%08lX",(unsigned long)v);return b;}

// A failure that knows which mod it concerns and what kind it is, so the one warning can say what to do.
struct Failure:std::runtime_error{msg::Entry entry;Failure(msg::Entry e,const char*technical):std::runtime_error(technical),entry(std::move(e)){}};
// Requirements checked before anything was prepared, loaded or published; every problem of every mod at once.
struct Refused:std::exception{std::vector<msg::Entry>entries;};

void warn(const std::vector<msg::Entry>&entries,bool stopped){
    for(auto&e:entries)log(msg::clean(e.who+L" "+msg::headline(e.cat)+L" "+e.detail,2000).c_str());
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
// Folders in start order before metal-sonic is moved first: ordinal by name (the same sort as always).
std::vector<std::wstring> modFolders(){auto root=modsRoot();WIN32_FIND_DATAW data;HANDLE f=FindFirstFileW((root+L"\\*").c_str(),&data);if(f==INVALID_HANDLE_VALUE)return {};std::vector<std::wstring>dirs;
    do{std::wstring name=data.cFileName;if(name==L"."||name==L".."||!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))continue;
        if(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){FindClose(f);throw Failure({Cat::Environment,L"Mod folder \""+name+L"\"",L"It is a link (junction) to another place; the loader only reads real folders."},"Mod junctions are not supported");}
        dirs.push_back(root+L"\\"+name);}while(FindNextFileW(f,&data));FindClose(f);std::sort(dirs.begin(),dirs.end());return dirs;}

struct Candidate{std::wstring dir,folder,label,homepage;std::string id;compat::Description desc;};

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
        auto config=child(dir,L"mod.ini");if(GetFileAttributesW(config.c_str())==INVALID_FILE_ATTRIBUTES)continue;
        Candidate c;c.dir=dir;c.folder=leaf(dir);c.label=msg::label(c.folder,c.folder);std::wstring name;
        try{
            no_reparse(root,config);
            name=optionalIni(config,L"name");if(!name.empty())c.label=msg::label(name,c.folder);
            auto schema=ini(config,L"schema");
            if(schema!=L"1"){
                bool digits=!schema.empty()&&schema.size()<10&&std::all_of(schema.begin(),schema.end(),[](wchar_t ch){return ch>=L'0'&&ch<=L'9';});
                if(digits&&std::stoul(schema)>1)throw Failure({Cat::NeedsNewerLoader,c.label,L"Its mod.ini uses format version "+schema+L", and this loader release reads version 1."},"Unsupported mod.ini version");
                throw Failure({Cat::Files,c.label,L"Its mod.ini does not state format version 1."},"Unsupported mod.ini version");
            }
            c.id=utf8(ini(config,L"id"));
            c.desc=readDescription(dir,c.id);
        }catch(const Failure&f){refused.push_back(f.entry);continue;}
        catch(const std::exception&e){refused.push_back({Cat::Files,c.label,technical(e.what())});continue;}
        if(name.empty()&&!c.desc.name.empty())c.label=msg::label(msg::wide(c.desc.name),c.folder);
        c.homepage=msg::wide(c.desc.homepage);
        for(auto&n:c.desc.notes)log(msg::clean(c.label+L": "+msg::wide(n),2000).c_str());
        list.push_back(std::move(c));
    }
    compat::Self self{SSASR_HOST_ABI_V1,1,1,kCapabilities,CHAOGARAGE_RELEASE_A,kRetiredCapabilities};
    std::string hostSha,proxySha;bool hostRead=false,proxyRead=false;
    auto host=[&]{if(!hostRead){hostSha=selfSha();hostRead=true;}return hostSha;};
    auto proxy=[&]{if(!proxyRead){proxySha=b->bootstrap_image?moduleSha(b->bootstrap_image):std::string();proxyRead=true;}return proxySha;};
    for(auto&c:list)compat::checkLoader(c.desc,self,host,proxy);
    std::vector<size_t>at(list.size());std::iota(at.begin(),at.end(),size_t(0));
    std::stable_sort(at.begin(),at.end(),[&](size_t x,size_t y){return (list[x].id=="metal-sonic")>(list[y].id=="metal-sonic");});
    std::vector<compat::Started>order;for(auto i:at)order.push_back({list[i].id,utf8(list[i].folder),&list[i].desc});
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

// Unchanged discovery, now over the checked folders. Runtimes keep the start order: folder order, metal-sonic first.
void discover(const std::vector<Candidate>&list){auto root=modsRoot();std::set<std::wstring>ids;
    std::vector<const Candidate*>byFolder;for(auto&c:list)byFolder.push_back(&c);
    std::sort(byFolder.begin(),byFolder.end(),[](const Candidate*x,const Candidate*y){return x->dir<y->dir;});
    for(auto*c:byFolder){auto&dir=c->dir;try{auto config=child(dir,L"mod.ini");no_reparse(root,config);require(ini(config,L"schema")==L"1","Unsupported mod.ini version");auto id=ini(config,L"id");require(ids.insert(id).second,"Duplicate mod ID");auto plan=child(dir,ini(config,L"plan"));no_reparse(root,plan);hash_matches(plan,narrowAscii(ini(config,L"plan_sha256")));auto dataPlan=JsonParser(text_file(plan)).parse();
            if(dataPlan.has("schema")&&dataPlan.at("schema").kind==Json::Number&&dataPlan.at("schema").n>1)throw Failure({Cat::NeedsNewerLoader,c->label,L"Its plan.json uses format version "+std::to_wstring(dataPlan.at("schema").n)+L", and this loader release reads version 1."},"Unsupported plan version");
            auto paths=prepare(gameRoot,dir,dataPlan);for(auto&r:paths)if(r.archive)for(auto&change:r.archive->changes)change.second.owner=c->id;for(auto&g:dataPlan.at("archive_groups").list())if(g.at("format").str()=="xpac")legacyCaches.push_back({dir,g.at("expected_composed_sha256").str(),g.at("expected_composed_size").u32()});for(auto&r:paths){auto found=redirects.find(fold(r.logical));if(found==redirects.end())redirects.emplace(fold(r.logical),r);else {require(found->second.archive&&r.archive,"Two mods replace the same resource; combination needs a merge plan");found->second.archive=merge_virtual_xpac(found->second.archive,r.archive);}}auto runtime=child(dir,ini(config,L"runtime"));no_reparse(root,runtime);hash_matches(runtime,narrowAscii(ini(config,L"runtime_sha256")));Mod m;m.dir=dir;m.runtime=runtime;m.id=id;m.label=c->label;mods.push_back(m);}
        catch(const Failure&){throw;}
        catch(const std::exception&e){auto cat=classify(e.what());throw Failure({cat,c->label,cat==Cat::Conflict?L"Another mod changes the same game file in a way that cannot be combined. "+technical(e.what()):technical(e.what()),c->homepage},e.what());}}
    std::stable_sort(mods.begin(),mods.end(),[](const Mod&a,const Mod&b){return (a.id==L"metal-sonic")>(b.id==L"metal-sonic");});
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
    bool publishing=false;
    try{require(mods.empty()&&hooks.empty(),"Host initialization was invoked twice");require((uintptr_t)b->game_image==0x400000,"Unsupported game image base");gameRoot=full(b->game_directory);
        // The two loader files must come from the same release. A 1.0 dinput8.dll calls with version 1; a 1.1 one names its release.
        std::wstring proxyRelease;
        if(v2){auto r=reinterpret_cast<const SsasrBootstrapV2*>(b)->bootstrap_release;if(r){size_t n=wcsnlen(r,33);if(n<=32)proxyRelease.assign(r,n);}}
        if(v1||proxyRelease!=CHAOGARAGE_RELEASE_W){openMods();if(v2&&proxyRelease.empty())proxyRelease=L"unknown";warn({pairEntry(proxyRelease)},false);return NotStartedWarned;}
        if(!openMods())return S_FALSE;
        auto checked=checkMods(b);if(checked.empty())return S_FALSE;
        discover(checked);if(mods.empty())return S_FALSE;
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
        log(L"READY: ChaoGarage " CHAOGARAGE_RELEASE_W L": mod resources and runtime initialized before game startup. Virtual archives enabled; no full XPAC disk copies.");return S_OK;
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
