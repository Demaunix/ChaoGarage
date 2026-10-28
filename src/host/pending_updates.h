#pragma once
// 1.3.0 ("pending-mod-updates"): a mod's own updater may stage a complete new copy of its folder while the game runs (it
// can't replace the folder then: the loader keeps the mod's payloads open). At the next start, after the mods folder is
// found and before any mod is read, the loader puts it in place:
//   <game>\mods-updates\staged\<folder>\        the new folder, complete
//   <game>\mods-updates\staged\<folder>.ready   written LAST by the updater: format=1, folder=, version=, and one
//                                                file=<sha256> <size> <path> line per file of the staged folder
//   <game>\mods-updates\previous\<folder>\      the replaced folder (the one before it is removed first)
// The staged folder must hold exactly the listed files (sizes and SHA-256, no links); then two renames: mods\<folder> ->
// previous\<folder>, staged\<folder> -> mods\<folder> (the first undone if the second fails). The marker is the journal: a
// start that finds one with mods\<folder> missing (a crash between the renames) finishes or undoes the swap.
// Nothing else is touched. The loader only checks that the staging is complete and unchanged; the updater is the mod's own
// (Steam Multiplayer checks a signed release before it stages anything).
#include "files.h"
#include <functional>
namespace ssasr {
const size_t kPendingMarkers=16,kPendingMarkerBytes=64*1024,kPendingFiles=2000;
struct PendingFile{std::wstring path;uint64_t size=0;std::string sha;};
struct PendingMarker{std::wstring folder,version;std::vector<PendingFile>files;};
struct PendingResult{std::wstring folder,version,detail;bool applied=false;};
inline bool pending_plain_name(const std::wstring&n){
    if(n.empty()||n.size()>100||n==L"."||n==L".."||n.front()==L' '||n.back()==L' '||n.front()==L'.'||n.back()==L'.')return false;
    for(wchar_t c:n)if(c<0x20||c>0x7e||wcschr(L"\\/:*?\"<>|",c))return false;
    return true;}
// The marker's text: "key=value" lines (LF or CRLF), ASCII. Returns a reason, or empty when it is usable.
inline std::string pending_parse(const std::string&text,PendingMarker&m){
    m=PendingMarker{};if(text.size()>kPendingMarkerBytes)return "the marker is larger than 64 KB";
    bool format=false;size_t at=0;
    while(at<text.size()){size_t e=text.find('\n',at);std::string line=text.substr(at,e==text.npos?text.npos:e-at);at=e==text.npos?text.size():e+1;
        if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
        for(unsigned char c:line)if(c<0x20||c>0x7e)return "the marker has a control or non-ASCII character";
        size_t eq=line.find('=');if(eq==line.npos)return "a marker line has no '='";auto key=line.substr(0,eq),value=line.substr(eq+1);
        if(key=="format"){if(format||value!="1")return "the marker's format isn't 1";format=true;}
        else if(!format)return "the marker doesn't start with format=1";
        else if(key=="folder"){if(!m.folder.empty())return "the marker names its folder twice";m.folder=wide(value);if(!pending_plain_name(m.folder))return "the marker's folder isn't a plain folder name";}
        else if(key=="version"){if(!m.version.empty()||value.empty()||value.size()>32)return "the marker's version is missing or too long";m.version=wide(value);}
        else if(key=="file"){if(m.files.size()>=kPendingFiles)return "the marker lists too many files";
            if(value.size()<68||value[64]!=' ')return "a marker file line is malformed";PendingFile f;f.sha=value.substr(0,64);
            for(char c:f.sha)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return "a marker file line is malformed";
            size_t sp=value.find(' ',65);if(sp==value.npos||sp==65||sp-65>12)return "a marker file line is malformed";
            for(size_t i=65;i<sp;++i){if(value[i]<'0'||value[i]>'9')return "a marker file line is malformed";f.size=f.size*10+(value[i]-'0');}
            try{f.path=relative(wide(value.substr(sp+1)));}catch(...){return "a marker file path leaves the folder";}
            for(auto&g:m.files)if(fold(g.path)==fold(f.path))return "a file is listed twice";
            m.files.push_back(f);}
        else return "the marker has an unknown key";}
    if(!format||m.folder.empty()||m.version.empty()||m.files.empty())return "the marker is missing a line";
    return {};}
inline bool pending_exists(const std::wstring&p){return GetFileAttributesW(p.c_str())!=INVALID_FILE_ATTRIBUTES;}
inline bool pending_real_dir(const std::wstring&p){DWORD a=GetFileAttributesW(p.c_str());return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY)&&!(a&FILE_ATTRIBUTE_REPARSE_POINT);}
// Every file under dir as relative paths; a link (file or folder) anywhere is refused.
inline std::string pending_list(const std::wstring&dir,const std::wstring&prefix,std::vector<std::wstring>&out){
    WIN32_FIND_DATAW d;HANDLE f=FindFirstFileW((dir+L"\\*").c_str(),&d);if(f==INVALID_HANDLE_VALUE)return "the staged folder can't be read";std::string why;
    do{std::wstring n=d.cFileName;if(n==L"."||n==L"..")continue;auto rel=prefix.empty()?n:prefix+L"\\"+n;
        if(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)why="the staged folder holds a link";
        else if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)why=pending_list(dir+L"\\"+n,rel,out);
        else if(out.size()>=kPendingFiles)why="the staged folder holds too many files";
        else out.push_back(rel);}while(why.empty()&&FindNextFileW(f,&d));
    FindClose(f);return why;}
