#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include "../src/game/poker_table_model.h"
#include "../src/game/game_stats_record.h"
#include "native_danger_steps.inc"
extern "C" volatile long g_BJ3PokerContext=1;
static unsigned Count(uint32_t n){unsigned k=0;for(;n;n&=n-1)++k;return k;}
using Native=void(__cdecl*)(void*);
static Native Code(const unsigned char* native,size_t length) {
    auto code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(code);
    const unsigned char prefix[]={0x56,0x8B,0x74,0x24,0x08};
    const unsigned char suffix[]={0x83,0xC4,0x10,0x5E,0xC3};
    memcpy(code,prefix,sizeof(prefix));memcpy(code+sizeof(prefix),native,length);memcpy(code+sizeof(prefix)+length,suffix,sizeof(suffix));
    DWORD old=0;assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),code,4096);return reinterpret_cast<Native>(code);
}
int main(){
    const auto advance=Code(native_advance,sizeof(native_advance)),clear=Code(native_clear,sizeof(native_clear));
    unsigned char board[0x3C00]={};auto step=reinterpret_cast<int32_t*>(board+0x397C),maximum=reinterpret_cast<int32_t*>(board+0x3980);
    *maximum=bj3six::MaxDangerStep;
    for(int old=-1;old<=8;++old) {
        *step=old;const auto before=bj3six::SkullMaskForThreshold(old);advance(board);
        assert(*step==(old<8?old+1:8));const auto added=bj3six::SkullMaskForThreshold(*step)&~before;
        assert(Count(added)==(old<8?1u:0u));
        *step=old;clear(board);assert(*step==(old<0?-1:old-1));
        const auto removed=before&~bj3six::SkullMaskForThreshold(*step);
        assert(Count(removed)==(old<0?0u:1u));
        if(old>=0)assert(bj3six::MakeSkullClearTarget(old).affected_mask==removed);
    }
    for(int old=-1;old<=5;++old) {
        uint32_t legacy=0;for(unsigned id=1;id<=11;++id)if(static_cast<int32_t>(bj3six::CompatibilityRank(static_cast<bj3six::Category>(id)))-1<=old)legacy|=1u<<(id-1);
        *step=old;*maximum=5;assert(bj3six::PrepareDangerFields(step,maximum,2));
        assert(*maximum==8&&bj3six::SkullMaskForThreshold(*step)==legacy);
        const int converted=*step;assert(bj3six::PrepareDangerFields(step,maximum,3)&&*step==converted);
    }
    // The old shared group no longer makes Pyramid dangerous with Three.
    assert(bj3six::IsSkullMarked(bj3six::Category::ThreeOfAKind,2));
    assert(!bj3six::IsSkullMarked(bj3six::Category::Pyramid,2));
    assert(bj3six::IsSkullMarked(bj3six::Category::FourOfAKind,6));
    assert(!bj3six::IsSkullMarked(bj3six::Category::Tower,6));
    assert(!bj3six::IsSkullMarked(bj3six::Category::TwoTriplets,7));
    for(int stage=0;stage<=8;++stage)assert(bj3six::NativeCaptionTier(stage)>=0&&bj3six::NativeCaptionTier(stage)<=5);
    assert(!bj3six::IsSkullMarked(bj3six::Category::FiveOfAKind,8)&&!bj3six::IsSkullMarked(bj3six::Category::Flush,8));
    bj3save::GameStats state={};state.native_hand_counter=2;state.categories[10]=1;state.score=2500;state.native_card_count=5;
    bj3save::Record record={};assert(bj3save::Encode(state,&record)==bj3save::Status::Ok&&bj3save::Get32(record.data()+8)==3);
    bj3save::GameStats decoded={};assert(bj3save::Decode(record.data(),record.size(),state.native_save_sha256,&decoded)==bj3save::Status::Ok&&decoded.danger_schema==3&&decoded.categories==state.categories);
    bj3save::Put32(record.data()+8,2);bj3save::Put32(record.data()+104,bj3save::Checksum(record.data(),104));
    assert(bj3save::Decode(record.data(),record.size(),state.native_save_sha256,&decoded)==bj3save::Status::Ok&&decoded.danger_schema==2);
    std::cout<<"PASS actual native advance/clear snippets: one row per event; legacy masks preserved; V2/V3 discrimination; bounded native caption indices\n";
}
