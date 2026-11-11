#include "bootstrap_policy.h"
#include "chaogarage_release.h"
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>

namespace ssasr {
const uint8_t kGameSha256[32] = {
    0xc1,0x10,0xb4,0xd3,0xb9,0x72,0x93,0x17,0xa9,0x3e,0x95,0x6f,0x1a,0x49,0xbe,0x98,
    0x9c,0x7a,0xd3,0xc2,0xaa,0xba,0x7c,0x54,0xc4,0x13,0x19,0xc1,0x04,0x38,0x1b,0x03
};
bool SupportedMetadata(const BYTE* image, size_t size) {
    if (!image || size < sizeof(IMAGE_DOS_HEADER)) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x400) return false;
    const size_t offset = static_cast<size_t>(dos->e_lfanew);
    if (offset + sizeof(IMAGE_NT_HEADERS32) > size) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + offset);
    return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
        nt->FileHeader.TimeDateStamp == 0x4B700995 && nt->FileHeader.NumberOfSections == 6 &&
        nt->FileHeader.SizeOfOptionalHeader == sizeof(IMAGE_OPTIONAL_HEADER32) &&
        nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
        nt->OptionalHeader.ImageBase == kGameBase && nt->OptionalHeader.SizeOfImage == kImageSize &&
        nt->OptionalHeader.AddressOfEntryPoint == 0xAF82ED &&
        nt->OptionalHeader.NumberOfRvaAndSizes == IMAGE_NUMBEROF_DIRECTORY_ENTRIES &&
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress == 0 &&
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress == 0x2E71C4 &&
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size == 0x118 &&
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress == 0x27D000 &&
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size == 0x2CC;
}
bool SupportedStartupCaller(const BYTE* image, size_t size, uintptr_t caller) {
    if (reinterpret_cast<uintptr_t>(image) != kGameBase || caller != kGameBase+kStartupReturnRva ||
        size < kStartupReturnRva+7) return false;
    return SupportedStartupSpan(reinterpret_cast<uintptr_t>(image),caller,image+0x248F31,size-0x248F31);
}
bool SupportedStartupSpan(uintptr_t image_base,uintptr_t caller,const BYTE* span,size_t size) {
    if (image_base!=kGameBase || caller!=kGameBase+kStartupReturnRva || !span) return false;
    /* Full contiguous native call/prologue and continuation, after Steam has
       decrypted the game .text. Never accept the later CRT IO helper caller. */
    static const BYTE before[] = {
        0x6a,0x60,0x68,0xc8,0x6b,0x6e,0x00,0xe8,0x1f,0x0c,0x00,0x00,
        0x83,0x65,0xfc,0x00,0x8d,0x45,0x90,0x50,0xff,0x15,0x28,0xd1,0x67,0x00,
        0xc7,0x45,0xfc,0xfe,0xff,0xff,0xff
    };
    return size>=sizeof(before) && memcmp(span,before,sizeof(before)) == 0;
}
bool EqualSha256(const uint8_t hash[32]) {
    unsigned difference = 0;
    for (size_t i=0;i<32;++i) difference |= hash[i]^kGameSha256[i];
    return difference == 0;
}
bool EqualNormalizedSha256(const uint8_t hash[32]) {
    /* The Steam exe has the large-address bit clear and a zero checksum, so its normalized hash is its own hash. */
    return EqualSha256(hash);
}
bool LocateExePatchFields(const BYTE* header, size_t size, ExePatchFields& fields) {
    fields = ExePatchFields();
    if (!header || size < sizeof(IMAGE_DOS_HEADER)) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(header);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x400) return false;
    const uint64_t nt = static_cast<uint64_t>(dos->e_lfanew);
    /* Signature(4) + FileHeader(20) + the optional header up to and including CheckSum (64 + 4 bytes) */
    if (nt + 4 + 20 + 68 > size) return false;
    uint32_t signature; memcpy(&signature, header + nt, 4);
    uint16_t optionalMagic; memcpy(&optionalMagic, header + nt + 24, 2);
    uint16_t optionalSize; memcpy(&optionalSize, header + nt + 4 + 16, 2);
    if (signature != IMAGE_NT_SIGNATURE || optionalMagic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || optionalSize < 68) return false;
    uint16_t characteristics; memcpy(&characteristics, header + nt + 4 + 18, 2);
    fields.valid = true;
    fields.characteristics = nt + 4 + 18;
    fields.checksum = nt + 24 + 64;
    fields.largeAddressBit = (characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
    return true;
}
void NormalizeExeChunk(const ExePatchFields& fields, uint64_t offset, BYTE* chunk, size_t size) {
    if (!fields.valid || !chunk) return;
    auto inside = [&](uint64_t at) { return at >= offset && at - offset < size; };
    if (inside(fields.characteristics)) chunk[fields.characteristics - offset] &= static_cast<BYTE>(~(IMAGE_FILE_LARGE_ADDRESS_AWARE & 0xFF));
    for (uint64_t i = 0; i < 4; ++i) if (inside(fields.checksum + i)) chunk[fields.checksum + i - offset] = 0;
}
bool HashExecutableFile(const wchar_t* path, bool normalized, uint8_t hash[32], bool* largeAddressBit) {
    if (largeAddressBit) *largeAddressBit = false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    ExePatchFields fields;
    bool okay = true;
    if (normalized) {
        BYTE header[1024]; DWORD got = 0; LARGE_INTEGER zero{};
        okay = ReadFile(file, header, sizeof(header), &got, nullptr) && LocateExePatchFields(header, got, fields) &&
               SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) != 0;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE digest = nullptr;
    if (okay) okay = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (okay) okay = BCryptCreateHash(algorithm, &digest, nullptr, 0, nullptr, 0, 0) >= 0;
    BYTE data[16384]; DWORD count = 0; uint64_t offset = 0;
    while (okay) {
        if (!ReadFile(file, data, sizeof(data), &count, nullptr)) { okay = false; break; }
        if (!count) break;
        if (normalized) NormalizeExeChunk(fields, offset, data, count);
        offset += count;
        if (BCryptHashData(digest, data, count, 0) < 0) { okay = false; break; }
    }
    if (okay) okay = BCryptFinishHash(digest, hash, 32, 0) >= 0;
    if (digest) BCryptDestroyHash(digest);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (okay && largeAddressBit) *largeAddressBit = fields.largeAddressBit;
    return okay;
}
/* Order: nothing is said when the proxy is not next to the game or there are no mods
   (an unmodded game must start exactly as before). From there on every refusal is
   explained once; when the host was called it explains its own refusals. */
