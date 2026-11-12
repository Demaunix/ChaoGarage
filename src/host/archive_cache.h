#pragma once
// What the loader remembers between starts so that it does not read gigabytes of unchanged files at every start. Three records
// in mods\.chaogarage-cache, each a change detector (not a proof) that only ever saves work the loader would otherwise repeat:
//   * verified-archives.txt (1.3.2): an original game archive whose whole-file SHA-256 equals the plan's base_sha256. Several
//     mods naming one archive hash it at most once per start (remembered in memory).
//   * verified-mod-files.txt (1.3.3): a mod's own payload file whose SHA-256 equals the plan's payload_sha256.
//   * verified-composed.txt (1.3.3): one mod's combined layout of an archive whose SHA-256 equals the plan's
//     expected_composed_sha256. That layout is a fixed function of the loader build, the plan (whose SHA-256 is checked against
//     mod.ini at every start, from the very bytes that are parsed), the original archive and the payloads; so once those are
//     verified at a start, a layout made from the same ones was already checked. Its record is keyed by exactly those inputs
//     and written only after the full hash matched at a start.
// A file is remembered by its identity, read from the open handle that also denies writers: full path, volume serial, 128-bit
// file ID, size, creation, last-write and change times, and the file's change-journal number when the drive keeps a journal.
// The change time (1.3.3) moves at every write and every change of the file's times or attributes, also when a tool puts the
// last-write time back afterwards, and Windows' usual calls cannot set it; it is what makes a copy over the file, an extraction
// over it or an edit in place show, even with the same size and dates. A record is used only if every part still matches AND
// it holds the SHA-256 the plan requires. Anything else (no record, an unreadable or damaged record, another folder or volume,
// a drive that is not NTFS or ReFS, a link in place of the file, any mismatch) means the whole file is hashed, as before.
// What a record cannot notice: a change made without any of those parts moving (for example a sector going bad on the disk, or
// a tool that deliberately writes the change time back with low-level calls). The per-member hash checks of the original
// archives stay at every start. Deleting the records is always safe.
#include "files.h"
#include <winioctl.h>
#include <cstdio>
#include <map>
#include <string>
namespace ssasr {
namespace archive_cache {
struct Stats{unsigned hashed=0,fromMemory=0,fromRecord=0,written=0;uint64_t hashedBytes=0;
    unsigned filesHashed=0,filesFromMemory=0,filesFromRecord=0;uint64_t filesHashedBytes=0;          // 1.3.3: mods' payload files
    unsigned composedHashed=0,composedFromRecord=0;uint64_t composedHashedBytes=0;};                // 1.3.3: combined layouts
// One record file: a header line, "key|sha" lines, then "end|<SHA-256 of everything above>".
struct Records{const wchar_t*name;const char*header;size_t maxRecords;uint64_t maxBytes;std::map<std::string,std::string>map;bool loaded=false,dirty=false;};
struct State{std::wstring directory;                  // mods\.chaogarage-cache; empty: no records are read or written
    std::string build;                                // 1.3.3: SHA-256 of this ChaoGarage.dll; empty: no layout record is used
    std::map<std::string,std::string>memo,fileMemo;   // identity key -> the SHA-256 verified in this start
    Records archives{L"verified-archives.txt","chaogarage-verified-archives 2\n",256,1024*1024};
    Records files{L"verified-mod-files.txt","chaogarage-verified-mod-files 1\n",8192,8*1024*1024};
    Records composed{L"verified-composed.txt","chaogarage-verified-composed 1\n",1024,1024*1024};
    Stats stats;};
inline State&state(){static State s;return s;}
inline void reset(Records&r){r.map.clear();r.loaded=false;r.dirty=false;}
inline void load(Records&r);inline void save(Records&r);
// Writes the records changed in this start (payload files and layouts are written once, at the end of preparing the mods).
inline void flush(){auto&s=state();for(Records*r:{&s.archives,&s.files,&s.composed})if(r->dirty){save(*r);r->dirty=false;}}
// For tests: end this "start" (its records are written) and forget what this process knows (the records on disk stay).
inline void forget_memory(){flush();auto&s=state();s.memo.clear();s.fileMemo.clear();reset(s.archives);reset(s.files);reset(s.composed);}
inline void set_directory(const std::wstring&dir){auto&s=state();s.directory=dir;s.memo.clear();s.fileMemo.clear();reset(s.archives);reset(s.files);reset(s.composed);}
inline void set_build(const std::string&b){state().build=b;}
inline std::wstring record_path(const Records&r){auto&s=state();return s.directory.empty()?std::wstring():s.directory+L"\\"+r.name;}
inline std::wstring record_path(){return record_path(state().archives);}
inline std::string narrow(const std::wstring&w){if(w.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);require(n>0,"Invalid path text");std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),s.data(),n,nullptr,nullptr);return s;}
inline bool is_sha_text(const std::string&s){return s.size()==64&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
// The file's change-journal number, or 0 when the drive keeps no journal or does not say (then the other parts decide).
inline uint64_t journal_number(HANDLE file){
    struct{WORD minMajor,maxMajor;}in{2,3};unsigned char out[1024];DWORD got=0;
    if(!DeviceIoControl(file,FSCTL_READ_FILE_USN_DATA,&in,sizeof(in),out,sizeof(out),&got,nullptr)||got<8)return 0;
    WORD major;memcpy(&major,out+4,2);size_t at=major==2?24:major==3?40:0;if(!at||got<at+8)return 0;
    int64_t usn;memcpy(&usn,out+at,8);return usn>0?(uint64_t)usn:0;}
// The key: "<folded full path>|volume|file ID|size|write|creation|change|journal". False when the file system gives no usable
// identity (no volume serial, file ID or change time, not NTFS or ReFS, a link): then nothing is remembered or trusted.
inline bool identity(HANDLE file,const std::wstring&path,std::string&key){
    BY_HANDLE_FILE_INFORMATION i{};if(!GetFileInformationByHandle(file,&i))return false;
    if(i.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))return false;
    // The 128-bit file ID and 64-bit volume serial (the 64-bit index alone is not unique on ReFS).
    FILE_ID_INFO id{};if(!GetFileInformationByHandleEx(file,FileIdInfo,&id,sizeof(id))||id.VolumeSerialNumber==0)return false;
    {bool zero=true;for(auto b:id.FileId.Identifier)if(b)zero=false;if(zero)return false;}
    FILE_BASIC_INFO basic{};if(!GetFileInformationByHandleEx(file,FileBasicInfo,&basic,sizeof(basic))||basic.ChangeTime.QuadPart==0)return false;
    // Only NTFS and ReFS give file IDs and times that are exact and stable; anything else (FAT, exFAT, a network drive) is hashed.
    wchar_t fs[32]{};if(!GetVolumeInformationByHandleW(file,nullptr,0,nullptr,nullptr,nullptr,fs,32)||(_wcsicmp(fs,L"NTFS")!=0&&_wcsicmp(fs,L"ReFS")!=0))return false;
    // The handle follows a link silently, so the name itself is looked at too: a link in place of the file is never remembered.
    {DWORD a=GetFileAttributesW(path.c_str());if(a==INVALID_FILE_ATTRIBUTES||(a&FILE_ATTRIBUTE_REPARSE_POINT))return false;}
    std::string p;try{p=narrow(fold(full(path)));}catch(...){return false;}
    if(p.find('|')!=p.npos||p.find('\n')!=p.npos||p.find('\r')!=p.npos)return false;
    char b[320];int n=sprintf_s(b,"|%016llx|",(unsigned long long)id.VolumeSerialNumber);
    for(int k=15;k>=0;--k)n+=sprintf_s(b+n,sizeof(b)-n,"%02x",(unsigned)id.FileId.Identifier[k]);
    sprintf_s(b+n,sizeof(b)-n,"|%08lx%08lx|%016llx|%016llx|%016llx|%016llx",(unsigned long)i.nFileSizeHigh,(unsigned long)i.nFileSizeLow,
        (unsigned long long)basic.LastWriteTime.QuadPart,(unsigned long long)basic.CreationTime.QuadPart,(unsigned long long)basic.ChangeTime.QuadPart,
        (unsigned long long)journal_number(file));
    key=p+b;return true;}
