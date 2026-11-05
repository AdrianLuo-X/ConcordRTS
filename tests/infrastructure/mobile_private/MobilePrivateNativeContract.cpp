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
using Query=bool(*)(const WorldObjectInstance&,const SDKPoint&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,RuntimeMapRenderCache&,RuntimeMobileSpriteBoundsProbe&);
static_assert(std::is_same_v<decltype(&verifyRuntimeMobileObjectSpriteBounds),Query>);
static_assert(sizeof(SDKPoint)==8&&alignof(SDKPoint)==4&&offsetof(SDKPoint,x)==0&&offsetof(SDKPoint,y)==4);
static_assert(std::numeric_limits<decltype(SDKPoint::x)>::digits==31);
static_assert(std::is_trivially_copyable_v<SDKPoint>);
static_assert(sizeof(RuntimeInputPixelPoint)==8&&alignof(RuntimeInputPixelPoint)==4);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelPoint::x)>::digits==31);
int main(){
 unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
 require(sizeof(SDKPoint)==8);require(alignof(SDKPoint)==4);require(sizeof(Query)==sizeof(void*));require(std::is_trivially_copyable_v<SDKPoint>);RuntimeInputPixelPoint p;require(p.x==0&&p.y==0);
 constexpr std::array<std::int32_t,7> v{std::numeric_limits<std::int32_t>::min(),-32768,-1,0,1,32767,std::numeric_limits<std::int32_t>::max()};unsigned points=0;
 for(auto x:v)for(auto y:v){SDKPoint n{x,y};auto a=runtimeRenderPlacementPixelPoint(n);auto b=runtimeRenderPlacementNativePoint(a);require(a.x==n.x&&a.y==n.y);require(b.x==n.x&&b.y==n.y);++points;}
 std::cout<<"Mobile diagnostic ABI: SDKpointSize="<<sizeof(SDKPoint)<<" pointAlign="<<alignof(SDKPoint)<<" offsets=0,4 queryPointerSize="<<sizeof(Query)<<" originalArguments=6\n";
 std::cout<<"Native mobile diagnostic values: checks="<<checks<<" points="<<points<<" staticAssertions=6 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
