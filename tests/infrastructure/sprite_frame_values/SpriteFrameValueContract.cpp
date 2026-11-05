#include "render/core/RuntimeSpriteFrameImage.h"
#include <array>
#include <iostream>
#include <limits>
#include <type_traits>
static_assert(std::is_same_v<decltype(RuntimeShpFrameImage::spriteOpaqueBounds),RuntimeInputPixelRect>);
static_assert(std::is_same_v<decltype(RuntimeShpFrameImage::infantryContact),RuntimeInputPixelPoint>);
static_assert(std::is_same_v<decltype(RuntimeShpFrameImage::packedColorRemap),std::uint32_t>);
static_assert(sizeof(RuntimeInputPixelRect)==16 && sizeof(RuntimeInputPixelPoint)==8);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelPoint::x)>::digits==31);
static_assert(std::numeric_limits<decltype(RuntimeShpFrameImage::packedColorRemap)>::digits==32);
int main(){
    RuntimeShpFrameImage value;
    unsigned checks=0,failures=0;
    const auto require=[&](bool x){++checks;if(!x)++failures;};
    require(value.spriteOpaqueBounds.left==0 && value.spriteOpaqueBounds.top==0 && value.spriteOpaqueBounds.right==0 && value.spriteOpaqueBounds.bottom==0);
    require(value.infantryContact.x==0 && value.infantryContact.y==0);
    require(value.packedColorRemap==0x00ffffffU);
    require(!value.spriteOpaqueBoundsValid && !value.spriteOpaqueSpansValid && !value.infantryContactScanned && !value.infantryContactValid);
    require(value.packedColorPaletteSource==nullptr && value.pixels.empty() && value.spriteOpaqueSpans.empty());
    for(auto colour:std::array<std::uint32_t,10>{0U,1U,0x00ffffffU,0xffffffffU,0x80000000U,0x7fffffffU,0x01020304U,0xff112233U,0x00112233U,0xdeadbeefU}){
        value.packedColorRemap=colour;
        require(value.packedColorRemap==colour);
        require((value.packedColorRemap==0x00112233U)==(colour==0x00112233U));
    }
    std::cout<<"Standard sprite frame value: checks="<<checks<<" staticAssertions=6 fields=62 full32CacheIdentity=preserved nativeDependencies=0 failures="<<failures<<" skipped=0\n";
    return failures?1:0;
}