// The staged folder holds exactly the marker's files, each with its size and SHA-256.
inline std::string pending_verify(const std::wstring&staged,const PendingMarker&m){
    if(!pending_real_dir(staged))return "the staged folder is missing or a link";
    std::vector<std::wstring>found;auto why=pending_list(staged,L"",found);if(!why.empty())return why;
    if(found.size()!=m.files.size())return "the staged folder's files differ from the marker's";
    for(auto&f:m.files){bool listed=false;for(auto&g:found)listed=listed||fold(g)==fold(f.path);if(!listed)return "the staged folder's files differ from the marker's";
        try{Handle h(CreateFileW((staged+L"\\"+f.path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
            if(h.h==INVALID_HANDLE_VALUE)return "a staged file can't be read";if(size(h.h)!=f.size)return "a staged file's size differs from the marker's";
            if(hash_range(h.h,0,f.size)!=f.sha)return "a staged file's SHA-256 differs from the marker's";}catch(...){return "a staged file can't be read";}}
    if(!pending_exists(staged+L"\\mod.ini"))return "the staged folder has no mod.ini";
    return {};}
// Removes a tree without ever following a link (a link is removed as an entry).
inline bool pending_remove(const std::wstring&p){DWORD a=GetFileAttributesW(p.c_str());if(a==INVALID_FILE_ATTRIBUTES)return GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND;
    if(!(a&FILE_ATTRIBUTE_DIRECTORY)||(a&FILE_ATTRIBUTE_REPARSE_POINT)){SetFileAttributesW(p.c_str(),FILE_ATTRIBUTE_NORMAL);return (a&FILE_ATTRIBUTE_DIRECTORY)?RemoveDirectoryW(p.c_str())!=0:DeleteFileW(p.c_str())!=0;}
    WIN32_FIND_DATAW d;HANDLE f=FindFirstFileW((p+L"\\*").c_str(),&d);bool ok=true;
    if(f!=INVALID_HANDLE_VALUE){do{std::wstring n=d.cFileName;if(n!=L"."&&n!=L"..")ok=pending_remove(p+L"\\"+n)&&ok;}while(FindNextFileW(f,&d));FindClose(f);}
    return RemoveDirectoryW(p.c_str())!=0&&ok;}
// A rename on the same drive, tried a few times (an antivirus scan may hold a new file for a moment).
inline bool pending_move(const std::wstring&from,const std::wstring&to){for(int i=0;i<10;++i){if(MoveFileExW(from.c_str(),to.c_str(),0))return true;Sleep(100);}return false;}
inline std::string pending_text(const std::wstring&p){Handle f(CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(f.h==INVALID_HANDLE_VALUE)return {};auto n=size(f.h);if(n>kPendingMarkerBytes)return std::string(kPendingMarkerBytes+1,' ');std::string s((size_t)n,0);if(n)read(f.h,s.data(),(DWORD)n);return s;}
// One marker. `mods`: <game>\mods; `root`: <game>\mods-updates.
inline PendingResult pending_apply_one(const std::wstring&mods,const std::wstring&root,const std::wstring&markerName){
    PendingResult r;r.folder=markerName.substr(0,markerName.size()-6);auto marker=root+L"\\staged\\"+markerName;
    auto drop=[&](const std::string&why){r.detail=wide(why);pending_remove(root+L"\\staged\\"+r.folder);DeleteFileW(marker.c_str());return r;};
    if(!pending_plain_name(r.folder))return drop("the marker's name isn't a plain folder name");
    PendingMarker m;auto why=pending_parse(pending_text(marker),m);if(!why.empty())return drop(why);
    r.version=m.version;if(fold(m.folder)!=fold(r.folder))return drop("the marker names another folder");
    auto live=mods+L"\\"+r.folder,staged=root+L"\\staged\\"+r.folder,previous=root+L"\\previous\\"+r.folder;
    DWORD la=GetFileAttributesW(live.c_str());if(la!=INVALID_FILE_ATTRIBUTES&&(!(la&FILE_ATTRIBUTE_DIRECTORY)||(la&FILE_ATTRIBUTE_REPARSE_POINT)))return drop("the mod's folder isn't a real folder");
    bool liveThere=la!=INVALID_FILE_ATTRIBUTES;
    why=pending_verify(staged,m);
    if(!liveThere){
        // Only a start that stopped between the two renames (the previous copy is there): never a new install.
        if(!pending_real_dir(previous))return drop("the mod isn't installed (an update only replaces an installed mod)");
        if(why.empty()&&pending_move(staged,live)){DeleteFileW(marker.c_str());r.applied=true;r.detail=L"finished an update that a previous start had begun";return r;}
        if(pending_move(previous,live)){r.detail=L"put the previous copy back (the update was incomplete)";pending_remove(staged);DeleteFileW(marker.c_str());return r;}
        r.detail=L"the mod's folder is missing and its previous copy can't be put back (kept for the next start)";return r;}
    if(!why.empty())return drop(why);
    if(!pending_remove(previous)){r.detail=L"the older previous copy can't be removed (kept for the next start)";return r;}
    CreateDirectoryW((root+L"\\previous").c_str(),nullptr);
    if(!pending_move(live,previous)){r.detail=L"the mod's folder is in use (kept for the next start)";return r;}
    if(!pending_move(staged,live)){r.detail=pending_move(previous,live)?L"the new folder can't be moved in (kept for the next start)":L"the new folder can't be moved in, and the mod's folder can't be put back";return r;}
    DeleteFileW(marker.c_str());r.applied=true;return r;}
// Every waiting update, before any mod is read. `mods`: the real mods folder; its sibling mods-updates holds the staging.
inline std::vector<PendingResult> apply_pending_updates(const std::wstring&mods){
    std::vector<PendingResult>out;size_t cut=mods.find_last_of(L'\\');if(cut==mods.npos)return out;
    auto root=mods.substr(0,cut)+L"\\mods-updates",staged=root+L"\\staged";
    if(!pending_real_dir(root)||!pending_real_dir(staged))return out;
    std::vector<std::wstring>markers;WIN32_FIND_DATAW d;HANDLE f=FindFirstFileW((staged+L"\\*.ready").c_str(),&d);if(f==INVALID_HANDLE_VALUE)return out;
    do{std::wstring n=d.cFileName;if(!(d.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&n.size()>6&&fold(n.substr(n.size()-6))==L".ready")markers.push_back(n);}while(FindNextFileW(f,&d));
    FindClose(f);std::sort(markers.begin(),markers.end());if(markers.size()>kPendingMarkers)markers.resize(kPendingMarkers);
    for(auto&n:markers)try{out.push_back(pending_apply_one(mods,root,n));}catch(...){PendingResult r;r.folder=n;r.detail=L"stopped by an internal error";out.push_back(r);}
    return out;}
}
