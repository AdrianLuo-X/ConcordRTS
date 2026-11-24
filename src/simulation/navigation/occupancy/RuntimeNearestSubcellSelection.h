#pragma once
#include "world/model/GameWorld.h"
#include <vector>
namespace concord::simulation::navigation {
// Borrows the caller's existing grounded local object. Its subCell retains
// the last visited slot, exactly as the original loop, even when no slot wins.
int selectNearestAvailableSubCell(const std::vector<int>& available,
    WorldObjectInstance& grounded, int x, int y, double desiredX, double desiredY);
}
