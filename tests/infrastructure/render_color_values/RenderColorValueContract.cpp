#include "render/core/RuntimeMapColorValues.h"
#include <array>
#include <iostream>
#include <limits>
#include <type_traits>
static_assert(std::is_same_v<RuntimeMapColorValue,std::uint32_t>);
static_assert(sizeof(RuntimeMapColorValue)==4);
static_assert(std::numeric_limits<RuntimeMapColorValue>::digits==32);
static_assert(std::is_same_v<RuntimeOwnerRemapColorTable::mapped_type,RuntimeMapColorValue>);
static_assert(std::is_same_v<RuntimeTerrainColorTable::mapped_type,RuntimeMapColorValue>);
static_assert(std::is_same_v<RuntimeOwnerRemapColorTable::key_type,std::string>&&std::is_same_v<RuntimeTerrainColorTable::key_type,int>);
int main(){
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    RuntimeOwnerRemapColorTable owner;RuntimeTerrainColorTable terrain;
    require(owner.empty()&&terrain.empty());
    constexpr std::array<std::uint32_t,10> patterns{0U,1U,0x00ffffffU,0xffffffffU,0x80000000U,0x7fffffffU,0x01020304U,0xff112233U,0x00112233U,0xdeadbeefU};
    unsigned index=0;
    for(auto value:patterns){auto key=std::to_string(index);owner[key]=value;terrain[static_cast<int>(index)]=value;
        require(owner.at(key)==value&&terrain.at(static_cast<int>(index))==value);
        require((owner.at(key)==0x00112233U)==(value==0x00112233U));++index;}
    require(owner.at("7")!=owner.at("8"));const auto* borrowed=&owner;auto copy=owner;copy["7"]=123U;
    require(borrowed==&owner&&borrowed->at("7")==0xff112233U&&copy.at("7")==123U);
    owner={};terrain={};require(owner.empty()&&terrain.empty());
    std::cout<<"Standard render colour value: checks="<<checks<<" staticAssertions=6 patterns=10 full32=preserved nativeDependencies=0 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
