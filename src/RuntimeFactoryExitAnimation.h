#pragma once

#include "ArtDatabase.h"
#include "RuntimeRuleTime.h"

#include <algorithm>

// Keep the existing authored range convention. Explicit 0..0 (or 1..1
// damaged) strips are single poses, never an animation fallback.
inline int runtimeFactoryExitFrameDelay(const ArtType* art, int fixedTickHz = 15) {
    const int frames = art == nullptr || art->rate <= 0 ? 6
        : std::clamp((900 + art->rate / 2) / art->rate, 1, 36);
    return runtimeRuleFramesToTicks(frames, fixedTickHz);
}

inline int runtimeFactoryExitFirstFrame(const ArtType* art) {
    return art == nullptr ? 0 : std::max(0, art->hasAnimationLoopStart
        ? art->animationLoopStart : art->hasAnimationStart ? art->animationStart : 0);
}

inline int runtimeFactoryExitFrameCount(const ArtType* art) {
    const int first = runtimeFactoryExitFirstFrame(art);
    return art == nullptr ? 1 : std::clamp(art->hasAnimationLoopEnd
        ? art->animationLoopEnd - first
        : art->hasAnimationEnd ? art->animationEnd : 1, 1, 1024);
}

inline int runtimeFactoryExitTransitionTicks(const ArtType* art, int fixedTickHz = 15) {
    return runtimeFactoryExitFrameCount(art) * runtimeFactoryExitFrameDelay(art, fixedTickHz);
}

inline int runtimeFactoryAirExitTransitionTicks(const ArtType* factoryArt,
    const ArtDatabase* art, int fixedTickHz) {
    int ticks = runtimeRuleFramesToTicks(8, fixedTickHz);
    if (factoryArt != nullptr && art != nullptr) for (const auto& layer : factoryArt->auxiliaryAnimations) {
        std::string key = layer.key;
        for (char& c : key) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (key != "roofdeployinganim" && key != "underroofdooranim") continue;
        ticks = std::max(ticks, runtimeFactoryExitTransitionTicks(art->findArtType(layer.image), fixedTickHz));
        if (!layer.damagedImage.empty()) ticks = std::max(ticks,
            runtimeFactoryExitTransitionTicks(art->findArtType(layer.damagedImage), fixedTickHz));
    }
    return ticks;
}

inline int runtimeFactoryExitFrame(const ArtType* art, int mainFrames,
    int startTick, int endTick, int tick, int fixedTickHz = 15) {
    const int first = std::clamp(runtimeFactoryExitFirstFrame(art), 0, std::max(0, mainFrames - 1));
    const int last = std::min(std::max(0, mainFrames - 1), first + runtimeFactoryExitFrameCount(art) - 1);
    const int openingAge = std::max(0, tick - startTick);
    const int closingRemaining = std::max(0, endTick - tick - 1);
    // Native factory strips start with the open roof and end closed.
    // Run backward to open, hold the first pose, then run forward to close.
    return std::max(first, last - std::min(openingAge, closingRemaining) / runtimeFactoryExitFrameDelay(art, fixedTickHz));
}
