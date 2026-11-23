#pragma once

#include <windows.h>
#include <stdint.h>

#include "six_card_core.h"

namespace bj3six {

struct CardProjection {
    int32_t selected_value;
    int32_t primary_value;
    int32_t alternate_value;
    uint32_t flags;
};

struct HandSnapshot {
    uint32_t board;
    uint32_t board_vptr;
    uint32_t generation;
    uint32_t native_count;
    uint32_t card_count;
    Evaluation evaluation;
    CardProjection cards[6];
    int32_t resolved_values[6];
    uint32_t display_types[6];
    int32_t bonus_values[6];
};

enum class ProjectionClaimResult : uint32_t {
    Claimed = 1,
    AlreadyClaimed = 2,
    MissingOrStale = 3,
};

class SidecarStore {
public:
    SidecarStore();
    bool Capture(const HandSnapshot& snapshot);
    bool CopyForBoard(uint32_t board, HandSnapshot* output) const;
    bool CopyUnclaimedProjectionForBoard(uint32_t board,
        HandSnapshot* output) const;
    bool CopyClaimedProjectionForBoard(uint32_t board, HandSnapshot* output) const;
    bool ClearBoard(uint32_t board);
    ProjectionClaimResult TryClaimProjection(uint32_t board, uint32_t generation);
    bool TryClaimAwardCount(uint32_t board, uint32_t generation,
        Category* category);
    bool QueueDuplicateAwardSkip(uint32_t board, uint32_t generation);
    bool ConsumeDuplicateAwardSkip(uint32_t board, uint32_t generation);
    bool Clear(uint32_t board, uint32_t generation);

private:
    enum { kCapacity = 16 };
    struct Entry {
        bool active;
        bool projection_claimed;
        bool award_counted;
        uint32_t pending_duplicate_skips;
        HandSnapshot snapshot;
    };

    mutable SRWLOCK lock_;
    Entry entries_[kCapacity];
};

} // namespace bj3six
