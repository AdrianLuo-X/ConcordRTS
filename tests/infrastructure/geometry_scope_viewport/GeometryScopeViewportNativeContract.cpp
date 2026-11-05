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
using Scope=RuntimeGeometryQueryScope;
#ifdef CONCORD_GEOMETRY_SCOPE_STANDARD_VIEWPORT
using Viewport=RuntimeInputPixelRect;
#else
using Viewport=RECT;
#endif
static_assert(std::is_constructible_v<Scope,const GameWorld&,const Viewport&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,RuntimeMapRenderCache&>);
static_assert(std::is_constructible_v<Scope,const GameWorld&,const Viewport&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,RuntimeMapRenderCache&,bool>);
static_assert(std::numeric_limits<decltype(RECT::left)>::digits==31);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelRect::left)>::digits==31);
static_assert(!std::is_copy_constructible_v<Scope>&&!std::is_move_constructible_v<Scope>);
static_assert(std::is_nothrow_destructible_v<Scope>);
int main(){
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    require(sizeof(Scope)==sizeof(void*));require(alignof(Scope)==alignof(void*));
    require(!std::is_copy_constructible_v<Scope>);require(!std::is_copy_assignable_v<Scope>);require(!std::is_move_constructible_v<Scope>);require(!std::is_move_assignable_v<Scope>);
    require(!std::is_default_constructible_v<Scope>);require(std::is_destructible_v<Scope>&&std::is_nothrow_destructible_v<Scope>);
    require(sizeof(RECT)==sizeof(RuntimeInputPixelRect));require(alignof(RECT)==alignof(RuntimeInputPixelRect));
#define FIELD(x) require(offsetof(RECT,x)==offsetof(RuntimeInputPixelRect,x));require(sizeof(RECT::x)==sizeof(RuntimeInputPixelRect::x))
    FIELD(left);FIELD(top);FIELD(right);FIELD(bottom);
#undef FIELD
    constexpr std::array<std::int32_t,7> values{std::numeric_limits<std::int32_t>::min(),-32768,-1,0,1,32767,std::numeric_limits<std::int32_t>::max()};
    unsigned rects=0;
    for(auto l:values)for(auto t:values)for(auto r:values)for(auto b:values){
        RECT native{l,t,r,b};const auto p=runtimeRenderPlacementPixelRect(native);const auto back=runtimeRenderPlacementNativeRect(p);
        require(p.left==native.left&&p.top==native.top&&p.right==native.right&&p.bottom==native.bottom);
        require(back.left==native.left&&back.top==native.top&&back.right==native.right&&back.bottom==native.bottom);++rects;
    }
    RuntimeInputPixelRect zero;require(zero.left==0&&zero.top==0&&zero.right==0&&zero.bottom==0);require(sizeof(bool)==1);
    std::cout<<"Geometry scope ABI: scopeSize="<<sizeof(Scope)<<" align="<<alignof(Scope)<<" copy=0 move=0 defaultConstructor=0 nothrowDestructor=1 viewportBytes="<<sizeof(RuntimeInputPixelRect)<<" viewportAlign="<<alignof(RuntimeInputPixelRect)<<" boolBytes="<<sizeof(bool)<<" defaultBoolFiveArgs=1 explicitBoolSixArgs=1\n";
    std::cout<<"Native scope viewport values: checks="<<checks<<" rects="<<rects<<" staticAssertions=6 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
