#pragma once
// Small synthetic SSRT tables and menu scenes for the 1.3.0 member patch tests (test_members.cpp, run_e2e.cpp). No game data.
#include "ssrt_patch.h"
#include "scene_patch.h"
#include "rtsc.h"
#include <cstdio>
namespace fixture {
using namespace ssasr;
inline std::string u32(uint32_t v){return std::string((const char*)&v,4);}
inline std::string area(const std::string&name){std::string a(64,'\0');memcpy(a.data(),name.data(),name.size());return a;}

struct TRow {std::string name;std::vector<SsrtField>fields;};
struct TTable {std::string name;std::vector<TRow>rows;};
constexpr uint32_t T_INT=0xFBE46712,NAME=0x8D0E5F60,COST=0x11111111;
inline SsrtField num(uint32_t h,uint32_t v){SsrtField f;f.hash=h;f.type=T_INT;f.bits=v;return f;}
inline SsrtField txt(uint32_t h,const std::string&s){SsrtField f;f.hash=h;f.type=kSsrtText;f.text=s;return f;}
inline TRow row(const std::string&name,uint32_t cost){return {name,{txt(NAME,name),num(COST,cost)}};}
// header, then each table (header + row offsets), then rows, then texts; offsets are absolute
inline std::string table_file(const std::vector<TTable>&tables){
    std::string tableBlocks,rowBlocks,texts;size_t headerSize=8+4*tables.size(),tablesSize=0,rowsSize=0;
    for(auto&t:tables){tablesSize+=72+4*t.rows.size();for(auto&r:t.rows)rowsSize+=72+12*r.fields.size();}
    size_t rowAt=headerSize+tablesSize,textAt=rowAt+rowsSize;std::string head="SSRT"+u32((uint32_t)tables.size());size_t tableAt=headerSize;
    for(auto&t:tables){head+=u32((uint32_t)tableAt);std::string tb=u32(0)+area(t.name)+u32((uint32_t)t.rows.size());
        for(auto&r:t.rows){tb+=u32((uint32_t)rowAt);std::string rb=u32((uint32_t)r.fields.size())+u32(0)+area(r.name);
            for(auto&f:r.fields){uint32_t v=f.bits;if(f.type==kSsrtText){v=(uint32_t)(textAt+texts.size());texts+=f.text+'\0';}rb+=u32(f.hash)+u32(f.type)+u32(v);}
            rowBlocks+=rb;rowAt+=rb.size();}
        tableBlocks+=tb;tableAt+=tb.size();}
    return head+tableBlocks+rowBlocks+texts;
}
// An ssrt-patch/1 adding one row {Name, Cost} to Sheet1.
inline std::string addRow(const char*mode,const char*name,uint32_t cost){char b[512];
    sprintf_s(b,R"({"format":"ssrt-patch/1","rows":[{"mode":"%s","table":"Sheet1","row":"%s","word4":"00000000","fields":[{"hash":"8D0E5F60","type":"B7459825","text":"%s"},{"hash":"11111111","type":"FBE46712","bits":"%08X"}]}]})",mode,name,name,cost);return b;}

// A shader cache record (28-byte key starting with `fill`, no bindings, an 8-byte bytecode body tagged `body`) and a cache file.
inline std::string shader_record(char fill,uint8_t body){std::string r(28,fill);r+=u32(0)+u32(8)+u32(0xFFFE0000u|body)+u32(0x0000FFFFu);return r;}
inline std::string shader_file(const std::vector<std::string>&records){ShaderRecords m;for(auto&r:records)m[r.substr(0,28)]=r;return shader_bytes(m);}
inline std::string dds(char fill,size_t n=192){return "DDS "+std::string(124,'\0')+std::string(n-128,fill);}
inline std::string block(const char*tag,const std::string&body){std::string b=std::string(tag,4)+std::string(12,'\0')+body;return block_finish(b);}
inline std::string rect(float a){std::string r;for(int i=0;i<8;++i){float v=a+i;r.append((const char*)&v,4);}return r;}
// A scene with one picture list, one text list and (optionally) a FONT block, packed at zlib level 1.
inline std::string scene_file(const TextEntries&texts,const std::vector<std::pair<uint32_t,uint32_t>>&pictures,const std::vector<std::string>&images,bool font=true){
    Scene sc;ScenePictures p;p.images=images;for(auto&[k,img]:pictures)p.pictures[k]={rect((float)k),img};
    std::string ptemplate=block("PTEX",std::string(12,'\0')),ttemplate=block("TEXT",std::string(12,'\0')),rtemplate=std::string(20,'\0');
    auto made=ptex_make(ptemplate,p);std::vector<uint32_t>pos{8};for(size_t j=0;j<images.size();++j)pos.push_back(made.second+8*(uint32_t)j);
    sc.chunks[1].push_back({made.first,relo_make(rtemplate,"PTEX",pos)});
    std::vector<uint32_t>tp;for(size_t j=0;j<texts.size();++j)tp.push_back(16+8*(uint32_t)j);sc.chunks[1].push_back({text_make(ttemplate,texts),relo_make(rtemplate,"TEXT",tp)});
    if(font)sc.chunks[1].push_back({block("FONT","glyphs-1"),relo_make(rtemplate,"FONT",{})});
    return scene_pack(sc,1);
}
}
