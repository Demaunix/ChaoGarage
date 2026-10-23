// chaogarage-members (not "...patch": Windows asks for administrator rights for 32-bit programs with "patch" in the name): makes, applies and compares member patches with the loader's own engine (src\host\*_patch.h), so mod build
// scripts, the manager and the tests get exactly the bytes the loader builds at game start. Reads and writes only the files named.
//   chaogarage-members extract <archive.xpac> <key> <out>
//   chaogarage-members make ssrt|scene <base> <modified> <patch-out> [--if-absent]
//   chaogarage-members apply ssrt|scene <base> <out> [--level N] [?]<patch> ...      (a leading ? marks a patch optional)
//   chaogarage-members same ssrt|scene <a> <b>
#include "ssrt_patch.h"
#include "scene_patch.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
using namespace ssasr;

static std::string load(const std::string&p){std::ifstream f(p,std::ios::binary);require((bool)f,("Cannot read "+p).c_str());return std::string(std::istreambuf_iterator<char>(f),{});}
static void save(const std::string&p,const std::string&d){auto parent=std::filesystem::path(p).parent_path();if(!parent.empty())std::filesystem::create_directories(parent);std::ofstream f(p,std::ios::binary|std::ios::trunc);require((bool)f,("Cannot write "+p).c_str());f.write(d.data(),(std::streamsize)d.size());require((bool)f,("Cannot write "+p).c_str());}
static std::string folder(const std::string&p){auto s=p.find_last_of("\\/");return s==std::string::npos?std::string("."):p.substr(0,s);}

static std::string extract(const std::string&archive,uint32_t key){
    auto d=load(archive);require(d.size()>=24,"Not an archive");uint32_t count=patch_u32(d,12);
    for(uint32_t i=0;i<count;++i){size_t r=24+20ull*i;if(patch_u32(d,r)!=key)continue;uint32_t off=patch_u32(d,r+4),stored=patch_u32(d,r+12);
        require(uint64_t(off)+stored<=d.size(),"Member outside the archive");return d.substr(off,stored);}
    throw std::runtime_error("No such member");
}

int main(int argc,char**argv){
    try{
        std::vector<std::string>a(argv+1,argv+argc);require(!a.empty(),"Usage: see the comment at the top of patch_tool.cpp");
        if(a[0]=="extract"&&a.size()==4){save(a[3],extract(a[1],(uint32_t)std::stoul(a[2],nullptr,16)));return 0;}
        require(a.size()>=4&&(a[1]=="ssrt"||a[1]=="scene"),"Usage: see the comment at the top of patch_tool.cpp");bool ssrt=a[1]=="ssrt";
        if(a[0]=="same"&&a.size()==4){bool same=ssrt?ssrt_same_content(load(a[2]),load(a[3])):scene_same_content(load(a[2]),load(a[3]));std::puts(same?"same":"different");return same?0:3;}
        if(a[0]=="make"&&(a.size()==5||(a.size()==6&&a[5]=="--if-absent"))){bool ifAbsent=a.size()==6;
            if(ssrt)save(a[4],ssrt_make(load(a[2]),load(a[3]),ifAbsent));
            else{auto made=scene_make(load(a[2]),load(a[3]),ifAbsent);auto dir=folder(a[4]);for(auto&[name,data]:made.files)save(dir+"\\"+name,data);save(a[4],made.json);}
            return 0;}
        if(a[0]=="apply"&&a.size()>=5){int level=6;size_t i=4;if(a[i]=="--level"){require(a.size()>i+2,"--level needs a number and a patch");level=std::stoi(a[i+1]);i+=2;}
            PatchResult r;
            if(ssrt){std::vector<PatchInput<SsrtPatch>>in;
                for(;i<a.size();++i){bool opt=a[i][0]=='?';auto p=opt?a[i].substr(1):a[i];in.push_back({p,opt,ssrt_patch_read(JsonParser(load(p)).parse())});}
                r=compose_patches<SsrtPatch>(in,[&](const std::vector<const PatchInput<SsrtPatch>*>&use){return ssrt_apply(load(a[2]),use);});}
            else{std::vector<PatchInput<ScenePatch>>in;
                for(;i<a.size();++i){bool opt=a[i][0]=='?';auto p=opt?a[i].substr(1):a[i];auto dir=folder(p);
                    in.push_back({p,opt,scene_patch_read(JsonParser(load(p)).parse(),[&](const std::string&name){return load(dir+"\\"+name);})});}
                auto base=load(a[2]);r=compose_patches<ScenePatch>(in,[&](const std::vector<const PatchInput<ScenePatch>*>&use){return scene_apply(base,use,level);});}
            for(auto&n:r.notes)std::printf("note: %s\n",n.c_str());save(a[3],r.data);return 0;}
        throw std::runtime_error("Usage: see the comment at the top of patch_tool.cpp");
    }catch(const std::exception&e){std::fprintf(stderr,"chaogarage-members: %s\n",e.what());return 2;}
}
