#pragma once
#include "six_card_core.h"
namespace bj3six {
// HUD only: strongest pattern already present, never a prediction of future cards.
inline Category PresentPattern(const uint8_t* counts) noexcept {
    uint8_t sorted[7]={};
    for(unsigned i=0;i<7;++i) sorted[i]=counts[i];
    for(unsigned i=0;i<7;++i) for(unsigned j=i+1;j<7;++j)
        if(sorted[j]>sorted[i]){const auto t=sorted[i];sorted[i]=sorted[j];sorted[j]=t;}
    if(sorted[0]>=6)return Category::Flush;
    if(sorted[0]>=5)return Category::FiveOfAKind;
    if(sorted[1]>=3)return Category::TwoTriplets;
    if(sorted[0]>=4&&sorted[1]>=2)return Category::Tower;
    if(sorted[0]>=4)return Category::FourOfAKind;
    if(sorted[2]>=2)return Category::ThreePair;
    if(sorted[5]>=1)return Category::Spectrum;
    if(sorted[0]>=3&&sorted[1]>=2)return Category::Pyramid;
    if(sorted[0]>=3)return Category::ThreeOfAKind;
    if(sorted[1]>=2)return Category::TwoPair;
    if(sorted[0]>=2)return Category::Pair;
    return Category::Invalid;
}
inline void VisitPartial(const uint8_t* masks,unsigned count,unsigned index,uint8_t* counts,Category* best) noexcept {
    if(index==count){
        const auto candidate=PresentPattern(counts);
        if(candidate!=Category::Invalid && (*best==Category::Invalid || static_cast<unsigned>(candidate)<static_cast<unsigned>(*best))) *best=candidate;
        return;
    }
    for(unsigned color=0;color<7;++color) if(masks[index]&(1u<<color)) {
        ++counts[color];VisitPartial(masks,count,index+1,counts,best);--counts[color];
    }
}
inline Category EvaluatePresentOptions(const uint8_t* masks,unsigned count) noexcept {
    if(!masks || count>6 || count==0)return Category::Invalid;
    for(unsigned i=0;i<count;++i)if(!(masks[i]&127) || (masks[i]&128))return Category::Invalid;
    uint8_t counts[7]={};Category best=Category::Invalid;
    VisitPartial(masks,count,0,counts,&best);return best;
}
}
