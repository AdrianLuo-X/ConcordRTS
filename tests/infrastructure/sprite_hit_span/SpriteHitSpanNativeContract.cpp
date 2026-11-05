#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "render/core/RuntimeMapRenderer.h"
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>
namespace {
// Frozen original Win32 declaration, solely an ABI/value reference.
struct OriginalWin32SpriteHitSpan { LONG y=0; LONG left=0; LONG right=0; };
static_assert(sizeof(LONG)==4 && std::numeric_limits<LONG>::digits==31);
static_assert(std::is_signed_v<decltype(RuntimeSpriteHitSpan::y)>);
static_assert(std::is_signed_v<decltype(RuntimeSpriteHitSpan::left)>);
static_assert(std::is_signed_v<decltype(RuntimeSpriteHitSpan::right)>);
static_assert(sizeof(RuntimeSpriteHitSpan)==12 && alignof(RuntimeSpriteHitSpan)==4);
static_assert(std::is_standard_layout_v<RuntimeSpriteHitSpan> && std::is_trivially_copyable_v<RuntimeSpriteHitSpan>);
static_assert(offsetof(RuntimeSpriteHitSpan,y)==0 && offsetof(RuntimeSpriteHitSpan,left)==4 && offsetof(RuntimeSpriteHitSpan,right)==8);
static_assert(sizeof(RuntimeSpriteHitSpan)==sizeof(OriginalWin32SpriteHitSpan));
}
int main(){
 constexpr std::array<std::int32_t,7> values{std::numeric_limits<std::int32_t>::min(),-65536,-1,0,1,65536,std::numeric_limits<std::int32_t>::max()};
 unsigned triples=0,failures=0;
 const RuntimeSpriteHitSpan zero{};const OriginalWin32SpriteHitSpan oldZero{};
 failures+=std::memcmp(&zero,&oldZero,sizeof(zero))!=0;
 for(auto y:values)for(auto left:values)for(auto right:values){
  const RuntimeSpriteHitSpan actual{y,left,right};const OriginalWin32SpriteHitSpan original{y,left,right};
  failures+=(actual.y!=original.y || actual.left!=original.left || actual.right!=original.right || std::memcmp(&actual,&original,sizeof(actual))!=0);++triples;
 }
 std::cout<<"Native sprite-hit-span value: triples="<<triples<<" defaultZero=1 bytes=12 align=4 offsets=0,4,8 signedBits=32 failures="<<failures<<" skipped=0 sameOriginalLongBytes=1 algorithms=unchanged\n";
 return failures?1:0;
}
