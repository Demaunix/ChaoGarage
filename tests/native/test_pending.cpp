// 1.3.0: a staged mod update is put in place at start (src/host/pending_updates.h), only when complete and unchanged.
#include "pending_updates.h"
#include <iostream>
using namespace ssasr;
unsigned checks=0;
void check(bool value,const char*message){++checks;if(!value){std::cerr<<"FAILED: "<<message<<"\n";throw std::runtime_error(message);}}
std::wstring root,mods,updates;
void dirs(const std::wstring&d){for(size_t at=root.size();;){at=d.find(L'\\',at+1);CreateDirectoryW(d.substr(0,at).c_str(),nullptr);if(at==d.npos)break;}}
void make(const std::wstring&p,const std::string&b){dirs(p.substr(0,p.find_last_of(L'\\')));Handle f(CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr));require(f.h!=INVALID_HANDLE_VALUE,"Fixture create");if(!b.empty())write(f.h,b.data(),(DWORD)b.size());}
std::string text(const std::wstring&p){if(!pending_exists(p))return "<missing>";return text_file(p);}
std::string narrow(const std::wstring&w){std::string s;for(wchar_t c:w)s+=(char)c;return s;}  // (fixture names are ASCII)
std::string shaOf(const std::string&b){Hash h;if(!b.empty())h.add(b.data(),(DWORD)b.size());return h.finish();}
// A fresh game: mods\Mod (version 1, its config) and a staged version 2 with its marker.
void game(){pending_remove(root);CreateDirectoryW(root.c_str(),nullptr);
    make(mods+L"\\Mod\\mod.ini","[mod]\r\nversion=1\r\n");make(mods+L"\\Mod\\config.ini","player");make(mods+L"\\Other\\mod.ini","other");}
std::string stage(const std::wstring&folder=L"Mod",const std::string&extraLine=""){
    std::vector<std::pair<std::string,std::string>>files={{"mod.ini","[mod]\r\nversion=2\r\n"},{"config.ini","player"},{"runtime/new.dll",std::string(5000,'d')}};
    std::string m="format=1\nfolder="+narrow(folder)+"\nversion=2\n";
    for(auto&f:files){auto p=f.first;std::replace(p.begin(),p.end(),'/','\\');make(updates+L"\\staged\\"+folder+L"\\"+std::wstring(p.begin(),p.end()),f.second);m+="file="+shaOf(f.second)+" "+std::to_string(f.second.size())+" "+f.first+"\n";}
    m+=extraLine;make(updates+L"\\staged\\"+folder+L".ready",m);return m;}
