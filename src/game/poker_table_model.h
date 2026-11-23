#pragma once
#include "poker_context.h"

namespace bj3six {
struct TableRect { int32_t x,y,width,height; };
struct TableRow {
    Category category;
    const char* name;
    const char* description;
    int32_t score;
    uint32_t charge_percent;
    TableRect bounds;
    bool dangerous;
    bool current;
};
// Shared by drawing and mouse hit testing. All coordinates are canvas pixels.
inline TableRow MakeTableRow(uint32_t index,int32_t height,int32_t threshold,Category current) noexcept {
    static const char* descriptions[11]={
        "Six cards of one color.",
        "Five cards of one color and one other card.",
        "Two different sets of three cards.",
        "Four cards of one color and two of another.",
        "Four cards of one color and two different single cards.",
        "Three different pairs.",
        "Six cards of different colors.",
        "Three of one color, two of another, and one other card.",
        "Three cards of one color and three different single cards.",
        "Two different pairs and two different single cards.",
        "One pair and four different single cards."
    };
    TableRow row={};
    if(index>=11 || height<=0) return row;
    row.category=static_cast<Category>(index+1);
    const PokerRule* rule=CurrentRuleFor(row.category);
    row.name=CategoryName(row.category);row.description=descriptions[index];
    row.score=rule->base_score;row.charge_percent=rule->charge_percent;
    // Scale the edges, then subtract, to avoid gaps at nonintegral scales.
    const int32_t top=TableTop()+static_cast<int32_t>(index)*13;
    row.bounds={101*height/600,top*height/600,
        219*height/600-101*height/600,(top+13)*height/600-top*height/600};
    row.dangerous=!IsQuestPoker() && IsSkullMarked(row.category,threshold);
    row.current=row.category==current;
    return row;
}
inline Category HitTable(int32_t x,int32_t y,int32_t height) noexcept {
    for(uint32_t i=0;i<11;++i) {
        const TableRect b=MakeTableRow(i,height,-1,Category::Invalid).bounds;
        if(b.width>0 && b.height>0 && x>=b.x && x<b.x+b.width && y>=b.y && y<b.y+b.height)
            return static_cast<Category>(i+1);
    }
    return Category::Invalid;
}
struct SkullClearTarget {
    Category primary;
    uint32_t affected_mask;
    int32_t x1200,y1200;
};
inline SkullClearTarget MakeSkullClearTarget(int32_t tier) noexcept {
    SkullClearTarget result={};
    if(tier<0 || tier>MaxDangerStep)return result;
    result.primary=static_cast<Category>(11-tier);
    result.affected_mask=1u<<(static_cast<uint32_t>(result.primary)-1);
    const auto row=MakeTableRow(static_cast<uint32_t>(result.primary)-1,1200,tier,Category::Invalid);
    result.x1200=row.bounds.x+12;result.y1200=row.bounds.y+12;
    return result;
}
}
