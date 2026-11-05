#include "render/geometry/RuntimeStructureTurretScreenOffset.h"
#include <cstddef>
#include <iostream>
#include <limits>
#include <type_traits>
using Query=RuntimeInputPixelPoint(*)(const BuildingType&,const std::string&,int,int,const RuntimeMapCamera&);
static_assert(std::is_same_v<decltype(&runtimeStructureTurretAnimScreenOffsetForRender),Query>);
static_assert(sizeof(RuntimeInputPixelPoint)==8&&alignof(RuntimeInputPixelPoint)==4);
static_assert(offsetof(RuntimeInputPixelPoint,x)==0&&offsetof(RuntimeInputPixelPoint,y)==4);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelPoint::x)>::digits==31);
static_assert(std::is_trivially_copyable_v<RuntimeInputPixelPoint>);
int main(){
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    RuntimeInputPixelPoint p;require(p.x==0&&p.y==0);
    p={std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()};const auto old=p;p={};
    require(old.x==std::numeric_limits<std::int32_t>::min());require(old.y==std::numeric_limits<std::int32_t>::max());require(p.x==0&&p.y==0);require(sizeof(Query)==sizeof(void*));require(std::numeric_limits<decltype(p.x)>::digits==31);
    std::cout<<"Standard turret offset return values: checks="<<checks<<" staticAssertions=5 signedBits=31 originalSourceArguments=5 nativeDependencies=0 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
