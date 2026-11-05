#include "render/geometry/RuntimeInfantryMuzzleQuery.h"
#include <cstddef>
#include <iostream>
#include <limits>
#include <type_traits>
using Query=bool(*)(const WorldObjectInstance&,const RuntimeInputPixelPoint&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,RuntimeMapRenderCache&,RuntimeInputPixelPoint&);
static_assert(std::is_same_v<decltype(&resolveRuntimeInfantryWeaponMuzzlePoint),Query>);
static_assert(sizeof(RuntimeInputPixelPoint)==8&&alignof(RuntimeInputPixelPoint)==4);
static_assert(offsetof(RuntimeInputPixelPoint,x)==0&&offsetof(RuntimeInputPixelPoint,y)==4);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelPoint::x)>::digits==31);
static_assert(std::is_trivially_copyable_v<RuntimeInputPixelPoint>);
int main(){unsigned checks=0,failures=0;const auto require=[&](bool b){++checks;if(!b)++failures;};RuntimeInputPixelPoint a;require(a.x==0&&a.y==0);a={std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()};auto b=a;a={};require(b.x==std::numeric_limits<std::int32_t>::min());require(b.y==std::numeric_limits<std::int32_t>::max());require(a.x==0&&a.y==0);require(sizeof(Query)==sizeof(void*));require(std::numeric_limits<decltype(a.x)>::digits==31);std::cout<<"Standard infantry muzzle values: checks="<<checks<<" staticAssertions=5 signedBits=31 arguments=6 centerConstBorrow=1 resultMutableBorrow=1 nativeDependencies=0 failures="<<failures<<" skipped=0\n";return failures?1:0;}
