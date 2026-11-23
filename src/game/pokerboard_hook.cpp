#include "poker_context.h"
#include "game_stats_record.h"
#include "game_statistics_session.h"
#include "sixth_acquisition.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <stdint.h>
#include <string.h>

#include "poker_sidecar.h"
#include "poker_rules.h"
extern "C" __declspec(dllexport) BOOL __stdcall BJ3BuildSpecialUseHand(void*,void*,void*,bj3six::HandSnapshot*) noexcept;
extern "C" __declspec(dllexport) BOOL __stdcall BJ3CaptureAcceptedHandOnce(const bj3six::HandSnapshot*) noexcept;

extern "C" __declspec(dllexport) volatile LONG
    g_BJ3SixCardCategory9AdmissionCount = 0;
extern "C" __declspec(dllexport) volatile LONG
    g_BJ3SixCardTestSeedArm = 0;
extern "C" __declspec(dllexport) volatile LONG
    g_BJ3SixCardTestSeedExpectedBoard = 0;
extern "C" __declspec(dllexport) volatile LONG
    g_BJ3SixCardTestSeedExpectedVptr = 0;
extern "C" __declspec(dllexport) volatile LONG
    g_BJ3SixCardTestSeedStatus = 0;
extern "C" __declspec(dllexport) volatile LONG
    g_BJ3SixCardTestSeedBoard = 0;
extern "C" __declspec(dllexport) volatile LONG
    g_BJ3SixCardTestSeedGeneration = 0;
extern "C" __declspec(dllexport) volatile LONG
    g_BJ3SixCardCategory9AdmissionBoard = 0;
extern "C" __declspec(dllexport) volatile LONG
    g_BJ3SixCardCategory9AdmissionGeneration = 0;
extern "C" __declspec(dllexport) volatile LONG
    g_BJ3SixCardCategory9AdmissionOriginalJpTaken = 0;
extern "C" __declspec(dllexport) volatile LONG
    g_BJ3SixCardCategory9AdmissionForcedFallthrough = 0;

extern "C" __declspec(dllexport)
void BJ3PokerCategory9CurveAdmissionDetour() noexcept;

