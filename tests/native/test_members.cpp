// 1.3.0 member patches: the rules of ssrt-patch/1 and scene-patch/1 on small synthetic tables and scenes (no game data).
// The real members (Shopping.dat, the 40 shared scenes) are checked by tests\real\real_members.py.
#include "fixture_members.h"
#include <iostream>
using namespace ssasr;
using namespace fixture;
unsigned checks=0;
void check(bool value,const std::string&message){++checks;require(value,message);}
template<class F> bool throwsClash(F f){try{f();}catch(const PatchClash&){return true;}catch(...){return false;}return false;}
template<class F> bool throwsError(F f){try{f();}catch(const PatchClash&){return false;}catch(const std::exception&){return true;}return false;}
template<class F> std::string errorText(F f){try{f();}catch(const std::exception&e){return e.what();}return "";}
std::vector<PatchInput<SsrtPatch>> ssrt_inputs(std::initializer_list<std::tuple<const char*,bool,std::string>>list){
    std::vector<PatchInput<SsrtPatch>>r;for(auto&[who,opt,json]:list)r.push_back({who,opt,ssrt_patch_read(JsonParser(json).parse())});return r;}
PatchResult ssrt_run(const std::string&base,const std::vector<PatchInput<SsrtPatch>>&in){
    return compose_patches<SsrtPatch>(in,[&](const std::vector<const PatchInput<SsrtPatch>*>&use){return ssrt_apply(base,use);});}
const SsrtRow* find_row(const std::vector<SsrtTable>&t,const std::string&name,size_t nth=1){for(auto&r:t[0].rows)if(r.name==name&&!--nth)return &r;return nullptr;}
uint32_t cost_of(const SsrtRow*r){for(auto&f:r->fields)if(f.hash==COST)return f.bits;return 0;}

