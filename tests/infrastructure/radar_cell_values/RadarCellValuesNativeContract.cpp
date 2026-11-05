#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "render/core/RuntimeMapRenderer.h"
#include "ui/radar/RuntimeRadarCellColors.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
using OriginalColor=COLORREF;
#ifdef CONCORD_RADAR_CELL_STANDARD_VALUES
using ActualColor=RuntimeMapColorValue;
#else
using ActualColor=OriginalColor;
#endif
using Setter=void(RuntimeRadarCellColors::*)(int,ActualColor);
using Getter=bool(RuntimeRadarCellColors::*)(int,ActualColor&)const;
static_assert(std::is_same_v<decltype(&RuntimeRadarCellColors::setColor),Setter>);
static_assert(std::is_same_v<decltype(&RuntimeRadarCellColors::findColor),Getter>);
static_assert(sizeof(OriginalColor)==4&&alignof(OriginalColor)==4);
static_assert(sizeof(ActualColor)==4&&alignof(ActualColor)==4);
static_assert(std::numeric_limits<ActualColor>::digits==32);
static_assert(!std::is_copy_constructible_v<RuntimeRadarCellColors>);
int main(){
 unsigned checks=0,failures=0;const auto require=[&](bool v){++checks;if(!v)++failures;};
 RuntimeRadarCellColors actual(0,false),reference(0,true);
 require(sizeof(ActualColor)==sizeof(OriginalColor));require(alignof(ActualColor)==alignof(OriginalColor));
 require(actual.denseBytes()==512U*512U*(sizeof(OriginalColor)+sizeof(unsigned char)));require(reference.denseBytes()==0);
 constexpr std::array<std::uint32_t,10> colors{0U,1U,0xffU,0xff00U,0xff0000U,0xffffffU,0x01010203U,0x80000000U,0xabcdef12U,0xffffffffU};
 constexpr std::array<int,5> keys{0,511511,-1,512000,std::numeric_limits<int>::max()};unsigned pairs=0;
 for(int key:keys)for(auto v:colors){
  actual.setColor(key,static_cast<ActualColor>(v));reference.setColor(key,static_cast<ActualColor>(v));ActualColor a=0,b=0;
  require(actual.findColor(key,a)&&static_cast<std::uint32_t>(a)==v);require(reference.findColor(key,b)&&static_cast<std::uint32_t>(b)==v);++pairs;
 }
 ActualColor a=0x6e512913U,b=a;require(!actual.findColor(123,a)&&a==0x6e512913U);require(!reference.findColor(123,b)&&b==0x6e512913U);
 actual.markResource(42);reference.markResource(42);actual.setColor(42,0);reference.setColor(42,0);require(actual.isResource(42)&&reference.isResource(42));
 std::cout<<"Radar cell ABI: nativeColorSize="<<sizeof(OriginalColor)<<" colorAlign="<<alignof(ActualColor)<<" classSize="<<sizeof(RuntimeRadarCellColors)<<" classAlign="<<alignof(RuntimeRadarCellColors)<<" denseBytes="<<actual.denseBytes()<<" setterBytes="<<sizeof(Setter)<<" getterBytes="<<sizeof(Getter)<<"\n";
 std::cout<<"Native radar cell values: checks="<<checks<<" rawColors="<<colors.size()<<" keyPairs="<<pairs<<" staticAssertions=6 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
