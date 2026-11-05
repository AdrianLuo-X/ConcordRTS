#include "render/geometry/RuntimeObjectWorldRenderPlacement.h"
#include <cstddef>
#include <iostream>
#include <limits>
#include <type_traits>
static_assert(std::is_same_v<decltype(RuntimeObjectWorldRenderPlacement::center),RuntimeInputPixelPoint>);
static_assert(std::is_same_v<decltype(RuntimeObjectWorldRenderPlacement::sortPoint),RuntimeInputPixelPoint>);
static_assert(std::is_same_v<decltype(RuntimeObjectWorldRenderPlacement::bodyViewport),RuntimeInputPixelRect>);
static_assert(sizeof(RuntimeObjectWorldRenderPlacement)==40 && alignof(RuntimeObjectWorldRenderPlacement)==4);
static_assert(std::is_standard_layout_v<RuntimeObjectWorldRenderPlacement> && std::is_trivially_copyable_v<RuntimeObjectWorldRenderPlacement>);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelPoint::x)>::digits==31);
int main(){
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    RuntimeObjectWorldRenderPlacement p;
    require(p.center.x==0&&p.center.y==0&&p.sortPoint.x==0&&p.sortPoint.y==0);
    require(p.bodyViewport.left==0&&p.bodyViewport.top==0&&p.bodyViewport.right==0&&p.bodyViewport.bottom==0);
    require(!p.airborneForeground&&p.factoryForegroundHostId==-1);
    p.center={std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()};p.sortPoint={-1,0};p.bodyViewport={9,8,-7,-6};p.airborneForeground=true;p.factoryForegroundHostId=31;
    auto copy=p;p={};require(copy.center.x==std::numeric_limits<std::int32_t>::min()&&copy.center.y==std::numeric_limits<std::int32_t>::max());
    require(copy.sortPoint.x==-1&&copy.sortPoint.y==0&&copy.bodyViewport.left==9&&copy.bodyViewport.right==-7);
    require(copy.airborneForeground&&copy.factoryForegroundHostId==31&&!p.airborneForeground&&p.factoryForegroundHostId==-1);
    std::cout<<"Standard placement value: checks="<<checks<<" staticAssertions=6 fields=5 nativeDependencies=0 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