void ssrt_tests(){
    auto base=table_file({{"Sheet1",{row("Item1",10),row("Item2",20),row("Item2",21)}},{"Other",{row("X",1)}}});
    auto t0=ssrt_read(base);check(t0.size()==2&&t0[0].rows.size()==3,"synthetic table reads back");
    // an added row is appended; every original byte except the table offset stays
    auto r=ssrt_run(base,ssrt_inputs({{"egg",false,addRow("add","Item98",0)}}));auto t=ssrt_read(r.data);
    check(t[0].rows.size()==4&&t[0].rows[3].name=="Item98"&&t[1].rows.size()==1,"row added at the end of its table only");
    bool kept=true;for(size_t i=0;i<base.size();++i)if((i<8||i>=12)&&base[i]!=r.data[i])kept=false;check(kept,"original bytes unchanged except the table offset");
    // the same row from two mods: once; different content: refused unless one side is optional
    r=ssrt_run(base,ssrt_inputs({{"a",false,addRow("add","Item98",5)},{"b",false,addRow("add","Item98",5)}}));
    check(ssrt_read(r.data)[0].rows.size()==4&&r.notes.size()==1,"identical row from two mods added once");
    auto clash=[&]{ssrt_run(base,ssrt_inputs({{"a",false,addRow("add","Item98",5)},{"b",false,addRow("add","Item98",6)}}));};
    check(throwsError(clash)&&errorText(clash).find("a and")==std::string::npos&&errorText(clash).find("b and a")!=std::string::npos,"different rows refused, naming both mods");
    r=ssrt_run(base,ssrt_inputs({{"a",false,addRow("add","Item98",5)},{"b",true,addRow("add","Item98",6)}}));
    check(cost_of(find_row(ssrt_read(r.data),"Item98"))==5&&r.skipped==std::vector<int>{1},"later optional patch gives way");
    r=ssrt_run(base,ssrt_inputs({{"a",true,addRow("add","Item98",5)},{"b",false,addRow("add","Item98",6)}}));
    check(cost_of(find_row(ssrt_read(r.data),"Item98"))==6&&r.skipped==std::vector<int>{0},"earlier optional patch gives way");
    // add_if_absent against a plain add: the add wins in either order
    auto x=ssrt_run(base,ssrt_inputs({{"a",false,addRow("add_if_absent","Item98",5)},{"b",false,addRow("add","Item98",6)}}));
    auto y=ssrt_run(base,ssrt_inputs({{"b",false,addRow("add","Item98",6)},{"a",false,addRow("add_if_absent","Item98",5)}}));
    check(ssrt_same_content(x.data,y.data)&&cost_of(find_row(ssrt_read(x.data),"Item98"))==6,"if-absent row gives way to an add in either order");
    // a row the member already has
    check(throwsError([&]{ssrt_run(base,ssrt_inputs({{"a",false,addRow("add","Item1",99)}}));}),"add over a different member row refused");
    r=ssrt_run(base,ssrt_inputs({{"a",false,addRow("add_if_absent","Item1",99)}}));check(cost_of(find_row(ssrt_read(r.data),"Item1"))==10,"if-absent leaves the member row");
    r=ssrt_run(base,ssrt_inputs({{"a",false,addRow("add","Item2",21)}}));check(ssrt_read(r.data)[0].rows.size()==3,"row equal to a repeated member row is already there");
    // changed fields
    auto set=[](const char*rowName,const char*extra,uint32_t v){char b[512];sprintf_s(b,R"({"format":"ssrt-patch/1","set_fields":[{"table":"Sheet1","row":"%s"%s,"hash":"11111111","type":"FBE46712","bits":"%08X"}]})",rowName,extra,v);return std::string(b);};
    r=ssrt_run(base,ssrt_inputs({{"a",false,set("Item1",R"(,"expect_bits":"0000000A")",11)}}));check(cost_of(find_row(ssrt_read(r.data),"Item1"))==11&&r.data.size()==base.size(),"field changed in place");
    check(throwsError([&]{ssrt_run(base,ssrt_inputs({{"a",false,set("Item1",R"(,"expect_bits":"0000000B")",11)}}));}),"expected value mismatch refused");
    check(throwsError([&]{ssrt_run(base,ssrt_inputs({{"a",false,set("Item1","",11)},{"b",false,set("Item1","",12)}}));}),"two different values refused");
    r=ssrt_run(base,ssrt_inputs({{"a",false,set("Item1","",11)},{"b",false,set("Item1","",11)}}));check(r.notes.size()==1,"the same value twice is fine");
    check(throwsError([&]{ssrt_run(base,ssrt_inputs({{"a",false,set("Item2","",5)}}));}),"repeated row name needs an occurrence");
    r=ssrt_run(base,ssrt_inputs({{"a",false,set("Item2",R"(,"occurrence":2)",5)}}));
    check(cost_of(find_row(ssrt_read(r.data),"Item2",1))==20&&cost_of(find_row(ssrt_read(r.data),"Item2",2))==5,"occurrence 2 changes the second row");
    check(throwsError([&]{ssrt_run(base,ssrt_inputs({{"a",false,set("Item2",R"(,"occurrence":3)",5)}}));}),"occurrence past the rows refused");
    r=ssrt_run(base,ssrt_inputs({{"a",false,addRow("add","Item98",1)},{"b",false,set("Item98","",2)}}));check(cost_of(find_row(ssrt_read(r.data),"Item98"))==2,"field of a row an earlier patch adds");
    check(throwsError([&]{ssrt_run(base,ssrt_inputs({{"a",false,set("Nope","",2)}}));}),"missing row refused");
    check(throwsError([&]{ssrt_run(base,ssrt_inputs({{"a",false,R"({"format":"ssrt-patch/1","rows":[{"mode":"add","table":"Nope","row":"R","word4":"00000000","fields":[]}]})"}}));}),"missing table refused");
    r=ssrt_run(base,ssrt_inputs({{"a",false,R"({"format":"ssrt-patch/1","set_fields":[{"table":"Sheet1","row":"Item1","hash":"8D0E5F60","type":"B7459825","text":"Renamed","expect_text":"Item1"}]})"}}));
    check(find_row(ssrt_read(r.data),"Item1")->fields[0].text=="Renamed","text field changed (new text appended)");
    // reading: wrong types and bad input are errors, never clashes
    check(throwsError([&]{ssrt_patch_read(JsonParser(R"({"format":"ssrt-patch/1","rows":[{"mode":"add","table":"Sheet1","row":"R","word4":"00000000","fields":[{"hash":"8D0E5F60","type":"B7459825","bits":"00000001"}]}]})").parse());}),"text field given bits refused");
    check(throwsError([&]{ssrt_patch_read(JsonParser(R"({"format":"ssrt-patch/2"})").parse());}),"unknown format refused");
    check(throwsError([&]{ssrt_patch_read(JsonParser(R"({"format":"ssrt-patch/1","rows":[{"mode":"replace","table":"Sheet1","row":"R","word4":"00000000","fields":[]}]})").parse());}),"unknown mode refused");
    check(throwsError([&]{ssrt_run("NOPE"+base.substr(4),ssrt_inputs({{"a",false,addRow("add","Item98",0)}}));}),"a member that is no table refused");
    check(throwsError([&]{ssrt_read(base.substr(0,base.size()-30));}),"truncated table refused");
    // make, then apply: the same rows
    auto modified=ssrt_run(base,ssrt_inputs({{"a",false,addRow("add","Item98",3)},{"b",false,set("Item2",R"(,"occurrence":2)",9)}})).data;
    auto made=ssrt_make(base,modified,false);r=ssrt_run(base,ssrt_inputs({{"made",false,made}}));
    check(ssrt_same_content(r.data,modified)&&made.find("\"occurrence\": 2")!=std::string::npos,"make then apply gives the modified table");
}

