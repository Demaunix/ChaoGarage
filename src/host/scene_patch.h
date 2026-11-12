#pragma once
// 1.3.0: "scene-patch/1" — pictures, images, texts and whole blocks added to or changed in the game's menu scenes (.stz).
// A scene: 18 words {offset, unpacked size, packed size} for six zlib-packed sections. Sections 0 and 3 hold blocks ("chunks":
// tag, padded size, used size, data from +16); sections 1 and 4 hold one fix-up block per block, in the same order (+16: the tag
// it belongs to, then {1, position} pairs and {0, 0}); positions are offsets from +16 of the block that hold an offset from +16.
// PTEX (pictures): +16 count, +20 image count, +24 image table offset, +28 pictures of 40 bytes {key, 8 floats, image index};
// the table lists {offset, size} of DDS images. TEXT: +24 count, +28 entries {key, offset of zero-terminated words}.
// Pictures and texts are kept sorted by key and rebuilt exactly as the mods' own build scripts did (sorted picture and
// text lists, fix-up lists, packing), which made the scenes tested in the game; packing uses zlib at the level asked for.
#include "member_patch.h"
#include "zlib_codec.h"
#include <array>
namespace ssasr {
inline std::string scene_sha(const std::string&s){Hash h;if(!s.empty())h.add(s.data(),(DWORD)s.size());return h.finish();}
struct SceneChunk {std::string data,relo;};
struct Scene {std::array<std::string,6>sections;std::vector<SceneChunk>chunks[2];};   // chunks[0]: section 0 (fix-ups 1), chunks[1]: section 3 (fix-ups 4)
inline std::vector<std::string> scene_blocks(const std::string&s){std::vector<std::string>r;size_t p=0;
    while(p<s.size()){uint32_t n=patch_u32(s,p+4);require(n>=16&&n%4==0&&n<=s.size()-p,"Scene block has an invalid size");r.push_back(s.substr(p,n));p+=n;}return r;}
inline Scene scene_unpack(const std::string&d){
    require(d.size()>=72,"Member is not a scene");Scene sc;
    for(int i=0;i<6;++i){uint32_t off=patch_u32(d,12*i),n=patch_u32(d,12*i+4),c=patch_u32(d,12*i+8);
        if(!c){require(!n&&off<=d.size(),"Scene section header is invalid");continue;}   // the game's own scenes give empty sections a position, rebuilt ones 0
        require(off>=72&&uint64_t(off)+c<=d.size()&&n<=64u*1024*1024,"Scene section outside the member");
        sc.sections[i]=zlib_unpack((const unsigned char*)d.data()+off,c,n);}
    for(int s=0;s<2;++s){auto data=scene_blocks(sc.sections[s*3]),relo=scene_blocks(sc.sections[s*3+1]);
        require(data.size()==relo.size(),"Scene fix-up lists do not match its blocks");
        for(size_t i=0;i<data.size();++i){require(relo[i].size()>=28&&!memcmp(relo[i].data()+16,data[i].data(),4),"Scene fix-up list belongs to another block");sc.chunks[s].push_back({data[i],relo[i]});}}
    return sc;
}
inline std::string scene_pack(const Scene&sc,int level){
    std::array<std::string,6>sections=sc.sections;
    for(int s=0;s<2;++s){std::string data,relo;for(auto&c:sc.chunks[s]){data+=c.data;relo+=c.relo;}sections[s*3]=data;sections[s*3+1]=relo;}
    std::string out(72,'\0');
    for(int i=0;i<6;++i){if(sections[i].empty())continue;auto c=zlib_pack(sections[i],level);out.append((32-out.size()%32)%32,'\0');
        patch_put32(out,12*i,(uint32_t)out.size());patch_put32(out,12*i+4,(uint32_t)sections[i].size());patch_put32(out,12*i+8,(uint32_t)c.size());out+=c;}
    out.append((32-out.size()%32)%32,'\0');return out;
}
// The digest a mod's plan pins for a patched scene: every unpacked section with its size, so packing details never matter.
inline std::string scene_digest(const Scene&sc){std::array<std::string,6>s=sc.sections;
    for(int k=0;k<2;++k){std::string a,b;for(auto&c:sc.chunks[k]){a+=c.data;b+=c.relo;}s[k*3]=a;s[k*3+1]=b;}
    Hash h;for(auto&x:s){uint32_t n=(uint32_t)x.size();h.add(&n,4);if(n)h.add(x.data(),n);}return h.finish();}
inline std::string scene_digest(const std::string&d){return scene_digest(scene_unpack(d));}
inline std::string block_finish(std::string d){uint32_t used=(uint32_t)d.size()-16;d.append((64-d.size()%64)%64,'\0');patch_put32(d,4,(uint32_t)d.size());patch_put32(d,8,used);return d;}
inline std::string relo_make(const std::string&templ,const char*tag,const std::vector<uint32_t>&positions){
    std::string r=templ.substr(0,20);memcpy(r.data()+16,tag,4);for(auto p:positions){uint32_t v[2]={1,p};r.append((const char*)v,8);}r.append(8,'\0');return block_finish(r);}
// Fix-up records of a block: refused unless they are exactly the {1, position} ... {0, 0} form.
inline std::vector<uint32_t> relo_positions(const std::string&r){uint32_t used=patch_u32(r,8);require(used>=12&&used%8==4&&16ull+used<=r.size(),"Scene fix-up list has an unexpected form");
    std::vector<uint32_t>p;for(size_t q=20;q+8<=16ull+used;q+=8){uint32_t t=patch_u32(r,q),v=patch_u32(r,q+4);if(q+8==16ull+used){require(!t&&!v,"Scene fix-up list is not terminated");break;}require(t==1,"Scene fix-up list has an unexpected record");p.push_back(v);}return p;}

struct ScenePicture {std::string rect;uint32_t image=0;};   // rect: the 8 floats as stored (32 bytes)
struct ScenePictures {std::map<uint32_t,ScenePicture>pictures;std::vector<std::string>images;};
inline ScenePictures ptex_read(const std::string&c){
    require(c.size()>=28,"Picture list too short");ScenePictures r;uint32_t n=patch_u32(c,16),nt=patch_u32(c,20),table=patch_u32(c,24);
    require(n<=100000&&nt<=100000&&28ull+40ull*n<=c.size()&&16ull+table+8ull*nt<=c.size(),"Picture list has invalid counts");uint64_t copied=0;
    for(uint32_t j=0;j<nt;++j){uint32_t p=patch_u32(c,16+table+8*j),len=patch_u32(c,16+table+8*j+4);
        require(16ull+p+len<=c.size()&&len>=4&&!memcmp(c.data()+16+p,"DDS ",4),"Picture list image is not a DDS image");copied+=len;require(copied<=c.size(),"Picture list images overlap");r.images.push_back(c.substr(16+p,len));}
    for(uint32_t j=0;j<n;++j){size_t q=28+40ull*j;uint32_t key=patch_u32(c,q),img=patch_u32(c,q+36);require(img<nt,"Picture names an image that is not there");
        require(r.pictures.emplace(key,ScenePicture{c.substr(q+4,32),img}).second,"Picture list names one key twice");}
    // The rebuilt form must be what the game reads: the fix-up list is checked separately (ptex_check).
    return r;
}
inline std::pair<std::string,uint32_t> ptex_make(const std::string&templ,const ScenePictures&p){
    std::string b=templ.substr(0,28);patch_put32(b,16,(uint32_t)p.pictures.size());patch_put32(b,20,(uint32_t)p.images.size());patch_put32(b,24,0);
    for(auto&[key,pic]:p.pictures){b.append((const char*)&key,4);b+=pic.rect;b.append((const char*)&pic.image,4);}
    std::vector<uint32_t>entries;for(auto&img:p.images){b.append((4-b.size()%4)%4,'\0');entries.push_back((uint32_t)b.size()-16);entries.push_back((uint32_t)img.size());b+=img;}
    b.append((4-b.size()%4)%4,'\0');uint32_t table=(uint32_t)b.size()-16;for(auto e:entries)b.append((const char*)&e,4);patch_put32(b,24,table);
    return {block_finish(b),table};
}
inline void ptex_check(const SceneChunk&c){auto p=relo_positions(c.relo);uint32_t nt=patch_u32(c.data,20),table=patch_u32(c.data,24);
    std::vector<uint32_t>want{8};for(uint32_t j=0;j<nt;++j)want.push_back(table+8*j);require(p==want,"Picture list fix-ups have an unexpected form");}
// Texts as stored, in order. A key may be there more than once (the game's navigator_info scenes have INFO twice).
using TextEntries=std::vector<std::pair<uint32_t,std::string>>;
inline TextEntries text_read(const std::string&c){TextEntries r;uint64_t copied=0;uint32_t n=patch_u32(c,24);require(n<=1000000&&28ull+8ull*n<=c.size(),"Text list has an invalid count");
    for(uint32_t j=0;j<n;++j){uint32_t key=patch_u32(c,28+8*j),p=patch_u32(c,32+8*j);require(16ull+p<c.size(),"Text outside its list");
        auto e=c.find('\0',16+p);require(e!=std::string::npos,"Text is not terminated");copied+=e-16-p;require(copied<=c.size(),"Text list texts overlap");r.push_back({key,c.substr(16+p,e-16-p)});}return r;}
inline TextEntries text_sorted(TextEntries t){std::sort(t.begin(),t.end());return t;}
inline std::string text_make(const std::string&templ,const TextEntries&t){
    std::string b=templ.substr(0,28);patch_put32(b,24,(uint32_t)t.size());b.append(8*t.size(),'\0');size_t j=0;
    for(auto&[key,words]:t){uint32_t p=(uint32_t)b.size()-16;patch_put32(b,28+8*j,key);patch_put32(b,32+8*j,p);b+=words;b+='\0';++j;}return block_finish(b);}
inline void text_check(const SceneChunk&c){auto p=relo_positions(c.relo);uint32_t n=patch_u32(c.data,24);std::vector<uint32_t>want;for(uint32_t j=0;j<n;++j)want.push_back(16+8*j);require(p==want,"Text list fix-ups have an unexpected form");}
// The one picture list and the one text list of a scene (sections 0 and 3); -1 if there is none, refused if there are several.
inline std::pair<int,int> scene_find(const Scene&sc,const char*tag){std::pair<int,int>at{-1,-1};
    for(int s=0;s<2;++s)for(size_t i=0;i<sc.chunks[s].size();++i)if(!memcmp(sc.chunks[s][i].data.data(),tag,4)){require(at.first<0,"Scene has more than one list of a kind");at={s,(int)i};}return at;}
inline bool scene_same_content(const std::string&a,const std::string&b){
    auto x=scene_unpack(a),y=scene_unpack(b);for(int i:{2,5})if(x.sections[i]!=y.sections[i])return false;
    for(int s=0;s<2;++s){if(x.chunks[s].size()!=y.chunks[s].size())return false;
        for(size_t i=0;i<x.chunks[s].size();++i){auto&c=x.chunks[s][i];auto&d=y.chunks[s][i];if(memcmp(c.data.data(),d.data.data(),4))return false;
            if(c.data.substr(12,4)!=d.data.substr(12,4))return false;
            if(!memcmp(c.data.data(),"PTEX",4)){ptex_check(c);ptex_check(d);auto p=ptex_read(c.data),q=ptex_read(d.data);if(p.pictures.size()!=q.pictures.size())return false;
                for(auto&[k,v]:p.pictures){auto w=q.pictures.find(k);if(w==q.pictures.end()||w->second.rect!=v.rect||q.images[w->second.image]!=p.images[v.image])return false;}
                auto ia=p.images,ib=q.images;std::sort(ia.begin(),ia.end());std::sort(ib.begin(),ib.end());if(ia!=ib)return false;}
            else if(!memcmp(c.data.data(),"TEXT",4)){text_check(c);text_check(d);if(c.data.substr(16,8)!=d.data.substr(16,8)||text_sorted(text_read(c.data))!=text_sorted(text_read(d.data)))return false;}
            else if(c.data!=d.data||c.relo!=d.relo)return false;}}
    return true;
}

struct ScenePatch {
    std::vector<std::string>images;                                              // appended (with the patch's pictures)
    struct Replace {std::string oldSha,data;};std::vector<Replace>replaceImages;  // an existing image, found by its SHA-256
    struct Picture {uint32_t key=0;std::string rect,imageSha;bool ifAbsent=false;};std::vector<Picture>pictures;
    struct Text {uint32_t key=0;std::string words,mode,expect;bool hasExpect=false;};std::vector<Text>texts;
    struct Block {std::string tag,oldSha,data,relo;};std::vector<Block>blocks;    // relo empty: the block's fix-up list stays
};
// `file` reads a file named by the patch (relative to the patch); its SHA-256 is checked here.
inline ScenePatch scene_patch_read(const Json&j,const std::function<std::string(const std::string&)>&file){
    require(j.at("format").str()=="scene-patch/1","Unknown scene patch format");ScenePatch p;
    auto named=[&](const Json&o,const char*f,const char*sha){auto d=file(o.at(f).str());auto want=o.at(sha).str();require(patch_sha_text(want)&&scene_sha(d)==want,"Patch file hash mismatch");return d;};
    auto sha=[](const Json&o,const char*k){auto s=o.at(k).str();require(patch_sha_text(s),"Invalid SHA-256 in patch (expected 64 lowercase hex digits)");return s;};
    auto dds=[](const std::string&d){require(d.size()>=128&&!memcmp(d.data(),"DDS ",4),"Patch image is not a DDS image");return d;};
    if(j.has("images"))for(auto&o:j.at("images").list())p.images.push_back(dds(named(o,"file","sha256")));
    if(j.has("replace_images"))for(auto&o:j.at("replace_images").list())p.replaceImages.push_back({sha(o,"old_sha256"),dds(named(o,"file","sha256"))});
    if(j.has("pictures"))for(auto&o:j.at("pictures").list()){ScenePatch::Picture q;q.ifAbsent=patch_mode(o,{"add","add_if_absent"})=="add_if_absent";q.key=patch_hex32(o.at("key"));
        q.rect=patch_hex_bytes(o.at("rect_hex").str());require(q.rect.size()==32,"A picture's rect_hex holds 8 floats (64 hex digits)");q.imageSha=sha(o,"image_sha256");p.pictures.push_back(q);}
    if(j.has("texts"))for(auto&o:j.at("texts").list()){ScenePatch::Text t;t.mode=patch_mode(o,{"add","add_if_absent","set"});t.key=patch_hex32(o.at("key"));t.words=patch_words(o,"text","text_hex");
        if(o.has("expect_text")||o.has("expect_text_hex")){require(t.mode=="set","Only a set takes an expected text");t.hasExpect=true;t.expect=patch_words(o,"expect_text","expect_text_hex");}p.texts.push_back(t);}
    if(j.has("blocks"))for(auto&o:j.at("blocks").list()){ScenePatch::Block b;b.tag=o.at("tag").str();require(b.tag.size()==4&&b.tag!="PTEX"&&b.tag!="TEXT","Invalid block tag in patch");
        b.oldSha=sha(o,"old_sha256");b.data=named(o,"file","sha256");require(b.data.size()>=16&&b.data.size()%64==0&&!memcmp(b.data.data(),b.tag.data(),4)&&patch_u32(b.data,4)==b.data.size(),"Patch block is not a complete block");
        if(o.has("relo_file")){b.relo=named(o,"relo_file","relo_sha256");relo_positions(b.relo);require(!memcmp(b.relo.data()+16,b.tag.data(),4),"Patch fix-up list belongs to another block");}p.blocks.push_back(b);}
    std::set<uint32_t>pk,tk;for(auto&q:p.pictures)require(pk.insert(q.key).second,"A patch names one picture twice");for(auto&t:p.texts)require(tk.insert(t.key).second,"A patch names one text twice");
    require(p.images.size()+p.replaceImages.size()+p.pictures.size()+p.texts.size()+p.blocks.size()>0,"Empty scene patch");return p;
}

inline PatchResult scene_apply(const std::string&base,const std::vector<const PatchInput<ScenePatch>*>&ps,int level){
    auto sc=scene_unpack(base);PatchResult res;
    bool wantPictures=false,wantTexts=false,wantBlocks=false;
    for(auto*p:ps){wantPictures|=!p->patch.pictures.empty()||!p->patch.replaceImages.empty();wantTexts|=!p->patch.texts.empty();wantBlocks|=!p->patch.blocks.empty();}
    if(!wantPictures&&!wantTexts&&!wantBlocks){res.data=level?base:scene_digest(sc);return res;}
    auto pt=scene_find(sc,"PTEX"),tt=scene_find(sc,"TEXT");
    // texts: each key's words (a key that is there several times with the same words counts once; with different words it is
    // "mixed" and no patch may touch it). The list is written back from baseTexts, so repeated keys stay repeated.
    ScenePictures pics;std::vector<std::string>shas,baseShas;std::map<uint32_t,std::string>texts;TextEntries baseTexts;std::set<uint32_t>mixed;
    if(wantPictures){for(int k=0;k<(int)ps.size();++k)if(pt.first<0&&(!ps[k]->patch.pictures.empty()||!ps[k]->patch.replaceImages.empty()))throw PatchClash(k,-1,"the scene has no picture list");
        auto&c=sc.chunks[pt.first][pt.second];ptex_check(c);require(sc.sections[2].empty()&&sc.sections[5].empty(),"Scene keeps images outside its picture list");
        pics=ptex_read(c.data);for(auto&d:pics.images)shas.push_back(scene_sha(d));baseShas=shas;}
    if(wantTexts){for(int k=0;k<(int)ps.size();++k)if(tt.first<0&&!ps[k]->patch.texts.empty())throw PatchClash(k,-1,"the scene has no text list");
        auto&c=sc.chunks[tt.first][tt.second];text_check(c);baseTexts=text_read(c.data);
        for(auto&[key,words]:baseTexts){auto e=texts.emplace(key,words);if(!e.second&&e.first->second!=words)mixed.insert(key);}}
    auto pic=[](uint32_t k){return "picture "+patch_hex_text(k);};auto txt=[](uint32_t k){return "text "+patch_hex_text(k);};
    auto shaShort=[](const std::string&s){return s.substr(0,8);};
    // 1. Images repainted, found by their content in the member.
    std::map<size_t,int>replacedBy;bool picturesChanged=false;
    for(int k=0;k<(int)ps.size();++k)for(auto&r:ps[k]->patch.replaceImages){size_t found=SIZE_MAX;
        for(size_t i=0;i<baseShas.size();++i)if(baseShas[i]==r.oldSha){if(found!=SIZE_MAX)throw PatchClash(k,-1,"image "+shaShort(r.oldSha)+" is there more than once");found=i;}
        if(found==SIZE_MAX)throw PatchClash(k,-1,"image "+shaShort(r.oldSha)+" is not there");
        auto e=replacedBy.find(found);
        if(e!=replacedBy.end()){if(pics.images[found]==r.data){res.notes.push_back("image "+shaShort(r.oldSha)+": "+ps[k]->who+" repaints it like "+ps[e->second]->who);continue;}
            throw PatchClash(k,e->second,"image "+shaShort(r.oldSha)+" is repainted differently");}
        pics.images[found]=r.data;shas[found]=scene_sha(r.data);replacedBy[found]=k;picturesChanged=true;}
    // 2. Pictures. All of a patch's images are appended (in patch order, then in the order the patch lists them) when at least
    // one of its pictures is finally kept — exactly what its author built — and none when all gave way, so the order of mods
    // never decides which images are in the scene.
    struct Added {std::string rect,imageSha;int by;bool ifAbsent;};std::map<uint32_t,Added>added;
    for(int k=0;k<(int)ps.size();++k){auto&p=ps[k]->patch;std::vector<std::string>own;for(auto&d:p.images)own.push_back(scene_sha(d));
        for(auto&q:p.pictures){
            bool known=std::find(own.begin(),own.end(),q.imageSha)!=own.end()||std::find(shas.begin(),shas.end(),q.imageSha)!=shas.end();
            // An image another patch repainted: that patch is the other side (it may be the one to give way).
            auto repaintedBy=[&](const std::string&sha)->int{for(auto&[i,by]:replacedBy)if(baseShas[i]==sha)return by;return -1;};
            if(!known)throw PatchClash(k,repaintedBy(q.imageSha),pic(q.key)+" uses image "+shaShort(q.imageSha)+", which is not there"+(repaintedBy(q.imageSha)>=0?" (another mod repaints it)":""));
            auto b=pics.pictures.find(q.key);
            if(b!=pics.pictures.end()){
                if(b->second.rect==q.rect&&shas[b->second.image]==q.imageSha){res.notes.push_back(pic(q.key)+" from "+ps[k]->who+" is already there");continue;}
                if(q.ifAbsent){res.notes.push_back(pic(q.key)+" from "+ps[k]->who+" is left as it is (already there)");continue;}
                bool sameAsOriginal=b->second.rect==q.rect&&baseShas[b->second.image]==q.imageSha;
                throw PatchClash(k,sameAsOriginal?repaintedBy(q.imageSha):-1,pic(q.key)+" is already there with another picture");}
            auto e=added.find(q.key);
            if(e!=added.end()){
                if(e->second.rect==q.rect&&e->second.imageSha==q.imageSha){res.notes.push_back(pic(q.key)+": "+ps[k]->who+" adds the same picture as "+ps[e->second.by]->who);continue;}
                if(q.ifAbsent){res.notes.push_back(pic(q.key)+": "+ps[e->second.by]->who+"'s picture is kept, "+ps[k]->who+"'s if-absent picture is left out");continue;}
                if(!e->second.ifAbsent)throw PatchClash(k,e->second.by,pic(q.key)+" is added by both with different pictures");
                res.notes.push_back(pic(q.key)+": "+ps[k]->who+"'s picture replaces "+ps[e->second.by]->who+"'s if-absent picture");}
            added[q.key]={q.rect,q.imageSha,k,q.ifAbsent};}}
    if(!added.empty()){std::set<int>kept;for(auto&[key,a]:added)kept.insert(a.by);
        for(int k:kept)for(auto&d:ps[k]->patch.images){auto s=scene_sha(d);if(std::find(shas.begin(),shas.end(),s)==shas.end()){pics.images.push_back(d);shas.push_back(s);}}
        for(auto&[key,a]:added)pics.pictures[key]={a.rect,(uint32_t)(std::find(shas.begin(),shas.end(),a.imageSha)-shas.begin())};
        picturesChanged=true;}
    // 3. Texts.
    std::map<uint32_t,std::pair<int,bool>>textBy;std::map<uint32_t,int>setBy;bool textsChanged=false;
    for(int k=0;k<(int)ps.size();++k)for(auto&t:ps[k]->patch.texts){auto e=texts.find(t.key);auto by=textBy.find(t.key);auto sb=setBy.find(t.key);
        if(mixed.count(t.key))throw PatchClash(k,-1,txt(t.key)+" is there more than once with different words");
        if(t.mode=="set"){if(e==texts.end())throw PatchClash(k,-1,txt(t.key)+" is not there");
            if(sb!=setBy.end()){if(e->second==t.words){res.notes.push_back(txt(t.key)+": "+ps[k]->who+" sets the same words as "+ps[sb->second]->who);continue;}
                throw PatchClash(k,sb->second,txt(t.key)+" is set to different words");}
            if(t.hasExpect&&e->second!=t.expect)throw PatchClash(k,by==textBy.end()?-1:by->second.first,txt(t.key)+" no longer has the expected words");
            e->second=t.words;setBy[t.key]=k;textsChanged=true;continue;}
        if(e!=texts.end()){if(e->second==t.words){res.notes.push_back(txt(t.key)+" from "+ps[k]->who+" is already there");continue;}
            if(t.mode=="add_if_absent"){res.notes.push_back(txt(t.key)+" from "+ps[k]->who+" is left as it is (already there)");continue;}
            if(by==textBy.end())throw PatchClash(k,sb==setBy.end()?-1:sb->second,txt(t.key)+" is already there with other words");
            if(!by->second.second)throw PatchClash(k,by->second.first,txt(t.key)+" is added by both with different words");
            res.notes.push_back(txt(t.key)+": "+ps[k]->who+"'s words replace "+ps[by->second.first]->who+"'s if-absent words");}
        texts[t.key]=t.words;textBy[t.key]={k,t.mode=="add_if_absent"};textsChanged=true;}
    // 4. Whole blocks, found by tag and by their content in the member (before any patch).
    std::vector<std::string>blockShas[2];
    if(wantBlocks)for(int s=0;s<2;++s)for(auto&c:sc.chunks[s])blockShas[s].push_back(scene_sha(c.data));
    std::map<std::pair<int,int>,int>blockBy;
    for(int k=0;k<(int)ps.size();++k)for(auto&b:ps[k]->patch.blocks){std::pair<int,int>at{-1,-1};
        for(int s=0;s<2;++s)for(size_t i=0;i<sc.chunks[s].size();++i){
            if(memcmp(sc.chunks[s][i].data.data(),b.tag.data(),4)||blockShas[s][i]!=b.oldSha)continue;
            if(at.first>=0)throw PatchClash(k,-1,"block "+b.tag+" "+shaShort(b.oldSha)+" is there more than once");at={s,(int)i};}
        if(at.first<0)throw PatchClash(k,-1,"block "+b.tag+" "+shaShort(b.oldSha)+" is not there");
        auto&c=sc.chunks[at.first][at.second];auto e=blockBy.find(at);
        if(e!=blockBy.end()){if(c.data==b.data&&(b.relo.empty()||c.relo==b.relo)){res.notes.push_back("block "+b.tag+": "+ps[k]->who+" makes the same change as "+ps[e->second]->who);continue;}
            throw PatchClash(k,e->second,"block "+b.tag+" is changed differently");}
        c.data=b.data;if(!b.relo.empty())c.relo=b.relo;blockBy[at]=k;}
    if(picturesChanged){auto&c=sc.chunks[pt.first][pt.second];auto made=ptex_make(c.data,pics);std::vector<uint32_t>pos{8};
        for(size_t j=0;j<pics.images.size();++j)pos.push_back(made.second+8*(uint32_t)j);c.relo=relo_make(c.relo,"PTEX",pos);c.data=made.first;}
    if(textsChanged){TextEntries out;std::set<uint32_t>inBase;
        for(auto&[key,words]:baseTexts){out.push_back({key,mixed.count(key)?words:texts.at(key)});inBase.insert(key);}
        for(auto&[key,words]:texts)if(!inBase.count(key))out.push_back({key,words});
        out=text_sorted(out);auto&c=sc.chunks[tt.first][tt.second];c.data=text_make(c.data,out);std::vector<uint32_t>pos;
        for(size_t j=0;j<out.size();++j)pos.push_back(16+8*(uint32_t)j);c.relo=relo_make(c.relo,"TEXT",pos);}
    res.data=level?scene_pack(sc,level):scene_digest(sc);return res;   // level 0: only the digest (the loader's self-check)
}

// What a patch from `base` to `modified` holds, and the files it names (relative paths).
struct SceneMade {std::string json;std::map<std::string,std::string>files;};
inline SceneMade scene_make(const std::string&base,const std::string&modified,bool ifAbsent){
    auto a=scene_unpack(base),b=scene_unpack(modified);SceneMade m;require(a.sections[2]==b.sections[2]&&a.sections[5]==b.sections[5],"The modified scene changes data a patch cannot express");
    std::vector<std::string>images,replaces,pictures,texts,blocks;
    auto file=[&](const std::string&dir,const std::string&d,const char*ext){auto s=scene_sha(d);auto n=dir+"/"+s.substr(0,16)+ext;m.files[n]=d;return std::make_pair(n,s);};
    for(int s=0;s<2;++s){require(a.chunks[s].size()==b.chunks[s].size(),"The modified scene adds or removes blocks");
        for(size_t i=0;i<a.chunks[s].size();++i){auto&x=a.chunks[s][i];auto&y=b.chunks[s][i];require(!memcmp(x.data.data(),y.data.data(),4),"The modified scene reorders blocks");
            std::string tag=x.data.substr(0,4);
            if(tag=="PTEX"){auto p=ptex_read(x.data),q=ptex_read(y.data);require(q.images.size()>=p.images.size(),"The modified scene removes images");
                for(size_t j=0;j<p.images.size();++j)if(p.images[j]!=q.images[j]){auto f=file("images",q.images[j],".dds");
                    replaces.push_back("  {\"old_sha256\": \""+scene_sha(p.images[j])+"\", \"file\": \""+f.first+"\", \"sha256\": \""+f.second+"\"}");}
                for(size_t j=p.images.size();j<q.images.size();++j){auto f=file("images",q.images[j],".dds");images.push_back("  {\"file\": \""+f.first+"\", \"sha256\": \""+f.second+"\"}");}
                for(auto&[k,v]:p.pictures){auto w=q.pictures.find(k);require(w!=q.pictures.end()&&w->second.rect==v.rect&&w->second.image==v.image,"The modified scene moves or removes picture "+patch_hex_text(k));}
                for(auto&[k,v]:q.pictures)if(!p.pictures.count(k))pictures.push_back(std::string("  {\"mode\": \"")+(ifAbsent?"add_if_absent":"add")+"\", \"key\": \""+patch_hex_text(k)+"\", \"rect_hex\": \""+patch_bytes_hex(v.rect)+"\", \"image_sha256\": \""+scene_sha(q.images[v.image])+"\"}");}
            else if(tag=="TEXT"){std::map<uint32_t,std::vector<std::string>>p,q;for(auto&[k,v]:text_read(x.data))p[k].push_back(v);for(auto&[k,v]:text_read(y.data))q[k].push_back(v);
                auto one=[](const std::vector<std::string>&v){return std::all_of(v.begin(),v.end(),[&](const std::string&w){return w==v[0];});};
                for(auto&[k,v]:p){auto w=q.find(k);require(w!=q.end()&&w->second.size()==v.size(),"The modified scene removes or repeats text "+patch_hex_text(k));
                    if(w->second==v)continue;require(one(v)&&one(w->second),"The modified scene changes a repeated text differently: "+patch_hex_text(k));
                    texts.push_back("  {\"mode\": \"set\", \"key\": \""+patch_hex_text(k)+"\", "+patch_words_json("text","text_hex",w->second[0])+", "+patch_words_json("expect_text","expect_text_hex",v[0])+"}");}
                for(auto&[k,v]:q)if(!p.count(k)){require(v.size()==1,"The modified scene adds a text twice: "+patch_hex_text(k));
                    texts.push_back(std::string("  {\"mode\": \"")+(ifAbsent?"add_if_absent":"add")+"\", \"key\": \""+patch_hex_text(k)+"\", "+patch_words_json("text","text_hex",v[0])+"}");}}
            else if(x.data!=y.data||x.relo!=y.relo){auto f=file("blocks",y.data,".bin");std::string relo;
                if(x.relo!=y.relo){auto r=file("blocks",y.relo,".relo.bin");relo=", \"relo_file\": \""+r.first+"\", \"relo_sha256\": \""+r.second+"\"";}
                blocks.push_back("  {\"tag\": "+json_quote(tag)+", \"old_sha256\": \""+scene_sha(x.data)+"\", \"file\": \""+f.first+"\", \"sha256\": \""+f.second+"\""+relo+"}");}}}
    auto list=[](const std::vector<std::string>&v){std::string r;for(auto&x:v)r+=(r.empty()?"":",\n")+x;return "[\n"+r+"]";};
    require(images.size()+replaces.size()+pictures.size()+texts.size()+blocks.size()>0,"The two scenes have the same content");
    m.json="{\"format\": \"scene-patch/1\",\n \"images\": "+list(images)+",\n \"replace_images\": "+list(replaces)+",\n \"pictures\": "+list(pictures)+",\n \"texts\": "+list(texts)+",\n \"blocks\": "+list(blocks)+"}\n";
    return m;
}
}
