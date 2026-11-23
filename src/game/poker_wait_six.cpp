#include "sixth_acquisition.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>

extern "C" uint32_t __stdcall BJ3ShouldWaitForSix(void* board) noexcept;
extern "C" __declspec(dllexport) volatile LONG g_BJ3WaitSixHoldCount = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3WaitSixLastBoard = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3WaitSixInstallState = 0;
static uintptr_t g_wait_continue = 0x006C4193u;
static uintptr_t g_wait_tail = 0x006C4910u;
static const uint8_t kOriginal[6] = {0x8D,0x9D,0x78,0x38,0x00,0x00};

extern "C" uint32_t __stdcall BJ3WaitSixDecision(void* board) noexcept {
    const uint32_t wait = BJ3ShouldWaitForSix(board) || BJ3SixthAcquisitionPending(board);
    if (wait) {
        InterlockedIncrement(&g_BJ3WaitSixHoldCount);
        InterlockedExchange(&g_BJ3WaitSixLastBoard,
            static_cast<LONG>(reinterpret_cast<uintptr_t>(board)));
    }
    return wait;
}

// Entered at the six-byte LEA at 0x006C418D. All temporary registers,
// flags and stack slots are restored before either native continuation.
extern "C" __declspec(naked) void BJ3WaitSixDetour() noexcept {
    __asm {
        pushfd
        pushad
        push ebp
        call BJ3WaitSixDecision
        test eax, eax
        jnz hold_hand
        popad
        popfd
        lea ebx, [ebp + 03878h]
        jmp dword ptr [g_wait_continue]
    hold_hand:
        popad
        popfd
        lea ebx, [ebp + 03878h]
        // Native tail 0x006C499D can read this local.
        mov dword ptr [esp + 070h], ebx
        jmp dword ptr [g_wait_tail]
    }
}

// Startup-only installer. The launcher keeps the game threads suspended.
// The experiment DLL remains mapped until process exit; no live unload.
extern "C" __declspec(dllexport) DWORD WINAPI
BJ3InstallWaitForSixHook(LPVOID unused) noexcept {
    (void)unused;
    if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x00400000u)) return 0;
    uint8_t* site = reinterpret_cast<uint8_t*>(0x006C418Du);
    uint8_t patch[6] = {0xE9,0,0,0,0,0x90};
    const int32_t rel = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(&BJ3WaitSixDetour) - (0x006C418Du + 5));
    memcpy(patch + 1, &rel, 4);
    if (g_BJ3WaitSixInstallState == 1)
        return memcmp(site, patch, 6) == 0 ? 1 : 0;
    if (memcmp(site, kOriginal, 6) != 0) return 0;
    DWORD previous = 0;
    if (!VirtualProtect(site, 6, PAGE_EXECUTE_READWRITE, &previous)) return 0;
    memcpy(site, patch, 6);
    const BOOL flushed = FlushInstructionCache(GetCurrentProcess(), site, 6);
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(site, 6, previous, &ignored);
    const bool matches = memcmp(site, patch, 6) == 0;
    if (flushed && restored && matches) {
        InterlockedExchange(&g_BJ3WaitSixInstallState, 1);
        return 1;
    }
    // Failed startup is terminated by the existing launcher.
    InterlockedExchange(&g_BJ3WaitSixInstallState, -1);
    return 0;
}
