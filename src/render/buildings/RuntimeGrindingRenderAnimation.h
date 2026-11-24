#pragma once

#include "data/art/ArtDatabase.h"

#include <algorithm>
#include <span>

namespace concord::render {

template<class Actor>
const Actor* selectRuntimeGrindingAnimation(int buildingId, std::span<const Actor> actors) {
    for (const auto& actor : actors) {
        if (actor.renderIntent == "grinderCycle" && actor.targetObjectId == buildingId
            && actor.remainingTicks > 0) return &actor;
    }
    return nullptr;
}

inline int selectRuntimeGrindingAnimationFrame(int frameIndex, int totalTicks, int remainingTicks,
    const ArtType* art, int hz, int (*frameDelayTicks)(const ArtType*, int)) {
    if (frameIndex >= 0) return frameIndex;
    if (!art) return 0;
    const int start = art->hasAnimationStart ? art->animationStart
        : (art->hasAnimationLoopStart ? art->animationLoopStart : 0);
    const int end = art->hasAnimationLoopEnd ? std::max(start + 1, art->animationLoopEnd) : start + 1;
    const int delay = frameDelayTicks(art, hz);
    return std::clamp(start + (totalTicks - remainingTicks) / delay, start, end - 1);
}

} // namespace concord::render