// --- scenes --------------------------------------------------------------------------------------------------------
std::vector<PatchInput<ScenePatch>> scene_inputs(std::initializer_list<std::tuple<const char*,bool,std::string>>list,const std::map<std::string,std::string>&files){
    std::vector<PatchInput<ScenePatch>>r;
    for(auto&[who,opt,json]:list)r.push_back({who,opt,scene_patch_read(JsonParser(json).parse(),[&](const std::string&n){auto f=files.find(n);require(f!=files.end(),"no such fixture file");return f->second;})});
    return r;}
PatchResult scene_run(const std::string&base,const std::vector<PatchInput<ScenePatch>>&in){
    return compose_patches<ScenePatch>(in,[&](const std::vector<const PatchInput<ScenePatch>*>&use){return scene_apply(base,use,1);});}
ScenePictures pictures_of(const std::string&s){auto sc=scene_unpack(s);auto at=scene_find(sc,"PTEX");ptex_check(sc.chunks[at.first][at.second]);return ptex_read(sc.chunks[at.first][at.second].data);}
TextEntries texts_of(const std::string&s){auto sc=scene_unpack(s);auto at=scene_find(sc,"TEXT");text_check(sc.chunks[at.first][at.second]);return text_read(sc.chunks[at.first][at.second].data);}
std::string words_of(const std::string&s,uint32_t key){for(auto&[k,w]:texts_of(s))if(k==key)return w;return "<none>";}

