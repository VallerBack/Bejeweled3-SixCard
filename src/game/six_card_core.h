#pragma once

#include <stdint.h>

namespace bj3six {

enum class Category : uint32_t {
    Invalid = 0,
    Flush = 1,
    FiveOfAKind = 2,
    TwoTriplets = 3,
    Tower = 4,
    FourOfAKind = 5,
    ThreePair = 6,
    Spectrum = 7,
    Pyramid = 8,
    ThreeOfAKind = 9,
    TwoPair = 10,
    Pair = 11,
};

enum class EvaluationStatus : uint32_t {
    NotEvaluated = 0,
    Classified = 1,
    WrongCardCount = 2,
    UnsupportedGemValue = 3,
    MarkedOrSpecialCard = 4,
};

struct Evaluation {
    EvaluationStatus status;
    Category category;
    uint32_t distinct_gems;
    uint8_t multiplicities[6];
};

// Only ordinary gem IDs 0..6 are classified. Special/marked values are kept
// by the sidecar but deliberately produce MarkedOrSpecialCard/UnsupportedGemValue.
Evaluation EvaluateSix(const int32_t* values, uint32_t count, bool has_marked_card);
// Bit i means card i is a wildcard. Input/display identities are never mutated.
Evaluation EvaluateSixWild(const int32_t* values, uint32_t count, uint32_t wildcard_mask);
Evaluation EvaluateSixOptions(const uint8_t* allowed_colors, uint32_t count);
Evaluation EvaluateSixOptionsResolved(const uint8_t* allowed_colors, uint32_t count, int32_t* resolved);
const char* CategoryName(Category category);

} // namespace bj3six
