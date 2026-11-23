#pragma once
#include <stdint.h>
struct SixthGemSource {
    float x,y;int32_t color;
    float alternate_x,alternate_y;int32_t alternate_color;
    uint32_t dual;
};
extern "C" void __stdcall BJ3BeginSixthAcquisition(uint32_t board,uint32_t generation,
    const SixthGemSource* source,int32_t resolved_color) noexcept;
extern "C" uint32_t __stdcall BJ3SixthAcquisitionPending(void* board) noexcept;
