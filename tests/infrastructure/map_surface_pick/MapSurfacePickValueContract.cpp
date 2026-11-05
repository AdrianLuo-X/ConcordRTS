#include "render/geometry/RuntimeMapSurfacePick.h"
#include <cstddef>
#include <iostream>
#include <limits>
#include <type_traits>
using Query=bool(*)(const GameWorld&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,const RuntimeInputPixelRect&,RuntimeInputPixelPoint,RuntimeMapRenderCache&,RuntimeMapSurfaceCell&);
static_assert(std::is_same_v<decltype(&pickRuntimeMapSurfaceCell),Query>);
static_assert(sizeof(RuntimeMapSurfaceCell)==8&&alignof(RuntimeMapSurfaceCell)==4);
static_assert(offsetof(RuntimeMapSurfaceCell,x)==0&&offsetof(RuntimeMapSurfaceCell,y)==4);
static_assert(std::is_same_v<decltype(RuntimeMapSurfaceCell::x),std::int32_t>&&std::is_same_v<decltype(RuntimeMapSurfaceCell::y),std::int32_t>);
static_assert(!std::is_same_v<RuntimeMapSurfaceCell,RuntimeInputPixelPoint>);
static_assert(std::is_trivially_copyable_v<RuntimeMapSurfaceCell>);
int main(){unsigned checks=0,failures=0;auto require=[&](bool b){++checks;if(!b)++failures;};RuntimeMapSurfaceCell a;require(a.x==0&&a.y==0);a={std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()};auto b=a;a={};require(b.x==std::numeric_limits<std::int32_t>::min()&&b.y==std::numeric_limits<std::int32_t>::max());require(a.x==0&&a.y==0);require(sizeof(Query)==sizeof(void*));require(std::numeric_limits<decltype(b.x)>::digits==31);require(sizeof(RuntimeInputPixelPoint)==8&&sizeof(RuntimeInputPixelRect)==16);std::cout<<"Standard surface pick values: checks="<<checks<<" staticAssertions=6 signedBits=31 logicalCellDistinct=1 arguments=7 nativeDependencies=0 failures="<<failures<<" skipped=0\n";return failures?1:0;}
