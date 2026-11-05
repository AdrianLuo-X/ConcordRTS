#include "render/geometry/RuntimeStructureSpriteGeometry.h"
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>
static_assert(std::is_same_v<decltype(RuntimeStructureSpriteGeometry::bounds),RuntimeInputPixelRect>);
static_assert(std::is_same_v<decltype(RuntimeInputPixelRect::left),std::int32_t>);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelRect::left)>::digits==31);
static_assert(std::is_same_v<decltype(RuntimeStructureSpriteGeometry::hitSpans),std::vector<RuntimeSpriteHitSpan>>);
static_assert(std::is_same_v<decltype(RuntimeStructureSpriteGeometry::foregroundHitSpans),std::vector<RuntimeSpriteHitSpan>>);
static_assert(std::is_same_v<decltype(RuntimeStructureSpriteGeometry::layerCount),int>);
int main(){
    RuntimeStructureSpriteGeometry a;
    unsigned checks=0,failures=0;
    const auto require=[&](bool x){++checks;if(!x)++failures;};
    require(a.bounds.left==0&&a.bounds.top==0&&a.bounds.right==0&&a.bounds.bottom==0);
    require(a.layerCount==0&&a.hitSpans.empty()&&a.foregroundHitSpans.empty());
    a.bounds={std::numeric_limits<std::int32_t>::min(),-1,0,std::numeric_limits<std::int32_t>::max()};
    a.hitSpans={{-2,-3,1},{0,1,2}};a.foregroundHitSpans={{0,1,2}};a.layerCount=7;
    auto b=a;b.hitSpans[0].left=8;b.foregroundHitSpans.clear();
    require(a.hitSpans[0].left==-3&&a.foregroundHitSpans.size()==1);
    require(b.layerCount==7&&b.bounds.left==std::numeric_limits<std::int32_t>::min()&&b.bounds.bottom==std::numeric_limits<std::int32_t>::max());
    auto c=std::move(b);require(c.hitSpans[0].left==8&&c.hitSpans.size()==2&&c.foregroundHitSpans.empty());
    a={};require(a.bounds.bottom==0&&a.hitSpans.empty()&&a.foregroundHitSpans.empty()&&a.layerCount==0);
    std::cout<<"Standard structure geometry value: checks="<<checks<<" staticAssertions=6 fields=4 nativeDependencies=0 failures="<<failures<<" skipped=0\n";
    return failures?1:0;
}
