#include "render/geometry/RuntimeSpriteHitSpanQueries.h"
#include <cstddef>
#include <iostream>
#include <limits>
#include <type_traits>
using Query=bool(*)(const std::vector<RuntimeSpriteHitSpan>&,RuntimeInputPixelPoint);
static_assert(std::is_same_v<decltype(&runtimeSpriteHitSpansContain),Query>);
static_assert(sizeof(RuntimeInputPixelPoint)==8&&alignof(RuntimeInputPixelPoint)==4);
static_assert(offsetof(RuntimeInputPixelPoint,x)==0&&offsetof(RuntimeInputPixelPoint,y)==4);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelPoint::x)>::digits==31);
static_assert(std::is_trivially_copyable_v<RuntimeInputPixelPoint>);
int main(){
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    RuntimeInputPixelPoint p;require(p.x==0&&p.y==0);
    p={std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()};auto copy=p;p={};
    require(copy.x==std::numeric_limits<std::int32_t>::min());require(copy.y==std::numeric_limits<std::int32_t>::max());require(p.x==0&&p.y==0);
    require(sizeof(Query)==sizeof(void*));require(sizeof(bool)==1);
    std::cout<<"Standard point query contract: checks="<<checks<<" staticAssertions=5 signedBits=31 pointByValue=1 borrowedConstSpans=1 nativeDependencies=0 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
