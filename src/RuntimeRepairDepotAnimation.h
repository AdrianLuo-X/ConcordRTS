#pragma once

#include "ArtDatabase.h"
#include "RuntimeRuleTime.h"

#include <algorithm>
#include <limits>
#include <string>

// Repair buildings use three mutually exclusive SpecialAnim sequences:
// extend, work, and retract. Their End values are counts (including the
// damaged ranges), whereas the work sequence's LoopEnd is inclusive.
inline bool runtimeRepairDepotAnimationKey(const std::string& key) {
    return key == "SpecialAnim" || key == "SpecialAnimTwo"
        || key == "SpecialAnimThree";
}

inline int runtimeRepairDepotAnimationDelay(const ArtType* art, int tickHz) {
    return std::max(1, runtimeRuleFramesToTicks(
        runtimeRuleAnimationDelayFrames(art ? art->rate : 0), tickHz));
}

inline int runtimeRepairDepotAnimationStart(const ArtType* art, bool loop) {
    if (!art) return 0;
    return std::max(0, loop && art->hasAnimationLoopStart ? art->animationLoopStart
        : art->hasAnimationStart ? art->animationStart : 0);
}

inline int runtimeRepairDepotAnimationSpan(const ArtType* art, bool loop) {
    if (!art) return 1;
    const int start = runtimeRepairDepotAnimationStart(art, loop);
    return loop && art->hasAnimationLoopEnd
        ? std::max(1, art->animationLoopEnd - start + 1)
        : art->hasAnimationEnd ? std::max(1, art->animationEnd) : 1;
}

inline int runtimeRepairDepotAnimationDuration(const ArtType* art, int tickHz) {
    return static_cast<int>(std::min<long long>(std::numeric_limits<int>::max(),
        static_cast<long long>(runtimeRepairDepotAnimationSpan(art, false))
            * runtimeRepairDepotAnimationDelay(art, tickHz)));
}

inline int runtimeRepairDepotAnimationFrame(
    const ArtType* art, bool loop, int ageTicks, int tickHz
) {
    const int span = runtimeRepairDepotAnimationSpan(art, loop);
    const int step = std::max(0, ageTicks) / runtimeRepairDepotAnimationDelay(art, tickHz);
    return runtimeRepairDepotAnimationStart(art, loop)
        + (loop ? step % span : std::min(span - 1, step));
}

// Shipyard SpecialAnim sequences end before LoopEnd. The last damaged
// LoopEnd is also the first shadow frame in the stock SHP assets.
inline int runtimeNavalRepairAnimationSpan(const ArtType* art, bool loop) {
    if (!art || !art->hasAnimationLoopEnd)
        return runtimeRepairDepotAnimationSpan(art, loop);
    return std::max(1, art->animationLoopEnd - runtimeRepairDepotAnimationStart(art, loop));
}

inline int runtimeNavalRepairAnimationDuration(const ArtType* art, int tickHz) {
    return static_cast<int>(std::min<long long>(std::numeric_limits<int>::max(),
        static_cast<long long>(runtimeNavalRepairAnimationSpan(art, false))
            * runtimeRepairDepotAnimationDelay(art, tickHz)));
}

inline int runtimeNavalRepairAnimationFrame(
    const ArtType* art, bool loop, int ageTicks, int tickHz
) {
    const int span = runtimeNavalRepairAnimationSpan(art, loop);
    const int step = std::max(0, ageTicks) / runtimeRepairDepotAnimationDelay(art, tickHz);
    return runtimeRepairDepotAnimationStart(art, loop)
        + (loop ? step % span : std::min(span - 1, step));
}