namespace {

const uint32_t kPokerBoardHandCountOffset = 0x3710;
const uint32_t kPokerBoardHandTableOffset = 0x3238;
const uint32_t kPokerBoardHandStride = 0xF8;
const uint32_t kPokerBoardCapacity = 5;
const uint32_t kPokerBoard188Rva = 0x002B11B0;
const uint32_t kPokerBoard2CCrva = 0x002B9B80;
const uint32_t kPokerBoard30CRva = 0x002C2660;
const uint32_t kPokerBoard58Rva = 0x002C3E50;
const uint32_t kPokerBoard224Rva = 0x00329380;
const uint32_t kPokerBoard3E4Rva = 0x002BE510;
const uint32_t kPokerRankerRva = 0x002B2D50;
const uint32_t kUpdateRankerCallsiteRva = 0x002B6101;
const uint8_t kUpdateRankerCallsiteOriginal[5] = {0xE8, 0x4A, 0xCC, 0xFF, 0xFF};
const uint32_t kCategory9CurveGateRva = 0x002C423C;
const uint8_t kCategory9CurveGateOriginal[8] = {
    0x7A, 0x28, 0x8D, 0xB5, 0x28, 0x36, 0x00, 0x00};
const uint32_t kScoreAwardCallsiteRva = 0x002B6243;
const uint8_t kScoreAwardCallsiteOriginal[5] = {0xE8, 0x58, 0xC7, 0xFF, 0xFF};
const uint32_t kHandBusterCacheLoadRva = 0x002B29CA;
const uint8_t kHandBusterCacheLoadOriginal[6] = {0x8B, 0x9E, 0x20, 0x3A, 0x00, 0x00};
const uint32_t kHandCountdownCacheLoadRva = 0x002B806D;
const uint8_t kHandCountdownCacheLoadOriginal[6] = {0x8B, 0xBE, 0x20, 0x3A, 0x00, 0x00};
const uint32_t kCountFiveD4Return = 0x00712A2F;
const uint32_t kCountFivePairDispatchReturn = 0x0073AD00;
const uint32_t kCountFiveSelectionCleanupReturn = 0x0073B544;

volatile LONG g_generation = 0;
bj3six::SidecarStore g_sidecars;
volatile LONG g_update_ranker_hook_state = 0;
uint8_t g_update_ranker_original[5] = {0xE8, 0x4A, 0xCC, 0xFF, 0xFF};
uint8_t g_update_ranker_patch[5] = {};
uint8_t g_score_award_original[5] = {0xE8, 0x58, 0xC7, 0xFF, 0xFF};
uint8_t g_score_award_patch[5] = {};
uint8_t g_handbuster_cache_original[6] = {0x8B, 0x9E, 0x20, 0x3A, 0x00, 0x00};
uint8_t g_handbuster_cache_patch[6] = {};
uint8_t g_handcountdown_cache_original[6] = {0x8B, 0xBE, 0x20, 0x3A, 0x00, 0x00};
uint8_t g_handcountdown_cache_patch[6] = {};
uintptr_t g_handbuster_cache_continue = 0;
uintptr_t g_handcountdown_cache_continue = 0;
DWORD g_pending_board_tls = TLS_OUT_OF_INDEXES;
DWORD g_pending_generation_tls = TLS_OUT_OF_INDEXES;
volatile LONG g_category9_curve_gate_hook_state = 0;
uint8_t g_category9_curve_gate_original[8] = {
    0x7A, 0x28, 0x8D, 0xB5, 0x28, 0x36, 0x00, 0x00};
uint8_t g_category9_curve_gate_patch[8] = {};
uintptr_t g_category9_curve_gate_fallthrough = 0;
uintptr_t g_category9_curve_gate_taken_target = 0;

struct GateTelemetry {
    volatile LONG sequence;
    uint32_t board;
    uint32_t caller_return;
    uint32_t native_count;
    uint32_t native_result;
    uint32_t returned_result;
    uint32_t overridden;
    uint32_t exactly_one_selected_source;
};

struct EventTelemetry {
    volatile LONG sequence;
    uint32_t board;
    uint32_t caller_return;
    uint32_t native_count;
    uint32_t source_candidate;
    uint32_t target_candidate;
    uint32_t target_x;
    uint32_t target_y;
    uint32_t target_primary;
    uint32_t target_alternate;
    uint32_t target_flags;
    uint32_t target_selected;
    uint32_t target_candidate_raw;
    uint32_t capture_gate_flags;
    uint32_t forwarded_return;
    uint32_t capture_committed;
    uint32_t capture_status;
    uint32_t capture_category;
};

constexpr LONG kGateHistoryCapacity = 128;
constexpr LONG kEventHistoryCapacity = 64;
constexpr LONG kD4GateHistoryCapacity = 64;

struct GateHistoryEntry {
    volatile LONG published_index;
    GateTelemetry sample;
};

struct EventHistoryEntry {
    volatile LONG published_index;
    EventTelemetry sample;
};

struct D4GateHistoryEntry {
    volatile LONG published_index;
    GateTelemetry sample;
};

struct ResetTelemetry {
    volatile LONG sequence;
    uint32_t board;
    uint32_t caller_return;
    uint32_t native_count;
    uint32_t generation;
    uint32_t sidecar_present;
    uint32_t sidecar_cleared;
    uint32_t native_return;
};

struct RestoreTelemetry {
    volatile LONG sequence;
    uint32_t board;
    uint32_t caller_return;
    uint32_t sidecar_generation_before;
    uint32_t sidecar_present_before;
    uint32_t sidecar_cleared_before_forward;
    uint32_t native_return;
    uint32_t native_before_readable;
    uint32_t native_count_before;
    int32_t native_rows_before[kPokerBoardCapacity];
    uint32_t native_after_readable;
    uint32_t native_count_after;
    int32_t native_rows_after[kPokerBoardCapacity];
    uint32_t sidecar_present_after;
};

struct DrawTelemetry {
    volatile LONG sequence;
    uint32_t board;
    uint32_t graphics;
    uint32_t caller_return;
    uint32_t native_count;
    uint32_t sidecar_present;
    uint32_t generation;
    uint32_t card_count;
    uint32_t category;
    uint32_t native_return;
};

struct RankerTelemetry {
    volatile LONG sequence;
    uint32_t board;
    uint32_t generation;
    uint32_t card_count;
    uint32_t sidecar_category;
    uint32_t evaluation_status;
    uint32_t native_rank;
    uint32_t sidecar_present;
    uint32_t returned_rank;
    uint32_t projected_rank;
    uint32_t overridden;
};

struct ProgressionTelemetry {
    volatile LONG sequence;
    uint32_t board;
    uint32_t generation;
    uint32_t native_index;
    uint32_t selected_index;
    uint32_t category;
    uint32_t projected_rank;
    uint32_t overridden;
};

struct AwardTelemetry {
    volatile LONG sequence;
    uint32_t board;
    uint32_t generation;
    uint32_t category;
    uint32_t category_count;
    uint32_t counted;
};

uint32_t ProjectSixCardCategoryToNativeRank(uint32_t category) noexcept {
    return bj3six::CompatibilityRank(static_cast<bj3six::Category>(category));
}

extern "C" void __cdecl BJ3TryCreatePokerEndLevelDialog(void* board) noexcept;

void PublishProgressionTelemetry(ProgressionTelemetry* target, uint32_t board,
        uint32_t generation, uint32_t native_index, uint32_t selected_index,
        uint32_t category, uint32_t projected_rank, bool overridden) noexcept {
    if (target == nullptr) return;
    InterlockedIncrement(&target->sequence);
    target->board = board;
    target->generation = generation;
    target->native_index = native_index;
    target->selected_index = selected_index;
    target->category = category;
    target->projected_rank = projected_rank;
    target->overridden = overridden ? 1u : 0u;
    MemoryBarrier();
    InterlockedIncrement(&target->sequence);
}

bool BoardMatchesSidecar(void* board, const bj3six::HandSnapshot& sidecar) noexcept {
    if (board == nullptr) return false;
    __try {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(board);
        const uint32_t current_vptr = *reinterpret_cast<const uint32_t*>(bytes);
        const uint32_t current_count = *reinterpret_cast<const uint32_t*>(
            bytes + kPokerBoardHandCountOffset);
        // The experiment routes PokerBoard through a per-instance cloned
        // vtable, so compare against the vptr captured with this exact
        // sidecar generation instead of the executable's original vtable.
        return current_vptr == sidecar.board_vptr
            && current_count == kPokerBoardCapacity;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsCurrentPublishedBoard(uint32_t board) noexcept {
    if (board == 0) return false;
    __try {
        const uint32_t app = *reinterpret_cast<const uint32_t*>(0x00887F34u);
        if (app == 0) return false;
        return *reinterpret_cast<const uint32_t*>(
            static_cast<uintptr_t>(app) + 0x0BE0u) == board;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadFullHand(void* board, void* candidate, bj3six::HandSnapshot* output) {
    if (board == nullptr || candidate == nullptr || output == nullptr) return false;
    __try {
        const uint32_t count = *reinterpret_cast<const uint32_t*>(
            reinterpret_cast<const uint8_t*>(board) + kPokerBoardHandCountOffset);
        if (count != kPokerBoardCapacity) return false;

        bj3six::HandSnapshot snapshot = {};
        snapshot.board = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(board));
        snapshot.board_vptr = *reinterpret_cast<const uint32_t*>(board);
        snapshot.generation = static_cast<uint32_t>(InterlockedIncrement(&g_generation));
        snapshot.native_count = count;
        snapshot.card_count = 6;

        const uint8_t* table = reinterpret_cast<const uint8_t*>(board) + kPokerBoardHandTableOffset;
        int32_t values[6] = {};
        for (uint32_t index = 0; index != 5; ++index) {
            const int32_t value = *reinterpret_cast<const int32_t*>(table + index * kPokerBoardHandStride);
            values[index] = value;
            snapshot.cards[index].selected_value = value;
            snapshot.cards[index].primary_value = value;
            snapshot.cards[index].alternate_value = *reinterpret_cast<const int32_t*>(table + index * kPokerBoardHandStride + 4);
        }

        const uint8_t* item = reinterpret_cast<const uint8_t*>(candidate);
        const int32_t primary = *reinterpret_cast<const int32_t*>(item + 0x220);
        const int32_t alternate = *reinterpret_cast<const int32_t*>(item + 0x21C);
        const uint32_t flags = *reinterpret_cast<const uint32_t*>(item + 0x228);
        const bool marked = (flags & 2u) != 0;
        const int32_t selected = marked ? alternate : primary;
        values[5] = selected;
        snapshot.cards[5].selected_value = selected;
        snapshot.cards[5].primary_value = primary;
        // Native appender initializes row+4 to -2; only an explicit second
        // accepted candidate creates a dual. Candidate+21C is not that proof.
        snapshot.cards[5].alternate_value = marked ? alternate : -2;
        snapshot.cards[5].flags = flags;
        uint8_t masks[6] = {};
        for (uint32_t i=0;i<6;++i) {
            const bool wildcard = i==5 ? marked :
                *reinterpret_cast<const uint32_t*>(table+i*kPokerBoardHandStride+8)==1;
            if(wildcard) { masks[i]=127; snapshot.cards[i].flags |= 2u; }
            else {
                const int32_t allowed_primary=snapshot.cards[i].primary_value;
                const int32_t allowed_alternate=snapshot.cards[i].alternate_value;
                if(allowed_primary>=0 && allowed_primary<=6) masks[i]|=static_cast<uint8_t>(1u<<allowed_primary);
                if(allowed_alternate>=0 && allowed_alternate<=6) masks[i]|=static_cast<uint8_t>(1u<<allowed_alternate);
            }
        }
        snapshot.evaluation = bj3six::EvaluateSixOptionsResolved(masks,6,snapshot.resolved_values);
        for(uint32_t i=0;i<6;++i) {
            uint32_t type=0;
            if(i<5) type=*reinterpret_cast<const uint32_t*>(table+i*kPokerBoardHandStride+8);
            else if(marked) type=1;
            else {
                const int32_t id=*reinterpret_cast<const int32_t*>(item+0x188);
                if(id==*reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(board)+0x3A98)) type=2;
                if(id==*reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(board)+0x3A9C)) type=3;
            }
            snapshot.display_types[i]=type;
            if(type==2 || type==3) snapshot.bonus_values[i]=*reinterpret_cast<const int32_t*>(
                reinterpret_cast<const uint8_t*>(board)+(type==2 ? 0x39F4 : 0x39F8));
        }
        *output = snapshot;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Narrow experiment guard for the second caller of PokerBoard vslot +0x188.
// Its native false result clears the selected source cell at VA 0x0073B555.
// Return true only while a count-five PokerBoard has exactly one valid,
// ordinary selected grid cell, preserving that source latch for the pending
// source-to-destination gesture. Any malformed table fails closed.
bool HasExactlyOneSelectedSourceCell(void* board) noexcept {
    if (board == nullptr) return false;
    __try {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(board);
        if (*reinterpret_cast<const uint32_t*>(bytes + kPokerBoardHandCountOffset) != kPokerBoardCapacity)
            return false;
        const uint32_t board_address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(board));
        uint32_t selected_count = 0;
        const uint32_t* grid = reinterpret_cast<const uint32_t*>(bytes + 0xF4);
        for (uint32_t index = 0; index != 64; ++index) {
            const uint32_t pointer = grid[index];
            if (pointer < 0x10000u || pointer >= 0x7FFF0000u || (pointer & 3u) != 0)
                return false;
            const uint8_t* cell = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(pointer));
            if (*reinterpret_cast<const uint32_t*>(cell) != board_address)
                return false;
            const int32_t x = *reinterpret_cast<const int32_t*>(cell + 0x08);
            const int32_t y = *reinterpret_cast<const int32_t*>(cell + 0x0C);
            if (x < 0 || x >= 8 || y < 0 || y >= 8) return false;
            const uint32_t flags = *reinterpret_cast<const uint32_t*>(cell + 0x228);
            const uint8_t selected = *(cell + 0x23C);
            if (selected > 1) return false;
            if (selected == 1) {
                if (flags != 0) return false;
                if (++selected_count > 1) return false;
            }
        }
        return selected_count == 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadNativeHandSummary(void* board, uint32_t* count, int32_t rows[kPokerBoardCapacity]) noexcept {
    if (board == nullptr || count == nullptr || rows == nullptr) return false;
    __try {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(board);
        *count = *reinterpret_cast<const uint32_t*>(bytes + kPokerBoardHandCountOffset);
        const uint8_t* table = bytes + kPokerBoardHandTableOffset;
        for (uint32_t index = 0; index != kPokerBoardCapacity; ++index) {
            rows[index] = *reinterpret_cast<const int32_t*>(table + index * kPokerBoardHandStride);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

extern "C" __declspec(dllexport) bj3six::HandSnapshot g_BJ3SixCardLastCapture = {};
extern "C" __declspec(dllexport) GateTelemetry g_BJ3SixCardGateLastCall = {};
extern "C" __declspec(dllexport) EventTelemetry g_BJ3SixCardEventLastCall = {};
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardGateHistoryCount = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardGateHistoryOverflow = 0;
extern "C" __declspec(dllexport) GateHistoryEntry
    g_BJ3SixCardGateHistory[kGateHistoryCapacity] = {};
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardEventHistoryCount = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardEventHistoryOverflow = 0;
extern "C" __declspec(dllexport) EventHistoryEntry
    g_BJ3SixCardEventHistory[kEventHistoryCapacity] = {};
extern "C" __declspec(dllexport) GateTelemetry g_BJ3SixCardD4LastCall = {};
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardD4CallCount = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardD4CountFiveCount = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardD4NativeFalseCount = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardD4OverrideCount = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardD4HistoryCount = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardD4HistoryOverflow = 0;
extern "C" __declspec(dllexport) D4GateHistoryEntry
    g_BJ3SixCardD4History[kD4GateHistoryCapacity] = {};
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardCleanupCountFiveCount = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardCleanupSelectedSourceCount = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardCleanupOverrideCount = 0;
extern "C" __declspec(dllexport) ResetTelemetry g_BJ3SixCardResetLastCall = {};
extern "C" __declspec(dllexport) RestoreTelemetry g_BJ3SixCardRestoreLastCall = {};
extern "C" __declspec(dllexport) DrawTelemetry g_BJ3SixCardDrawLastCall = {};
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardDrawEntryCount = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardDrawReturnCount = 0;
extern "C" __declspec(dllexport) RankerTelemetry g_BJ3SixCardRankerLastCall = {};
extern "C" __declspec(dllexport) ProgressionTelemetry g_BJ3SixCardHandBusterProgressionLastCall = {};
extern "C" __declspec(dllexport) ProgressionTelemetry g_BJ3SixCardHandCountdownProgressionLastCall = {};
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardAwardCounts[12] = {};
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardGameCounts[12]={};
extern "C" DWORD WINAPI BJ3InstallDangerHooks(LPVOID) noexcept;
extern "C" __declspec(dllexport) volatile LONG g_BJ3DangerMigrations=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3DangerAddedEvents=0,g_BJ3DangerStepViolations=0;
extern "C" __declspec(dllexport) int32_t g_BJ3DangerLastTransition[5]={};
static bool PrepareDanger(void* board,uint32_t schema=3) noexcept {
    if(!board || g_BJ3PokerContext!=1)return true;
    if(schema==3 && !IsCurrentPublishedBoard(reinterpret_cast<uint32_t>(board)))return true;
    auto bytes=static_cast<uint8_t*>(board);
    if(!bj3six::PrepareDangerFields(reinterpret_cast<int32_t*>(bytes+0x397C),reinterpret_cast<int32_t*>(bytes+0x3980),schema))return false;
    if(schema==2)++g_BJ3DangerMigrations;
    return true;
}
extern "C" __declspec(dllexport) volatile LONG g_BJ3GameStatisticsEpoch=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3GameStatisticsBoard=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3StatsSnapshotNativeCounter=0,g_BJ3StatsSnapshotCardCount=0,g_BJ3StatsSnapshotScore=0,g_BJ3StatsSnapshotSum=0,g_BJ3StatsSnapshotReason=0;
static bj3six::GameStatisticsSession g_game_statistics_session;
static void ObserveGameStatistics(void* board) noexcept {
    const uint32_t address=reinterpret_cast<uint32_t>(board);
    if(bj3six::IsQuestPoker() || !board || !IsCurrentPublishedBoard(address) || *reinterpret_cast<uint32_t*>(board)!=0x0081181Cu)return;
    const uint8_t* bytes=static_cast<uint8_t*>(board);
    const bj3six::GameStatisticsState state={address,
        *reinterpret_cast<const uint32_t*>(bytes+0x3960),
        *reinterpret_cast<const uint32_t*>(bytes+0x3710),
        *reinterpret_cast<const int32_t*>(bytes+0xC84)};
    if(g_game_statistics_session.Observe(state)) {
        for(auto& count:g_BJ3SixCardGameCounts)InterlockedExchange(&count,0);
        g_BJ3GameStatisticsBoard=address;++g_BJ3GameStatisticsEpoch;
    }
}

extern "C" BOOL __stdcall BJ3CopyGameStatistics(void* board,bj3save::GameStats* output) noexcept {
    if(!board || !output || *reinterpret_cast<uint32_t*>(board)!=0x0081181Cu)return FALSE;
    const uint32_t address=reinterpret_cast<uint32_t>(board);
    const uint8_t* bytes=static_cast<uint8_t*>(board);bj3save::GameStats state={};
    state.native_hand_counter=*reinterpret_cast<const uint32_t*>(bytes+0x3960);
    state.score=*reinterpret_cast<const uint32_t*>(bytes+0xC84);
    state.native_card_count=*reinterpret_cast<const uint32_t*>(bytes+0x3710);
    g_BJ3StatsSnapshotNativeCounter=state.native_hand_counter;g_BJ3StatsSnapshotCardCount=state.native_card_count;g_BJ3StatsSnapshotScore=state.score;
    ObserveGameStatistics(board);
    if(static_cast<uint32_t>(g_BJ3GameStatisticsBoard)!=address){g_BJ3StatsSnapshotReason=-1;return FALSE;}
    uint32_t total=0;
    for(uint32_t i=0;i<11;++i){state.categories[i]=InterlockedCompareExchange(&g_BJ3SixCardGameCounts[i+1],0,0);total+=state.categories[i];}
    g_BJ3StatsSnapshotSum=total;*output=state;
    bj3six::HandSnapshot hand={};
    if(g_sidecars.CopyForBoard(address,&hand) && state.native_card_count==5){g_BJ3StatsSnapshotReason=-2;return FALSE;}
    if(!bj3save::Stable(state)){g_BJ3StatsSnapshotReason=-3;return FALSE;}
    g_BJ3StatsSnapshotReason=1;return TRUE;
}
extern "C" BOOL __stdcall BJ3ImportGameStatistics(void* board,const bj3save::GameStats* state) noexcept {
    if(!board || !state || *reinterpret_cast<uint32_t*>(board)!=0x0081181Cu || !bj3save::Stable(*state))return FALSE;
    const uint8_t* bytes=static_cast<uint8_t*>(board);
    if(*reinterpret_cast<const uint32_t*>(bytes+0x3960)!=state->native_hand_counter ||
       *reinterpret_cast<const uint32_t*>(bytes+0xC84)!=state->score ||
       *reinterpret_cast<const uint32_t*>(bytes+0x3710)!=state->native_card_count)return FALSE;
    if(!PrepareDanger(board,state->danger_schema))return FALSE;
    g_game_statistics_session=bj3six::GameStatisticsSession{};
    g_game_statistics_session.Observe({reinterpret_cast<uint32_t>(board),state->native_hand_counter,state->native_card_count,static_cast<int32_t>(state->score)});
    for(uint32_t i=0;i<11;++i)InterlockedExchange(&g_BJ3SixCardGameCounts[i+1],state->categories[i]);
    InterlockedExchange(&g_BJ3SixCardGameCounts[0],0);
    g_BJ3GameStatisticsBoard=reinterpret_cast<uint32_t>(board);++g_BJ3GameStatisticsEpoch;return TRUE;
}

extern "C" __declspec(dllexport) AwardTelemetry g_BJ3SixCardAwardLastCall = {};
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardDuplicateAwardRequestCount = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardDuplicateAwardSkipCount = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardScoreGateLastBoard = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardScoreGateLastGeneration = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixCardScoreGateLastAction = 0;

namespace {

enum ScoreGateAction {
    kScoreGateForwarded = 1,
    kScoreGateSuppressed = 2,
    kScoreGateStaleOrUnmatched = 3,
};

void PublishScoreGateAction(uint32_t board, uint32_t generation, LONG action) noexcept {
    InterlockedExchange(&g_BJ3SixCardScoreGateLastBoard, static_cast<LONG>(board));
    InterlockedExchange(&g_BJ3SixCardScoreGateLastGeneration,
        static_cast<LONG>(generation));
    InterlockedExchange(&g_BJ3SixCardScoreGateLastAction, action);
}

} // namespace

namespace {

void TrySeedTwoTripletsForTest(uint32_t board) noexcept {
    if (InterlockedCompareExchange(&g_BJ3SixCardTestSeedArm, 0, 0) != 1)
        return;

    if (board == 0 || board != static_cast<uint32_t>(
            InterlockedCompareExchange(&g_BJ3SixCardTestSeedExpectedBoard, 0, 0)) ||
        !IsCurrentPublishedBoard(board)) return;

    void* board_pointer = reinterpret_cast<void*>(static_cast<uintptr_t>(board));
    uint32_t current_vptr = 0;
    __try {
        current_vptr = *reinterpret_cast<const uint32_t*>(board_pointer);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    const uint32_t expected_vptr = static_cast<uint32_t>(
        InterlockedCompareExchange(&g_BJ3SixCardTestSeedExpectedVptr, 0, 0));
    if (expected_vptr == 0 || current_vptr != expected_vptr) return;

    uint32_t native_count = 0;
    int32_t native_rows[kPokerBoardCapacity] = {};
    if (!ReadNativeHandSummary(board_pointer, &native_count, native_rows) ||
        native_count != kPokerBoardCapacity) return;
    if (InterlockedCompareExchange(&g_BJ3SixCardTestSeedArm, 0, 1) != 1)
        return;
    InterlockedExchange(&g_BJ3SixCardTestSeedStatus, 10);
    const int32_t values[6] = {3, 3, 3, 6, 6, 6};
    bj3six::HandSnapshot snapshot = {};
    snapshot.board = board;
    snapshot.board_vptr = current_vptr;
    snapshot.generation = static_cast<uint32_t>(InterlockedIncrement(&g_generation));
    if (snapshot.generation == 0)
        snapshot.generation = static_cast<uint32_t>(InterlockedIncrement(&g_generation));
    snapshot.native_count = native_count;
    snapshot.card_count = 6;
    snapshot.evaluation = bj3six::EvaluateSix(values, 6, false);
    if (snapshot.evaluation.status != bj3six::EvaluationStatus::Classified ||
        snapshot.evaluation.category != bj3six::Category::TwoTriplets ||
        !BoardMatchesSidecar(board_pointer, snapshot)) {
        InterlockedExchange(&g_BJ3SixCardTestSeedStatus, 16);
        return;
    }
    for (uint32_t index = 0; index != 6; ++index) {
        snapshot.cards[index].selected_value = values[index];
        snapshot.cards[index].primary_value = values[index];
        snapshot.cards[index].alternate_value = values[index];
        snapshot.cards[index].flags = 0;
    }
    // Capture replaces only this Board's extension-side entry. Native rows
    // 0-4 and the native count are read-only throughout this test.
    if (!g_sidecars.Capture(snapshot)) {
        InterlockedExchange(&g_BJ3SixCardTestSeedStatus, 17);
        return;
    }
    bj3six::HandSnapshot readback = {};
    if (!g_sidecars.CopyForBoard(board, &readback) ||
        readback.board != board || readback.board_vptr != current_vptr ||
        readback.generation != snapshot.generation || readback.card_count != 6 ||
        readback.native_count != kPokerBoardCapacity ||
        readback.evaluation.category != bj3six::Category::TwoTriplets) {
        g_sidecars.ClearBoard(board);
        InterlockedExchange(&g_BJ3SixCardTestSeedStatus, 18);
        return;
    }
    g_BJ3SixCardLastCapture = snapshot;
    MemoryBarrier();
    InterlockedExchange(&g_BJ3SixCardTestSeedBoard, static_cast<LONG>(board));
    InterlockedExchange(&g_BJ3SixCardTestSeedGeneration,
        static_cast<LONG>(snapshot.generation));
    InterlockedExchange(&g_BJ3SixCardTestSeedStatus, 2);
}

} // namespace

extern "C" __declspec(dllexport) BOOL __stdcall
BJ3ShouldForceCategory9CurveAdmission(uint32_t board,
        uint32_t original_jp_taken) noexcept {
    if (original_jp_taken == 0) return FALSE;
    TrySeedTwoTripletsForTest(board);

    bj3six::HandSnapshot sidecar = {};
    if (!g_sidecars.CopyUnclaimedProjectionForBoard(board, &sidecar) ||
        sidecar.board != board ||
        sidecar.evaluation.category != bj3six::Category::TwoTriplets ||
        !IsCurrentPublishedBoard(board) ||
        !BoardMatchesSidecar(reinterpret_cast<void*>(
            static_cast<uintptr_t>(board)), sidecar)) return FALSE;

    InterlockedExchange(&g_BJ3SixCardCategory9AdmissionBoard,
        static_cast<LONG>(board));
    InterlockedExchange(&g_BJ3SixCardCategory9AdmissionGeneration,
        static_cast<LONG>(sidecar.generation));
    InterlockedExchange(&g_BJ3SixCardCategory9AdmissionOriginalJpTaken, 1);
    InterlockedExchange(&g_BJ3SixCardCategory9AdmissionForcedFallthrough, 1);
    InterlockedIncrement(&g_BJ3SixCardCategory9AdmissionCount);
    return TRUE;
}

extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthSpecialBonusCredited = 0;
struct PendingSixthTotalBonus {uint32_t board,generation;int32_t amount;};
static PendingSixthTotalBonus g_pending_sixth_total_bonus={};
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthBonusTotalPublishes=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthBonusTotalBefore=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthBonusTotalAfter=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthBonusTotalAmount=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SixthBonusTotalGeneration=0;
extern "C" BOOL __stdcall BJ3PublishSixthBonusToTotal(uint32_t board,uint32_t generation) noexcept {
    if(g_pending_sixth_total_bonus.board!=board || g_pending_sixth_total_bonus.generation!=generation ||
        g_pending_sixth_total_bonus.amount<=0) return FALSE;
    bj3six::HandSnapshot active={};
    if(!IsCurrentPublishedBoard(board) || !g_sidecars.CopyForBoard(board,&active) ||
        active.generation!=generation || !BoardMatchesSidecar(reinterpret_cast<void*>(board),active)) return FALSE;
    const int32_t amount=g_pending_sixth_total_bonus.amount;
    int32_t* total=reinterpret_cast<int32_t*>(board+0x3A1Cu);
    const int32_t before=*total;
    *total+=amount;
    // Presentation only: C84/D20/E00 were already credited at the award gate.
    g_pending_sixth_total_bonus={};
    g_BJ3SixthBonusTotalBefore=before;g_BJ3SixthBonusTotalAfter=*total;
    g_BJ3SixthBonusTotalAmount=amount;g_BJ3SixthBonusTotalGeneration=generation;
    ++g_BJ3SixthBonusTotalPublishes;
    return TRUE;
}


struct PendingElevenCharge {uint32_t board,generation,percent;};
static PendingElevenCharge g_pending_eleven_charge={};
extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenBaseSelections=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenRuleBoard=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenRuleGeneration=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenRuleCategory=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenNativeAmount=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenBaseAmount=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenChargePercent=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenChargeEligible=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenChargeApplications=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3ElevenChargeDecisions=0;
extern "C" __declspec(dllexport) float g_BJ3ElevenChargeRange[2]={};
extern "C" BOOL __cdecl BJ3ShouldSkipDuplicateAward(uint32_t board,int32_t* native_amount) noexcept {
    bj3six::HandSnapshot claimed={};
    if(!native_amount || !g_sidecars.CopyClaimedProjectionForBoard(board,&claimed) ||
        !IsCurrentPublishedBoard(board) ||
        !BoardMatchesSidecar(reinterpret_cast<void*>(board),claimed)) return FALSE;
    const bj3six::PokerRule* rule=bj3six::CurrentRuleFor(claimed.evaluation.category);
    if(!rule) return FALSE;
    // Keep native rank-dependent setup, but skip repeat score/progress/counter
    // tails for every six-card category, not only the historical Pyramid case.
    bj3six::Category category=bj3six::Category::Invalid;
    if(!g_sidecars.TryClaimAwardCount(board,claimed.generation,&category)) {
        g_sidecars.ConsumeDuplicateAwardSkip(board,claimed.generation);
        ++g_BJ3SixCardDuplicateAwardSkipCount;
        PublishScoreGateAction(board,claimed.generation,kScoreGateSuppressed);
        return TRUE;
    }
    g_BJ3ElevenRuleBoard=board;g_BJ3ElevenRuleGeneration=claimed.generation;
    g_BJ3ElevenRuleCategory=static_cast<LONG>(category);
    g_BJ3ElevenNativeAmount=*native_amount;g_BJ3ElevenBaseAmount=rule->base_score;
    g_BJ3ElevenChargePercent=rule->charge_percent;g_BJ3ElevenChargeEligible=0;
    *native_amount=rule->base_score;
    g_pending_eleven_charge={board,claimed.generation,rule->charge_percent};
    ++g_BJ3ElevenBaseSelections;
    const uint32_t category_index=static_cast<uint32_t>(category);
    ObserveGameStatistics(reinterpret_cast<void*>(board));
    if(!bj3six::IsQuestPoker())InterlockedIncrement(&g_BJ3SixCardGameCounts[category_index]);
    const LONG category_count=InterlockedIncrement(&g_BJ3SixCardAwardCounts[category_index]);
    const int32_t extra=claimed.bonus_values[5];
    if(extra>0) {
        uint8_t* target=reinterpret_cast<uint8_t*>(board);
        *reinterpret_cast<int32_t*>(target+0xC84)+=extra;
        *reinterpret_cast<int32_t*>(target+0xD20)+=extra;
        *reinterpret_cast<int32_t*>(target+0xE00)+=extra;
        g_pending_sixth_total_bonus={board,claimed.generation,extra};
        InterlockedExchange(&g_BJ3SixthSpecialBonusCredited,extra);
    }
    InterlockedIncrement(&g_BJ3SixCardAwardLastCall.sequence);
    g_BJ3SixCardAwardLastCall.board=board;g_BJ3SixCardAwardLastCall.generation=claimed.generation;
    g_BJ3SixCardAwardLastCall.category=category_index;
    g_BJ3SixCardAwardLastCall.category_count=category_count;
    g_BJ3SixCardAwardLastCall.counted=1;
    MemoryBarrier();InterlockedIncrement(&g_BJ3SixCardAwardLastCall.sequence);
    PublishScoreGateAction(board,claimed.generation,kScoreGateForwarded);
    return FALSE;
}

// Called at the existing cache-load seam before the native progress guards.
// Return -1 to take the native "no HandBuster row" branch; native countdown,
// sound and rank consumers keep their valid seven-class compatibility state.
static bool ApplyElevenCharge(uint32_t board,const bj3six::HandSnapshot& hand,uint32_t native_index) noexcept {
    if(g_pending_eleven_charge.board!=board || g_pending_eleven_charge.generation!=hand.generation) return false;
    const uint32_t percent=g_pending_eleven_charge.percent;
    g_pending_eleven_charge={};
    uint8_t* bytes=reinterpret_cast<uint8_t*>(board);
    float* target=reinterpret_cast<float*>(bytes+0x3974);
    const float before=*target;
    const bool eligible=static_cast<int32_t>(native_index)>-1 &&
        *reinterpret_cast<const int32_t*>(bytes+0x397C)>-1;
    if(eligible) {*target=bj3six::ChargedTarget(before,percent);++g_BJ3ElevenChargeApplications;}
    g_BJ3ElevenChargeEligible=eligible ? 1:0;
    g_BJ3ElevenChargeRange[0]=before;g_BJ3ElevenChargeRange[1]=*target;
    ++g_BJ3ElevenChargeDecisions;
    return true;
}

extern "C" __declspec(dllexport) __declspec(naked)
void BJ3DuplicateAwardScoreGate() noexcept {
    __asm {
        pushfd
        pushad
        sub esp, 4
        lea eax,[esp+4]
        push eax
        push esi
        call BJ3ShouldSkipDuplicateAward
        add esp, 8
        mov dword ptr [esp], eax
        cmp dword ptr [esp], 0
        je forward_native_score_helper
        add esp, 4
        popad
        popfd
        mov dword ptr [esp], 006B624Fh
        ret
    forward_native_score_helper:
        add esp, 4
        popad
        popfd
        push 006B29A0h
        ret
    }
}

namespace {

void PublishEventTelemetry(const EventTelemetry& sample) noexcept {
    InterlockedIncrement(&g_BJ3SixCardEventLastCall.sequence);
    g_BJ3SixCardEventLastCall.board = sample.board;
    g_BJ3SixCardEventLastCall.caller_return = sample.caller_return;
    g_BJ3SixCardEventLastCall.native_count = sample.native_count;
    g_BJ3SixCardEventLastCall.source_candidate = sample.source_candidate;
    g_BJ3SixCardEventLastCall.target_candidate = sample.target_candidate;
    g_BJ3SixCardEventLastCall.target_x = sample.target_x;
    g_BJ3SixCardEventLastCall.target_y = sample.target_y;
    g_BJ3SixCardEventLastCall.target_primary = sample.target_primary;
    g_BJ3SixCardEventLastCall.target_alternate = sample.target_alternate;
    g_BJ3SixCardEventLastCall.target_flags = sample.target_flags;
    g_BJ3SixCardEventLastCall.target_selected = sample.target_selected;
    g_BJ3SixCardEventLastCall.target_candidate_raw = sample.target_candidate_raw;
    g_BJ3SixCardEventLastCall.capture_gate_flags = sample.capture_gate_flags;
    g_BJ3SixCardEventLastCall.forwarded_return = sample.forwarded_return;
    g_BJ3SixCardEventLastCall.capture_committed = sample.capture_committed;
    g_BJ3SixCardEventLastCall.capture_status = sample.capture_status;
    g_BJ3SixCardEventLastCall.capture_category = sample.capture_category;
    MemoryBarrier();
    InterlockedIncrement(&g_BJ3SixCardEventLastCall.sequence);
}

void RecordCountFiveGateHistory(const GateTelemetry& sample) noexcept {
    if (sample.native_count != kPokerBoardCapacity ||
            (sample.caller_return != kCountFiveD4Return &&
             sample.caller_return != kCountFiveSelectionCleanupReturn)) return;
    const LONG ordinal = InterlockedIncrement(&g_BJ3SixCardGateHistoryCount);
    if (ordinal <= 0 || ordinal > kGateHistoryCapacity) {
        InterlockedIncrement(&g_BJ3SixCardGateHistoryOverflow);
        return;
    }
    GateHistoryEntry& entry = g_BJ3SixCardGateHistory[ordinal - 1];
    memcpy(&entry.sample, &sample, sizeof(sample));
    MemoryBarrier();
    InterlockedExchange(&entry.published_index, ordinal);
}

void PublishD4GateLastCall(const GateTelemetry& sample) noexcept {
    InterlockedIncrement(&g_BJ3SixCardD4LastCall.sequence);
    g_BJ3SixCardD4LastCall.board = sample.board;
    g_BJ3SixCardD4LastCall.caller_return = sample.caller_return;
    g_BJ3SixCardD4LastCall.native_count = sample.native_count;
    g_BJ3SixCardD4LastCall.native_result = sample.native_result;
    g_BJ3SixCardD4LastCall.returned_result = sample.returned_result;
    g_BJ3SixCardD4LastCall.overridden = sample.overridden;
    g_BJ3SixCardD4LastCall.exactly_one_selected_source =
        sample.exactly_one_selected_source;
    MemoryBarrier();
    InterlockedIncrement(&g_BJ3SixCardD4LastCall.sequence);
}

void RecordD4CountFiveHistory(const GateTelemetry& sample) noexcept {
    const LONG ordinal = InterlockedIncrement(&g_BJ3SixCardD4HistoryCount);
    if (ordinal <= 0 || ordinal > kD4GateHistoryCapacity) {
        InterlockedIncrement(&g_BJ3SixCardD4HistoryOverflow);
        return;
    }
    D4GateHistoryEntry& entry = g_BJ3SixCardD4History[ordinal - 1];
    memcpy(&entry.sample, &sample, sizeof(sample));
    MemoryBarrier();
    InterlockedExchange(&entry.published_index, ordinal);
}

void RecordEventHistory(const EventTelemetry& sample) noexcept {
    const LONG ordinal = InterlockedIncrement(&g_BJ3SixCardEventHistoryCount);
    if (ordinal <= 0 || ordinal > kEventHistoryCapacity) {
        InterlockedIncrement(&g_BJ3SixCardEventHistoryOverflow);
        return;
    }
    EventHistoryEntry& entry = g_BJ3SixCardEventHistory[ordinal - 1];
    memcpy(&entry.sample, &sample, sizeof(sample));
    MemoryBarrier();
    InterlockedExchange(&entry.published_index, ordinal);
}

void PublishResetTelemetry(const ResetTelemetry& sample) noexcept {
    InterlockedIncrement(&g_BJ3SixCardResetLastCall.sequence);
    g_BJ3SixCardResetLastCall.board = sample.board;
    g_BJ3SixCardResetLastCall.caller_return = sample.caller_return;
    g_BJ3SixCardResetLastCall.native_count = sample.native_count;
    g_BJ3SixCardResetLastCall.generation = sample.generation;
    g_BJ3SixCardResetLastCall.sidecar_present = sample.sidecar_present;
    g_BJ3SixCardResetLastCall.sidecar_cleared = sample.sidecar_cleared;
    g_BJ3SixCardResetLastCall.native_return = sample.native_return;
    MemoryBarrier();
    InterlockedIncrement(&g_BJ3SixCardResetLastCall.sequence);
}

void PublishRestoreTelemetry(const RestoreTelemetry& sample) noexcept {
    InterlockedIncrement(&g_BJ3SixCardRestoreLastCall.sequence);
    g_BJ3SixCardRestoreLastCall.board = sample.board;
    g_BJ3SixCardRestoreLastCall.caller_return = sample.caller_return;
    g_BJ3SixCardRestoreLastCall.sidecar_generation_before = sample.sidecar_generation_before;
    g_BJ3SixCardRestoreLastCall.sidecar_present_before = sample.sidecar_present_before;
    g_BJ3SixCardRestoreLastCall.sidecar_cleared_before_forward = sample.sidecar_cleared_before_forward;
    g_BJ3SixCardRestoreLastCall.native_return = sample.native_return;
    g_BJ3SixCardRestoreLastCall.native_before_readable = sample.native_before_readable;
    g_BJ3SixCardRestoreLastCall.native_count_before = sample.native_count_before;
    for (uint32_t index = 0; index != kPokerBoardCapacity; ++index) {
        g_BJ3SixCardRestoreLastCall.native_rows_before[index] = sample.native_rows_before[index];
    }
    g_BJ3SixCardRestoreLastCall.native_after_readable = sample.native_after_readable;
    g_BJ3SixCardRestoreLastCall.native_count_after = sample.native_count_after;
    for (uint32_t index = 0; index != kPokerBoardCapacity; ++index) {
        g_BJ3SixCardRestoreLastCall.native_rows_after[index] = sample.native_rows_after[index];
    }
    g_BJ3SixCardRestoreLastCall.sidecar_present_after = sample.sidecar_present_after;
    MemoryBarrier();
    InterlockedIncrement(&g_BJ3SixCardRestoreLastCall.sequence);
}

} // namespace

// Observation-only wrapper for the pinned PokerBoard +0x3E4 draw method.
// It records the live Graphics argument, native count and matching sidecar,
// then calls the native method exactly once and forwards its EAX unchanged.
// It does not draw or read/write any native hand-table row.
extern "C" __declspec(dllexport) uint32_t __fastcall BJ3PokerBoard3E4DrawObserver(
    void* board, void* unused_edx, void* graphics) noexcept {
    (void)unused_edx;
    DrawTelemetry sample = {};
    sample.board = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(board));
    sample.graphics = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(graphics));
    sample.caller_return = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress()));
    sample.native_count = 0xFFFFFFFFu;

    InterlockedIncrement(&g_BJ3SixCardDrawEntryCount);
    if (board != nullptr) {
        __try {
            sample.native_count = *reinterpret_cast<const uint32_t*>(
                reinterpret_cast<const uint8_t*>(board) + kPokerBoardHandCountOffset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            sample.native_count = 0xFFFFFFFFu;
        }
        bj3six::HandSnapshot sidecar = {};
        if (g_sidecars.CopyForBoard(sample.board, &sidecar)) {
            sample.sidecar_present = 1;
            sample.generation = sidecar.generation;
            sample.card_count = sidecar.card_count;
            sample.category = static_cast<uint32_t>(sidecar.evaluation.category);
        }
    }

    HMODULE executable = GetModuleHandleW(nullptr);
    uint32_t native_return = 0;
    if (executable != nullptr) {
        typedef uint32_t (__thiscall *OriginalFn)(void*, void*);
        OriginalFn original = reinterpret_cast<OriginalFn>(
            reinterpret_cast<uint8_t*>(executable) + kPokerBoard3E4Rva);
        native_return = original(board, graphics);
    }
    sample.native_return = native_return;

    InterlockedIncrement(&g_BJ3SixCardDrawLastCall.sequence);
    g_BJ3SixCardDrawLastCall.board = sample.board;
    g_BJ3SixCardDrawLastCall.graphics = sample.graphics;
    g_BJ3SixCardDrawLastCall.caller_return = sample.caller_return;
    g_BJ3SixCardDrawLastCall.native_count = sample.native_count;
    g_BJ3SixCardDrawLastCall.sidecar_present = sample.sidecar_present;
    g_BJ3SixCardDrawLastCall.generation = sample.generation;
    g_BJ3SixCardDrawLastCall.card_count = sample.card_count;
    g_BJ3SixCardDrawLastCall.category = sample.category;
    g_BJ3SixCardDrawLastCall.native_return = sample.native_return;
    MemoryBarrier();
    InterlockedIncrement(&g_BJ3SixCardDrawLastCall.sequence);
    InterlockedIncrement(&g_BJ3SixCardDrawReturnCount);
    return native_return;
}

// Wrap the original PokerBoard update slot. When its native reset work has
// returned and the fixed hand count is zero, retire this Board's sidecar entry.
// The native update's EAX result is returned unchanged, and the Board is only
// read while the original object is still live.
extern "C" void __stdcall BJ3ClearPendingSpecialUse() noexcept;
extern "C" void __stdcall BJ3DiscardSixthPresentation(void*) noexcept;
extern "C" void __stdcall BJ3UpdateSixthPresentation(void*) noexcept;
extern "C" void __stdcall BJ3PumpDeferredSave(void*) noexcept;
extern "C" void __stdcall BJ3OnSixthAccepted(void*) noexcept;
extern "C" __declspec(dllexport) uint32_t __fastcall BJ3PokerBoard58ResetHook(
    void* board, void* unused_edx) noexcept {
    (void)unused_edx;
    HMODULE executable = GetModuleHandleW(nullptr);
    if (executable == nullptr) return 0;
    typedef uint32_t (__thiscall *OriginalFn)(void*);
    OriginalFn original = reinterpret_cast<OriginalFn>(
        reinterpret_cast<uint8_t*>(executable) + kPokerBoard58Rva);
    if(!bj3six::IsQuestPoker() && !PrepareDanger(board))return 0;
    const int32_t danger_before=board?*reinterpret_cast<int32_t*>(static_cast<uint8_t*>(board)+0x397C):-1;
    ObserveGameStatistics(board);
    const uint32_t native_return = original(board);
    const int32_t danger_after=board?*reinterpret_cast<int32_t*>(static_cast<uint8_t*>(board)+0x397C):-1;
    if(!bj3six::IsQuestPoker() && danger_after!=danger_before) {
        const uint32_t added=bj3six::SkullMaskForThreshold(danger_after)&~bj3six::SkullMaskForThreshold(danger_before);
        if(added)++g_BJ3DangerAddedEvents;
        if(added && (added&(added-1)))++g_BJ3DangerStepViolations;
        g_BJ3DangerLastTransition[0]=danger_before;g_BJ3DangerLastTransition[1]=danger_after;
        g_BJ3DangerLastTransition[2]=added;g_BJ3DangerLastTransition[3]=board?*reinterpret_cast<int32_t*>(static_cast<uint8_t*>(board)+0x3960):0;
        g_BJ3DangerLastTransition[4]=static_cast<int32_t>(bj3six::SkullMaskForThreshold(danger_after));
    }
    ObserveGameStatistics(board);

    BJ3UpdateSixthPresentation(board);
    if (board == nullptr) return native_return;
    BJ3TryCreatePokerEndLevelDialog(board);
    ResetTelemetry telemetry = {};
    bj3six::HandSnapshot existing = {};
    bool readable = false;
    __try {
        telemetry.native_count = *reinterpret_cast<const uint32_t*>(
            reinterpret_cast<const uint8_t*>(board) + kPokerBoardHandCountOffset);
        readable = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        readable = false;
    }
    if(readable && telemetry.native_count==0) {
        BJ3ClearPendingSpecialUse();
        if(g_pending_eleven_charge.board==reinterpret_cast<uint32_t>(board)) g_pending_eleven_charge={};
        if(g_pending_sixth_total_bonus.board==reinterpret_cast<uint32_t>(board)) g_pending_sixth_total_bonus={};
    }
    BJ3PumpDeferredSave(board);
    if (!readable || telemetry.native_count != 0 ||
        !g_sidecars.CopyForBoard(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(board)), &existing)) {
        return native_return;
    }

    telemetry.board = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(board));
    telemetry.caller_return = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress()));
    telemetry.generation = existing.generation;
    telemetry.sidecar_present = 1;
    telemetry.native_return = native_return;
    telemetry.sidecar_cleared = g_sidecars.Clear(telemetry.board, existing.generation) ? 1u : 0u;
    PublishResetTelemetry(telemetry);
    return native_return;
}

// Called only through the PokerBoard-specific cloned +0x224 slot. Invalidate
// the whole Board entry before forwarding: both durable restore and in-place
// snapshot restore can mutate the native five-card state, and a failed reader
// can still leave partial mutations. Clearing the entry also retires its claim
// and any pending duplicate-award skip token. A later +0x30C event may capture
// a fresh sidecar after the restored Board resumes ordinary play.
extern "C" __declspec(dllexport) uint32_t __fastcall BJ3PokerBoard224RestoreHook(
    void* board, void* unused_edx, void* buffer, uint32_t flags) noexcept {
    (void)unused_edx;
    HMODULE executable = GetModuleHandleW(nullptr);
    if (executable == nullptr) return 0;

    RestoreTelemetry telemetry = {};
    telemetry.board = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(board));
    telemetry.caller_return = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress()));
    telemetry.native_before_readable = ReadNativeHandSummary(
        board, &telemetry.native_count_before, telemetry.native_rows_before) ? 1u : 0u;
    bj3six::HandSnapshot existing = {};
    if (g_sidecars.CopyForBoard(telemetry.board, &existing)) {
        telemetry.sidecar_present_before = 1;
        telemetry.sidecar_generation_before = existing.generation;
    }
    telemetry.sidecar_cleared_before_forward =
        g_sidecars.ClearBoard(telemetry.board) ? 1u : 0u;
    g_pending_sixth_total_bonus={};g_pending_eleven_charge={};
    BJ3ClearPendingSpecialUse();BJ3DiscardSixthPresentation(board);

    typedef uint32_t (__thiscall *OriginalFn)(void*, void*, uint32_t);
    OriginalFn original = reinterpret_cast<OriginalFn>(
        reinterpret_cast<uint8_t*>(executable) + kPokerBoard224Rva);
    telemetry.native_return = original(board, buffer, flags);
    telemetry.native_after_readable = ReadNativeHandSummary(
        board, &telemetry.native_count_after, telemetry.native_rows_after) ? 1u : 0u;
    bj3six::HandSnapshot after = {};
    telemetry.sidecar_present_after =
        g_sidecars.CopyForBoard(telemetry.board, &after) ? 1u : 0u;
    PublishRestoreTelemetry(telemetry);
    return telemetry.native_return;
}

