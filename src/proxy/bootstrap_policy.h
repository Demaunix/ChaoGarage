#pragma once
#include "bootstrap_api.h"
#include <stddef.h>
#include <string>

namespace ssasr {
constexpr uintptr_t kGameBase = 0x400000;
constexpr DWORD kImageSize = 0xB4E000;
constexpr DWORD kGetStartupInfoIatRva = 0x27D128;
constexpr DWORD kStartupCallRva = 0x248F45;
constexpr DWORD kStartupReturnRva = 0x248F4B;
extern const uint8_t kGameSha256[32];

bool SupportedMetadata(const BYTE* image, size_t readable_size);
bool SupportedStartupCaller(const BYTE* image, size_t readable_size, uintptr_t caller);
bool SupportedStartupSpan(uintptr_t image_base,uintptr_t caller,const BYTE* span,size_t size);
bool EqualSha256(const uint8_t actual[32]);

/* 1.3.2: the Steam exe with only the 4 GB (large address) patch. That patch changes exactly two things in the file: the
   IMAGE_FILE_LARGE_ADDRESS_AWARE bit (0x20) of the PE "Characteristics" and the optional header's "CheckSum". The exe is
   accepted when it is byte for byte the Steam exe except for those, the bit is set, and nothing else differs: its SHA-256
   with the bit cleared and the checksum zeroed equals the Steam exe's own (which has the bit clear and a zero checksum, so
   the pinned value is kGameSha256). */
struct ExePatchFields {
    bool valid=false;
    uint64_t characteristics=0;     /* file offset of the Characteristics WORD */
    uint64_t checksum=0;            /* file offset of the CheckSum DWORD */
    bool largeAddressBit=false;     /* the bit was set in the file */
};
/* `header` = the first bytes of the file (at least e_lfanew + 92 of them). */
bool LocateExePatchFields(const BYTE* header, size_t size, ExePatchFields& fields);
/* Clears the bit and zeroes the checksum in `chunk`, which holds the file bytes from `offset` on. Any chunking gives the same bytes. */
void NormalizeExeChunk(const ExePatchFields& fields, uint64_t offset, BYTE* chunk, size_t size);
/* True when the normalized hash is the Steam exe's. */
bool EqualNormalizedSha256(const uint8_t actual[32]);
/* SHA-256 of the exe file at `path`, read in chunks (the proxy's production code, also called by the tests). With `normalized`
   the large-address bit is cleared and the checksum zeroed on the way, and `largeAddressBit` says whether the bit was set.
   False when the file cannot be read, or (normalized) is not a PE32 file the two fields can be placed in. */
bool HashExecutableFile(const wchar_t* path, bool normalized, uint8_t hash[32], bool* largeAddressBit);

enum class InitResult : DWORD {
    NeverCalled, NoMods, UnknownExecutable, InvalidLocation, HostUnavailable,
    HostRejected, Loaded, UnrecoverablePartialPublication, PairMismatch
};
/* A fact worth one log line, not a warning: nothing is shown to the player. */
enum class Notice { LargeAddressPatchAccepted };
enum class HostLoad { Loaded, NotFound, LoadFailed, NotALoader };
enum class Pairing { Match, Older, OtherRelease, InvalidInfo };
/* Situations in which the proxy itself tells the player that mods were not started.
   At most one per start; the host shows its own warning for everything it checks. */
enum class Warning {
    ExecutableUnreadable, UnsupportedExecutable, HostMissing, HostNotLoadable, HostNotALoader,
    HostOlder, HostOtherRelease, HostInvalid, HostUnexpected
};

/* Dependency injection keeps the exact production decision order executable
   in native fixtures without loading the game or altering an installed file. */
struct InitEnvironment {
    virtual bool ResolveLocation() = 0;
    virtual bool HasCandidateMod() = 0;
    virtual bool HashExecutable(uint8_t hash[32]) = 0;
    /* SHA-256 of the exe with the large-address bit cleared and the checksum zeroed; `largeAddressBit` says whether the bit was set.
       False when the file cannot be read or is not a PE file this loader can locate the two fields in. */
    virtual bool HashExecutableNormalized(uint8_t hash[32], bool& largeAddressBit) = 0;
    virtual HostLoad LoadHost(DWORD& error) = 0;
    virtual Pairing CheckPair() = 0;
    virtual HRESULT InitializeHost(const uint8_t hash[32]) = 0;
    virtual void Warn(Warning warning, DWORD code) = 0;
    virtual void Note(Notice notice) = 0;
    virtual ~InitEnvironment() = default;
};
InitResult Initialize(InitEnvironment& environment);

/* The warning text: fixed sentences, numbers and the host's release name (already
   limited to 32 printable characters by the caller). Nothing is opened. */
const wchar_t* WarningTitle();
std::wstring WarningText(Warning warning, DWORD code, const std::wstring& host_release);
/* Keeps a release name to at most 32 printable ASCII characters; anything else becomes '?'. */
std::wstring CleanRelease(const wchar_t* text, size_t readable);

struct IatOperations {
    virtual bool Writable(void* address, DWORD& old_protection) = 0;
    virtual void* CompareExchange(void* volatile* address, void* replacement, void* expected) = 0;
    virtual bool RestoreProtection(void* address, DWORD old_protection) = 0;
    virtual ~IatOperations() = default;
};
enum class IatResult { Replaced, ProtectedFailure, ForeignPointer, RestoreFailure };
IatResult ReplaceIat(IatOperations& operations, void* volatile* slot, void* expected, void* replacement);
}
