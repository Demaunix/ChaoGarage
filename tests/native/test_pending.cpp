// 1.3.0: a staged mod update is put in place at start (src/host/pending_updates.h), only when complete, unchanged and made
// for the installed folder; the player's kept files come from the installed folder at the swap.
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
const std::string kInstalledIni="[mod]\r\nversion=1\r\n";
// A fresh game: mods\Mod (version 1, the player's config) and another mod.
void game(){pending_remove(root);CreateDirectoryW(root.c_str(),nullptr);
    make(mods+L"\\Mod\\mod.ini",kInstalledIni);make(mods+L"\\Mod\\config.ini","player");make(mods+L"\\Other\\mod.ini","other");}
// A staged version 2 (with the release's default config.ini) and its marker, made for the installed mod.ini.
std::string stage(const std::wstring&folder=L"Mod",const std::string&replacesIni=kInstalledIni){
    std::vector<std::pair<std::string,std::string>>files={{"mod.ini","[mod]\r\nversion=2\r\n"},{"config.ini","release default"},{"runtime/new.dll",std::string(5000,'d')}};
    std::string m="format=1\nfolder="+narrow(folder)+"\nversion=2\nreplaces="+shaOf(replacesIni)+"\nkeep=config.ini\n";
    for(auto&f:files){auto p=f.first;std::replace(p.begin(),p.end(),'/','\\');make(updates+L"\\staged\\"+folder+L"\\"+std::wstring(p.begin(),p.end()),f.second);m+="file="+shaOf(f.second)+" "+std::to_string(f.second.size())+" "+f.first+"\n";}
    make(updates+L"\\staged\\"+folder+L".ready",m);return m;}
bool isV(int v){return text(mods+L"\\Mod\\mod.ini").find("version="+std::to_string(v))!=std::string::npos;}
bool staged(){return pending_exists(updates+L"\\staged\\Mod");}
bool marker(const wchar_t*ext=L".ready"){return pending_exists(updates+L"\\staged\\Mod"+ext);}
// The first rename only, as a start stopped in the middle would leave it (the journal written first).
void cutAfterFirstMove(){CreateDirectoryW((updates+L"\\previous").c_str(),nullptr);
    check(MoveFileExW((updates+L"\\staged\\Mod.ready").c_str(),(updates+L"\\staged\\Mod.swapping").c_str(),0)!=0,"Fixture: the journal");
    check(MoveFileExW((mods+L"\\Mod").c_str(),(updates+L"\\previous\\Mod").c_str(),0)!=0,"Fixture: the first rename only");}