// Experiment-only caller-specific count-five adjustments. The D4 exception
// admits the sixth source-input path; the cleanup exception below preserves a
// single live source latch while its destination gesture is pending. All other
// callers and counts keep their native result.
extern "C" __declspec(dllexport) uint32_t __fastcall BJ3PokerBoard188Hook(
    void* board, void* unused_edx) noexcept {
    (void)unused_edx;
    const uint32_t caller = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress()));
    HMODULE executable = GetModuleHandleW(nullptr);
    if (executable == nullptr) return 0;
    typedef uint32_t (__thiscall *OriginalFn)(void*);
    OriginalFn original = reinterpret_cast<OriginalFn>(
        reinterpret_cast<uint8_t*>(executable) + kPokerBoard188Rva);
    const uint32_t native_result = original(board);

    uint32_t count = 0;
    bool readable = false;
    if (board != nullptr) {
        __try {
            count = *reinterpret_cast<const uint32_t*>(
                reinterpret_cast<const uint8_t*>(board) + kPokerBoardHandCountOffset);
            readable = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            readable = false;
        }
    }

    const bool count_five_d4_override = readable && caller == kCountFiveD4Return &&
        count == kPokerBoardCapacity && (native_result & 0xFFu) == 0;
    const bool cleanup_candidate = readable && caller == kCountFiveSelectionCleanupReturn &&
        count == kPokerBoardCapacity && (native_result & 0xFFu) == 0;
    const bool exactly_one_selected_source = cleanup_candidate &&
        HasExactlyOneSelectedSourceCell(board);
    const bool count_five_cleanup_hold = cleanup_candidate && exactly_one_selected_source;
    const bool override = count_five_d4_override || count_five_cleanup_hold;
    const uint32_t returned_result = override ? 1u : native_result;

    GateTelemetry history_sample = {};
    history_sample.board = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(board));
    history_sample.caller_return = caller;
    history_sample.native_count = readable ? count : 0xFFFFFFFFu;
    history_sample.native_result = native_result;
    history_sample.returned_result = returned_result;
    history_sample.overridden = override ? 1u : 0u;
    history_sample.exactly_one_selected_source = exactly_one_selected_source ? 1u : 0u;

    if (caller == kCountFiveD4Return) {
        InterlockedIncrement(&g_BJ3SixCardD4CallCount);
        PublishD4GateLastCall(history_sample);
        if (history_sample.native_count == kPokerBoardCapacity) {
            InterlockedIncrement(&g_BJ3SixCardD4CountFiveCount);
            if ((history_sample.native_result & 0xFFu) == 0)
                InterlockedIncrement(&g_BJ3SixCardD4NativeFalseCount);
            if (history_sample.overridden != 0)
                InterlockedIncrement(&g_BJ3SixCardD4OverrideCount);
            RecordD4CountFiveHistory(history_sample);
        }
    }

    if (caller == kCountFiveSelectionCleanupReturn &&
            history_sample.native_count == kPokerBoardCapacity) {
        InterlockedIncrement(&g_BJ3SixCardCleanupCountFiveCount);
        if (history_sample.exactly_one_selected_source != 0)
            InterlockedIncrement(&g_BJ3SixCardCleanupSelectedSourceCount);
        if (history_sample.overridden != 0)
            InterlockedIncrement(&g_BJ3SixCardCleanupOverrideCount);
    }

    InterlockedIncrement(&g_BJ3SixCardGateLastCall.sequence);
    g_BJ3SixCardGateLastCall.board = history_sample.board;
    g_BJ3SixCardGateLastCall.caller_return = history_sample.caller_return;
    g_BJ3SixCardGateLastCall.native_count = history_sample.native_count;
    g_BJ3SixCardGateLastCall.native_result = history_sample.native_result;
    g_BJ3SixCardGateLastCall.returned_result = history_sample.returned_result;
    g_BJ3SixCardGateLastCall.overridden = history_sample.overridden;
    g_BJ3SixCardGateLastCall.exactly_one_selected_source =
        history_sample.exactly_one_selected_source;
    MemoryBarrier();
    InterlockedIncrement(&g_BJ3SixCardGateLastCall.sequence);
    RecordCountFiveGateHistory(history_sample);
    return returned_result;
}

