#include "render/geometry/RuntimeProjectileScreenPoint.h"
#include <cstddef>
#include <iostream>
#include <limits>
#include <type_traits>
using Query=RuntimeInputPixelPoint(*)(const RuntimeProjectileRenderActor&,const RuntimeInputPixelRect&,const RuntimeMapCamera&);
static_assert(std::is_same_v<decltype(&runtimeProjectileActorScreenPoint),Query>);
static_assert(sizeof(RuntimeInputPixelPoint)==8&&alignof(RuntimeInputPixelPoint)==4&&offsetof(RuntimeInputPixelPoint,x)==0&&offsetof(RuntimeInputPixelPoint,y)==4);
static_assert(sizeof(RuntimeInputPixelRect)==16&&alignof(RuntimeInputPixelRect)==4);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelPoint::x)>::digits==31);
static_assert(std::is_standard_layout_v<RuntimeInputPixelPoint>&&std::is_trivially_copyable_v<RuntimeInputPixelPoint>);
int main(){
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    RuntimeInputPixelPoint p;RuntimeInputPixelRect r;require(p.x==0&&p.y==0);require(r.left==0&&r.top==0&&r.right==0&&r.bottom==0);
    p={std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()};auto pc=p;p={};require(pc.x==std::numeric_limits<std::int32_t>::min()&&pc.y==std::numeric_limits<std::int32_t>::max());
    r={19,8,-7,-2};auto rc=r;r={};require(rc.left==19&&rc.right==-7&&rc.top==8&&rc.bottom==-2);require(p.x==0&&p.y==0);require(r.left==0&&r.bottom==0);
    std::cout<<"Standard projectile screen values: checks="<<checks<<" staticAssertions=5 signedBits=31 originalQueryBorrowing=1 nativeDependencies=0 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
