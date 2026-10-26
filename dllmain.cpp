// Hitman: Blood Money "Give All" crash fix, patched in memory (no changes to HitmanBloodMoney.exe).
//
// 1. ZItem::s_ItemMemoryAllocator: the game sets it up on a static 96 KB buffer, which runs out
//    when every weapon is loaded at once. Its static initialiser is changed to pass a 4 MB block
//    from VirtualAlloc instead (only the two push operands change).
// 2. The name lookup that crashed on a corrupt table offset returns "not found" instead when an
//    offset is above 16 MB.
//
// Supports the Steam, GOG and disc (retail patch, 4,366,336-byte exe) builds; on any other build,
// or if any of the expected bytes differ, it changes nothing. Writes HitmanGiveAllFix.log next to
// the game.
//
// Timing: the allocator patch has to land before the game's static initialisers run.
// - Steam: the exe is packed; the unpacker loads the game's DLLs (and ASI loaders) after
//   unpacking, so the code is already there when DllMain runs.
// - Disc: the exe is not packed, so the code is there when DllMain runs.
// - GOG: the exe is SecuROM-encrypted when DllMain runs. msvcr71!_initterm is hooked and the
//   patches go in when the game's CRT startup calls it to run the static initialisers.

#include "pch.h"

#include <intrin.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

struct Build {
    const char* name;
    DWORD linkTime;         // IMAGE_FILE_HEADER::TimeDateStamp of HitmanBloodMoney.exe
    uintptr_t poolInit;     // push 0; push name; push 18000h; push buffer; mov ecx, allocator; call
    uint32_t poolName;      // "ZItem::s_ItemMemoryAllocator"
    uint32_t poolBuffer;    // the static 96 KB buffer
    uint32_t allocator;
    uintptr_t lookupCheck1;    // push ecx; mov ecx,[esp+1Ch]; push edx   (eax = table offset)
    uintptr_t lookupCheck2;    // cmp ebp,ebx; lea esi,[eax+ebx*4-3]      (eax = table offset)
    uintptr_t lookupNotFound;  // pop edi; pop esi; pop ebp; xor eax,eax; pop ebx; add esp,0Ch; ret 8
};

const Build kBuilds[] = {
    {"Steam", 0x447EF98A, 0x73CA80, 0x772FA8, 0x982920, 0x99AE68, 0x464EF9, 0x464F3D, 0x464F92},
    {"GOG", 0x446B55B0, 0x7290F0, 0x75FF68, 0x96E8F0, 0x986E38, 0x465019, 0x46505D, 0x4650B2},
    // From DustyRogue's HitmanBloodMoney_GiveAllCrashFix_v2.ips, made for this exe.
    {"disc", 0x4492B845, 0x73D3E0, 0x773FA8, 0x983F20, 0x99C468, 0x465059, 0x46509D, 0x4650F2},
};

constexpr uint32_t kOldPoolSize = 0x18000;    // 96 KB
constexpr uint32_t kNewPoolSize = 0x400000;   // 4 MB
constexpr uint32_t kMaxTableOffset = 0x1000000;

const uint8_t kCheck1Code[] = {0x51, 0x8B, 0x4C, 0x24, 0x1C, 0x52, 0x03, 0xC8};
const uint8_t kCheck2Code[] = {0x3B, 0xEB, 0x8D, 0x74, 0x98, 0xFD, 0x7D, 0x4D};
const uint8_t kNotFoundCode[] = {0x5F, 0x5E, 0x5D, 0x33, 0xC0, 0x5B, 0x83, 0xC4, 0x0C, 0xC2, 0x08, 0x00};
constexpr size_t kStolen = 6;  // bytes of kCheck1Code / kCheck2Code moved into the check stubs

const Build* g_build = nullptr;
char g_logPath[MAX_PATH];

void Log(const char* format, ...) {
    FILE* f = nullptr;
    if (!g_logPath[0] || fopen_s(&f, g_logPath, "a") != 0 || !f) {
        return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}

// True if [address, address + n) is readable memory holding these bytes.
bool Matches(uintptr_t address, const void* bytes, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
        address + n > reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
        return false;
    }
    return memcmp(reinterpret_cast<const void*>(address), bytes, n) == 0;
}