// This is installed only into a PokerBoard-specific cloned vtable in the
// experiment harness. The candidate's role and caller have not been validated
// on the live six-card route, so this callback must remain pass-through-only.
// The caller-gated +0x30C wrapper is the verified sidecar commit path.
extern "C" __declspec(dllexport) uint32_t __fastcall BJ3PokerBoard2CCHook(
    void* board, void* unused_edx, void* arg1, void* candidate) noexcept {
    (void)unused_edx;
    HMODULE executable = GetModuleHandleW(nullptr);
    if (executable == nullptr) return 0;
    typedef uint32_t (__thiscall *OriginalFn)(void*, void*, void*);
    OriginalFn original = reinterpret_cast<OriginalFn>(
        reinterpret_cast<uint8_t*>(executable) + kPokerBoard2CCrva);
    return original(board, arg1, candidate);
}

// The pinned PokerBoard +0x30C entry is a short forwarding wrapper. On the

struct ActionEvidence {
    uint32_t ordinal,kind,request,board,count,first,second,flags_first,flags_second;
    int32_t color; uint32_t retired,committed;
};
extern "C" __declspec(dllexport) ActionEvidence g_BJ3ActionEvidence[128]={};
extern "C" __declspec(dllexport) volatile LONG g_BJ3ActionEvidenceCount=0;
static uint32_t g_request_id=0;
struct PendingSpecialUse {
    uint32_t request,board,first,second,retired;
    bj3six::HandSnapshot hand;
};
static PendingSpecialUse g_special_use={};
extern "C" void __stdcall BJ3ClearPendingSpecialUse() noexcept {g_special_use={};}
static void RecordAction(uint32_t kind,void* board,uint32_t first,uint32_t second,
        uint32_t fa,uint32_t fb,int32_t color,uint32_t retired,uint32_t commit) noexcept {
    const LONG n=InterlockedIncrement(&g_BJ3ActionEvidenceCount);
    if(n>128) return;
    ActionEvidence e={static_cast<uint32_t>(n),kind,g_request_id,reinterpret_cast<uint32_t>(board),
        board ? *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(board)+0x3710) : 0,
        first,second,fa,fb,color,retired,commit};
    g_BJ3ActionEvidence[n-1]=e;MemoryBarrier();
}
static void StageSpecialUse(void* board,void* first,void* second) noexcept {
    g_special_use={};++g_request_id;
    __try {
        if(!board || !first || !second) return;
        const uint8_t* a=static_cast<uint8_t*>(first),*b=static_cast<uint8_t*>(second);
        if(*reinterpret_cast<void* const*>(a)!=board || *reinterpret_cast<void* const*>(b)!=board) return;
        const uint32_t fa=*reinterpret_cast<const uint32_t*>(a+0x228),fb=*reinterpret_cast<const uint32_t*>(b+0x228);
        RecordAction(1,board,reinterpret_cast<uint32_t>(first),reinterpret_cast<uint32_t>(second),fa,fb,-1,0,0);
        bj3six::HandSnapshot hand={};
        if(!BJ3BuildSpecialUseHand(board,first,second,&hand))return;
        g_special_use={g_request_id,reinterpret_cast<uint32_t>(board),reinterpret_cast<uint32_t>(first),reinterpret_cast<uint32_t>(second),0,hand};
    } __except(EXCEPTION_EXECUTE_HANDLER) {g_special_use={};}
}
extern "C" __declspec(dllexport) uint32_t __fastcall BJ3PokerRetireSpecial(void* board,void*,void* item) noexcept {
    const uint32_t ptr=reinterpret_cast<uint32_t>(item);
    if(g_special_use.board==reinterpret_cast<uint32_t>(board) &&
        g_special_use.request==g_request_id && (ptr==g_special_use.first || ptr==g_special_use.second)) {
        g_special_use.retired=ptr;
        RecordAction(2,board,g_special_use.first,g_special_use.second,0,0,g_special_use.hand.cards[5].selected_value,ptr,0);
    }
    typedef uint32_t (__thiscall *Fn)(void*,void*);
    return reinterpret_cast<Fn>(0x006AF8E0u)(board,item);
}

