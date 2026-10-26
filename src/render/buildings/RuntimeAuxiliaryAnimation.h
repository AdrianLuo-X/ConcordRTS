#pragma once

#include "data/art/ArtDatabase.h"
#include "runtime/timing/RuntimeRuleTime.h"

#include <algorithm>
#include <cstdint>

namespace runtime_auxiliary_animation_detail {

struct FrameRange {
    int begin;
    int end;
};

inline FrameRange resolveRange(const ArtType* art, int available, bool mainFramesOnly) {
    const int authoredBegin = !art ? 0 : art->hasAnimationLoopStart
        ? art->animationLoopStart : art->hasAnimationStart ? art->animationStart : 0;
    const int begin = std::clamp(authoredBegin, 0, available - 1);

    // LoopEnd is an exclusive frame index. End retains the local art contract:
    // a frame count relative to Start, or the resolved begin when Start is absent.
    std::int64_t requestedEnd;
    if (art && art->hasAnimationLoopEnd) {
        requestedEnd = art->animationLoopEnd;
    } else if (art && art->hasAnimationEnd) {
        const int base = art->hasAnimationStart ? art->animationStart : begin;
        requestedEnd = static_cast<std::int64_t>(base) + std::max(1, art->animationEnd);
    } else {
        requestedEnd = mainFramesOnly && available % 2 == 0 ? available / 2 : available;
    }
    return {begin, static_cast<int>(std::clamp<std::int64_t>(requestedEnd, begin + 1, available))};
}

struct FrameCycle {
    FrameRange range;
    std::int64_t forwardCount;
    std::int64_t returnCount;

    int at(std::int64_t advance) const {
        const std::int64_t phase = advance % (forwardCount + returnCount);
        if (phase < forwardCount) return range.begin + static_cast<int>(phase);
        // The return leg excludes both endpoints, so each turning frame is
        // displayed once: [a,b,c,d] followed by [c,b], then the next cycle.
        return range.end - 2 - static_cast<int>(phase - forwardCount);
    }
};

} // namespace runtime_auxiliary_animation_detail

inline int runtimeAuxiliaryAnimationFrame(const ArtType* art, int frameCount,
    bool mainFramesOnlyFallback, int animationTick, int fixedTickHz) {
    if (frameCount <= 1) return 0;

    using namespace runtime_auxiliary_animation_detail;
    const auto range = resolveRange(art, frameCount, mainFramesOnlyFallback);
    const std::int64_t length = range.end - range.begin;
    const FrameCycle cycle{range, length, art && art->pingPong && length > 1 ? length - 2 : 0};
    const int tickInterval = std::max(1, runtimeRuleFramesToTicks(
        runtimeRuleAnimationDelayFrames(art ? art->rate : 0), fixedTickHz));
    // Retain the renderer's first displayed advance at zero/negative ticks.
    const std::int64_t advance = std::max(1, animationTick / tickInterval);
    return cycle.at(advance);
}
