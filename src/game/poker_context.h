#pragma once
#include <stdint.h>
#include "quest_poker_rules.h"
extern "C" volatile long g_BJ3PokerContext; // 0=native, 1=endless, 2=original Quest Poker
namespace bj3six {
inline bool IsQuestPoker() noexcept {return g_BJ3PokerContext==2;}
inline const PokerRule* CurrentRuleFor(Category category) noexcept {
    return IsQuestPoker()?quest::RuleFor(category):RuleFor(category);
}
inline int32_t TableTop() noexcept {return IsQuestPoker()?92:117;}
}