// observed ordinary event route, arg2/arg3 identify the destination as
// x=arg3, y=arg2. Snapshot that grid object before forwarding; commit it to
// extension storage only for the known count-five caller. Native arguments,
// return value, fixed records, and count are preserved.
extern "C" __declspec(dllexport) uint32_t __fastcall BJ3PokerBoard30CHook(
    void* board, void* unused_edx, void* source_candidate,
    uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6) noexcept {
    (void)unused_edx;
    (void)arg4;
    (void)arg5;
    (void)arg6;
    const uint32_t caller = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress()));
    HMODULE executable = GetModuleHandleW(nullptr);
    if (executable == nullptr) return 0;

    EventTelemetry telemetry = {};
    bj3six::HandSnapshot snapshot = {};
    bool snapshot_ready = false;
    bool target_readable = false;
    uint32_t count = 0xFFFFFFFFu;
    uint32_t target_pointer = 0;
    // Bit map: caller=1, count-five=2, coordinates=4, board gate clear=8,
    // target nonnull=16, target differs from source=32, owner matches=64,
    // coordinates match target=128, six-value snapshot ready=256,
    // sidecar committed=512, sidecar already existed=1024.

    if (board != nullptr) {
        __try {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(board);
            count = *reinterpret_cast<const uint32_t*>(bytes + kPokerBoardHandCountOffset);
            telemetry.board = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(board));
            telemetry.caller_return = caller;
            telemetry.native_count = count;
            telemetry.source_candidate = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(source_candidate));
            telemetry.target_x = arg3;
            telemetry.target_y = arg2;

            const bool caller_matches = caller == kCountFivePairDispatchReturn;
            const bool count_matches = count == kPokerBoardCapacity;
            const bool coordinates_valid = arg2 < 8 && arg3 < 8;
            if (caller_matches) telemetry.capture_gate_flags |= 1u;
            if (count_matches) telemetry.capture_gate_flags |= 2u;
            if (coordinates_valid) telemetry.capture_gate_flags |= 4u;
            if (caller_matches && count_matches && coordinates_valid) {
                const uint32_t board_gate = *reinterpret_cast<const uint32_t*>(bytes + 0x3178);
                if (board_gate == 0) {
                    telemetry.capture_gate_flags |= 8u;
                const uint32_t index = arg3 + arg2 * 8;
                target_pointer = *reinterpret_cast<const uint32_t*>(
                    bytes + 0xF4 + index * sizeof(uint32_t));
                    telemetry.target_candidate_raw = target_pointer;
                    if (target_pointer != 0) telemetry.capture_gate_flags |= 16u;
                    if (target_pointer != 0 && target_pointer != telemetry.source_candidate) {
                        telemetry.capture_gate_flags |= 32u;
                        if (*reinterpret_cast<const uint32_t*>(target_pointer) == telemetry.board) {
                            telemetry.capture_gate_flags |= 64u;
                            const int32_t x = *reinterpret_cast<const int32_t*>(
                                reinterpret_cast<const uint8_t*>(target_pointer) + 0x08);
                            const int32_t y = *reinterpret_cast<const int32_t*>(
                                reinterpret_cast<const uint8_t*>(target_pointer) + 0x0C);
                            if (x == static_cast<int32_t>(arg3) &&
                                    y == static_cast<int32_t>(arg2)) {
                                telemetry.capture_gate_flags |= 128u;
                                target_readable = true;
                            }
                        }
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            target_readable = false;
        }
    }

    telemetry.target_candidate = target_readable ? target_pointer : 0;
    if (target_readable && ReadFullHand(board, reinterpret_cast<void*>(target_pointer), &snapshot)) {
        telemetry.target_primary = static_cast<uint32_t>(snapshot.cards[5].primary_value);
        telemetry.target_alternate = static_cast<uint32_t>(snapshot.cards[5].alternate_value);
        telemetry.target_flags = snapshot.cards[5].flags;
        telemetry.target_selected = static_cast<uint32_t>(snapshot.cards[5].selected_value);
        snapshot_ready = true;
        telemetry.capture_gate_flags |= 256u;
    }

    if(count==5) StageSpecialUse(board,source_candidate, reinterpret_cast<void*>(target_pointer));
    typedef uint32_t (__thiscall *OriginalFn)(void*, void*, uint32_t, uint32_t,
        uint32_t, uint32_t, uint32_t);
    OriginalFn original = reinterpret_cast<OriginalFn>(
        reinterpret_cast<uint8_t*>(executable) + kPokerBoard30CRva);
    const uint32_t forwarded_return = original(board, source_candidate, arg2, arg3, arg4, arg5, arg6);
    telemetry.forwarded_return = forwarded_return;

    // Request-time snapshot is diagnostic only; completed-pair callback owns commit.
    PublishEventTelemetry(telemetry);
    RecordEventHistory(telemetry);
    return forwarded_return;
}

extern "C" __declspec(dllexport) uint32_t __cdecl BJ3SixCardGetLastCapture(
    bj3six::HandSnapshot* output) noexcept {
    if (output == nullptr || g_BJ3SixCardLastCapture.board == 0) return 0;
    MemoryBarrier();
    *output = g_BJ3SixCardLastCapture;
    return 1;
}


extern "C" __declspec(dllexport) volatile LONG g_BJ3CacheSyncWrites=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3CacheSyncGeneration=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3CacheSyncCategory=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3CacheSyncBefore=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3CacheSyncAfter=0;
static bool SyncSixCardCompatibilityCache(uint32_t board,const bj3six::HandSnapshot& hand) noexcept {
    const uint32_t rank=ProjectSixCardCategoryToNativeRank(static_cast<uint32_t>(hand.evaluation.category));
    if(rank<1 || rank>7) return false;
    int32_t* cache=reinterpret_cast<int32_t*>(board+0x3A20u);
    const int32_t selected=static_cast<int32_t>(rank)-1;
    if(*cache!=selected) {
        g_BJ3CacheSyncBefore=*cache;g_BJ3CacheSyncAfter=selected;
        g_BJ3CacheSyncGeneration=hand.generation;
        g_BJ3CacheSyncCategory=static_cast<LONG>(hand.evaluation.category);
        *cache=selected;++g_BJ3CacheSyncWrites;
    }
    return true;
}
struct SkullDecisionEntry {
    uint32_t ordinal,board,generation,category;
    int32_t threshold,cache_before,cache_after;
    uint32_t old_would_coin,would_coin;
};
extern "C" __declspec(dllexport) SkullDecisionEntry g_BJ3SkullDecisions[64]={};
extern "C" __declspec(dllexport) volatile LONG g_BJ3SkullDecisionCalls=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SkullDecisionRecords=0;
extern "C" void __stdcall BJ3RecordSixCardSkullDecision(void* board) noexcept {
    const uint32_t address=reinterpret_cast<uint32_t>(board);
    bj3six::HandSnapshot hand={};
    if(bj3six::IsQuestPoker() || !board || !IsCurrentPublishedBoard(address) ||
        !g_sidecars.CopyForBoard(address,&hand) || !BoardMatchesSidecar(board,hand)) return;
    ++g_BJ3SkullDecisionCalls;
    const int32_t before=*reinterpret_cast<int32_t*>(address+0x3A20u);
    const int32_t threshold=*reinterpret_cast<int32_t*>(address+0x397Cu);
    if(!SyncSixCardCompatibilityCache(address,hand)) return;
    const int32_t after=*reinterpret_cast<int32_t*>(address+0x3A20u);
    const uint32_t category=static_cast<uint32_t>(hand.evaluation.category);
    const LONG count=g_BJ3SkullDecisionRecords;
    if(count>=64) return;
    if(count>0 && count<=64) {
        const SkullDecisionEntry& last=g_BJ3SkullDecisions[count-1];
        if(last.board==address && last.generation==hand.generation && last.threshold==threshold &&
            last.cache_before==before && last.cache_after==after) return;
    }
    const LONG ordinal=InterlockedIncrement(&g_BJ3SkullDecisionRecords);
    if(ordinal>64) return;
    g_BJ3SkullDecisions[ordinal-1]={static_cast<uint32_t>(ordinal),address,hand.generation,category,
        threshold,before,after,threshold>=before ? 1u:0u,bj3six::IsSkullMarked(hand.evaluation.category,threshold) ? 1u:0u};
    MemoryBarrier();
}
extern "C" int32_t __stdcall BJ3DangerCompareValue(void* board) noexcept {
    if(!board)return -1;
    const uint32_t address=reinterpret_cast<uint32_t>(board);
    const int32_t threshold=*reinterpret_cast<int32_t*>(address+0x397C);
    if(bj3six::IsQuestPoker())return threshold;
    bj3six::HandSnapshot hand={};
    if(!g_sidecars.CopyForBoard(address,&hand) || !BoardMatchesSidecar(board,hand))return threshold;
    const int32_t cache=*reinterpret_cast<int32_t*>(address+0x3A20);
    return bj3six::IsSkullMarked(hand.evaluation.category,threshold)?cache:cache-1;
}
extern "C" int32_t __stdcall BJ3DangerNativeCaptionIndex(void* board) noexcept {
    if(!board)return 0;
    const int32_t threshold=*reinterpret_cast<int32_t*>(static_cast<uint8_t*>(board)+0x397C);
    return bj3six::IsQuestPoker()?threshold:bj3six::NativeCaptionTier(threshold);
}
static uintptr_t g_skull_decision_continue=0x006C42BCu;
extern "C" __declspec(dllexport) __declspec(naked) void BJ3SkullDecisionCacheGate() noexcept {
    __asm {
        pushfd
        pushad
        push ebp
        call BJ3RecordSixCardSkullDecision
        push ebp
        call BJ3DangerCompareValue
        mov dword ptr [esp+018h],eax
        popad
        popfd
        jmp dword ptr [g_skull_decision_continue]
    }
}