bool Write(uintptr_t address, const void* bytes, size_t n) {
    DWORD old;
    if (!VirtualProtect(reinterpret_cast<void*>(address), n, PAGE_EXECUTE_READWRITE, &old)) {
        return false;
    }
    memcpy(reinterpret_cast<void*>(address), bytes, n);
    VirtualProtect(reinterpret_cast<void*>(address), n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), n);
    return true;
}

void Put32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
uint32_t Rel32(uintptr_t from, uintptr_t to) { return static_cast<uint32_t>(to - from); }

// The allocator's static initialiser as the build has it.
void PoolInitCode(const Build& b, uint8_t (&code)[22]) {
    uint8_t* p = code;
    *p++ = 0x6A; *p++ = 0x00;                      // push 0
    *p++ = 0x68; Put32(p, b.poolName); p += 4;      // push name
    *p++ = 0x68; Put32(p, kOldPoolSize); p += 4;    // push 18000h
    *p++ = 0x68; Put32(p, b.poolBuffer); p += 4;    // push buffer
    *p++ = 0xB9; Put32(p, b.allocator);             // mov ecx, allocator
}

// The first piece of the code this fix changes that is not as expected, or null if all of it is
// there (the game is the expected build, unpacked / decrypted, and not patched already).
const char* Mismatch(const Build& b) {
    uint8_t poolInit[22];
    PoolInitCode(b, poolInit);
    if (!Matches(b.poolInit, poolInit, sizeof(poolInit))) return "item pool initialiser";
    if (!Matches(b.poolName, "ZItem::s_ItemMemoryAllocator", 29)) return "item pool name";
    if (!Matches(b.lookupCheck1, kCheck1Code, sizeof(kCheck1Code))) return "lookup check 1";
    if (!Matches(b.lookupCheck2, kCheck2Code, sizeof(kCheck2Code))) return "lookup check 2";
    if (!Matches(b.lookupNotFound, kNotFoundCode, sizeof(kNotFoundCode))) return "lookup exit";
    return nullptr;
}

bool CodeReady(const Build& b) { return !Mismatch(b); }

void PatchPool(const Build& b) {
    // The allocator stays zero until its initialiser has run; after that it is too late.
    const uint32_t* allocator = reinterpret_cast<const uint32_t*>(b.allocator);
    if (allocator[3] || allocator[4]) {
        Log("item pool: already set up (buffer %08X, %u bytes), left at 96 KB", allocator[3], allocator[4]);
        return;
    }
    void* pool = VirtualAlloc(nullptr, kNewPoolSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pool) {
        Log("item pool: VirtualAlloc of %u bytes failed (error %lu), left at 96 KB", kNewPoolSize, GetLastError());
        return;
    }
    uint32_t buffer = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pool));
    if (Write(b.poolInit + 8, &kNewPoolSize, 4) && Write(b.poolInit + 13, &buffer, 4)) {
        Log("item pool: %u KB at %08X instead of 96 KB at %08X", kNewPoolSize / 1024, buffer, b.poolBuffer);
    } else {
        Log("item pool: could not write the initialiser (error %lu)", GetLastError());
    }
}

// Writes a stub for site: if eax > 16 MB, go to notFound; else run the 6 bytes at site and jump
// back after them.
void BuildOffsetCheck(uint8_t* stub, uintptr_t site, uintptr_t notFound) {
    uintptr_t s = reinterpret_cast<uintptr_t>(stub);
    uint8_t* p = stub;
    *p++ = 0x3D; Put32(p, kMaxTableOffset); p += 4;                        // cmp eax, 1000000h
    *p++ = 0x0F; *p++ = 0x87; Put32(p, Rel32(s + 11, notFound)); p += 4;   // ja notFound
    memcpy(p, reinterpret_cast<const void*>(site), kStolen); p += kStolen;
    *p++ = 0xE9; Put32(p, Rel32(s + 22, site + kStolen));                  // jmp site+6
}

// Replaces the 6 bytes at site with "jmp stub; nop".
bool JumpTo(uintptr_t site, const uint8_t* stub) {
    uint8_t jump[kStolen] = {0xE9, 0, 0, 0, 0, 0x90};
    Put32(jump + 1, Rel32(site + 5, reinterpret_cast<uintptr_t>(stub)));
    return Write(site, jump, sizeof(jump));
}

