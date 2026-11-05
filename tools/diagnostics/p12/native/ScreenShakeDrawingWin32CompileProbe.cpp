#include "render/core/win32/RuntimeScreenShakeDrawingWin32.h"
#include <cstddef>
#include <type_traits>
using Standard=bool(*)(RuntimeScreenShakeDrawTarget&,const RuntimeInputPixelRect&,const RuntimeMapRenderContext&,RuntimeMapRenderCache&);
using Native=bool(*)(HDC,const RECT&,const RuntimeMapRenderContext&,RuntimeMapRenderCache&);
static_assert(std::is_same_v<decltype(static_cast<Standard>(&applyRuntimeScreenShake)),Standard>);
static_assert(std::is_same_v<decltype(static_cast<Native>(&applyRuntimeScreenShake)),Native>);
static_assert(std::is_abstract_v<RuntimeScreenShakeDrawTarget> && std::has_virtual_destructor_v<RuntimeScreenShakeDrawTarget>);
static_assert(!std::is_copy_constructible_v<RuntimeScreenShakeDrawTargetWin32> && !std::is_move_constructible_v<RuntimeScreenShakeDrawTargetWin32>);
static_assert(sizeof(LONG)==4&&std::is_signed_v<LONG> && sizeof(RuntimeInputPixelRect)==16&&alignof(RuntimeInputPixelRect)==alignof(RECT));
static_assert(offsetof(RuntimeInputPixelRect,right)==offsetof(RECT,right)&&offsetof(RuntimeInputPixelRect,bottom)==offsetof(RECT,bottom));
#if _ITERATOR_DEBUG_LEVEL==0
static_assert(sizeof(RuntimeMapCamera)==20&&sizeof(RuntimeMapRenderContext)==408&&sizeof(RuntimeMapRenderCache)==5584&&sizeof(RuntimeMapRenderStats)==2248);
#else
static_assert(sizeof(RuntimeMapCamera)==20&&sizeof(RuntimeMapRenderContext)==456&&sizeof(RuntimeMapRenderCache)==6736&&sizeof(RuntimeMapRenderStats)==2432);
#endif
// Actual private adapter instantiation only; no fake model or execution credit.
bool concordScreenShakeDrawingWin32CompileProbe(HDC dc,const RECT& r,
 const RuntimeMapRenderContext& context,RuntimeMapRenderCache& cache){return applyRuntimeScreenShakeWin32(dc,r,context,cache);}