inline std::string text_hash(const std::string&t){Hash h;if(!t.empty())h.add(t.data(),(DWORD)t.size());return h.finish();}
// Reads a record file once. Any doubt (missing, unreadable, too big, wrong header, wrong check line, a bad line) leaves the
// whole map empty.
inline void load(Records&r){
    auto&s=state();if(r.loaded)return;r.loaded=true;r.map.clear();
    try{auto path=record_path(r);if(path.empty())return;
        no_reparse(s.directory.substr(0,s.directory.find_last_of(L'\\')),path);   // a link in the way: no records
        Handle f(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));if(f.h==INVALID_HANDLE_VALUE)return;
        auto n=size(f.h);if(n>r.maxBytes)return;std::string t((size_t)n,0);if(n)read(f.h,t.data(),(DWORD)n);
        std::string head=r.header;if(t.compare(0,head.size(),head)!=0)return;
        auto at=t.rfind("end|");if(at==t.npos||at<head.size()||(at>0&&t[at-1]!='\n'))return;
        auto tail=t.substr(at);if(tail.size()!=4+64+1||tail.back()!='\n'||tail.substr(4,64)!=text_hash(t.substr(0,at)))return;
        std::map<std::string,std::string>m;size_t p=head.size();
        while(p<at){auto e=t.find('\n',p);if(e==t.npos||e>at)return;auto line=t.substr(p,e-p);p=e+1;auto cut=line.rfind('|');if(cut==line.npos)return;
            auto key=line.substr(0,cut),sha=line.substr(cut+1);if(!is_sha_text(sha)||key.empty())return;m[key]=sha;}
        if(m.size()>r.maxRecords)return;
        r.map=std::move(m);
    }catch(...){r.map.clear();}}