// Experimental adapter for the update-side E8 call at VA 0x006B6101.
// Every classified six-card chart category is projected through the current
// temporary 1..7 policy. Only repeated Pyramid rank-3 awards use the duplicate
// score-tail skip; all other duplicate calls preserve their native rank.
extern "C" __declspec(dllexport) uint32_t __stdcall BJ3PokerUpdateRankerPassThrough(
    void* board) noexcept {
    HMODULE executable = GetModuleHandleW(nullptr);
    if (executable == nullptr) return 0;

    RankerTelemetry sample = {};
    sample.board = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(board));
    bj3six::HandSnapshot sidecar = {};
    if (g_sidecars.CopyForBoard(sample.board, &sidecar)) {
        sample.sidecar_present = 1;
        sample.generation = sidecar.generation;
        sample.card_count = sidecar.card_count;
        sample.sidecar_category = static_cast<uint32_t>(sidecar.evaluation.category);
        sample.evaluation_status = static_cast<uint32_t>(sidecar.evaluation.status);
    }

    typedef uint32_t (__stdcall *OriginalRanker)(void*);
    OriginalRanker original = reinterpret_cast<OriginalRanker>(
        reinterpret_cast<uint8_t*>(executable) + kPokerRankerRva);
    const uint32_t native_rank = original(board);
    sample.native_rank = native_rank;

    uint32_t returned_rank = native_rank;
    if (BoardMatchesSidecar(board, sidecar)
            && sample.sidecar_present != 0
            && sidecar.board == sample.board
            && sidecar.generation != 0
            && sidecar.native_count == kPokerBoardCapacity
            && sidecar.card_count == 6
            && sidecar.evaluation.status == bj3six::EvaluationStatus::Classified) {
        const uint32_t projection = ProjectSixCardCategoryToNativeRank(sample.sidecar_category);
        if (projection >= 1 && projection <= 7) {
            const bj3six::ProjectionClaimResult claim =
                g_sidecars.TryClaimProjection(sample.board, sidecar.generation);
            if (claim == bj3six::ProjectionClaimResult::Claimed) {
                SyncSixCardCompatibilityCache(sample.board,sidecar);
                sample.projected_rank = projection;
                returned_rank = projection;
                sample.overridden = 1;
            } else if (claim == bj3six::ProjectionClaimResult::AlreadyClaimed &&
                    sidecar.evaluation.category == bj3six::Category::Pyramid &&
                    projection == 3 &&
                    g_sidecars.QueueDuplicateAwardSkip(sample.board, sidecar.generation)) {
                // Keep rank 3 flowing through native rank-specific setup. The
                // second patched callsite skips only the duplicate award tail.
                sample.projected_rank = projection;
                returned_rank = projection;
                sample.overridden = 1;
                InterlockedIncrement(&g_BJ3SixCardDuplicateAwardRequestCount);
            }
        }
    }
    sample.returned_rank = returned_rank;

    InterlockedIncrement(&g_BJ3SixCardRankerLastCall.sequence);
    g_BJ3SixCardRankerLastCall.board = sample.board;
    g_BJ3SixCardRankerLastCall.generation = sample.generation;
    g_BJ3SixCardRankerLastCall.card_count = sample.card_count;
    g_BJ3SixCardRankerLastCall.sidecar_category = sample.sidecar_category;
    g_BJ3SixCardRankerLastCall.evaluation_status = sample.evaluation_status;
    g_BJ3SixCardRankerLastCall.native_rank = sample.native_rank;
    g_BJ3SixCardRankerLastCall.sidecar_present = sample.sidecar_present;
    g_BJ3SixCardRankerLastCall.returned_rank = sample.returned_rank;
    g_BJ3SixCardRankerLastCall.projected_rank = sample.projected_rank;
    g_BJ3SixCardRankerLastCall.overridden = sample.overridden;
    MemoryBarrier();
    InterlockedIncrement(&g_BJ3SixCardRankerLastCall.sequence);
    return returned_rank;
}

// Extension-side progression adapter. The displaced native MOV is replayed by
// each thunk and its value is passed in; absent a claimed, matching six-card
// sidecar this helper returns that value unchanged. For an active projection,
// the seven-entry native curves use the projected one-based rank minus one.
extern "C" uint32_t __cdecl BJ3SelectProgressionCacheIndex(uint32_t board,
        uint32_t native_index, uint32_t consumer) noexcept {
    uint32_t selected_index = native_index;
    uint32_t generation = 0;
    uint32_t category = 0;
    uint32_t projected_rank = 0;
    bool overridden = false;
    bj3six::HandSnapshot sidecar = {};
    void* board_pointer = reinterpret_cast<void*>(static_cast<uintptr_t>(board));
    if (g_sidecars.CopyClaimedProjectionForBoard(board, &sidecar) &&
        sidecar.board == board && BoardMatchesSidecar(board_pointer, sidecar)) {
        generation = sidecar.generation;
        category = static_cast<uint32_t>(sidecar.evaluation.category);
        projected_rank = ProjectSixCardCategoryToNativeRank(category);
        if (projected_rank >= 1 && projected_rank <= 7) {
            if(consumer==1) {
                ApplyElevenCharge(board,sidecar,native_index);
                selected_index=0xFFFFFFFFu; // signed -1 skips native Buster rows.
            } else selected_index = projected_rank - 1;
            overridden = true;
        }
    }

    if (consumer == 1) {
        PublishProgressionTelemetry(&g_BJ3SixCardHandBusterProgressionLastCall,
            board, generation, native_index, selected_index, category,
            projected_rank, overridden);
    } else if (consumer == 2) {
        PublishProgressionTelemetry(&g_BJ3SixCardHandCountdownProgressionLastCall,
            board, generation, native_index, selected_index, category,
            projected_rank, overridden);
    }
    return selected_index;
}

extern "C" __declspec(dllexport) __declspec(naked)
void BJ3HandBusterProgressionIndexDetour() noexcept {
    __asm {
        mov ebx, dword ptr [esi+03A20h]
        pushfd
        pushad
        push 1
        push ebx
        push esi
        call BJ3SelectProgressionCacheIndex
        add esp, 12
        mov dword ptr [esp+16], eax
        popad
        popfd
        jmp dword ptr [g_handbuster_cache_continue]
    }
}

extern "C" __declspec(dllexport) __declspec(naked)
void BJ3HandCountdownProgressionIndexDetour() noexcept {
    __asm {
        mov edi, dword ptr [esi+03A20h]
        pushfd
        pushad
        push 2
        push edi
        push esi
        call BJ3SelectProgressionCacheIndex
        add esp, 12
        mov dword ptr [esp], eax
        popad
        popfd
        jmp dword ptr [g_handcountdown_cache_continue]
    }
}

