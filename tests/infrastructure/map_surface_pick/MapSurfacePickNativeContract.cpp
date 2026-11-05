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
using SDKPoint=POINT;using SDKRect=RECT;
#ifdef CONCORD_MAP_SURFACE_STANDARD_VALUES
using PixelPoint=RuntimeInputPixelPoint;using PixelRect=RuntimeInputPixelRect;using Cell=RuntimeMapSurfaceCell;
#else
using PixelPoint=SDKPoint;using PixelRect=SDKRect;using Cell=SDKPoint;
#endif
using Query=bool(*)(const GameWorld&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,const PixelRect&,PixelPoint,RuntimeMapRenderCache&,Cell&);
static_assert(std::is_same_v<decltype(&pickRuntimeMapSurfaceCell),Query>);
static_assert(sizeof(PixelPoint)==8&&alignof(PixelPoint)==4&&offsetof(PixelPoint,x)==0&&offsetof(PixelPoint,y)==4);
static_assert(sizeof(PixelRect)==16&&alignof(PixelRect)==4&&offsetof(PixelRect,left)==0&&offsetof(PixelRect,top)==4&&offsetof(PixelRect,right)==8&&offsetof(PixelRect,bottom)==12);
static_assert(sizeof(Cell)==8&&alignof(Cell)==4&&offsetof(Cell,x)==0&&offsetof(Cell,y)==4);
static_assert(std::numeric_limits<decltype(Cell::x)>::digits==31&&std::is_signed_v<decltype(Cell::y)>);
static_assert(std::is_trivially_copyable_v<PixelPoint>&&std::is_trivially_copyable_v<PixelRect>&&std::is_trivially_copyable_v<Cell>);
static_assert(sizeof(bool)==1);
int main(){
 unsigned checks=0,failures=0;const auto require=[&](bool b){++checks;if(!b)++failures;};
 PixelPoint p{};PixelRect r{};Cell c{};require(p.x==0&&p.y==0);require(r.left==0&&r.top==0&&r.right==0&&r.bottom==0);require(c.x==0&&c.y==0);require(sizeof(Query)==sizeof(void*));
 constexpr std::array<std::int32_t,7> v{std::numeric_limits<std::int32_t>::min(),-32768,-1,0,1,32767,std::numeric_limits<std::int32_t>::max()};unsigned points=0,rects=0,cells=0;
 for(auto x:v)for(auto y:v){SDKPoint a{x,y};auto b=runtimeRenderPlacementPixelPoint(a);auto n=runtimeRenderPlacementNativePoint(b);require(a.x==b.x&&a.y==b.y);require(a.x==n.x&&a.y==n.y);++points;
 Cell cell{x,y};Cell copy=cell;require(copy.x==a.x&&copy.y==a.y);copy={y,x};require(cell.x==a.x&&cell.y==a.y);++cells;}
 for(auto l:v)for(auto t:v)for(auto rr:v)for(auto bb:v){SDKRect a{l,t,rr,bb};auto b=runtimeRenderPlacementPixelRect(a);auto n=runtimeRenderPlacementNativeRect(b);require(b.left==a.left&&b.top==a.top&&b.right==a.right&&b.bottom==a.bottom);require(n.left==a.left&&n.top==a.top&&n.right==a.right&&n.bottom==a.bottom);++rects;}
 std::cout<<"Surface pick ABI: point=8/4 rect=16/4 cell=8/4 bool=1 arguments=7 inputByValue=1 outputByReference=1\n";
 std::cout<<"Native surface pick values: checks="<<checks<<" points="<<points<<" rects="<<rects<<" cells="<<cells<<" staticAssertions=7 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
