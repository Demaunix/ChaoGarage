#pragma once
// 1.3.2: the check that an original game archive is unmodified (its whole-file SHA-256 equals the plan's base_sha256), done
// without reading the whole file at every start.
//   * At most once per start: an archive that several mods name is hashed once (the first mod's check is remembered in memory).
//   * Across starts: a verified archive is remembered in mods\.chaogarage-cache\verified-archives.txt, keyed by the file's full
//     path, size, creation time, last-write time, NTFS file ID and volume serial number (all read from the open handle that
//     also denies writers). The record is used only if every one of those still matches AND its SHA-256 equals the one the plan
//     requires. Anything else (no record, an unreadable or damaged record, a different folder or volume, a drive that is not NTFS
//     or ReFS, a link in place of the archive, any mismatch) means the whole file is hashed, as before. A record is written only
//     after a full hash matched. The record is a change detector, not a proof: a tool that edits an archive in place and then
//     restores its size and last-write time would not be noticed (the same trade-off every build cache makes).
// Every per-member hash check of the callers is unchanged. The record file is an optimisation: deleting it is always safe.
#include "files.h"
#include <cstdio>
#include <map>
#include <string>
namespace ssasr {
namespace archive_cache {
struct Stats{unsigned hashed=0,fromMemory=0,fromRecord=0,written=0;uint64_t hashedBytes=0;};
struct State{std::wstring directory;                  // mods\.chaogarage-cache; empty: no records are read or written
    std::map<std::string,std::string>memo,records;    // identity key -> the verified SHA-256
    bool loaded=false;Stats stats;};
inline State&state(){static State s;return s;}
// For tests: forget what this process knows (the records on disk stay).
inline void forget_memory(){auto&s=state();s.memo.clear();s.records.clear();s.loaded=false;}
inline void set_directory(const std::wstring&dir){auto&s=state();s.directory=dir;s.memo.clear();s.records.clear();s.loaded=false;}
inline std::wstring record_path(){auto&s=state();return s.directory.empty()?std::wstring():s.directory+L"\\verified-archives.txt";}
inline std::string narrow(const std::wstring&w){if(w.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);require(n>0,"Invalid path text");std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),s.data(),n,nullptr,nullptr);return s;}
inline bool is_sha_text(const std::string&s){return s.size()==64&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
// The key: "<folded full path>|volume|idHigh|idLow|sizeHigh|sizeLow|writeHigh|writeLow|createHigh|createLow". False when the
// file system gives no usable identity (volume serial or file index zero): then nothing is remembered or trusted.
inline bool identity(HANDLE file,const std::wstring&path,std::string&key){
    BY_HANDLE_FILE_INFORMATION i{};if(!GetFileInformationByHandle(file,&i))return false;
    if(i.dwVolumeSerialNumber==0||(i.nFileIndexHigh==0&&i.nFileIndexLow==0)||(i.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return false;
    // Only NTFS and ReFS give file IDs and times that are exact and stable; anything else (FAT, exFAT, a network drive) is hashed.
    wchar_t fs[32]{};if(!GetVolumeInformationByHandleW(file,nullptr,0,nullptr,nullptr,nullptr,fs,32)||(_wcsicmp(fs,L"NTFS")!=0&&_wcsicmp(fs,L"ReFS")!=0))return false;
    // The handle follows a link silently, so the name itself is looked at too: a link in place of the archive file is never remembered.
    {DWORD a=GetFileAttributesW(path.c_str());if(a==INVALID_FILE_ATTRIBUTES||(a&FILE_ATTRIBUTE_REPARSE_POINT))return false;}
    std::string p;try{p=narrow(fold(full(path)));}catch(...){return false;}
    if(p.find('|')!=p.npos||p.find('\n')!=p.npos||p.find('\r')!=p.npos)return false;
    char b[200];sprintf_s(b,"|%08lx|%08lx|%08lx|%08lx|%08lx|%08lx|%08lx|%08lx|%08lx",(unsigned long)i.dwVolumeSerialNumber,(unsigned long)i.nFileIndexHigh,(unsigned long)i.nFileIndexLow,
        (unsigned long)i.nFileSizeHigh,(unsigned long)i.nFileSizeLow,(unsigned long)i.ftLastWriteTime.dwHighDateTime,(unsigned long)i.ftLastWriteTime.dwLowDateTime,
        (unsigned long)i.ftCreationTime.dwHighDateTime,(unsigned long)i.ftCreationTime.dwLowDateTime);
    key=p+b;return true;}
inline std::string text_hash(const std::string&t){Hash h;if(!t.empty())h.add(t.data(),(DWORD)t.size());return h.finish();}
constexpr const char*kHeader="chaogarage-verified-archives 1\n";
// Reads the record file once. Any doubt (missing, unreadable, wrong header, wrong check line, a bad line) leaves the
// whole map empty.
inline void load(){
    auto&s=state();if(s.loaded)return;s.loaded=true;s.records.clear();
    try{auto path=record_path();if(path.empty())return;
        no_reparse(s.directory.substr(0,s.directory.find_last_of(L'\\')),path);   // a link in the way: no records
        Handle f(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));if(f.h==INVALID_HANDLE_VALUE)return;
        auto n=size(f.h);if(n>1024*1024)return;std::string t((size_t)n,0);if(n)read(f.h,t.data(),(DWORD)n);
        std::string head=kHeader;if(t.compare(0,head.size(),head)!=0)return;
        auto at=t.rfind("end|");if(at==t.npos||at<head.size()||(at>0&&t[at-1]!='\n'))return;
        auto tail=t.substr(at);if(tail.size()!=4+64+1||tail.back()!='\n'||tail.substr(4,64)!=text_hash(t.substr(0,at)))return;
        std::map<std::string,std::string>r;size_t p=head.size();
        while(p<at){auto e=t.find('\n',p);if(e==t.npos||e>at)return;auto line=t.substr(p,e-p);p=e+1;auto cut=line.rfind('|');if(cut==line.npos)return;
            auto key=line.substr(0,cut),sha=line.substr(cut+1);if(!is_sha_text(sha)||key.empty())return;r[key]=sha;}
        s.records=std::move(r);
    }catch(...){s.records.clear();}}
inline void save(){
    auto&s=state();
    try{auto path=record_path();if(path.empty())return;
        auto parent=s.directory.substr(0,s.directory.find_last_of(L'\\'));mkdirs(parent,s.directory);
        {   // a start that was killed while writing leaves its temporary file behind: remove those older than an hour
            WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW((path+L".tmp-*").c_str(),&d);
            if(h!=INVALID_HANDLE_VALUE){FILETIME now;GetSystemTimeAsFileTime(&now);auto t=[](const FILETIME&f){return (uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;};
                do{if(!(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&t(now)>t(d.ftLastWriteTime)&&t(now)-t(d.ftLastWriteTime)>3600ull*10000000ull)DeleteFileW((s.directory+L"\\"+d.cFileName).c_str());}while(FindNextFileW(h,&d));
                FindClose(h);}}
        std::string t=kHeader;for(auto&[k,v]:s.records)t+=k+"|"+v+"\n";t+="end|"+text_hash(t)+"\n";
        auto temp=path+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        {Handle f(CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));if(f.h==INVALID_HANDLE_VALUE)return;
            try{write(f.h,t.data(),(DWORD)t.size());FlushFileBuffers(f.h);}catch(...){CloseHandle(f.h);f.h=INVALID_HANDLE_VALUE;DeleteFileW(temp.c_str());return;}}
        try{atomic_publish(temp,path);++s.stats.written;}catch(...){DeleteFileW(temp.c_str());}
    }catch(...){}}
// Throws "Original archive differs from required unmodified game file" when the file is not the one the plan names.
// `file` is the open handle (read access, writers denied) the caller keeps using.
inline void verify_original(HANDLE file,uint64_t length,const std::wstring&path,const std::string&expected){
    auto&s=state();std::string key;bool keyed=is_sha_text(expected)&&identity(file,path,key);
    if(keyed){
        auto m=s.memo.find(key);if(m!=s.memo.end()&&m->second==expected){++s.stats.fromMemory;return;}
        load();auto r=s.records.find(key);if(r!=s.records.end()&&r->second==expected){s.memo[key]=expected;++s.stats.fromRecord;return;}
    }
    auto actual=hash_range(file,0,length);++s.stats.hashed;s.stats.hashedBytes+=length;
    require(actual==expected,"Original archive differs from required unmodified game file");
    if(keyed){
        s.memo[key]=expected;load();
        // One record per path: an older identity of the same file is dropped.
        auto prefix=key.substr(0,key.find('|')+1);for(auto it=s.records.begin();it!=s.records.end();)it=it->first.compare(0,prefix.size(),prefix)==0?s.records.erase(it):std::next(it);
        if(s.records.size()>=256)s.records.clear();
        s.records[key]=expected;save();
    }}
}
}
