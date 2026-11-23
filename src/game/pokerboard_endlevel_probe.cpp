#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

namespace {

const uint32_t kPokerBoardVtable = 0x0081181C;
const uint32_t kPokerBoardEndDialogCreator = 0x006BB590;
const uint32_t kPokerBoardEndDialogSlotOffset = 0x404;

struct ProbeTelemetry {
    volatile LONG sequence;
    uint32_t board;
    volatile LONG update_calls;
    uint32_t request_seen;
    uint32_t creator_target;
    uint32_t creator_called;
};

typedef void (__thiscall *PokerEndDialogCreatorFn)(void* board);

} // namespace

extern "C" __declspec(dllexport) volatile LONG
    g_BJ3EndDialogProbeRequest = 0;
extern "C" __declspec(dllexport) ProbeTelemetry
    g_BJ3EndDialogProbeTelemetry = {};

extern "C" void __cdecl BJ3TryCreatePokerEndLevelDialog(void* board) noexcept {
    if (board == nullptr) return;
    InterlockedIncrement(&g_BJ3EndDialogProbeTelemetry.update_calls);
    if (InterlockedCompareExchange(&g_BJ3EndDialogProbeRequest, 0, 1) != 1)
        return;

    ProbeTelemetry sample = {};
    sample.board = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(board));
    sample.update_calls = static_cast<uint32_t>(g_BJ3EndDialogProbeTelemetry.update_calls);
    sample.request_seen = 1;
    const uint32_t vptr = *reinterpret_cast<const uint32_t*>(board);
    if (vptr == kPokerBoardVtable) {
        const uint32_t* vtable = reinterpret_cast<const uint32_t*>(vptr);
        sample.creator_target = vtable[kPokerBoardEndDialogSlotOffset / 4];
        if (sample.creator_target == kPokerBoardEndDialogCreator) {
            reinterpret_cast<PokerEndDialogCreatorFn>(
                sample.creator_target)(board);
            sample.creator_called = 1;
        }
    }

    InterlockedIncrement(&g_BJ3EndDialogProbeTelemetry.sequence);
    g_BJ3EndDialogProbeTelemetry.board = sample.board;
    g_BJ3EndDialogProbeTelemetry.request_seen = sample.request_seen;
    g_BJ3EndDialogProbeTelemetry.creator_target = sample.creator_target;
    g_BJ3EndDialogProbeTelemetry.creator_called = sample.creator_called;
    MemoryBarrier();
    InterlockedIncrement(&g_BJ3EndDialogProbeTelemetry.sequence);
}


