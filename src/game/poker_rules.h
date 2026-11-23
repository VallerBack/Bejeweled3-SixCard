#pragma once
#include <stdint.h>
#include "six_card_core.h"
#include "danger_steps.h"
namespace bj3six {
struct PokerRule {Category category;int32_t base_score;uint32_t charge_percent;};
inline const PokerRule* RuleFor(Category category) noexcept {
 static const PokerRule rules[11]={
  {Category::Flush,100000,100},{Category::FiveOfAKind,75000,80},
  {Category::TwoTriplets,50000,65},{Category::Tower,35000,50},
  {Category::FourOfAKind,25000,40},{Category::ThreePair,20000,30},
  {Category::Spectrum,15000,25},{Category::Pyramid,10000,20},
  {Category::ThreeOfAKind,7500,15},{Category::TwoPair,5000,10},
  {Category::Pair,2500,5}
 };
 const uint32_t id=static_cast<uint32_t>(category);
 return id>=1 && id<=11 ? &rules[id-1]:nullptr;
}
// One compatibility mapping for native cache consumers and visible danger rows.
inline uint32_t CompatibilityRank(Category category) noexcept {
 static const uint32_t rank[12]={0,7,7,6,6,6,5,4,3,3,2,1};
 const uint32_t id=static_cast<uint32_t>(category);
 return id<12 ? rank[id]:0;
}
inline bool IsSkullMarked(Category category,int32_t threshold) noexcept {
 const int32_t index=DangerIndex(category);
 return index>=0 && threshold>=index;
}
inline uint32_t SkullMaskForThreshold(int32_t threshold) noexcept {
 uint32_t mask=0;
 for(uint32_t id=1;id<=11;++id) if(IsSkullMarked(static_cast<Category>(id),threshold)) mask|=1u<<(id-1);
 return mask;
}
inline float ChargedTarget(float current,uint32_t percent) noexcept {
 const float value=current+static_cast<float>(percent)/100.0f;
 return value>1.0f ? 1.0f:value;
}
}
