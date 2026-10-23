#pragma once
// 1.3.0: "ssrt-patch/1" — rows added to, and fields changed in, the game's SSRT tables (Shopping.dat, racer settings, ...).
// Layout (little-endian): "SSRT", table count, table offsets. Table: +4 name (64-byte area), +68 row count, +72 row offsets.
// Row: +0 field count, +4 a word kept as it is, +8 name (64-byte area), +72 fields of 12 bytes {hash, type, value}; a text
// field's value is the file offset of zero-terminated text.
// Applying never moves an original byte: new texts, rows and longer row lists are appended (4-byte aligned) and only the
// table's offset in the header (and, for a changed field, that field's value) is rewritten — the way the mods' own builders
// (Egg Hangar's build_shop.py, Codex's add_shopping_registration.py) made the tables that were tested in the game.
#include "member_patch.h"
namespace ssasr {
constexpr uint32_t kSsrtText=0xB7459825;
struct SsrtField {uint32_t hash=0,type=0,bits=0;std::string text;
    bool same(const SsrtField&o)const{return hash==o.hash&&type==o.type&&(type==kSsrtText?text==o.text:bits==o.bits);}};
struct SsrtRow {std::string name;uint32_t word4=0;std::vector<SsrtField>fields;
    bool same(const SsrtRow&o)const{if(name!=o.name||word4!=o.word4||fields.size()!=o.fields.size())return false;
        for(size_t i=0;i<fields.size();++i)if(!fields[i].same(o.fields[i]))return false;return true;}};
struct SsrtTable {std::string name;uint32_t at=0;std::vector<uint32_t>rowAt;std::vector<SsrtRow>rows;};
struct SsrtRowAdd {std::string table;SsrtRow row;bool ifAbsent=false;};
// occurrence: which row of that name in the member (1 = the first); needed only when the name is there more than once
// (the game's own Shopping.dat has two Item19 and two Item23 rows). Rows are only ever appended, so the count is stable.
struct SsrtSet {std::string table,row;uint32_t occurrence=0;SsrtField value;bool hasExpect=false;SsrtField expect;};
struct SsrtPatch {std::vector<SsrtRowAdd>rows;std::vector<SsrtSet>sets;};

inline std::string ssrt_text(const std::string&d,uint32_t p,size_t limit){
    require(p<d.size(),"SSRT text outside the file");auto e=d.find('\0',p);require(e!=std::string::npos&&e-p<=limit,"SSRT text is not terminated");return d.substr(p,e-p);}
inline std::vector<SsrtTable> ssrt_read(const std::string&d){
    require(d.size()>=12&&!memcmp(d.data(),"SSRT",4),"Member is not an SSRT table file");
    uint32_t n=patch_u32(d,4);require(n>0&&n<=4096,"Invalid SSRT table count");std::vector<SsrtTable>tables(n);
    for(uint32_t i=0;i<n;++i){auto&t=tables[i];t.at=patch_u32(d,8+4ull*i);t.name=ssrt_text(d,t.at+4,63);uint32_t rows=patch_u32(d,t.at+68ull);
        require(rows<=1000000&&uint64_t(t.at)+72+4ull*rows<=d.size(),"Invalid SSRT row count");
        for(uint32_t j=0;j<rows;++j){uint32_t r=patch_u32(d,t.at+72ull+4ull*j);uint32_t nf=patch_u32(d,r);
            require(nf<=4096&&uint64_t(r)+72+12ull*nf<=d.size(),"Invalid SSRT row");SsrtRow row;row.word4=patch_u32(d,r+4ull);row.name=ssrt_text(d,r+8,63);
            for(uint32_t k=0;k<nf;++k){uint64_t q=r+72ull+12ull*k;SsrtField f;f.hash=patch_u32(d,(size_t)q);f.type=patch_u32(d,(size_t)q+4);f.bits=patch_u32(d,(size_t)q+8);
                if(f.type==kSsrtText)f.text=ssrt_text(d,f.bits,1u<<20);row.fields.push_back(f);}
            t.rowAt.push_back(r);t.rows.push_back(std::move(row));}}
    return tables;
}
inline bool ssrt_same_content(const std::string&a,const std::string&b){auto x=ssrt_read(a),y=ssrt_read(b);if(x.size()!=y.size())return false;
    for(size_t i=0;i<x.size();++i){if(x[i].name!=y[i].name||x[i].rows.size()!=y[i].rows.size())return false;
        for(size_t j=0;j<x[i].rows.size();++j)if(!x[i].rows[j].same(y[i].rows[j]))return false;}return true;}

inline SsrtField ssrt_field_json(const Json&o){SsrtField f;f.hash=patch_hex32(o.at("hash"));f.type=patch_hex32(o.at("type"));return f;}
inline void ssrt_value_json(SsrtField&f,const Json&o,const char*bits,const char*text,const char*hex){
    if(f.type==kSsrtText){require(!o.has(bits),"A text field takes text, not bits");f.text=patch_words(o,text,hex);}
    else{require(!o.has(text)&&!o.has(hex),"A number field takes bits, not text");f.bits=patch_hex32(o.at(bits));}}
inline SsrtPatch ssrt_patch_read(const Json&j){
    require(j.at("format").str()=="ssrt-patch/1","Unknown SSRT patch format");SsrtPatch p;
    if(j.has("rows"))for(auto&o:j.at("rows").list()){SsrtRowAdd a;a.ifAbsent=patch_mode(o,{"add","add_if_absent"})=="add_if_absent";a.table=o.at("table").str();
        a.row.name=o.at("row").str();require(!a.row.name.empty()&&a.row.name.size()<=63&&a.row.name.find('\0')==std::string::npos,"Invalid SSRT row name");
        a.row.word4=patch_hex32(o.at("word4"));std::set<uint32_t>seen;
        for(auto&fo:o.at("fields").list()){auto f=ssrt_field_json(fo);ssrt_value_json(f,fo,"bits","text","text_hex");
            require(seen.insert(f.hash).second,"A row names one field twice");a.row.fields.push_back(f);}
        require(a.row.fields.size()<=4096,"Too many fields in a row");p.rows.push_back(std::move(a));}
    if(j.has("set_fields"))for(auto&o:j.at("set_fields").list()){SsrtSet s;s.table=o.at("table").str();s.row=o.at("row").str();
        if(o.has("occurrence")){s.occurrence=o.at("occurrence").u32();require(s.occurrence>0,"A row occurrence counts from 1");}
        s.value=ssrt_field_json(o);ssrt_value_json(s.value,o,"bits","text","text_hex");
        if(o.has("expect_bits")||o.has("expect_text")||o.has("expect_text_hex")){s.hasExpect=true;s.expect=s.value;ssrt_value_json(s.expect,o,"expect_bits","expect_text","expect_text_hex");}
        p.sets.push_back(std::move(s));}
    std::set<std::pair<std::string,std::string>>names;for(auto&a:p.rows)require(names.insert({a.table,a.row.name}).second,"A patch adds one row twice");
    require(!p.rows.empty()||!p.sets.empty(),"Empty SSRT patch");return p;
}

// Resolves every patch against the member and the patches before it, then writes the result (see compose_patches for clashes).
inline PatchResult ssrt_apply(const std::string&base,const std::vector<const PatchInput<SsrtPatch>*>&ps){
    auto tables=ssrt_read(base);PatchResult res;
    auto tableOf=[&](int k,const std::string&name)->size_t{size_t found=SIZE_MAX;
        for(size_t i=0;i<tables.size();++i)if(tables[i].name==name){if(found!=SIZE_MAX)throw PatchClash(k,-1,"table "+name+" appears more than once");found=i;}
        if(found==SIZE_MAX)throw PatchClash(k,-1,"no table "+name);return found;};
    auto baseRows=[&](size_t t,const std::string&row){std::vector<size_t>r;for(size_t i=0;i<tables[t].rows.size();++i)if(tables[t].rows[i].name==row)r.push_back(i);return r;};
    struct Added {size_t table;SsrtRow row;int by;bool ifAbsent;};std::vector<Added>added;
    auto entry=[&](size_t t,const std::string&row){return "table "+tables[t].name+" row "+row;};
    for(int k=0;k<(int)ps.size();++k)for(auto&a:ps[k]->patch.rows){
        size_t t=tableOf(k,a.table);auto in=baseRows(t,a.row.name);
        if(!in.empty()){if(std::any_of(in.begin(),in.end(),[&](size_t i){return tables[t].rows[i].same(a.row);}))res.notes.push_back(entry(t,a.row.name)+" from "+ps[k]->who+" is already there");
            else if(a.ifAbsent)res.notes.push_back(entry(t,a.row.name)+" from "+ps[k]->who+" is left as it is (already there)");
            else throw PatchClash(k,-1,entry(t,a.row.name)+" is already there with other content");continue;}
        auto e=std::find_if(added.begin(),added.end(),[&](const Added&x){return x.table==t&&x.row.name==a.row.name;});
        if(e==added.end()){added.push_back({t,a.row,k,a.ifAbsent});continue;}
        if(e->row.same(a.row))res.notes.push_back(entry(t,a.row.name)+": "+ps[k]->who+" adds the same row as "+ps[e->by]->who);
        else if(a.ifAbsent)res.notes.push_back(entry(t,a.row.name)+": "+ps[e->by]->who+"'s row is kept, "+ps[k]->who+"'s if-absent row is left out");
        else if(e->ifAbsent){res.notes.push_back(entry(t,a.row.name)+": "+ps[k]->who+"'s row replaces "+ps[e->by]->who+"'s if-absent row");added.erase(e);added.push_back({t,a.row,k,false});}
        else throw PatchClash(k,e->by,entry(t,a.row.name)+" is added by both with different content");
    }
    // Changed fields: of a row in the member (written in place) or of a row a patch adds (changed before it is written).
    struct SetAt {size_t table,row;bool inBase;size_t field;int by;SsrtField value;};std::vector<SetAt>sets;
    for(int k=0;k<(int)ps.size();++k)for(auto&s:ps[k]->patch.sets){
        size_t t=tableOf(k,s.table);auto in=baseRows(t,s.row);
        if(in.size()>1&&!s.occurrence)throw PatchClash(k,-1,entry(t,s.row)+" is there more than once and the patch does not say which");
        if(!in.empty()&&s.occurrence>in.size())throw PatchClash(k,-1,entry(t,s.row)+" is not there "+std::to_string(s.occurrence)+" times");
        const SsrtRow*row=nullptr;size_t index=0;int owner=-1;bool inBase=!in.empty();
        if(inBase){index=in[s.occurrence?s.occurrence-1:0];row=&tables[t].rows[index];}
        else for(size_t i=0;i<added.size();++i)if(added[i].table==t&&added[i].row.name==s.row){row=&added[i].row;index=i;owner=added[i].by;}
        if(!row)throw PatchClash(k,-1,entry(t,s.row)+" is not there");
        size_t fi=SIZE_MAX;for(size_t i=0;i<row->fields.size();++i)if(row->fields[i].hash==s.value.hash){if(fi!=SIZE_MAX)throw PatchClash(k,owner,entry(t,s.row)+" names field "+patch_hex_text(s.value.hash)+" twice");fi=i;}
        auto what=entry(t,s.row)+" field "+patch_hex_text(s.value.hash);
        if(fi==SIZE_MAX||row->fields[fi].type!=s.value.type)throw PatchClash(k,owner,what+" is not there with that type");
        if(s.hasExpect&&!row->fields[fi].same(s.expect))throw PatchClash(k,owner,what+" no longer has the expected value");
        auto e=std::find_if(sets.begin(),sets.end(),[&](const SetAt&x){return x.table==t&&x.row==index&&x.inBase==inBase&&x.field==fi;});
        if(e!=sets.end()){if(e->value.same(s.value)){res.notes.push_back(what+": "+ps[k]->who+" sets the same value as "+ps[e->by]->who);continue;}
            throw PatchClash(k,e->by,what+" is set to different values");}
        sets.push_back({t,index,inBase,fi,k,s.value});
    }
    std::string out=base;
    auto put=[&](const std::string&b)->uint32_t{out.append((4-out.size()%4)%4,'\0');require(out.size()+b.size()<=UINT32_MAX,"SSRT file too large");auto at=(uint32_t)out.size();out+=b;return at;};
    for(auto&s:sets){if(!s.inBase){added[s.row].row.fields[s.field]=s.value;continue;}
        auto at=tables[s.table].rowAt[s.row]+72+12*(uint32_t)s.field+8;patch_put32(out,at,s.value.type==kSsrtText?put(s.value.text+std::string(1,'\0')):s.value.bits);}
    for(size_t t=0;t<tables.size();++t){std::vector<uint32_t>rows;
        for(auto&a:added){if(a.table!=t)continue;std::string row(72,'\0');uint32_t nf=(uint32_t)a.row.fields.size();memcpy(row.data(),&nf,4);memcpy(row.data()+4,&a.row.word4,4);memcpy(row.data()+8,a.row.name.data(),a.row.name.size());
            for(auto&f:a.row.fields){uint32_t v[3]={f.hash,f.type,f.type==kSsrtText?put(f.text+std::string(1,'\0')):f.bits};row.append((const char*)v,12);}
            rows.push_back(put(row));}
        if(rows.empty())continue;
        auto&tb=tables[t];std::string block=base.substr(tb.at,72+4*tb.rows.size());patch_put32(block,68,(uint32_t)(tb.rows.size()+rows.size()));
        for(auto r:rows)block.append((const char*)&r,4);patch_put32(out,8+4*t,put(block));
    }
    ssrt_read(out);res.data=std::move(out);return res;
}

// Writes the patch that turns `base` into `modified`: rows the modified file adds (after all the base rows, which must stay in
// order) and fields it changes (each with the base value as "expect"). Removed rows or fields cannot be expressed: refused.
inline std::string ssrt_make(const std::string&base,const std::string&modified,bool ifAbsent){
    auto a=ssrt_read(base),b=ssrt_read(modified);require(a.size()==b.size(),"The modified file has another number of tables");
    auto field=[](const SsrtField&f,const char*bits,const char*text,const char*hex){
        return f.type==kSsrtText?patch_words_json(text,hex,f.text):std::string("\"")+bits+"\": \""+patch_hex_text(f.bits)+"\"";};
    std::string rows,sets;
    for(size_t t=0;t<a.size();++t){require(a[t].name==b[t].name&&b[t].rows.size()>=a[t].rows.size(),"The modified file renames or shortens a table");
        for(size_t i=0;i<a[t].rows.size();++i){auto&x=a[t].rows[i];auto&y=b[t].rows[i];
            require(x.name==y.name&&x.word4==y.word4&&x.fields.size()==y.fields.size(),"The modified file reorders, renames or reshapes a row: "+x.name);
            for(size_t k=0;k<x.fields.size();++k){require(x.fields[k].hash==y.fields[k].hash&&x.fields[k].type==y.fields[k].type,"The modified file reshapes row "+x.name);
                if(x.fields[k].same(y.fields[k]))continue;
                size_t count=0,nth=0;for(size_t r=0;r<a[t].rows.size();++r)if(a[t].rows[r].name==x.name){++count;if(r<=i)++nth;}
                auto occurrence=count>1?", \"occurrence\": "+std::to_string(nth):std::string();
                sets+=std::string(sets.empty()?"":",\n")+"  {\"table\": "+json_quote(a[t].name)+", \"row\": "+json_quote(x.name)+occurrence+", \"hash\": \""+patch_hex_text(y.fields[k].hash)+"\", \"type\": \""+patch_hex_text(y.fields[k].type)+"\", "
                    +field(y.fields[k],"bits","text","text_hex")+", "+field(x.fields[k],"expect_bits","expect_text","expect_text_hex")+"}";}}
        for(size_t i=a[t].rows.size();i<b[t].rows.size();++i){auto&y=b[t].rows[i];std::string fs;
            for(auto&f:y.fields)fs+=std::string(fs.empty()?"":",\n")+"    {\"hash\": \""+patch_hex_text(f.hash)+"\", \"type\": \""+patch_hex_text(f.type)+"\", "+field(f,"bits","text","text_hex")+"}";
            rows+=std::string(rows.empty()?"":",\n")+"  {\"mode\": \""+(ifAbsent?"add_if_absent":"add")+"\", \"table\": "+json_quote(a[t].name)+", \"row\": "+json_quote(y.name)
                +", \"word4\": \""+patch_hex_text(y.word4)+"\", \"fields\": [\n"+fs+"]}";}}
    require(!rows.empty()||!sets.empty(),"The two files have the same rows");
    return "{\"format\": \"ssrt-patch/1\",\n \"rows\": [\n"+rows+"],\n \"set_fields\": [\n"+sets+"]}\n";
}
}
