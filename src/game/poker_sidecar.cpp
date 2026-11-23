#include "poker_sidecar.h"

#include <string.h>

namespace bj3six {

SidecarStore::SidecarStore() {
    InitializeSRWLock(&lock_);
    memset(entries_, 0, sizeof(entries_));
}

bool SidecarStore::Capture(const HandSnapshot& snapshot) {
    if (snapshot.board == 0 || snapshot.card_count != 6) return false;
    AcquireSRWLockExclusive(&lock_);

    Entry* target = nullptr;
    Entry* empty = nullptr;
    for (uint32_t index = 0; index != kCapacity; ++index) {
        Entry& entry = entries_[index];
        if (entry.active && entry.snapshot.board == snapshot.board) {
            target = &entry;
            break;
        }
        if (!entry.active && empty == nullptr) empty = &entry;
    }
    if (target == nullptr) target = empty;
    if (target != nullptr) {
        const bool preserve_generation_state = target->active &&
            target->snapshot.generation == snapshot.generation;
        target->snapshot = snapshot;
        if (!preserve_generation_state) {
            target->projection_claimed = false;
            target->award_counted = false;
            target->pending_duplicate_skips = 0;
        }
        target->active = true;
    }

    ReleaseSRWLockExclusive(&lock_);
    return target != nullptr;
}

bool SidecarStore::CopyForBoard(uint32_t board, HandSnapshot* output) const {
    if (board == 0 || output == nullptr) return false;
    AcquireSRWLockShared(&lock_);
    bool found = false;
    for (uint32_t index = 0; index != kCapacity; ++index) {
        const Entry& entry = entries_[index];
        if (entry.active && entry.snapshot.board == board) {
            *output = entry.snapshot;
            found = true;
            break;
        }
    }
    ReleaseSRWLockShared(&lock_);
    return found;
}

bool SidecarStore::CopyUnclaimedProjectionForBoard(uint32_t board,
        HandSnapshot* output) const {
    if (board == 0 || output == nullptr) return false;
    AcquireSRWLockShared(&lock_);
    bool found = false;
    for (uint32_t index = 0; index != kCapacity; ++index) {
        const Entry& entry = entries_[index];
        if (entry.active && !entry.projection_claimed &&
            entry.snapshot.board == board && entry.snapshot.generation != 0 &&
            entry.snapshot.card_count == 6 && entry.snapshot.native_count == 5 &&
            entry.snapshot.evaluation.status == EvaluationStatus::Classified) {
            *output = entry.snapshot;
            found = true;
            break;
        }
    }
    ReleaseSRWLockShared(&lock_);
    return found;
}

bool SidecarStore::CopyClaimedProjectionForBoard(uint32_t board,
        HandSnapshot* output) const {
    if (board == 0 || output == nullptr) return false;
    AcquireSRWLockShared(&lock_);
    bool found = false;
    for (uint32_t index = 0; index != kCapacity; ++index) {
        const Entry& entry = entries_[index];
        if (entry.active && entry.projection_claimed &&
            entry.snapshot.board == board && entry.snapshot.generation != 0 &&
            entry.snapshot.card_count == 6 && entry.snapshot.native_count == 5 &&
            entry.snapshot.evaluation.status == EvaluationStatus::Classified) {
            *output = entry.snapshot;
            found = true;
            break;
        }
    }
    ReleaseSRWLockShared(&lock_);
    return found;
}

bool SidecarStore::ClearBoard(uint32_t board) {
    if (board == 0) return false;
    AcquireSRWLockExclusive(&lock_);
    bool cleared = false;
    for (uint32_t index = 0; index != kCapacity; ++index) {
        Entry& entry = entries_[index];
        if (entry.active && entry.snapshot.board == board) {
            memset(&entry.snapshot, 0, sizeof(entry.snapshot));
            entry.projection_claimed = false;
            entry.award_counted = false;
            entry.pending_duplicate_skips = 0;
            entry.active = false;
            cleared = true;
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    return cleared;
}

ProjectionClaimResult SidecarStore::TryClaimProjection(uint32_t board, uint32_t generation) {
    if (board == 0 || generation == 0) return ProjectionClaimResult::MissingOrStale;
    AcquireSRWLockExclusive(&lock_);
    ProjectionClaimResult result = ProjectionClaimResult::MissingOrStale;
    for (uint32_t index = 0; index != kCapacity; ++index) {
        Entry& entry = entries_[index];
        if (entry.active && entry.snapshot.board == board &&
            entry.snapshot.generation == generation) {
            if (!entry.projection_claimed) {
                entry.projection_claimed = true;
                result = ProjectionClaimResult::Claimed;
            } else result = ProjectionClaimResult::AlreadyClaimed;
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    return result;
}

bool SidecarStore::TryClaimAwardCount(uint32_t board, uint32_t generation,
        Category* category) {
    if (board == 0 || generation == 0 || category == nullptr) return false;
    AcquireSRWLockExclusive(&lock_);
    bool claimed = false;
    for (uint32_t index = 0; index != kCapacity; ++index) {
        Entry& entry = entries_[index];
        if (entry.active && entry.projection_claimed && !entry.award_counted &&
            entry.snapshot.board == board && entry.snapshot.generation == generation &&
            entry.snapshot.card_count == 6 && entry.snapshot.native_count == 5 &&
            entry.snapshot.evaluation.status == EvaluationStatus::Classified) {
            *category = entry.snapshot.evaluation.category;
            entry.award_counted = true;
            claimed = true;
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    return claimed;
}

bool SidecarStore::QueueDuplicateAwardSkip(uint32_t board, uint32_t generation) {
    if (board == 0 || generation == 0) return false;
    AcquireSRWLockExclusive(&lock_);
    bool queued = false;
    for (uint32_t index = 0; index != kCapacity; ++index) {
        Entry& entry = entries_[index];
        if (entry.active && entry.snapshot.board == board &&
            entry.snapshot.generation == generation && entry.projection_claimed &&
            entry.pending_duplicate_skips != 0xFFFFFFFFu) {
            ++entry.pending_duplicate_skips;
            queued = true;
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    return queued;
}

bool SidecarStore::ConsumeDuplicateAwardSkip(uint32_t board, uint32_t generation) {
    if (board == 0 || generation == 0) return false;
    AcquireSRWLockExclusive(&lock_);
    bool consumed = false;
    for (uint32_t index = 0; index != kCapacity; ++index) {
        Entry& entry = entries_[index];
        if (entry.active && entry.snapshot.board == board &&
            entry.snapshot.generation == generation && entry.pending_duplicate_skips != 0) {
            --entry.pending_duplicate_skips;
            consumed = true;
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    return consumed;
}

bool SidecarStore::Clear(uint32_t board, uint32_t generation) {
    if (board == 0 || generation == 0) return false;
    AcquireSRWLockExclusive(&lock_);
    bool cleared = false;
    for (uint32_t index = 0; index != kCapacity; ++index) {
        Entry& entry = entries_[index];
        if (entry.active && entry.snapshot.board == board &&
            entry.snapshot.generation == generation) {
            memset(&entry.snapshot, 0, sizeof(entry.snapshot));
            entry.projection_claimed = false;
            entry.award_counted = false;
            entry.pending_duplicate_skips = 0;
            entry.active = false;
            cleared = true;
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    return cleared;
}

} // namespace bj3six
