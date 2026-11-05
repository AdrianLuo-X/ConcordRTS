#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "OriginalNativeMobileProbe.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>
int main(){
    using Old=OriginalNativeMobileProbe;using New=RuntimeMobileSpriteBoundsProbe;
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    require(sizeof(Old)==sizeof(New));require(alignof(Old)==alignof(New));
    std::cout<<"Mobile probe ABI: size="<<sizeof(New)<<" align="<<alignof(New)<<" fields=13\n";
#define MEMBER(x) require(offsetof(Old,x)==offsetof(New,x));require(sizeof(Old::x)==sizeof(New::x));std::cout<<"field "#x<<" offset="<<offsetof(New,x)<<" size="<<sizeof(New::x)<<"\n"
    MEMBER(spriteBounds);MEMBER(renderedBounds);MEMBER(renderedPixels);MEMBER(outsidePixels);MEMBER(layerCount);MEMBER(resourceResolved);MEMBER(exactMatch);MEMBER(clipped);MEMBER(diagnostic);MEMBER(pixels);MEMBER(imageWidth);MEMBER(imageHeight);MEMBER(imageOrigin);
#undef MEMBER
    Old old;New value;
    require(value.spriteBounds.left==old.spriteBounds.left&&value.spriteBounds.top==0&&value.spriteBounds.right==0&&value.spriteBounds.bottom==0);
    require(value.renderedBounds.left==old.renderedBounds.left&&value.renderedBounds.top==0&&value.renderedBounds.right==0&&value.renderedBounds.bottom==0);
    require(value.imageOrigin.x==old.imageOrigin.x&&value.imageOrigin.y==0);
    require(value.renderedPixels==0&&value.outsidePixels==0&&value.layerCount==0&&value.imageWidth==0&&value.imageHeight==0);
    require(!value.resourceResolved&&!value.exactMatch&&!value.clipped&&value.diagnostic.empty()&&value.pixels.empty());
    constexpr std::array<std::int32_t,7> v{std::numeric_limits<std::int32_t>::min(),-32768,-1,0,1,32767,std::numeric_limits<std::int32_t>::max()};
    unsigned rectCases=0,pointCases=0;
    for(auto l:v)for(auto t:v)for(auto r:v)for(auto b:v){old.spriteBounds={l,t,r,b};old.renderedBounds={r,b,l,t};value.spriteBounds={l,t,r,b};value.renderedBounds={r,b,l,t};
        require(value.spriteBounds.left==old.spriteBounds.left&&value.spriteBounds.top==old.spriteBounds.top&&value.spriteBounds.right==old.spriteBounds.right&&value.spriteBounds.bottom==old.spriteBounds.bottom&&value.renderedBounds.left==old.renderedBounds.left&&value.renderedBounds.top==old.renderedBounds.top&&value.renderedBounds.right==old.renderedBounds.right&&value.renderedBounds.bottom==old.renderedBounds.bottom);++rectCases;}
    for(auto x:v)for(auto y:v){old.imageOrigin={x,y};value.imageOrigin={x,y};require(old.imageOrigin.x==value.imageOrigin.x&&old.imageOrigin.y==value.imageOrigin.y);++pointCases;}
    for(int bits=0;bits!=8;++bits){value.resourceResolved=(bits&1)!=0;value.exactMatch=(bits&2)!=0;value.clipped=(bits&4)!=0;old.resourceResolved=value.resourceResolved;old.exactMatch=value.exactMatch;old.clipped=value.clipped;require(old.resourceResolved==value.resourceResolved&&old.exactMatch==value.exactMatch&&old.clipped==value.clipped);}
    value.diagnostic="owned partial-result diagnostic";value.pixels={0U,0xffffffffU,0x80112233U,0x00112233U};value.imageWidth=2;value.imageHeight=2;
    New copy=value;copy.pixels[0]=17U;copy.diagnostic="changed";
    require(value.pixels[0]==0U&&value.diagnostic=="owned partial-result diagnostic"&&copy.pixels.data()!=value.pixels.data());
    New moved=std::move(copy);require(moved.pixels.size()==4&&moved.pixels[0]==17U&&moved.diagnostic=="changed"&&moved.imageWidth==2&&moved.imageHeight==2);
    value={};require(value.pixels.empty()&&value.diagnostic.empty()&&!value.resourceResolved&&value.imageOrigin.x==0&&value.spriteBounds.right==0);
    std::cout<<"Native mobile probe value: checks="<<checks<<" rectCases="<<rectCases<<" pointCases="<<pointCases<<" fields=13 ownedPixelsAndDiagnostic=preserved failures="<<failures<<" skipped=0\n";return failures?1:0;
}
