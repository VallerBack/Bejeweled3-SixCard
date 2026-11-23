#include "six_card_core.h"

namespace bj3six {

Evaluation EvaluateSix(const int32_t* values, uint32_t count, bool has_marked_card) {
    Evaluation result = {};
    result.status = EvaluationStatus::NotEvaluated;
    result.category = Category::Invalid;

    if (count != 6 || values == nullptr) {
        result.status = EvaluationStatus::WrongCardCount;
        return result;
    }
    if (has_marked_card) {
        result.status = EvaluationStatus::MarkedOrSpecialCard;
        return result;
    }

    uint8_t frequencies[7] = {};
    for (uint32_t index = 0; index != 6; ++index) {
        if (values[index] < 0 || values[index] > 6) {
            result.status = EvaluationStatus::UnsupportedGemValue;
            return result;
        }
        ++frequencies[static_cast<uint32_t>(values[index])];
    }

    uint8_t pattern[6] = {};
    uint32_t distinct = 0;
    for (uint32_t gem = 0; gem != 7; ++gem) {
        if (frequencies[gem] != 0) {
            pattern[distinct++] = frequencies[gem];
        }
    }
    for (uint32_t i = 0; i < distinct; ++i) {
        for (uint32_t j = i + 1; j < distinct; ++j) {
            if (pattern[j] > pattern[i]) {
                const uint8_t temporary = pattern[i];
                pattern[i] = pattern[j];
                pattern[j] = temporary;
            }
        }
    }

    result.distinct_gems = distinct;
    for (uint32_t i = 0; i < distinct; ++i) {
        result.multiplicities[i] = pattern[i];
    }

    if (distinct == 1 && pattern[0] == 6) result.category = Category::Flush;
    else if (distinct == 2 && pattern[0] == 5 && pattern[1] == 1) result.category = Category::FiveOfAKind;
    else if (distinct == 2 && pattern[0] == 3 && pattern[1] == 3) result.category = Category::TwoTriplets;
    else if (distinct == 2 && pattern[0] == 4 && pattern[1] == 2) result.category = Category::Tower;
    else if (distinct == 3 && pattern[0] == 4 && pattern[1] == 1 && pattern[2] == 1) result.category = Category::FourOfAKind;
    else if (distinct == 3 && pattern[0] == 2 && pattern[1] == 2 && pattern[2] == 2) result.category = Category::ThreePair;
    else if (distinct == 6) result.category = Category::Spectrum;
    else if (distinct == 3 && pattern[0] == 3 && pattern[1] == 2 && pattern[2] == 1) result.category = Category::Pyramid;
    else if (distinct == 4 && pattern[0] == 3 && pattern[1] == 1 && pattern[2] == 1 && pattern[3] == 1) result.category = Category::ThreeOfAKind;
    else if (distinct == 4 && pattern[0] == 2 && pattern[1] == 2 && pattern[2] == 1 && pattern[3] == 1) result.category = Category::TwoPair;
    else if (distinct == 5 && pattern[0] == 2) result.category = Category::Pair;

    result.status = result.category == Category::Invalid
        ? EvaluationStatus::UnsupportedGemValue
        : EvaluationStatus::Classified;
    return result;
}

namespace {
void SearchWild(int32_t* values, uint32_t mask, uint32_t index, Evaluation* best) {
    if (best->status == EvaluationStatus::Classified && best->category == Category::Flush) return;
    while (index < 6 && (mask & (1u << index)) == 0) ++index;
    if (index == 6) {
        const Evaluation candidate = EvaluateSix(values, 6, false);
        if (candidate.status == EvaluationStatus::Classified &&
            (best->status != EvaluationStatus::Classified ||
             static_cast<uint32_t>(candidate.category) < static_cast<uint32_t>(best->category)))
            *best = candidate;
        return;
    }
    for (int32_t color=0;color<7;++color) {
        values[index]=color;
        SearchWild(values,mask,index+1,best);
    }
}
}

Evaluation EvaluateSixWild(const int32_t* values, uint32_t count, uint32_t wildcard_mask) {
    if (!values || count != 6) return EvaluateSix(values,count,false);
    Evaluation result = {};
    result.status = EvaluationStatus::UnsupportedGemValue;
    if ((wildcard_mask & ~63u) != 0) return result;
    int32_t chosen[6] = {};
    for (uint32_t i=0;i<6;++i) {
        if ((wildcard_mask & (1u<<i)) == 0 && (values[i]<0 || values[i]>6)) return result;
        chosen[i]=values[i];
    }
    SearchWild(chosen,wildcard_mask,0,&result);
    return result;
}

namespace {
void SearchOptions(int32_t* values,const uint8_t* masks,uint32_t index,Evaluation* best,int32_t* resolved) {
    if (best->status==EvaluationStatus::Classified && best->category==Category::Flush) return;
    if(index==6) {
        Evaluation candidate=EvaluateSix(values,6,false);
        if(candidate.status==EvaluationStatus::Classified &&
           (best->status!=EvaluationStatus::Classified ||
            static_cast<uint32_t>(candidate.category)<static_cast<uint32_t>(best->category))) {
            *best=candidate;
            if(resolved) for(uint32_t i=0;i<6;++i) resolved[i]=values[i];
        }
        return;
    }
    for(int32_t color=0;color<7;++color) if(masks[index]&(1u<<color)) {
        values[index]=color; SearchOptions(values,masks,index+1,best,resolved);
    }
}
}
Evaluation EvaluateSixOptionsResolved(const uint8_t* masks,uint32_t count,int32_t* resolved) {
    Evaluation best={}; best.status=EvaluationStatus::WrongCardCount;
    if(!masks || count!=6) return best;
    best.status=EvaluationStatus::UnsupportedGemValue;
    for(uint32_t i=0;i<6;++i) if(!masks[i] || (masks[i]&128u)) return best;
    int32_t values[6]={}; SearchOptions(values,masks,0,&best,resolved);return best;
}

Evaluation EvaluateSixOptions(const uint8_t* masks,uint32_t count) {
    return EvaluateSixOptionsResolved(masks,count,nullptr);
}

const char* CategoryName(Category category) {
    switch (category) {
    case Category::Flush: return "Flush";
    case Category::FiveOfAKind: return "5 of a Kind";
    case Category::TwoTriplets: return "2 Triplets";
    case Category::Tower: return "Tower";
    case Category::FourOfAKind: return "4 of a Kind";
    case Category::ThreePair: return "3 Pair";
    case Category::Spectrum: return "Spectrum";
    case Category::Pyramid: return "Pyramid";
    case Category::ThreeOfAKind: return "3 of a Kind";
    case Category::TwoPair: return "2 Pair";
    case Category::Pair: return "Pair";
    default: return "Unclassified";
    }
}

} // namespace bj3six
