#pragma once

#include "world/values/RuntimeCellPoint.h"

#include <span>
#include <string>

struct WorldObjectInstance;

namespace concord::simulation::ai {

RuntimeCellPoint runtimeTeamHouseCenter(
    std::span<const WorldObjectInstance> objects,
    const std::string& owner,
    RuntimeCellPoint fallback);

RuntimeCellPoint runtimeTeamGatherPoint(
    RuntimeCellPoint center,
    RuntimeCellPoint ownBase,
    RuntimeCellPoint enemyBase,
    bool outsideEnemyBase,
    bool hasOwnBase,
    int distance);

} // namespace concord::simulation::ai
