#pragma once
#include "six_card_core.h"
namespace bj3six {
// Pair -> Two Pair -> Three -> Pyramid -> Spectrum -> Three Pair -> Four
// -> Tower -> Two Triplets. Keep the former top two safe categories unchanged.
constexpr int32_t MaxDangerStep=8;
inline int32_t DangerIndex(Category category) noexcept {
    const auto id=static_cast<uint32_t>(category);
    return id>=1&&id<=11?11-static_cast<int32_t>(id):-1;
}
inline int32_t UpgradeLegacyDangerStep(int32_t tier) noexcept {
    static const int32_t step[6]={0,1,3,4,5,8};
    return tier==-1?-1:tier>=0&&tier<=5?step[tier]:-2;
}
inline int32_t NativeCaptionTier(int32_t step) noexcept {
    static const int32_t tier[9]={0,1,2,2,3,4,5,5,5};
    return step==-1?-1:step>=0&&step<=MaxDangerStep?tier[step]:0;
}
inline bool PrepareDangerFields(int32_t* step,int32_t* maximum,uint32_t schema) noexcept {
    if(!step||!maximum)return false;
    const int32_t value=schema==2?UpgradeLegacyDangerStep(*step):*step;
    if(value< -1||value>MaxDangerStep)return false;
    *step=value;*maximum=MaxDangerStep;return true;
}
}