int wmain(int argc,wchar_t**argv){try{
    check(argc==2,"Fixture path");root=full(argv[1]);mods=root+L"\\mods";updates=root+L"\\mods-updates";
    // 1. A complete staging: swapped in, the player's config kept, the old folder kept as previous, the markers gone.
    game();stage();auto r=apply_pending_updates(mods);
    check(r.size()==1&&r[0].applied&&r[0].folder==L"Mod"&&r[0].version==L"2","A complete staging is applied");
    check(isV(2)&&text(mods+L"\\Mod\\runtime\\new.dll").size()==5000,"The new folder is in mods");
    check(text(mods+L"\\Mod\\config.ini")=="player","The player's config.ini (keep=) comes from the installed folder");
    check(text(updates+L"\\previous\\Mod\\mod.ini")==kInstalledIni,"The replaced folder waits in previous");
    check(!staged()&&!marker()&&!marker(L".swapping"),"Staging, marker and journal are gone");
    check(text(mods+L"\\Other\\mod.ini")=="other","Other mods are untouched");
    check(apply_pending_updates(mods).empty(),"Nothing to do at the following start");
    // 2. The player edits config.ini after the download (e.g. turns updates off): the edit is what goes in.
    game();stage();make(mods+L"\\Mod\\config.ini","player, edited later: Auto=0");r=apply_pending_updates(mods);
    check(r.size()==1&&r[0].applied&&text(mods+L"\\Mod\\config.ini")=="player, edited later: Auto=0","A config edit made after the download is kept");
    // 3. The next update replaces the older previous copy.
    stage(L"Mod","[mod]\r\nversion=2\r\n");r=apply_pending_updates(mods);
    check(r.size()==1&&r[0].applied&&text(updates+L"\\previous\\Mod\\mod.ini").find("version=2")!=std::string::npos,"An older previous copy is replaced");
    // 4. Refused stagings change nothing in mods and are removed.
    struct Bad{const char*what;std::function<void()>spoil;};
    Bad bads[]={
        {"A changed file",[]{make(updates+L"\\staged\\Mod\\mod.ini","[mod]\r\nversion=3\r\n");}},
        {"An extra file",[]{make(updates+L"\\staged\\Mod\\extra.txt","x");}},
        {"A missing file",[]{check(DeleteFileW((updates+L"\\staged\\Mod\\runtime\\new.dll").c_str())!=0,"Fixture delete");}},
        {"A marker of another folder",[]{auto m=text(updates+L"\\staged\\Mod.ready");m.replace(m.find("folder=Mod"),10,"folder=Oth");make(updates+L"\\staged\\Mod.ready",m);}},
        {"An unknown marker key",[]{auto m=text(updates+L"\\staged\\Mod.ready");make(updates+L"\\staged\\Mod.ready",m+"run=cmd\n");}},
        {"A marker path leaving the folder",[]{auto m=text(updates+L"\\staged\\Mod.ready");m.replace(m.find(" mod.ini"),8," ..\\x.ini");make(updates+L"\\staged\\Mod.ready",m);}},
        {"No replaces line",[]{auto m=text(updates+L"\\staged\\Mod.ready");auto a=m.find("replaces=");m.erase(a,m.find('\n',a)-a+1);make(updates+L"\\staged\\Mod.ready",m);}},
        {"The installed mod was replaced by hand after the download",[]{make(mods+L"\\Mod\\mod.ini","[mod]\r\nversion=1.5 (from GameBanana)\r\n");}},
    };
    for(auto&b:bads){game();stage();b.spoil();auto before=text(mods+L"\\Mod\\mod.ini");r=apply_pending_updates(mods);
        check(r.size()==1&&!r[0].applied&&!r[0].detail.empty(),b.what);
        check(text(mods+L"\\Mod\\mod.ini")==before,"Refused: the mod's folder is unchanged");
        check(!staged()&&!marker(),"Refused: the staging is removed");}
    // 5. Uninstalled after the download (the folder deleted), with an older previous copy there: nothing comes back.
    game();stage();r=apply_pending_updates(mods);stage(L"Mod","[mod]\r\nversion=2\r\n");pending_remove(mods+L"\\Mod");
    r=apply_pending_updates(mods);
    check(r.size()==1&&!r[0].applied&&!pending_exists(mods+L"\\Mod"),"An uninstalled mod is not reinstalled");
    check(!staged()&&pending_exists(updates+L"\\previous\\Mod\\mod.ini"),"Its staging is removed; the previous copy is left alone");
    // 6. A staging for a mod that was never installed: never a new install.
    game();stage(L"NewMod");r=apply_pending_updates(mods);
    check(r.size()==1&&!r[0].applied&&!pending_exists(mods+L"\\NewMod"),"A staging for an uninstalled mod is refused");
    // 7. Odd marker names: only the marker goes, never a folder built from the name.
    game();stage();make(updates+L"\\staged\\..ready","x");make(updates+L"\\staged\\...ready","x");make(updates+L"\\staged\\ .ready","x");make(updates+L"\\staged\\Mod.ready.tmp","half");
    r=apply_pending_updates(mods);
    check(isV(2),"A good staging next to odd marker names is still applied");
    check(!pending_exists(updates+L"\\staged\\..ready")&&!pending_exists(updates+L"\\staged\\...ready")&&pending_exists(updates+L"\\previous\\Mod\\mod.ini"),"Odd marker names remove only themselves");
    check(pending_exists(updates+L"\\staged\\Mod.ready.tmp"),"A .tmp marker is not a marker");
    // 8. The journal: a start stopped after the first rename. Complete staging: finished. Incomplete: the previous copy back.
    game();stage();cutAfterFirstMove();r=apply_pending_updates(mods);
    check(r.size()==1&&r[0].applied&&isV(2)&&!marker(L".swapping"),"A swap cut short is finished from the journal");
    game();stage();cutAfterFirstMove();make(updates+L"\\staged\\Mod\\extra.txt","x");r=apply_pending_updates(mods);
    check(r.size()==1&&!r[0].applied&&isV(1)&&!marker(L".swapping")&&!staged(),"An incomplete staging after a cut-short swap: the previous copy goes back");
    // A start stopped after both renames (before the journal went): finished, logged as done.
    game();stage();cutAfterFirstMove();check(MoveFileExW((updates+L"\\staged\\Mod").c_str(),(mods+L"\\Mod").c_str(),0)!=0,"Fixture: the second rename");
    r=apply_pending_updates(mods);check(r.size()==1&&r[0].applied&&isV(2)&&!marker(L".swapping"),"A journal left after both renames is closed as done");
    // A journal with both folders still there (the first rename never happened): an ordinary update again.
    game();stage();check(MoveFileExW((updates+L"\\staged\\Mod.ready").c_str(),(updates+L"\\staged\\Mod.swapping").c_str(),0)!=0,"Fixture: journal only");
    make(updates+L"\\staged\\Mod.ready","stale");r=apply_pending_updates(mods);
    check(r.size()==1&&r[0].applied&&isV(2)&&!marker()&&!marker(L".swapping"),"A journal before any move: applied once (a stale .ready of the same folder is ignored)");
    // 9. The mod's folder in use (a file held open without delete sharing): left as it was, the staging waits.
    game();stage();{Handle held(CreateFileW((mods+L"\\Mod\\mod.ini").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
        r=apply_pending_updates(mods);
        check(r.size()==1&&!r[0].applied&&isV(1)&&marker()&&!marker(L".swapping"),"A folder in use is left as it was; the staging waits with its marker");}
    r=apply_pending_updates(mods);check(r.size()==1&&r[0].applied&&isV(2),"Applied at the next start");
    // 10. The updaters' lock held (a game staging right now): nothing is touched.
    game();stage();{Handle lock(CreateFileW((updates+L"\\lock").c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_FLAG_DELETE_ON_CLOSE,nullptr));
        r=apply_pending_updates(mods);check(r.size()==1&&!r[0].applied&&isV(1)&&marker(),"While another game holds the lock nothing is touched");}
    // 11. Links: in the staging, and a junction at previous: refused, and nothing outside is touched.
    game();stage();
    if(CreateSymbolicLinkW((updates+L"\\staged\\Mod\\link").c_str(),(mods+L"\\Other").c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|0x2)){
        r=apply_pending_updates(mods);check(r.size()==1&&!r[0].applied,"A link in the staging is refused");
        check(text(mods+L"\\Other\\mod.ini")=="other","The link's target is untouched");
        game();stage();make(root+L"\\outside\\Mod\\keep.txt","outside");
        check(CreateSymbolicLinkW((updates+L"\\previous").c_str(),(root+L"\\outside").c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|0x2)!=0,"Fixture: previous is a link");
        r=apply_pending_updates(mods);
        check(r.size()==1&&!r[0].applied&&isV(1),"A link at mods-updates\\previous is refused");
        check(text(root+L"\\outside\\Mod\\keep.txt")=="outside","Nothing behind that link is removed");}
    else std::cout<<"(symbolic links need Developer Mode here: the link cases were skipped)\n";
    // 12. No mods-updates folder: nothing.
    game();check(apply_pending_updates(mods).empty(),"No staging folder: nothing to do");
    pending_remove(root);
    std::cout<<"pending updates: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
