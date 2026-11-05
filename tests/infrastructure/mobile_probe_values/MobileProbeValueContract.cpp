#include "render/geometry/RuntimeMobileSpriteBoundsProbe.h"
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>
static_assert(std::is_same_v<decltype(RuntimeMobileSpriteBoundsProbe::spriteBounds),RuntimeInputPixelRect>);
static_assert(std::is_same_v<decltype(RuntimeMobileSpriteBoundsProbe::renderedBounds),RuntimeInputPixelRect>);
static_assert(std::is_same_v<decltype(RuntimeMobileSpriteBoundsProbe::imageOrigin),RuntimeInputPixelPoint>);
static_assert(std::is_same_v<decltype(RuntimeMobileSpriteBoundsProbe::diagnostic),std::string>);
static_assert(std::is_same_v<decltype(RuntimeMobileSpriteBoundsProbe::pixels),std::vector<std::uint32_t>>);
static_assert(std::is_same_v<decltype(RuntimeMobileSpriteBoundsProbe::imageWidth),int> && std::is_same_v<decltype(RuntimeMobileSpriteBoundsProbe::imageHeight),int>);
int main(){
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    RuntimeMobileSpriteBoundsProbe p;
    require(p.spriteBounds.left==0&&p.spriteBounds.top==0&&p.spriteBounds.right==0&&p.spriteBounds.bottom==0);
    require(p.renderedBounds.left==0&&p.renderedBounds.top==0&&p.renderedBounds.right==0&&p.renderedBounds.bottom==0);
    require(p.imageOrigin.x==0&&p.imageOrigin.y==0&&p.imageWidth==0&&p.imageHeight==0);
    require(p.renderedPixels==0&&p.outsidePixels==0&&p.layerCount==0&&!p.resourceResolved&&!p.exactMatch&&!p.clipped&&p.diagnostic.empty()&&p.pixels.empty());
    p.spriteBounds={9,8,-7,-6};p.renderedBounds={-1,-2,0,0};
    p.imageOrigin={std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()};
    p.resourceResolved=true;p.clipped=true;p.layerCount=3;p.diagnostic="partial native result";p.pixels={0U,0xffffffffU,0x80112233U,0x00112233U};p.imageWidth=2;p.imageHeight=2;
    auto copy=p;copy.pixels[0]=17;copy.diagnostic="copy";
    require(p.pixels[0]==0U&&p.diagnostic=="partial native result"&&copy.pixels.data()!=p.pixels.data());
    auto moved=std::move(copy);require(moved.pixels[0]==17U&&moved.diagnostic=="copy"&&moved.resourceResolved&&moved.clipped&&!moved.exactMatch);
    require(moved.spriteBounds.left==9&&moved.spriteBounds.right==-7&&moved.imageOrigin.x==std::numeric_limits<std::int32_t>::min()&&moved.imageOrigin.y==std::numeric_limits<std::int32_t>::max());
    p={};require(p.diagnostic.empty()&&p.pixels.empty()&&!p.resourceResolved&&!p.clipped&&p.imageOrigin.x==0&&p.spriteBounds.right==0);
    std::cout<<"Standard mobile probe value: checks="<<checks<<" staticAssertions=6 fields=13 nativeDependencies=0 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
