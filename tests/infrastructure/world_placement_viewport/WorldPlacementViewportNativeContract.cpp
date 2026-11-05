#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "render/core/RuntimeMapRenderer.h"
#include "render/geometry/win32/RuntimeRenderPlacementWin32.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
#ifdef CONCORD_WORLD_PLACEMENT_STANDARD_VIEWPORT
using Viewport=RuntimeInputPixelRect;
#else
using Viewport=RECT;
#endif
using Query=RuntimeObjectWorldRenderPlacement(*)(const WorldObjectInstance&,const GameWorld&,const Viewport&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,RuntimeMapRenderCache&);
static_assert(std::is_same_v<decltype(&runtimeObjectWorldRenderPlacement),Query>);
static_assert(sizeof(RuntimeObjectWorldRenderPlacement)==40&&alignof(RuntimeObjectWorldRenderPlacement)==4);
static_assert(std::is_trivially_copyable_v<RuntimeObjectWorldRenderPlacement>);
static_assert(std::numeric_limits<decltype(RECT::left)>::digits==31);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelRect::left)>::digits==31);
int main(){
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    require(sizeof(RECT)==sizeof(RuntimeInputPixelRect));require(alignof(RECT)==alignof(RuntimeInputPixelRect));require(sizeof(Query)==sizeof(void*));
    RuntimeObjectWorldRenderPlacement p;
    require(p.center.x==0&&p.center.y==0&&p.sortPoint.x==0&&p.sortPoint.y==0);
    require(p.bodyViewport.left==0&&p.bodyViewport.top==0&&p.bodyViewport.right==0&&p.bodyViewport.bottom==0);
    require(!p.airborneForeground&&p.factoryForegroundHostId==-1);
    constexpr std::array<std::int32_t,7> v{std::numeric_limits<std::int32_t>::min(),-32768,-1,0,1,32767,std::numeric_limits<std::int32_t>::max()};unsigned rects=0;
    for(auto l:v)for(auto t:v)for(auto r:v)for(auto b:v){RECT native{l,t,r,b};const auto value=runtimeRenderPlacementPixelRect(native);const auto back=runtimeRenderPlacementNativeRect(value);
        require(value.left==native.left&&value.top==native.top&&value.right==native.right&&value.bottom==native.bottom);
        require(back.left==native.left&&back.top==native.top&&back.right==native.right&&back.bottom==native.bottom);++rects;}
    std::cout<<"World placement viewport ABI: resultSize="<<sizeof(p)<<" resultAlign="<<alignof(decltype(p))<<" viewportSize="<<sizeof(RuntimeInputPixelRect)<<" viewportAlign="<<alignof(RuntimeInputPixelRect)<<" queryPointerSize="<<sizeof(Query)<<" viewportByConstRef=1 acceptedResultDefaults=1\n";
    std::cout<<"Native world placement viewport: checks="<<checks<<" rects="<<rects<<" staticAssertions=5 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
