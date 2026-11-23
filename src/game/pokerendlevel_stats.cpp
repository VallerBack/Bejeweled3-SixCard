#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "six_card_core.h"

extern "C" volatile LONG g_BJ3SixCardGameCounts[12];

namespace {

const uint32_t kImageBase = 0x00400000;
const uint32_t kPokerEndLevelDialogVtable = 0x0081495C;
const uint32_t kPokerEndLevelStatsDrawSlot = 0x008149C4;
const uint32_t kPokerEndLevelStatsDrawOriginal = 0x00751500;
const uint32_t kGlobalImageFont = 0x008883D4;
const uint32_t kImageFontVtable = 0x0081D4E4;
const uint32_t kGraphicsFontOffset = 0x68;
const uint32_t kGraphicsDrawString = 0x004309B0;
const int32_t kStatsColumnX = 145;
const int32_t kStatsFirstRowY = 365;
const int32_t kStatsRowStep = 24;

struct TargetSexyStringView {
    uint32_t reserved;
    union Storage {
        uint16_t inline_characters[8];
        const uint16_t* heap_data;
    } storage;
    uint32_t length;
    uint32_t capacity;
};

struct TargetSexyColor {
    uint32_t channels[4];
};

struct StatsDrawTelemetry {
    volatile LONG sequence;
    uint32_t dialog;
    uint32_t graphics;
    uint32_t native_renderer_replaced;
    uint32_t vptr_match;
    uint32_t font;
    uint32_t rows_drawn;
    int32_t first_x;
    int32_t first_y;
    int32_t last_x;
    int32_t last_y;
    uint32_t counts[11];
};

static_assert(sizeof(TargetSexyStringView) == 0x1C,
    "target x86 SexyString storage must remain 0x1C bytes");
static_assert(sizeof(TargetSexyColor) == 0x10,
    "target x86 SexyColor storage must remain 0x10 bytes");

volatile LONG g_hook_state = 0;
#include "native_statistics_bytes.inc"
static void* g_native_statistics=nullptr;
static int32_t g_chart_counts[11]={};
static TargetSexyStringView g_chart_labels[11]={};
static uint16_t g_chart_text[11][32]={};
static const uint32_t g_chart_colors[11][4]={
    {255,128,128,255},{255,192,128,255},{255,255,128,255},
    {192,255,128,255},{128,255,128,255},{128,255,192,255},
    {128,255,255,255},{128,192,255,255},{128,128,255,255},
    {192,128,255,255},{255,128,255,255}};
extern "C" __declspec(naked) void BJ3StatisticsScaleFloor() noexcept {
    __asm {
        cmp ecx,1
        jge scale_valid
        mov ecx,1
        mov dword ptr [esp+018h],ecx
    scale_valid:
        cmp ecx,10
        mov dword ptr [esp+020h],1
        ret
    }
}
extern "C" int32_t __stdcall BJ3StatisticsBarHeightValue() noexcept {
    const uint32_t app=*reinterpret_cast<uint32_t*>(0x00887F34u);
    return app ? *reinterpret_cast<int32_t*>(app+0x9E0)*24/1200:24;
}
extern "C" __declspec(naked) void BJ3StatisticsBarHeight() noexcept {
    __asm {
        push ecx
        push edx
        call BJ3StatisticsBarHeightValue
        pop edx
        pop ecx
        ret
    }
}
static bool InitNativeStatistics() noexcept {
    if(g_native_statistics)return true;
    if(memcmp(reinterpret_cast<void*>(0x00751500u),kNativeStatisticsBytes,sizeof(kNativeStatisticsBytes)))return false;
    auto code=static_cast<uint8_t*>(VirtualAlloc(nullptr,sizeof(kNativeStatisticsBytes),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!code)return false;
    memcpy(code,kNativeStatisticsBytes,sizeof(kNativeStatisticsBytes));
    for(unsigned off:kNativeStatisticsRel32) {
        int32_t rel=0;memcpy(&rel,code+off,4);
        rel+=static_cast<int32_t>(0x00751500u-reinterpret_cast<uint32_t>(code));memcpy(code+off,&rel,4);
    }
    // Replace data sources; leave the native seven temporary strings and their
    // matching destructor count intact. Eleven borrowed labels live in the DLL.
    auto pointer=[&](uint32_t address,uint8_t opcode,const void* value,unsigned bytes) {
        auto at=code+address-0x00751500u;memset(at,0x90,bytes);at[0]=opcode;
        const uint32_t p=reinterpret_cast<uint32_t>(value);memcpy(at+1,&p,4);
    };
    pointer(0x007515E7u,0xB8,g_chart_counts,5);
    code[0x007515F4u-0x00751500u+2]=11;
    pointer(0x007518A6u,0xB8,g_chart_colors,7);
    pointer(0x007518ADu,0xB9,g_chart_labels,7);
    code[0x00751D78u-0x00751500u+2]=28;
    const int32_t end=760+11*28;
    memcpy(code+0x00751D7Bu-0x00751500u+1,&end,4);
    auto call=[&](uint32_t address,uintptr_t target,unsigned bytes) {
        auto at=code+address-0x00751500u;memset(at,0x90,bytes);at[0]=0xE8;
        const int32_t rel=static_cast<int32_t>(target-reinterpret_cast<uintptr_t>(at+5));memcpy(at+1,&rel,4);
    };
    call(0x0075160Bu,reinterpret_cast<uintptr_t>(&BJ3StatisticsScaleFloor),11);
    call(0x00751C64u,reinterpret_cast<uintptr_t>(&BJ3StatisticsBarHeight),5);
    call(0x00751D1Au,reinterpret_cast<uintptr_t>(&BJ3StatisticsBarHeight),5);
    DWORD old=0;
    if(!VirtualProtect(code,sizeof(kNativeStatisticsBytes),PAGE_EXECUTE_READ,&old)){VirtualFree(code,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),code,sizeof(kNativeStatisticsBytes));g_native_statistics=code;return true;
}
static void PrepareNativeStatistics() noexcept {
    for(uint32_t i=0;i<11;++i) {
        const LONG count=InterlockedCompareExchange(&g_BJ3SixCardGameCounts[i+1],0,0);
        g_chart_counts[i]=count<0 ? 0:count;
        const char* name=bj3six::CategoryName(static_cast<bj3six::Category>(i+1));
        uint32_t length=0;while(name[length] && length<31){g_chart_text[i][length]=static_cast<uint8_t>(name[length]);++length;}
        g_chart_text[i][length]=0;auto& label=g_chart_labels[i];memset(&label,0,sizeof(label));label.length=length;
        if(length<8){memcpy(label.storage.inline_characters,g_chart_text[i],length*2);label.capacity=7;}
        else {label.storage.heap_data=g_chart_text[i];label.capacity=length;}
    }
}

bool ReadDialogVptr(void* dialog, uint32_t* vptr) noexcept {
    if (dialog == nullptr || vptr == nullptr) return false;
    __try {
        *vptr = *reinterpret_cast<const uint32_t*>(dialog);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

extern "C" __declspec(dllexport) StatsDrawTelemetry
    g_BJ3SixCardStatsDrawLastCall = {};

extern "C" __declspec(dllexport) void __fastcall
BJ3PokerEndLevelStatsDrawHook(void* dialog, void* unused_edx,
        void* graphics) noexcept {
    (void)unused_edx;
    StatsDrawTelemetry sample = {};
    sample.dialog = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dialog));
    sample.graphics = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(graphics));
    sample.native_renderer_replaced = 1;
    uint32_t vptr = 0;
    sample.vptr_match = ReadDialogVptr(dialog, &vptr) &&
        vptr == kPokerEndLevelDialogVtable ? 1u : 0u;
    if(sample.vptr_match && graphics && g_native_statistics) {
        PrepareNativeStatistics();
        typedef void (__thiscall *Draw)(void*,void*);
        reinterpret_cast<Draw>(g_native_statistics)(dialog,graphics);
        sample.font=*reinterpret_cast<uint32_t*>(0x0087DC64u);
        sample.rows_drawn=11;
        for(uint32_t i=0;i<11;++i)sample.counts[i]=g_chart_counts[i];
        sample.first_x=400;sample.first_y=760;
        sample.last_x=400;sample.last_y=1040;
    }

    InterlockedIncrement(&g_BJ3SixCardStatsDrawLastCall.sequence);
    g_BJ3SixCardStatsDrawLastCall.dialog = sample.dialog;
    g_BJ3SixCardStatsDrawLastCall.graphics = sample.graphics;
    g_BJ3SixCardStatsDrawLastCall.native_renderer_replaced =
        sample.native_renderer_replaced;
    g_BJ3SixCardStatsDrawLastCall.vptr_match = sample.vptr_match;
    g_BJ3SixCardStatsDrawLastCall.font = sample.font;
    g_BJ3SixCardStatsDrawLastCall.rows_drawn = sample.rows_drawn;
    g_BJ3SixCardStatsDrawLastCall.first_x = sample.first_x;
    g_BJ3SixCardStatsDrawLastCall.first_y = sample.first_y;
    g_BJ3SixCardStatsDrawLastCall.last_x = sample.last_x;
    g_BJ3SixCardStatsDrawLastCall.last_y = sample.last_y;
    for (uint32_t index = 0; index != 11; ++index)
        g_BJ3SixCardStatsDrawLastCall.counts[index] = sample.counts[index];
    MemoryBarrier();
    InterlockedIncrement(&g_BJ3SixCardStatsDrawLastCall.sequence);
}

extern "C" __declspec(dllexport) DWORD WINAPI
BJ3InstallPokerEndLevelStatsDrawHook(LPVOID unused) noexcept {
    (void)unused;
    if(!InitNativeStatistics())return 18;
    if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(kImageBase)) return 11;
    const LONG hook = static_cast<LONG>(reinterpret_cast<uintptr_t>(
        &BJ3PokerEndLevelStatsDrawHook));
    volatile LONG* slot = reinterpret_cast<volatile LONG*>(
        static_cast<uintptr_t>(kPokerEndLevelStatsDrawSlot));
    const LONG current = *slot;
    if (current == hook) return 1;
    if (current != static_cast<LONG>(kPokerEndLevelStatsDrawOriginal)) return 12;

