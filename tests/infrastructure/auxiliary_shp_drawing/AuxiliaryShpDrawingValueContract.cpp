#include "render/core/RuntimeMapRenderer.h"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
using Signature=bool(*)(RuntimeAuxiliaryShpDrawTarget&,const RuntimeInputPixelRect&,const RuntimeInputPixelPoint&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,RuntimeMapRenderCache&,const std::string&,int,std::int32_t,std::int32_t,bool);
static_assert(std::is_same_v<decltype(&drawRuntimeAuxiliaryShpFrame),Signature>);
static_assert(std::is_abstract_v<RuntimeAuxiliaryShpDrawTarget> && std::has_virtual_destructor_v<RuntimeAuxiliaryShpDrawTarget>);
static_assert(!std::is_copy_constructible_v<RuntimeAuxiliaryShpDrawTarget> && !std::is_move_constructible_v<RuntimeAuxiliaryShpDrawTarget>);
static_assert(std::is_same_v<decltype(RuntimeInputPixelPoint::x),std::int32_t>);
int main(){unsigned checks=0,failed=0;auto require=[&](bool v){++checks;if(!v)++failed;};
 constexpr std::array<std::size_t,12> actual{sizeof(RuntimeMapCamera),alignof(RuntimeMapCamera),sizeof(RuntimeMapRenderContext),alignof(RuntimeMapRenderContext),sizeof(RuntimeMapRenderCache),alignof(RuntimeMapRenderCache),sizeof(RuntimeMapRenderStats),alignof(RuntimeMapRenderStats),sizeof(RuntimeInputPixelRect),alignof(RuntimeInputPixelRect),sizeof(RuntimeInputPixelPoint),alignof(RuntimeInputPixelPoint)};
#if defined(_MSC_VER) && defined(_WIN64)
#if _ITERATOR_DEBUG_LEVEL==0
 constexpr std::array<std::size_t,12> original{20,4,408,8,5584,8,2248,8,16,4,8,4};
#else
 constexpr std::array<std::size_t,12> original{20,4,456,8,6736,8,2432,8,16,4,8,4};
#endif
 for(std::size_t i=0;i<actual.size();++i)require(actual[i]==original[i]);
#else
 for(auto v:actual)require(v>0);
#endif
 RuntimeInputPixelRect rect{};RuntimeInputPixelPoint point{};
 require(rect.left==0&&rect.top==0&&rect.right==0&&rect.bottom==0);
 require(point.x==0&&point.y==0);
 std::cout<<"Auxiliary SHP standard ABI:";for(auto v:actual)std::cout<<' '<<v;
 std::cout<<"\nAuxiliary SHP standard: checks="<<checks<<" layouts=12 signature=exact pixelDefaults=zero noModelConstruction=yes nativeRenderingClaim=no failures="<<failed<<" skipped=0\n";return failed?1:0;
}