int wmain(int argc,wchar_t**argv){try{
    check(argc==2,"Fixture path");root=full(argv[1]);mods=root+L"\\mods";updates=root+L"\\mods-updates";
    // 1. A complete staging: swapped in, the old folder kept as previous, the marker gone.
    game();stage();auto r=apply_pending_updates(mods);
    check(r.size()==1&&r[0].applied&&r[0].folder==L"Mod"&&r[0].version==L"2","A complete staging is applied");
    check(text(mods+L"\\Mod\\mod.ini").find("version=2")!=std::string::npos&&text(mods+L"\\Mod\\runtime\\new.dll").size()==5000,"The new folder is in mods");
    check(text(updates+L"\\previous\\Mod\\mod.ini").find("version=1")!=std::string::npos,"The replaced folder waits in previous");
    check(!pending_exists(updates+L"\\staged\\Mod")&&!pending_exists(updates+L"\\staged\\Mod.ready"),"Staging and marker are gone");
    check(text(mods+L"\\Other\\mod.ini")=="other","Other mods are untouched");
    check(apply_pending_updates(mods).empty(),"Nothing to do at the following start");
    // 2. The next update replaces the older previous copy.
    stage();r=apply_pending_updates(mods);
    check(r.size()==1&&r[0].applied&&text(updates+L"\\previous\\Mod\\mod.ini").find("version=2")!=std::string::npos,"An older previous copy is replaced");
    // 3. Refused stagings change nothing in mods and are removed.
    struct Bad{const char*what;std::function<void()>spoil;};
    Bad bads[]={
        {"A changed file",[]{make(updates+L"\\staged\\Mod\\mod.ini","[mod]\r\nversion=3\r\n");}},
        {"An extra file",[]{make(updates+L"\\staged\\Mod\\extra.txt","x");}},
        {"A missing file",[]{check(DeleteFileW((updates+L"\\staged\\Mod\\config.ini").c_str())!=0,"Fixture delete");}},
        {"A marker of another folder",[]{auto m=text(updates+L"\\staged\\Mod.ready");m.replace(m.find("folder=Mod"),10,"folder=Oth");make(updates+L"\\staged\\Mod.ready",m);}},
        {"An unknown marker key",[]{auto m=text(updates+L"\\staged\\Mod.ready");make(updates+L"\\staged\\Mod.ready",m+"run=cmd\n");}},
        {"A marker path leaving the folder",[]{auto m=text(updates+L"\\staged\\Mod.ready");m.replace(m.find(" mod.ini"),8," ..\\x.ini");make(updates+L"\\staged\\Mod.ready",m);}},
        {"No mod.ini",[]{auto m=text(updates+L"\\staged\\Mod.ready");auto a=m.find("file=");auto b=m.find('\n',a);m.erase(a,b-a+1);make(updates+L"\\staged\\Mod.ready",m);DeleteFileW((updates+L"\\staged\\Mod\\mod.ini").c_str());}},
    };
    for(auto&b:bads){game();stage();b.spoil();r=apply_pending_updates(mods);
        check(r.size()==1&&!r[0].applied&&!r[0].detail.empty(),b.what);
        check(text(mods+L"\\Mod\\mod.ini").find("version=1")!=std::string::npos,"Refused: the mod's folder is unchanged");
        check(!pending_exists(updates+L"\\staged\\Mod")&&!pending_exists(updates+L"\\staged\\Mod.ready"),"Refused: the staging is removed");}
    // 4. A marker for a mod that isn't installed: never a new install.
    game();stage(L"NewMod");r=apply_pending_updates(mods);
    check(r.size()==1&&!r[0].applied&&!pending_exists(mods+L"\\NewMod"),"A staging for an uninstalled mod is refused");
    // 5. Odd marker names are refused without touching anything.
    game();make(updates+L"\\staged\\ .ready","format=1\n");make(updates+L"\\staged\\Mod.ready.tmp","half");r=apply_pending_updates(mods);
    check(r.size()==1&&!r[0].applied&&pending_exists(updates+L"\\staged\\Mod.ready.tmp"),"A name with a space is refused; a .tmp marker is not one");
    // 6. Crash between the two renames: mods\Mod gone, previous there. Complete staging: finished. Incomplete: put back.
    game();stage();CreateDirectoryW((updates+L"\\previous").c_str(),nullptr);
    check(MoveFileExW((mods+L"\\Mod").c_str(),(updates+L"\\previous\\Mod").c_str(),0)!=0,"Fixture: the first rename only");
    r=apply_pending_updates(mods);
    check(r.size()==1&&r[0].applied&&text(mods+L"\\Mod\\mod.ini").find("version=2")!=std::string::npos,"A swap cut short is finished");
    game();stage();pending_remove(updates+L"\\previous");CreateDirectoryW((updates+L"\\previous").c_str(),nullptr);
    check(MoveFileExW((mods+L"\\Mod").c_str(),(updates+L"\\previous\\Mod").c_str(),0)!=0,"Fixture: the first rename only (2)");
    make(updates+L"\\staged\\Mod\\extra.txt","x");r=apply_pending_updates(mods);
    check(r.size()==1&&!r[0].applied&&text(mods+L"\\Mod\\mod.ini").find("version=1")!=std::string::npos,"An incomplete staging after a cut-short swap: the previous copy goes back");
    // 7. The mod's folder in use (a file held open without delete sharing): kept as it was, staging kept for the next start.
    game();stage();{Handle held(CreateFileW((mods+L"\\Mod\\config.ini").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
        r=apply_pending_updates(mods);
        check(r.size()==1&&!r[0].applied&&text(mods+L"\\Mod\\mod.ini").find("version=1")!=std::string::npos,"A folder in use is left as it was");
        check(pending_exists(updates+L"\\staged\\Mod.ready"),"The staging waits for the next start");}
    r=apply_pending_updates(mods);check(r.size()==1&&r[0].applied,"Applied at the next start");
    // 8. A link inside the staging is refused (and removing it never touches the target).
    game();stage();
    if(CreateSymbolicLinkW((updates+L"\\staged\\Mod\\link").c_str(),(mods+L"\\Other").c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|0x2)){
        r=apply_pending_updates(mods);check(r.size()==1&&!r[0].applied,"A link in the staging is refused");
        check(text(mods+L"\\Other\\mod.ini")=="other","The link's target is untouched");}
    else std::cout<<"(symbolic links need Developer Mode here: the link case was skipped)\n";
    // 9. No mods-updates folder: nothing.
    game();check(apply_pending_updates(mods).empty(),"No staging folder: nothing to do");
    pending_remove(root);
    std::cout<<"pending updates: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
