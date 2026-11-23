#pragma once
#include <array>
#include <stdint.h>
#include <stddef.h>
namespace bj3save {
struct GameStats {
    std::array<uint8_t,32> native_save_sha256;
    uint32_t native_hand_counter,score,native_card_count;
    std::array<uint32_t,11> categories;
    uint32_t danger_schema=3;
};
enum class Status {Ok,WrongSize,WrongFormat,WrongVersion,Corrupt,WrongNativeSave,UnstableState};
using Record=std::array<uint8_t,108>;
inline void Put32(uint8_t* p,uint32_t v) noexcept {
    for(unsigned i=0;i<4;++i)p[i]=static_cast<uint8_t>(v>>(8*i));
}
inline uint32_t Get32(const uint8_t* p) noexcept {
    uint32_t v=0;for(unsigned i=0;i<4;++i)v|=static_cast<uint32_t>(p[i])<<(8*i);return v;
}
inline uint32_t Checksum(const uint8_t* p,size_t size) noexcept {
    uint32_t a=1,b=0;for(size_t i=0;i<size;++i){a=(a+p[i])%65521;b=(b+a)%65521;}return (b<<16)|a;
}
inline bool Stable(const GameStats& state) noexcept {
    uint64_t total=0;for(auto count:state.categories)total+=count;
    return state.native_card_count<=5 && (total==state.native_hand_counter || total+1==state.native_hand_counter);
}
// V3 preserves V2's 108-byte layout; its version marks the native file's danger
// threshold as a per-category step instead of the old seven-class tier.
// Statistics companion only. Native game bytes and transient pointers/effects
// are never serialized here. Caller must bind to the completed native save.
inline Status Encode(const GameStats& state,Record* output) noexcept {
    if(!output || !Stable(state))return Status::UnstableState;
    Record data={};const char magic[8]={'B','J','3','S','T','A','T','2'};
    for(unsigned i=0;i<8;++i)data[i]=static_cast<uint8_t>(magic[i]);
    Put32(data.data()+8,3);Put32(data.data()+12,88);
    for(unsigned i=0;i<32;++i)data[16+i]=state.native_save_sha256[i];
    Put32(data.data()+48,state.native_hand_counter);Put32(data.data()+52,state.score);
    Put32(data.data()+56,state.native_card_count);
    for(unsigned i=0;i<11;++i)Put32(data.data()+60+i*4,state.categories[i]);
    Put32(data.data()+104,Checksum(data.data(),104));*output=data;return Status::Ok;
}
inline Status Decode(const uint8_t* data,size_t size,
        const std::array<uint8_t,32>& native_sha,GameStats* output) noexcept {
    if(!data || !output || size!=108)return Status::WrongSize;
    const char magic[8]={'B','J','3','S','T','A','T','2'};
    for(unsigned i=0;i<8;++i)if(data[i]!=static_cast<uint8_t>(magic[i]))return Status::WrongFormat;
    const uint32_t version=Get32(data+8);
    if((version!=2&&version!=3) || Get32(data+12)!=88)return Status::WrongVersion;
    if(Get32(data+104)!=Checksum(data,104))return Status::Corrupt;
    GameStats state={};
    state.danger_schema=version;
    for(unsigned i=0;i<32;++i){if(data[16+i]!=native_sha[i])return Status::WrongNativeSave;state.native_save_sha256[i]=data[16+i];}
    state.native_hand_counter=Get32(data+48);state.score=Get32(data+52);state.native_card_count=Get32(data+56);
    for(unsigned i=0;i<11;++i)state.categories[i]=Get32(data+60+i*4);
    if(!Stable(state))return Status::UnstableState;
    *output=state;return Status::Ok;
}
}
