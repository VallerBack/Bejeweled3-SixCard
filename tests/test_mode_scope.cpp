#include "../src/game/mode_scope.cpp"

#include <assert.h>

extern "C" void __stdcall BJ3DiscardSixthPresentation(void*) noexcept {}

extern "C" void __stdcall BJ3CancelDeferredSave() noexcept {}

extern "C" uint32_t __fastcall BJ3PokerBoard224RestoreHook(void*,void*,void*,uint32_t) noexcept{return 1;}

#include "../src/game/poker_table_model.h"

static unsigned char app[0xC00]={};

static uint32_t seen[6]={};

static __declspec(naked) uint32_t FakeFactory() {

    __asm {

        mov seen[16],eax

        mov seen[20],ebx

        mov eax,[esp+4]

        mov seen[0],eax

        mov eax,[esp+8]

        mov seen[4],eax

        mov eax,[esp+12]

        mov seen[8],eax

        mov eax,[esp+16]

        mov seen[12],eax

        mov eax,012345678h

        ret 16

    }

}

static __declspec(naked) uint32_t ExerciseGate() {

    __asm {

        push ebx

        mov ebx,0ABCDEF12h

        mov eax,087654321h

        push 1

        push 011223344h

        push 0

        push offset app

        call BJ3FactoryModeGate

        pop ebx

        ret

    }

}

int main() {

    auto memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));

    assert(memory);memory[0]=0x90;memory[1]=0x91;

    Patch patch;patch.address=memory;patch.before={0x11,0x22};patch.after={0x90,0x91};patch.task={0x33,0x44};patches.push_back(patch);

    ready=true;active=true;

    assert(Select(false) && memory[0]==0x11 && memory[1]==0x22);

    assert(Select(false));assert(Select(true) && memory[0]==0x90 && memory[1]==0x91);

    memory[0]=0x99;assert(!Select(false) && memory[1]==0x91);memory[0]=0x90;

    *reinterpret_cast<uint32_t*>(app+0xBC8)=0x11223344;

    const uint32_t address=reinterpret_cast<uint32_t>(app);

    BJ3SelectFactoryMode(address,0,0x55667788,0);assert(!active); // Quest parser, record0.

    BJ3SelectFactoryMode(address,1,0x11223344,1);assert(!active); // Other secret mode.

    BJ3SelectFactoryMode(address,0,0x11223344,0);assert(!active); // Wrong context flag.

    BJ3SelectFactoryMode(address,0,0x11223344,1);assert(active); // Standalone Poker.

    BJ3SelectFactoryMode(address,17,0x55667788,0);assert(!active);

    *reinterpret_cast<uint32_t*>(app+0xBC4)=0x55667788;

    BJ3SelectFactoryMode(address,8,0x55667788,0);assert(active==2&&g_BJ3PokerContext==2&&memory[0]==0x33&&memory[1]==0x44);

    for(unsigned id=1;id<=11;++id) {

        const auto category=static_cast<bj3six::Category>(id);

        const auto rule=bj3six::CurrentRuleFor(category);assert(rule&&rule->charge_percent==0);

        auto row=bj3six::MakeTableRow(id-1,600,6,category);assert(!row.dangerous&&row.score==rule->base_score&&row.bounds.y==92+13*(id-1));

        assert(bj3six::HitTable(row.bounds.x,row.bounds.y,600)==category);

    }

    const int expected[11]={900,650,600,500,400,300,250,200,150,100,50};
    for(unsigned i=0;i<11;++i)assert(bj3six::CurrentRuleFor(static_cast<bj3six::Category>(i+1))->base_score==expected[i]);
    assert(bj3six::CurrentRuleFor(bj3six::Category::Flush)->base_score==900);

    assert(bj3six::CurrentRuleFor(bj3six::Category::FiveOfAKind)->base_score==650);

    BJ3SelectFactoryMode(address,8,0x55667788,1);assert(active==0); // Wrong task flag.

    BJ3SelectFactoryMode(address,18,0x55667788,0);assert(active==2&&g_BJ3PokerContext==2);

    assert(bj3six::quest::TaskFor(18)->credits==3500);

    BJ3SelectFactoryMode(address,37,0x55667788,0);assert(active==2&&g_BJ3PokerContext==2);

    assert(bj3six::quest::TaskFor(37)->credits==4000);

    BJ3SelectFactoryMode(address,37,0x55667788,1);assert(active==0);

    BJ3SelectFactoryMode(address,7,0x55667788,0);assert(active==0); // Other task.

    BJ3SelectFactoryMode(address,0,0x11223344,1);assert(active==1&&g_BJ3PokerContext==1);

    assert(bj3six::CurrentRuleFor(bj3six::Category::Flush)->base_score==100000);

    factory_resume=reinterpret_cast<uintptr_t>(&FakeFactory);

    assert(ExerciseGate()==0x12345678 && active);

    assert(seen[0]==address && seen[1]==0 && seen[2]==0x11223344 && seen[3]==1);

    assert(seen[4]==0x87654321 && seen[5]==0xABCDEF12);

    assert(Select(false));assert(VirtualFree(memory,0,MEM_RELEASE));

    return 0;

}

