// Lets the fix load as dsound.dll from the game folder (the game imports DirectSound on both
// builds), next to a mod that already uses dinput8.dll. Every export jumps to the same function
// in the system's dsound.dll, loaded on the first call.

#include <windows.h>

namespace {

HMODULE g_dsound = nullptr;

extern "C" FARPROC __cdecl Resolve(const char* name) {
    if (!g_dsound) {
        char path[MAX_PATH];
        UINT n = GetSystemDirectoryA(path, MAX_PATH);
        if (n && n < MAX_PATH - 12) {
            lstrcatA(path, "\\dsound.dll");
            g_dsound = LoadLibraryA(path);
        }
    }
    FARPROC fn = g_dsound ? GetProcAddress(g_dsound, name) : nullptr;
    if (!fn) {
        FatalAppExitA(0, "dsound.dll (HitmanGiveAllFix): the system's dsound.dll could not be loaded.");
    }
    return fn;
}

}  // namespace

#define FORWARD(name)                                                        \
    extern "C" const char kName_##name[] = #name;                            \
    extern "C" __declspec(naked) void Proxy_##name() {                       \
        __asm push offset kName_##name                                       \
        __asm call Resolve                                                   \
        __asm add esp, 4                                                     \
        __asm jmp eax                                                        \
    }

FORWARD(DirectSoundCreate)
FORWARD(DirectSoundEnumerateA)
FORWARD(DirectSoundEnumerateW)
FORWARD(DllCanUnloadNow)
FORWARD(DllGetClassObject)
FORWARD(DirectSoundCaptureCreate)
FORWARD(DirectSoundCaptureEnumerateA)
FORWARD(DirectSoundCaptureEnumerateW)
FORWARD(GetDeviceID)
FORWARD(DirectSoundFullDuplexCreate)
FORWARD(DirectSoundCreate8)
FORWARD(DirectSoundCaptureCreate8)