InitResult Initialize(InitEnvironment& environment) {
    if (!environment.ResolveLocation()) return InitResult::InvalidLocation;
    if (!environment.HasCandidateMod()) return InitResult::NoMods;
    uint8_t hash[32]{};
    if (!environment.HashExecutable(hash)) { environment.Warn(Warning::ExecutableUnreadable,0); return InitResult::UnknownExecutable; }
    if (!EqualSha256(hash)) {
        /* Only the Steam exe with the 4 GB patch is accepted besides the Steam exe itself: the bit must be set, and every
           other byte must match (a checksum change alone, with the bit clear, is not the patch and stays refused). */
        uint8_t normalized[32]{};bool largeAddress=false;
        if (!(environment.HashExecutableNormalized(normalized,largeAddress) && largeAddress && EqualNormalizedSha256(normalized))) {
            environment.Warn(Warning::UnsupportedExecutable,0); return InitResult::UnknownExecutable;
        }
        environment.Note(Notice::LargeAddressPatchAccepted);
        memcpy(hash,kGameSha256,32);   /* the build identity the host reports to mods (their "target" is descriptive only) */
    }
    DWORD error=0;
    switch (environment.LoadHost(error)) {
    case HostLoad::Loaded: break;
    case HostLoad::NotFound: environment.Warn(Warning::HostMissing,error); return InitResult::HostUnavailable;
    case HostLoad::LoadFailed: environment.Warn(Warning::HostNotLoadable,error); return InitResult::HostUnavailable;
    case HostLoad::NotALoader: environment.Warn(Warning::HostNotALoader,0); return InitResult::HostUnavailable;
    }
    switch (environment.CheckPair()) {
    case Pairing::Match: break;
    case Pairing::Older: environment.Warn(Warning::HostOlder,0); return InitResult::PairMismatch;
    case Pairing::OtherRelease: environment.Warn(Warning::HostOtherRelease,0); return InitResult::PairMismatch;
    case Pairing::InvalidInfo: environment.Warn(Warning::HostInvalid,0); return InitResult::PairMismatch;
    }
    const HRESULT result=environment.InitializeHost(hash);
    if (result==SSASR_HOST_ROLLBACK_FAILED) return InitResult::UnrecoverablePartialPublication;
    if (result==S_FALSE) return InitResult::NoMods;
    if (SUCCEEDED(result)) return InitResult::Loaded;
    if (result!=CHAOGARAGE_HOST_NOT_STARTED_WARNED) environment.Warn(Warning::HostUnexpected,static_cast<DWORD>(result));
    return InitResult::HostRejected;
}
const wchar_t* WarningTitle() { return L"ChaoGarage " CHAOGARAGE_RELEASE_W L" - mods were not started"; }
std::wstring CleanRelease(const wchar_t* text, size_t readable) {
    std::wstring out;
    for (size_t i=0;text && i<readable && text[i];++i) {
        if (out.size()==32) { out+=L"..."; break; }
        out+=(text[i]>L' ' && text[i]<=L'~') ? text[i] : L'?';
    }
    return out;
}
std::wstring WarningText(Warning warning, DWORD code, const std::wstring& host_release) {
    const std::wstring both = L"Install both ChaoGarage.dll and dinput8.dll from the same ChaoGarage release:\r\n" CHAOGARAGE_RELEASES_URL_W;
    wchar_t number[32]{};
    std::wstring text = L"The game will start without mods.\r\n\r\n";
    switch (warning) {
    case Warning::ExecutableUnreadable:
        text += L"The game's executable could not be read to confirm its version, so the mods were not started.";
        break;
    case Warning::UnsupportedExecutable:
        text += L"This game executable is not the version ChaoGarage supports, so the mods were not started.\r\n"
                L"ChaoGarage " CHAOGARAGE_RELEASE_W L" supports the Steam PC release of Sonic & SEGA All-Stars Racing "
                L"(executable SHA-256 c110b4d3...04381b03), also with the 4 GB (large address) patch and nothing else changed. Any other changed executable is not supported. "
                L"Updating the loader does not change this.";
        break;
    case Warning::HostMissing:
        text += L"There are mods in the mods folder, but ChaoGarage.dll is not next to the game's executable.\r\n\r\n" + both;
        break;
    case Warning::HostNotLoadable:
        swprintf_s(number, L"%lu", static_cast<unsigned long>(code));
        text += std::wstring(L"ChaoGarage.dll could not be loaded (Windows error ") + number + L"). It may be damaged or not the 32-bit loader file.\r\n\r\n" + both;
        break;
    case Warning::HostNotALoader:
        text += L"ChaoGarage.dll is not a ChaoGarage loader file (it has no SsasrModsHostInitialize entry point).\r\n\r\n" + both;
        break;
    case Warning::HostOlder:
        text += L"ChaoGarage.dll and dinput8.dll are from different releases: dinput8.dll is release " CHAOGARAGE_RELEASE_W
                L", ChaoGarage.dll is from an earlier release (before 1.1.0).\r\n\r\n" + both;
        break;
    case Warning::HostOtherRelease:
        text += L"ChaoGarage.dll and dinput8.dll are from different releases: dinput8.dll is release " CHAOGARAGE_RELEASE_W
                L", ChaoGarage.dll is release " + (host_release.empty() ? std::wstring(L"(unnamed)") : host_release) + L".\r\n\r\n" + both;
        break;
    case Warning::HostInvalid:
        text += L"ChaoGarage.dll reports release information this dinput8.dll cannot read; the file may be damaged.\r\n\r\n" + both;
        break;
    case Warning::HostUnexpected:
        swprintf_s(number, L"0x%08lX", static_cast<unsigned long>(code));
        text += std::wstring(L"ChaoGarage.dll reported an unexpected error (") + number + L"). Details may be in mods\\SsasrMods.log.";
        break;
    }
    text += L"\r\n\r\nPress Ctrl+C to copy this message. Nothing was downloaded or opened.";
    return text;
}
IatResult ReplaceIat(IatOperations& operations, void* volatile* slot, void* expected, void* replacement) {
    DWORD protection = 0;
    if (!operations.Writable(const_cast<void*>(reinterpret_cast<const volatile void*>(slot)),protection))
        return IatResult::ProtectedFailure;
    void* previous = operations.CompareExchange(slot,replacement,expected);
    if (!operations.RestoreProtection(const_cast<void*>(reinterpret_cast<const volatile void*>(slot)),protection)) {
        /* While still writable, undo only our exact write. Never overwrite a
           foreign writer. A protection failure disables host initialization. */
        if (previous == expected) operations.CompareExchange(slot,expected,replacement);
        operations.RestoreProtection(const_cast<void*>(reinterpret_cast<const volatile void*>(slot)),protection);
        return IatResult::RestoreFailure;
    }
    return previous == expected ? IatResult::Replaced : IatResult::ForeignPointer;
}
}
