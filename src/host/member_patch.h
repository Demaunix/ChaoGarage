#pragma once
// 1.3.0: what the member patch engines share. A patch names entries (rows, pictures, texts) by name or hash, never by position,
// so it fits any version of the member that still has, or still lacks, those entries.
#include "files.h"
#include "json.h"
#include <functional>
#include <map>
#include <set>
namespace ssasr {
inline void require(bool ok,const std::string&message){if(!ok)throw std::runtime_error(message);}
// Two patches (or a patch and the member it is applied to) want different things for one entry. `earlier` is the index of the
// other patch, or -1 when the member itself is the other side; the composer decides which side gives way.
struct PatchClash:std::runtime_error{int later,earlier;PatchClash(int l,int e,const std::string&what):std::runtime_error(what),later(l),earlier(e){}};
// One mod's patch for one member, after its file was read: `who` names the mod in notes and messages.
template<class P> struct PatchInput {std::string who;bool optional=false;P patch;};
struct PatchResult {std::string data;std::vector<std::string>notes;std::vector<int>skipped;};

inline uint32_t patch_hex32(const Json&j){auto s=j.str();require(s.size()==8,"Invalid hash in patch (expected 8 hex digits)");uint32_t v=0;
    for(char c:s){unsigned d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:99;require(d<16,"Invalid hash in patch (expected 8 hex digits)");v=(v<<4)|d;}return v;}
inline std::string patch_hex_text(uint32_t v){char b[9];const char*x="0123456789ABCDEF";for(int i=7;i>=0;--i){b[i]=x[v&15];v>>=4;}b[8]=0;return b;}
inline std::string patch_bytes_hex(const std::string&s){std::string r;const char*x="0123456789abcdef";for(unsigned char c:s){r+=x[c>>4];r+=x[c&15];}return r;}
inline std::string patch_hex_bytes(const std::string&s){require(s.size()%2==0,"Invalid hex bytes in patch");std::string r;
    auto d=[](char c)->unsigned{unsigned v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:99;require(v<16,"Invalid hex bytes in patch");return v;};
    for(size_t i=0;i<s.size();i+=2)r+=(char)((d(s[i])<<4)|d(s[i+1]));return r;}
// Words in a patch: "text" when they are plain printable ASCII, otherwise "text_hex" (exact bytes, never re-encoded).
inline std::string patch_words(const Json&o,const char*plain,const char*hex){
    bool p=o.has(plain),h=o.has(hex);require(p!=h,"Patch entry needs exactly one of text or text_hex");
    auto s=p?o.at(plain).str():patch_hex_bytes(o.at(hex).str());require(s.find('\0')==std::string::npos,"Patch text contains a zero byte");return s;}
inline bool patch_sha_text(const std::string&s){return s.size()==64&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
inline bool patch_plain(const std::string&s){for(unsigned char c:s)if(c<32||c>126)return false;return true;}
inline std::string json_quote(const std::string&s){std::string r="\"";for(char c:s){if(c=='"'||c=='\\')r+='\\';r+=c;}return r+"\"";}
inline std::string patch_words_json(const char*plain,const char*hex,const std::string&s){
    return patch_plain(s)?std::string("\"")+plain+"\": "+json_quote(s):std::string("\"")+hex+"\": \""+patch_bytes_hex(s)+"\"";}
inline std::string patch_mode(const Json&o,std::initializer_list<const char*>allowed){auto m=o.at("mode").str();
    for(auto a:allowed)if(m==a)return m;throw std::runtime_error("Unknown patch mode: "+m);}
// Reads a little-endian word inside a buffer, refusing reads past its end.
inline uint32_t patch_u32(const std::string&s,size_t p){require(p<=s.size()&&s.size()-p>=4,"Member data ends early");uint32_t v;memcpy(&v,s.data()+p,4);return v;}
inline void patch_put32(std::string&s,size_t p,uint32_t v){require(p<=s.size()&&s.size()-p>=4,"Member data ends early");memcpy(s.data()+p,&v,4);}

// Applies the patches in order; on a clash the optional side gives way (the later patch first, as for whole members in 1.2.0) and
// the remaining patches are applied again from the start. Without an optional side the clash is refused, naming both mods.
template<class P> PatchResult compose_patches(const std::vector<PatchInput<P>>&inputs,
        const std::function<PatchResult(const std::vector<const PatchInput<P>*>&)>&apply){
    std::vector<int>active;for(int i=0;i<(int)inputs.size();++i)active.push_back(i);std::vector<std::string>gaveWay;std::vector<int>skipped;
    for(;;){std::vector<const PatchInput<P>*>use;for(int i:active)use.push_back(&inputs[i]);
        try{auto r=apply(use);r.notes.insert(r.notes.begin(),gaveWay.begin(),gaveWay.end());r.skipped=skipped;return r;}
        catch(const PatchClash&c){
            int later=active.at(c.later),earlier=c.earlier<0?-1:active.at(c.earlier);auto&l=inputs[later];
            int drop=l.optional?later:earlier>=0&&inputs[earlier].optional?earlier:-2;
            std::string other=earlier<0?"the member it is applied to":inputs[earlier].who;
            if(drop==-2)throw std::runtime_error("Two mods change the same entry ("+l.who+" and "+other+": "+c.what()+")");
            gaveWay.push_back(inputs[drop].who+"'s optional patch gives way ("+c.what()+(drop==later?"; "+other+" kept)":"; "+l.who+" kept)"));
            skipped.push_back(drop);active.erase(std::find(active.begin(),active.end(),drop));}
    }
}
}
