#pragma once

#include "ArtDatabase.h"
#include "GameWorld.h"

#include <algorithm>
#include <cmath>
#include <string>

// Original NATBNK special layers: front/back rise, front/back reverse.
inline bool runtimeTankBunkerFrontLayer(const std::string& key) {
    return key == "SpecialAnim" || key == "SpecialAnimThree";
}

inline bool runtimeTankBunkerWaitingForFacing(const WorldObjectInstance& host) {
    return host.tankBunkerOccupantId >= 0 && !host.tankBunkerLowering
        && host.tankBunkerAnimationStartTick < 0;
}

inline bool runtimeTankBunkerLayerVisible(const WorldObjectInstance& host, const std::string& key) {
    if (host.tankBunkerAnimationStartTick < 0) return false;
    return host.tankBunkerLowering
        ? key == "SpecialAnimThree" || key == "SpecialAnimFour"
        : host.tankBunkerOccupantId >= 0 && (key == "SpecialAnim" || key == "SpecialAnimTwo");
}

inline int runtimeTankBunkerFrameDelay(const ArtType& animation, int tickHz) {
    const int delayAt30Hz = animation.rate > 0
        ? std::clamp((900 + animation.rate / 2) / animation.rate, 1, 36) : 6;
    return std::max(1, static_cast<int>(std::lround(delayAt30Hz * std::max(1, tickHz) / 30.0)));
}

inline int runtimeTankBunkerFrameCount(const ArtType& animation) {
    // The original damaged reverse definition uses Start=10, End=9,
    // LoopEnd=9. Reverse ranges describe a span, not absolute end indices.
    return animation.reverse ? std::max(1, animation.animationLoopEnd + 1)
        : std::max(1, animation.animationLoopEnd - animation.animationStart);
}

inline int runtimeTankBunkerFrame(const ArtType& animation, int age, int tickHz) {
    const int advance = std::min(runtimeTankBunkerFrameCount(animation) - 1,
        std::max(0, age) / runtimeTankBunkerFrameDelay(animation, tickHz));
    return animation.animationStart + (animation.reverse
        ? runtimeTankBunkerFrameCount(animation) - 1 - advance : advance);
}
