#pragma once

#include "simulation/production/RuntimeFactoryExitAnimation.h"

// Shipyard ProductionAnim is a single successful-delivery cycle. Its authored
// rate uses the same clock as the other AnimType building layers.
inline int runtimeShipyardProductionFrameDelay(const ArtType* art, int hz) {
    return std::max(1, runtimeRuleFramesToTicks(runtimeRuleAnimationDelayFrames(art ? art->rate : 0), hz));
}

inline int runtimeShipyardProductionDuration(const ArtType& factory, const ArtDatabase& database, int hz) {
    int duration = 0;
    for (const auto& layer : factory.auxiliaryAnimations) {
        if (layer.key != "ProductionAnim") continue;
        for (const auto& image : {layer.image, layer.damagedImage}) {
            const auto* animation = database.findArtType(image);
            if (animation) duration = std::max(duration, runtimeFactoryExitFrameCount(animation)
                * runtimeShipyardProductionFrameDelay(animation, hz));
        }
    }
    return duration;
}

inline int runtimeShipyardProductionFrame(const ArtType* art, int mainFrames, int age, int hz) {
    const int first = std::clamp(runtimeFactoryExitFirstFrame(art), 0, std::max(0, mainFrames - 1));
    const int last = std::min(std::max(0, mainFrames - 1), first + runtimeFactoryExitFrameCount(art) - 1);
    return std::min(last, first + std::max(0, age) / runtimeShipyardProductionFrameDelay(art, hz));
}
