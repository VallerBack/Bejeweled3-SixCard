#pragma once
#include "poker_rules.h"
// User-approved original Quest task-credit policy (Run428 balance).
// Independent constants: later endless-mode balance changes must not silently
// change the three original Quest objectives.
namespace bj3six { namespace quest {
inline const PokerRule* RuleFor(Category category) noexcept {
    static const PokerRule rules[11]={
        {Category::Flush,900,0},{Category::FiveOfAKind,650,0},
        {Category::TwoTriplets,600,0},{Category::Tower,500,0},
        {Category::FourOfAKind,400,0},{Category::ThreePair,300,0},
        {Category::Spectrum,250,0},{Category::Pyramid,200,0},
        {Category::ThreeOfAKind,150,0},{Category::TwoPair,100,0},
        {Category::Pair,50,0}
    };
    const uint32_t id=static_cast<uint32_t>(category);
    return id>=1&&id<=11?&rules[id-1]:nullptr;
}
struct OriginalTask {uint32_t ordinal,credits,hands;};
inline const OriginalTask* TaskFor(uint32_t ordinal) noexcept {
    static const OriginalTask tasks[]={{8,3000,10},{18,3500,10},{37,4000,10}};
    for(const auto& task:tasks)if(task.ordinal==ordinal)return &task;
    return nullptr;
}
constexpr int32_t FlameBonus=50,StarBonus=100;
}}
