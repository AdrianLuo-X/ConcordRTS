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
using SDKRect=RECT;
#ifdef CONCORD_BUILDING_VISIBLE_STANDARD_VIEWPORT
using Viewport=RuntimeInputPixelRect;
#else
using Viewport=SDKRect;
#endif
using Output=std::unordered_map<int,std::vector<RuntimeSpriteHitSpan>>;
using Query=bool(*)(const GameWorld&,const Viewport&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,RuntimeMapRenderCache&,Output&);
static_assert(std::is_same_v<decltype(&resolveRuntimeBuildingVisibleHitSpans),Query>);
static_assert(sizeof(SDKRect)==16&&alignof(SDKRect)==4&&offsetof(SDKRect,left)==0&&offsetof(SDKRect,top)==4&&offsetof(SDKRect,right)==8&&offsetof(SDKRect,bottom)==12);
static_assert(sizeof(RuntimeInputPixelRect)==16&&alignof(RuntimeInputPixelRect)==4);
static_assert(std::numeric_limits<decltype(SDKRect::left)>::digits==31);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelRect::left)>::digits==31);
static_assert(sizeof(bool)==1);
int main(){
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    require(sizeof(SDKRect)==sizeof(RuntimeInputPixelRect));require(alignof(SDKRect)==alignof(RuntimeInputPixelRect));require(sizeof(Query)==sizeof(void*));require(sizeof(bool)==1);
    RuntimeInputPixelRect r;require(r.left==0&&r.top==0&&r.right==0&&r.bottom==0);
    constexpr std::array<std::int32_t,7> v{std::numeric_limits<std::int32_t>::min(),-32768,-1,0,1,32767,std::numeric_limits<std::int32_t>::max()};unsigned rects=0;
    for(auto l:v)for(auto t:v)for(auto right:v)for(auto b:v){SDKRect native{l,t,right,b};const auto value=runtimeRenderPlacementPixelRect(native);const auto back=runtimeRenderPlacementNativeRect(value);require(value.left==native.left&&value.top==native.top&&value.right==native.right&&value.bottom==native.bottom);require(back.left==native.left&&back.top==native.top&&back.right==native.right&&back.bottom==native.bottom);++rects;}
    std::cout<<"Visible spans viewport ABI: viewportSize="<<sizeof(Viewport)<<" viewportAlign="<<alignof(Viewport)<<" offsets=0,4,8,12 queryPointerSize="<<sizeof(Query)<<" boolSize="<<sizeof(bool)<<" viewportByConstRef=1 originalOutputReference=1\n";
    std::cout<<"Native visible viewport values: checks="<<checks<<" rects="<<rects<<" staticAssertions=6 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
