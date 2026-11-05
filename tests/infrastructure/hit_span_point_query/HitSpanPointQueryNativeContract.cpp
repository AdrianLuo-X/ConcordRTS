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
using SpanInput=const std::vector<RuntimeSpriteHitSpan>&;
#ifdef CONCORD_HIT_SPAN_POINT_QUERY_STANDARD
using ExpectedQuery=bool(*)(SpanInput,RuntimeInputPixelPoint);
#else
using ExpectedQuery=bool(*)(SpanInput,POINT);
#endif
static_assert(std::is_same_v<decltype(&runtimeSpriteHitSpansContain),ExpectedQuery>);
static_assert(std::numeric_limits<decltype(POINT::x)>::digits==31);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelPoint::x)>::digits==31);
static_assert(std::is_trivially_copyable_v<RuntimeInputPixelPoint>);
int main(){
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    require(sizeof(POINT)==sizeof(RuntimeInputPixelPoint));require(alignof(POINT)==alignof(RuntimeInputPixelPoint));
    require(offsetof(POINT,x)==offsetof(RuntimeInputPixelPoint,x));require(offsetof(POINT,y)==offsetof(RuntimeInputPixelPoint,y));
    require(sizeof(POINT::x)==sizeof(RuntimeInputPixelPoint::x));require(sizeof(POINT::y)==sizeof(RuntimeInputPixelPoint::y));
    require(sizeof(ExpectedQuery)==sizeof(decltype(&runtimeSpriteHitSpansContain)));require(sizeof(bool)==1);
    constexpr std::array<std::int32_t,7> values{std::numeric_limits<std::int32_t>::min(),-32768,-1,0,1,32767,std::numeric_limits<std::int32_t>::max()};
    unsigned points=0;
    for(auto x:values)for(auto y:values){
        POINT native{x,y};const auto value=runtimeRenderPlacementPixelPoint(native);const auto back=runtimeRenderPlacementNativePoint(value);
        require(value.x==native.x&&value.y==native.y);require(back.x==native.x&&back.y==native.y);++points;
    }
    std::cout<<"Point query ABI: pointSize="<<sizeof(RuntimeInputPixelPoint)<<" align="<<alignof(RuntimeInputPixelPoint)<<" xOffset="<<offsetof(RuntimeInputPixelPoint,x)<<" yOffset="<<offsetof(RuntimeInputPixelPoint,y)<<" functionPointerSize="<<sizeof(ExpectedQuery)<<" boolSize="<<sizeof(bool)<<" borrowedConstSpans=1 pointByValue=1\n";
    std::cout<<"Native point query values: checks="<<checks<<" points="<<points<<" staticAssertions=4 failures="<<failures<<" skipped=0\n";
    return failures?1:0;
}
