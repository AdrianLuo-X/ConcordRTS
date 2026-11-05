#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "OriginalNativeRenderPlacement.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
int main(){
    using Old=OriginalNativeRenderPlacement;using New=RuntimeObjectWorldRenderPlacement;
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    require(sizeof(Old)==sizeof(New));require(alignof(Old)==alignof(New));
    std::cout<<"Placement ABI: size="<<sizeof(New)<<" align="<<alignof(New)<<" fields=5\n";
#define MEMBER(x) require(offsetof(Old,x)==offsetof(New,x));require(sizeof(Old::x)==sizeof(New::x));std::cout<<"field "#x<<" offset="<<offsetof(New,x)<<" size="<<sizeof(New::x)<<"\n"
    MEMBER(center);MEMBER(sortPoint);MEMBER(bodyViewport);MEMBER(airborneForeground);MEMBER(factoryForegroundHostId);
#undef MEMBER
    Old old;New value;
    require(value.center.x==old.center.x&&value.center.y==old.center.y&&value.center.x==0);
    require(value.sortPoint.x==old.sortPoint.x&&value.sortPoint.y==old.sortPoint.y&&value.sortPoint.x==0);
    require(value.bodyViewport.left==old.bodyViewport.left&&value.bodyViewport.top==old.bodyViewport.top&&value.bodyViewport.right==old.bodyViewport.right&&value.bodyViewport.bottom==old.bodyViewport.bottom&&value.bodyViewport.left==0);
    require(!value.airborneForeground&&!old.airborneForeground);
    require(value.factoryForegroundHostId==-1&&old.factoryForegroundHostId==-1);
    constexpr std::array<std::int32_t,7> v{std::numeric_limits<std::int32_t>::min(),-32768,-1,0,1,32767,std::numeric_limits<std::int32_t>::max()};
    unsigned pointCases=0,rectCases=0;
    for(auto x:v)for(auto y:v){old.center={x,y};old.sortPoint={y,x};value.center={x,y};value.sortPoint={y,x};
        require(old.center.x==value.center.x&&old.center.y==value.center.y&&old.sortPoint.x==value.sortPoint.x&&old.sortPoint.y==value.sortPoint.y);++pointCases;}
    for(auto l:v)for(auto t:v)for(auto r:v)for(auto b:v){old.bodyViewport={l,t,r,b};value.bodyViewport={l,t,r,b};
        require(old.bodyViewport.left==value.bodyViewport.left&&old.bodyViewport.top==value.bodyViewport.top&&old.bodyViewport.right==value.bodyViewport.right&&old.bodyViewport.bottom==value.bodyViewport.bottom);++rectCases;}
    for(auto id:v){old.factoryForegroundHostId=id;value.factoryForegroundHostId=id;require(old.factoryForegroundHostId==value.factoryForegroundHostId);}
    old.airborneForeground=value.airborneForeground=true;require(old.airborneForeground==value.airborneForeground);
    static_assert(std::is_trivially_copyable_v<Old>&&std::is_trivially_copyable_v<New>);
    std::cout<<"Native placement value: checks="<<checks<<" pointCases="<<pointCases<<" rectCases="<<rectCases<<" fields=5 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
