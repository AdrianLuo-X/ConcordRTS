#include "render/core/RuntimeMapRenderer.h"
#include <array>
#include <iostream>
#include <type_traits>
using DrawSignature=RuntimeMapRenderStats(*)(RuntimeMapDrawTarget&,const RuntimeInputPixelRect&,const GameWorld&,const RuntimeMapRenderContext&,RuntimeMapCamera&,RuntimeMapRenderCache&);
static_assert(std::is_same_v<decltype(&drawRuntimeMap),DrawSignature>);
static_assert(std::is_abstract_v<RuntimeMapDrawTarget> && std::has_virtual_destructor_v<RuntimeMapDrawTarget>);
static_assert(!std::is_copy_constructible_v<RuntimeMapDrawTarget> && !std::is_move_constructible_v<RuntimeMapDrawTarget>);
static_assert(sizeof(RuntimeMapDrawingCapability)==4);
static_assert(std::is_same_v<decltype(RuntimeInputPixelRect::left),std::int32_t>);
class UnavailableTarget final:public RuntimeMapDrawTarget {
 unsigned& alive_;
public:
 explicit UnavailableTarget(unsigned& alive):alive_(alive){++alive_;}
 ~UnavailableTarget()override{--alive_;}
 RuntimeMapDrawingCapability capability()const noexcept override{return RuntimeMapDrawingCapability::Unavailable;}
 RuntimeMapRenderStats drawMap(const RuntimeInputPixelRect&,const GameWorld&,const RuntimeMapRenderContext&,RuntimeMapCamera&,RuntimeMapRenderCache&)override{return {};}
};
int main(){unsigned checks=0,failed=0;auto require=[&](bool v){++checks;if(!v)++failed;};
 constexpr std::array<std::size_t,10> actual{sizeof(RuntimeMapCamera),alignof(RuntimeMapCamera),sizeof(RuntimeMapRenderContext),alignof(RuntimeMapRenderContext),sizeof(RuntimeMapRenderCache),alignof(RuntimeMapRenderCache),sizeof(RuntimeMapRenderStats),alignof(RuntimeMapRenderStats),sizeof(RuntimeInputPixelRect),alignof(RuntimeInputPixelRect)};
#if defined(_MSC_VER) && defined(_WIN64)
#if _ITERATOR_DEBUG_LEVEL==0
 constexpr std::array<std::size_t,10> original{20,4,408,8,5584,8,2248,8,16,4};
#else
 constexpr std::array<std::size_t,10> original{20,4,456,8,6736,8,2432,8,16,4};
#endif
 for(std::size_t i=0;i<actual.size();++i)require(actual[i]==original[i]);
#else
 for(auto v:actual)require(v>0);
#endif
 unsigned alive=0;{UnavailableTarget target(alive);const RuntimeMapDrawTarget& port=target;require(alive==1);require(port.capability()==RuntimeMapDrawingCapability::Unavailable);}require(alive==0);
 RuntimeMapRenderStats stats;require(!stats.rendered&&!stats.lightingTintApplied&&stats.visibleTerrainCells==0&&stats.texturedTerrainCells==0);require(stats.behindMarkerFrame==-1&&stats.tankBunkerFrontFrame==-1);
 RuntimeInputPixelRect rect{};require(rect.left==0&&rect.top==0&&rect.right==0&&rect.bottom==0);
 std::cout<<"Map drawing standard ABI:";for(auto v:actual)std::cout<<' '<<v;std::cout<<"\nMap drawing standard: checks="<<checks<<" layoutValues=10 interfaceSignature=exact noModelConstruction=yes unavailableCapabilityOnly=yes failures="<<failed<<" skipped=0\n";return failed?1:0;}
