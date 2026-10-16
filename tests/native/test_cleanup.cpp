#include "legacy_cache.h"
#include <iostream>
using namespace ssasr;
unsigned checks=0;
void check(bool value,const char*message){++checks;require(value,message);}
int wmain(int argc,wchar_t**argv){try{
    check(argc==2,"Fixture path");auto root=full(argv[1]);
    const char bytes[]="Only an authenticated old generated archive may be deleted.";
    Hash h;h.add(bytes,sizeof(bytes));LegacyCache c{root,h.finish(),sizeof(bytes)};
    auto dir=child(root,L".cache\\"+wide(c.hash)),file=child(dir,L"data.bin");
    mkdirs(root,dir);
    auto make=[&](const std::wstring&p,const char*b,DWORD n){Handle f(CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr));require(f.h!=INVALID_HANDLE_VALUE,"Fixture create");write(f.h,b,n);};
    make(file,bytes,sizeof(bytes));
    {Handle f(CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));check(remove_legacy_cache(c)==CacheCleanup::Kept,"In-use cache retained");}
    auto wrongSize=c;wrongSize.bytes++;check(remove_legacy_cache(wrongSize)==CacheCleanup::Kept,"Wrong size retained");
    auto sentinel=child(dir,L"keep.txt");make(sentinel,"keep",4);
    check(remove_legacy_cache(c)==CacheCleanup::Removed,"Authenticated cache removed");
    check(GetFileAttributesW(file.c_str())==INVALID_FILE_ATTRIBUTES,"Archive gone");
    check(GetFileAttributesW(sentinel.c_str())!=INVALID_FILE_ATTRIBUTES,"Other file retained");
    check(remove_legacy_cache(c)==CacheCleanup::Missing,"Repeated cleanup harmless");
    char altered[sizeof(bytes)];memcpy(altered,bytes,sizeof(bytes));altered[0]^=1;make(file,altered,sizeof(altered));
    check(remove_legacy_cache(c)==CacheCleanup::Kept,"Same-size changed data retained");
    check(hash_file(file)!=c.hash,"Changed data still present");
    auto invalid=c;invalid.hash="../outside";check(remove_legacy_cache(invalid)==CacheCleanup::Kept,"Invalid path refused");
    make(file,bytes,sizeof(bytes));check(DeleteFileW(sentinel.c_str())!=0,"Remove fixture sentinel");
    check(remove_legacy_cache(c)==CacheCleanup::Removed,"Second authenticated removal");
    check(GetFileAttributesW(dir.c_str())==INVALID_FILE_ATTRIBUTES,"Only empty signature directory removed");
    mkdirs(root,dir);check(CreateDirectoryW(file.c_str(),nullptr)!=0,"Directory-shaped fixture");
    check(remove_legacy_cache(c)==CacheCleanup::Kept,"Directory never removed as a cache file");
    check(RemoveDirectoryW(file.c_str())&&RemoveDirectoryW(dir.c_str()),"Fixture directory cleanup");
    std::cout<<"PASS "<<checks<<" legacy-cache cleanup checks\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
