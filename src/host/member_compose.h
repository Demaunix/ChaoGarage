#pragma once
// 1.3.0: puts patched members together, after every mod's whole-member changes were combined (1.2.0 rules). For each member
// that has patches: the member as combined so far, then each mod's patch in folder order (member_patch.h: clashes, optional
// giving way). The result is kept in mods\.chaogarage-cache\<name>\ and served like any other member. The name is the SHA-256
// of everything the result depends on (this loader build, the member, every patch), so a changed input never reuses an old
// result; the result itself is checked against its recorded SHA-256 at every start.
#include "virtual_xpac.h"
namespace ssasr {
// A clash nobody gave way on, naming the later mod (for the warning) and both sides.
struct MemberConflict:std::runtime_error{std::string later;MemberConflict(const std::string&l,const std::string&what):std::runtime_error(what),later(l){}};
// A patch that does not give, on the original member, the result its plan names: the mod's files do not match.
struct MemberPatchMismatch:std::runtime_error{std::string owner;MemberPatchMismatch(const std::string&o,const std::string&what):std::runtime_error(what),owner(o){}};
constexpr int kScenePackLevel=6;   // the loader's zlib level for patched scenes; the plans pin unpacked content, never packed bytes

inline std::string read_range(VirtualSource&s,uint64_t off,uint32_t n){std::lock_guard<std::mutex>lock(s.mutex);std::string b(n,'\0');if(n){seek(s.sync.h,off);read(s.sync.h,b.data(),n);}return b;}
inline std::string sha_text(const std::string&s){Hash h;if(!s.empty())h.add(s.data(),(DWORD)s.size());return h.finish();}
struct MemberAt {uint32_t source;uint64_t offset;uint32_t bytes;};
inline std::map<uint32_t,MemberAt> archive_members(const VirtualArchive&a){
    struct Row{uint32_t key,off,size2,stored,flags;};std::map<uint32_t,MemberAt>m;require(a.metadata.size()>=24,"Missing archive metadata");
    uint32_t count;memcpy(&count,a.metadata.data()+12,4);require(24ull+20ull*count<=a.metadata.size(),"Invalid archive directory");
    for(uint32_t i=0;i<count;++i){Row r{};memcpy(&r,a.metadata.data()+24+20*i,20);
        if(!r.stored){m[r.key]={VirtualArchive::Zero,0,0};continue;}
        const auto&span=a.spans.at(a.locate(r.off));require(span.start==r.off&&span.length==r.stored&&span.source<a.sources.size(),"Invalid member source span");
        m[r.key]={span.source,span.offset,r.stored};}
    return m;
}
inline void archive_layout(VirtualArchive&a,const std::map<uint32_t,MemberAt>&members){
    struct Row{uint32_t key,off,size2,stored,flags;};
    uint32_t header[6]={0,0,(uint32_t)virtual_align(24ull+20ull*members.size()),(uint32_t)members.size(),0,0};
    a.metadata.assign(header[2],0);memcpy(a.metadata.data(),header,24);a.spans.clear();
    a.spans.push_back({0,header[2],0,VirtualArchive::Metadata});uint64_t cursor=header[2];size_t i=0;
    for(auto&[key,m]:members){Row r{key,(uint32_t)cursor,m.bytes,m.bytes,0};memcpy(a.metadata.data()+24+20*i++,&r,20);
        if(m.bytes)a.spans.push_back({cursor,m.bytes,m.offset,m.source});
        auto end=virtual_align(cursor+m.bytes);if(end>cursor+m.bytes)a.spans.push_back({cursor+m.bytes,end-cursor-m.bytes,0,VirtualArchive::Zero});cursor=end;}
    a.bytes=cursor;
}

// Applies the patches of every patched member of one archive. `cacheRoot`: mods\.chaogarage-cache; `build`: the SHA-256 of this
// loader's ChaoGarage.dll; `log` receives one line per member and per note; `used` collects the cache entries this start uses.
inline void compose_patched_members(VirtualArchive&a,const std::wstring&modsRoot,const std::wstring&cacheRoot,const std::string&build,
        const std::function<void(const std::string&)>&log,std::set<std::wstring>&used){
    if(a.patches.empty())return;
    auto members=archive_members(a);
    for(auto&[key,list]:a.patches){
        auto at=members.find(key);require(at!=members.end(),"Patched member absent");
        auto&m=at->second;std::string base=m.source==VirtualArchive::Zero?std::string():read_range(*a.sources.at(m.source),m.offset,m.bytes);
        bool scene=(bool)list[0].scene;for(auto&p:list)require((bool)p.scene==scene,"Two mods patch one member in different formats");
        std::string material="chaogarage-member/1\n"+build+"\n"+virtual_key_text(key)+"\n"+sha_text(base)+"\n";
        for(auto&p:list)material+=p.owner+"\n"+p.patchSha+"\n"+(p.optional?"optional":"required")+"\n";
        auto name=wide(sha_text(material));auto dir=child(cacheRoot,name);auto file=child(dir,L"member.bin"),info=child(dir,L"info.json");
        no_reparse(modsRoot,file);no_reparse(modsRoot,info);used.insert(fold(name));
        std::string data;std::vector<std::string>notes;bool cached=false;
        try{auto j=JsonParser(text_file(info,1u<<20)).parse();auto bytes=text_file(file,256u*1024*1024);
            if(sha_text(bytes)==j.at("sha256").str()){for(auto&n:j.at("notes").list())notes.push_back(n.str());data=std::move(bytes);cached=true;}}
        catch(const std::exception&){}
        if(!cached){
            // Each patch must first give, on the original member, exactly what its author tested (the plan's pin).
            for(auto&p:list){auto stock=read_range(*a.sources.at(0),p.stockOffset,p.stockLength);PatchResult alone;
                if(p.ssrt){std::vector<const PatchInput<SsrtPatch>*>one;PatchInput<SsrtPatch>in{p.owner,p.optional,*p.ssrt};one.push_back(&in);alone=ssrt_apply(stock,one);}
                else{std::vector<const PatchInput<ScenePatch>*>one;PatchInput<ScenePatch>in{p.owner,p.optional,*p.scene};one.push_back(&in);alone=scene_apply(stock,one,0);}
                if((p.ssrt?sha_text(alone.data):alone.data)!=p.expectedOnStock)throw MemberPatchMismatch(p.owner,"Member "+virtual_key_text(key)+": the patch does not give the result its plan names on the original member");}
            PatchResult r;
            try{
                if(scene){std::vector<PatchInput<ScenePatch>>in;for(auto&p:list)in.push_back({p.owner,p.optional,*p.scene});
                    r=compose_patches<ScenePatch>(in,[&](const std::vector<const PatchInput<ScenePatch>*>&use){return scene_apply(base,use,kScenePackLevel);});}
                else{std::vector<PatchInput<SsrtPatch>>in;for(auto&p:list)in.push_back({p.owner,p.optional,*p.ssrt});
                    r=compose_patches<SsrtPatch>(in,[&](const std::vector<const PatchInput<SsrtPatch>*>&use){return ssrt_apply(base,use);});}
            }catch(const std::runtime_error&e){
                std::string what=e.what();if(what.rfind("Two mods change the same entry",0)!=0)throw;
                // compose_patches names the later mod first: "(<later> and <earlier>: ...)"
                auto open=what.find('(');auto sep=what.find(" and ",open);throw MemberConflict(open==std::string::npos||sep==std::string::npos?std::string():what.substr(open+1,sep-open-1),
                    "Two mods change the same entry of archive member "+virtual_key_text(key)+what.substr(30));}
            data=std::move(r.data);notes=std::move(r.notes);
            mkdirs(modsRoot,dir);std::string j="{\"sha256\": \""+sha_text(data)+"\", \"notes\": [";
            for(size_t i=0;i<notes.size();++i)j+=(i?", ":"")+json_quote(notes[i]);j+="]}\n";
            auto stamp=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
            for(auto&[target,bytes]:{std::make_pair(file,std::cref(data)),std::make_pair(info,std::cref(j))}){auto temp=target+stamp;no_reparse(modsRoot,temp);
                try{{Handle out(CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));require(out.h!=INVALID_HANDLE_VALUE,"Cannot create the member cache");
                    if(!bytes.empty())write(out.h,bytes.data(),(DWORD)bytes.size());require(FlushFileBuffers(out.h)!=0,"Member cache flush failed");}
                    atomic_publish(temp,target);}catch(...){DeleteFileW(temp.c_str());throw;}}
            require(text_file(file,256u*1024*1024)==data,"Member cache did not read back");}
        // Served from the cache file (read-only, writers denied while the game runs).
        auto source=std::make_shared<VirtualSource>(file);require(size(source->sync.h)==data.size()&&hash_range(source->sync.h,0,data.size())==sha_text(data),"Member cache changed while opening");
        a.sources.push_back(source);m={(uint32_t)a.sources.size()-1,0,(uint32_t)data.size()};
        std::string who;for(auto&p:list)who+=(who.empty()?"":", ")+p.owner;
        log("member "+virtual_key_text(key)+": patches of "+who+" applied"+(cached?" (result reused)":""));
        for(auto&n:notes)log("member "+virtual_key_text(key)+": "+n);
    }
    archive_layout(a,members);a.patches.clear();
}
// Removes cache entries this start did not use and that are older than a week: only folders named like a SHA-256 holding
// nothing but member.bin and info.json. Anything else is left alone.
inline unsigned prune_member_cache(const std::wstring&modsRoot,const std::wstring&cacheRoot,const std::set<std::wstring>&used){
    unsigned removed=0;WIN32_FIND_DATAW d;HANDLE f=FindFirstFileW((cacheRoot+L"\\*").c_str(),&d);if(f==INVALID_HANDLE_VALUE)return 0;
    FILETIME now;GetSystemTimeAsFileTime(&now);auto ticks=[](const FILETIME&t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
    do{std::wstring n=d.cFileName;
        if(n.size()!=64||!std::all_of(n.begin(),n.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');})||used.count(fold(n)))continue;
        if(!(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))continue;
        if(ticks(now)-ticks(d.ftLastWriteTime)<7ull*24*3600*10000000ull)continue;
        try{auto dir=child(cacheRoot,n);no_reparse(modsRoot,dir);bool other=false;WIN32_FIND_DATAW e;HANDLE g=FindFirstFileW((dir+L"\\*").c_str(),&e);
            if(g!=INVALID_HANDLE_VALUE){do{std::wstring x=e.cFileName;if(x!=L"."&&x!=L".."&&x!=L"member.bin"&&x!=L"info.json")other=true;}while(FindNextFileW(g,&e));FindClose(g);}
            if(other)continue;DeleteFileW((dir+L"\\member.bin").c_str());DeleteFileW((dir+L"\\info.json").c_str());if(RemoveDirectoryW(dir.c_str()))++removed;}
        catch(...){}
    }while(FindNextFileW(f,&d));FindClose(f);return removed;
}
}
