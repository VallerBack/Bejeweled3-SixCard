#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
extern "C" int32_t __stdcall BJ3DangerCompareValue(void*) noexcept;
extern "C" int32_t __stdcall BJ3DangerNativeCaptionIndex(void*) noexcept;
static uintptr_t hint_compare_next=0x006B40D8,hint_name_next=0x006B41C9;
static uintptr_t coin_effect_next=0x006C43B1,bonus_compare_next=0x006C4FCC,flight_scale_next=0x006BB98B;
extern "C" __declspec(naked) void BJ3DangerHintCompareGate() noexcept {
    __asm {
        pushfd
        pushad
        push esi
        call BJ3DangerCompareValue
        mov [esp+01Ch],eax
        popad
        popfd
        jmp dword ptr [hint_compare_next]
    }
}
extern "C" __declspec(naked) void BJ3DangerHintNameGate() noexcept {
    __asm {
        pushfd
        pushad
        push esi
        call BJ3DangerNativeCaptionIndex
        mov [esp+01Ch],eax
        popad
        popfd
        jmp dword ptr [hint_name_next]
    }
}
extern "C" __declspec(naked) void BJ3DangerCoinEffectGate() noexcept {
    __asm {
        pushfd
        pushad
        push ebp
        call BJ3DangerCompareValue
        mov [esp+014h],eax
        popad
        popfd
        jmp dword ptr [coin_effect_next]
    }
}
extern "C" __declspec(naked) void BJ3DangerBonusCompareGate() noexcept {
    __asm {
        pushfd
        pushad
        push edi
        call BJ3DangerCompareValue
        mov [esp+014h],eax
        popad
        popfd
        jmp dword ptr [bonus_compare_next]
    }
}
extern "C" __declspec(naked) void BJ3DangerFlightScaleGate() noexcept {
    __asm {
        pushfd
        pushad
        push ecx
        call BJ3DangerNativeCaptionIndex
        mov ecx,eax
        mov eax,[esp+01Ch]
        sub eax,ecx
        mov [esp+01Ch],eax
        pushfd
        pop ecx
        mov [esp+020h],ecx
        popad
        popfd
        jmp dword ptr [flight_scale_next]
    }
}
extern "C" DWORD WINAPI BJ3InstallDangerHooks(LPVOID) noexcept {
    struct Site{uintptr_t at,target;uint8_t original[6];};
    const Site sites[]={
        {0x006B40D2,reinterpret_cast<uintptr_t>(&BJ3DangerHintCompareGate),{0x8B,0x86,0x7C,0x39,0,0}},
        {0x006B41C3,reinterpret_cast<uintptr_t>(&BJ3DangerHintNameGate),{0x8B,0x86,0x7C,0x39,0,0}},
        {0x006C43AB,reinterpret_cast<uintptr_t>(&BJ3DangerCoinEffectGate),{0x8B,0x95,0x7C,0x39,0,0}},
        {0x006C4FC6,reinterpret_cast<uintptr_t>(&BJ3DangerBonusCompareGate),{0x8B,0x97,0x7C,0x39,0,0}},
        {0x006BB985,reinterpret_cast<uintptr_t>(&BJ3DangerFlightScaleGate),{0x2B,0x81,0x7C,0x39,0,0}}};
    for(const auto& s:sites) {
        uint8_t patch[6]={0xE9,0,0,0,0,0x90};const int32_t rel=static_cast<int32_t>(s.target-(s.at+5));memcpy(patch+1,&rel,4);
        auto at=reinterpret_cast<uint8_t*>(s.at);
        if(!memcmp(at,patch,6))continue;if(memcmp(at,s.original,6))return 0;
        DWORD old=0,ignored=0;if(!VirtualProtect(at,6,PAGE_EXECUTE_READWRITE,&old))return 0;
        memcpy(at,patch,6);const bool restored=VirtualProtect(at,6,old,&ignored)!=FALSE;
        if(!restored||!FlushInstructionCache(GetCurrentProcess(),at,6))return 0;
    }
    return 1;
}
