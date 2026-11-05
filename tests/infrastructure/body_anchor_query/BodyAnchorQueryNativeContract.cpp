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
using SDKRect=RECT;
#ifdef CONCORD_BODY_ANCHOR_STANDARD_VALUES
using ReturnPoint=RuntimeInputPixelPoint;
using Viewport=RuntimeInputPixelRect;
#else
using ReturnPoint=SDKPoint;
using Viewport=SDKRect;
#endif
using Query=ReturnPoint(*)(const WorldObjectInstance&,const Viewport&,const RuntimeMapRenderContext&,const RuntimeMapCamera&);
static_assert(std::is_same_v<decltype(&runtimeObjectBodyAnchorForRender),Query>);
static_assert(sizeof(SDKPoint)==8&&alignof(SDKPoint)==4&&offsetof(SDKPoint,x)==0&&offsetof(SDKPoint,y)==4);
static_assert(sizeof(SDKRect)==16&&alignof(SDKRect)==4);
static_assert(std::numeric_limits<decltype(SDKPoint::x)>::digits==31);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelPoint::x)>::digits==31);
static_assert(std::is_standard_layout_v<ReturnPoint>&&std::is_trivially_copyable_v<ReturnPoint>);
int main(){
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    require(sizeof(SDKPoint)==sizeof(RuntimeInputPixelPoint));require(alignof(SDKPoint)==alignof(RuntimeInputPixelPoint));
    require(sizeof(SDKRect)==sizeof(RuntimeInputPixelRect));require(alignof(SDKRect)==alignof(RuntimeInputPixelRect));require(sizeof(Query)==sizeof(void*));
    RuntimeInputPixelPoint p;RuntimeInputPixelRect r;require(p.x==0&&p.y==0);require(r.left==0&&r.top==0&&r.right==0&&r.bottom==0);
    constexpr std::array<std::int32_t,7> v{std::numeric_limits<std::int32_t>::min(),-32768,-1,0,1,32767,std::numeric_limits<std::int32_t>::max()};unsigned points=0,rects=0;
    for(auto x:v)for(auto y:v){SDKPoint native{x,y};const auto value=runtimeRenderPlacementPixelPoint(native);const auto back=runtimeRenderPlacementNativePoint(value);require(value.x==native.x&&value.y==native.y);require(back.x==native.x&&back.y==native.y);++points;}
    for(auto l:v)for(auto t:v)for(auto right:v)for(auto b:v){SDKRect native{l,t,right,b};const auto value=runtimeRenderPlacementPixelRect(native);const auto back=runtimeRenderPlacementNativeRect(value);require(value.left==native.left&&value.top==native.top&&value.right==native.right&&value.bottom==native.bottom);require(back.left==native.left&&back.top==native.top&&back.right==native.right&&back.bottom==native.bottom);++rects;}
    std::cout<<"Body anchor ABI: returnSize="<<sizeof(ReturnPoint)<<" returnAlign="<<alignof(ReturnPoint)<<" returnOffsets=0,4 viewportSize="<<sizeof(Viewport)<<" viewportAlign="<<alignof(Viewport)<<" queryPointerSize="<<sizeof(Query)<<" viewportByConstRef=1 pointByValue=1\n";
    std::cout<<"Native body anchor values: checks="<<checks<<" points="<<points<<" rects="<<rects<<" staticAssertions=6 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
