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
#ifdef CONCORD_INFANTRY_MUZZLE_STANDARD_VALUES
using Point=RuntimeInputPixelPoint;
#else
using Point=SDKPoint;
#endif
using Query=bool(*)(const WorldObjectInstance&,const Point&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,RuntimeMapRenderCache&,Point&);
static_assert(std::is_same_v<decltype(&resolveRuntimeInfantryWeaponMuzzlePoint),Query>);
static_assert(sizeof(Point)==8&&alignof(Point)==4&&offsetof(Point,x)==0&&offsetof(Point,y)==4);
static_assert(sizeof(RuntimeInputPixelPoint)==8&&alignof(RuntimeInputPixelPoint)==4);
static_assert(std::numeric_limits<decltype(SDKPoint::x)>::digits==31);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelPoint::x)>::digits==31);
static_assert(std::is_trivially_copyable_v<SDKPoint>&&std::is_trivially_copyable_v<RuntimeInputPixelPoint>);
int main(){unsigned checks=0,failures=0;const auto require=[&](bool b){++checks;if(!b)++failures;};require(sizeof(Point)==8);require(alignof(Point)==4);require(sizeof(Query)==sizeof(void*));require(std::is_trivially_copyable_v<Point>);Point p{};require(p.x==0&&p.y==0);
constexpr std::array<std::int32_t,7> v{std::numeric_limits<std::int32_t>::min(),-32768,-1,0,1,32767,std::numeric_limits<std::int32_t>::max()};unsigned points=0;
for(auto x:v)for(auto y:v){SDKPoint n{x,y};auto a=runtimeRenderPlacementPixelPoint(n);auto b=runtimeRenderPlacementNativePoint(a);require(a.x==n.x&&a.y==n.y);require(b.x==n.x&&b.y==n.y);++points;}
std::cout<<"Infantry muzzle ABI: pointSize=8 pointAlign=4 offsets=0,4 boolSize="<<sizeof(bool)<<" arguments=6 centerConstBorrow=1 resultMutableBorrow=1\n";
std::cout<<"Native infantry muzzle values: checks="<<checks<<" points="<<points<<" staticAssertions=6 failures="<<failures<<" skipped=0\n";return failures?1:0;}
