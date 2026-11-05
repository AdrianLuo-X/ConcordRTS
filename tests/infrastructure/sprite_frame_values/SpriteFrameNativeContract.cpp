#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "OriginalNativeFrameReference.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>

static_assert(sizeof(LONG)==4 && std::numeric_limits<LONG>::digits==31);
static_assert(sizeof(COLORREF)==4 && !std::is_signed_v<COLORREF>);
static_assert(sizeof(RuntimeShpFrameImage)==sizeof(OriginalNativeShpFrameImage));
static_assert(alignof(RuntimeShpFrameImage)==alignof(OriginalNativeShpFrameImage));
namespace {
unsigned checks=0,failures=0;
void require(bool condition){++checks;if(!condition)++failures;}
template<class Object,class Member> std::uintptr_t offset(const Object& object,const Member& member){
    return reinterpret_cast<std::uintptr_t>(&member)-reinterpret_cast<std::uintptr_t>(&object);
}
}
int main(){
    RuntimeShpFrameImage actual;
    OriginalNativeShpFrameImage original;
    std::cout<<"Frame ABI: size="<<sizeof(actual)<<" align="<<alignof(RuntimeShpFrameImage)<<" fields=62\n";
#define FIELD(name) require(sizeof(actual.name)==sizeof(original.name));require(offset(actual,actual.name)==offset(original,original.name));std::cout<<"field " #name " size="<<sizeof(actual.name)<<" offset="<<offset(actual,actual.name)<<'\n'
    FIELD(offsetX);
    FIELD(offsetY);
    FIELD(globalWidth);
    FIELD(globalHeight);
    FIELD(width);
    FIELD(height);
    FIELD(pixels);
    FIELD(rawColorIndices);
    FIELD(ownerRemapMask);
    FIELD(lightLevels);
    FIELD(normalIndices);
    FIELD(normalBrightnessLevels);
    FIELD(vplSections);
    FIELD(depthLevels);
    FIELD(sceneDepthBoundsValid);
    FIELD(sceneDepthBoundsTop);
    FIELD(sceneDepthBoundsBottom);
    FIELD(pixelMaterials);
    FIELD(paletteRemapStart);
    FIELD(paletteRemapEnd);
    FIELD(paletteRemapRangeFromHeader);
    FIELD(fallbackOwnerRemapCount);
    FIELD(zeroColorVoxelCount);
    FIELD(rasterPathStats);
    FIELD(rasterFunctionActuallyCalled);
    FIELD(rasterCacheKey);
    FIELD(rasterCacheHit);
    FIELD(vxlAlreadyVplResolved);
    FIELD(lightLevelsApplied);
    FIELD(remapLightApplied);
    FIELD(opaqueBoundsWidth);
    FIELD(opaqueBoundsHeight);
    FIELD(topFacePixelCount);
    FIELD(sideFacePixelCount);
    FIELD(lowerFacePixelCount);
    FIELD(pointSampleCount);
    FIELD(faceSampleCount);
    FIELD(exposedFaceCount);
    FIELD(sourcePath);
    FIELD(packedColorTablesValid);
    FIELD(packedColorPaletteSource);
    FIELD(packedColorRemap);
    FIELD(packedColorBrightnessLift);
    FIELD(packedColorGradeContrast);
    FIELD(packedColorGradeSaturation);
    FIELD(packedColorGradeValueLift);
    FIELD(packedColorExtraUnitLight);
    FIELD(packedColorRemapStart);
    FIELD(packedColorRemapEnd);
    FIELD(packedColorOwnerRemap);
    FIELD(packedColorGradeEnabled);
    FIELD(packedColorVxlPaletteLookup);
    FIELD(packedBaseColors);
    FIELD(packedRemapColors);
    FIELD(spriteOpaqueBoundsValid);
    FIELD(spriteOpaqueBounds);
    FIELD(spriteOpaqueSpansValid);
    FIELD(spriteOpaqueSpans);
    FIELD(shadowMaskSpanCache);
    FIELD(infantryContactScanned);
    FIELD(infantryContactValid);
    FIELD(infantryContact);
#undef FIELD
    require(actual.spriteOpaqueBounds.left==0 && actual.spriteOpaqueBounds.top==0 && actual.spriteOpaqueBounds.right==0 && actual.spriteOpaqueBounds.bottom==0);
    require(actual.infantryContact.x==0 && actual.infantryContact.y==0);
    require(actual.packedColorRemap==RGB(255,255,255));
    require(!actual.packedColorTablesValid && !actual.spriteOpaqueBoundsValid && !actual.spriteOpaqueSpansValid && !actual.infantryContactScanned && !actual.infantryContactValid);
    require(actual.packedColorPaletteSource==nullptr && actual.pixels.empty() && actual.sourcePath.empty());
    constexpr std::array<std::int32_t,7> values{std::numeric_limits<std::int32_t>::min(),-65536,-1,0,1,65536,std::numeric_limits<std::int32_t>::max()};
    unsigned rectCases=0,pointCases=0,colourCases=0;
    for(auto left:values)for(auto top:values)for(auto right:values)for(auto bottom:values){
        actual.spriteOpaqueBounds={left,top,right,bottom};original.spriteOpaqueBounds={left,top,right,bottom};
        require(std::memcmp(&actual.spriteOpaqueBounds,&original.spriteOpaqueBounds,sizeof(original.spriteOpaqueBounds))==0);++rectCases;
    }
    for(auto x:values)for(auto y:values){
        actual.infantryContact={x,y};original.infantryContact={x,y};
        require(std::memcmp(&actual.infantryContact,&original.infantryContact,sizeof(original.infantryContact))==0);++pointCases;
    }
    constexpr std::array<std::uint32_t,10> colours{0U,1U,0x00ffffffU,0xffffffffU,0x80000000U,0x7fffffffU,0x01020304U,0xff112233U,0x00112233U,0xdeadbeefU};
    for(auto packed:colours){
        actual.packedColorRemap=packed;original.packedColorRemap=static_cast<COLORREF>(packed);
        require(std::memcmp(&actual.packedColorRemap,&original.packedColorRemap,sizeof(original.packedColorRemap))==0);
        require(static_cast<std::uint32_t>(actual.packedColorRemap)==packed);
        require((actual.packedColorRemap==static_cast<COLORREF>(0x00112233U))==(packed==0x00112233U));++colourCases;
    }
    std::cout<<"Native sprite frame value: checks="<<checks<<" fields=62 rectCases="<<rectCases<<" pointCases="<<pointCases<<" colourPatterns="<<colourCases<<" full32CacheIdentity=preserved failures="<<failures<<" skipped=0\n";
    return failures?1:0;
}
