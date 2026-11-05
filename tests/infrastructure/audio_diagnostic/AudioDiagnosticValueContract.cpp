#include "audio/backend/RuntimeVoiceAudio.h"
#include <limits>
#include <type_traits>
#include <utility>
#include <iostream>
using Code=decltype(std::declval<const RuntimeVoiceAudio&>().lastError());
static_assert(std::is_same_v<Code,std::int32_t>);
static_assert(sizeof(Code)==4 && std::is_signed_v<Code>);
static_assert(std::numeric_limits<Code>::digits==31);
static_assert(std::numeric_limits<Code>::min()==(-2147483647-1));
static_assert(std::numeric_limits<Code>::max()==2147483647);
static_assert(std::is_same_v<decltype(std::declval<const RuntimeVoiceAudio&>().ready()),bool>);
int main(){std::cout<<"Audio diagnostic value contract: assertions=6 failures=0 skipped=0 signedBits=32 nativeIncludes=0 deviceInstantiation=0\n";}
