#include "render/core/win32/RuntimeAuxiliaryShpDrawingWin32.h"
#include <cstddef>
#include <type_traits>
using Native=bool(*)(HDC,const RECT&,const POINT&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,RuntimeMapRenderCache&,const std::string&,int,int,int,bool);
static_assert(std::is_same_v<decltype(static_cast<Native>(&drawRuntimeAuxiliaryShpFrame)),Native>);
static_assert(sizeof(LONG)==sizeof(std::int32_t)&&sizeof(RECT)==sizeof(RuntimeInputPixelRect)&&alignof(RECT)==alignof(RuntimeInputPixelRect));
static_assert(sizeof(POINT)==sizeof(RuntimeInputPixelPoint)&&alignof(POINT)==alignof(RuntimeInputPixelPoint));
static_assert(offsetof(RECT,right)==offsetof(RuntimeInputPixelRect,right)&&offsetof(POINT,y)==offsetof(RuntimeInputPixelPoint,y));
static_assert(!std::is_copy_constructible_v<RuntimeAuxiliaryShpDrawTargetWin32>);
#if _ITERATOR_DEBUG_LEVEL==0
static_assert(sizeof(RuntimeMapCamera)==20&&sizeof(RuntimeMapRenderContext)==408&&sizeof(RuntimeMapRenderCache)==5584&&sizeof(RuntimeMapRenderStats)==2248);
#else
static_assert(sizeof(RuntimeMapCamera)==20&&sizeof(RuntimeMapRenderContext)==456&&sizeof(RuntimeMapRenderCache)==6736&&sizeof(RuntimeMapRenderStats)==2432);
#endif
// Actual adapter instantiation only, not a renderer-execution claim.
bool concordAuxiliaryShpDrawingWin32CompileProbe(HDC dc,const RECT& r,const POINT& p,
 const RuntimeMapRenderContext& c,const RuntimeMapCamera& camera,RuntimeMapRenderCache& cache,
 const std::string& id,int frame,int dx,int dy,bool main){return drawRuntimeAuxiliaryShpFrameWin32(dc,r,p,c,camera,cache,id,frame,dx,dy,main);}
