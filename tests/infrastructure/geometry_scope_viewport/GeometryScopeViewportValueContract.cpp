#include "render/geometry/RuntimeGeometryQueryScope.h"
#include <iostream>
#include <limits>
#include <type_traits>
using Scope=RuntimeGeometryQueryScope;
static_assert(std::is_constructible_v<Scope,const GameWorld&,const RuntimeInputPixelRect&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,RuntimeMapRenderCache&>);
static_assert(std::is_constructible_v<Scope,const GameWorld&,const RuntimeInputPixelRect&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,RuntimeMapRenderCache&,bool>);
static_assert(!std::is_copy_constructible_v<Scope>&&!std::is_move_constructible_v<Scope>);
static_assert(std::is_nothrow_destructible_v<Scope>);
static_assert(sizeof(Scope)==sizeof(void*)&&alignof(Scope)==alignof(void*));
static_assert(std::numeric_limits<decltype(RuntimeInputPixelRect::left)>::digits==31);
int main(){
    unsigned checks=0,failures=0;const auto require=[&](bool x){++checks;if(!x)++failures;};
    RuntimeInputPixelRect r;require(r.left==0&&r.top==0&&r.right==0&&r.bottom==0);
    r={std::numeric_limits<std::int32_t>::min(),7,std::numeric_limits<std::int32_t>::max(),-9};const auto copy=r;r={};
    require(copy.left==std::numeric_limits<std::int32_t>::min()&&copy.right==std::numeric_limits<std::int32_t>::max());require(copy.top==7&&copy.bottom==-9);
    require(r.left==0&&r.bottom==0);require(!std::is_default_constructible_v<Scope>);require(!std::is_copy_assignable_v<Scope>&&!std::is_move_assignable_v<Scope>);
    std::cout<<"Standard scope viewport contract: checks="<<checks<<" staticAssertions=6 signedBits=31 scopeOwnershipUnchanged=1 nativeDependencies=0 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
