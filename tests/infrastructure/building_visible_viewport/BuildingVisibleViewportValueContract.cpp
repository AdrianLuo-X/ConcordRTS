#include "render/geometry/RuntimeBuildingVisibleHitSpans.h"
#include <cstddef>
#include <iostream>
#include <limits>
#include <type_traits>
using Output=std::unordered_map<int,std::vector<RuntimeSpriteHitSpan>>;
using Query=bool(*)(const GameWorld&,const RuntimeInputPixelRect&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,RuntimeMapRenderCache&,Output&);
static_assert(std::is_same_v<decltype(&resolveRuntimeBuildingVisibleHitSpans),Query>);
static_assert(sizeof(RuntimeInputPixelRect)==16&&alignof(RuntimeInputPixelRect)==4);
static_assert(offsetof(RuntimeInputPixelRect,left)==0&&offsetof(RuntimeInputPixelRect,top)==4&&offsetof(RuntimeInputPixelRect,right)==8&&offsetof(RuntimeInputPixelRect,bottom)==12);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelRect::left)>::digits==31);
static_assert(sizeof(bool)==1);
int main(){
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    RuntimeInputPixelRect r;require(r.left==0&&r.top==0&&r.right==0&&r.bottom==0);
    r={std::numeric_limits<std::int32_t>::min(),7,std::numeric_limits<std::int32_t>::max(),-9};const auto old=r;r={};
    require(old.left==std::numeric_limits<std::int32_t>::min());require(old.right==std::numeric_limits<std::int32_t>::max());require(old.top==7&&old.bottom==-9);require(r.left==0&&r.bottom==0);require(sizeof(Query)==sizeof(void*));
    std::cout<<"Standard visible viewport values: checks="<<checks<<" staticAssertions=5 signedBits=31 originalOutputReference=1 nativeDependencies=0 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