namespace {

bool ReadCallsite(uint8_t* callsite, uint8_t* output) noexcept {
    if (callsite == nullptr || output == nullptr) return false;
    __try {
        memcpy(output, callsite, 5);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteCallsite(uint8_t* callsite, const uint8_t* bytes) noexcept {
    if (callsite == nullptr || bytes == nullptr) return false;
    DWORD old_protection = 0;
    if (!VirtualProtect(callsite, 5, PAGE_EXECUTE_READWRITE, &old_protection)) return false;
    memcpy(callsite, bytes, 5);
    FlushInstructionCache(GetCurrentProcess(), callsite, 5);
    DWORD ignored_protection = 0;
    const BOOL protection_restored = VirtualProtect(callsite, 5, old_protection,
        &ignored_protection);
    uint8_t readback[5] = {};
    return protection_restored && ReadCallsite(callsite, readback)
        && memcmp(readback, bytes, 5) == 0;
}

bool ReadInlineSite(uint8_t* site, uint8_t* output) noexcept {
    if (site == nullptr || output == nullptr) return false;
    __try {
        memcpy(output, site, 6);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteInlineSite(uint8_t* site, const uint8_t* bytes) noexcept {
    if (site == nullptr || bytes == nullptr) return false;
    DWORD old_protection = 0;
    if (!VirtualProtect(site, 6, PAGE_EXECUTE_READWRITE, &old_protection)) return false;
    memcpy(site, bytes, 6);
    FlushInstructionCache(GetCurrentProcess(), site, 6);
    DWORD ignored_protection = 0;
    const BOOL protection_restored = VirtualProtect(site, 6, old_protection,
        &ignored_protection);
    uint8_t readback[6] = {};
    return protection_restored && ReadInlineSite(site, readback)
        && memcmp(readback, bytes, 6) == 0;
}

bool BuildCallPatch(uint8_t* callsite, const void* destination_function,
        uint8_t* output) noexcept {
    const uintptr_t destination = reinterpret_cast<uintptr_t>(destination_function);
    const uintptr_t return_address = reinterpret_cast<uintptr_t>(callsite)
        + 5;
    const intptr_t displacement = static_cast<intptr_t>(destination)
        - static_cast<intptr_t>(return_address);
    if (displacement < INT32_MIN || displacement > INT32_MAX) return false;
    output[0] = 0xE8;
    const int32_t relative = static_cast<int32_t>(displacement);
    memcpy(output + 1, &relative, sizeof(relative));
    return true;
}

bool BuildInlinePatch(uint8_t* site, const void* destination_function,
        uint8_t* output) noexcept {
    const uintptr_t destination = reinterpret_cast<uintptr_t>(destination_function);
    const uintptr_t return_address = reinterpret_cast<uintptr_t>(site) + 5;
    const intptr_t displacement = static_cast<intptr_t>(destination)
        - static_cast<intptr_t>(return_address);
    if (displacement < INT32_MIN || displacement > INT32_MAX) return false;
    output[0] = 0xE9;
    const int32_t relative = static_cast<int32_t>(displacement);
    memcpy(output + 1, &relative, sizeof(relative));
    output[5] = 0x90;
    return true;
}

bool MatchesEither(const uint8_t* current, const uint8_t* original,
        const uint8_t* patch) noexcept {
    return memcmp(current, original, 5) == 0 || memcmp(current, patch, 5) == 0;
}


static const uint8_t kSkullCacheLoadOriginal[6]={0x8B,0x8D,0x7C,0x39,0,0};
static uint8_t g_skull_cache_load_patch[6]={};
static bool InstallSkullDecisionCacheGate() noexcept {
    uint8_t* site=reinterpret_cast<uint8_t*>(0x006C42B6u);
    uint8_t current[6]={};
    if(!ReadInlineSite(site,current) || !BuildInlinePatch(site,&BJ3SkullDecisionCacheGate,g_skull_cache_load_patch)) return false;
    if(memcmp(current,g_skull_cache_load_patch,6)==0) return true;
    return memcmp(current,kSkullCacheLoadOriginal,6)==0 && WriteInlineSite(site,g_skull_cache_load_patch);
}
static bool RestoreSkullDecisionCacheGate() noexcept {
    uint8_t* site=reinterpret_cast<uint8_t*>(0x006C42B6u);uint8_t current[6]={};
    if(!ReadInlineSite(site,current)) return false;
    if(memcmp(current,kSkullCacheLoadOriginal,6)==0) return true;
    return memcmp(current,g_skull_cache_load_patch,6)==0 && WriteInlineSite(site,kSkullCacheLoadOriginal);
}

bool InstallUpdateAndScoreCallsites() noexcept {
    HMODULE executable = GetModuleHandleW(nullptr);
    if (executable == nullptr || reinterpret_cast<uintptr_t>(executable) != 0x00400000u)
        return false;
    if(!InstallSkullDecisionCacheGate() || BJ3InstallDangerHooks(nullptr)!=1) return false;
    uint8_t* ranker_site = reinterpret_cast<uint8_t*>(executable) + kUpdateRankerCallsiteRva;
    uint8_t* score_site = reinterpret_cast<uint8_t*>(executable) + kScoreAwardCallsiteRva;
    uint8_t* handbuster_site = reinterpret_cast<uint8_t*>(executable) + kHandBusterCacheLoadRva;
    uint8_t* handcountdown_site = reinterpret_cast<uint8_t*>(executable) + kHandCountdownCacheLoadRva;
    uint8_t ranker_current[5] = {};
    uint8_t score_current[5] = {};
    uint8_t handbuster_current[6] = {};
    uint8_t handcountdown_current[6] = {};
    if (!ReadCallsite(ranker_site, ranker_current) ||
        !ReadCallsite(score_site, score_current) ||
        !ReadInlineSite(handbuster_site, handbuster_current) ||
        !ReadInlineSite(handcountdown_site, handcountdown_current) ||
        memcmp(ranker_current, kUpdateRankerCallsiteOriginal, 5) != 0 ||
        memcmp(score_current, kScoreAwardCallsiteOriginal, 5) != 0 ||
        memcmp(handbuster_current, kHandBusterCacheLoadOriginal, 6) != 0 ||
        memcmp(handcountdown_current, kHandCountdownCacheLoadOriginal, 6) != 0 ||
        !BuildCallPatch(ranker_site, &BJ3PokerUpdateRankerPassThrough,
            g_update_ranker_patch) ||
        !BuildCallPatch(score_site, &BJ3DuplicateAwardScoreGate,
            g_score_award_patch) ||
        !BuildInlinePatch(handbuster_site, &BJ3HandBusterProgressionIndexDetour,
            g_handbuster_cache_patch) ||
        !BuildInlinePatch(handcountdown_site, &BJ3HandCountdownProgressionIndexDetour,
            g_handcountdown_cache_patch)) return false;

    memcpy(g_update_ranker_original, ranker_current, 5);
    memcpy(g_score_award_original, score_current, 5);
    memcpy(g_handbuster_cache_original, handbuster_current, 6);
    memcpy(g_handcountdown_cache_original, handcountdown_current, 6);
    g_handbuster_cache_continue = reinterpret_cast<uintptr_t>(handbuster_site) + 6;
    g_handcountdown_cache_continue = reinterpret_cast<uintptr_t>(handcountdown_site) + 6;

    // Install passive downstream adapters first; redirect the ranker last so
    // a projected rank cannot become active before both progress consumers are ready.
    const bool score_written = WriteCallsite(score_site, g_score_award_patch);
    const bool handbuster_written = score_written &&
        WriteInlineSite(handbuster_site, g_handbuster_cache_patch);
    const bool handcountdown_written = handbuster_written &&
        WriteInlineSite(handcountdown_site, g_handcountdown_cache_patch);
    const bool ranker_written = handcountdown_written &&
        WriteCallsite(ranker_site, g_update_ranker_patch);
    uint8_t ranker_readback[5] = {};
    uint8_t score_readback[5] = {};
    uint8_t handbuster_readback[6] = {};
    uint8_t handcountdown_readback[6] = {};
    const bool all_match = ranker_written &&
        ReadCallsite(ranker_site, ranker_readback) &&
        ReadCallsite(score_site, score_readback) &&
        ReadInlineSite(handbuster_site, handbuster_readback) &&
        ReadInlineSite(handcountdown_site, handcountdown_readback) &&
        memcmp(ranker_readback, g_update_ranker_patch, 5) == 0 &&
        memcmp(score_readback, g_score_award_patch, 5) == 0 &&
        memcmp(handbuster_readback, g_handbuster_cache_patch, 6) == 0 &&
        memcmp(handcountdown_readback, g_handcountdown_cache_patch, 6) == 0;
    if (all_match) return true;

    uint8_t current5[5] = {};
    uint8_t current6[6] = {};
    if (ReadCallsite(ranker_site, current5) &&
        memcmp(current5, g_update_ranker_patch, 5) == 0)
        WriteCallsite(ranker_site, g_update_ranker_original);
    if (ReadInlineSite(handcountdown_site, current6) &&
        memcmp(current6, g_handcountdown_cache_patch, 6) == 0)
        WriteInlineSite(handcountdown_site, g_handcountdown_cache_original);
    if (ReadInlineSite(handbuster_site, current6) &&
        memcmp(current6, g_handbuster_cache_patch, 6) == 0)
        WriteInlineSite(handbuster_site, g_handbuster_cache_original);
    if (ReadCallsite(score_site, current5) &&
        memcmp(current5, g_score_award_patch, 5) == 0)
        WriteCallsite(score_site, g_score_award_original);
    return false;
}

bool UninstallUpdateAndScoreCallsites() noexcept {
    HMODULE executable = GetModuleHandleW(nullptr);
    if (executable == nullptr || reinterpret_cast<uintptr_t>(executable) != 0x00400000u)
        return false;
    uint8_t* ranker_site = reinterpret_cast<uint8_t*>(executable) + kUpdateRankerCallsiteRva;
    uint8_t* score_site = reinterpret_cast<uint8_t*>(executable) + kScoreAwardCallsiteRva;
    uint8_t* handbuster_site = reinterpret_cast<uint8_t*>(executable) + kHandBusterCacheLoadRva;
    uint8_t* handcountdown_site = reinterpret_cast<uint8_t*>(executable) + kHandCountdownCacheLoadRva;
    uint8_t ranker_current[5] = {};
    uint8_t score_current[5] = {};
    uint8_t handbuster_current[6] = {};
    uint8_t handcountdown_current[6] = {};
    if (!ReadCallsite(ranker_site, ranker_current) ||
        !ReadCallsite(score_site, score_current) ||
        !ReadInlineSite(handbuster_site, handbuster_current) ||
        !ReadInlineSite(handcountdown_site, handcountdown_current) ||
        !MatchesEither(ranker_current, g_update_ranker_original, g_update_ranker_patch) ||
        !MatchesEither(score_current, g_score_award_original, g_score_award_patch) ||
        !(memcmp(handbuster_current, g_handbuster_cache_original, 6) == 0 ||
          memcmp(handbuster_current, g_handbuster_cache_patch, 6) == 0) ||
        !(memcmp(handcountdown_current, g_handcountdown_cache_original, 6) == 0 ||
          memcmp(handcountdown_current, g_handcountdown_cache_patch, 6) == 0)) return false;

    const bool ranker_was_patched =
        memcmp(ranker_current, g_update_ranker_patch, 5) == 0;
    const bool score_was_patched = memcmp(score_current, g_score_award_patch, 5) == 0;
    const bool handbuster_was_patched =
        memcmp(handbuster_current, g_handbuster_cache_patch, 6) == 0;
    const bool handcountdown_was_patched =
        memcmp(handcountdown_current, g_handcountdown_cache_patch, 6) == 0;
    if (ranker_was_patched && !WriteCallsite(ranker_site, g_update_ranker_original))
        return false;
    if (handcountdown_was_patched &&
        !WriteInlineSite(handcountdown_site, g_handcountdown_cache_original)) {
        if (ranker_was_patched) WriteCallsite(ranker_site, g_update_ranker_patch);
        return false;
    }
    if (handbuster_was_patched &&
        !WriteInlineSite(handbuster_site, g_handbuster_cache_original)) {
        if (handcountdown_was_patched)
            WriteInlineSite(handcountdown_site, g_handcountdown_cache_patch);
        if (ranker_was_patched) WriteCallsite(ranker_site, g_update_ranker_patch);
        return false;
    }
    if (score_was_patched && !WriteCallsite(score_site, g_score_award_original)) {
        if (handbuster_was_patched)
            WriteInlineSite(handbuster_site, g_handbuster_cache_patch);
        if (handcountdown_was_patched)
            WriteInlineSite(handcountdown_site, g_handcountdown_cache_patch);
        if (ranker_was_patched) WriteCallsite(ranker_site, g_update_ranker_patch);
        return false;
    }

    uint8_t ranker_readback[5] = {};
    uint8_t score_readback[5] = {};
    uint8_t handbuster_readback[6] = {};
    uint8_t handcountdown_readback[6] = {};
    return ReadCallsite(ranker_site, ranker_readback) &&
        ReadCallsite(score_site, score_readback) &&
        ReadInlineSite(handbuster_site, handbuster_readback) &&
        ReadInlineSite(handcountdown_site, handcountdown_readback) &&
        memcmp(ranker_readback, g_update_ranker_original, 5) == 0 &&
        memcmp(score_readback, g_score_award_original, 5) == 0 &&
        memcmp(handbuster_readback, g_handbuster_cache_original, 6) == 0 &&
        memcmp(handcountdown_readback, g_handcountdown_cache_original, 6) == 0 &&
        RestoreSkullDecisionCacheGate();
}

bool ReadEightBytes(uint8_t* site, uint8_t* output) noexcept {
    if (site == nullptr || output == nullptr) return false;
    __try {
        memcpy(output, site, 8);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteEightBytes(uint8_t* site, const uint8_t* bytes) noexcept {
    if (site == nullptr || bytes == nullptr) return false;
    DWORD old_protection = 0;
    if (!VirtualProtect(site, 8, PAGE_EXECUTE_READWRITE, &old_protection)) return false;
    memcpy(site, bytes, 8);
    FlushInstructionCache(GetCurrentProcess(), site, 8);
    DWORD ignored_protection = 0;
    const BOOL protection_restored = VirtualProtect(site, 8, old_protection,
        &ignored_protection);
    uint8_t readback[8] = {};
    return protection_restored && ReadEightBytes(site, readback) &&
        memcmp(readback, bytes, 8) == 0;
}

bool BuildCategory9AdmissionPatch(uint8_t* site, uint8_t* output) noexcept {
    const uintptr_t destination = reinterpret_cast<uintptr_t>(
        &BJ3PokerCategory9CurveAdmissionDetour);
    const uintptr_t return_address = reinterpret_cast<uintptr_t>(site) + 5;
    const intptr_t displacement = static_cast<intptr_t>(destination) -
        static_cast<intptr_t>(return_address);
    if (displacement < INT32_MIN || displacement > INT32_MAX) return false;
    output[0] = 0xE9;
    const int32_t relative = static_cast<int32_t>(displacement);
    memcpy(output + 1, &relative, sizeof(relative));
    output[5] = 0x90;
    output[6] = 0x90;
    output[7] = 0x90;
    return true;
}

bool InstallCategory9CurveAdmissionHook() noexcept {
    HMODULE executable = GetModuleHandleW(nullptr);
    if (executable == nullptr || reinterpret_cast<uintptr_t>(executable) != 0x00400000u)
        return false;
    uint8_t* site = reinterpret_cast<uint8_t*>(executable) + kCategory9CurveGateRva;
    uint8_t current[8] = {};
    if (!ReadEightBytes(site, current) ||
        memcmp(current, kCategory9CurveGateOriginal, 8) != 0 ||
        !BuildCategory9AdmissionPatch(site, g_category9_curve_gate_patch)) return false;

    memcpy(g_category9_curve_gate_original, current, 8);
    g_category9_curve_gate_fallthrough = reinterpret_cast<uintptr_t>(site) + 8;
    g_category9_curve_gate_taken_target = reinterpret_cast<uintptr_t>(site) + 2 + 0x28;
    if (g_category9_curve_gate_fallthrough != 0x006C4244u ||
        g_category9_curve_gate_taken_target != 0x006C4266u) return false;

    if (WriteEightBytes(site, g_category9_curve_gate_patch)) return true;
    if (ReadEightBytes(site, current) &&
        memcmp(current, g_category9_curve_gate_patch, 8) == 0)
        WriteEightBytes(site, g_category9_curve_gate_original);
    return false;
}

bool UninstallCategory9CurveAdmissionHook() noexcept {
    HMODULE executable = GetModuleHandleW(nullptr);
    if (executable == nullptr || reinterpret_cast<uintptr_t>(executable) != 0x00400000u)
        return false;
    uint8_t* site = reinterpret_cast<uint8_t*>(executable) + kCategory9CurveGateRva;
    uint8_t current[8] = {};
    if (!ReadEightBytes(site, current)) return false;
    if (memcmp(current, kCategory9CurveGateOriginal, 8) == 0) return true;
    if (memcmp(current, g_category9_curve_gate_patch, 8) != 0) return false;
    return WriteEightBytes(site, g_category9_curve_gate_original);
}

} // namespace

// Invoke only while all target threads are quiescent. The same-bitness startup
// helper loads this DLL and calls the installer before resuming game threads.
extern "C" __declspec(dllexport) DWORD WINAPI BJ3InstallUpdateRankerCallsiteHook(
    LPVOID unused) noexcept {
    (void)unused;
    const LONG previous = InterlockedCompareExchange(&g_update_ranker_hook_state, 1, 0);
    if (previous == 2) {
        HMODULE executable = GetModuleHandleW(nullptr);
        if (executable == nullptr) return 0;
        uint8_t ranker_current[5] = {};
        uint8_t score_current[5] = {};
        uint8_t handbuster_current[6] = {};
        uint8_t handcountdown_current[6] = {};
        uint8_t* ranker_site = reinterpret_cast<uint8_t*>(executable) +
            kUpdateRankerCallsiteRva;
        uint8_t* score_site = reinterpret_cast<uint8_t*>(executable) +
            kScoreAwardCallsiteRva;
        uint8_t* handbuster_site = reinterpret_cast<uint8_t*>(executable) +
            kHandBusterCacheLoadRva;
        uint8_t* handcountdown_site = reinterpret_cast<uint8_t*>(executable) +
            kHandCountdownCacheLoadRva;
        return InstallSkullDecisionCacheGate() && ReadCallsite(ranker_site, ranker_current) &&
            ReadCallsite(score_site, score_current) &&
            ReadInlineSite(handbuster_site, handbuster_current) &&
            ReadInlineSite(handcountdown_site, handcountdown_current) &&
            memcmp(ranker_current, g_update_ranker_patch, 5) == 0 &&
            memcmp(score_current, g_score_award_patch, 5) == 0 &&
            memcmp(handbuster_current, g_handbuster_cache_patch, 6) == 0 &&
            memcmp(handcountdown_current, g_handcountdown_cache_patch, 6) == 0 ? 1 : 0;
    }
    if (previous != 0) return 0;
    const bool installed = InstallUpdateAndScoreCallsites();
    InterlockedExchange(&g_update_ranker_hook_state, installed ? 2 : -1);
    return installed ? 1 : 0;
}

extern "C" __declspec(dllexport) DWORD WINAPI BJ3UninstallUpdateRankerCallsiteHook(
    LPVOID unused) noexcept {
    (void)unused;
    const LONG previous = InterlockedCompareExchange(&g_update_ranker_hook_state, 3, 2);
    if (previous != 2 && previous != -1) return 0;
    if (previous == -1 && InterlockedCompareExchange(&g_update_ranker_hook_state, 3, -1) != -1)
        return 0;
    const bool removed = UninstallUpdateAndScoreCallsites();
    InterlockedExchange(&g_update_ranker_hook_state, removed ? 0 : previous);
    return removed ? 1 : 0;
}

extern "C" __declspec(dllexport) __declspec(naked)
void BJ3PokerCategory9CurveAdmissionDetour() noexcept {
    __asm {
        pushfd
        pushad
        sub esp, 4
        mov eax, dword ptr [esp+36]
        test eax, 4
        jz force_fallthrough
        mov eax, dword ptr [esp+32]
        and eax, 04500h
        jnz original_jp_taken
        push 1
        mov eax, dword ptr [esp+16]
        push eax
        call BJ3ShouldForceCategory9CurveAdmission
        mov dword ptr [esp], eax
        cmp dword ptr [esp], 0
        jne force_fallthrough
        test dword ptr [esp+36], 4
        jnz original_jp_taken

    force_fallthrough:
        add esp, 4
        popad
        popfd
        lea esi, dword ptr [ebp+03628h]
        jmp dword ptr [g_category9_curve_gate_fallthrough]

    original_jp_taken:
        add esp, 4
        popad
        popfd
        jmp dword ptr [g_category9_curve_gate_taken_target]
    }
}

extern "C" __declspec(dllexport) DWORD WINAPI
BJ3InstallCategory9CurveAdmissionHook(LPVOID unused) noexcept {
    (void)unused;
    const LONG previous = InterlockedCompareExchange(
        &g_category9_curve_gate_hook_state, 1, 0);
    if (previous == 2) {
        HMODULE executable = GetModuleHandleW(nullptr);
        if (executable == nullptr) return 0;
        uint8_t current[8] = {};
        uint8_t* site = reinterpret_cast<uint8_t*>(executable) +
            kCategory9CurveGateRva;
        return ReadEightBytes(site, current) &&
            memcmp(current, g_category9_curve_gate_patch, 8) == 0 ? 1 : 0;
    }
    if (previous != 0) return 0;
    const bool installed = InstallCategory9CurveAdmissionHook();
    InterlockedExchange(&g_category9_curve_gate_hook_state,
        installed ? 2 : -1);
    return installed ? 1 : 0;
}

extern "C" __declspec(dllexport) DWORD WINAPI
BJ3UninstallCategory9CurveAdmissionHook(LPVOID unused) noexcept {
    (void)unused;
    const LONG previous = InterlockedCompareExchange(
        &g_category9_curve_gate_hook_state, 3, 2);
    if (previous != 2 && previous != -1) return 0;
    if (previous == -1 && InterlockedCompareExchange(
            &g_category9_curve_gate_hook_state, 3, -1) != -1) return 0;
    const bool removed = UninstallCategory9CurveAdmissionHook();
    InterlockedExchange(&g_category9_curve_gate_hook_state,
        removed ? 0 : previous);
    return removed ? 1 : 0;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}

// Shared with the +0x0AC face-draw callback. This copies the actual classified
// extension sidecar; it never reads a sixth native record.
extern "C" __declspec(dllexport) BOOL __stdcall
BJ3CopyClassifiedSixthCardForDraw(uint32_t board, uint32_t board_vptr,
        uint32_t* generation, int32_t* gem_value,
        uint32_t* category) noexcept {
    if (board == 0 || generation == nullptr || gem_value == nullptr ||
        category == nullptr ||
        !IsCurrentPublishedBoard(board)) return FALSE;

    bj3six::HandSnapshot sidecar = {};
    if (!g_sidecars.CopyForBoard(board, &sidecar) ||
        sidecar.board != board || sidecar.board_vptr != board_vptr ||
        sidecar.generation == 0 || sidecar.native_count != kPokerBoardCapacity ||
        sidecar.card_count != 6 ||
        sidecar.evaluation.status != bj3six::EvaluationStatus::Classified ||
        !BoardMatchesSidecar(reinterpret_cast<void*>(static_cast<uintptr_t>(board)),
            sidecar)) return FALSE;

    const int32_t selected = sidecar.cards[5].selected_value;
    if (selected < 0 || selected > 6) return FALSE;
    *generation = sidecar.generation;
    *gem_value = selected;
    *category = static_cast<uint32_t>(sidecar.evaluation.category);
    return TRUE;
}

// Run392: derived from the active per-Board sidecar rather than native row six.
extern "C" uint32_t __stdcall BJ3ShouldWaitForSix(void* board) noexcept {
    if (!board) return 0;
    __try {
        const uint8_t* bytes = static_cast<const uint8_t*>(board);
        if (*reinterpret_cast<const uint32_t*>(bytes) != 0x0081181Cu ||
            *reinterpret_cast<const uint32_t*>(bytes + kPokerBoardHandCountOffset) != 5)
            return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    bj3six::HandSnapshot snapshot = {};
    const bool ready = g_sidecars.CopyForBoard(
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(board)), &snapshot) &&
        snapshot.card_count == 6 && snapshot.generation != 0 &&
        snapshot.evaluation.status == bj3six::EvaluationStatus::Classified;
    return ready ? 0 : 1;
}

// Copies active extension storage only; no historical or synthetic hand.
extern "C" BOOL __stdcall BJ3CopyActiveHandForDisplay(uint32_t board,
        uint32_t vptr, bj3six::HandSnapshot* output) noexcept {
    if (!output || !IsCurrentPublishedBoard(board)) return FALSE;
    bj3six::HandSnapshot hand = {};
    if (!g_sidecars.CopyForBoard(board, &hand) || hand.board_vptr != vptr ||
        hand.generation == 0 || hand.card_count != 6 ||
        hand.evaluation.status != bj3six::EvaluationStatus::Classified ||
        !BoardMatchesSidecar(reinterpret_cast<void*>(board), hand)) return FALSE;
    *output = hand;
    return TRUE;
}

extern "C" __declspec(dllexport) volatile LONG g_BJ3CompletedPairCalls = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3CompletedPairCommits = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3CompletedPairRejected = 0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3CancelledPairCalls = 0;
extern "C" __declspec(dllexport) uint32_t __fastcall BJ3PokerCompletedPair(
        void* board, void*, void* record) noexcept {
    InterlockedIncrement(&g_BJ3CompletedPairCalls);
    bj3six::HandSnapshot pending = {};
    bool ready = false;
    SixthGemSource visual={};bool visual_ready=false;
    __try {
        const uint8_t* bytes = static_cast<const uint8_t*>(board);
        if (board && record && IsCurrentPublishedBoard(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(board))) &&
            *reinterpret_cast<const uint32_t*>(bytes)==0x0081181Cu &&
            *reinterpret_cast<const uint32_t*>(bytes+0x3710)==5) {
            void* first = reinterpret_cast<void**>(record)[0];
            void* second = reinterpret_cast<void**>(record)[1];
            RecordAction(3,board,reinterpret_cast<uint32_t>(first),reinterpret_cast<uint32_t>(second),0,0,-1,g_special_use.retired,0);
            if(first) {
                visual.x=*reinterpret_cast<float*>(static_cast<uint8_t*>(first)+0x10);
                visual.y=*reinterpret_cast<float*>(static_cast<uint8_t*>(first)+0x14);
                visual.color=*reinterpret_cast<int32_t*>(static_cast<uint8_t*>(first)+0x220);
                visual_ready=true;
            }
            if(first && !second && *(static_cast<uint8_t*>(record)+0xFA)!=0 &&
                g_special_use.board==reinterpret_cast<uint32_t>(board) &&
                g_special_use.request==g_request_id && g_special_use.retired &&
                ((reinterpret_cast<uint32_t>(first)==g_special_use.first && g_special_use.retired==g_special_use.second) ||
                 (reinterpret_cast<uint32_t>(first)==g_special_use.second && g_special_use.retired==g_special_use.first))) {
                bj3six::HandSnapshot existing={};
                if(!g_sidecars.CopyForBoard(reinterpret_cast<uint32_t>(board),&existing)) {
                    pending=g_special_use.hand;
                    ready=pending.evaluation.status==bj3six::EvaluationStatus::Classified;
                }
            }
            if (first && second &&
                *reinterpret_cast<void**>(first)==board && *reinterpret_cast<void**>(second)==board) {
                const uint8_t* a=static_cast<const uint8_t*>(first);
                const uint8_t* b=static_cast<const uint8_t*>(second);
                const int32_t ma=*reinterpret_cast<const int32_t*>(a+0x190);
                const int32_t mb=*reinterpret_cast<const int32_t*>(b+0x190);
                const uint32_t fa=*reinterpret_cast<const uint32_t*>(a+0x228);
                const uint32_t fb=*reinterpret_cast<const uint32_t*>(b+0x228);
                void* selected=first;
                // Mirror native 0x006B9BFB..0x006B9C52 without invoking appender.
                const int64_t delta=static_cast<int64_t>(ma)-mb;
                const bool adjacent=ma>-1 && mb>-1 && (delta==1 || delta==-1) &&
                    !(fa&2u) && !(fb&2u);
                if (!adjacent && (ma==-1 || mb>ma || (fb&2u))) selected=second;
                bj3six::HandSnapshot existing = {};
                if (!g_sidecars.CopyForBoard(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(board)),&existing))
                    ready=ReadFullHand(board,selected,&pending);
                if (ready && adjacent) {
                    visual.alternate_x=*reinterpret_cast<const float*>(b+0x10);
                    visual.alternate_y=*reinterpret_cast<const float*>(b+0x14);
                    visual.alternate_color=*reinterpret_cast<const int32_t*>(b+0x220);
                    visual.dual=1;
                    pending.cards[5].alternate_value=*reinterpret_cast<const int32_t*>(b+0x220);
                    uint8_t masks[6]={};
                    for(uint32_t i=0;i<6;++i) {
                        const bj3six::CardProjection& card=pending.cards[i];
                        if(card.flags&2u) masks[i]=127;
                        else {
                            if(card.primary_value>=0 && card.primary_value<=6) masks[i]|=static_cast<uint8_t>(1u<<card.primary_value);
                            if(card.alternate_value>=0 && card.alternate_value<=6) masks[i]|=static_cast<uint8_t>(1u<<card.alternate_value);
                        }
                    }
                    pending.evaluation=bj3six::EvaluateSixOptionsResolved(masks,6,pending.resolved_values);
                }
                visual.x=*reinterpret_cast<float*>(static_cast<uint8_t*>(selected)+0x10);
                visual.y=*reinterpret_cast<float*>(static_cast<uint8_t*>(selected)+0x14);
                visual.color=*reinterpret_cast<int32_t*>(static_cast<uint8_t*>(selected)+0x220);
                visual_ready=true;
                ready=ready && pending.evaluation.status==bj3six::EvaluationStatus::Classified;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { ready=false; }
    typedef uint32_t (__thiscall *Fn)(void*,void*);
    const uint32_t result=reinterpret_cast<Fn>(0x006B9BD0u)(board,record);
    if (ready && BJ3CaptureAcceptedHandOnce(&pending)) {
        if(visual_ready) BJ3BeginSixthAcquisition(pending.board,pending.generation,&visual,pending.resolved_values[5]);
        g_BJ3SixCardLastCapture=pending;
        MemoryBarrier();
        RecordAction(4,board,reinterpret_cast<uint32_t>(record),0,0,0,pending.cards[5].selected_value,g_special_use.retired,1);
        g_special_use={};
        InterlockedIncrement(&g_BJ3CompletedPairCommits);
        BJ3OnSixthAccepted(board);
    } else InterlockedIncrement(&g_BJ3CompletedPairRejected);
    return result;
}
extern "C" __declspec(dllexport) uint32_t __fastcall BJ3PokerCancelledPair(
        void* board, void*, void* record) noexcept {
    RecordAction(5,board,reinterpret_cast<uint32_t>(record),0,0,0,-1,g_special_use.retired,0);
    g_special_use={};
    InterlockedIncrement(&g_BJ3CancelledPairCalls);
    typedef uint32_t (__thiscall *Fn)(void*,void*);
    return reinterpret_cast<Fn>(0x006B03B0u)(board,record);
}

// Native shared request routes a validated existing-Hypercube use through
// PokerBoard +2CC (0x00735262 / 0x007352C4), without a C20 +2FC completion.
extern "C" __declspec(dllexport) volatile LONG g_BJ3SpecialUseCalls=0;
extern "C" __declspec(dllexport) volatile LONG g_BJ3SpecialUseCommits=0;
extern "C" __declspec(dllexport) uint32_t __fastcall BJ3PokerSpecialUse(
        void* board,void*,void* hypercube,void* matched) noexcept {
    ++g_BJ3SpecialUseCalls;
    bj3six::HandSnapshot pending={};bool ready=false;
    SixthGemSource visual={};
    __try {
        bj3six::HandSnapshot existing={};
        if(board && hypercube && matched &&
            IsCurrentPublishedBoard(reinterpret_cast<uint32_t>(board)) &&
            *reinterpret_cast<uint32_t*>(board)==0x0081181Cu &&
            *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(board)+0x3710)==5 &&
            *reinterpret_cast<void**>(hypercube)==board && *reinterpret_cast<void**>(matched)==board &&
            (*reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(hypercube)+0x228)&2u) &&
            !g_sidecars.CopyForBoard(reinterpret_cast<uint32_t>(board),&existing) &&
            BJ3BuildSpecialUseHand(board,hypercube,matched,&pending)) {
            visual.x=*reinterpret_cast<float*>(static_cast<uint8_t*>(matched)+0x10);
            visual.y=*reinterpret_cast<float*>(static_cast<uint8_t*>(matched)+0x14);
            visual.color=*reinterpret_cast<int32_t*>(static_cast<uint8_t*>(matched)+0x220);
            ready=true;
        }
        RecordAction(6,board,reinterpret_cast<uint32_t>(hypercube),reinterpret_cast<uint32_t>(matched),0,0,ready ? pending.cards[5].selected_value:-1,0,0);
    } __except(EXCEPTION_EXECUTE_HANDLER) {ready=false;}
    // The native callback performs the real special-use action via 0x00714700.
    // Snapshot before forwarding: native processing may consume both objects.
    typedef uint32_t (__thiscall *Fn)(void*,void*,void*);
    const uint32_t result=reinterpret_cast<Fn>(0x006B9B80u)(board,hypercube,matched);
    if(ready && BJ3CaptureAcceptedHandOnce(&pending)) {
        BJ3BeginSixthAcquisition(pending.board,pending.generation,&visual,pending.resolved_values[5]);
        g_BJ3SixCardLastCapture=pending;MemoryBarrier();++g_BJ3SpecialUseCommits;
        BJ3OnSixthAccepted(board);
        RecordAction(4,board,reinterpret_cast<uint32_t>(hypercube),reinterpret_cast<uint32_t>(matched),0,0,pending.cards[5].selected_value,0,1);
        g_special_use={};
    }
    return result;
}

extern "C" BOOL __stdcall BJ3HasSixthTransient(void* board) noexcept {
    if(!board || !IsCurrentPublishedBoard(reinterpret_cast<uint32_t>(board)))return FALSE;
    const uint8_t* b=static_cast<const uint8_t*>(board);
    if(*reinterpret_cast<const uint32_t*>(b)!=0x0081181Cu || *reinterpret_cast<const uint32_t*>(b+0x3710)!=5)return FALSE;
    bj3six::HandSnapshot hand={};
    return g_sidecars.CopyForBoard(reinterpret_cast<uint32_t>(board),&hand) && hand.card_count==6;
}

extern "C" void* __stdcall BJ3CurrentPokerBoard() noexcept {
    const uint32_t app=*reinterpret_cast<uint32_t*>(0x00887F34u);
    if(!app)return nullptr;
    void* board=*reinterpret_cast<void**>(app+0xBE0);
    return board && *reinterpret_cast<uint32_t*>(board)==0x0081181Cu ? board:nullptr;
}

// Shared production builder for request staging and dedicated special completion.
// Both Hypercubes yield ONE wildcard. A Hypercube plus a colored gem remains
// a single ordinary-color card, regardless of argument orientation.
extern "C" __declspec(dllexport) BOOL __stdcall BJ3BuildSpecialUseHand(
        void* board,void* first,void* second,bj3six::HandSnapshot* output) noexcept {
    if(!board || !first || !second || first==second || !output)return FALSE;
    __try {
        if(*reinterpret_cast<void**>(first)!=board || *reinterpret_cast<void**>(second)!=board)return FALSE;
        const auto a=static_cast<const uint8_t*>(first),b=static_cast<const uint8_t*>(second);
        const bool wa=(*reinterpret_cast<const uint32_t*>(a+0x228)&2u)!=0;
        const bool wb=(*reinterpret_cast<const uint32_t*>(b+0x228)&2u)!=0;
        if(!wa && !wb)return FALSE;
        void* selected=wa?second:first;
        bj3six::HandSnapshot hand={};if(!ReadFullHand(board,selected,&hand))return FALSE;
        if(wa && wb) {
            hand.cards[5]={-1,-1,-2,2};hand.display_types[5]=1;
        } else {
            const int32_t color=hand.cards[5].primary_value;
            if(color<0 || color>6)return FALSE;
            hand.cards[5]={color,color,-2,0};hand.display_types[5]=0;
        }
        hand.bonus_values[5]=0;
        uint8_t masks[6]={};
        for(uint32_t i=0;i<6;++i) {
            const auto& card=hand.cards[i];
            if(card.flags&2u)masks[i]=127;
            else {
                if(card.primary_value>=0 && card.primary_value<=6)masks[i]|=static_cast<uint8_t>(1u<<card.primary_value);
                if(card.alternate_value>=0 && card.alternate_value<=6)masks[i]|=static_cast<uint8_t>(1u<<card.alternate_value);
            }
        }
        hand.evaluation=bj3six::EvaluateSixOptionsResolved(masks,6,hand.resolved_values);
        if(hand.evaluation.status!=bj3six::EvaluationStatus::Classified)return FALSE;
        *output=hand;return TRUE;
    } __except(EXCEPTION_EXECUTE_HANDLER){return FALSE;}
}

extern "C" __declspec(dllexport) BOOL __stdcall BJ3CaptureAcceptedHandOnce(const bj3six::HandSnapshot* hand) noexcept {
    if(!hand || hand->card_count!=6 || hand->evaluation.status!=bj3six::EvaluationStatus::Classified)return FALSE;
    bj3six::HandSnapshot existing={};
    // Native completion callbacks run on the Board caller thread. Recheck
    // after forwarding: a nested completion may already have supplied sixth.
    if(g_sidecars.CopyForBoard(hand->board,&existing))return FALSE;
    return g_sidecars.Capture(*hand);
}
