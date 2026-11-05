#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "render/core/RuntimeMapRenderer.h"
#include "render/geometry/win32/RuntimeRenderPlacementWin32.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
using SDKPoint=POINT;
#ifdef CONCORD_STRUCTURE_QUERY_STANDARD_POINT
using Center=RuntimeInputPixelPoint;
#else
using Center=SDKPoint;
#endif
using Query=bool(*)(const WorldObjectInstance&,const Center&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,RuntimeMapRenderCache&,RuntimeStructureSpriteGeometry&);
static_assert(std::is_same_v<decltype(&resolveRuntimeStructureSpriteGeometry),Query>);
static_assert(sizeof(SDKPoint)==8&&alignof(SDKPoint)==4&&offsetof(SDKPoint,x)==0&&offsetof(SDKPoint,y)==4);
static_assert(sizeof(RuntimeInputPixelPoint)==8&&alignof(RuntimeInputPixelPoint)==4);
static_assert(std::numeric_limits<decltype(SDKPoint::x)>::digits==31);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelPoint::x)>::digits==31);
static_assert(sizeof(bool)==1);
static_assert(sizeof(RuntimeStructureSpriteGeometry)==72&&alignof(RuntimeStructureSpriteGeometry)==8);
int main(){
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    require(sizeof(SDKPoint)==sizeof(RuntimeInputPixelPoint));require(alignof(SDKPoint)==alignof(RuntimeInputPixelPoint));require(sizeof(Query)==sizeof(void*));require(sizeof(bool)==1);
    RuntimeInputPixelPoint p;require(p.x==0&&p.y==0);
    constexpr std::array<std::int32_t,7> v{std::numeric_limits<std::int32_t>::min(),-32768,-1,0,1,32767,std::numeric_limits<std::int32_t>::max()};unsigned points=0;
    for(auto x:v)for(auto y:v){SDKPoint native{x,y};const auto value=runtimeRenderPlacementPixelPoint(native);const auto back=runtimeRenderPlacementNativePoint(value);require(value.x==native.x&&value.y==native.y);require(back.x==native.x&&back.y==native.y);++points;}
    std::cout<<"Structure query point ABI: pointSize="<<sizeof(Center)<<" pointAlign="<<alignof(Center)<<" offsets=0,4 queryPointerSize="<<sizeof(Query)<<" boolSize="<<sizeof(bool)<<" centerByConstRef=1 originalOutputReference=1 geometrySize="<<sizeof(RuntimeStructureSpriteGeometry)<<" geometryAlign="<<alignof(RuntimeStructureSpriteGeometry)<<"\n";
    std::cout<<"Native structure query point values: checks="<<checks<<" points="<<points<<" staticAssertions=7 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
