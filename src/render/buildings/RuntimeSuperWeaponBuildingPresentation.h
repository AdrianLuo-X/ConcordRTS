#pragma once

#include "data/art/ArtDatabase.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"

#include <algorithm>
#include <span>
#include <string>

namespace concord::render {

inline bool isRuntimeSuperWeaponBuildingLayerKey(const std::string& key) {
    return key == "SuperAnim" || key == "SuperAnimTwo"
        || key == "SuperAnimThree" || key == "SuperAnimFour";
}

inline int selectRuntimeSuperWeaponBuildingFrame(const ArtType* art, int ageTicks, int tickHz,
    int (*delayTicks)(const ArtType*, int)) {
    if (!art) return 0;
    const int start = art->hasAnimationStart ? art->animationStart
        : art->hasAnimationLoopStart ? art->animationLoopStart : 0;
    const int step = std::max(0, ageTicks) / delayTicks(art, tickHz);
    if (art->hasAnimationEnd) {
        // NAMISL_F/H use 0..15, and the damaged halves use 16..31.
        // End is an absolute inclusive frame here, not a count to add to Start.
        return std::min(std::max(start, art->animationEnd), start + step);
    }
    const int loopStart = art->hasAnimationLoopStart ? art->animationLoopStart : start;
    const int span = art->hasAnimationLoopEnd ? std::max(1, art->animationLoopEnd - loopStart) : 1;
    return loopStart + step % span;
}

struct RuntimeSuperWeaponBuildingLayer {
    const ArtType::ActiveAnimation* layer = nullptr;
    int ageTicks = 0;
};

template<class LookupAnimation>
RuntimeSuperWeaponBuildingLayer selectRuntimeSuperWeaponBuildingLayer(int objectId,
    std::span<const ArtType::ActiveAnimation> layers, std::span<const SpecialMechanismRenderSnapshot> poses,
    int tickHz, bool canResolveAnimation, LookupAnimation&& lookupAnimation,
    int (*delayTicks)(const ArtType*, int)) {
    RuntimeSuperWeaponBuildingLayer result;
    if (std::none_of(layers.begin(), layers.end(),
            [](const auto& layer) { return isRuntimeSuperWeaponBuildingLayerKey(layer.key); })) return result;
    std::string key = "SuperAnim";
    for (const auto& pose : poses) {
        if (pose.kind == "superWeaponBuilding" && pose.objectId == objectId) {
            key = pose.resourceId;
            result.ageTicks = pose.progress;
            break;
        }
    }
    const auto find = [&](const std::string& name) -> const ArtType::ActiveAnimation* {
        for (const auto& layer : layers) if (layer.key == name) return &layer;
        return nullptr;
    };
    result.layer = find(key);
    if (!result.layer) { result.layer = find("SuperAnim"); result.ageTicks = 0; }
    if (result.layer && canResolveAnimation && (key == "SuperAnimTwo" || key == "SuperAnimFour")) {
        const auto& layer = *result.layer;
        const auto* animation = lookupAnimation(layer);
        if (animation && animation->hasAnimationEnd) {
            const int start = animation->hasAnimationStart ? animation->animationStart : 0;
            const int duration = std::max(1, animation->animationEnd - start + 1)
                * delayTicks(animation, tickHz);
            if (result.ageTicks >= duration) {
                const auto* next = find(key == "SuperAnimTwo" ? "SuperAnimThree" : "SuperAnim");
                if (next) { result.layer = next; result.ageTicks -= duration; }
            }
        }
    }
    return result;
}

} // namespace concord::render
