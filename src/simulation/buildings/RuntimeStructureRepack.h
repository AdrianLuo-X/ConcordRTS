#pragma once

#include "world/model/GameWorld.h"

// The yard remains a live structure until its saved reverse buildup finishes.
// Keep the state recognizable on the completion boundary after the timer clears.
inline bool runtimeStructureIsRepacking(const WorldObjectInstance& object) {
    return object.alive && object.kind == WorldObjectKind::Structure
        && object.mission == "Repacking";
}
