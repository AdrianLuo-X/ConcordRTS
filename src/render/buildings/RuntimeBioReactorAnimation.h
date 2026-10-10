#pragma once

#include "world/model/GameWorld.h"
#include "data/rules/RulesDatabase.h"

#include <algorithm>
#include <limits>
#include <string_view>

// Match the existing powered-layer clock, including an open offline pause.
inline int runtimeBioReactorPoweredClockTick(const WorldObjectInstance& object, int tick) {
    const int source = !object.poweredOperational && object.powerAnimationPausedAtTick >= 0
        ? object.powerAnimationPausedAtTick : tick;
    return static_cast<int>(std::clamp(static_cast<long long>(source)
        - std::max(0, object.powerAnimationPausedTicks), 0LL,
        static_cast<long long>(std::numeric_limits<int>::max())));
}

// Only actual manifest changes establish a new raw and powered animation epoch.
inline void runtimeRestartBioReactorAnimation(WorldObjectInstance& object, int tick) {
    object.bioReactorAnimationStartTick = tick;
    object.bioReactorAnimationPoweredStartTick = runtimeBioReactorPoweredClockTick(object, tick);
}

inline int runtimeBioReactorAnimationTick(
    const BuildingType* building,
    const WorldObjectInstance& object,
    std::string_view lowerAnimationKey,
    int currentTick,
    bool powered = false,
    bool poweredEffect = false,
    int rawTick = -1
) {
    if (building == nullptr || !building->infantryAbsorb
        || object.bioReactorAnimationStartTick < 0
        || (lowerAnimationKey != "activeanim" && lowerAnimationKey != "activeanimtwo")) {
        return currentTick;
    }
    const int tick = rawTick >= 0 ? rawTick : currentTick;
    if (powered || poweredEffect) {
        // Legacy one-epoch records preserve their original powered-layer clock.
        if (object.bioReactorAnimationPoweredStartTick < 0) return currentTick;
        return std::max(0, runtimeBioReactorPoweredClockTick(object, tick)
            - object.bioReactorAnimationPoweredStartTick);
    }
    return std::max(0, tick - object.bioReactorAnimationStartTick);
}
