#include "render/geometry/RuntimeVoxelMuzzleQuery.h"
#include <concepts>
#include <cstddef>
#include <iostream>
#include <limits>
#include <type_traits>
using Query=bool(*)(const WorldObjectInstance&,const RuntimeInputPixelPoint&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,RuntimeMapRenderCache&,int,int,int,int,RuntimeInputPixelPoint&,bool);
static_assert(std::is_same_v<decltype(&resolveRuntimeVoxelWeaponMuzzlePoint),Query>);
static_assert(sizeof(RuntimeInputPixelPoint)==8&&alignof(RuntimeInputPixelPoint)==4);
static_assert(offsetof(RuntimeInputPixelPoint,x)==0&&offsetof(RuntimeInputPixelPoint,y)==4);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelPoint::x)>::digits==31);
static_assert(std::is_trivially_copyable_v<RuntimeInputPixelPoint>);
static_assert(sizeof(int)==4&&std::numeric_limits<int>::digits==31&&sizeof(bool)==1);
static_assert(requires(const WorldObjectInstance& o,const RuntimeInputPixelPoint& p,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,RuntimeMapRenderCache& k,RuntimeInputPixelPoint& r){{resolveRuntimeVoxelWeaponMuzzlePoint(o,p,c,a,k,0,0,0,0,r)}->std::same_as<bool>;});
int main(){unsigned checks=0,failures=0;const auto require=[&](bool b){++checks;if(!b)++failures;};RuntimeInputPixelPoint a;require(a.x==0&&a.y==0);a={std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()};auto b=a;a={};require(b.x==std::numeric_limits<std::int32_t>::min());require(b.y==std::numeric_limits<std::int32_t>::max());require(a.x==0&&a.y==0);require(sizeof(Query)==sizeof(void*));require(std::numeric_limits<decltype(a.x)>::digits==31);std::cout<<"Standard voxel muzzle values: checks="<<checks<<" staticAssertions=7 signedBits=31 arguments=11 outputBeforeLastBool=1 lastBoolDefaultTrue=1 nativeDependencies=0 failures="<<failures<<" skipped=0\n";return failures?1:0;}
