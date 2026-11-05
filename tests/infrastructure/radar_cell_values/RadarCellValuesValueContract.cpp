#include "ui/radar/RuntimeRadarCellColors.h"
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
using Setter=void(RuntimeRadarCellColors::*)(int,RuntimeMapColorValue);
using Getter=bool(RuntimeRadarCellColors::*)(int,RuntimeMapColorValue&)const;
static_assert(sizeof(RuntimeMapColorValue)==4&&alignof(RuntimeMapColorValue)==4);
static_assert(std::numeric_limits<RuntimeMapColorValue>::digits==32);
static_assert(std::is_same_v<RuntimeMapColorValue,std::uint32_t>);
static_assert(std::is_same_v<decltype(&RuntimeRadarCellColors::setColor),Setter>);
static_assert(std::is_same_v<decltype(&RuntimeRadarCellColors::findColor),Getter>);
static_assert(!std::is_copy_constructible_v<RuntimeRadarCellColors>);
int main(){unsigned checks=0,failures=0;const auto require=[&](bool v){++checks;if(!v)++failures;};RuntimeRadarCellColors values(0,false);
 RuntimeMapColorValue out=0xabcdef12U;require(!values.findColor(99,out)&&out==0xabcdef12U);require(values.denseBytes()==1310720U);
 values.setColor(0,0xffffffffU);require(values.findColor(0,out)&&out==0xffffffffU);values.setColor(-1,0x80000000U);require(values.findColor(-1,out)&&out==0x80000000U);
 values.markResource(0);values.setColor(0,0);require(values.isResource(0));require(values.findColor(0,out)&&out==0);require(!values.isResource(99));require(values.fallbackColorCount()==1);
 std::cout<<"Radar cell standard values: checks="<<checks<<" staticAssertions=6 failures="<<failures<<" skipped=0\n";return failures?1:0;}
