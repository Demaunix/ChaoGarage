// The production proxy code (proxy.cpp compiled into this program) loading the host file next to it and
// deciding whether the pair matches. Run from a folder holding the shipped ChaoGarage.dll (expect "match"),
// a host from before 1.1.0 (expect "older"), no ChaoGarage.dll ("missing") or a DLL without the entry point
// ("not-a-loader"). The host is loaded but never initialized; no game is involved.
#include "proxy.cpp"
#include <cstdio>
int main(int argc,char**argv) {
    if (argc!=2) return 1;
    const std::string expected=argv[1];
    g_self=GetModuleHandleW(nullptr);
    WindowsEnvironment environment;
    if (!environment.ResolveLocation()) return 2;
    DWORD error=0;
    const auto load=environment.LoadHost(error);
    if (GetModuleHandleW(L"SsasrModsHost.dll")) return 4;   /* only the production file name is ever loaded */
    std::string got;
    switch (load) {
    case ssasr::HostLoad::NotFound: got="missing"; break;
    case ssasr::HostLoad::LoadFailed: got="not-loadable"; break;
    case ssasr::HostLoad::NotALoader: got="not-a-loader"; break;
    case ssasr::HostLoad::Loaded:
        switch (environment.CheckPair()) {
        case ssasr::Pairing::Match: got="match"; break;
        case ssasr::Pairing::Older: got="older"; break;
        case ssasr::Pairing::OtherRelease: got="other-release"; break;
        case ssasr::Pairing::InvalidInfo: got="invalid-info"; break;
        }
        {
            HMODULE host=GetModuleHandleW(L"ChaoGarage.dll");
            std::wstring actual,folder,executable;
            if (!host || !ModulePath(host,actual) || !ModulePath(nullptr,executable) || !ParentPath(executable,folder)) return 6;
            if (actual!=folder+L"\\ChaoGarage.dll") return 7;
        }
        break;
    }
    printf("%s host pairing: expected %s, got %s\n",got==expected?"PASS":"FAIL",expected.c_str(),got.c_str());
    return got==expected?0:3;
}
