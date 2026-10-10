#pragma once

#include "world/model/GameWorld.h"

// A sold structure leaves simulation immediately, but its existing saved
// buildup timer retains the reverse animation until the final frame boundary.
inline bool runtimeStructureIsSelling(const WorldObjectInstance& object) {
    return object.kind == WorldObjectKind::Structure && !object.alive
        && object.mission == "Sold" && object.buildAnimTicksTotal > 0
        && object.buildAnimTicksRemaining > 0;
}
