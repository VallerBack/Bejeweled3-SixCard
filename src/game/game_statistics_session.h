#pragma once
#include <stdint.h>
namespace bj3six {
struct GameStatisticsState {
    uint32_t board,native_hand_counter,card_count;
    int32_t score;
};
class GameStatisticsSession {
public:
    bool Observe(const GameStatisticsState& current) noexcept {
        const bool fresh_start=current.native_hand_counter<=1 && current.card_count==0 && current.score==0;
        const bool previous_fresh=previous_.native_hand_counter<=1 && previous_.card_count==0 && previous_.score==0;
        const bool changed=!initialized_ || current.board!=previous_.board ||
            current.native_hand_counter<previous_.native_hand_counter || (fresh_start && !previous_fresh);
        previous_=current;initialized_=true;return changed;
    }
private:
    bool initialized_=false;
    GameStatisticsState previous_={};
};
}
