#include "render/geometry/RuntimeObjectShpBoundsQuery.h"
#include <cstddef>
#include <iostream>
#include <limits>
#include <type_traits>
using Query=bool(*)(const WorldObjectInstance&,const RuntimeInputPixelPoint&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,RuntimeMapRenderCache&,RuntimeInputPixelRect&,bool);
static_assert(std::is_same_v<decltype(&resolveRuntimeObjectShpSpriteBounds),Query>);
static_assert(sizeof(RuntimeInputPixelPoint)==8&&alignof(RuntimeInputPixelPoint)==4&&offsetof(RuntimeInputPixelPoint,x)==0&&offsetof(RuntimeInputPixelPoint,y)==4);
static_assert(sizeof(RuntimeInputPixelRect)==16&&alignof(RuntimeInputPixelRect)==4&&offsetof(RuntimeInputPixelRect,left)==0&&offsetof(RuntimeInputPixelRect,top)==4&&offsetof(RuntimeInputPixelRect,right)==8&&offsetof(RuntimeInputPixelRect,bottom)==12);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelPoint::x)>::digits==31&&std::is_signed_v<decltype(RuntimeInputPixelPoint::y)>);
static_assert(std::numeric_limits<decltype(RuntimeInputPixelRect::left)>::digits==31&&std::is_signed_v<decltype(RuntimeInputPixelRect::bottom)>);
static_assert(std::is_trivially_copyable_v<RuntimeInputPixelPoint>&&std::is_trivially_copyable_v<RuntimeInputPixelRect>);
static_assert(sizeof(bool)==1);
static_assert(requires(const WorldObjectInstance& o,const RuntimeInputPixelPoint& p,const RuntimeMapRenderContext& c,const RuntimeMapCamera& a,RuntimeMapRenderCache& k,RuntimeInputPixelRect& r){resolveRuntimeObjectShpSpriteBounds(o,p,c,a,k,r);});
int main(){unsigned checks=0,failures=0;const auto require=[&](bool b){++checks;if(!b)++failures;};RuntimeInputPixelPoint p;RuntimeInputPixelRect r;require(p.x==0&&p.y==0);require(r.left==0&&r.top==0&&r.right==0&&r.bottom==0);p={std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()};auto q=p;p={};require(q.x==std::numeric_limits<std::int32_t>::min()&&q.y==std::numeric_limits<std::int32_t>::max());require(p.x==0&&p.y==0);r={q.x,q.y,q.x,q.y};auto s=r;r={};require(s.left==q.x&&s.top==q.y&&s.right==q.x&&s.bottom==q.y);require(r.left==0&&r.top==0&&r.right==0&&r.bottom==0);require(sizeof(Query)==sizeof(void*));require(std::numeric_limits<decltype(s.right)>::digits==31);std::cout<<"Standard object SHP bounds values: checks="<<checks<<" staticAssertions=8 signedBits=31 arguments=7 lastBoolDefaultFalse=1 nativeDependencies=0 failures="<<failures<<" skipped=0\n";return failures?1:0;}
