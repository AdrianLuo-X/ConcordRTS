#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/values/RuntimeColorValues.h"
#include <windows.h>
#include <cstdint>
#include <iostream>
#include <type_traits>
static_assert(sizeof(COLORREF)==4 && !std::is_signed_v<COLORREF>);
int main() {
    std::uint64_t checks=0,failures=0;
    const auto compare=[&](std::uint32_t packed) {
        const COLORREF original=static_cast<COLORREF>(packed);
        const auto current=RuntimeRgbColorValue::fromPackedRgb(packed);
        ++checks;if(current.red()!=GetRValue(original) || current.green()!=GetGValue(original) || current.blue()!=GetBValue(original))++failures;
        ++checks;if(current.packedRgb()!=RGB(GetRValue(original),GetGValue(original),GetBValue(original)))++failures;
    };
    for(std::uint32_t value=0;value<0x01000000U;++value)compare(value);
    for(std::uint32_t value=0;value<65536;++value) {
        compare(value|0x80000000U);compare(value|0xff000000U);compare((value<<8)|0x400000ffU);
    }
    std::cout<<"UI RGB native differential: checks="<<checks<<" failures="<<failures<<" allRgb24=16777216 upperFlags=196608\n";
    return failures==0?0:1;
}
