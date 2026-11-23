#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include "quest_poker_rules.h"

extern "C" uint32_t __fastcall BJ3PokerBoard224RestoreHook(void*,void*,void*,uint32_t) noexcept;
extern "C" __declspec(dllexport) volatile LONG g_BJ3PokerContext=0;
extern "C" __declspec(dllexport) uint32_t g_BJ3ModeScopeTaskHashes[512]={};
extern "C" void __stdcall BJ3CancelDeferredSave() noexcept;
extern "C" void __stdcall BJ3DiscardSixthPresentation(void*) noexcept;

// Capture only mutations made by our seven startup installers. Retain the
// factory gateway permanently; switch the complete captured set before the
// shared factory can construct/initialize a new Board, on its caller thread.
namespace {
struct Region {unsigned char* address;std::vector<unsigned char> before;};
struct Patch {unsigned char* address;std::vector<unsigned char> before,after,task;};
std::vector<Region> regions;
std::vector<Patch> patches;
int active=1;bool ready=false;
uintptr_t factory_resume=0;
const unsigned char factory_original[]={0x6A,0xFF,0x68,0xC4,0x0C,0x77,0};
bool Write(unsigned char* at,const unsigned char* data,size_t n) {
    DWORD old=0,ignored=0;
    if(!VirtualProtect(at,n,PAGE_EXECUTE_READWRITE,&old))return false;
    memcpy(at,data,n);
    const BOOL restored=VirtualProtect(at,n,old,&ignored);
    return restored && FlushInstructionCache(GetCurrentProcess(),at,n);
}
const std::vector<unsigned char>& Target(const Patch& p,int kind){return kind==2?p.task:kind==1?p.after:p.before;}
bool Select(int enable) {
    if(!ready)return false;
    if(active==enable)return true;
    for(const auto& p:patches) {
        const auto& expected=Target(p,active);
        if(memcmp(p.address,expected.data(),expected.size()))return false;
    }
    for(const auto& p:patches) {
        const auto& target=Target(p,enable);
        if(!Write(p.address,target.data(),target.size()))return false;
    }
    active=enable;return true;
}
}
struct ScopeRange {uint32_t address,length,before_hash,after_hash;};
extern "C" __declspec(dllexport) ScopeRange g_BJ3ModeScopeRanges[512]={};
static uint32_t Hash(const std::vector<unsigned char>& b){uint32_t h=2166136261u;for(auto c:b)h=(h^c)*16777619u;return h;}
extern "C" __declspec(dllexport) volatile LONG g_BJ3ModeScopeActive=1;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ModeScopePatchCount=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ModeScopeCalls=0;
extern "C" __declspec(dllexport) uint32_t g_BJ3ModeScopeLast[5]={};
extern "C" __declspec(dllexport) DWORD WINAPI BJ3BeginModeScope(LPVOID) noexcept {
    if(!regions.empty() || ready)return 0;
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if(base!=reinterpret_cast<unsigned char*>(0x400000))return 0;
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    auto section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) {
        if(memcmp(section[i].Name,".text\0",6) && memcmp(section[i].Name,".rdata\0",7))continue;
        Region r;r.address=base+section[i].VirtualAddress;
        r.before.assign(r.address,r.address+section[i].Misc.VirtualSize);
        regions.push_back(r);
    }
    return regions.size()==2?1:0;
}
extern "C" void __stdcall BJ3SelectFactoryMode(uint32_t app,uint32_t index,uint32_t parser,uint32_t flag) noexcept {
    const uint32_t secret=app?*reinterpret_cast<uint32_t*>(app+0xBC8):0;
    // secret.cfg record index0 is the standalone Poker record (#1000).
    // Quest parser BC4, other secret modes, and unknown contexts stay native.
    const uint32_t quest=app?*reinterpret_cast<uint32_t*>(app+0xBC4):0;
    const int enable=secret && parser==secret && index==0 && flag==1?1:
        quest && parser==quest && bj3six::quest::TaskFor(index) && flag==0?2:0;
    g_BJ3ModeScopeLast[0]=app;g_BJ3ModeScopeLast[1]=index;
    g_BJ3ModeScopeLast[2]=parser;g_BJ3ModeScopeLast[3]=flag;
    g_BJ3ModeScopeLast[4]=enable;
    BJ3CancelDeferredSave();
    const uint32_t old_board=app?*reinterpret_cast<uint32_t*>(app+0xBE0):0;
    if(old_board)BJ3DiscardSixthPresentation(reinterpret_cast<void*>(old_board));
    if(!Select(enable))ExitProcess(19);
    g_BJ3PokerContext=enable;g_BJ3ModeScopeActive=enable;++g_BJ3ModeScopeCalls;
}
extern "C" __declspec(naked) void BJ3FactoryModeGate() noexcept {
    __asm {
        pushfd
        pushad
        push dword ptr [esp+034h]
        push dword ptr [esp+034h]
        push dword ptr [esp+034h]
        push dword ptr [esp+034h]
        call BJ3SelectFactoryMode
        popad
        popfd
        jmp dword ptr [factory_resume]
    }
}
extern "C" __declspec(dllexport) DWORD WINAPI BJ3FinishModeScope(LPVOID) noexcept {
    if(ready || regions.size()!=2)return 0;
    for(const auto& r:regions) {
        size_t i=0;
        while(i<r.before.size()) {
            if(r.address[i]==r.before[i]){++i;continue;}
            const size_t start=i;
            while(i<r.before.size() && r.address[i]!=r.before[i])++i;
            Patch p;p.address=r.address+start;
            p.before.assign(r.before.begin()+start,r.before.begin()+i);
            p.after.assign(r.address+start,r.address+i);p.task=p.after;
            const uintptr_t address=reinterpret_cast<uintptr_t>(p.address);
            const uint32_t restore=reinterpret_cast<uint32_t>(&BJ3PokerBoard224RestoreHook);
            for(size_t k=0;k<p.task.size();++k) {
                const uintptr_t at=address+k;
                if((at>=0x7176CB && at<0x7176CF) || (at>=0x721925 && at<0x721929) ||
                   (at>=0x7A9934 && at<0x7A993E) || (at>=0x8149C4 && at<0x8149C8))p.task[k]=p.before[k];
                if(at>=0x811A40 && at<0x811A44)p.task[k]=reinterpret_cast<const unsigned char*>(&restore)[at-0x811A40];
            }
            patches.push_back(p);
        }
    }
    if(patches.empty() || patches.size()>512)return 0;
    for(size_t i=0;i<patches.size();++i){const auto& p=patches[i];g_BJ3ModeScopeRanges[i]={reinterpret_cast<uint32_t>(p.address),static_cast<uint32_t>(p.before.size()),Hash(p.before),Hash(p.after)};g_BJ3ModeScopeTaskHashes[i]=Hash(p.task);}
    auto factory=reinterpret_cast<unsigned char*>(0x746B90);
    if(memcmp(factory,factory_original,7))return 0;
    auto trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,12,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!trampoline)return 0;
    memcpy(trampoline,factory_original,7);trampoline[7]=0xE9;
    const int32_t back=static_cast<int32_t>((factory+7)-(trampoline+12));memcpy(trampoline+8,&back,4);
    DWORD old=0;if(!VirtualProtect(trampoline,12,PAGE_EXECUTE_READ,&old))return 0;
    FlushInstructionCache(GetCurrentProcess(),trampoline,12);
    factory_resume=reinterpret_cast<uintptr_t>(trampoline);
    unsigned char jump[7]={0xE9,0,0,0,0,0x90,0x90};
    const int32_t rel=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&BJ3FactoryModeGate)-(reinterpret_cast<uintptr_t>(factory)+5));
    memcpy(jump+1,&rel,4);
    if(!Write(factory,jump,7))return 0;
    ready=true;g_BJ3ModeScopePatchCount=static_cast<LONG>(patches.size());
    // Native menus/tasks are the default. Standalone Poker explicitly opts in.
    if(!Select(false))return 0;g_BJ3ModeScopeActive=0;
    return 1;
}