void scene_tests(){
    auto img0=dds('a'),img1=dds('b'),egg=dds('e'),steam=dds('s');
    auto base=scene_file({{0x10,"TEN"},{0x20,"INFO"},{0x20,"INFO"},{0x30,"X"},{0x30,"Y"}},{{0x100,0},{0x200,1}},{img0,img1});
    check(scene_same_content(base,base)&&pictures_of(base).images.size()==2,"synthetic scene reads back");
    std::map<std::string,std::string>files{{"egg.dds",egg},{"steam.dds",steam},{"font.bin",block("FONT","glyphs-2")}};
    auto shaEgg=scene_sha(egg),shaSteam=scene_sha(steam),sha1=scene_sha(img1);
    auto addPic=[&](const char*mode,uint32_t key,const std::string&imageSha,bool withImage){char b[1024];
        sprintf_s(b,R"({"format":"scene-patch/1",%s"pictures":[{"mode":"%s","key":"%08X","rect_hex":"%s","image_sha256":"%s"}]})",
            withImage?(R"("images":[{"file":"egg.dds","sha256":")"+shaEgg+R"("}],)").c_str():"",mode,key,patch_bytes_hex(rect((float)key)).c_str(),imageSha.c_str());return std::string(b);};
    // a picture with its image
    auto r=scene_run(base,scene_inputs({{"egg",false,addPic("add",0x150,shaEgg,true)}},files));auto p=pictures_of(r.data);
    check(p.pictures.size()==3&&p.images.size()==3&&p.images[p.pictures.at(0x150).image]==egg&&p.images[0]==img0,"picture and image added, member images kept in place");
    // a picture key the member has with another picture: refused; if-absent: left; the same: already there
    check(throwsError([&]{scene_run(base,scene_inputs({{"egg",false,addPic("add",0x100,shaEgg,true)}},files));}),"different picture under a member key refused");
    r=scene_run(base,scene_inputs({{"egg",false,addPic("add_if_absent",0x100,shaEgg,true)}},files));check(r.data==base,"if-absent picture leaves the scene unchanged (no image appended)");
    // two mods: if-absent and add, both orders give the same scene
    auto x=scene_run(base,scene_inputs({{"egg",false,addPic("add_if_absent",0x150,shaEgg,true)},{"metal",false,addPic("add",0x150,sha1,false)}},files));
    auto y=scene_run(base,scene_inputs({{"metal",false,addPic("add",0x150,sha1,false)},{"egg",false,addPic("add_if_absent",0x150,shaEgg,true)}},files));
    check(scene_same_content(x.data,y.data)&&pictures_of(x.data).images[pictures_of(x.data).pictures.at(0x150).image]==img1,"if-absent picture gives way to an add in either order");
    check(throwsError([&]{scene_run(base,scene_inputs({{"egg",false,addPic("add",0x150,shaEgg,true)},{"metal",false,addPic("add",0x150,sha1,false)}},files));}),"two different pictures under one key refused");
    check(throwsError([&]{scene_run(base,scene_inputs({{"egg",false,addPic("add",0x150,shaSteam,false)}},files));}),"picture with an unknown image refused");
    // repainting an image found by content; a picture cut from the repainted image
    char rb[1024];sprintf_s(rb,R"({"format":"scene-patch/1","replace_images":[{"old_sha256":"%s","file":"steam.dds","sha256":"%s"}],"pictures":[{"mode":"add","key":"00000160","rect_hex":"%s","image_sha256":"%s"}]})",
        sha1.c_str(),shaSteam.c_str(),patch_bytes_hex(rect(1)).c_str(),shaSteam.c_str());std::string repaint=rb;
    r=scene_run(base,scene_inputs({{"steam",false,repaint}},files));p=pictures_of(r.data);
    check(p.images.size()==2&&p.images[1]==steam&&p.pictures.at(0x200).image==1&&p.pictures.at(0x160).image==1,"image repainted in place; new picture uses it");
    sprintf_s(rb,R"({"format":"scene-patch/1","replace_images":[{"old_sha256":"%s","file":"egg.dds","sha256":"%s"}]})",sha1.c_str(),shaEgg.c_str());std::string repaint2=rb;
    check(throwsError([&]{scene_run(base,scene_inputs({{"steam",false,repaint},{"other",false,repaint2}},files));}),"one image repainted differently by two mods refused");
    r=scene_run(base,scene_inputs({{"steam",false,repaint},{"other",true,repaint2}},files));check(r.skipped==std::vector<int>{1}&&pictures_of(r.data).images[1]==steam,"optional repaint gives way");
    sprintf_s(rb,R"({"format":"scene-patch/1","replace_images":[{"old_sha256":"%s","file":"egg.dds","sha256":"%s"}]})",shaSteam.c_str(),shaEgg.c_str());
    check(throwsError([&]{scene_run(base,scene_inputs({{"x",false,rb}},files));}),"repaint of an image that is not there refused");
    // texts: add, set with expect, repeated keys
    auto text=[](const char*mode,uint32_t key,const char*words,const char*expect){char b[512];
        sprintf_s(b,R"({"format":"scene-patch/1","texts":[{"mode":"%s","key":"%08X","text":"%s"%s%s%s}]})",mode,key,words,expect?R"(,"expect_text":")":"",expect?expect:"",expect?"\"":"");return std::string(b);};
    r=scene_run(base,scene_inputs({{"egg",false,text("add",0x15,"EGG HANGAR",nullptr)}},files));check(words_of(r.data,0x15)=="EGG HANGAR"&&texts_of(r.data).size()==6,"text added");
    auto tx=texts_of(r.data);check(std::is_sorted(tx.begin(),tx.end()),"texts kept sorted");
    r=scene_run(base,scene_inputs({{"steam",false,text("set",0x10,"UNMUTE","TEN")}},files));check(words_of(r.data,0x10)=="UNMUTE","text set");
    check(throwsError([&]{scene_run(base,scene_inputs({{"steam",false,text("set",0x10,"UNMUTE","NOTHING")}},files));}),"expected words mismatch refused");
    r=scene_run(base,scene_inputs({{"steam",false,text("set",0x20,"DETAILS","INFO")}},files));
    {int n=0;for(auto&[k,w]:texts_of(r.data))if(k==0x20){++n;check(w=="DETAILS","repeated key: every copy set");}check(n==2,"repeated key stays repeated");}
    check(throwsError([&]{scene_run(base,scene_inputs({{"x",false,text("set",0x30,"Z",nullptr)}},files));}),"key repeated with different words refused");
    check(throwsError([&]{scene_run(base,scene_inputs({{"a",false,text("add",0x15,"ONE",nullptr)},{"b",false,text("add",0x15,"TWO",nullptr)}},files));}),"two different texts under one key refused");
    r=scene_run(base,scene_inputs({{"a",false,text("add_if_absent",0x10,"ONE",nullptr)}},files));check(r.data==base,"if-absent text leaves the member text");
    check(throwsError([&]{scene_run(base,scene_inputs({{"a",false,text("set",0x99,"ONE",nullptr)}},files));}),"set of a missing text refused");
    // whole blocks, found by content
    auto fontSha=scene_sha(block("FONT","glyphs-1"));auto blockPatch=[&](const std::string&old){return R"({"format":"scene-patch/1","blocks":[{"tag":"FONT","old_sha256":")"+old+R"(","file":"font.bin","sha256":")"+scene_sha(files["font.bin"])+"\"}]}";};
    r=scene_run(base,scene_inputs({{"steam",false,blockPatch(fontSha)}},files));auto sc=scene_unpack(r.data);check(sc.chunks[1][2].data==files["font.bin"],"block replaced");
    check(throwsError([&]{scene_run(base,scene_inputs({{"steam",false,blockPatch(shaEgg)}},files));}),"block that is not there refused");
    // reading: a file whose hash differs, a non-DDS image
    check(throwsError([&]{scene_patch_read(JsonParser(R"({"format":"scene-patch/1","images":[{"file":"egg.dds","sha256":")"+shaSteam+"\"}]}").parse(),[&](const std::string&n){return files[n];});}),"patch file hash mismatch refused");
    files["bad.dds"]=std::string(200,'x');
    check(throwsError([&]{scene_patch_read(JsonParser(R"({"format":"scene-patch/1","images":[{"file":"bad.dds","sha256":")"+scene_sha(files["bad.dds"])+"\"}]}").parse(),[&](const std::string&n){return files[n];});}),"non-DDS image refused");
    // damaged scenes and unexpected shapes are errors, never clashes
    auto damaged=base;damaged[damaged.size()-40]^=0x55;check(throwsError([&]{scene_run(damaged,scene_inputs({{"egg",false,addPic("add",0x150,shaEgg,true)}},files));})||scene_same_content(damaged,base),"damaged scene refused (or damage outside the data)");
    auto noText=scene_file({},{{0x100,0}},{img0});
    check(throwsClash([&]{std::vector<const PatchInput<ScenePatch>*>use;auto in=scene_inputs({{"egg",false,text("add",0x15,"EGG",nullptr)}},files);use.push_back(&in[0]);
        auto s=scene_unpack(noText);s.chunks[1].erase(s.chunks[1].begin()+1);scene_apply(scene_pack(s,1),use,1);}),"a scene without a text list: the patch cannot apply");
    // make, then apply: the same scene, and the same bytes at the same level
    auto modified=scene_run(base,scene_inputs({{"egg",false,addPic("add",0x150,shaEgg,true)},{"steam",false,repaint},{"t",false,text("add",0x15,"EGG",nullptr)},{"f",false,blockPatch(fontSha)}},files)).data;
    auto made=scene_make(base,modified,false);auto all=files;for(auto&[n,d]:made.files)all[n]=d;
    r=scene_run(base,scene_inputs({{"made",false,made.json}},all));check(r.data==modified,"make then apply gives the modified scene, byte for byte");
    check(scene_digest(r.data)==scene_digest(modified)&&scene_digest(r.data)!=scene_digest(base),"digest follows the content");
    sc=scene_unpack(modified);auto repacked=scene_pack(sc,9);check(repacked!=modified&&scene_digest(repacked)==scene_digest(modified),"digest ignores the packing level");
}

int main(){try{
    auto z=zlib_pack(std::string(100000,'q'),9);check(zlib_unpack((const unsigned char*)z.data(),z.size(),100000)==std::string(100000,'q'),"zlib round trip");
    check(throwsError([&]{zlib_unpack((const unsigned char*)z.data(),z.size(),99999);}),"zlib size mismatch refused");
    check(throwsError([&]{zlib_unpack((const unsigned char*)z.data(),z.size()-3,100000);}),"truncated zlib refused");
    ssrt_tests();scene_tests();
    std::cout<<"PASS "<<checks<<" member patch checks (ssrt-patch/1, scene-patch/1, zlib)\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<"\n";return 1;}}
