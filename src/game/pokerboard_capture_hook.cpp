#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

extern "C" __declspec(dllexport) uint32_t __fastcall BJ3PokerBoard30CHook(
    void* board, void* unused_edx, void* source_candidate,
    uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5,
    uint32_t arg6) noexcept;
extern "C" __declspec(dllexport) uint32_t __fastcall BJ3PokerBoard188Hook(
    void* board, void* unused_edx) noexcept;
extern "C" __declspec(dllexport) uint32_t __fastcall BJ3PokerBoard58ResetHook(
    void* board, void* unused_edx) noexcept;

extern "C" DWORD WINAPI BJ3InstallWaitForSixHook(LPVOID) noexcept;
extern "C" DWORD WINAPI BJ3InstallGameStatsPersistence(LPVOID) noexcept;

extern "C" uint32_t __fastcall BJ3PokerCompletedPair(void*,void*,void*) noexcept;
extern "C" uint32_t __fastcall BJ3PokerCancelledPair(void*,void*,void*) noexcept;
extern "C" uint32_t __fastcall BJ3PokerRetireSpecial(void*,void*,void*) noexcept;
extern "C" uint32_t __fastcall BJ3PokerSpecialUse(void*,void*,void*,void*) noexcept;
static DWORD InstallPairSlot(uintptr_t address, LONG original, LONG hook) noexcept {
    volatile LONG* slot=reinterpret_cast<volatile LONG*>(address);
    if (*slot==hook) return 1;
    if (*slot!=original) return 0;
    DWORD old=0, ignored=0;
    if (!VirtualProtect(const_cast<LONG*>(slot),4,PAGE_READWRITE,&old)) return 0;
    const LONG replaced=InterlockedCompareExchange(slot,hook,original);
    const BOOL restored=VirtualProtect(const_cast<LONG*>(slot),4,old,&ignored);
    return replaced==original && restored && *slot==hook ? 1 : 0;
}

namespace {

const uintptr_t kImageBase = 0x00400000u;
const uintptr_t kPokerBoard58Slot = 0x00811874u;
const LONG kPokerBoard58Original = 0x006C3E50;
const uintptr_t kPokerBoard188Slot = 0x008119A4u;
const LONG kPokerBoard188Original = 0x006B11B0;
const uintptr_t kPokerBoard30CSlot = 0x00811B28u;
const LONG kPokerBoard30COriginal = 0x006C2660;

}

extern "C" __declspec(dllexport) DWORD WINAPI
BJ3InstallPokerBoardSidecarResetHook(LPVOID unused) noexcept {
    (void)unused;
    if (BJ3InstallWaitForSixHook(nullptr) != 1) return 16;
    if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(kImageBase)) return 11;

    volatile LONG* slot = reinterpret_cast<volatile LONG*>(kPokerBoard58Slot);
    const LONG hook = static_cast<LONG>(reinterpret_cast<uintptr_t>(
        &BJ3PokerBoard58ResetHook));
    const LONG current = *slot;
    if (current == hook) return 1;
    if (current != kPokerBoard58Original) return 12;

    DWORD old_protection = 0;
    if (!VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG),
            PAGE_READWRITE, &old_protection)) return 13;
    const LONG replaced = InterlockedCompareExchange(slot, hook,
        kPokerBoard58Original);
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG),
        old_protection, &ignored);
    if (replaced != kPokerBoard58Original) return 14;
    return restored ? 1 : 15;
}

extern "C" __declspec(dllexport) DWORD WINAPI
BJ3InstallPokerBoardCountFiveSelectionHook(LPVOID unused) noexcept {
    (void)unused;
    if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(kImageBase)) return 11;

    volatile LONG* slot = reinterpret_cast<volatile LONG*>(kPokerBoard188Slot);
    const LONG hook = static_cast<LONG>(reinterpret_cast<uintptr_t>(
        &BJ3PokerBoard188Hook));
    const LONG current = *slot;
    if (current == hook) return 1;
    if (current != kPokerBoard188Original) return 12;

    DWORD old_protection = 0;
    if (!VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG),
            PAGE_READWRITE, &old_protection)) return 13;
    const LONG replaced = InterlockedCompareExchange(slot, hook,
        kPokerBoard188Original);
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG),
        old_protection, &ignored);
    if (replaced != kPokerBoard188Original) return 14;
    return restored ? 1 : 15;
}

// Called by the same-bitness startup launcher while the game threads remain
// suspended. This forwards every native call and adds only the sidecar capture.
extern "C" __declspec(dllexport) DWORD WINAPI
BJ3InstallPokerBoardSixthCardCaptureHook(LPVOID unused) noexcept {
    if(BJ3InstallGameStatsPersistence(nullptr)!=1)return 20;
    (void)unused;
    if (InstallPairSlot(0x00811AE8u,0x006B9B80,
            static_cast<LONG>(reinterpret_cast<uintptr_t>(&BJ3PokerSpecialUse)))!=1) return 19;
    if (InstallPairSlot(0x00811B2Cu,0x006AF8E0,
            static_cast<LONG>(reinterpret_cast<uintptr_t>(&BJ3PokerRetireSpecial)))!=1) return 18;
    if (InstallPairSlot(0x00811B18u,0x006B9BD0,
            static_cast<LONG>(reinterpret_cast<uintptr_t>(&BJ3PokerCompletedPair)))!=1) return 16;
    if (InstallPairSlot(0x00811B1Cu,0x006B03B0,
            static_cast<LONG>(reinterpret_cast<uintptr_t>(&BJ3PokerCancelledPair)))!=1) return 17;
    if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(kImageBase)) return 11;

    volatile LONG* slot = reinterpret_cast<volatile LONG*>(kPokerBoard30CSlot);
    const LONG hook = static_cast<LONG>(reinterpret_cast<uintptr_t>(
        &BJ3PokerBoard30CHook));
    const LONG current = *slot;
    if (current == hook) return 1;
    if (current != kPokerBoard30COriginal) return 12;

    DWORD old_protection = 0;
    if (!VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG),
            PAGE_READWRITE, &old_protection)) return 13;
    const LONG replaced = InterlockedCompareExchange(slot, hook,
        kPokerBoard30COriginal);
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG),
        old_protection, &ignored);
    if (replaced != kPokerBoard30COriginal) return 14;
    return restored ? 1 : 15;
}