inline void save(Records&r){
    auto&s=state();
    try{auto path=record_path(r);if(path.empty())return;
        auto parent=s.directory.substr(0,s.directory.find_last_of(L'\\'));mkdirs(parent,s.directory);
        {   // a start that was killed while writing leaves its temporary file behind: remove those older than an hour
            WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW((path+L".tmp-*").c_str(),&d);
            if(h!=INVALID_HANDLE_VALUE){FILETIME now;GetSystemTimeAsFileTime(&now);auto t=[](const FILETIME&f){return (uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;};
                do{if(!(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&t(now)>t(d.ftLastWriteTime)&&t(now)-t(d.ftLastWriteTime)>3600ull*10000000ull)DeleteFileW((s.directory+L"\\"+d.cFileName).c_str());}while(FindNextFileW(h,&d));
                FindClose(h);}}
        std::string t=r.header;for(auto&[k,v]:r.map)t+=k+"|"+v+"\n";t+="end|"+text_hash(t)+"\n";
        if(t.size()>r.maxBytes)return;
        auto temp=path+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        {Handle f(CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));if(f.h==INVALID_HANDLE_VALUE)return;
            try{write(f.h,t.data(),(DWORD)t.size());FlushFileBuffers(f.h);}catch(...){CloseHandle(f.h);f.h=INVALID_HANDLE_VALUE;DeleteFileW(temp.c_str());return;}}
        try{atomic_publish(temp,path);++s.stats.written;}catch(...){DeleteFileW(temp.c_str());}
    }catch(...){}}
inline void load(){load(state().archives);}
inline void save(){save(state().archives);}
// Adds a record for one key, dropping the older records that start with the same prefix (one record per place).
inline void remember(Records&r,const std::string&key,const std::string&prefix,const std::string&sha){
    load(r);for(auto it=r.map.begin();it!=r.map.end();)it=it->first.compare(0,prefix.size(),prefix)==0?r.map.erase(it):std::next(it);
    if(r.map.size()>=r.maxRecords)r.map.clear();
    r.map[key]=sha;r.dirty=true;}
// Throws `mismatch` when the file is not the one the plan names. `file` is the open handle (read access, writers denied) the
// caller keeps using. Archives are written to their record at once (as in 1.3.2); mod files when the records are flushed.
inline void verify_whole(Records&r,std::map<std::string,std::string>&memo,HANDLE file,uint64_t length,const std::wstring&path,const std::string&expected,const char*mismatch,
        unsigned&hashed,uint64_t&hashedBytes,unsigned&fromMemory,unsigned&fromRecord,bool writeNow){
    std::string key;bool keyed=is_sha_text(expected)&&identity(file,path,key);
    if(keyed){
        auto m=memo.find(key);if(m!=memo.end()&&m->second==expected){++fromMemory;return;}
        load(r);auto f=r.map.find(key);if(f!=r.map.end()&&f->second==expected){memo[key]=expected;++fromRecord;return;}
    }
    auto actual=hash_range(file,0,length);++hashed;hashedBytes+=length;
    require(actual==expected,mismatch);
    if(keyed){memo[key]=expected;remember(r,key,key.substr(0,key.find('|')+1),expected);if(writeNow){save(r);r.dirty=false;}}}
// Throws "Original archive differs from required unmodified game file" when the file is not the one the plan names.
inline void verify_original(HANDLE file,uint64_t length,const std::wstring&path,const std::string&expected){
    auto&s=state();verify_whole(s.archives,s.memo,file,length,path,expected,"Original archive differs from required unmodified game file",
        s.stats.hashed,s.stats.hashedBytes,s.stats.fromMemory,s.stats.fromRecord,true);}
// 1.3.3: a mod's payload file. Throws "Payload hash mismatch" when it is not the one the plan names.
inline void verify_payload(HANDLE file,uint64_t length,const std::wstring&path,const std::string&expected){
    auto&s=state();verify_whole(s.files,s.fileMemo,file,length,path,expected,"Payload hash mismatch",
        s.stats.filesHashed,s.stats.filesHashedBytes,s.stats.filesFromMemory,s.stats.filesFromRecord,false);}
// 1.3.3: one mod's combined layout of one archive. The place is the mod folder and the archive (one record each); the
// material is everything the layout is made of. Both are empty (no record used or written) when anything is missing or odd.
inline std::string composed_place(const std::wstring&mod,const std::wstring&archive){
    try{auto p=narrow(fold(full(mod)))+"*"+narrow(fold(full(archive)));if(p.find_first_of("|\r\n")!=p.npos)return {};return p;}catch(...){return {};}}
inline std::string composed_material(const std::string&planSha,size_t group,const std::wstring&archive,const std::string&baseSha,const std::string&expected,uint64_t bytes,uint64_t members){
    auto&s=state();if(!is_sha_text(s.build)||!is_sha_text(planSha)||!is_sha_text(baseSha)||!is_sha_text(expected))return {};
    try{std::string t="chaogarage-composed/1\n"+s.build+"\n"+planSha+"\n"+std::to_string(group)+"\n"+narrow(fold(full(archive)))+"\n"+baseSha+"\n"+expected+"\n"+std::to_string(bytes)+"\n"+std::to_string(members)+"\n";
        return text_hash(t);}catch(...){return {};}}
inline bool composed_known(const std::string&place,const std::string&material,const std::string&expected){
    auto&s=state();if(place.empty()||material.empty()||!is_sha_text(expected))return false;
    load(s.composed);auto f=s.composed.map.find(place+"|"+material);if(f==s.composed.map.end()||f->second!=expected)return false;
    ++s.stats.composedFromRecord;return true;}
// Called only after the full hash of the layout matched in this start.
inline void composed_verified(const std::string&place,const std::string&material,const std::string&expected){
    auto&s=state();if(place.empty()||material.empty()||!is_sha_text(expected))return;
    remember(s.composed,place+"|"+material,place+"|",expected);}
}
}