    DWORD old_protection = 0;
    if (!VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG),
            PAGE_READWRITE, &old_protection)) return 13;
    const LONG replaced = InterlockedCompareExchange(slot, hook,
        static_cast<LONG>(kPokerEndLevelStatsDrawOriginal));
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG),
        old_protection, &ignored);
    return replaced == static_cast<LONG>(kPokerEndLevelStatsDrawOriginal) &&
        restored ? 1 : 14;
}

extern "C" __declspec(dllexport) DWORD WINAPI
BJ3UninstallPokerEndLevelStatsDrawHook(LPVOID unused) noexcept {
    (void)unused;
    if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(kImageBase)) return 11;
    const LONG hook = static_cast<LONG>(reinterpret_cast<uintptr_t>(
        &BJ3PokerEndLevelStatsDrawHook));
    volatile LONG* slot = reinterpret_cast<volatile LONG*>(
        static_cast<uintptr_t>(kPokerEndLevelStatsDrawSlot));
    const LONG current = *slot;
    if (current == static_cast<LONG>(kPokerEndLevelStatsDrawOriginal)) return 1;
    if (current != hook) return 12;

    DWORD old_protection = 0;
    if (!VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG),
            PAGE_READWRITE, &old_protection)) return 13;
    const LONG replaced = InterlockedCompareExchange(slot,
        static_cast<LONG>(kPokerEndLevelStatsDrawOriginal), hook);
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG),
        old_protection, &ignored);
    return replaced == hook && restored ? 1 : 14;
}

extern "C" __declspec(dllexport) void* __stdcall BJ3GetNativeStatisticsRenderer() noexcept {return g_native_statistics;}
