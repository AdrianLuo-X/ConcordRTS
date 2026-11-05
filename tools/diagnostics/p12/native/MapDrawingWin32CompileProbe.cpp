#include "render/core/win32/RuntimeMapDrawingWin32.h"
#include <cstdint>
#include <type_traits>
static_assert(sizeof(LONG)==sizeof(std::int32_t));
static_assert(sizeof(RECT)==sizeof(RuntimeInputPixelRect)&&alignof(RECT)==alignof(RuntimeInputPixelRect));
static_assert(!std::is_copy_constructible_v<RuntimeMapDrawTargetWin32>);
// Match the original same-profile native layout as well as the pure header.
#if defined(_MSC_VER) && defined(_WIN64)
#if _ITERATOR_DEBUG_LEVEL==0
static_assert(sizeof(RuntimeMapCamera)==20 && sizeof(RuntimeMapRenderContext)==408 && sizeof(RuntimeMapRenderCache)==5584 && sizeof(RuntimeMapRenderStats)==2248);
#else
static_assert(sizeof(RuntimeMapCamera)==20 && sizeof(RuntimeMapRenderContext)==456 && sizeof(RuntimeMapRenderCache)==6736 && sizeof(RuntimeMapRenderStats)==2432);
#endif
static_assert(alignof(RuntimeMapRenderContext)==8 && alignof(RuntimeMapRenderCache)==8 && alignof(RuntimeMapRenderStats)==8);
#endif
using NativeMap=RuntimeMapRenderStats(*)(HDC,const RECT&,const GameWorld&,const RuntimeMapRenderContext&,RuntimeMapCamera&,RuntimeMapRenderCache&);
static_assert(std::is_same_v<decltype(static_cast<NativeMap>(&drawRuntimeMap)),NativeMap>);
// Real adapter body instantiation in actual Runtime/Contract profiles. This is
// a compile/ABI object, not a native rendering execution or an alternate oracle.
RuntimeMapRenderStats concordMapDrawingWin32CompileProbe(HDC dc,const RECT& r,
 const GameWorld& w,const RuntimeMapRenderContext& c,RuntimeMapCamera& camera,RuntimeMapRenderCache& cache){return drawRuntimeMapWin32(dc,r,w,c,camera,cache);}
