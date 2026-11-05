#include "render/geometry/RuntimeObjectWorldRenderPlacement.h"
#include <iostream>
#include <limits>
#include <type_traits>
using Query=RuntimeObjectWorldRenderPlacement(*)(const WorldObjectInstance&,const GameWorld&,const RuntimeInputPixelRect&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,RuntimeMapRenderCache&);
static_assert(std::is_same_v<decltype(&runtimeObjectWorldRenderPlacement),Query>);
static_assert(sizeof(RuntimeObjectWorldRenderPlacement)==40&&alignof(RuntimeObjectWorldRenderPlacement)==4);
static_assert(std::is_trivially_copyable_v<RuntimeObjectWorldRenderPlacement>);
static_assert(sizeof(RuntimeInputPixelRect)==16&&alignof(RuntimeInputPixelRect)==4);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelRect::left)>::digits==31);
int main(){
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    RuntimeObjectWorldRenderPlacement p;
    require(p.center.x==0&&p.center.y==0&&p.sortPoint.x==0&&p.sortPoint.y==0);
    require(p.bodyViewport.left==0&&p.bodyViewport.top==0&&p.bodyViewport.right==0&&p.bodyViewport.bottom==0);
    require(!p.airborneForeground&&p.factoryForegroundHostId==-1);
    RuntimeInputPixelRect r{std::numeric_limits<std::int32_t>::min(),9,std::numeric_limits<std::int32_t>::max(),-7};const auto copied=r;r={};
    require(copied.left==std::numeric_limits<std::int32_t>::min()&&copied.right==std::numeric_limits<std::int32_t>::max());require(copied.top==9&&copied.bottom==-7);require(r.left==0&&r.right==0);
    std::cout<<"Standard world placement viewport: checks="<<checks<<" staticAssertions=5 signedBits=31 resultContractUnchanged=1 nativeDependencies=0 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
