#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "render/core/RuntimeMapRenderer.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
#include <unordered_map>
using OriginalColor=COLORREF;
using OriginalOwnerTable=std::unordered_map<std::string,OriginalColor>;
using OriginalTerrainTable=std::unordered_map<int,OriginalColor>;
using ActualOwnerTable=std::remove_const_t<std::remove_pointer_t<decltype(RuntimeMapRenderContext::ownerRemapColors)>>;
using ActualTerrainTable=decltype(RuntimeMapRenderCache::terrainColorsByTileIndex);
int main(){
    unsigned checks=0,failures=0;
    const auto require=[&](bool x){++checks;if(!x)++failures;};
    require(sizeof(OriginalColor)==4&&sizeof(ActualOwnerTable::mapped_type)==4&&sizeof(ActualTerrainTable::mapped_type)==4);
    require(std::numeric_limits<ActualOwnerTable::mapped_type>::digits==32&&std::numeric_limits<ActualTerrainTable::mapped_type>::digits==32);
    require(sizeof(OriginalOwnerTable)==sizeof(ActualOwnerTable)&&alignof(OriginalOwnerTable)==alignof(ActualOwnerTable));
    require(sizeof(OriginalTerrainTable)==sizeof(ActualTerrainTable)&&alignof(OriginalTerrainTable)==alignof(ActualTerrainTable));
    std::cout<<"Render colour ABI: cacheSize="<<sizeof(RuntimeMapRenderCache)<<" cacheAlign="<<alignof(RuntimeMapRenderCache)<<" terrainOffset="<<offsetof(RuntimeMapRenderCache,terrainColorsByTileIndex)<<" terrainBytes="<<sizeof(ActualTerrainTable)<<" contextSize="<<sizeof(RuntimeMapRenderContext)<<" contextAlign="<<alignof(RuntimeMapRenderContext)<<" ownerOffset="<<offsetof(RuntimeMapRenderContext,ownerRemapColors)<<" ownerBorrowBytes="<<sizeof(RuntimeMapRenderContext::ownerRemapColors)<<"\n";
    ActualOwnerTable actualOwner;ActualTerrainTable actualTerrain;OriginalOwnerTable oldOwner;OriginalTerrainTable oldTerrain;
    require(actualOwner.empty()&&actualTerrain.empty()&&oldOwner.empty()&&oldTerrain.empty());
    constexpr std::array<std::uint32_t,10> patterns{0U,1U,0x00ffffffU,0xffffffffU,0x80000000U,0x7fffffffU,0x01020304U,0xff112233U,0x00112233U,0xdeadbeefU};
    unsigned cases=0;
    for(auto value:patterns){
        const auto key=std::to_string(cases);const auto index=static_cast<int>(cases);
        actualOwner[key]=value;actualTerrain[index]=value;oldOwner[key]=value;oldTerrain[index]=value;
        require(actualOwner.at(key)==oldOwner.at(key)&&actualTerrain.at(index)==oldTerrain.at(index));
        require(actualOwner.at(key)==value&&actualTerrain.at(index)==value);
        require(GetRValue(actualOwner.at(key))==(value&255U));
        require(GetGValue(actualOwner.at(key))==((value>>8U)&255U));
        require(GetBValue(actualOwner.at(key))==((value>>16U)&255U));
        require(static_cast<std::uint32_t>(RGB(GetRValue(value),GetGValue(value),GetBValue(value)))==(value&0x00ffffffU));
        ++cases;
    }
    require(actualOwner.at("7")!=actualOwner.at("8")&&oldOwner.at("7")!=oldOwner.at("8"));
    const auto* borrowed=&actualOwner;auto copy=actualOwner;copy["7"]=123U;
    require(borrowed==&actualOwner&&borrowed->at("7")==0xff112233U&&copy.at("7")==123U);
    require(actualOwner.size()==oldOwner.size()&&actualTerrain.size()==oldTerrain.size());
    std::cout<<"Native render colour value: checks="<<checks<<" patterns="<<cases<<" full32=preserved rgbOrder=preserved ownership=preserved failures="<<failures<<" skipped=0\n";
    return failures?1:0;
}
