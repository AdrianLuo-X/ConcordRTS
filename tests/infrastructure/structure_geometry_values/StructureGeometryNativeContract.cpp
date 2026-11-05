#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "OriginalNativeStructureGeometry.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>
int main(){
    using Old=OriginalNativeStructureGeometry; using New=RuntimeStructureSpriteGeometry;
    unsigned checks=0,failures=0;
    const auto require=[&](bool x){++checks;if(!x)++failures;};
    require(sizeof(Old)==sizeof(New)); require(alignof(Old)==alignof(New));
    std::cout<<"Structure ABI: size="<<sizeof(New)<<" align="<<alignof(New)<<" fields=4\n";
#define MEMBER(x) require(offsetof(Old,x)==offsetof(New,x)); require(sizeof(Old::x)==sizeof(New::x)); std::cout<<"field "#x<<" offset="<<offsetof(New,x)<<" size="<<sizeof(New::x)<<"\n"
    MEMBER(bounds); MEMBER(hitSpans); MEMBER(foregroundHitSpans); MEMBER(layerCount);
#undef MEMBER
    Old old; New value;
    require(value.bounds.left==old.bounds.left && value.bounds.top==old.bounds.top && value.bounds.right==old.bounds.right && value.bounds.bottom==old.bounds.bottom);
    require(value.layerCount==0 && old.layerCount==0 && value.hitSpans.empty() && value.foregroundHitSpans.empty());
    constexpr std::array<std::int32_t,7> v{std::numeric_limits<std::int32_t>::min(),-32768,-1,0,1,32767,std::numeric_limits<std::int32_t>::max()};
    unsigned rectCases=0;
    for(auto l:v)for(auto t:v)for(auto r:v)for(auto b:v){
        old.bounds={l,t,r,b};value.bounds={l,t,r,b};
        require(value.bounds.left==old.bounds.left && value.bounds.top==old.bounds.top && value.bounds.right==old.bounds.right && value.bounds.bottom==old.bounds.bottom);
        ++rectCases;
    }
    value.hitSpans={{-5,-7,9},{0,0,1}};value.foregroundHitSpans={{7,-1,2}};value.layerCount=-1;
    New copy=value;copy.hitSpans[0].left=123;copy.foregroundHitSpans.clear();
    require(value.hitSpans[0].left==-7 && value.foregroundHitSpans.size()==1 && copy.hitSpans[0].left==123);
    New moved=std::move(copy);require(moved.hitSpans.size()==2 && moved.foregroundHitSpans.empty() && moved.layerCount==-1);
    require(value.bounds.left==v.back() && moved.bounds.right==v.back());
    value={};require(value.hitSpans.empty() && value.foregroundHitSpans.empty() && value.bounds.left==0 && value.layerCount==0);
    std::cout<<"Native structure geometry value: checks="<<checks<<" rectCases="<<rectCases<<" fields=4 ownership=preserved failures="<<failures<<" skipped=0\n";
    return failures?1:0;
}