void PatchLookup(const Build& b) {
    auto* stubs = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!stubs) {
        Log("lookup checks: VirtualAlloc failed (error %lu)", GetLastError());
        return;
    }
    BuildOffsetCheck(stubs, b.lookupCheck1, b.lookupNotFound);
    BuildOffsetCheck(stubs + 32, b.lookupCheck2, b.lookupNotFound);
    DWORD old;
    VirtualProtect(stubs, 64, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), stubs, 64);
    if (JumpTo(b.lookupCheck1, stubs) && JumpTo(b.lookupCheck2, stubs + 32)) {
        Log("lookup checks: added");
    } else {
        Log("lookup checks: could not write (error %lu)", GetLastError());
    }
}

void Apply() {
    PatchPool(*g_build);
    PatchLookup(*g_build);
}

// --- GOG: wait for SecuROM to decrypt the code -------------------------------------------------
// The game's CRT startup runs its static initialisers through msvcr71!_initterm, after SecuROM
// has decrypted the code. (The first GetCommandLineA call after decryption comes too late: the
// initialisers have run by then.) _initterm only calls each function pointer in a range, so it is
// replaced outright by a jump to a copy that applies the patches first, on the first call made
// once the code is decrypted; then the original bytes go back. DLLs call _initterm during their
// own startup too, earlier; those calls just run their initialisers.

using Initialiser = void(__cdecl*)();
uint8_t* g_initterm = nullptr;
uint8_t g_inittermBytes[5];
volatile LONG g_applied = 0;

void __cdecl InittermHook(Initialiser* begin, Initialiser* end) {
    if (!g_applied && CodeReady(*g_build) && InterlockedExchange(&g_applied, 1) == 0) {
        Log("game code decrypted (_initterm from %08X)", reinterpret_cast<uintptr_t>(_ReturnAddress()));
        Apply();
        Write(reinterpret_cast<uintptr_t>(g_initterm), g_inittermBytes, sizeof(g_inittermBytes));
    }
    for (; begin < end; ++begin) {
        if (*begin) {
            (*begin)();
        }
    }
}

bool HookInitterm() {
    HMODULE crt = GetModuleHandleA("msvcr71.dll");
    g_initterm = crt ? reinterpret_cast<uint8_t*>(GetProcAddress(crt, "_initterm")) : nullptr;
    if (!g_initterm) {
        return false;
    }
    memcpy(g_inittermBytes, g_initterm, sizeof(g_inittermBytes));
    uint8_t jump[5] = {0xE9};
    Put32(jump + 1, Rel32(reinterpret_cast<uintptr_t>(g_initterm) + 5, reinterpret_cast<uintptr_t>(&InittermHook)));
    return Write(reinterpret_cast<uintptr_t>(g_initterm), jump, sizeof(jump));
}

void Start() {
    char path[MAX_PATH];
    if (GetModuleFileNameA(nullptr, path, MAX_PATH)) {
        char* slash = strrchr(path, '\\');
        if (slash) {
            slash[1] = '\0';
            sprintf_s(g_logPath, "%sHitmanGiveAllFix.log", path);
            DeleteFileA(g_logPath);
        }
    }
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleA(nullptr));
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(dos) + dos->e_lfanew);
    DWORD stamp = nt->FileHeader.TimeDateStamp;
    for (const Build& b : kBuilds) {
        if (b.linkTime == stamp) {
            g_build = &b;
        }
    }
    if (!g_build) {
        Log("HitmanGiveAllFix: unknown game build (link time %08lX), nothing changed", stamp);
        return;
    }
    Log("HitmanGiveAllFix: %s build", g_build->name);
    if (CodeReady(*g_build)) {
        g_applied = 1;
        Apply();
    } else if (HookInitterm()) {
        // Normal on GOG. On another build it stays this way if the exe has been patched already
        // (e.g. with the .ips); nothing is changed then.
        Log("waiting for the game code to be decrypted (%s not as expected yet)", Mismatch(*g_build));
    } else {
        Log("could not hook msvcr71!_initterm, nothing changed");
    }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        Start();
    }
    return TRUE;
}
