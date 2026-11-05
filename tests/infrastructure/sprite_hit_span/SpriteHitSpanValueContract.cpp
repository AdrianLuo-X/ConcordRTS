#include "render/geometry/RuntimeSpriteHitSpan.h"
#include <cstddef>
#include <iostream>
#include <limits>
#include <type_traits>
static_assert(std::is_same_v<decltype(RuntimeSpriteHitSpan::y),std::int32_t>);
static_assert(std::is_same_v<decltype(RuntimeSpriteHitSpan::left),std::int32_t>);
static_assert(std::is_same_v<decltype(RuntimeSpriteHitSpan::right),std::int32_t>);
static_assert(sizeof(RuntimeSpriteHitSpan)==12);
static_assert(alignof(RuntimeSpriteHitSpan)==4);
static_assert(offsetof(RuntimeSpriteHitSpan,y)==0 && offsetof(RuntimeSpriteHitSpan,left)==4 && offsetof(RuntimeSpriteHitSpan,right)==8);
static_assert(std::is_standard_layout_v<RuntimeSpriteHitSpan> && std::is_trivially_copyable_v<RuntimeSpriteHitSpan>);
constexpr RuntimeSpriteHitSpan zero{};
static_assert(zero.y==0 && zero.left==0 && zero.right==0);
constexpr RuntimeSpriteHitSpan extremes{std::numeric_limits<std::int32_t>::min(),-1,std::numeric_limits<std::int32_t>::max()};
static_assert(extremes.y==(-2147483647-1) && extremes.left==-1);
static_assert(extremes.right==2147483647);
int main(){std::cout<<"Standard sprite-hit-span value: staticAssertions=10 signedBits=32 bytes=12 zeroDefault=1 nativeIncludes=0 failures=0 skipped=0 algorithms=unchanged\n";}
